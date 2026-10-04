#pragma once

#include <vector>

#include "ssc/analysis/PitchTracker.h"
#include "ssc/analysis/ResonanceDetector.h"
#include "ssc/analysis/SpectrumAnalyzer.h"

namespace ssc::analysis
{
/**
    The whole resonance-hint analysis of a signal (research §11c, VIS-4, VIS-5): an 8192-point
    spectrum (at 48 kHz, scaled with fs) averaged over 75 ms and computed every 1024 samples,
    the pitch tracker and the detector.

    Runs on the analysis thread; prepare() allocates, push() is allocation-free.
*/
class ResonanceHints
{
public:
    /** base: the detector settings (sensitivity …); size, range and timing are set here. */
    void prepare (double sampleRate, const ResonanceDetector::Config& base = {});
    void reset() noexcept;

    /** Feeds samples (mono); returns true when at least one analysis frame was completed. */
    bool push (const float* samples, int numSamples) noexcept;

    /** Visible hints, sorted by frequency. */
    const std::vector<ResonanceDetector::Hint>& hints() const noexcept { return detector.hints(); }

    int appearances() const noexcept { return detector.appearances(); }
    int visibilityChanges() const noexcept { return detector.visibilityChanges(); }

private:
    void analyseFrame() noexcept;

    SpectrumAnalyzer spectrum;
    ResonanceDetector detector;
    PitchTracker pitch;
    std::vector<float> recent; ///< the latest pitch-frame samples (ring)
    std::vector<float> pitchFrame;
    double fs = 48000.0;
    int hop = 1024, pending = 0, write = 0;
};
} // namespace ssc::analysis
