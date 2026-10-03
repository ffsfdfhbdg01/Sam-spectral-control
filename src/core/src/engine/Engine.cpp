#include "ssc/engine/Engine.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::engine
{
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

    for (auto& band : *bandProcessors)
        band.prepare (cfg.sampleRate, cfg.maxBlockSize);
    soloProcessor->prepare (cfg.sampleRate, cfg.maxBlockSize);
    solo = dry;
    soloPointers.resize (solo.size());
    for (std::size_t ch = 0; ch < solo.size(); ++ch)
        soloPointers[ch] = solo[ch].data();
    soloAmount.prepare (cfg.sampleRate, 0.01);
    soloAmount.setCurrentAndTarget (0.0);

    // Input history for detection and the Max Lookahead delay of the main path (DYN-8).
    maxLookaheadSamples = static_cast<int> (std::ceil (kMaxLookaheadSeconds * cfg.sampleRate));
    const int historySize = nextPowerOfTwo (maxLookaheadSamples + cfg.maxBlockSize + 1);
    history.assign (static_cast<std::size_t> (cfg.numChannels), std::vector<double> (static_cast<std::size_t> (historySize), 0.0));
    historyPointers.resize (history.size());
    for (std::size_t ch = 0; ch < history.size(); ++ch)
        historyPointers[ch] = history[ch].data();
    historyMask = historySize - 1;
    historyWrite = 0;
    lookaheadSamples = 0;

    // Latency so far is the lookahead only; the dry delay is sized for the M3–M4 maximum.
    spectralEngine->prepare (cfg.sampleRate, cfg.numChannels, cfg.maxBlockSize);
    autoGain->prepare (cfg.sampleRate, cfg.numChannels);
    const auto erbBands = static_cast<std::size_t> (autoGain->getBands().numBands());
    bandCurvesDb.assign (static_cast<std::size_t> (kNumBands), std::vector<double> (erbBands, 0.0));
    channelCurveDb.assign (erbBands, 0.0);
    curveActive.fill (false);
    curveLanes.fill ({});
    spectralEnabled = false;
    const int maxSpectralLatency = spectral::fftSizeFor (Quality::Max, cfg.sampleRate);
    soloDelay.prepare (cfg.numChannels, maxSpectralLatency, cfg.maxBlockSize);

    latencySamples.store (0, std::memory_order_relaxed);
    const int maxLatency = maxLookaheadSamples + maxSpectralLatency + static_cast<int> (cfg.sampleRate * 0.05);
    dryDelay.prepare (cfg.numChannels, maxLatency, cfg.maxBlockSize);
    dryDelay.setDelay (0);

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
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0);
    for (auto& band : *bandProcessors)
        band.reset();
    soloProcessor->reset();
    spectralEngine->reset();
    soloDelay.reset();
    autoGain->reset();
}

void Engine::updateAutoGainCurves (const BandSettingsArray& bands) noexcept
{
    // EQ magnitude at the ERB band centres per channel, for the Stable speed. Only changed bands
    // are redesigned, so automation of one band costs one design per block.
    const auto& erb = autoGain->getBands();
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto bi = static_cast<std::size_t> (b);
        const auto& band = bands[bi];
        const bool active = band.active && ! filters::isIdentity (band.shape);
        const auto lanes = active ? makeLanes (band.component, band.channelMask, band.includeLfe, cfg.topology) : LaneLayout {};
        if (active == curveActive[bi] && (! active || band.shape == curveShapes[bi]) && lanes == curveLanes[bi])
            continue;
        changed = true;
        curveActive[bi] = active;
        curveShapes[bi] = band.shape;
        curveLanes[bi] = lanes;
        auto& curve = bandCurvesDb[bi];
        std::fill (curve.begin(), curve.end(), 0.0);
        if (active)
        {
            const auto design = filters::designBand (band.shape, cfg.sampleRate);
            for (int e = 0; e < erb.numBands(); ++e)
                curve[static_cast<std::size_t> (e)] = gainToDb (std::abs (design.digitalResponse (erb.centreHz (e), cfg.sampleRate)));
        }
    }

    // Which channels the bands process; BS.1770 weights (surrounds +1.5 dB, LFE excluded).
    auto& covered = autoGainCovered;
    covered.fill (false);
    for (int b = 0; b < kNumBands; ++b)
        for (int l = 0; l < curveLanes[static_cast<std::size_t> (b)].numLanes; ++l)
        {
            const auto& lane = curveLanes[static_cast<std::size_t> (b)].lanes[static_cast<std::size_t> (l)];
            if (lane.a >= 0)
                covered[static_cast<std::size_t> (lane.a)] = true;
            if (lane.b >= 0)
                covered[static_cast<std::size_t> (lane.b)] = true;
        }
    for (int ch = 0; ch < cfg.numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t> (ch);
        float weight = 0.0f;
        if (! cfg.topology.isLfe (ch))
        {
            const auto group = cfg.topology.groups[c];
            weight = group == SpeakerGroup::SideSurround || group == SpeakerGroup::RearSurround ? 1.41f : 1.0f;
        }
        autoGainWeights[c] = weight;
        covered[c] = covered[c] && weight > 0.0f;
    }

    if (! changed)
        return;
    for (int ch = 0; ch < cfg.numChannels; ++ch)
    {
        std::fill (channelCurveDb.begin(), channelCurveDb.end(), 0.0);
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& lanes = curveLanes[static_cast<std::size_t> (b)];
            bool touches = false;
            for (int l = 0; l < lanes.numLanes; ++l)
                touches = touches || lanes.lanes[static_cast<std::size_t> (l)].a == ch || lanes.lanes[static_cast<std::size_t> (l)].b == ch;
            if (touches)
                for (std::size_t e = 0; e < channelCurveDb.size(); ++e)
                    channelCurveDb[e] += bandCurvesDb[static_cast<std::size_t> (b)][e];
        }
        autoGain->setStaticCurve (ch, channelCurveDb.data());
    }
}

float Engine::getBandGainChangeDb (int index) const noexcept
{
    const float dynamic = (*bandProcessors)[static_cast<std::size_t> (index)].getDynamicGainDb();
    return std::abs (dynamic) > 0.0f ? dynamic : spectralEngine->getBandGainDb (index);
}

void Engine::configure (const GlobalParams& params, const BandSettingsArray& bands) noexcept
{
    // Configuration changes set the latency (ADR-0011): Max Lookahead, and the spectral engine
    // while at least one band is in Spectral mode (its frame size follows Quality).
    const int lookahead =
        std::clamp (static_cast<int> (std::lround (0.001 * params.maxLookaheadMs * cfg.sampleRate)), 0, maxLookaheadSamples);
    const bool wantSpectral = std::any_of (bands.begin(), bands.end(), [] (const BandSettings& b) { return b.isSpectral(); });

    bool spectralChanged = wantSpectral != spectralEnabled;
    if (wantSpectral)
        spectralChanged = spectralEngine->setQuality (params.quality) || spectralChanged;
    if (spectralChanged)
        spectralEngine->reset();
    spectralEnabled = wantSpectral;

    const int spectralLatency = spectralEnabled ? spectralEngine->getLatencySamples() : 0;
    const int latency = lookahead + spectralLatency;
    if (lookahead != lookaheadSamples || latency != latencySamples.load (std::memory_order_relaxed))
    {
        lookaheadSamples = lookahead;
        dryDelay.setDelay (latency);
        soloDelay.setDelay (spectralLatency);
        latencySamples.store (latency, std::memory_order_relaxed);
    }
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

    configure (params, bands);
    autoGainSettings = { params.autoGain, params.autoGainWeighting, params.autoGainSpeed };
    if (params.autoGain)
        updateAutoGainCurves (bands);

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
            history[c][static_cast<std::size_t> ((historyWrite + static_cast<int> (i)) & historyMask)] = x;
        }
    }
    if (inputSanitised)
        nonFiniteEvents.fetch_add (1, std::memory_order_relaxed);

    // The main path is the input delayed by Max Lookahead (exactly the input when it is 0).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& h = history[static_cast<std::size_t> (ch)];
        auto& w = wet[static_cast<std::size_t> (ch)];
        for (std::size_t i = 0; i < n; ++i)
            w[i] = h[static_cast<std::size_t> ((historyWrite + static_cast<int> (i) - lookaheadSamples) & historyMask)];
    }

    // Solo auditions the (delayed) input, so it is taken before the bands run.
    const bool soloActive = soloAmount.isSmoothing() || soloAmount.getCurrent() > 0.0;
    if (soloActive)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (wet[static_cast<std::size_t> (ch)].begin(), wet[static_cast<std::size_t> (ch)].begin() + numSamples,
                       solo[static_cast<std::size_t> (ch)].begin());

    const DetectionInput detection { historyPointers.data(), historyMask, historyWrite, lookaheadSamples };
    for (std::size_t b = 0; b < bandProcessors->size(); ++b)
        (*bandProcessors)[b].process (bands[b], cfg.topology, wetPointers.data(), numChannels, numSamples, controlBlock, detection);
    historyWrite = (historyWrite + numSamples) & historyMask;

    if (spectralEnabled)
        spectralEngine->process (wetPointers.data(), numChannels, numSamples, bands, cfg.topology);

    // Dry path aligned with the processed path (latency).
    dryDelay.processInPlace (dryPointers.data(), numChannels, numSamples);

    // Global Auto Gain on the processed channels, before Solo, so the audition is unaffected (AG-5).
    if (autoGainSettings.enabled || std::abs (autoGain->getCorrectionDb()) > 0.0f)
        autoGain->process (dryPointers.data(), wetPointers.data(), numChannels, numSamples, autoGainSettings, autoGainWeights,
                           autoGainCovered);

    // Solo: crossfade the processed signal with the band's audition filter applied to the input.
    if (soloActive)
    {
        BandSettings audition;
        audition.active = true;
        audition.shape = lastSoloShape;
        soloProcessor->process (audition, cfg.topology, soloPointers.data(), numChannels, numSamples, controlBlock);
        if (spectralEnabled)
            soloDelay.processInPlace (soloPointers.data(), numChannels, numSamples);
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
        for (auto& band : *bandProcessors)
            band.reset();
        soloProcessor->reset();
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill (wet[static_cast<std::size_t> (ch)].begin(), wet[static_cast<std::size_t> (ch)].begin() + numSamples, 0.0);
        nonFiniteEvents.fetch_add (1, std::memory_order_relaxed);
    }

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
