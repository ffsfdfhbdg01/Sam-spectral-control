#include "ssc/loudness/Loudness.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ssc/spectral/SpectralEngine.h"

namespace ssc::loudness
{
namespace
{
    // ISO 226:2003, Table 1: frequency, exponent αf, magnitude of the linear transfer function
    // normalised at 1 kHz (LU) and threshold of hearing (Tf).
    constexpr std::array<double, 29> kFreq { 20,  25,  31.5, 40,   50,   63,   80,   100,  125,  160,  200,  250,  315,   400,  500,
                                             630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    constexpr std::array<double, 29> kAlpha { 0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330,
                                              0.315, 0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244,
                                              0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301 };
    constexpr std::array<double, 29> kLu { -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1, -2.0,  -1.1,  -0.4, 0.0,
                                           0.3,   0.5,   0.0,   -2.7,  -4.1,  -1.0,  1.7,   2.5,  1.2,  -2.1, -7.1, -11.2, -10.7, -3.1 };
    constexpr std::array<double, 29> kTf { 78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4, 8.6,  6.2, 4.4,
                                           3.0,  2.2,  2.4,  3.5,  1.7,  -1.3, -4.2, -6.0, -5.4, -1.5, 6.0,  12.6, 13.9, 12.3 };

    struct Contour
    {
        double alpha, lu, tf;
    };

    /** Table values interpolated on a log-frequency axis; constant beyond 20 Hz and 12.5 kHz. */
    Contour contourAt (double hz) noexcept
    {
        if (hz <= kFreq.front())
            return { kAlpha.front(), kLu.front(), kTf.front() };
        if (hz >= kFreq.back())
            return { kAlpha.back(), kLu.back(), kTf.back() };
        std::size_t i = 0;
        while (kFreq[i + 1] < hz)
            ++i;
        const double t = std::log (hz / kFreq[i]) / std::log (kFreq[i + 1] / kFreq[i]);
        auto lerp = [t] (double a, double b) { return a + (b - a) * t; };
        return { lerp (kAlpha[i], kAlpha[i + 1]), lerp (kLu[i], kLu[i + 1]), lerp (kTf[i], kTf[i + 1]) };
    }
} // namespace

double phonToSpl (double phon, double hz) noexcept
{
    const auto c = contourAt (hz);
    const double af =
        4.47e-3 * (std::pow (10.0, 0.025 * phon) - 1.15) + std::pow (0.4 * std::pow (10.0, (c.tf + c.lu) / 10.0 - 9.0), c.alpha);
    return 10.0 / c.alpha * std::log10 (std::max (af, 1.0e-30)) - c.lu + 94.0;
}

double splToPhon (double splDb, double hz) noexcept
{
    const auto c = contourAt (hz);
    const double bf = std::pow (0.4 * std::pow (10.0, (splDb + c.lu) / 10.0 - 9.0), c.alpha)
        - std::pow (0.4 * std::pow (10.0, (c.tf + c.lu) / 10.0 - 9.0), c.alpha) + 0.005135;
    return bf > 0.0 ? 40.0 * std::log10 (bf) + 94.0 : 0.0;
}

double phonToSone (double phon) noexcept
{
    if (phon >= 40.0)
        return std::pow (2.0, (phon - 40.0) / 10.0);
    return phon > 0.0 ? std::pow (phon / 40.0, 2.642) : 0.0;
}

double soneToPhon (double sone) noexcept
{
    if (sone >= 1.0)
        return 40.0 + 10.0 * std::log2 (sone);
    return sone > 0.0 ? 40.0 * std::pow (sone, 1.0 / 2.642) : 0.0;
}

void ErbBands::prepare (double sampleRate, int size)
{
    fftSize = size;
    centres.clear();
    firstBin.clear();
    lastBin.clear();

    const double binHz = sampleRate / size;
    const double lowErb = spectral::erbNumber (20.0);
    const double highErb = spectral::erbNumber (std::min (20000.0, 0.45 * sampleRate));
    for (double erb = lowErb; erb + 1.0 <= highErb + 1.0e-9; erb += 1.0)
    {
        const int first = std::max (1, static_cast<int> (std::ceil (spectral::erbToHz (erb) / binHz)));
        const int last = std::min (size / 2, static_cast<int> (std::floor (spectral::erbToHz (erb + 1.0) / binHz)));
        if (last < first)
            continue; // narrower than a bin at low frequencies: merged into the next band
        centres.push_back (spectral::erbToHz (erb + 0.5));
        firstBin.push_back (first);
        lastBin.push_back (last);
    }
}

void ErbBands::accumulate (const float* spectrum, double windowPower, double weight, double* powersOut) const noexcept
{
    // Mean square of the signal in the band: Σ |X|² · 2 / (N · Σ w²) over the positive bins.
    const double scale = weight * 2.0 / (static_cast<double> (fftSize) * windowPower);
    for (std::size_t b = 0; b < centres.size(); ++b)
    {
        double sum = 0.0;
        for (int k = firstBin[b]; k <= lastBin[b]; ++k)
        {
            const double re = spectrum[2 * k];
            const double im = spectrum[2 * k + 1];
            sum += re * re + im * im;
        }
        powersOut[b] += sum * scale;
    }
}

double loudnessOf (const double* bandPowers, const ErbBands& bands, AutoGainWeighting weighting) noexcept
{
    const int n = bands.numBands();
    if (weighting == AutoGainWeighting::Digital)
    {
        double total = 0.0;
        for (int b = 0; b < n; ++b)
            total += bandPowers[b];
        return total > 1.0e-30 ? 10.0 * std::log10 (total) : -300.0;
    }

    double sones = 0.0;
    for (int b = 0; b < n; ++b)
    {
        const double power = bandPowers[b];
        if (power <= 1.0e-30)
            continue;
        // dBFS of a sine with this mean square (full scale sine = 0.5) → SPL → loudness level.
        const double spl = 10.0 * std::log10 (2.0 * power) + kSplAtFullScale;
        sones += phonToSone (splToPhon (spl, bands.centreHz (b)));
    }
    return soneToPhon (sones);
}
} // namespace ssc::loudness
