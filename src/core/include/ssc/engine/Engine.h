#pragma once

#include <vector>

#include "ssc/engine/Types.h"
#include "ssc/util/DelayLine.h"
#include "ssc/util/LinearSmoother.h"

namespace ssc::engine
{
struct EngineConfig
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;
};

/** Global (non-band) parameter values, read once per host block. */
struct GlobalParams
{
    double outputGainDb = 0.0;
    double mix = 1.0;      ///< 0 … 1
    bool bypass = false;
};

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
    void process (Sample* const* channels, int numChannels, int numSamples, const GlobalParams& params) noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }
    const EngineConfig& getConfig() const noexcept { return cfg; }

private:
    template <typename Sample>
    void processChunk (Sample* const* channels, int numChannels, int numSamples) noexcept;

    static constexpr double kRampSeconds = 0.02;

    EngineConfig cfg;
    int latencySamples = 0;

    std::vector<std::vector<double>> dry;
    std::vector<double*> dryPointers;
    MultiChannelDelay<double> dryDelay;

    LinearSmoother<double> outputGain, mix, bypass;
};
} // namespace ssc::engine
