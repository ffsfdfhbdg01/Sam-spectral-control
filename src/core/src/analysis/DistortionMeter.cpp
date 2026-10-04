#include "ssc/analysis/DistortionMeter.h"

#include <algorithm>
#include <cmath>

#include "ssc/util/Math.h"

namespace ssc::analysis
{
namespace
{
    std::vector<double> hann (int size)
    {
        std::vector<double> w (static_cast<std::size_t> (size));
        for (int i = 0; i < size; ++i)
            w[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (kTwoPi * i / size);
        return w;
    }

    /** |H|² of the products filters of a shaper at `fraction` of its sample rate. */
    double productsFilterGain (const filters::Shaper& s, double fraction) noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -kTwoPi * fraction);
        double gain = 1.0;
        if (s.dcBlock)
            gain *= std::norm ((1.0 - z1) / (1.0 - s.dcPole * z1));
        if (s.lowPass)
            gain *= std::norm ((1.0 - s.lowPassPole) / (1.0 - s.lowPassPole * z1));
        return gain;
    }
} // namespace

bool DistortionMeter::isPreparedFor (double baseRate, int factor, oversampling::FilterType type, Quality quality) const noexcept
{
    return std::abs (baseRate - fb) < 0.5 && factor == osFactor && (factor == 1 || (type == osType && quality == osQuality));
}

void DistortionMeter::prepare (double baseRate, int factor, oversampling::FilterType type, Quality quality)
{
    fb = baseRate;
    osFactor = std::max (1, factor);
    osType = type;
    osQuality = quality;
    fp = fb * osFactor;

    // The shadow runs at 4× the base rate or more. A quintic curve of a signal below half the base
    // rate has products up to 2.5× the base rate: those above half the shadow rate fold inside the
    // shadow but stay above fp/2, so they still count as aliasing, with the right power.
    shadowFactor = std::max (2, 4 / osFactor);
    const int capture = engine::BandCapture::lengthFor (osFactor);
    window = engine::BandCapture::windowFor (osFactor);
    const int m = shadowFactor;

    // Images of the interpolation (−90 dB) make products 84 dB below the true ones: far below what
    // the meters show.
    interpolator.prepare (1, capture, m, oversampling::FilterType::LinearPhaseFir, Quality::Normal);
    shadowDelay = static_cast<int> (std::lround (interpolator.getUpsampleDelay()));

    actualFft.prepare (window);
    shadowFft.prepare (window * m);
    actualWindow = hann (window);
    shadowWindow = hann (window * m); // the same window in time
    double sumSquares = 0.0;
    for (const double w : actualWindow)
        sumSquares += w * w;
    windowNorm = 2.0 / (window * sumSquares);

    // Decimation: bins above half the base rate leak into the band attenuated.
    bandBins = window / (2 * osFactor);
    decimation.assign (static_cast<std::size_t> (window / 2 + 1), 1.0);
    if (osFactor > 1)
    {
        oversampling::Oversampler decimator;
        decimator.prepare (1, 16, osFactor, type, quality);
        for (int k = bandBins + 1; k <= window / 2; ++k)
            decimation[static_cast<std::size_t> (k)] = decimator.decimationGain (static_cast<double> (k) / window);
    }

    xs.assign (static_cast<std::size_t> (capture), 0.0);
    actual.assign (static_cast<std::size_t> (window), 0.0);
    actualLinear.assign (static_cast<std::size_t> (window), 0.0);
    upsampled.assign (static_cast<std::size_t> (capture * m), 0.0);
    shadow.assign (static_cast<std::size_t> (capture * m), 0.0);
    const auto largest = static_cast<std::size_t> (window * m);
    fftIn.assign (largest + 2, 0.0f);
    fftOut.assign (largest + 2, 0.0f);
    actualSpec.assign (static_cast<std::size_t> (window / 2 + 1), {});
    actualLinearSpec.assign (actualSpec.size(), {});
    shadowSpec.assign (static_cast<std::size_t> (window * m / 2 + 1), {});
}

void DistortionMeter::windowedSpectrum (RealFft& fft, const double* signal, const std::vector<double>& taper, Spectrum& out) noexcept
{
    const int n = fft.size();
    double weighted = 0.0, weights = 0.0;
    for (int i = 0; i < n; ++i)
    {
        weighted += taper[static_cast<std::size_t> (i)] * signal[i];
        weights += taper[static_cast<std::size_t> (i)];
    }
    const double mean = weighted / weights; // the products' DC is blocked: it must not leak into bin 1
    for (int i = 0; i < n; ++i)
        fftIn[static_cast<std::size_t> (i)] = static_cast<float> (taper[static_cast<std::size_t> (i)] * (signal[i] - mean));
    fft.forward (fftIn.data(), fftOut.data());
    for (int k = 0; k <= n / 2; ++k)
        out[static_cast<std::size_t> (k)] = { fftOut[static_cast<std::size_t> (2 * k)], fftOut[static_cast<std::size_t> (2 * k + 1)] };
}

ShaperDistortion DistortionMeter::analyse (const float* x, filters::ShaperKind kind) noexcept
{
    if (kind == filters::ShaperKind::None || osFactor == 0)
        return {};
    const auto s = filters::makeShaper (kind, fp);
    const int capture = engine::BandCapture::lengthFor (osFactor);
    const int m = shadowFactor;

    // Actual: the ADAA products, less the ADAA of the identity scaled by the fitted gain change.
    for (int i = 0; i < window; ++i)
    {
        const double now = x[kStart + i], before = x[kStart + i - 1];
        actual[static_cast<std::size_t> (i)] = filters::shaperAntialiased (s, now, before);
        actualLinear[static_cast<std::size_t> (i)] = 0.5 * (now + before);
    }
    windowedSpectrum (actualFft, actual.data(), actualWindow, actualSpec);
    windowedSpectrum (actualFft, actualLinear.data(), actualWindow, actualLinearSpec);
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= bandBins; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        num += (actualSpec[i] * std::conj (actualLinearSpec[i])).real();
        den += std::norm (actualLinearSpec[i]);
    }
    const double beta = den > 0.0 ? num / den : 0.0;

    double inBand = 0.0, leaked = 0.0;
    for (int k = 1; k <= window / 2; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        const double power =
            productsFilterGain (s, static_cast<double> (k) / window) * std::norm (actualSpec[i] - beta * actualLinearSpec[i]);
        if (k <= bandBins)
            inBand += power;
        else
            leaked += power * decimation[i];
    }

    // Shadow: the same two-input polynomial on the band-limited input and its copy one period earlier.
    for (int i = 0; i < capture; ++i)
        xs[static_cast<std::size_t> (i)] = x[i];
    interpolator.reset();
    interpolator.upsample (0, xs.data(), upsampled.data(), capture);
    const auto high = upsampled.size();
    const auto period = static_cast<std::size_t> (m);
    for (std::size_t j = 0; j < period; ++j)
        shadow[j] = 0.0;
    for (std::size_t j = period; j < high; ++j)
        shadow[j] = filters::shaperAntialiased (s, upsampled[j], upsampled[j - period]);
    const auto first = static_cast<std::size_t> (kStart * m + shadowDelay); // the shadow at the time of actual[0]
    windowedSpectrum (shadowFft, shadow.data() + first, shadowWindow, shadowSpec);

    // What lies above fp/2 folds at fp: into the band, or above it and through the decimation filter.
    double aliased = 0.0;
    const double scale = 1.0 / (static_cast<double> (m) * m);
    for (int k = window / 2 + 1; k <= window * m / 2; ++k)
    {
        int folded = k % window;
        if (folded > window / 2)
            folded = window - folded;
        const double weight = productsFilterGain (s, static_cast<double> (folded) / window)
            * (folded <= bandBins ? 1.0 : decimation[static_cast<std::size_t> (folded)]);
        aliased += weight * std::norm (shadowSpec[static_cast<std::size_t> (k)]) * scale;
    }
    return { windowNorm * (inBand + leaked), windowNorm * (aliased + leaked) };
}
} // namespace ssc::analysis
