#pragma once

#include <algorithm>
#include <cmath>

#include "ssc/engine/Types.h"

namespace ssc::filters
{
/**
    Nonlinearity of the Warm and Sharp Characters (research §9c, CHR-2, CHR-3).

    The contribution u of a band (output − input of its shaped sections) passes through
    S(u) = P(a·u)/a with P(v) = v + p₂·v² + p₃·v³ between the points where P′(v) = 0 and constant
    beyond them: a smooth polynomial soft clipper. Only the distortion products N(u) = S(u) − u
    are added to the band's linear output, so
    - the linear response is exactly that of the design and a band at 0 dB adds nothing,
    - N and its antiderivative are low-order polynomials: cheap and well conditioned at any level.
    The products are anti-aliased with first-order ADAA (Parker, Zavalishin & Le Bivic, DAFx-16;
    Bilbao et al., IEEE SPL 2017); its half-sample delay applies to the products only.

    Warm: asymmetric (p₂ < 0) — mostly H2 with some H3, clipping softer on one side; the products
    are high-passed at 5 Hz (the even term carries DC) and low-passed at 12 kHz.
    Sharp: symmetric (p₂ = 0) — odd harmonics only, unfiltered.
    Calibration (CHR-3): sine at 0 dBFS at the centre of a +6 dB bell (u ≈ 1) → THD ≈ 0.25 %.
*/
struct Shaper
{
    bool enabled = false;
    bool warm = false;
    double c2 = 0.0, c3 = 0.0;       ///< N(u) = c₂·u² + c₃·u³ inside the polynomial region
    double alpha = 0.0, beta = 0.0;  ///< ∫N = α·u³ + β·u⁴ inside the region
    double uLow = -1.0, uHigh = 1.0; ///< the polynomial region in u
    double uInner = 1.0;             ///< min(−uLow, uHigh): |u| ≤ uInner is inside the region
    double sLow = 0.0, sHigh = 0.0;  ///< S(u) beyond the region (clip levels)
    double fLow = 0.0, fHigh = 0.0;  ///< ∫S from 0 to the region ends
    double dcPole = 0.0;             ///< DC blocker on the products (Warm)
    double lowPassPole = 0.0;        ///< one-pole low-pass on the products (Warm)
};

/** Shaper of a Character at a sample rate (Clear: disabled). */
Shaper makeShaper (Character character, double sampleRate) noexcept;

struct ShaperState
{
    double previous = 0.0; ///< previous u (ADAA)
    double dcIn = 0.0, dcOut = 0.0;
    double lowPass = 0.0;
};

/** N(u) = S(u) − u. */
inline double shaperProducts (const Shaper& s, double u) noexcept
{
    if (u > s.uHigh)
        return s.sHigh - u;
    if (u < s.uLow)
        return s.sLow - u;
    return u * u * (s.c2 + s.c3 * u);
}

/** ∫₀ᵘ N. */
inline double shaperIntegral (const Shaper& s, double u) noexcept
{
    if (u > s.uHigh)
        return s.fHigh + s.sHigh * (u - s.uHigh) - 0.5 * u * u;
    if (u < s.uLow)
        return s.fLow + s.sLow * (u - s.uLow) - 0.5 * u * u;
    return u * u * u * (s.alpha + s.beta * u);
}

/** First-order ADAA of N between the previous sample w and u: (∫N(u) − ∫N(w)) / (u − w). */
inline double shaperAntialiased (const Shaper& s, double u, double w) noexcept
{
    if (std::max (std::abs (u), std::abs (w)) <= s.uInner)
    {
        const double squares = u * u + w * w;
        return s.alpha * (squares + u * w) + s.beta * (u + w) * squares; // exact quotient
    }
    const double d = u - w;
    if (std::abs (d) > 1.0e-6 * (std::abs (u) + std::abs (w)))
        return (shaperIntegral (s, u) - shaperIntegral (s, w)) / d;
    return shaperProducts (s, 0.5 * (u + w));
}

/** Distortion products to add to the band output for the contribution u. */
inline double shapeContribution (const Shaper& s, ShaperState& st, double u) noexcept
{
    double n = shaperAntialiased (s, u, st.previous);
    st.previous = u;
    if (s.warm)
    {
        const double high = n - st.dcIn + s.dcPole * st.dcOut;
        st.dcIn = n;
        st.dcOut = high;
        st.lowPass = high + s.lowPassPole * (st.lowPass - high);
        n = st.lowPass;
    }
    return n;
}
} // namespace ssc::filters
