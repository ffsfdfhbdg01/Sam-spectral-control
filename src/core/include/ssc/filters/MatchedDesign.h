#pragma once

#include "ssc/filters/Sections.h"

namespace ssc::filters
{
/**
    Digital section whose magnitude response matches an analog prototype (no bilinear cramping).

    Poles: impulse-invariant mapping of the analog poles (z = e^{sT}), so resonances keep their
    frequency and damping up to Nyquist. Zeros: chosen so that |H(e^{jω})|² equals |Ha(jΩ)|² at DC,
    at Nyquist and at matchHz (second order) — the magnitude-squared of a quadratic numerator is
    linear in three coefficients, which makes the fit a 3×3 linear problem (approach of
    M. Vicanek, "Matched Second Order Digital Filters", 2016). First-order sections match DC and
    Nyquist. The numerator is always minimum phase.

    Analog poles above Nyquist cannot be mapped; their frequency is limited to 0.49·fs.
*/
Biquad matchSection (const AnalogSection& analog, double sampleRate, double matchHz) noexcept;

/** Notch with exact zeros on the unit circle at notchHz and matched poles; unity gain at DC. */
Biquad matchedNotch (double notchHz, double q, double sampleRate) noexcept;

/** Second-order all-pass with matched poles and mirrored zeros (|H| = 1 exactly). */
Biquad matchedAllPass (double hz, double q, double sampleRate) noexcept;
} // namespace ssc::filters
