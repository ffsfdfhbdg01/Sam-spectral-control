#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include <juce_core/juce_core.h>

#include "plugin/AnalyzerSettings.h"
#include "ssc/analysis/SpectrumActivity.h"
#include "ssc/analysis/SpectrumAnalyzer.h"
#include "ssc/util/SpscRingBuffer.h"
#include "ssc/util/TripleBuffer.h"

namespace ssc::plugin
{
/** One analyzer snapshot published to the editor. */
struct SpectrumFrame
{
    static constexpr int kMaxPoints = 512;

    int numPoints = 0;
    std::array<float, kMaxPoints> hz {};
    std::array<float, kMaxPoints> preDb {};
    std::array<float, kMaxPoints> postDb {};
    std::array<float, kMaxPoints> peakDb {};   ///< peak hold of the main spectrum (post, or pre in Pre mode)
    std::array<float, kMaxPoints> activity {}; ///< glow of the main spectrum, 0 … 1 (VIS-2)
    bool hasPre = false, hasPost = false, hasPeak = false, hasActivity = false;
    std::uint64_t serial = 0;
};

/**
    Runs the pre/post spectrum analyzers on a background thread (ADR-0010).

    The audio thread only pushes mono-summed samples into lock-free FIFOs; the analysis thread
    runs while at least one editor is open and publishes frames through a triple buffer: the
    averaged spectra, the peak hold and the activity glow of the main spectrum (research §11c).
    Settings come from the message thread and are applied by the analysis thread.
*/
class AnalyzerService : private juce::Thread
{
public:
    AnalyzerService();
    ~AnalyzerService() override;

    /** Reconfigures for a new sample rate. Must not run concurrently with pushPre/pushPost. */
    void prepare (double sampleRate);

    // Audio thread
    void pushPre (const float* samples, int numSamples) noexcept { preFifo.write (samples, static_cast<std::size_t> (numSamples)); }
    void pushPost (const float* samples, int numSamples) noexcept { postFifo.write (samples, static_cast<std::size_t> (numSamples)); }

    // Message thread
    void addClient();
    void removeClient();
    void setSettings (const AnalyzerSettings& settings);
    AnalyzerSettings getSettings() const;

    // Reader (editor) side
    TripleBuffer<SpectrumFrame>& frames() noexcept { return published; }

private:
    void run() override;
    void configureAnalyzers (const AnalyzerSettings& settings);
    void publishFrame (double dtSeconds);

    SpscRingBuffer<float> preFifo, postFifo;
    analysis::SpectrumAnalyzer preAnalyzer, postAnalyzer;
    analysis::SpectrumActivity activity;
    analysis::PeakHold peakHold;
    TripleBuffer<SpectrumFrame> published;
    std::array<float, 2048> scratch {};
    double sampleRate = 48000.0;
    int clients = 0;
    std::uint64_t serial = 0;

    mutable std::mutex settingsLock; ///< message thread ↔ analysis thread only, never the audio thread
    AnalyzerSettings pendingSettings, activeSettings;
    std::atomic<bool> settingsChanged { false };
};
} // namespace ssc::plugin
