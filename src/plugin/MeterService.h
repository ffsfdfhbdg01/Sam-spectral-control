#pragma once

#include <array>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "ssc/analysis/DistortionMeter.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/TripleBuffer.h"

namespace ssc::plugin
{
/** Readings of the THD+N and aliasing meters (research §13c, MTR-1), relative to the output power. */
struct MeterReadings
{
    bool valid = false; ///< there is output to measure against
    float thdDb = -200.0f;
    float aliasingDb = -200.0f;
    std::array<float, kNumBands> bandThdDb {};
    std::array<float, kNumBands> bandAliasingDb {};
    std::array<bool, kNumBands> bandMeasured {}; ///< the band has an active nonlinearity or gain modulation
    bool nonlinear = false;                      ///< some band nonlinearity is running (OS-5)
    int osFactor = 1;                            ///< oversampling of the band chain in use
    int maxOsFactor = 1;                         ///< the largest the sample rate allows
    std::uint64_t serial = 0;
};

/** Display floor of the meters: lower values read "< −120 dB" (MTR-3). */
inline constexpr float kMeterFloorDb = -120.0f;

/** OS-5: aliasing threshold above which a higher oversampling factor is suggested. */
inline constexpr float kOversamplingHintDb = -80.0f;

/** OS-5: the oversampling factor to suggest, or 0 when none (no nonlinearity, aliasing below the
    threshold, or the factor is already the largest). */
int suggestedOversampling (const MeterReadings& readings, float thresholdDb = kOversamplingHintDb) noexcept;

/**
    The THD+N and aliasing meters (research §13c, MTR-1…MTR-4) on a background thread.

    While a client (the editor) is registered, the engine records the input of every band
    nonlinearity and the gain modulation of its Dynamic bands and publishes a capture every 125 ms;
    this thread analyses each capture (analysis::DistortionMeter), weighs the bands by how their
    products reach the output, averages powers with a 300 ms time constant and publishes the
    readings through a triple buffer. The audio thread only records and copies.
*/
class MeterService : private juce::Thread
{
public:
    explicit MeterService (engine::Engine& engine);
    ~MeterService() override;

    // Message thread
    void addClient();
    void removeClient();

    // Reader (editor) side
    TripleBuffer<MeterReadings>& readings() noexcept { return published; }

    static constexpr double kAveragingSeconds = 0.3;
    static constexpr int kPollMs = 20;
    /** Band analyses per capture at most (the others keep their last result, in turn). */
    static constexpr int kMaxAnalysesPerCapture = 8;

    /** Analyses one capture and returns the readings (the work of the thread; for tests). */
    const MeterReadings& process (const engine::DistortionCapture& capture);

private:
    void run() override;

    engine::Engine& engine;
    analysis::DistortionMeter meter;
    std::array<double, kNumBands> thdPower {}, aliasingPower {};    ///< averaged
    std::array<double, kNumBands> lastProducts {}, lastAliasing {}; ///< of the last analysis per band
    std::array<std::uint64_t, kNumBands> analysedAt {};             ///< capture serial of that analysis
    int nextBand = 0;                                               ///< round robin
    double outputPower = 0.0;
    bool primed = false;
    std::uint64_t lastCapture = 0; ///< serial of the last capture analysed: captures may be skipped
    MeterReadings current;
    TripleBuffer<MeterReadings> published;
    std::uint64_t serial = 0;
    int clients = 0;
};
} // namespace ssc::plugin
