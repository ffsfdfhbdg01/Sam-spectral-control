#pragma once

#include <vector>

namespace ssc::analysis
{
/**
    How active each region of the spectrum is, for the display glow (research §11c, VIS-2).

    Works on the display grid (log-spaced points, levels in dB with the display tilt), fed with the
    levels of single frames (no display averaging):
    - level: exponential average of the power per point (τ ≈ 300 ms), in dB,
    - density: that level relative to the mean level of the audible points — how much a region
      stands out from the spectrum as a whole (a flat, tilted spectrum has none),
    - flux: positive level change between frames (fresh onsets), averaged over ≈ 150 ms.
    Activity = clamp ((density + fluxWeight · flux) / spanDb, 0, 1), faded out between floorDb and
    floorDb + fadeDb so that silence never glows.

    prepare() allocates; push() is allocation-free.
*/
class SpectrumActivity
{
public:
    struct Config
    {
        double averagingMs = 300.0;
        double fluxMs = 150.0;
        double spanDb = 12.0;    ///< density (+ flux) giving full activity
        double fluxWeight = 0.5; ///< dB of activity per dB of level rise
        double floorDb = -90.0;  ///< no activity below this level
        double fadeDb = 20.0;    ///< full activity above floorDb + fadeDb
    };

    void prepare (int numPoints, const Config& config);
    void reset() noexcept;

    /** One frame of levels (dB per point), taken dtSeconds after the previous one. */
    void push (const float* levelDb, int numPoints, double dtSeconds) noexcept;

    /** Activity per point, 0 … 1. */
    const std::vector<float>& activity() const noexcept { return values; }

private:
    Config cfg;
    std::vector<float> power, previousDb, flux, values;
    bool primed = false;
};

/**
    Peak hold for a display curve: each point keeps its highest level for holdSeconds, then falls
    at decayDbPerSecond, never below the current level.
*/
class PeakHold
{
public:
    void prepare (int numPoints, double holdSeconds, double decayDbPerSecond);
    void reset() noexcept;

    void push (const float* levelDb, int numPoints, double dtSeconds) noexcept;

    const std::vector<float>& peaks() const noexcept { return peakDb; }

private:
    std::vector<float> peakDb, holdLeft;
    double hold = 2.0, decay = 6.0;
    bool primed = false;
};
} // namespace ssc::analysis
