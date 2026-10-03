#include "ssc/engine/Engine.h"

#include <algorithm>

#include "ssc/util/Math.h"

namespace ssc::engine
{
void Engine::prepare (const EngineConfig& config)
{
    cfg = config;
    cfg.numChannels = std::clamp (cfg.numChannels, 1, kMaxChannels);
    cfg.maxBlockSize = std::max (1, cfg.maxBlockSize);

    dry.assign (static_cast<std::size_t> (cfg.numChannels), std::vector<double> (static_cast<std::size_t> (cfg.maxBlockSize), 0.0));
    dryPointers.resize (dry.size());
    for (std::size_t ch = 0; ch < dry.size(); ++ch)
        dryPointers[ch] = dry[ch].data();

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
}

template <typename Sample>
void Engine::process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params) noexcept
{
    numChannels = std::min (numChannels, cfg.numChannels);

    outputGain.setTarget (dbToGain (params.outputGainDb));
    mix.setTarget (std::clamp (params.mix, 0.0, 1.0));
    bypass.setTarget (params.bypass ? 1.0 : 0.0);

    Sample* chunkPointers[kMaxChannels];
    for (int offset = 0; offset < numSamples; offset += cfg.maxBlockSize)
    {
        const int n = std::min (cfg.maxBlockSize, numSamples - offset);
        for (int ch = 0; ch < numChannels; ++ch)
            chunkPointers[ch] = channels[ch] + offset;
        processChunk (chunkPointers, numChannels, n);
    }
}

template <typename Sample>
void Engine::processChunk (Sample* const* channels, int numChannels, int numSamples) noexcept
{
    const auto n = static_cast<std::size_t> (numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        std::copy (channels[ch], channels[ch] + n, dry[static_cast<std::size_t> (ch)].begin());

    // Processing chain (bands, spectral engine, auto gain) is added in M2–M4.

    dryDelay.processInPlace (dryPointers.data(), numChannels, numSamples);

    const bool identity = ! outputGain.isSmoothing() && ! mix.isSmoothing() && ! bypass.isSmoothing()
                          && outputGain.getCurrent() == 1.0 && mix.getCurrent() == 1.0 && bypass.getCurrent() == 0.0;
    if (identity)
        return;

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
            const double d = dry[static_cast<std::size_t> (ch)][i];
            const double wet = static_cast<double> (channels[ch][i]);
            const double processed = (wet * m + d * (1.0 - m)) * g;
            channels[ch][i] = static_cast<Sample> (processed * (1.0 - b) + d * b);
        }
    }
}

template void Engine::process<float> (float* const*, int, int, const GlobalParams&) noexcept;
template void Engine::process<double> (double* const*, int, int, const GlobalParams&) noexcept;
} // namespace ssc::engine
