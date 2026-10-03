#include "ssc/filters/MatchedDesign.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::filters
{
namespace
{
    constexpr double kMaxPoleFraction = 0.49; // of the sample rate

    struct Denominator
    {
        double a1 = 0.0, a2 = 0.0;
    };

    /** Impulse-invariant mapping of the analog denominator d2 s² + d1 s + d0. */
    Denominator mapPoles (const AnalogSection& a, double sampleRate) noexcept
    {
        const double t = 1.0 / sampleRate;
        const double maxOmega = kTwoPi * kMaxPoleFraction * sampleRate;

        if (a.isFirstOrder())
        {
            const double pole = -a.d0 / a.d1; // real pole, negative for a stable section
            const double limited = std::max (pole, -maxOmega);
            return { -std::exp (limited * t), 0.0 };
        }

        // Natural frequency and damping of s² + (d1/d2) s + d0/d2.
        const double w0 = std::sqrt (a.d0 / a.d2);
        const double zeta = (a.d1 / a.d2) / (2.0 * w0);
        const double w = std::min (w0, maxOmega);

        if (zeta < 1.0)
        {
            const double wd = w * std::sqrt (1.0 - zeta * zeta);
            const double r = std::exp (-zeta * w * t);
            return { -2.0 * r * std::cos (wd * t), r * r };
        }

        // Over-damped: two real poles.
        const double root = std::sqrt (zeta * zeta - 1.0);
        const double p1 = std::exp (-w * (zeta - root) * t);
        const double p2 = std::exp (-w * (zeta + root) * t);
        return { -(p1 + p2), p1 * p2 };
    }
} // namespace

SvfParams toSvf (const Biquad& bq) noexcept
{
    SvfParams p;

    if (bq.firstOrder)
    {
        // H = n0/(1+s) + n1·s/(1+s) with s = p/g, p = (z−1)/(z+1).
        const double g = (1.0 + bq.a1) / (1.0 - bq.a1);
        const double n0 = (bq.b0 + bq.b1) / (1.0 + bq.a1);
        const double n1 = (bq.b0 - bq.b1) / (1.0 - bq.a1);
        p.g = g;
        p.k = 0.0;
        p.m0 = n1;
        p.m1 = n0 - n1;
        p.m2 = 0.0;
        return p;
    }

    const double A0 = 1.0 + bq.a1 + bq.a2;
    const double A1 = 2.0 * (1.0 - bq.a2);
    const double A2 = 1.0 - bq.a1 + bq.a2;
    const double B0 = bq.b0 + bq.b1 + bq.b2;
    const double B1 = 2.0 * (bq.b0 - bq.b2);
    const double B2 = bq.b0 - bq.b1 + bq.b2;

    p.g = std::sqrt (A0 / A2);
    p.k = A1 / std::sqrt (A0 * A2);
    const double n0 = B0 / A0;
    const double n1 = B1 * p.g / A0;
    const double n2 = B2 / A2;
    p.m0 = n2;
    p.m1 = n1 - n2 * p.k;
    p.m2 = n0 - n2;
    return p;
}

Biquad matchSection (const AnalogSection& analog, double sampleRate, double matchHz) noexcept
{
    const auto den = mapPoles (analog, sampleRate);
    const double nyquistOmega = kPi * sampleRate;

    Biquad bq;
    bq.a1 = den.a1;
    bq.a2 = den.a2;

    if (analog.isFirstOrder())
    {
        // First order: exact at DC and at the characteristic frequency (|H|² = (B0 φ0 + B1 φ1)/(A0 φ0 + A1 φ1)).
        bq.firstOrder = true;
        const double A0 = (1.0 + bq.a1) * (1.0 + bq.a1);
        const double A1 = (1.0 - bq.a1) * (1.0 - bq.a1);
        const double omega = kTwoPi * std::clamp (matchHz, 1.0e-4 * sampleRate, 0.45 * sampleRate) / sampleRate;
        const double phi1 = std::pow (std::sin (0.5 * omega), 2.0);
        const double phi0 = 1.0 - phi1;
        const double B0 = A0 * analog.magnitudeSquared (0.0);
        const double B1 = (analog.magnitudeSquared (omega * sampleRate) * (A0 * phi0 + A1 * phi1) - B0 * phi0) / phi1;
        const double r0 = std::sqrt (std::max (0.0, B0));
        const double r1 = std::sqrt (std::max (0.0, B1));
        bq.b0 = 0.5 * (r0 + r1);
        bq.b1 = 0.5 * (r0 - r1);
        return bq;
    }

    // |D(e^jω)|² = A0 φ0 + A1 φ1 + A2 φ2 with φ1 = sin²(ω/2), φ0 = 1 − φ1, φ2 = 4 φ0 φ1.
    const double A0 = (1.0 + bq.a1 + bq.a2) * (1.0 + bq.a1 + bq.a2);
    const double A1 = (1.0 - bq.a1 + bq.a2) * (1.0 - bq.a1 + bq.a2);
    const double A2 = -4.0 * bq.a2;

    const double matchOmega = kTwoPi * std::clamp (matchHz, 1.0e-4 * sampleRate, 0.48 * sampleRate) / sampleRate;
    const double phi1 = std::pow (std::sin (0.5 * matchOmega), 2.0);
    const double phi0 = 1.0 - phi1;
    const double phi2 = 4.0 * phi0 * phi1;
    const double denominatorAtMatch = A0 * phi0 + A1 * phi1 + A2 * phi2;
    const double target = analog.magnitudeSquared (matchOmega * sampleRate);

    // High-pass sections have a double zero at DC: numerator c·(1 − z⁻¹)², i.e. N(φ) = 4c²·φ1²…
    // matched in level at the characteristic frequency.
    if (analog.n0 == 0.0 && analog.n1 == 0.0)
    {
        const double c = std::sqrt (target * denominatorAtMatch) / (4.0 * phi1);
        bq.b0 = c;
        bq.b1 = -2.0 * c;
        bq.b2 = c;
        return bq;
    }

    const double B0 = A0 * analog.magnitudeSquared (0.0);
    const double B1 = A1 * analog.magnitudeSquared (nyquistOmega);
    double B2 = (target * denominatorAtMatch - B0 * phi0 - B1 * phi1) / phi2;

    const double root0 = std::sqrt (std::max (0.0, B0));
    const double root1 = std::sqrt (std::max (0.0, B1));

    // Minimum-phase factorisation of N(φ) = B0 φ0 + B1 φ1 + B2 φ2 into b0 + b1 z⁻¹ + b2 z⁻².
    // Either sign of N(−1) is possible; take the one that yields real coefficients.
    double w = 0.5 * (root0 + root1);
    double b1 = 0.5 * (root0 - root1);
    if (w * w + B2 < 0.0)
    {
        const double alternative = 0.5 * (root0 - root1);
        if (alternative * alternative + B2 >= 0.0)
        {
            w = alternative;
            b1 = 0.5 * (root0 + root1);
        }
        else
        {
            B2 = -w * w;
        }
    }

    bq.b0 = 0.5 * (w + std::sqrt (std::max (0.0, w * w + B2)));
    bq.b1 = b1;
    bq.b2 = bq.b0 != 0.0 ? -B2 / (4.0 * bq.b0) : 0.0;
    return bq;
}

Biquad matchedNotch (double notchHz, double q, double sampleRate) noexcept
{
    const double w0 = kTwoPi * notchHz;
    AnalogSection poles;
    poles.d2 = 1.0;
    poles.d1 = w0 / q;
    poles.d0 = w0 * w0;
    const auto den = mapPoles (poles, sampleRate);

    const double wd = kTwoPi * std::min (notchHz, 0.49 * sampleRate) / sampleRate;
    Biquad bq;
    bq.a1 = den.a1;
    bq.a2 = den.a2;
    bq.b0 = 1.0;
    bq.b1 = -2.0 * std::cos (wd);
    bq.b2 = 1.0;

    const double dcGain = (bq.b0 + bq.b1 + bq.b2) / (1.0 + bq.a1 + bq.a2);
    const double scale = dcGain != 0.0 ? 1.0 / dcGain : 1.0;
    bq.b0 *= scale;
    bq.b1 *= scale;
    bq.b2 *= scale;
    return bq;
}

Biquad matchedAllPass (double hz, double q, double sampleRate) noexcept
{
    const double w0 = kTwoPi * hz;
    AnalogSection poles;
    poles.d2 = 1.0;
    poles.d1 = w0 / q;
    poles.d0 = w0 * w0;
    const auto den = mapPoles (poles, sampleRate);

    Biquad bq;
    bq.a1 = den.a1;
    bq.a2 = den.a2;
    bq.b0 = den.a2;
    bq.b1 = den.a1;
    bq.b2 = 1.0;
    return bq;
}
} // namespace ssc::filters
