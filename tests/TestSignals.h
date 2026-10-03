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
