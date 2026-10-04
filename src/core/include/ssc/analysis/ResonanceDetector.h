#pragma once

#include <cstdint>
#include <vector>

#include "ssc/analysis/PitchTracker.h"

namespace ssc::analysis
{
/**
    Resonance hints for the display (research §11c, VIS-4, VIS-5). Graphics only: nothing here
    touches the audio.

    Per frame of a high-resolution, briefly averaged power spectrum (8192 points at 48 kHz,
    50–100 ms):
    1. steady partials are removed: they have the analysis window's shape (≥ 12 dB lower within
       ±4 bins on both sides) and are bridged over; what remains is the broadband part of the
       programme — and a resonance shapes everything that passes through it, so it stays,
    2. floor F[k]: the minimum of each bin over the last ≈ 2.5 s (minimum statistics, R. Martin,
       IEEE Trans. Speech Audio Process. 9(5), 2001): notes and hits come and go and drop out,
    3. envelope E[k]: median of the floor over ⅓ octave (robust against the peaks themselves),
    4. prominence R[k] = F[k] − E[k]; candidates are local maxima with R near the sensitivity, a
       −3 dB width between ≈ 55 Hz (narrower: left-overs of partials with vibrato or onsets; less
       in the bass) and a fraction of an ERB (Glasberg & Moore 1990), one per resonance, and not
       more than 18 dB below the mean level of their ⅓ octave (masked),
    5. candidates are tracked between frames (±1/24 octave): persistence, frequency stability,
    6. harmonics of a clearly voiced note (YIN f0) are penalised (sustained notes),
    7. score = prominence + persistence + stability − harmonicity + ear sensitivity (ISO 226),
    8. hysteresis: a hint shows after its score has been above θ_on for 300 ms and hides after it
       has been below θ_off for 1 s.

    prepare() allocates; push() is allocation-free.
*/
class ResonanceDetector
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        int fftSize = 8192;
        double minHz = 40.0;
        double maxHz = 16000.0;
        double minProminenceDb = 9.0;            ///< sensitivity: 6 dB (high) … 12 dB (low)
        double maxWidthErb = 0.8;                ///< −3 dB width of a candidate, in ERB
        double lineDropDb = 12.0;                ///< steady partials fall by this much within ±4 bins on both sides
        double minWidthHz = 55.0;                ///< narrower floor peaks are left-overs of partials (vibrato, onsets) …
        double minWidthErb = 0.3;                ///< … unless wider than this fraction of an ERB (the bass)
        double audibleRangeDb = 18.0;            ///< a peak more than this below its ⅓ octave's mean level is masked
        double floorSeconds = 2.5;               ///< minimum-statistics window
        double frameSeconds = 1024.0 / 48000.0;  ///< time between frames (sizes the floor window)
        double showScore = 1.8, hideScore = 0.9; ///< hysteresis thresholds θ_on, θ_off
        double showAfterSeconds = 0.3;
        double hideAfterSeconds = 1.0;
        int maxTracks = 48;
    };

    struct Hint
    {
        float hz = 0.0f;
        float prominenceDb = 0.0f;
        float seconds = 0.0f;    ///< how long the resonance has been present
        float confidence = 0.0f; ///< 0 … 1
    };

    void prepare (const Config& config);
    void reset() noexcept;

    /**
        One analysis frame: power per FFT bin (0 … fftSize/2, sine-calibrated), the pitch of the
        same moment, and the time since the previous frame.
    */
    void push (const float* binPower, int numBins, const PitchTracker::Result& pitch, double dtSeconds) noexcept;

    /** Visible hints, sorted by frequency. */
    const std::vector<Hint>& hints() const noexcept { return visibleHints; }

    /** Number of times a hint appeared or disappeared since prepare()/reset() (flicker, VIS-4). */
    int visibilityChanges() const noexcept { return changes; }

    /** Number of times a hint appeared since prepare()/reset(). */
    int appearances() const noexcept { return shown; }

    /** Peaks of the last frame above the prominence threshold, before the width tests (evaluation). */
    struct Candidate
    {
        float hz = 0.0f, prominenceDb = 0.0f, widthBins = 0.0f;
    };
    const std::vector<Candidate>& lastCandidates() const noexcept { return candidates; }

    /** Last frame without its steady partials, and the floor (dB per bin; evaluation). */
    const std::vector<float>& frameLevels() const noexcept { return levelDb; }
    const std::vector<float>& floorLevels() const noexcept { return shapeDb; }

private:
    struct Peak
    {
        double hz = 0.0, prominenceDb = 0.0;
        bool used = false;
        double lowHz = 0.0, highHz = 0.0; ///< −3 dB span
    };

    struct Track
    {
        double hz = 0.0;
        double prominenceDb = 0.0;
        double presence = 0.0;    ///< averaged 1/0 per frame (seen / missed)
        double harmonicity = 0.0; ///< averaged 1/0 per frame (on a harmonic of the voiced f0)
        double drift = 0.0;       ///< averaged |frequency change| in octaves
        double age = 0.0, missing = 0.0, above = 0.0, below = 0.0;
        bool visible = false;
        double score = 0.0;
    };

    void removeLines (int lo, int hi) noexcept;
    void updateFloor (int bins) noexcept;
    void computeEnvelope (int lo, int hi) noexcept;
    void findPeaks (int lo, int hi) noexcept;
    double scoreOf (const Track& t) const noexcept;
    double sensitivityDb (double hz) const noexcept;

    Config cfg;
    double binHz = 0.0;
    std::vector<float> levelDb, floorDb, shapeDb, envelopeDb, gridDb, window;
    std::vector<double> cumulativePower; ///< prefix sums of the frame's power (band energies)
    std::vector<std::uint8_t> lineMask;
    std::vector<float> runningMin;           ///< minimum of the current sub-window
    std::vector<std::vector<float>> subMins; ///< minima of the previous sub-windows
    int subFrames = 15, subIndex = 0, framesInSub = 0;
    std::vector<int> gridBin;
    std::vector<Peak> peaks;
    std::vector<Track> tracks;
    std::vector<Hint> visibleHints;
    std::vector<Candidate> candidates;
    int numTracks = 0;
    int changes = 0, shown = 0;
};
} // namespace ssc::analysis
