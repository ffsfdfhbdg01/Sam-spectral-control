#pragma once

#include <algorithm>
#include <cmath>

#include "ssc/engine/Types.h"

/**
    Building blocks of the Dynamic and Spectral band modes (research §6c, DYN-4…DYN-7):
    a feed-forward static characteristic with a quadratic soft knee, decoupled attack/release
    smoothing in dB and a mean-square level detector (Giannoulis, Massberg & Reiss 2012).
*/
namespace ssc::dynamics
{
/** Ratio at or above which the characteristic is a limiter (∞:1); matches the parameter display. */
inline constexpr double kInfiniteRatio = 99.99;

/** Level reported for silence, so the dB domain never sees −inf. */
inline constexpr double kSilenceDb = -200.0;

/**
    Static characteristic: gain change in dB for a detected level in dB.

    In the active region the slope of output over input level is 1/R, so the gain change is
    c·(x − T) with c = 1/R − 1 (c = −1 for ∞:1). Above acts on levels over the threshold, Below on
    levels under it; the knee of width W blends quadratically with a continuous derivative.
    The result is limited to ±Range. R < 1 expands, R > 1 compresses (DYN-5).
*/
struct GainComputer
{
    double thresholdDb = -20.0;
    double kneeDb = 6.0;
    double rangeDb = 12.0;
    double slope = -0.5; ///< c = 1/R − 1
    bool above = true;

    static GainComputer make (double thresholdDb, double ratio, double kneeDb, double rangeDb, Direction direction) noexcept
    {
        GainComputer g;
        g.thresholdDb = thresholdDb;
        g.kneeDb = std::max (0.0, kneeDb);
        g.rangeDb = std::max (0.0, rangeDb);
        g.slope = ratio >= kInfiniteRatio ? -1.0 : 1.0 / std::max (ratio, 1.0e-3) - 1.0;
        g.above = direction == Direction::Above;
        return g;
    }

    double operator() (double levelDb) const noexcept
    {
        const double over = levelDb - thresholdDb;
        const double halfKnee = 0.5 * kneeDb;
        // 2W, never a literal zero: with W = 0 the knee branches are not taken, but MSVC still warns
        // about a division by zero there once a constant 0 dB knee is inlined (C4723).
        const double twiceKnee = 2.0 * std::max (kneeDb, 1.0e-12);
        double gain = 0.0;

        if (above)
        {
            if (over > halfKnee)
                gain = slope * over;
            else if (over >= -halfKnee && kneeDb > 0.0)
                gain = slope * (over + halfKnee) * (over + halfKnee) / twiceKnee;
        }
        else
        {
            if (over < -halfKnee)
                gain = slope * over;
            else if (over <= halfKnee && kneeDb > 0.0)
                gain = -slope * (halfKnee - over) * (halfKnee - over) / twiceKnee;
        }

        return std::clamp (gain, -rangeDb, rangeDb);
    }
};

/**
    Threshold side of a band from the way it changes the gain (the band's Direction): Down cuts —
    compression above the threshold, expansion (R < 1) below it, like a gate; Up boosts — upward
    compression below the threshold (quiet parts are lifted), upward expansion above it.
*/
inline Direction thresholdSideFor (bool upward, double ratio) noexcept
{
    return upward == (ratio >= 1.0) ? Direction::Below : Direction::Above;
}

/** One-pole coefficient for a time constant (63 % of a step after `seconds`). */
inline double onePoleCoefficient (double seconds, double sampleRate) noexcept
{
    return seconds > 0.0 ? std::exp (-1.0 / (seconds * sampleRate)) : 0.0;
}

/**
    Decoupled attack/release smoothing of a gain change in dB. Attack is the response to a rising
    level and release to a falling one, whichever way the band acts: compression (slope c < 0) takes
    gain away as the level rises — more cut above the threshold, less boost below it — expansion
    (c > 0) adds gain. So a downward compressor cuts with the attack, an upward compressor takes its
    boost back with the attack (a loud onset is not lifted) and a gate opens with the attack.
    Attack and release are time constants.
*/
struct GainSmoother
{
    double attackCoeff = 0.0;
    double releaseCoeff = 0.0;
    double state = 0.0;

    void setTimes (double attackSeconds, double releaseSeconds, double updateRate) noexcept
    {
        attackCoeff = onePoleCoefficient (attackSeconds, updateRate);
        releaseCoeff = onePoleCoefficient (releaseSeconds, updateRate);
    }

    /** `slope` is the gain computer's c = 1/R − 1: a rising level moves the gain in its sign. */
    double process (double targetDb, double slope) noexcept
    {
        const double a = (targetDb - state) * slope > 0.0 ? attackCoeff : releaseCoeff;
        state = a * state + (1.0 - a) * targetDb;
        return state;
    }
};

/**
    Mean-square detector with two cascaded one-pole smoothers (each with half the time constant):
    the same response time as one pole but far less ripple at twice the signal frequency, which
    would otherwise bias the attack/release smoothing. level() is in dBFS with a full-scale sine
    reading 0 dB.
*/
struct RmsDetector
{
    double coeff = 0.0;
    double stage1 = 0.0;
    double meanSquare = 0.0;

    void setTime (double seconds, double sampleRate) noexcept { coeff = onePoleCoefficient (0.5 * seconds, sampleRate); }

    void process (double x) noexcept
    {
        stage1 = coeff * stage1 + (1.0 - coeff) * x * x;
        meanSquare = coeff * meanSquare + (1.0 - coeff) * stage1;
    }

    double levelDb() const noexcept
    {
        const double power = 2.0 * meanSquare;
        return power > 1.0e-20 ? 10.0 * std::log10 (power) : kSilenceDb;
    }
};

/** Detector time constant for a band: about two periods of its frequency, at least 2 ms. */
inline double detectorTimeFor (double bandHz) noexcept
{
    return std::max (0.002, 2.0 / std::max (bandHz, 1.0));
}
} // namespace ssc::dynamics
