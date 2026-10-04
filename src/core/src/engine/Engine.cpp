#include "ssc/engine/Engine.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::engine
{
namespace
{
    std::size_t oversamplerIndex (oversampling::FilterType type, int factor, Quality quality) noexcept
    {
        const int factorIndex = factor >= 8 ? 2 : (factor >= 4 ? 1 : 0);
        return static_cast<std::size_t> ((static_cast<int> (type) * 3 + factorIndex) * 4 + static_cast<int> (quality));
    }

    /** Low/High Cut with the Brickwall slope: linear phase in Natural Phase (ADR-0005). */
    bool isBrickwallCut (const BandSettings& band) noexcept
    {
        return band.active && band.shape.slope == Slope::Brickwall
            && (band.shape.shape == Shape::LowCut || band.shape.shape == Shape::HighCut);
    }

    void allocate (std::vector<std::vector<double>>& buffers, std::vector<double*>& pointers, std::size_t channels, std::size_t size)
    {
        buffers.assign (channels, std::vector<double> (size, 0.0));
        pointers.resize (channels);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = buffers[ch].data();
    }
} // namespace

void Engine::prepare (const EngineConfig& config)
{
    cfg = config;
    cfg.numChannels = std::clamp (cfg.numChannels, 1, kMaxChannels);
    cfg.maxBlockSize = std::max (1, cfg.maxBlockSize);
    chunkSize = std::min (cfg.maxBlockSize, kMaxChunk);
    const auto channels = static_cast<std::size_t> (cfg.numChannels);
    const auto chunk = static_cast<std::size_t> (chunkSize);

    allocate (dry, dryPointers, channels, chunk);
    allocate (wet, wetPointers, channels, chunk);
    allocate (solo, soloPointers, channels, chunk);

    maxFactor = maxOversamplingFor (cfg.sampleRate);
    for (auto& band : *bandProcessors)
        band.prepare (cfg.sampleRate, chunkSize * maxFactor);
    soloProcessor->prepare (cfg.sampleRate, chunkSize);
    soloAmount.prepare (cfg.sampleRate, 0.01);
    soloAmount.setCurrentAndTarget (0.0);
    soloWasActive = false;

    // Input history for detection and the Max Lookahead delay of the main path (DYN-8).
    maxLookaheadSamples = static_cast<int> (std::ceil (kMaxLookaheadSeconds * cfg.sampleRate));
    const int historySize = nextPowerOfTwo (maxLookaheadSamples + chunkSize + 1);
    history.assign (channels, std::vector<double> (static_cast<std::size_t> (historySize), 0.0));
    historyPointers.resize (channels);
    for (std::size_t ch = 0; ch < channels; ++ch)
        historyPointers[ch] = history[ch].data();
    historyMask = historySize - 1;
    historyWrite = 0;
    lookaheadSamples = 0;

    // Oversampling (ADR-0008): every factor allowed at this rate, both filter types and every
    // Quality are prepared now, so switching them in process() does not allocate.
    int maxOsLatency = 0;
    for (const auto type : { oversampling::FilterType::PolyphaseIir, oversampling::FilterType::LinearPhaseFir })
        for (int factor = 2; factor <= maxFactor; factor *= 2)
            for (const auto quality : { Quality::Eco, Quality::Normal, Quality::High, Quality::Max })
            {
                auto& os = (*oversamplers)[oversamplerIndex (type, factor, quality)];
                os.prepare (cfg.numChannels, chunkSize, factor, type, quality);
                maxOsLatency = std::max (maxOsLatency, os.getLatencySamples());
            }
    allocate (wetHigh, wetHighPointers, channels, chunk * static_cast<std::size_t> (maxFactor));
    allocate (osBypass, osBypassPointers, channels, chunk);
    osInput.assign (chunk * static_cast<std::size_t> (maxFactor), 0.0);
    osOutput.assign (chunk, 0.0);
    const int osHistorySize = maxFactor > 1 ? nextPowerOfTwo ((maxLookaheadSamples + chunkSize + 1) * maxFactor) : 1;
    osHistory.assign (channels, std::vector<double> (static_cast<std::size_t> (osHistorySize), 0.0));
    osHistoryPointers.resize (channels);
    for (std::size_t ch = 0; ch < channels; ++ch)
        osHistoryPointers[ch] = osHistory[ch].data();
    osHistoryMask = osHistorySize - 1;
    osBypassDelay.prepare (cfg.numChannels, maxLookaheadSamples + maxOsLatency, chunkSize);
    renderPadDelay.prepare (cfg.numChannels, maxOsLatency, chunkSize);
    renderPadActive = false;
    osRampLength = std::max (1, static_cast<int> (std::lround (0.005 * cfg.sampleRate)));
    osCovered.fill (false);

    corrector->prepare (cfg.sampleRate, cfg.numChannels, chunkSize);
    correctionActive = false;
    const int maxCorrection = phase::correctorDelayFor (Quality::Max, cfg.sampleRate);

    spectralEngine->prepare (cfg.sampleRate, cfg.numChannels, chunkSize);
    autoGain->prepare (cfg.sampleRate, cfg.numChannels);
    const auto erbBands = static_cast<std::size_t> (autoGain->getBands().numBands());
    bandCurvesDb.assign (static_cast<std::size_t> (kNumBands), std::vector<double> (erbBands, 0.0));
    channelCurveDb.assign (erbBands, 0.0);
    curveActive.fill (false);
    curveLanes.fill ({});
    spectralEnabled = false;
    const int maxSpectralLatency = spectral::fftSizeFor (Quality::Max, cfg.sampleRate);
    soloDelay.prepare (cfg.numChannels, maxOsLatency + maxCorrection + maxSpectralLatency, chunkSize);

    latencySamples.store (0, std::memory_order_relaxed);
    dryDelay.prepare (cfg.numChannels, maxLookaheadSamples + maxOsLatency + maxCorrection + maxSpectralLatency, chunkSize);
    dryDelay.setDelay (0);

    outputGain.prepare (cfg.sampleRate, kRampSeconds);
    mix.prepare (cfg.sampleRate, kRampSeconds);
    bypass.prepare (cfg.sampleRate, kRampSeconds);
    outputGain.setCurrentAndTarget (1.0);
    mix.setCurrentAndTarget (1.0);
    bypass.setCurrentAndTarget (0.0);

    // At least the capture window at 1× plus a chunk (low sample rates).
    meterPeriod = std::max (static_cast<int> (std::lround (kMeterPeriodSeconds * cfg.sampleRate)), BandCapture::lengthFor (1) + chunkSize);

    setOversampling (1, oversampling::FilterType::PolyphaseIir, Quality::Normal);
    reset();
    prepared = true;
}

void Engine::reset() noexcept
{
    dryDelay.reset();
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0);
    soloProcessor->reset();
    spectralEngine->reset();
    soloDelay.reset();
    autoGain->reset();
    osBypassDelay.reset();
    renderPadDelay.reset();
    samplePosition = 0;
    resetChain();
    attachRecorders (false);
    for (auto& r : *recorders)
        r.clear();
    meterCounter = 0;
}

void Engine::resetChain() noexcept
{
    for (auto& band : *bandProcessors)
        band.reset();
    if (oversampler != nullptr)
        oversampler->reset();
    for (auto& h : osHistory)
        std::fill (h.begin(), h.end(), 0.0);
    osHistoryWrite = 0;
    for (std::size_t c = 0; c < osRamp.size(); ++c)
        osRamp[c] = osCovered[c] ? osRampLength : 0;
    corrector->reset();
}

void Engine::setOversampling (int factor, oversampling::FilterType type, Quality quality) noexcept
{
    osFactor = factor;
    osType = type;
    osQuality = quality;
    oversampler = factor > 1 ? &(*oversamplers)[oversamplerIndex (type, factor, quality)] : nullptr;

    // Bands run at the oversampled rate (only a change of rate resets them).
    for (auto& band : *bandProcessors)
        band.setSampleRate (cfg.sampleRate * factor);
    if (oversampler != nullptr)
        oversampler->reset();
    for (auto& h : osHistory)
        std::fill (h.begin(), h.end(), 0.0);
    osHistoryWrite = 0;
    osBypassDelay.reset();
    for (std::size_t c = 0; c < osRamp.size(); ++c)
        osRamp[c] = osCovered[c] ? osRampLength : 0;
    for (auto& r : *recorders)
        r.recorded = 0; // recorded at the previous rate
}

void Engine::updateMetering() noexcept
{
    const bool wanted = meteringRequested.load (std::memory_order_relaxed);
    if (wanted == metering)
        return;
    metering = wanted;
    attachRecorders (false);
    for (auto& r : *recorders)
        r.clear();
    meterCounter = 0;
}

void Engine::attachRecorders (bool attach) noexcept
{
    recording = attach;
    for (std::size_t b = 0; b < recorders->size(); ++b)
    {
        auto& r = (*recorders)[b];
        if (attach)
            r.startWindow();
        (*bandProcessors)[b].setRecorder (attach ? &r : nullptr);
    }
    meterEnergy = 0.0;
    meterWindowSamples = 0;
}

template <typename Sample>
void Engine::meterChunk (const Sample* const* channels, int numChannels, int numSamples) noexcept
{
    if (recording)
    {
        double energy = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto v = static_cast<double> (channels[ch][i]);
                energy += v * v;
            }
        meterEnergy += energy;
        meterWindowSamples += numSamples;
    }
    meterCounter += numSamples;
    if (meterCounter >= meterPeriod)
        publishDistortion();
}

void Engine::publishDistortion() noexcept
{
    // Research §13c: what reaches the meters per period. The products of the bands pass Auto Gain,
    // Mix, Output and Bypass on their way to the output.
    auto& out = captures->writeBuffer();
    out.baseRate = cfg.sampleRate;
    out.osFactor = osFactor;
    out.osType = osType;
    out.osQuality = osQuality;
    out.periodSeconds = meterCounter / cfg.sampleRate;
    out.outputPower = meterWindowSamples > 0 ? meterEnergy / meterWindowSamples : 0.0;
    const double wetGain = static_cast<double> (dbToGain (autoGain->getCorrectionDb())) * mix.getCurrent() * outputGain.getCurrent()
        * (1.0 - bypass.getCurrent());
    out.productsGain = wetGain * wetGain;

    for (std::size_t b = 0; b < recorders->size(); ++b)
    {
        auto& rec = (*recorders)[b];
        auto& band = out.bands[b];
        band.kind = rec.kind;
        const int length = BandCapture::lengthFor (osFactor);
        band.shaped = recording && rec.kind != filters::ShaperKind::None && rec.recorded >= length;
        double weighted = 0.0;
        std::size_t loudest = static_cast<std::size_t> (rec.lane);
        for (std::size_t l = 0; l < rec.laneEnergy.size(); ++l)
        {
            weighted += rec.laneWeight[l] * rec.laneEnergy[l];
            if (rec.laneEnergy[l] > rec.laneEnergy[loudest])
                loudest = l;
        }
        const double recorded = rec.laneEnergy[static_cast<std::size_t> (rec.lane)];
        band.lanesScale = recorded > 0.0 ? weighted / recorded : 1.0;
        band.modulationPower = rec.modulationSamples > 0 ? rec.modulationEnergy / rec.modulationSamples : 0.0;
        if (band.shaped)
            for (int i = 0; i < length; ++i) // oldest first
                band.x[static_cast<std::size_t> (i)] =
                    rec.ring[static_cast<std::size_t> ((rec.position - length + i) & BandRecorder::kRingMask)];

        // Next window: record the lane with the most products.
        rec.lane = static_cast<int> (loudest);
    }
    out.serial = ++meterSerial;
    captures->publish();
    attachRecorders (false);
    meterCounter = 0;
}

void Engine::updateCoverage (const BandSettingsArray& chain) noexcept
{
    // Channels processed by at least one band of the chain go through oversampling (CH-6).
    osCovered.fill (false);
    for (const auto& band : chain)
    {
        if (! band.active)
            continue;
        const auto lanes = makeLanes (band.component, band.channelMask, band.includeLfe, cfg.topology);
        for (int l = 0; l < lanes.numLanes; ++l)
        {
            const auto& lane = lanes.lanes[static_cast<std::size_t> (l)];
            if (lane.a >= 0)
                osCovered[static_cast<std::size_t> (lane.a)] = true;
            if (lane.b >= 0)
                osCovered[static_cast<std::size_t> (lane.b)] = true;
        }
    }
}

void Engine::updateCorrector (const BandSettingsArray& bands) noexcept
{
    // The corrector follows the bands as heard: Dynamic bands at their current gain.
    auto& target = *correctorBands;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto bi = static_cast<std::size_t> (b);
        const auto& band = bands[bi];
        auto& c = target[bi];
        c.brickwall = firBrickwall[bi];
        c.shape = band.shape;
        if (band.mode == BandMode::Dynamic && shapeHasGain (band.shape.shape))
            c.shape.gainDb += (*bandProcessors)[bi].getDynamicGainDb();
        c.active = band.active && (c.brickwall || ! filters::isIdentity (c.shape));
        c.lanes = c.active ? makeLanes (band.component, band.channelMask, band.includeLfe, cfg.topology) : LaneLayout {};
    }
    corrector->update (target, cfg.topology, cfg.sampleRate * osFactor);
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
    // Configuration changes set the latency (ADR-0011): Max Lookahead, oversampling, the Natural
    // Phase corrector and, while at least one band is in Spectral mode, the spectral frame.
    const int lookahead =
        std::clamp (static_cast<int> (std::lround (0.001 * params.maxLookaheadMs * cfg.sampleRate)), 0, maxLookaheadSamples);
    const bool wantSpectral = std::any_of (bands.begin(), bands.end(), [] (const BandSettings& b) { return b.isSpectral(); });

    bool spectralChanged = wantSpectral != spectralEnabled;
    if (wantSpectral)
        spectralChanged = spectralEngine->setQuality (params.quality) || spectralChanged;
    if (spectralChanged)
        spectralEngine->reset();
    spectralEnabled = wantSpectral;

    // Analog Feel: every band takes the model's curves and nonlinearity (ADR-0009).
    useHeardCopy = params.phaseMode == PhaseMode::AnalogFeel;
    if (useHeardCopy)
    {
        *heardBands = bands;
        for (auto& band : *heardBands)
            band.shape.analog = params.analogModel;
    }
    const BandSettingsArray& heard = useHeardCopy ? *heardBands : bands;

    // Natural Phase brickwall cuts are linear-phase parts of the corrector where its FIR is steeper
    // than the 96 dB/oct IIR; below that frequency the IIR (phase-corrected) is kept.
    const bool wantCorrection = usesPhaseCorrection (params.phaseMode, params.quality);
    const double firBrickwallHz = phase::minimumFirBrickwallHz (params.quality, cfg.sampleRate);
    useChainCopy = false;
    for (std::size_t b = 0; b < heard.size(); ++b)
    {
        firBrickwall[b] = wantCorrection && params.phaseMode == PhaseMode::NaturalPhase && isBrickwallCut (heard[b])
            && heard[b].shape.frequencyHz >= firBrickwallHz;
        useChainCopy = useChainCopy || firBrickwall[b];
    }
    if (useChainCopy)
    {
        *chainBands = heard;
        for (std::size_t b = 0; b < heard.size(); ++b)
            if (firBrickwall[b])
                (*chainBands)[b].active = false;
    }
    updateCoverage (useChainCopy ? *chainBands : heard);

    // Oversampling of the band chain (ADR-0008): polyphase IIR half-bands in Minimum Phase,
    // linear-phase FIR half-bands in Natural Phase and Analog Feel. An offline render may use its
    // own factor (OS-4).
    const int realtimeFactor = effectiveOversampling (params.oversampling, params.phaseMode, params.quality, cfg.sampleRate);
    const int renderFactor = params.renderOversampling.has_value()
        ? effectiveOversampling (*params.renderOversampling, params.phaseMode, params.quality, cfg.sampleRate)
        : realtimeFactor;
    const int factor = params.nonRealtime ? renderFactor : realtimeFactor;
    const auto type =
        params.phaseMode == PhaseMode::MinimumPhase ? oversampling::FilterType::PolyphaseIir : oversampling::FilterType::LinearPhaseFir;
    if (factor != osFactor || (factor > 1 && (type != osType || params.quality != osQuality)))
        setOversampling (factor, type, params.quality);

    // Natural Phase corrector (ADR-0005).
    const bool correctorChanged = corrector->setQuality (params.quality);
    if (wantCorrection != correctionActive && ! correctorChanged)
        corrector->reset();
    correctionActive = wantCorrection;

    const int osLatency = oversampler != nullptr ? oversampler->getLatencySamples() : 0;
    const int correctionLatency = correctionActive ? corrector->getLatencySamples() : 0;
    const int spectralLatency = spectralEnabled ? spectralEngine->getLatencySamples() : 0;

    // OS-4: real time and offline render report the same latency, so the render stays aligned
    // with what was heard; the processed path of the shorter one is padded at its end.
    const int sharedOsLatency =
        std::max (oversamplingLatency (type, realtimeFactor, params.quality), oversamplingLatency (type, renderFactor, params.quality));
    const bool padding = params.renderOversampling.has_value();
    if (padding && ! renderPadActive)
        renderPadDelay.reset();
    renderPadActive = padding;
    renderPadDelay.setDelay (sharedOsLatency - osLatency);

    const int latency = lookahead + sharedOsLatency + correctionLatency + spectralLatency;

    lookaheadSamples = lookahead;
    osBypassDelay.setDelay (lookahead + osLatency);
    soloDelay.setDelay (sharedOsLatency + correctionLatency + spectralLatency);
    dryDelay.setDelay (latency);
    latencySamples.store (latency, std::memory_order_relaxed);
}

int Engine::oversamplingLatency (oversampling::FilterType type, int factor, Quality quality) const noexcept
{
    return factor > 1 ? (*oversamplers)[oversamplerIndex (type, factor, quality)].getLatencySamples() : 0;
}

template <typename Sample>
void Engine::process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params,
                      const BandSettingsArray& bands) noexcept SSC_REALTIME
{
    if (! prepared)
        return;
    numChannels = std::min (numChannels, cfg.numChannels);
    controlBlock = controlBlockFor (params.quality);
    updateMetering();

    const bool soloing = params.soloBand >= 0 && params.soloBand < kNumBands && bands[static_cast<std::size_t> (params.soloBand)].active;
    soloAmount.setTarget (soloing ? 1.0 : 0.0);
    if (soloing)
        lastSoloShape = auditionShapeFor (bands[static_cast<std::size_t> (params.soloBand)].shape);

    configure (params, bands);
    const BandSettingsArray& heard = useHeardCopy ? *heardBands : bands; // as heard (Analog Feel model)
    const BandSettingsArray& chain = useChainCopy ? *chainBands : heard; // the IIR chain

    autoGainSettings = { params.autoGain, params.autoGainWeighting, params.autoGainSpeed };
    if (params.autoGain)
        updateAutoGainCurves (heard);

    outputGain.setTarget (dbToGain (params.outputGainDb));
    mix.setTarget (std::clamp (params.mix, 0.0, 1.0));
    bypass.setTarget (params.bypass ? 1.0 : 0.0);

    Sample* chunkPointers[kMaxChannels];
    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int n = std::min (chunkSize, numSamples - offset);
        for (int ch = 0; ch < numChannels; ++ch)
            chunkPointers[ch] = channels[ch] + offset;
        if (metering && ! recording)
        {
            // The capture window: the last BandCapture::lengthFor() samples at the processing rate
            // before the end of the period (at the granularity of a chunk).
            const int window = (BandCapture::lengthFor (osFactor) + osFactor - 1) / osFactor;
            if (meterCounter + n > meterPeriod - window)
                attachRecorders (true);
        }
        processChunk (chunkPointers, numChannels, n, chain, heard);
        if (metering)
            meterChunk (chunkPointers, numChannels, n);
    }
}

void Engine::runBands (const BandSettingsArray& chain, int numChannels, int offset, int numSamples) noexcept
{
    const auto n = static_cast<std::size_t> (numSamples);
    const auto start = static_cast<std::size_t> (offset);

    if (osFactor == 1)
    {
        // The main path is the input delayed by Max Lookahead (exactly the input when it is 0).
        double* segment[kMaxChannels];
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& h = history[static_cast<std::size_t> (ch)];
            auto& w = wet[static_cast<std::size_t> (ch)];
            for (std::size_t i = 0; i < n; ++i)
                w[start + i] =
                    h[static_cast<std::size_t> ((historyWrite + offset + static_cast<int> (i) - lookaheadSamples) & historyMask)];
            segment[ch] = w.data() + offset;
        }
        const DetectionInput detection { historyPointers.data(), historyMask, historyWrite + offset, lookaheadSamples };
        for (std::size_t b = 0; b < bandProcessors->size(); ++b)
            (*bandProcessors)[b].process (chain[b], cfg.topology, segment, numChannels, numSamples, controlBlock, detection);
        return;
    }

    // Oversampled: the upsampled input feeds the detection history and, delayed by the
    // lookahead, the bands; every channel runs through the filters so that they stay warm.
    const int factor = osFactor;
    const int highSamples = numSamples * factor;
    const int highLookahead = lookaheadSamples * factor;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t> (ch);
        oversampler->upsample (ch, dry[c].data() + offset, osInput.data(), numSamples);
        auto& ring = osHistory[c];
        auto& w = wetHigh[c];
        for (int j = 0; j < highSamples; ++j)
            ring[static_cast<std::size_t> ((osHistoryWrite + j) & osHistoryMask)] = osInput[static_cast<std::size_t> (j)];
        for (int j = 0; j < highSamples; ++j)
            w[static_cast<std::size_t> (j)] = ring[static_cast<std::size_t> ((osHistoryWrite + j - highLookahead) & osHistoryMask)];
    }

    const DetectionInput detection { osHistoryPointers.data(), osHistoryMask, osHistoryWrite, highLookahead };
    const int highControlBlock = std::min (BandProcessor::kMaxControlBlock, controlBlock * factor);
    for (std::size_t b = 0; b < bandProcessors->size(); ++b)
        (*bandProcessors)[b].process (chain[b], cfg.topology, wetHighPointers.data(), numChannels, highSamples, highControlBlock,
                                      detection);
    osHistoryWrite = (osHistoryWrite + highSamples) & osHistoryMask;

    // Channels that no band processes take the exactly delayed input instead (CH-6), with a short
    // crossfade when that changes.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t> (ch);
        oversampler->downsample (ch, wetHigh[c].data(), osOutput.data(), numSamples);
        const int target = osCovered[c] ? osRampLength : 0;
        int ramp = osRamp[c];
        auto& w = wet[c];
        const auto& bypassed = osBypass[c];
        for (std::size_t i = 0; i < n; ++i)
        {
            ramp += ramp < target ? 1 : (ramp > target ? -1 : 0);
            if (ramp == osRampLength)
                w[start + i] = osOutput[i];
            else if (ramp == 0)
                w[start + i] = bypassed[start + i];
            else
                w[start + i] = bypassed[start + i] + (osOutput[i] - bypassed[start + i]) * ramp / osRampLength;
        }
        osRamp[c] = ramp;
    }
}

template <typename Sample>
void Engine::processChunk (Sample* const* channels, int numChannels, int numSamples, const BandSettingsArray& chain,
                           const BandSettingsArray& bands) noexcept
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

    // Solo auditions the lookahead-delayed input at the base rate.
    const bool soloActive = soloAmount.isSmoothing() || soloAmount.getCurrent() > 0.0;
    if (soloActive)
    {
        if (! soloWasActive)
        {
            soloProcessor->reset();
            soloDelay.reset();
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& h = history[static_cast<std::size_t> (ch)];
            auto& s = solo[static_cast<std::size_t> (ch)];
            for (std::size_t i = 0; i < n; ++i)
                s[i] = h[static_cast<std::size_t> ((historyWrite + static_cast<int> (i) - lookaheadSamples) & historyMask)];
        }
    }
    soloWasActive = soloActive;

    // Exactly delayed input for the channels that bypass oversampling (CH-6).
    if (osFactor > 1)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (dry[static_cast<std::size_t> (ch)].begin(), dry[static_cast<std::size_t> (ch)].begin() + numSamples,
                       osBypass[static_cast<std::size_t> (ch)].begin());
        osBypassDelay.processInPlace (osBypassPointers.data(), numChannels, numSamples);
    }

    // Bands and the Natural Phase corrector, in segments that end on the corrector's update grid
    // (absolute sample positions), so the output does not depend on the host block size (NFR-3).
    int done = 0;
    while (done < numSamples)
    {
        int length = numSamples - done;
        if (correctionActive)
        {
            const int interval = corrector->getUpdateInterval();
            const auto phaseInGrid = static_cast<int> (samplePosition % interval);
            if (phaseInGrid == 0)
                updateCorrector (bands);
            length = std::min (length, interval - phaseInGrid);
        }

        runBands (chain, numChannels, done, length);

        if (correctionActive)
        {
            double* segment[kMaxChannels];
            for (int ch = 0; ch < numChannels; ++ch)
                segment[ch] = wet[static_cast<std::size_t> (ch)].data() + done;
            corrector->process (segment, numChannels, length);
        }
        done += length;
        samplePosition += length;
    }
    historyWrite = (historyWrite + numSamples) & historyMask;

    if (spectralEnabled)
        spectralEngine->process (wetPointers.data(), numChannels, numSamples, chain, cfg.topology);

    if (renderPadActive)
        renderPadDelay.processInPlace (wetPointers.data(), numChannels, numSamples);

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
        resetChain();
        soloProcessor->reset();
        spectralEngine->reset();
        autoGain->reset();
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

template void Engine::process<float> (float* const*, int, int, const GlobalParams&, const BandSettingsArray&) noexcept SSC_REALTIME;
template void Engine::process<double> (double* const*, int, int, const GlobalParams&, const BandSettingsArray&) noexcept SSC_REALTIME;
} // namespace ssc::engine
