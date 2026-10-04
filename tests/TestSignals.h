#pragma once

#include <cmath>
#include <random>
#include <vector>

#include "ssc/fft/RealFft.h"
#include "ssc/util/Math.h"

namespace ssc::test
{
inline std::vector<float> sine (double frequency, double amplitude, double sampleRate, int numSamples, double phase = 0.0)
{
    std::vector<float> out (static_cast<std::size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<std::size_t> (i)] = static_cast<float> (amplitude * std::sin (kTwoPi * frequency * i / sampleRate + phase));
    return out;
}

/** Exactly pink (power ∝ 1/f) noise synthesised in the frequency domain; size must be a power of two. */
inline std::vector<float> pinkNoise (int size, unsigned seed = 1234)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> phaseDist (0.0, kTwoPi);

    std::vector<float> spectrum (static_cast<std::size_t> (size + 2), 0.0f);
    for (int k = 1; k <= size / 2; ++k)
    {
        const double magnitude = 1.0 / std::sqrt (static_cast<double> (k));
        const double phase = k == size / 2 ? 0.0 : phaseDist (rng);
        spectrum[static_cast<std::size_t> (2 * k)] = static_cast<float> (magnitude * std::cos (phase));
        spectrum[static_cast<std::size_t> (2 * k + 1)] = static_cast<float> (magnitude * std::sin (phase));
    }

    std::vector<float> out (static_cast<std::size_t> (size));
    RealFft fft (size);
    fft.inverse (spectrum.data(), out.data());

    double sumSquares = 0.0;
    for (auto v : out)
        sumSquares += static_cast<double> (v) * v;
    const double rms = std::sqrt (sumSquares / size);
    for (auto& v : out)
        v = static_cast<float> (v * (0.1 / rms)); // -20 dBFS RMS
    return out;
}
} // namespace ssc::test

#include <complex>
#include <numbers>

namespace ssc::test
{
/** Complex spectrum X[k], k = 0 … size/2, of x zero-padded to `size` (a power of two), in double precision. */
inline std::vector<std::complex<double>> spectrumOf (const std::vector<double>& x, std::size_t size)
{
    std::vector<std::complex<double>> a (size);
    for (std::size_t i = 0; i < std::min (size, x.size()); ++i)
        a[i] = x[i];

    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    std::vector<std::complex<double>> twiddle (size / 2);
    for (std::size_t k = 0; k < size / 2; ++k)
        twiddle[k] = std::polar (1.0, -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (size));
    for (std::size_t len = 2; len <= size; len <<= 1)
        for (std::size_t i = 0; i < size; i += len)
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * twiddle[k * (size / len)];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    a.resize (size / 2 + 1);
    return a;
}
} // namespace ssc::test
