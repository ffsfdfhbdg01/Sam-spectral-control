#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "TestSignals.h"
#include "ssc/dynamics/Dynamics.h"
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

TEST_CASE ("Spectral band Up lifts a quiet tone and leaves a loud one (DYN-5)", "[engine][spectral][DYN-5]")
{
    // Upward compression in the spectrum: 4:1 below −20 dBFS, at most 12 dB.
    auto band = spectralBell (1000.0, 1.0);
    band.dynamics.direction = dynamics::thresholdSideFor (true, 4.0);
    band.dynamics.rangeDb = 12.0;
    BandSettingsArray bands {};
    bands[0] = band;

    for (const double levelDb : { -50.0, -6.0 })
    {
        CAPTURE (levelDb);
        StereoEngine engine;
        auto left = test::sine (1000.0, dbToGain (levelDb), kFs, 96000);
        std::vector<double> l (left.begin(), left.end());
        auto r = l;
        const auto input = l;
        engine.run (l, r, GlobalParams {}, bands);
        const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
        const double change = gainToDb (toneAmplitude (l, 1000.0, 48000 + latency, 32768) / toneAmplitude (input, 1000.0, 48000, 32768));
        CHECK (change == Approx (levelDb < -20.0 ? 12.0 : 0.0).margin (0.5));
    }
}

namespace
{
/** Change of a 1 kHz-relative tone of amplitude 0.1 (−20 dBFS) through a band, in dB. */
double attenuation (const BandSettings& band, double hz, Quality quality = Quality::Normal)
{
    BandSettingsArray bands {};
    bands[0] = band;
    StereoEngine engine;
    const auto tone = test::sine (hz, 0.1, kFs, 96000);
    std::vector<double> l (tone.begin(), tone.end());
    auto r = l;
    const auto input = l;
    GlobalParams params;
    params.quality = quality;
    engine.run (l, r, params, bands);
    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    return gainToDb (toneAmplitude (l, hz, 48000 + latency, 32768) / toneAmplitude (input, hz, 48000, 32768));
}

BandSettings shapedBand (BandMode mode, Shape shape, double hz, double q, Slope slope)
{
    auto band = spectralBell (hz, q);
    band.shape.shape = shape;
    band.shape.slope = slope;
    band.mode = mode;
    band.dynamics.selectivity = 0.5;
    return band;
}
} // namespace

TEST_CASE ("A Spectral band acts on the region of its shape: Q and slope as a Static or Dynamic band", "[engine][spectral][DYN-3]")
{
    // Every mode at full action (a tone far over the threshold, Range 12 dB): the attenuation of
    // tones across the region follows the band's own shape at −12 dB.
    struct Case
    {
        Shape shape;
        double hz, q;
        Slope slope;
        bool resolved; ///< wide enough for the STFT of the Normal quality to reach the full depth at the centre
    };
    const Case cases[] = { { Shape::Bell, 1000.0, 4.0, Slope::Db12, true },       { Shape::Bell, 1000.0, 1.0, Slope::Db12, true },
                           { Shape::Bell, 150.0, 4.0, Slope::Db12, false },       { Shape::Bell, 60.0, 4.0, Slope::Db12, false },
                           { Shape::Bell, 1000.0, 20.0, Slope::Db12, false },     { Shape::Bell, 300.0, 10.0, Slope::Db12, false },
                           { Shape::HighShelf, 4000.0, 0.71, Slope::Db24, true }, { Shape::LowShelf, 300.0, 0.71, Slope::Db6, true } };
    for (const auto& c : cases)
    {
        auto staticBand = shapedBand (BandMode::Static, c.shape, c.hz, c.q, c.slope);
        staticBand.shape.gainDb = -12.0;
        auto dynamicBand = shapedBand (BandMode::Dynamic, c.shape, c.hz, c.q, c.slope);
        dynamicBand.dynamics.thresholdDb = -80.0;
        dynamicBand.dynamics.ratio = 100.0;
        dynamicBand.dynamics.rangeDb = 12.0;
        auto spectral = dynamicBand;
        spectral.mode = BandMode::Spectral;

        for (const double ratio : { 1.0, 1.0 / 1.5, 1.0 / 1.25, 1.0 / 1.12, 1.12, 1.25, 1.5, 2.0, 3.0 })
        {
            const double hz = c.hz * ratio;
            const double s = attenuation (staticBand, hz);
            const double d = attenuation (dynamicBand, hz);
            const double sp = attenuation (spectral, hz);
            CAPTURE (static_cast<int> (c.shape), c.hz, c.q, hz, s, d, sp);
            CHECK (d == Approx (s).margin (0.5));
            if (ratio != 1.0 || c.resolved)
                CHECK (sp == Approx (s).margin (0.75));
            else
            {
                // A band narrower than the STFT resolves is shallower at its centre, never wider or
                // deeper; the High quality (a longer frame) gets closer.
                const double spHigh = attenuation (spectral, hz, Quality::High);
                CAPTURE (spHigh);
                CHECK (sp > s - 0.25);
                CHECK (sp < s + 3.25);
                CHECK (spHigh < sp - 0.4);
            }
        }
    }

    // A narrow band at a realistic threshold: loud tones on its skirts are detected through the
    // band's region as a Dynamic band detects them, so they are left (nearly) alone.
    for (const double q : { 2.0, 8.0 })
        for (const double ratio : { 1.0, 1.12, 1.25, 1.5, 2.0, 1.0 / 1.25, 1.0 / 1.5 })
        {
            const double hz = 2000.0 * ratio;
            auto dynamicBand = shapedBand (BandMode::Dynamic, Shape::Bell, 2000.0, q, Slope::Db12);
            dynamicBand.dynamics.thresholdDb = -30.0;
            dynamicBand.dynamics.ratio = 4.0;
            dynamicBand.dynamics.rangeDb = 12.0;
            auto spectral = dynamicBand;
            spectral.mode = BandMode::Spectral;
            const double d = attenuation (dynamicBand, hz);
            const double sp = attenuation (spectral, hz);
            CAPTURE (q, hz, d, sp);
            CHECK (sp == Approx (d).margin (0.3));
        }
}
