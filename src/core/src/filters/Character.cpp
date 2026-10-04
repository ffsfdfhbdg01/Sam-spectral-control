#include "ssc/filters/Character.h"

#include <algorithm>

#include "ssc/util/Math.h"

namespace ssc::filters
{
namespace
{
    struct Spec
    {
        double c2, c3, c5;
        double lfEmphasis, lfHz;
        double lowPassHz; ///< 0: none
        bool dcBlock;
    };

    /**
        Coefficients per kind (THD figures for a sine at the centre of a +6 dB bell, u ≈ amplitude):
        Warm/Sharp — 0.25 % at 0 dBFS (CHR-3). Analog Feel, soft (Warm) variant, at −18 dBFS (+4 dBu) /
        0 dBFS (+22 dBu): N ≈ −66 / −43 dB with more in the bass, P ≈ −56 / −38 dB (H2), S ≈ −106 / −50 dB
        (u⁵: clean at nominal level, rising steeply towards the rails); hard (Sharp) variants 6–10 dB more.
    */
    Spec specFor (ShaperKind kind) noexcept
    {
        switch (kind)
        {
            case ShaperKind::None: break;
            case ShaperKind::Warm: return { -0.129 * 0.0775, -0.0775 * 0.0775 / 3.0, 0.0, 0.0, 0.0, 12000.0, true };
            case ShaperKind::Sharp: return { 0.0, -0.245 * 0.245 / 3.0, 0.0, 0.0, 0.0, 0.0, false };
            case ShaperKind::NSoft: return { 0.016, -0.05, 0.0, 1.0, 120.0, 18000.0, true };
            case ShaperKind::NHard: return { 0.03, -0.12, 0.0, 1.5, 120.0, 18000.0, true };
            case ShaperKind::PSoft: return { 0.05, -0.03, 0.0, 0.0, 0.0, 15000.0, true };
            case ShaperKind::PHard: return { 0.1, -0.08, 0.0, 0.0, 0.0, 15000.0, true };
            case ShaperKind::SSoft: return { 0.0, -0.002, -0.02, 0.0, 0.0, 0.0, false };
            case ShaperKind::SHard: return { 0.0, -0.006, -0.06, 0.0, 0.0, 0.0, false };
        }
        return { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false };
    }

    /** First zero of S′(u) = 1 + 2c₂u + 3c₃u² + 5c₅u⁴ in the direction of `sign` (bisection). */
    double edge (const Spec& p, double sign) noexcept
    {
        auto slope = [&p] (double u) { return 1.0 + 2.0 * p.c2 * u + 3.0 * p.c3 * u * u + 5.0 * p.c5 * u * u * u * u; };
        double high = 1.0;
        while (slope (sign * high) > 0.0 && high < 1.0e6)
            high *= 2.0;
        double low = 0.0;
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (low + high);
            (slope (sign * mid) > 0.0 ? low : high) = mid;
        }
        return sign * 0.5 * (low + high);
    }
} // namespace

Shaper makeShaper (ShaperKind kind, double sampleRate) noexcept
{
    Shaper s;
    if (kind == ShaperKind::None)
        return s;

    const auto p = specFor (kind);
    s.enabled = true;
    s.c2 = p.c2;
    s.c3 = p.c3;
    s.c5 = p.c5;
    s.i3 = p.c2 / 3.0;
    s.i4 = p.c3 / 4.0;
    s.i6 = p.c5 / 6.0;
    s.quintic = p.c5 < 0.0 || p.c5 > 0.0;
    s.uLow = edge (p, -1.0);
    s.uHigh = edge (p, 1.0);
    s.uInner = std::min (-s.uLow, s.uHigh);

    // Inside the region N is the polynomial; its value and integral at the edges continue S beyond.
    const Shaper inside = [&] {
        Shaper t = s;
        t.uLow = -1.0e300;
        t.uHigh = 1.0e300;
        return t;
    }();
    s.sLow = s.uLow + shaperProducts (inside, s.uLow);
    s.sHigh = s.uHigh + shaperProducts (inside, s.uHigh);
    s.fLow = 0.5 * s.uLow * s.uLow + shaperIntegral (inside, s.uLow);
    s.fHigh = 0.5 * s.uHigh * s.uHigh + shaperIntegral (inside, s.uHigh);

    s.lfEmphasis = p.lfEmphasis;
    s.lfPole = p.lfEmphasis > 0.0 ? std::exp (-kTwoPi * p.lfHz / sampleRate) : 0.0;
    s.dcBlock = p.dcBlock;
    s.dcPole = std::exp (-kTwoPi * 5.0 / sampleRate);
    s.lowPass = p.lowPassHz > 0.0;
    s.lowPassPole = s.lowPass ? std::exp (-kTwoPi * std::min (p.lowPassHz, 0.45 * sampleRate) / sampleRate) : 0.0;
    return s;
}
} // namespace ssc::filters
