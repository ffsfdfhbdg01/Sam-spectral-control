#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/engine/ChannelTopology.h"
#include "ssc/engine/DistortionCapture.h"
#include "ssc/engine/Types.h"
#include "ssc/loudness/AutoGain.h"
#include "ssc/oversampling/Oversampler.h"
#include "ssc/phase/PhaseCorrector.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ssc/util/DelayLine.h"
#include "ssc/util/LinearSmoother.h"
#include "ssc/util/Realtime.h"
#include "ssc/util/TripleBuffer.h"

namespace ssc::engine
{
struct EngineConfig
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;
    ChannelTopology topology = ChannelTopology::stereo();
};

/** Global (non-band) parameter values, read once per host block. */
struct GlobalParams
{
    double outputGainDb = 0.0;
    double mix = 1.0; ///< 0 … 1
    bool bypass = false;
    bool delta = false; ///< hear what the plugin changes: (dry − mixed output) × output gain (GLB-2)
    Quality quality = Quality::Normal;
    int soloBand = -1;           ///< band auditioned on its own (−1 = none): its region of what is heard, on its target only
    double maxLookaheadMs = 0.0; ///< delay of the main path; Dynamic bands look ahead up to this (DYN-8)
    bool autoGain = false;       ///< global Auto Gain (AG-1)
    AutoGainWeighting autoGainWeighting = AutoGainWeighting::Fletcher;
    AutoGainSpeed autoGainSpeed = AutoGainSpeed::Stable;
    PhaseMode phaseMode = PhaseMode::MinimumPhase;
    AnalogModel analogModel = AnalogModel::N;      ///< curves and nonlinearity in Analog Feel (ADR-0009)
    Oversampling oversampling = Oversampling::Off; ///< of the band chain; see effectiveOversampling()

    /** Oversampling of an offline render (OS-4), used while nonRealtime; unset = as in real time.
        Both settings report the larger latency: the path with the shorter one is padded. */
    std::optional<Oversampling> renderOversampling;
    bool nonRealtime = false; ///< the host renders offline (AudioProcessor::isNonRealtime())
};

/** Largest oversampling factor at a sample rate: the internal rate stays at or below 400 kHz. */
constexpr int maxOversamplingFor (double sampleRate) noexcept
{
    int factor = 1;
    while (factor < 8 && sampleRate * factor * 2 <= 400000.0)
        factor *= 2;
    return factor;
}

/**
    Oversampling factor of the band chain: the setting, raised in Analog Feel to 2× at High and 4×
    at Max for its nonlinear models (research §8c), and capped by maxOversamplingFor().
*/
constexpr int effectiveOversampling (Oversampling setting, PhaseMode mode, Quality quality, double sampleRate) noexcept
{
    int factor = oversamplingFactor (setting);
    if (mode == PhaseMode::AnalogFeel)
        factor = std::max (factor, quality == Quality::Max ? 4 : (quality == Quality::High ? 2 : 1));
    return std::min (factor, maxOversamplingFor (sampleRate));
}

/**
    The corrector runs and the bands are heard with the magnitude of their analog prototypes
    (research §8c, PH-1): in Natural Phase (and their phase, latency D), in Analog Feel at High and
    Max (likewise), and in Minimum Phase at High and Max (minimum phase, no latency).
*/
constexpr bool usesPhaseCorrection (PhaseMode mode, Quality quality) noexcept
{
    const bool fine = quality == Quality::High || quality == Quality::Max;
    return mode == PhaseMode::NaturalPhase || ((mode == PhaseMode::AnalogFeel || mode == PhaseMode::MinimumPhase) && fine);
}

/** Coefficient update interval (samples) per Quality level (research §8). */
constexpr int controlBlockFor (Quality q) noexcept
{
    switch (q)
    {
        case Quality::Eco: return 32;
        case Quality::Normal: return 16;
        case Quality::High: return 8;
        case Quality::Max: return 4;
    }
    return 16;
}

/**
    Top-level DSP engine. Owns the processing chain and the dry path used by Mix and Bypass.

    Signal flow (ARC-2, ADR-0008, ADR-0005, ADR-0011):
    input → Max Lookahead delay → [upsample] → bands (IIR, dynamics, Character) → [downsample]
          → [Natural Phase corrector] → spectral engine → Auto Gain → output → Solo
    out = (mix * processed + (1 - mix) * dry) * outputGain, crossfaded with the latency-aligned
    dry signal when bypassed; Delta replaces it with (dry − mix * processed − (1 − mix) * dry) * outputGain.
    Solo plays the band's region of that signal (computed without the Auto Gain correction, AG-5)
    on the band's target only: the other channels and the other stereo component are silent.
    Channels that no band processes bypass oversampling exactly (CH-6). Latency = lookahead +
    oversampling + corrector + spectral frame; Solo adds none.

    prepare() allocates for every oversampling factor, phase mode and Quality, so process()
    stays real-time safe when they change.
*/
class Engine
{
public:
    void prepare (const EngineConfig& config);
    void reset() noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params,
                  const BandSettingsArray& bands) noexcept SSC_REALTIME;

    /** Convenience overload without bands (all inactive). */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params) noexcept
    {
        static const BandSettingsArray none {};
        process (channels, numChannels, numSamples, params, none);
    }

    const BandProcessor& band (int index) const noexcept { return (*bandProcessors)[static_cast<std::size_t> (index)]; }

    /** Live gain change of a band: its Dynamic gain, or the largest Spectral change; any thread (DYN-11). */
    float getBandGainChangeDb (int index) const noexcept;

    bool isSpectralEngineRunning() const noexcept { return spectralEnabled; }

    /** Global Auto Gain correction currently applied (dB); any thread. */
    float getAutoGainDb() const noexcept { return autoGain->getCorrectionDb(); }

    /** Reader side of the spectral mask view (one UI thread); see SpectralEngine::pollMaskView(). */
    const spectral::SpectralEngine::MaskView* pollSpectralView() noexcept { return spectralEngine->pollMaskView(); }

    /** Applies configuration parameters (Max Lookahead, phase mode, oversampling, Quality, spectral
        engine) without processing audio, so the latency is known right after prepare(). Real-time
        safe; audio thread or prepare. */
    void configure (const GlobalParams& params, const BandSettingsArray& bands) noexcept;

    /** Current latency in samples; any thread. */
    int getLatencySamples() const noexcept { return latencySamples.load (std::memory_order_relaxed); }

    /** Oversampling factor of the band chain in use (1 = off); audio thread. */
    int getOversamplingFactor() const noexcept { return osFactor; }

    /** True while the Natural Phase corrector is in the chain; audio thread. */
    bool isPhaseCorrectionActive() const noexcept { return correctionActive; }
    const EngineConfig& getConfig() const noexcept { return cfg; }

    /** Blocks in which non-finite audio was replaced by silence (input or filter state); any thread. */
    std::uint32_t getNonFiniteEvents() const noexcept { return nonFiniteEvents.load (std::memory_order_relaxed); }

    /** Switches the recording for the distortion meters on or off (research §13c); any thread,
        effective from the next block. Off by default: the meters only run while they are shown. */
    void setDistortionMetering (bool enabled) noexcept { meteringRequested.store (enabled, std::memory_order_relaxed); }

    /** Reader side (one thread) of the captures published every kMeterPeriodSeconds while metering. */
    TripleBuffer<DistortionCapture>& distortionCaptures() noexcept { return *captures; }

    static constexpr double kMeterPeriodSeconds = 0.125; ///< 8 readings per second (MTR-1: 5–10 Hz)

private:
    template <typename Sample>
    void processChunk (Sample* const* channels, int numChannels, int numSamples, const BandSettingsArray& chain,
                       const BandSettingsArray& bands) noexcept;
    void runBands (const BandSettingsArray& chain, int numChannels, int offset, int numSamples) noexcept;
    void updateCorrector (const BandSettingsArray& bands) noexcept;
    void setOversampling (int factor, oversampling::FilterType type, Quality quality) noexcept;
    int oversamplingLatency (oversampling::FilterType type, int factor, Quality quality) const noexcept;
    void updateCoverage (const BandSettingsArray& chain) noexcept;
    void resetChain() noexcept;
    void updateMetering() noexcept;
    void attachRecorders (bool attach) noexcept;
    template <typename Sample>
    void meterChunk (const Sample* const* channels, int numChannels, int numSamples) noexcept;
    void publishDistortion() noexcept;

    static constexpr double kRampSeconds = 0.02;
    static constexpr double kMaxLookaheadSeconds = 0.02;
    static constexpr int kMaxChunk = 512; ///< internal block: bounds the memory of the oversampled buffers

    int chunkSize = kMaxChunk;
    bool prepared = false; ///< process() before prepare() leaves the audio untouched (a host error)

    EngineConfig cfg;
    std::atomic<int> latencySamples { 0 };

    std::vector<std::vector<double>> dry, wet;
    std::vector<double*> dryPointers, wetPointers;

    // Undelayed input history: the detection signal of Dynamic bands and the source of the
    // main path, which is delayed by Max Lookahead.
    std::vector<std::vector<double>> history;
    std::vector<const double*> historyPointers;
    int historyMask = 0;
    int historyWrite = 0;
    int lookaheadSamples = 0;
    int maxLookaheadSamples = 0;
    // Large state lives on the heap, so an Engine fits on any thread's stack (Windows: 1 MB).
    std::unique_ptr<std::array<BandProcessor, kNumBands>> bandProcessors = std::make_unique<std::array<BandProcessor, kNumBands>>();
    // Solo (BAND-1): the band's region of what is heard, on the band's target only.
    void auditionSolo (int numChannels, int numSamples) noexcept;
    std::unique_ptr<BandProcessor> soloProcessor = std::make_unique<BandProcessor>();
    std::vector<std::vector<double>> solo;
    std::vector<double*> soloPointers;
    std::vector<std::vector<double>> deltaWet; ///< processed signal without the Auto Gain correction (AG-5)
    LinearSmoother<double> soloAmount;
    bool soloWasActive = false;
    BandSettings soloAudition; ///< region filter of the soloed band, with the band's target
    int controlBlock = 16;
    MultiChannelDelay<double> dryDelay;

    LinearSmoother<double> outputGain, mix, bypass, delta;
    std::atomic<std::uint32_t> nonFiniteEvents { 0 };

    // Spectral engine: runs while at least one band is in Spectral mode (ADR-0011).
    std::unique_ptr<spectral::SpectralEngine> spectralEngine = std::make_unique<spectral::SpectralEngine>();

    // Global Auto Gain: measured on the channels the bands process (CH-6), BS.1770 channel weights.
    void updateAutoGainCurves (const BandSettingsArray& bands) noexcept;
    std::unique_ptr<loudness::AutoGain> autoGain = std::make_unique<loudness::AutoGain>();
    std::array<float, kMaxChannels> autoGainWeights {};
    std::array<bool, kMaxChannels> autoGainCovered {};
    loudness::AutoGain::Settings autoGainSettings;
    std::array<filters::BandShape, kNumBands> curveShapes {};
    std::array<bool, kNumBands> curveActive {};
    std::array<LaneLayout, kNumBands> curveLanes {};
    std::vector<std::vector<double>> bandCurvesDb; ///< [band][ERB band]
    std::vector<double> channelCurveDb;            ///< scratch [ERB band]
    bool spectralEnabled = false;

    // Oversampling of the band chain (ADR-0008): one oversampler per filter type, factor and Quality.
    int maxFactor = 1;
    int osFactor = 1;
    oversampling::FilterType osType = oversampling::FilterType::PolyphaseIir;
    Quality osQuality = Quality::Normal;
    std::unique_ptr<std::array<oversampling::Oversampler, 24>> oversamplers = std::make_unique<std::array<oversampling::Oversampler, 24>>();
    oversampling::Oversampler* oversampler = nullptr;
    std::vector<std::vector<double>> osHistory, wetHigh, osBypass;
    std::vector<const double*> osHistoryPointers;
    std::vector<double*> wetHighPointers, osBypassPointers;
    std::vector<double> osInput, osOutput;
    int osHistoryMask = 0;
    int osHistoryWrite = 0;
    MultiChannelDelay<double> osBypassDelay;
    MultiChannelDelay<double> renderPadDelay; ///< OS-4: pads the processed path to the shared latency
    bool renderPadActive = false;
    std::array<bool, kMaxChannels> osCovered {};
    std::array<int, kMaxChannels> osRamp {}; ///< 0: exact bypass … osRampLength: oversampled path
    int osRampLength = 240;

    // Natural Phase (ADR-0005): updated on a fixed sample grid so the output is block-size independent.
    std::unique_ptr<phase::PhaseCorrector> corrector = std::make_unique<phase::PhaseCorrector>();
    std::unique_ptr<phase::CorrectorBands> correctorBands = std::make_unique<phase::CorrectorBands>();
    bool correctionActive = false;
    std::int64_t samplePosition = 0;
    // Bands as heard (Analog Feel: with the model) and the IIR chain (without Natural Phase brickwalls).
    std::unique_ptr<BandSettingsArray> heardBands = std::make_unique<BandSettingsArray>();
    std::unique_ptr<BandSettingsArray> chainBands = std::make_unique<BandSettingsArray>();
    bool useHeardCopy = false;
    bool useChainCopy = false;
    std::array<bool, kNumBands> firBrickwall {}; ///< Natural Phase brickwall cuts, realised by the corrector

    // Distortion meters (research §13c): recorders of the band nonlinearities, published per period.
    // The recorders are attached for a window at the end of each period, long enough to fill the
    // capture at the processing rate: the rest of the time the bands run as without meters.
    std::atomic<bool> meteringRequested { false };
    bool metering = false;
    bool recording = false;
    std::unique_ptr<std::array<BandRecorder, kNumBands>> recorders = std::make_unique<std::array<BandRecorder, kNumBands>>();
    std::unique_ptr<TripleBuffer<DistortionCapture>> captures = std::make_unique<TripleBuffer<DistortionCapture>>();
    int meterPeriod = 4800; ///< base-rate samples between captures
    int meterCounter = 0;   ///< base-rate samples since the last capture
    double meterEnergy = 0.0;
    int meterWindowSamples = 0;
    std::uint64_t meterSerial = 0;
};
} // namespace ssc::engine
