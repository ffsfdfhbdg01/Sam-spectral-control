#include "ssc/engine/Engine.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::engine
{
filters::BandShape auditionShapeFor (const filters::BandShape& band) noexcept
{
    filters::BandShape a;
    a.frequencyHz = band.frequencyHz;
    a.slope = Slope::Db12;
    switch (band.shape)
    {
        case Shape::LowShelf:
        case Shape::LowCut:
            a.shape = Shape::HighCut; // listen to the region below the band frequency
            a.q = 0.70710678;
            break;
        case Shape::HighShelf:
        case Shape::HighCut:
            a.shape = Shape::LowCut; // listen to the region above the band frequency
            a.q = 0.70710678;
            break;
        default:
            a.shape = Shape::BandPass;
            a.q = std::clamp (band.q, 0.5, 20.0);
            a.slope = Slope::Db6;
            break;
    }
    return a;
}

void Engine::prepare (const EngineConfig& config)
{
    cfg = config;
    cfg.numChannels = std::clamp (cfg.numChannels, 1, kMaxChannels);
    cfg.maxBlockSize = std::max (1, cfg.maxBlockSize);

    dry.assign (static_cast<std::size_t> (cfg.numChannels), std::vector<double> (static_cast<std::size_t> (cfg.maxBlockSize), 0.0));
    wet = dry;
    dryPointers.resize (dry.size());
    wetPointers.resize (wet.size());
    for (std::size_t ch = 0; ch < dry.size(); ++ch)
    {
        dryPointers[ch] = dry[ch].data();
        wetPointers[ch] = wet[ch].data();
    }

    for (auto& band : bandProcessors)
        band.prepare (cfg.sampleRate, cfg.maxBlockSize);
    soloProcessor.prepare (cfg.sampleRate, cfg.maxBlockSize);
    solo = dry;
    soloPointers.resize (solo.size());
    for (std::size_t ch = 0; ch < solo.size(); ++ch)
        soloPointers[ch] = solo[ch].data();
    soloAmount.prepare (cfg.sampleRate, 0.01);
    soloAmount.setCurrentAndTarget (0.0);

    // The processing chain has no latency yet; the delay is sized for the M2–M4 maximum.
    latencySamples = 0;
    const int maxLatency = static_cast<int> (cfg.sampleRate * 0.25);
    dryDelay.prepare (cfg.numChannels, maxLatency, cfg.maxBlockSize);
    dryDelay.setDelay (latencySamples);

    outputGain.prepare (cfg.sampleRate, kRampSeconds);
    mix.prepare (cfg.sampleRate, kRampSeconds);
    bypass.prepare (cfg.sampleRate, kRampSeconds);
    outputGain.setCurrentAndTarget (1.0);
    mix.setCurrentAndTarget (1.0);
    bypass.setCurrentAndTarget (0.0);

    reset();
}

void Engine::reset() noexcept
{
    dryDelay.reset();
    for (auto& band : bandProcessors)
        band.reset();
    soloProcessor.reset();
}

template <typename Sample>
void Engine::process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params,
                      const BandSettingsArray& bands) noexcept
{
    numChannels = std::min (numChannels, cfg.numChannels);
    controlBlock = controlBlockFor (params.quality);

    const bool soloing = params.soloBand >= 0 && params.soloBand < kNumBands && bands[static_cast<std::size_t> (params.soloBand)].active;
    soloAmount.setTarget (soloing ? 1.0 : 0.0);
    if (soloing)
        lastSoloShape = auditionShapeFor (bands[static_cast<std::size_t> (params.soloBand)].shape);

    outputGain.setTarget (dbToGain (params.outputGainDb));
    mix.setTarget (std::clamp (params.mix, 0.0, 1.0));
    bypass.setTarget (params.bypass ? 1.0 : 0.0);

    Sample* chunkPointers[kMaxChannels];
    for (int offset = 0; offset < numSamples; offset += cfg.maxBlockSize)
    {
        const int n = std::min (cfg.maxBlockSize, numSamples - offset);
        for (int ch = 0; ch < numChannels; ++ch)
            chunkPointers[ch] = channels[ch] + offset;
        processChunk (chunkPointers, numChannels, n, bands);
    }
}

template <typename Sample>
void Engine::processChunk (Sample* const* channels, int numChannels, int numSamples, const BandSettingsArray& bands) noexcept
{
    const auto n = static_cast<std::size_t> (numSamples);

    // Non-finite input (a host or upstream bug) is replaced by silence so it cannot reach the
    // filter states or the dry path (PLT-5).
    bool inputSanitised = false;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t> (ch);
        for (std::size_t i = 0; i < n; ++i)
        {
            auto x = static_cast<double> (channels[ch][i]);
            if (! std::isfinite (x))
            {
                x = 0.0;
                inputSanitised = true;
            }
            dry[c][i] = x;
            wet[c][i] = x;
        }
    }
    if (inputSanitised)
        nonFiniteEvents.fetch_add (1, std::memory_order_relaxed);

    for (std::size_t b = 0; b < bandProcessors.size(); ++b)
        bandProcessors[b].process (bands[b], cfg.topology, wetPointers.data(), numChannels, numSamples, controlBlock);

    // Solo: crossfade the processed signal with the band's audition filter applied to the input.
    if (soloAmount.isSmoothing() || soloAmount.getCurrent() > 0.0)
    {
        BandSettings audition;
        audition.active = true;
        audition.shape = lastSoloShape;
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (dry[static_cast<std::size_t> (ch)].begin(), dry[static_cast<std::size_t> (ch)].begin() + numSamples,
                       solo[static_cast<std::size_t> (ch)].begin());
        soloProcessor.process (audition, cfg.topology, soloPointers.data(), numChannels, numSamples, controlBlock);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double s = soloAmount.next();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = static_cast<std::size_t> (ch);
                wet[c][i] = wet[c][i] * (1.0 - s) + solo[c][i] * s;
            }
        }
    }

    // A filter that produced a non-finite or runaway value is restarted and the block muted.
    double energy = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
        for (std::size_t i = 0; i < n; ++i)
            energy += wet[static_cast<std::size_t> (ch)][i] * wet[static_cast<std::size_t> (ch)][i];
    if (! std::isfinite (energy))
    {
        for (auto& band : bandProcessors)
            band.reset();
        soloProcessor.reset();
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill (wet[static_cast<std::size_t> (ch)].begin(), wet[static_cast<std::size_t> (ch)].begin() + numSamples, 0.0);
        nonFiniteEvents.fetch_add (1, std::memory_order_relaxed);
    }

    dryDelay.processInPlace (dryPointers.data(), numChannels, numSamples);

    const bool identity = ! outputGain.isSmoothing() && ! mix.isSmoothing() && ! bypass.isSmoothing() && outputGain.getCurrent() == 1.0
        && mix.getCurrent() == 1.0 && bypass.getCurrent() == 0.0;
    if (identity)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& w = wet[static_cast<std::size_t> (ch)];
            for (std::size_t i = 0; i < n; ++i)
                channels[ch][i] = static_cast<Sample> (w[i]);
        }
        return;
    }

    const bool fullyBypassed = ! bypass.isSmoothing() && bypass.getCurrent() == 1.0;
    if (fullyBypassed)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& d = dry[static_cast<std::size_t> (ch)];
            for (std::size_t i = 0; i < n; ++i)
                channels[ch][i] = static_cast<Sample> (d[i]);
        }
        outputGain.skip (numSamples);
        mix.skip (numSamples);
        return;
    }

    for (std::size_t i = 0; i < n; ++i)
    {
        const double g = outputGain.next();
        const double m = mix.next();
        const double b = bypass.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            const double d = dry[c][i];
            const double processed = (wet[c][i] * m + d * (1.0 - m)) * g;
            channels[ch][i] = static_cast<Sample> (processed * (1.0 - b) + d * b);
        }
    }
}

template void Engine::process<float> (float* const*, int, int, const GlobalParams&, const BandSettingsArray&) noexcept;
template void Engine::process<double> (double* const*, int, int, const GlobalParams&, const BandSettingsArray&) noexcept;
} // namespace ssc::engine
