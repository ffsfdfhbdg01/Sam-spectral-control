#include "ssc/analysis/PitchTracker.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::analysis
{
void PitchTracker::prepare (double sampleRate, int window, double minHz, double maxHz, double yinThreshold)
{
    fs = sampleRate;
    w = std::max (64, window);
    threshold = yinThreshold;
    tauMin = std::max (2, static_cast<int> (std::floor (fs / maxHz)));
    tauMax = std::min (w - 1, static_cast<int> (std::ceil (fs / minHz)));
    n = nextPowerOfTwo (4 * w);
    fft.prepare (n);

    const auto size = static_cast<std::size_t> (n);
    a.assign (size, 0.0f);
    b.assign (size, 0.0f);
    specA.assign (size + 2, 0.0f);
    specB.assign (size + 2, 0.0f);
    product.assign (size + 2, 0.0f);
    correlation.assign (size, 0.0f);
    energy.assign (static_cast<std::size_t> (2 * w + 1), 0.0);
    difference.assign (static_cast<std::size_t> (w), 0.0);
}

PitchTracker::Result PitchTracker::analyse (const float* frame) noexcept
{
    // r(τ) = Σ_{j<W} x[j]·x[j+τ]: correlation of the first W samples with the whole frame.
    std::fill (a.begin(), a.end(), 0.0f);
    std::fill (b.begin(), b.end(), 0.0f);
    std::copy_n (frame, w, a.begin());
    std::copy_n (frame, 2 * w, b.begin());
    fft.forward (a.data(), specA.data());
    fft.forward (b.data(), specB.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        const auto re = static_cast<std::size_t> (2 * k), im = re + 1;
        // conj(A)·B
        product[re] = specA[re] * specB[re] + specA[im] * specB[im];
        product[im] = specA[re] * specB[im] - specA[im] * specB[re];
    }
    fft.inverse (product.data(), correlation.data());

    // Energies of the windows starting at τ (cumulative sums).
    energy[0] = 0.0;
    for (int j = 0; j < 2 * w; ++j)
        energy[static_cast<std::size_t> (j + 1)] = energy[static_cast<std::size_t> (j)] + static_cast<double> (frame[j]) * frame[j];
    const double e0 = energy[static_cast<std::size_t> (w)];
    if (e0 < 1.0e-12)
        return {};

    // d(τ) and the cumulative mean normalised difference d'(τ).
    double running = 0.0;
    difference[0] = 1.0;
    for (int tau = 1; tau < w; ++tau)
    {
        const double eTau = energy[static_cast<std::size_t> (tau + w)] - energy[static_cast<std::size_t> (tau)];
        const double d = std::max (0.0, e0 + eTau - 2.0 * correlation[static_cast<std::size_t> (tau)]);
        running += d;
        difference[static_cast<std::size_t> (tau)] = running > 0.0 ? d * tau / running : 1.0;
    }

    // First dip below the threshold (its local minimum), else the global minimum.
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (difference[static_cast<std::size_t> (tau)] < threshold)
        {
            while (tau + 1 <= tauMax && difference[static_cast<std::size_t> (tau + 1)] < difference[static_cast<std::size_t> (tau)])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (difference[static_cast<std::size_t> (tau)] < difference[static_cast<std::size_t> (best)])
                best = tau;
    }

    double period = best;
    if (best > 1 && best < w - 1)
    {
        const double ym = difference[static_cast<std::size_t> (best - 1)];
        const double y0 = difference[static_cast<std::size_t> (best)];
        const double yp = difference[static_cast<std::size_t> (best + 1)];
        const double denominator = ym - 2.0 * y0 + yp;
        if (denominator > 0.0)
            period += std::clamp (0.5 * (ym - yp) / denominator, -0.5, 0.5);
    }
    return { fs / period, difference[static_cast<std::size_t> (best)] };
}
} // namespace ssc::analysis
