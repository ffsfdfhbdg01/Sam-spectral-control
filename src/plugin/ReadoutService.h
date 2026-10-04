#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include <juce_core/juce_core.h>

#include "ssc/analysis/BandReadouts.h"
#include "ssc/util/TripleBuffer.h"

namespace ssc::plugin
{
/** Readouts of every band, published to the editor. */
struct ReadoutSnapshot
{
    std::array<analysis::BandReadout, kNumBands> bands {};
    std::uint64_t serial = 0;
};

/**
    Band readouts on a background thread (research §12c, PR-1, PR-2): every 50 ms it reads the band
    and global settings, recomputes the readouts of the bands whose settings changed (Dynamic
    bands also when their gain moved by 0.5 dB or more) and publishes them. Nothing of this runs
    on the audio thread; the work only happens while an editor is open.
*/
class ReadoutService : private juce::Thread
{
public:
    /** Reads the current settings (any thread: atomics only). */
    using SettingsSource = std::function<void (engine::GlobalParams&, engine::BandSettingsArray&,
                                               std::array<float, kNumBands>& dynamicGainsDb, double& sampleRate)>;

    explicit ReadoutService (SettingsSource source);
    ~ReadoutService() override;

    // Message thread
    void addClient();
    void removeClient();

    // Reader (editor) side
    TripleBuffer<ReadoutSnapshot>& readouts() noexcept { return published; }

    static constexpr int kIntervalMs = 50;

private:
    void run() override;
    bool changed (int band, const engine::BandSettings& settings, float dynamicGainDb) const noexcept;

    SettingsSource source;
    analysis::BandReadouts computer;
    double preparedRate = 0.0;

    engine::GlobalParams lastParams;
    engine::BandSettingsArray lastBands {};
    std::array<float, kNumBands> lastGains {};
    bool haveLast = false;

    ReadoutSnapshot current;
    TripleBuffer<ReadoutSnapshot> published;
    std::uint64_t serial = 0;
    int clients = 0;
};
} // namespace ssc::plugin
