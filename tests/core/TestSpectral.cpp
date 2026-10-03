#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;

BandSettings spectralBell (double hz, double q)
{
    BandSettings s;
    s.used = true;
    s.active = true;
    s.shape = { Shape::Bell, hz, 0.0, q, Slope::Db12 };
    s.mode = BandMode::Spectral;
    s.dynamics.thresholdMode = ThresholdMode::Absolute;
    s.dynamics.thresholdDb = -20.0;
    s.dynamics.ratio = 4.0;
    s.dynamics.kneeDb = 0.0;
    s.dynamics.rangeDb = 30.0;
    s.dynamics.attackMs = 5.0;
    s.dynamics.releaseMs = 50.0;
    s.dynamics.selectivity = 0.9;
    return s;
}

struct StereoEngine : Engine
{
    StereoEngine()
    {
        EngineConfig config;
        config.sampleRate = kFs;
        config.maxBlockSize = 512;
        prepare (config);
    }

    void run (std::vector<double>& left, std::vector<double>& right, const GlobalParams& params, const BandSettingsArray& bands)
    {
        for (std::size_t offset = 0; offset < left.size(); offset += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, left.size() - offset));
            double* ptrs[] = { left.data() + offset, right.data() + offset };
            process (ptrs, 2, n, params, bands);
        }
    }
};

/** Amplitude of a tone in a segment (single-bin DFT with a Hann window). */
double toneAmplitude (const std::vector<double>& x, double hz, std::size_t from, std::size_t length)
{
    std::complex<double> sum = 0.0;
    double windowSum = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length));
        sum += w * x[from + i] * std::polar (1.0, -kTwoPi * hz * static_cast<double> (from + i) / kFs);
        windowSum += w;
    }
    return 2.0 * std::abs (sum) / windowSum;
}
} // namespace

TEST_CASE ("Spectral engine: latency follows the frame size and only while a Spectral band exists (ADR-0011)", "[engine][spectral]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    GlobalParams params;
    engine.configure (params, bands);
    REQUIRE (engine.getLatencySamples() == 0);
    REQUIRE_FALSE (engine.isSpectralEngineRunning());

    bands[2] = spectralBell (1000.0, 1.0);
    bands[2].active = false; // switched off, but still in Spectral mode: the latency stays
    engine.configure (params, bands);
    REQUIRE (engine.isSpectralEngineRunning());
    REQUIRE (engine.getLatencySamples() == 4096);

    params.quality = Quality::Eco;
    engine.configure (params, bands);
    REQUIRE (engine.getLatencySamples() == 2048);
    params.quality = Quality::Max;
    params.maxLookaheadMs = 5.0;
    engine.configure (params, bands);
    REQUIRE (engine.getLatencySamples() == 8192 + 240);
}

TEST_CASE ("Spectral path with a unity mask reconstructs the delayed input (DYN-9)", "[engine][spectral][DYN-9]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = spectralBell (1000.0, 1.0);
    bands[0].dynamics.thresholdDb = 0.0; // never reached: the mask stays at 1
    GlobalParams params;

    const auto noise = ssc::test::pinkNoise (1 << 16);
    std::vector<double> left (noise.begin(), noise.end());
    for (auto& v : left)
        v *= 0.1;
    std::vector<double> right = left;
    const auto input = left;
    engine.run (left, right, params, bands);

    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    REQUIRE (latency == 4096);
    double residual = 0.0, energy = 0.0;
    for (std::size_t i = 3 * latency; i < input.size(); ++i)
    {
        residual += std::pow (left[i] - input[i - latency], 2.0);
        energy += std::pow (input[i - latency], 2.0);
    }
    INFO ("residual " << 10.0 * std::log10 (residual / energy) << " dB");
    REQUIRE (10.0 * std::log10 (residual / energy) <= -120.0);
}

TEST_CASE ("Spectral band reduces only the tone over the threshold (DYN-3)", "[engine][spectral][DYN-3]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = spectralBell (1500.0, 0.5); // a wide region: 1 kHz … 3 kHz
    bands[0].dynamics.thresholdDb = -20.0;
    bands[0].dynamics.ratio = 100.0;
    GlobalParams params;

    const int total = 96000;
    std::vector<double> left (static_cast<std::size_t> (total));
    for (int i = 0; i < total; ++i)
    {
        const double t = i / kFs;
        left[static_cast<std::size_t> (i)] = dbToGain (-10.0) * std::sin (kTwoPi * 1000.0 * t)
            + dbToGain (-30.0) * std::sin (kTwoPi * 1100.0 * t) + dbToGain (-30.0) * std::sin (kTwoPi * 3000.0 * t);
    }
    std::vector<double> right = left;
    const auto input = left;
    engine.run (left, right, params, bands);

    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    const std::size_t from = 60000, length = 32768;
    auto change = [&] (double hz) {
        return gainToDb (toneAmplitude (left, hz, from, length)) - gainToDb (toneAmplitude (input, hz, from - latency, length));
    };
    INFO ("1 kHz " << change (1000.0) << " dB, 1.1 kHz " << change (1100.0) << " dB, 3 kHz " << change (3000.0) << " dB");
    REQUIRE (change (1000.0) < -6.0);
    REQUIRE (change (1100.0) == Approx (0.0).margin (0.2));
    REQUIRE (change (3000.0) == Approx (0.0).margin (0.2));
}

TEST_CASE ("Spectral relative threshold does not depend on the programme level (DYN-10)", "[engine][spectral][DYN-10]")
{
    const auto noise = ssc::test::pinkNoise (1 << 17);
    auto gainAt = [&] (double levelDb) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = spectralBell (2000.0, 1.0);
        bands[0].dynamics.thresholdMode = ThresholdMode::Relative;
        bands[0].dynamics.thresholdDb = 6.0;
        bands[0].dynamics.kneeDb = 6.0;
        const double scale = dbToGain (levelDb);
        std::vector<double> left (noise.size()), right;
        for (std::size_t i = 0; i < noise.size(); ++i)
            left[i] = scale * (0.25 * noise[i] + 0.1 * std::sin (kTwoPi * 2000.0 * static_cast<double> (i) / kFs));
        right = left;
        engine.run (left, right, GlobalParams {}, bands);
        return engine.getBandGainChangeDb (0);
    };

    const double quiet = gainAt (-30.0);
    const double loud = gainAt (-10.0);
    INFO ("gain at -30 dB: " << quiet << ", at -10 dB: " << loud);
    REQUIRE (quiet < -1.0);
    REQUIRE (std::abs (quiet - loud) <= 0.5);
}

TEST_CASE ("Channels without spectral lanes are delayed bit-exactly (CH-6)", "[engine][spectral][CH-6]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = spectralBell (1000.0, 1.0);
    bands[0].component = StereoComponent::Left;
    GlobalParams params;

    const auto noise = ssc::test::pinkNoise (1 << 15);
    std::vector<double> left (noise.begin(), noise.end()), right = left;
    const auto input = right;
    engine.run (left, right, params, bands);

    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    for (std::size_t i = latency; i < input.size(); ++i)
        REQUIRE (right[i] == input[i - latency]);
}
