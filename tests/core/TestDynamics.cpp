#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <random>

#include "ssc/dynamics/Dynamics.h"
#include "ssc/util/Math.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::dynamics;

TEST_CASE ("Gain computer follows the static characteristic (DYN-4, DYN-5)", "[dynamics][DYN-4][DYN-5]")
{
    const double ratio = GENERATE (0.5, 0.9, 2.0, 4.0, 100.0);
    const double knee = GENERATE (0.0, 6.0, 24.0);
    const auto direction = GENERATE (Direction::Above, Direction::Below);
    const double threshold = -30.0;
    const auto gc = GainComputer::make (threshold, ratio, knee, 60.0, direction);
    const double c = ratio >= kInfiniteRatio ? -1.0 : 1.0 / ratio - 1.0;
    INFO ("ratio " << ratio << " knee " << knee << " above " << (direction == Direction::Above));

    for (double x = -80.0; x <= 0.0; x += 0.5)
    {
        const double over = x - threshold;
        double expected = 0.0;
        if (direction == Direction::Above)
        {
            if (2.0 * over > knee)
                expected = c * over;
            else if (knee > 0.0 && 2.0 * over >= -knee)
                expected = c * std::pow (over + knee / 2.0, 2.0) / (2.0 * knee);
        }
        else
        {
            if (2.0 * over < -knee)
                expected = c * over;
            else if (knee > 0.0 && 2.0 * over <= knee)
                expected = -c * std::pow (-over + knee / 2.0, 2.0) / (2.0 * knee);
        }
        REQUIRE (gc (x) == Approx (expected).margin (1e-9));
    }

    // Continuous at the knee edges.
    for (double edge : { threshold - knee / 2.0, threshold + knee / 2.0 })
        REQUIRE (gc (edge - 1e-7) == Approx (gc (edge + 1e-7)).margin (1e-5));

    // Sign of the change follows DYN-5: Above+compression cuts, Below+compression boosts, and so on.
    const double far = direction == Direction::Above ? threshold + 20.0 : threshold - 20.0;
    if (ratio > 1.0)
        REQUIRE ((direction == Direction::Above ? gc (far) < 0.0 : gc (far) > 0.0));
    else
        REQUIRE ((direction == Direction::Above ? gc (far) > 0.0 : gc (far) < 0.0));
}

TEST_CASE ("Ratio 1:1 does nothing; Range limits every change (DYN-4)", "[dynamics][DYN-4]")
{
    const auto unity = GainComputer::make (-20.0, 1.0, 6.0, 30.0, Direction::Above);
    for (double x = -100.0; x <= 20.0; x += 1.0)
        REQUIRE (unity (x) == 0.0);

    std::mt19937 rng (7);
    std::uniform_real_distribution<double> level (-150.0, 30.0), thr (-80.0, 30.0), ratioDist (0.1, 100.0), kneeDist (0.0, 24.0),
        rangeDist (0.0, 30.0);
    for (int i = 0; i < 20000; ++i)
    {
        const double range = rangeDist (rng);
        const auto gc =
            GainComputer::make (thr (rng), ratioDist (rng), kneeDist (rng), range, (i & 1) != 0 ? Direction::Above : Direction::Below);
        REQUIRE (std::abs (gc (level (rng))) <= range + 0.01);
    }
}

TEST_CASE ("Attack and release are time constants (DYN-6)", "[dynamics][DYN-6]")
{
    const double fs = 12000.0;
    const double compress = 1.0 / 2.0 - 1.0; // the slope of 2:1
    GainSmoother s;
    s.setTimes (0.005, 0.1, fs);

    // Attack: a step to −20 dB reaches 63 % after τ_attack.
    const int attackSamples = static_cast<int> (std::lround (0.005 * fs));
    double y = 0.0;
    for (int i = 0; i < attackSamples; ++i)
        y = s.process (-20.0, compress);
    REQUIRE (y == Approx (-20.0 * (1.0 - std::exp (-1.0))).epsilon (0.02));

    for (int i = 0; i < 10 * attackSamples; ++i)
        y = s.process (-20.0, compress);
    REQUIRE (y == Approx (-20.0).margin (0.01));

    // Release towards 0 dB with τ_release.
    const int releaseSamples = static_cast<int> (std::lround (0.1 * fs));
    for (int i = 0; i < releaseSamples; ++i)
        y = s.process (0.0, compress);
    REQUIRE (y == Approx (-20.0 * std::exp (-1.0)).epsilon (0.02));
}

TEST_CASE ("Attack answers a rising level and release a falling one, in every direction (DYN-6)", "[dynamics][DYN-6]")
{
    const double fs = 12000.0;
    const auto attackSamples = static_cast<int> (std::lround (0.005 * fs));
    const auto releaseSamples = static_cast<int> (std::lround (0.1 * fs));
    auto run = [] (GainSmoother& s, double target, double slope, int samples) {
        double y = s.state;
        for (int i = 0; i < samples; ++i)
            y = s.process (target, slope);
        return y;
    };

    // Upward compression (2:1 below the threshold): a quiet passage brings the boost in with the
    // release, a loud onset takes it back with the attack.
    const auto upward = GainComputer::make (-30.0, 2.0, 0.0, 12.0, Direction::Below);
    REQUIRE (upward (-60.0) == Approx (12.0));
    REQUIRE (upward (-10.0) == 0.0);
    GainSmoother up;
    up.setTimes (0.005, 0.1, fs);
    REQUIRE (run (up, upward (-60.0), upward.slope, releaseSamples) == Approx (12.0 * (1.0 - std::exp (-1.0))).epsilon (0.02));
    run (up, upward (-60.0), upward.slope, 20 * releaseSamples);
    REQUIRE (run (up, upward (-10.0), upward.slope, attackSamples) == Approx (12.0 * std::exp (-1.0)).epsilon (0.02));

    // Downward expansion (1:2 below the threshold, a gate): it closes with the release and opens
    // with the attack.
    const auto gate = GainComputer::make (-30.0, 0.5, 0.0, 12.0, Direction::Below);
    REQUIRE (gate (-60.0) == Approx (-12.0));
    GainSmoother g;
    g.setTimes (0.005, 0.1, fs);
    REQUIRE (run (g, gate (-60.0), gate.slope, releaseSamples) == Approx (-12.0 * (1.0 - std::exp (-1.0))).epsilon (0.02));
    run (g, gate (-60.0), gate.slope, 20 * releaseSamples);
    REQUIRE (run (g, gate (-10.0), gate.slope, attackSamples) == Approx (-12.0 * std::exp (-1.0)).epsilon (0.02));

    // Upward expansion (1:2 above): a loud passage brings its boost in with the attack.
    const auto expandUp = GainComputer::make (-30.0, 0.5, 0.0, 12.0, Direction::Above);
    GainSmoother e;
    e.setTimes (0.005, 0.1, fs);
    REQUIRE (run (e, expandUp (-10.0), expandUp.slope, attackSamples) == Approx (12.0 * (1.0 - std::exp (-1.0))).epsilon (0.02));
}

TEST_CASE ("Down cuts and Up boosts: the threshold side follows the ratio (DYN-5)", "[dynamics][DYN-5]")
{
    REQUIRE (thresholdSideFor (false, 2.0) == Direction::Above); // compression
    REQUIRE (thresholdSideFor (true, 2.0) == Direction::Below);  // upward compression
    REQUIRE (thresholdSideFor (false, 0.5) == Direction::Below); // expansion, like a gate
    REQUIRE (thresholdSideFor (true, 0.5) == Direction::Above);  // upward expansion
    for (const double ratio : { 0.2, 0.5, 2.0, 4.0, 100.0 })
        for (const bool upward : { false, true })
        {
            const auto gc = GainComputer::make (-30.0, ratio, 6.0, 12.0, thresholdSideFor (upward, ratio));
            for (const double level : { -70.0, -50.0, -30.0, -10.0, 10.0 })
                REQUIRE ((upward ? gc (level) >= 0.0 : gc (level) <= 0.0));
            REQUIRE (std::abs (gc (-70.0)) + std::abs (gc (10.0)) > 1.0); // it acts somewhere
        }
}

TEST_CASE ("RMS detector reads a sine in dBFS", "[dynamics]")
{
    const double fs = 48000.0;
    RmsDetector d;
    d.setTime (detectorTimeFor (1000.0), fs);
    for (int i = 0; i < 48000; ++i)
        d.process (0.5 * std::sin (kTwoPi * 1000.0 * i / fs));
    REQUIRE (d.levelDb() == Approx (gainToDb (0.5)).margin (0.2));

    RmsDetector silent;
    silent.setTime (0.002, fs);
    silent.process (0.0);
    REQUIRE (silent.levelDb() == kSilenceDb);
    REQUIRE (detectorTimeFor (50.0) == Approx (0.04));
    REQUIRE (detectorTimeFor (5000.0) == Approx (0.002));
}
