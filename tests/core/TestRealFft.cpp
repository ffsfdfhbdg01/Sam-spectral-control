#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "ssc/fft/RealFft.h"
#include "ssc/util/Math.h"

using Catch::Approx;

TEST_CASE ("RealFft matches a naive DFT", "[fft]")
{
    const int n = GENERATE (32, 64, 256, 1024);
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> x (static_cast<std::size_t> (n));
    for (auto& v : x)
        v = dist (rng);

    ssc::RealFft fft (n);
    std::vector<float> spectrum (static_cast<std::size_t> (n + 2));
    fft.forward (x.data(), spectrum.data());

    for (int k = 0; k <= n / 2; ++k)
    {
        std::complex<double> sum {};
        for (int t = 0; t < n; ++t)
            sum += static_cast<double> (x[static_cast<std::size_t> (t)]) * std::polar (1.0, -ssc::kTwoPi * k * t / n);

        REQUIRE (spectrum[static_cast<std::size_t> (2 * k)] == Approx (sum.real()).margin (1e-3));
        REQUIRE (spectrum[static_cast<std::size_t> (2 * k + 1)] == Approx (sum.imag()).margin (1e-3));
    }
}

TEST_CASE ("RealFft inverse(forward(x)) reconstructs x", "[fft]")
{
    const int n = GENERATE (32, 2048, 16384);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> x (static_cast<std::size_t> (n)), y (static_cast<std::size_t> (n));
    for (auto& v : x)
        v = dist (rng);

    ssc::RealFft fft (n);
    std::vector<float> spectrum (static_cast<std::size_t> (n + 2));
    fft.forward (x.data(), spectrum.data());
    fft.inverse (spectrum.data(), y.data());

    float maxError = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i)
        maxError = std::max (maxError, std::abs (x[i] - y[i]));
    REQUIRE (maxError < 1e-5f);
}

TEST_CASE ("RealFft puts a bin-centred cosine into a single bin", "[fft]")
{
    constexpr int n = 512;
    constexpr int bin = 37;
    std::vector<float> x (n);
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t> (i)] = static_cast<float> (std::cos (ssc::kTwoPi * bin * i / n));

    ssc::RealFft fft (n);
    std::vector<float> spectrum (n + 2);
    fft.forward (x.data(), spectrum.data());

    for (int k = 0; k <= n / 2; ++k)
    {
        const double magnitude = std::hypot (spectrum[static_cast<std::size_t> (2 * k)], spectrum[static_cast<std::size_t> (2 * k + 1)]);
        if (k == bin)
            REQUIRE (magnitude == Approx (n / 2.0).epsilon (1e-4));
        else
            REQUIRE (magnitude < 1e-2);
    }
}

TEST_CASE ("RealFft rejects invalid sizes", "[fft]")
{
    ssc::RealFft fft;
    REQUIRE_THROWS (fft.prepare (1000));
    REQUIRE_THROWS (fft.prepare (16));
    REQUIRE_NOTHROW (fft.prepare (64));
    REQUIRE (fft.size() == 64);
}
