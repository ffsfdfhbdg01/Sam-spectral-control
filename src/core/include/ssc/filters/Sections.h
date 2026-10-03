#pragma once

#include <cmath>
#include <complex>

namespace ssc::filters
{
/**
    Analog prototype section H(s) = (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0), with s in rad/s.
    A first-order section has n2 = d2 = 0.
*/
struct AnalogSection
{
    double n0 = 1.0, n1 = 0.0, n2 = 0.0;
    double d0 = 1.0, d1 = 0.0, d2 = 0.0;

    bool isFirstOrder() const noexcept { return std::fpclassify (d2) == FP_ZERO; }

    std::complex<double> response (double omega) const noexcept
    {
        const std::complex<double> s (0.0, omega);
        return (n2 * s * s + n1 * s + n0) / (d2 * s * s + d1 * s + d0);
    }

    double magnitudeSquared (double omega) const noexcept
    {
        const double w2 = omega * omega;
        const double nr = n0 - n2 * w2, ni = n1 * omega;
        const double dr = d0 - d2 * w2, di = d1 * omega;
        return (nr * nr + ni * ni) / (dr * dr + di * di);
    }
};

/** Digital section H(z) = (b0 + b1 z⁻¹ + b2 z⁻²) / (1 + a1 z⁻¹ + a2 z⁻²); first order when b2 = a2 = 0. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    bool firstOrder = false;

    /** omega in radians per sample. */
    std::complex<double> response (double omega) const noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -omega);
        const std::complex<double> z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
};

/**
    Parameters of a topology-preserving (trapezoidal) state-variable section.

    Second order: outputs v1 (band-pass s/(s²+ks+1)) and v2 (low-pass 1/(s²+ks+1)) of a prewarped
    SVF with cutoff parameter g; y = m0·x + m1·v1 + m2·v2.
    First order: one-pole low-pass with cutoff parameter g; y = m0·x + m1·lp.
*/
struct SvfParams
{
    double g = 1.0, k = 1.4142135623730951;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

/** Converts any stable digital section into equivalent SVF parameters (exact same transfer function). */
SvfParams toSvf (const Biquad& bq) noexcept;
} // namespace ssc::filters
