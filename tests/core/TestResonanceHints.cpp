#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "ResonanceMaterial.h"
#include "TestSignals.h"
#include "ssc/analysis/PitchTracker.h"
#include "ssc/analysis/ResonanceHints.h"

using Catch::Approx;
using namespace ssc;

namespace
{
constexpr double kFs = 48000.0;

/** Pink noise (-20 dBFS RMS) of the given length, with optional resonances. */
std::vector<float> noiseWith (double seconds, const std::vector<test::Resonance>& resonances, unsigned seed = 7)
{
    const auto length = static_cast<std::size_t> (seconds * kFs);
    const auto noise = test::pinkNoise (1 << 20, seed);
    std::vector<double> x (length);
    for (std::size_t i = 0; i < length; ++i)
        x[i] = 0.5 * noise[i % noise.size()];
    for (const auto& r : resonances)
        test::applyPeaking (x, kFs, r);
    return { x.begin(), x.end() };
}
} // namespace

TEST_CASE ("YIN finds the pitch of a harmonic tone and none in noise", "[analysis][VIS-5]")
{
    analysis::PitchTracker pitch;
    pitch.prepare (kFs, 1024, 70.0, 1000.0);
    std::vector<float> tone (static_cast<std::size_t> (pitch.frameLength()));
    for (std::size_t i = 0; i < tone.size(); ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= 8; ++h)
            v += std::sin (kTwoPi * 220.0 * h * static_cast<double> (i) / kFs) / h;
        tone[i] = static_cast<float> (0.2 * v);
    }
    const auto voiced = pitch.analyse (tone.data());
    CHECK (voiced.f0Hz == Approx (220.0).epsilon (0.01));
    CHECK (voiced.aperiodicity < 0.1);

    std::mt19937 rng (3);
    std::normal_distribution<float> gauss (0.0f, 0.1f);
    std::vector<float> noise (tone.size());
    for (auto& v : noise)
        v = gauss (rng);
    CHECK (pitch.analyse (noise.data()).aperiodicity > 0.3);
}

TEST_CASE ("Resonance hints: a resonance in noise shows, plain noise shows nothing (VIS-5)", "[analysis][VIS-5]")
{
    // +12 dB, Q 10 at 2 kHz in pink noise.
    {
        analysis::ResonanceHints hints;
        hints.prepare (kFs);
        const auto signal = noiseWith (8.0, { { 2000.0, 12.0, 10.0 } });
        double firstSeen = -1.0;
        for (std::size_t offset = 0; offset + 1024 <= signal.size(); offset += 1024)
        {
            hints.push (signal.data() + offset, 1024);
            for (const auto& h : hints.hints())
                if (firstSeen < 0.0 && std::abs (std::log2 (h.hz / 2000.0)) < 1.0 / 12.0)
                    firstSeen = static_cast<double> (offset) / kFs;
        }
        CHECK (firstSeen >= 0.0);
        CHECK (firstSeen < 4.0);
        REQUIRE (hints.hints().size() == 1);
        CHECK (hints.hints()[0].hz == Approx (2000.0).epsilon (0.03));
        CHECK (hints.hints()[0].prominenceDb > 6.0f);
        CHECK (hints.hints()[0].confidence > 0.5f);
    }

    // Pink noise alone: no hint in 20 s.
    {
        analysis::ResonanceHints hints;
        hints.prepare (kFs);
        const auto signal = noiseWith (20.0, {}, 11);
        for (std::size_t offset = 0; offset + 1024 <= signal.size(); offset += 1024)
            hints.push (signal.data() + offset, 1024);
        CHECK (hints.appearances() == 0);
    }
}

TEST_CASE ("Resonance hints do not flicker (VIS-4)", "[analysis][VIS-4]")
{
    // A resonance switching between +6 and +12 dB every 0.5 s keeps its hint steady.
    analysis::ResonanceHints hints;
    hints.prepare (kFs);
    const double seconds = 20.0;
    const auto weak = noiseWith (seconds, { { 3000.0, 6.0, 12.0 } }, 5);
    const auto strong = noiseWith (seconds, { { 3000.0, 12.0, 12.0 } }, 5);
    const auto half = static_cast<std::size_t> (0.5 * kFs);
    for (std::size_t offset = 0; offset + 1024 <= weak.size(); offset += 1024)
    {
        const auto& source = (offset / half) % 2 == 0 ? strong : weak;
        hints.push (source.data() + offset, 1024);
    }
    CHECK (hints.visibilityChanges() <= static_cast<int> (seconds / 2.0));
    CHECK (hints.visibilityChanges() <= 2);
}

TEST_CASE ("Resonance hints on music-like material: precision, recall, false alarms (VIS-5)", "[analysis][VIS-5]")
{
    // A small regression set above 400 Hz (where the criterion applies, research/11); the full
    // evaluation is tools/ResonanceEval.
    test::Score score;
    for (unsigned c = 0; c < 8; ++c)
    {
        const auto material = test::makeMaterial (1000u + c, 12.0, kFs, 2, false, 400.0);
        test::scoreRun (test::runDetector (material.samples, kFs), material.resonances, score);
    }
    int falseHints = 0;
    double minutes = 0.0;
    for (unsigned c = 0; c < 4; ++c)
    {
        const auto clean = test::makeMaterial (5000u + c, 15.0, kFs, 0, c % 2 == 0);
        const auto run = test::runDetector (clean.samples, kFs);
        falseHints += run.appearances;
        minutes += run.seconds / 60.0;
    }
    const int high = score.relevant - score.relevantLow;
    const double highRecall = high > 0 ? static_cast<double> (score.detected - score.detectedLow) / high : 1.0;
    std::printf ("[VIS-5] precision %.2f, recall above 400 Hz %.2f (%d/%d), false hints %.2f/min\n", score.precision(), highRecall,
                 score.detected - score.detectedLow, high, falseHints / minutes);
    // Regression bounds for this small set (full evaluation: research/11 §c, VIS-5).
    CHECK (score.precision() >= 0.75);
    CHECK (highRecall >= 0.5);
    CHECK (falseHints / minutes <= 1.0);
}
