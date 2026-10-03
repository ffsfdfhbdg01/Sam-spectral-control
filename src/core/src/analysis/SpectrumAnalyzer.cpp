#include "ssc/analysis/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ssc/util/Math.h"

namespace ssc::analysis
{
namespace
{
    constexpr float kFloorDb = -160.0f;

    /** A local maximum must exceed the mean of its neighbourhood by this ratio (6 dB) to be refined as a tone. */
    constexpr float kTonalProminence = 4.0f;
    constexpr int kNeighbourhood = 12;
    constexpr int kMainLobe = 3;
} // namespace

void SpectrumAnalyzer::prepare (const SpectrumAnalyzerConfig& config)
{
    if (! isPowerOfTwo (config.fftSize) || config.hopSize <= 0 || config.hopSize > config.fftSize)
        throw std::invalid_argument ("invalid SpectrumAnalyzer configuration");

    cfg = config;
    const int n = cfg.fftSize;
    fft.prepare (n);

    // Periodic 4-term Blackman-Harris window.
    window.resize (static_cast<std::size_t> (n));
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = kTwoPi * i / n;
        const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x) - 0.01168 * std::cos (3.0 * x);
        window[static_cast<std::size_t> (i)] = static_cast<float> (w);
        sum += w;
    }

    // A sine of amplitude A produces a peak |X| of A * sum(w) / 2.
    amplitudeScale = static_cast<float> (2.0 / sum);

    input.assign (static_cast<std::size_t> (n), 0.0f);
    frame.assign (static_cast<std::size_t> (n), 0.0f);
    spectrum.assign (static_cast<std::size_t> (n + 2), 0.0f);
    smoothedPower.assign (static_cast<std::size_t> (n / 2 + 1), 0.0f);

    const double hopSeconds = cfg.hopSize / cfg.sampleRate;
    smoothingAlpha = cfg.smoothingMs > 0.0 ? std::exp (-hopSeconds / (cfg.smoothingMs * 0.001)) : 0.0;

    // Logarithmic display grid with geometric-midpoint edges.
    const int points = std::max (2, cfg.numDisplayPoints);
    const double nyquist = cfg.sampleRate * 0.5;
    const double fMin = std::max (1.0, cfg.minHz);
    const double fMax = std::min (cfg.maxHz, nyquist);
    const double ratio = std::pow (fMax / fMin, 1.0 / (points - 1));
    const double binsPerHz = n / cfg.sampleRate;

    displayHz.resize (static_cast<std::size_t> (points));
    displayLevelDb.assign (static_cast<std::size_t> (points), kFloorDb);
    displayTilt.resize (static_cast<std::size_t> (points));
    displayBinLo.resize (static_cast<std::size_t> (points));
    displayBinHi.resize (static_cast<std::size_t> (points));

    for (int i = 0; i < points; ++i)
    {
        const double f = fMin * std::pow (ratio, i);
        const double lo = f / std::sqrt (ratio);
        const double hi = f * std::sqrt (ratio);
        const auto idx = static_cast<std::size_t> (i);
        displayHz[idx] = static_cast<float> (f);
        displayTilt[idx] = static_cast<float> (cfg.tiltDbPerOctave * std::log2 (f / 1000.0));
        displayBinLo[idx] = std::clamp (static_cast<int> (std::ceil (lo * binsPerHz)), 0, n / 2);
        displayBinHi[idx] = std::clamp (static_cast<int> (std::floor (hi * binsPerHz)), 0, n / 2);
    }

    reset();
}

void SpectrumAnalyzer::reset() noexcept
{
    std::fill (input.begin(), input.end(), 0.0f);
    std::fill (smoothedPower.begin(), smoothedPower.end(), 0.0f);
    std::fill (displayLevelDb.begin(), displayLevelDb.end(), kFloorDb);
    writeIndex = 0;
    samplesSinceFrame = 0;
    samplesSeen = 0;
    hasFrame = false;
}

bool SpectrumAnalyzer::push (const float* samples, int numSamples) noexcept
{
    bool computed = false;
    const int n = cfg.fftSize;

    for (int i = 0; i < numSamples; ++i)
    {
        input[static_cast<std::size_t> (writeIndex)] = samples[i];
        writeIndex = (writeIndex + 1) & (n - 1);
        ++samplesSinceFrame;
        if (samplesSeen < n)
            ++samplesSeen;

        if (samplesSinceFrame >= cfg.hopSize && samplesSeen >= n)
        {
            samplesSinceFrame = 0;
            computeFrame();
            computed = true;
        }
    }

    if (computed)
        updateDisplay();

    return computed;
}

void SpectrumAnalyzer::computeFrame() noexcept
{
    const int n = cfg.fftSize;

    // Oldest sample first: the ring buffer's write index points at the oldest sample.
    for (int i = 0; i < n; ++i)
    {
        const auto src = static_cast<std::size_t> ((writeIndex + i) & (n - 1));
        frame[static_cast<std::size_t> (i)] = input[src] * window[static_cast<std::size_t> (i)];
    }

    fft.forward (frame.data(), spectrum.data());

    const auto alpha = static_cast<float> (smoothingAlpha);
    const float useAlpha = hasFrame ? alpha : 0.0f;

    for (int k = 0; k <= n / 2; ++k)
    {
        const float re = spectrum[static_cast<std::size_t> (2 * k)] * amplitudeScale;
        const float im = spectrum[static_cast<std::size_t> (2 * k + 1)] * amplitudeScale;
        const float power = re * re + im * im;
        auto& s = smoothedPower[static_cast<std::size_t> (k)];
        s = useAlpha * s + (1.0f - useAlpha) * power;
    }

    hasFrame = true;
}

void SpectrumAnalyzer::updateDisplay() noexcept
{
    const int n = cfg.fftSize;
    const int numBins = n / 2 + 1;
    const double binsPerHz = n / cfg.sampleRate;
    const auto& p = smoothedPower;
    const auto points = static_cast<int> (displayHz.size());

    // Base curve: mean power of covered bins, or interpolation where bins are sparser than points.
    for (int i = 0; i < points; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const int lo = displayBinLo[idx];
        const int hi = displayBinHi[idx];
        float power;

        if (hi >= lo)
        {
            double sum = 0.0;
            for (int k = lo; k <= hi; ++k)
                sum += p[static_cast<std::size_t> (k)];
            power = static_cast<float> (sum / (hi - lo + 1));
        }
        else
        {
            const double position = displayHz[idx] * binsPerHz;
            const int k0 = std::clamp (static_cast<int> (std::floor (position)), 0, numBins - 1);
            const int k1 = std::min (k0 + 1, numBins - 1);
            const auto frac = static_cast<float> (position - k0);
            const float db0 = static_cast<float> (powerToDb (static_cast<double> (p[static_cast<std::size_t> (k0)])));
            const float db1 = static_cast<float> (powerToDb (static_cast<double> (p[static_cast<std::size_t> (k1)])));
            power = std::pow (10.0f, (db0 + (db1 - db0) * frac) * 0.1f);
        }

        displayLevelDb[idx] = std::max (kFloorDb, static_cast<float> (powerToDb (static_cast<double> (power))) + displayTilt[idx]);
    }

    // Tonal peaks: refine level and position, then raise the nearest display point.
    const double fMin = displayHz.front();
    const double logRatio = std::log (displayHz.back() / fMin) / (points - 1);

    for (int k = 2; k < numBins - 2; ++k)
    {
        const float centre = p[static_cast<std::size_t> (k)];
        if (centre <= p[static_cast<std::size_t> (k - 1)] || centre < p[static_cast<std::size_t> (k + 1)] || centre <= 0.0f)
            continue;

        double neighbourhood = 0.0;
        int count = 0;
        for (int j = k - kNeighbourhood; j <= k + kNeighbourhood; ++j)
        {
            if (j < 0 || j >= numBins || std::abs (j - k) <= kMainLobe)
                continue;
            neighbourhood += p[static_cast<std::size_t> (j)];
            ++count;
        }

        if (count > 0 && centre < kTonalProminence * static_cast<float> (neighbourhood / count))
            continue;

        const double ym = powerToDb (static_cast<double> (p[static_cast<std::size_t> (k - 1)]));
        const double y0 = powerToDb (static_cast<double> (centre));
        const double yp = powerToDb (static_cast<double> (p[static_cast<std::size_t> (k + 1)]));
        const double denominator = ym - 2.0 * y0 + yp;
        const double offset = denominator < 0.0 ? 0.5 * (ym - yp) / denominator : 0.0;
        const double peakDb = y0 - 0.25 * (ym - yp) * offset;
        const double peakHz = (k + offset) / binsPerHz;

        if (peakHz < fMin || peakHz > displayHz.back())
            continue;

        const int nearest = std::clamp (static_cast<int> (std::lround (std::log (peakHz / fMin) / logRatio)), 0, points - 1);
        const auto idx = static_cast<std::size_t> (nearest);
        const auto tilt = static_cast<float> (cfg.tiltDbPerOctave * std::log2 (peakHz / 1000.0));
        displayLevelDb[idx] = std::max (displayLevelDb[idx], static_cast<float> (peakDb) + tilt);
    }
}
} // namespace ssc::analysis
