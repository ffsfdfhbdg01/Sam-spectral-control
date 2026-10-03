#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/engine/ChannelTopology.h"
#include "ssc/engine/Types.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ssc/util/DelayLine.h"
#include "ssc/util/LinearSmoother.h"

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
    Quality quality = Quality::Normal;
    int soloBand = -1;           ///< band auditioned on its own (−1 = none); see auditionShapeFor()
    double maxLookaheadMs = 0.0; ///< delay of the main path; Dynamic bands look ahead up to this (DYN-8)
};

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

    Signal flow (finalised over milestones M2–M4):
    processed = chain(input); out = (mix * processed + (1 - mix) * dry) * outputGain,
    crossfaded with the latency-aligned dry signal when bypassed.

    prepare() allocates for the given configuration; process() is real-time safe.
*/
class Engine
{
public:
    void prepare (const EngineConfig& config);
    void reset() noexcept;

    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params,
                  const BandSettingsArray& bands) noexcept;

    /** Convenience overload without bands (all inactive). */
    template <typename Sample>
    void process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params) noexcept
    {
        static const BandSettingsArray none {};
        process (channels, numChannels, numSamples, params, none);
    }

    const BandProcessor& band (int index) const noexcept { return bandProcessors[static_cast<std::size_t> (index)]; }

    /** Live gain change of a band: its Dynamic gain, or the largest Spectral change; any thread (DYN-11). */
    float getBandGainChangeDb (int index) const noexcept;

    bool isSpectralEngineRunning() const noexcept { return spectralEnabled; }

    /** Reader side of the spectral mask view (one UI thread); see SpectralEngine::pollMaskView(). */
    const spectral::SpectralEngine::MaskView* pollSpectralView() noexcept { return spectralEngine.pollMaskView(); }

    /** Applies configuration parameters (Max Lookahead, later phase mode and spectral engine) without
        processing audio, so the latency is known right after prepare(). Audio thread or prepare. */
    void configure (const GlobalParams& params, const BandSettingsArray& bands) noexcept;

    /** Current latency in samples; any thread. */
    int getLatencySamples() const noexcept { return latencySamples.load (std::memory_order_relaxed); }
    const EngineConfig& getConfig() const noexcept { return cfg; }

    /** Blocks in which non-finite audio was replaced by silence (input or filter state); any thread. */
    std::uint32_t getNonFiniteEvents() const noexcept { return nonFiniteEvents.load (std::memory_order_relaxed); }

private:
    template <typename Sample>
    void processChunk (Sample* const* channels, int numChannels, int numSamples, const BandSettingsArray& bands) noexcept;

    static constexpr double kRampSeconds = 0.02;
    static constexpr double kMaxLookaheadSeconds = 0.02;

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
    std::array<BandProcessor, kNumBands> bandProcessors;
    BandProcessor soloProcessor;
    std::vector<std::vector<double>> solo;
    std::vector<double*> soloPointers;
    LinearSmoother<double> soloAmount;
    filters::BandShape lastSoloShape;
    int controlBlock = 16;
    MultiChannelDelay<double> dryDelay;

    LinearSmoother<double> outputGain, mix, bypass;
    std::atomic<std::uint32_t> nonFiniteEvents { 0 };

    // Spectral engine: runs while at least one band is in Spectral mode (ADR-0011).
    spectral::SpectralEngine spectralEngine;
    bool spectralEnabled = false;
    MultiChannelDelay<double> soloDelay;
};
} // namespace ssc::engine
