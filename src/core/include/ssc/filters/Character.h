#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "ssc/engine/Types.h"

namespace ssc::filters
{
/** Nonlinearity of a band: the Characters, and the soft/hard variants of the Analog Feel models. */
enum class ShaperKind : std::uint8_t
{
    None,
    Warm,
    Sharp,
    NSoft,
    NHard,
    PSoft,
    PHard,
    SSoft,
    SHard
};

inline constexpr int kNumShaperKinds = 9;

/**
    The nonlinearity a band uses (research §9c, CHR-4): its Character in Minimum and Natural Phase;
    in Analog Feel the Character picks the model's variant — Clear: the model's curves only,
    Warm: the soft variant, Sharp: the hard one.
*/
constexpr ShaperKind shaperKindFor (Character character, std::optional<AnalogModel> analog) noexcept
{
    if (character == Character::Clear)
        return ShaperKind::None;
    if (! analog.has_value())
        return character == Character::Warm ? ShaperKind::Warm : ShaperKind::Sharp;
    const bool hard = character == Character::Sharp;
    switch (*analog)
    {
        case AnalogModel::N: return hard ? ShaperKind::NHard : ShaperKind::NSoft;
        case AnalogModel::P: return hard ? ShaperKind::PHard : ShaperKind::PSoft;
        case AnalogModel::S: return hard ? ShaperKind::SHard : ShaperKind::SSoft;
    }
    return ShaperKind::None;
}

/**
    Band nonlinearity (research §9c, CHR-2, CHR-3, ADR-0009).

    The contribution u of a band (output − input of its shaped sections) passes through a smooth
    polynomial soft clipper S(u) = u + c₂u² + c₃u³ + c₅u⁵ between the points where S′(u) = 0 and
    constant beyond them. Only the distortion products N(u) = S(u) − u are added to the band's
    linear output, so
    - the linear response is exactly that of the design and a band at 0 dB adds nothing,
    - N and its antiderivative are polynomials: cheap and well conditioned at any level.
    The products are anti-aliased with first-order ADAA (Parker, Zavalishin & Le Bivic, DAFx-16;
    Bilbao et al., IEEE SPL 2017); its half-sample delay applies to the products only.

    Optional, per kind: LF emphasis (the products of u + k·LP(u): saturation that grows in the bass,
    as in a transformer core), a DC blocker (asymmetric curves) and a one-pole low-pass on the products.

    Characters: Warm — asymmetric, H2 with H3 20 dB lower, products DC-blocked and low-passed at
    12 kHz; Sharp — symmetric, odd harmonics. Calibration (CHR-3): sine at 0 dBFS at the centre of a
    +6 dB bell (u ≈ 1) → THD ≈ 0.25 %.
    Analog Feel (0 dBFS = +22 dBu, −18 dBFS = +4 dBu): N — class-A amplifier asymmetry (H2 + H3)
    and a transformer that saturates more in the bass; P — tube stage, dominant H2, soft compression
    at high levels; S — op-amp, clean until close to its rails, then odd harmonics (u⁵).
*/
struct Shaper
{
    bool enabled = false;
    double c2 = 0.0, c3 = 0.0, c5 = 0.0; ///< N(u) inside the polynomial region
    double i3 = 0.0, i4 = 0.0, i6 = 0.0; ///< ∫N = i₃u³ + i₄u⁴ + i₆u⁶ there (c₂/3, c₃/4, c₅/6)
    bool quintic = false;                ///< c₅ ≠ 0
    double uLow = -1.0, uHigh = 1.0;     ///< the polynomial region in u
    double uInner = 1.0;                 ///< min(−uLow, uHigh): |u| ≤ uInner is inside the region
    double sLow = 0.0, sHigh = 0.0;      ///< S(u) beyond the region (clip levels)
    double fLow = 0.0, fHigh = 0.0;      ///< ∫S from 0 to the region ends
    double lfEmphasis = 0.0;             ///< k of u + k·LP(u)
    double lfPole = 0.0;                 ///< pole of that one-pole low-pass
    bool dcBlock = false;
    double dcPole = 0.0;
    bool lowPass = false;
    double lowPassPole = 0.0;
};

/** Shaper of a kind at a sample rate (None: disabled). */
Shaper makeShaper (ShaperKind kind, double sampleRate) noexcept;

struct ShaperState
{
    double previous = 0.0; ///< previous input of N (ADAA)
    double lf = 0.0;       ///< LF emphasis low-pass
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
    const double u2 = u * u;
    return u2 * (s.c2 + u * (s.c3 + s.c5 * u2));
}

/** ∫₀ᵘ N. */
inline double shaperIntegral (const Shaper& s, double u) noexcept
{
    if (u > s.uHigh)
        return s.fHigh + s.sHigh * (u - s.uHigh) - 0.5 * u * u;
    if (u < s.uLow)
        return s.fLow + s.sLow * (u - s.uLow) - 0.5 * u * u;
    const double u3 = u * u * u;
    return u3 * (s.i3 + u * (s.i4 + s.i6 * u * u));
}

/** First-order ADAA of N between the previous input w and u: (∫N(u) − ∫N(w)) / (u − w). */
inline double shaperAntialiased (const Shaper& s, double u, double w) noexcept
{
    if (std::max (std::abs (u), std::abs (w)) <= s.uInner)
    {
        // Exact quotients of the monomials: (u³ − w³)/(u − w), (u⁴ − w⁴)/(u − w), (u⁶ − w⁶)/(u − w).
        const double squares = u * u + w * w;
        const double sum = u + w;
        const double cubic = s.i3 * (squares + u * w) + s.i4 * sum * squares;
        return s.quintic ? cubic + s.i6 * sum * (squares * squares - u * u * w * w) : cubic;
    }
    const double d = u - w;
    if (std::abs (d) > 1.0e-6 * (std::abs (u) + std::abs (w)))
        return (shaperIntegral (s, u) - shaperIntegral (s, w)) / d;
    return shaperProducts (s, 0.5 * (u + w));
}

/** Distortion products to add to the band output for the contribution u. */
inline double shapeContribution (const Shaper& s, ShaperState& st, double u) noexcept
{
    double x = u;
    if (s.lfEmphasis > 0.0)
    {
        st.lf = u + s.lfPole * (st.lf - u);
        x = u + s.lfEmphasis * st.lf;
    }
    double n = shaperAntialiased (s, x, st.previous);
    st.previous = x;
    if (s.dcBlock)
    {
        const double high = n - st.dcIn + s.dcPole * st.dcOut;
        st.dcIn = n;
        st.dcOut = high;
        n = high;
    }
    if (s.lowPass)
    {
        st.lowPass = n + s.lowPassPole * (st.lowPass - n);
        n = st.lowPass;
    }
    return n;
}
} // namespace ssc::filters
