#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <random>
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

BandSettings dynamicBell (double hz, double gainDb, double q)
{
    BandSettings s;
    s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12 };
    s.mode = BandMode::Dynamic;
    s.dynamics.kneeDb = 0.0;
    s.dynamics.rangeDb = 30.0;
    s.dynamics.attackMs = 5.0;
    s.dynamics.releaseMs = 50.0;
    return s;
}

struct StereoEngine : Engine
{
    explicit StereoEngine (int block = 512)
    {
        EngineConfig config;
        config.sampleRate = kFs;
        config.maxBlockSize = block;
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

double rmsDb (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += x[i] * x[i];
    return 10.0 * std::log10 (sum / static_cast<double> (to - from));
}

std::vector<double> sineAt (double hz, double levelDb, int samples)
{
    const double amplitude = dbToGain (levelDb);
    std::vector<double> x (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
        x[static_cast<std::size_t> (i)] = amplitude * std::sin (kTwoPi * hz * i / kFs);
    return x;
}
} // namespace

TEST_CASE ("Dynamic bell: steady-state gain follows the static characteristic (DYN-4, DYN-5)", "[engine][dynamic][DYN-4][DYN-5]")
{
    const double ratio = GENERATE (0.5, 2.0, 4.0, 100.0);
    const auto direction = GENERATE (Direction::Above, Direction::Below);
    const double knee = GENERATE (0.0, 6.0);

    auto band = dynamicBell (1000.0, 0.0, 1.0);
    band.dynamics.thresholdDb = -30.0;
    band.dynamics.ratio = ratio;
    band.dynamics.direction = direction;
    band.dynamics.kneeDb = knee;
    const auto computer = dynamics::GainComputer::make (-30.0, ratio, knee, 30.0, direction);

    for (double level : { -50.0, -36.0, -30.0, -24.0, -10.0 })
    {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = band;
        auto left = sineAt (1000.0, level, 36000);
        auto right = left;
        const auto input = left;
        engine.run (left, right, GlobalParams {}, bands);

        const double measured = rmsDb (left, 24000, 36000) - rmsDb (input, 24000, 36000);
        INFO ("ratio " << ratio << " above " << (direction == Direction::Above) << " knee " << knee << " level " << level);
        REQUIRE (measured == Approx (computer (level)).margin (0.1));
    }
}

TEST_CASE ("Dynamic band mix scales the gain change (DYN-7)", "[engine][dynamic][DYN-7]")
{
    auto measure = [] (double mix) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = dynamicBell (1000.0, 0.0, 1.0);
        bands[0].dynamics.thresholdDb = -40.0;
        bands[0].dynamics.ratio = 3.0;
        bands[0].dynamics.mix = mix;
        auto left = sineAt (1000.0, -10.0, 36000);
        auto right = left;
        const auto input = left;
        engine.run (left, right, GlobalParams {}, bands);
        return rmsDb (left, 24000, 36000) - rmsDb (input, 24000, 36000);
    };

    const double full = measure (1.0);
    REQUIRE (full == Approx (-20.0).margin (0.1)); // 30 dB over the threshold at 3:1
    REQUIRE (measure (0.5) == Approx (0.5 * full).margin (0.05));
    REQUIRE (measure (0.0) == Approx (0.0).margin (0.01));
}

TEST_CASE ("Static gain of a Dynamic band equals the Static design", "[engine][dynamic]")
{
    for (double gain : { 6.0, -4.1, 0.0 })
    {
        auto dynamicBand = dynamicBell (2000.0, gain, 2.0);
        dynamicBand.dynamics.thresholdDb = 0.0; // never reached
        dynamicBand.dynamics.direction = Direction::Above;

        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = dynamicBand;
        auto left = sineAt (2000.0, -20.0, 24000);
        auto right = left;
        const auto input = left;
        engine.run (left, right, GlobalParams {}, bands);
        REQUIRE (rmsDb (left, 12000, 24000) - rmsDb (input, 12000, 24000) == Approx (gain).margin (0.01));
    }
}

TEST_CASE ("Linked lanes share one gain; independent lanes do not (CH-5)", "[engine][dynamic][CH-5]")
{
    auto run = [] (DetectorLink link) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = dynamicBell (1000.0, 0.0, 1.0);
        bands[0].dynamics.thresholdDb = -40.0;
        bands[0].dynamics.ratio = 4.0;
        bands[0].dynamics.link = link;
        auto left = sineAt (1000.0, -10.0, 24000);
        auto right = sineAt (1000.0, -30.0, 24000);
        engine.run (left, right, GlobalParams {}, bands);
        return std::pair { engine.band (0).getLaneGainDb (0), engine.band (0).getLaneGainDb (1) };
    };

    for (auto link : { DetectorLink::Pair, DetectorLink::Group, DetectorLink::All })
    {
        const auto [l, r] = run (link);
        REQUIRE (std::abs (l - r) <= 1e-6);
        REQUIRE (l == Approx (-22.5).margin (0.2)); // the louder channel drives both
    }
    const auto [l, r] = run (DetectorLink::Independent);
    REQUIRE (l == Approx (-22.5).margin (0.2));
    REQUIRE (r == Approx (-7.5).margin (0.2));
}

TEST_CASE ("Lookahead delays the main path and catches transients early (DYN-8)", "[engine][dynamic][DYN-8]")
{
    const int lookahead = static_cast<int> (std::lround (0.005 * kFs));
    const int onset = 12000;
    std::vector<double> burst (24000, 0.0);
    for (int i = onset; i < 24000; ++i)
        burst[static_cast<std::size_t> (i)] = 0.5 * std::sin (kTwoPi * 1000.0 * (i - onset) / kFs);

    auto run = [&] (double lookaheadMs, double maxLookaheadMs, int& latency) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = dynamicBell (1000.0, 0.0, 1.0);
        bands[0].dynamics.thresholdDb = -40.0;
        bands[0].dynamics.ratio = 100.0;
        bands[0].dynamics.attackMs = 1.0;
        bands[0].dynamics.lookaheadMs = lookaheadMs;
        GlobalParams params;
        params.maxLookaheadMs = maxLookaheadMs;
        auto left = burst, right = burst;
        engine.run (left, right, params, bands);
        latency = engine.getLatencySamples();
        return left;
    };

    int latency = -1;
    const auto withLookahead = run (5.0, 5.0, latency);
    REQUIRE (latency == lookahead);

    int noLatency = -1;
    const auto without = run (0.0, 0.0, noLatency);
    REQUIRE (noLatency == 0);

    // The first 2 ms of the burst: with lookahead the reduction is already in place.
    auto peak = [] (const std::vector<double>& x, int from, int length) {
        double p = 0.0;
        for (int i = from; i < from + length; ++i)
            p = std::max (p, std::abs (x[static_cast<std::size_t> (i)]));
        return p;
    };
    const int window = static_cast<int> (0.002 * kFs);
    const double earlyWith = peak (withLookahead, onset + lookahead, window);
    const double earlyWithout = peak (without, onset, window);
    INFO ("early peak with lookahead " << earlyWith << ", without " << earlyWithout);
    REQUIRE (earlyWith < 0.5 * earlyWithout);

    // Nothing comes out before the delayed onset.
    REQUIRE (peak (withLookahead, 0, onset + lookahead - 1) == 0.0);
}

TEST_CASE ("Relative threshold does not depend on the programme level (DYN-10)", "[engine][dynamic][DYN-10]")
{
    // Pink noise with a resonance at 2 kHz, at two programme levels 20 dB apart.
    const auto noise = ssc::test::pinkNoise (1 << 17);
    auto gainAt = [&] (double levelDb) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = dynamicBell (2000.0, 0.0, 4.0);
        bands[0].dynamics.thresholdMode = ThresholdMode::Relative;
        bands[0].dynamics.thresholdDb = 0.0;
        bands[0].dynamics.ratio = 4.0;
        bands[0].dynamics.kneeDb = 6.0;
        const double scale = dbToGain (levelDb);
        std::vector<double> left (noise.size()), right;
        for (std::size_t i = 0; i < noise.size(); ++i)
            left[i] = scale * (0.25 * noise[i] + 0.2 * std::sin (kTwoPi * 2000.0 * static_cast<double> (i) / kFs));
        right = left;
        engine.run (left, right, GlobalParams {}, bands);
        return engine.band (0).getLaneGainDb (0);
    };

    const double quiet = gainAt (-30.0);
    const double loud = gainAt (-10.0);
    INFO ("gain at -30 dB: " << quiet << ", at -10 dB: " << loud);
    REQUIRE (quiet < -1.0); // the resonance stands out and is reduced
    REQUIRE (std::abs (quiet - loud) <= 0.5);
}

TEST_CASE ("Dynamic bands stay finite and bounded for random settings", "[engine][dynamic]")
{
    std::mt19937 rng (11);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    const auto noise = ssc::test::pinkNoise (16384);

    for (int trial = 0; trial < 40; ++trial)
    {
        StereoEngine engine (256);
        BandSettingsArray bands {};
        for (int b = 0; b < 4; ++b)
        {
            auto& s = bands[static_cast<std::size_t> (b)];
            s = dynamicBell (20.0 * std::pow (1000.0, uni (rng)), -30.0 + 60.0 * uni (rng), 0.1 + 20.0 * uni (rng));
            s.shape.shape = b == 1 ? Shape::LowShelf : (b == 2 ? Shape::HighShelf : Shape::Bell);
            s.dynamics.thresholdDb = -80.0 + 80.0 * uni (rng);
            s.dynamics.ratio = 0.1 + 100.0 * uni (rng);
            s.dynamics.direction = uni (rng) < 0.5 ? Direction::Above : Direction::Below;
            s.dynamics.rangeDb = 30.0 * uni (rng);
            s.dynamics.attackMs = 0.05 + 50.0 * uni (rng);
            s.dynamics.releaseMs = 1.0 + 500.0 * uni (rng);
            s.dynamics.kneeDb = 24.0 * uni (rng);
            s.dynamics.link = static_cast<DetectorLink> (trial % 4);
        }
        std::vector<double> left (noise.begin(), noise.end()), right = left;
        engine.run (left, right, GlobalParams {}, bands);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            REQUIRE (std::isfinite (left[i]));
            REQUIRE (std::abs (left[i]) < 1000.0);
        }
        REQUIRE (engine.getNonFiniteEvents() == 0);
    }
}

TEST_CASE ("Upward compression lifts a quiet band and takes the lift back at once on a loud onset (DYN-5, DYN-6)",
           "[engine][dynamic][DYN-5][DYN-6]")
{
    // A 1 kHz tone, quiet (−50 dBFS) for 1 s, then loud (−10 dBFS) for 1 s, then quiet again.
    auto band = dynamicBell (1000.0, 0.0, 1.0);
    band.dynamics.thresholdDb = -30.0;
    band.dynamics.ratio = 4.0;
    band.dynamics.rangeDb = 12.0;
    band.dynamics.attackMs = 5.0;
    band.dynamics.releaseMs = 200.0;
    BandSettingsArray bands {};

    const int second = static_cast<int> (kFs);
    auto input = sineAt (1000.0, -50.0, 3 * second);
    const auto loud = sineAt (1000.0, -10.0, 3 * second);
    std::copy (loud.begin() + second, loud.begin() + 2 * second, input.begin() + second);

    // Change in dB of the tone over 5 ms windows.
    auto change = [&input] (const std::vector<double>& out, int from) {
        const auto a = static_cast<std::size_t> (from);
        return rmsDb (out, a, a + 240) - rmsDb (input, a, a + 240);
    };
    for (const auto direction : { Direction::Below, Direction::Above })
    {
        CAPTURE (direction == Direction::Below);
        band.dynamics.direction = direction == Direction::Below ? dynamics::thresholdSideFor (true, 4.0) : Direction::Above;
        bands[0] = band;
        StereoEngine engine;
        auto left = input;
        auto right = input;
        engine.run (left, right, GlobalParams {}, bands);

        if (direction == Direction::Below)
        {
            // Quiet: lifted by the whole Range (4:1 would give 15 dB).
            CHECK (change (left, second - 2400) == Approx (12.0).margin (0.3));
            // The loud onset is not lifted: the boost goes with the 5 ms attack.
            CHECK (change (left, second + 960) < 0.5);
            CHECK (change (left, second + 24000) == Approx (0.0).margin (0.1));
            // Quiet again: the boost comes back with the 200 ms release, not at once.
            CHECK (change (left, 2 * second + 2400) < 6.0);
            CHECK (change (left, 3 * second - 2400) == Approx (12.0).margin (0.3));
        }
        else
        {
            // Downward compression of the same tone: nothing while quiet, 15 dB off… limited to 12.
            CHECK (change (left, second - 2400) == Approx (0.0).margin (0.1));
            CHECK (change (left, second + 24000) == Approx (-12.0).margin (0.3));
        }
    }
}

TEST_CASE ("Expansion below the threshold opens with the attack and closes with the release (DYN-6)", "[engine][dynamic][DYN-6]")
{
    auto band = dynamicBell (1000.0, 0.0, 1.0);
    band.dynamics.thresholdDb = -30.0;
    band.dynamics.ratio = 0.5;
    band.dynamics.direction = dynamics::thresholdSideFor (false, 0.5);
    band.dynamics.rangeDb = 12.0;
    band.dynamics.attackMs = 5.0;
    band.dynamics.releaseMs = 200.0;
    BandSettingsArray bands {};
    bands[0] = band;

    const int second = static_cast<int> (kFs);
    auto input = sineAt (1000.0, -50.0, 2 * second);
    const auto loud = sineAt (1000.0, -10.0, 2 * second);
    std::copy (loud.begin() + second, loud.end(), input.begin() + second);
    StereoEngine engine;
    auto left = input;
    auto right = input;
    engine.run (left, right, GlobalParams {}, bands);

    auto change = [&input, &left] (int from) {
        const auto a = static_cast<std::size_t> (from);
        return rmsDb (left, a, a + 240) - rmsDb (input, a, a + 240);
    };
    CHECK (change (second - 2400) == Approx (-12.0).margin (0.3)); // closed
    CHECK (change (second + 960) > -0.5);                          // open 20 ms after the onset
}
