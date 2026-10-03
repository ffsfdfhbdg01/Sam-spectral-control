#pragma once

#include <vector>

#include "ssc/engine/Types.h"

/**
    Loudness model of Auto Gain (research §7c, ADR-0007).

    Fletcher weighting: band powers in 1-ERB bands → SPL with a K-20 style calibration → loudness
    level of each band from the ISO 226:2003 equal-loudness contours (formula and table of the
    2003 edition; replaceable by the 2023 table) → sones → sum → loudness level in phons.
    Digital weighting: unweighted energy.
*/
namespace ssc::loudness
{
/** Sound pressure level (dB SPL) of a pure tone with the given loudness level (phon), ISO 226:2003. */
double phonToSpl (double phon, double hz) noexcept;

/** Loudness level (phon) of a pure tone at the given SPL, inverse of phonToSpl. */
double splToPhon (double splDb, double hz) noexcept;

/** Loudness (sone) of a loudness level (phon): doubling per 10 phon above 40 phon. */
double phonToSone (double phon) noexcept;
double soneToPhon (double sone) noexcept;

/** Calibration: a pink noise at −20 dBFS RMS plays at 83 dB SPL (K-20). */
inline constexpr double kSplAtFullScale = 103.0;

/** Bands of one ERB between 20 Hz and min (20 kHz, 0.45·fs), and the bins of an FFT that fall in each. */
class ErbBands
{
public:
    void prepare (double sampleRate, int fftSize);

    int numBands() const noexcept { return static_cast<int> (centres.size()); }
    double centreHz (int band) const noexcept { return centres[static_cast<std::size_t> (band)]; }

    /**
        Mean-square power per band from an unscaled real FFT (interleaved complex, N + 2 floats) of a
        frame windowed with a window whose Σ w² is windowPower. Adds to powersOut (weight scales it).
    */
    void accumulate (const float* spectrum, double windowPower, double weight, double* powersOut) const noexcept;

private:
    int fftSize = 0;
    std::vector<double> centres;
    std::vector<int> firstBin, lastBin;
};

/** Loudness of band powers (mean square, full scale sine = 0.5): phons (Fletcher) or dB energy (Digital). */
double loudnessOf (const double* bandPowers, const ErbBands& bands, AutoGainWeighting weighting) noexcept;
} // namespace ssc::loudness
