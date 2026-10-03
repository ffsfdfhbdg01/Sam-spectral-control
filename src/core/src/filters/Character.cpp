#include "ssc/filters/Character.h"

#include <algorithm>

#include "ssc/util/Math.h"

namespace ssc::filters
{
Shaper makeShaper (Character character, double sampleRate) noexcept
{
    Shaper s;
    if (character == Character::Clear)
        return s;

    // P(v) = v + p2·v² + p3·v³ and the drive a; calibrated for THD ≈ 0.25 % at u ≈ 1 (CHR-3):
    // Warm: H2 = |p2|·a/2, H3 = a²/12 (−20 dB below H2); Sharp: H3 = a²/12.
    const bool warm = character == Character::Warm;
    const double p2 = warm ? -0.129 : 0.0;
    const double p3 = -1.0 / 3.0;
    const double a = warm ? 0.0775 : 0.245;

    // P′(v) = 1 + 2·p2·v − v² = 0 at v = p2 ± √(p2² + 1).
    const double root = std::sqrt (p2 * p2 + 1.0);
    const double vHigh = p2 + root;
    const double vLow = p2 - root;
    auto poly = [&] (double v) { return v + p2 * v * v + p3 * v * v * v; };
    auto integral = [&] (double v) { return 0.5 * v * v + p2 * v * v * v / 3.0 + p3 * v * v * v * v / 4.0; };

    s.enabled = true;
    s.warm = warm;
    s.c2 = p2 * a;
    s.c3 = p3 * a * a;
    s.alpha = s.c2 / 3.0;
    s.beta = s.c3 / 4.0;
    s.uLow = vLow / a;
    s.uHigh = vHigh / a;
    s.uInner = std::min (-s.uLow, s.uHigh);
    s.sLow = poly (vLow) / a;
    s.sHigh = poly (vHigh) / a;
    s.fLow = integral (vLow) / (a * a);
    s.fHigh = integral (vHigh) / (a * a);
    s.dcPole = std::exp (-kTwoPi * 5.0 / sampleRate);
    s.lowPassPole = std::exp (-kTwoPi * std::min (12000.0, 0.45 * sampleRate) / sampleRate);
    return s;
}
} // namespace ssc::filters
