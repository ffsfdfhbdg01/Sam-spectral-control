#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"
#include "ssc/fft/RealFft.h"
#include "ssc/loudness/Loudness.h"
#include "ssc/util/Math.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;

struct StereoEngine : Engine
{
    StereoEngine()
    {
        EngineConfig config;
        config.sampleRate = kFs;
        config.maxBlockSize = 512;
        prepare (config);
    }

    /** Runs blocks; calls observe (sampleIndex) after every block. */
    template <typename Observer>
    void run (std::vector<double>& left, std::vector<double>& right, const GlobalParams& params, const BandSettingsArray& bands,
              Observer&& observe)
    {
        for (std::size_t offset = 0; offset < left.size(); offset += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, left.size() - offset));
            double* ptrs[] = { left.data() + offset, right.data() + offset };
            process (ptrs, 2, n, params, bands);
            observe (offset + static_cast<std::size_t> (n));
        }
    }

    void run (std::vector<double>& left, std::vector<double>& right, const GlobalParams& params, const BandSettingsArray& bands)
    {
        run (left, right, params, bands, [] (std::size_t) {});
    }
};

BandSettings bell (double hz, double gainDb, double q)
{
    BandSettings s;
    s.used = true;
    s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12 };
    return s;
}

std::vector<double> pink (double levelScale, std::size_t samples)
{
    const auto noise = ssc::test::pinkNoise (1 << 18);
    std::vector<double> x (samples);
    for (std::size_t i = 0; i < samples; ++i)
        x[i] = levelScale * noise[i % noise.size()];
    return x;
}

/** Loudness of a segment by the Auto Gain model (Fletcher or Digital). */
double modelLoudness (const std::vector<double>& x, std::size_t from, std::size_t to, AutoGainWeighting weighting)
{
    constexpr int n = 4096;
    RealFft fft (n);
    loudness::ErbBands bands;
    bands.prepare (kFs, n);
    std::vector<float> window (n), frame (n), spectrum (n + 2);
    double power = 0.0;
    for (int i = 0; i < n; ++i)
    {
        window[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (kTwoPi * i / n));
        power += std::pow (window[static_cast<std::size_t> (i)], 2.0);
    }
    std::vector<double> powers (static_cast<std::size_t> (bands.numBands()), 0.0);
    int frames = 0;
    for (std::size_t start = from; start + n <= to; start += n / 4, ++frames)
    {
        for (int i = 0; i < n; ++i)
            frame[static_cast<std::size_t> (i)] =
                static_cast<float> (x[start + static_cast<std::size_t> (i)]) * window[static_cast<std::size_t> (i)];
        fft.forward (frame.data(), spectrum.data());
        bands.accumulate (spectrum.data(), power, 1.0, powers.data());
    }
    for (auto& p : powers)
        p /= frames;
    return loudness::loudnessOf (powers.data(), bands, weighting);
}

/** BS.1770 K-weighted mean-square level (LUFS without gating; steady signals). */
double kWeightedLoudness (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    // ITU-R BS.1770-4 pre-filter (high shelf) and RLB high pass at 48 kHz.
    const double b1[] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 };
    const double a1[] = { 1.0, -1.69065929318241, 0.73248077421585 };
    const double b2[] = { 1.0, -2.0, 1.0 };
    const double a2[] = { 1.0, -1.99004745483398, 0.99007225036621 };
    double s1[2] = {}, s2[2] = {};
    double sum = 0.0;
    for (std::size_t i = 0; i < to; ++i)
    {
        double v = x[i];
        double y = b1[0] * v + s1[0];
        s1[0] = b1[1] * v - a1[1] * y + s1[1];
        s1[1] = b1[2] * v - a1[2] * y;
        v = y;
        y = b2[0] * v + s2[0];
        s2[0] = b2[1] * v - a2[1] * y + s2[1];
        s2[1] = b2[2] * v - a2[2] * y;
        if (i >= from)
            sum += y * y;
    }
    return -0.691 + 10.0 * std::log10 (sum / static_cast<double> (to - from));
}
} // namespace

TEST_CASE ("ISO 226:2003 contours and the sone scale", "[loudness][AG-3]")
{
    // Values of the ISO 226:2003 40-phon contour.
    REQUIRE (loudness::phonToSpl (40.0, 1000.0) == Approx (40.01).margin (0.02));
    REQUIRE (loudness::phonToSpl (40.0, 100.0) == Approx (64.37).margin (0.02));
    REQUIRE (loudness::phonToSpl (40.0, 20.0) == Approx (99.85).margin (0.02));
    REQUIRE (loudness::phonToSpl (40.0, 4000.0) == Approx (36.65).margin (0.02));
    REQUIRE (loudness::phonToSpl (80.0, 1000.0) == Approx (80.0).margin (0.3));
    for (double phon : { 20.0, 40.0, 60.0, 80.0, 100.0 })
        for (double hz : { 30.0, 100.0, 1000.0, 4000.0, 10000.0 })
            REQUIRE (loudness::splToPhon (loudness::phonToSpl (phon, hz), hz) == Approx (phon).margin (0.1)); // ISO 226 inverse form

    REQUIRE (loudness::phonToSone (40.0) == Approx (1.0));
    REQUIRE (loudness::phonToSone (50.0) == Approx (2.0));
    REQUIRE (loudness::soneToPhon (loudness::phonToSone (73.0)) == Approx (73.0));
    REQUIRE (loudness::soneToPhon (loudness::phonToSone (25.0)) == Approx (25.0));
}

TEST_CASE ("Auto Gain matches loudness after a bell boost (AG-6, AG-3)", "[engine][autogain][AG-6][AG-3]")
{
    const auto weighting = GENERATE (AutoGainWeighting::Fletcher, AutoGainWeighting::Digital);
    const auto speed = GENERATE (AutoGainSpeed::Stable, AutoGainSpeed::Medium);

    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = bell (3000.0, 6.0, 1.0);
    GlobalParams params;
    params.autoGain = true;
    params.autoGainWeighting = weighting;
    params.autoGainSpeed = speed;

    const std::size_t total = 48000 * 20;
    auto left = pink (0.1, total);
    auto right = left;
    const auto input = left;
    engine.run (left, right, params, bands);

    const std::size_t from = total - 48000 * 4;
    const double modelIn = modelLoudness (input, from, total, weighting);
    const double modelOut = modelLoudness (left, from, total, weighting);
    const double lufsIn = kWeightedLoudness (input, from, total);
    const double lufsOut = kWeightedLoudness (left, from, total);
    INFO ("weighting " << static_cast<int> (weighting) << " speed " << static_cast<int> (speed) << ": model " << modelOut - modelIn
                       << " dB, BS.1770 " << lufsOut - lufsIn << " LU, correction " << engine.getAutoGainDb());
    REQUIRE (engine.getAutoGainDb() < -0.5); // the boost was compensated
    if (weighting == AutoGainWeighting::Fletcher)
    {
        REQUIRE (std::abs (modelOut - modelIn) <= 0.5);
        REQUIRE (std::abs (lufsOut - lufsIn) <= 1.0);
    }
    else
    {
        REQUIRE (std::abs (modelOut - modelIn) <= 0.2); // RMS energy
    }
}

TEST_CASE ("Auto Gain follows its speed (AG-4)", "[engine][autogain][AG-4]")
{
    // Through the whole engine (band smoothing included); the time constant itself is tested below.
    const auto speed = GENERATE (AutoGainSpeed::Fast, AutoGainSpeed::Medium);
    const double tau = loudness::autoGainSeconds (speed);

    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = bell (1000.0, 0.0, 1.0);
    bands[0].shape.shape = Shape::HighShelf;
    GlobalParams params;
    params.autoGain = true;
    params.autoGainSpeed = speed;
    params.autoGainWeighting = AutoGainWeighting::Digital;

    // Settle with a flat band, then switch on a +6 dB shelf.
    const std::size_t settle = static_cast<std::size_t> (kFs * std::max (1.0, 5.0 * tau));
    const std::size_t after = static_cast<std::size_t> (kFs * 6.0 * tau) + 48000;
    auto left = pink (0.1, settle), right = left;
    engine.run (left, right, params, bands);

    bands[0].shape.gainDb = 6.0;
    std::vector<std::pair<double, double>> trajectory;
    auto l2 = pink (0.1, after), r2 = l2;
    engine.run (l2, r2, params, bands,
                [&] (std::size_t done) { trajectory.emplace_back (static_cast<double> (done) / kFs, engine.getAutoGainDb()); });

    const double final = trajectory.back().second;
    REQUIRE (final < -1.0);
    double crossing = -1.0;
    for (const auto& [t, c] : trajectory)
        if (crossing < 0.0 && c <= 0.632 * final)
            crossing = t;
    INFO ("speed " << static_cast<int> (speed) << " tau " << tau << " s, 63 % after " << crossing << " s, final " << final << " dB");
    REQUIRE (crossing == Approx (tau).epsilon (0.2).margin (0.03));
}

TEST_CASE ("Auto Gain time constant per speed after a loudness step (AG-4)", "[autogain][AG-4]")
{
    const auto speed = GENERATE (AutoGainSpeed::VeryFast, AutoGainSpeed::Fast, AutoGainSpeed::Medium, AutoGainSpeed::Long);
    const double tau = loudness::autoGainSeconds (speed);

    loudness::AutoGain autoGain;
    autoGain.prepare (kFs, 1);
    const loudness::AutoGain::Settings settings { true, AutoGainWeighting::Digital, speed };
    std::array<float, kMaxChannels> weights {};
    std::array<bool, kMaxChannels> covered {};
    weights[0] = 1.0f;
    covered[0] = true;

    // The processed signal becomes 6 dB louder at once; the correction should follow with τ.
    const auto noise = pink (0.1, 1 << 18);
    const std::size_t stepAt = static_cast<std::size_t> (kFs * std::max (1.0, 3.0 * tau));
    const std::size_t total = stepAt + static_cast<std::size_t> (kFs * 6.0 * tau);
    std::vector<double> in (512), out (512);
    std::vector<std::pair<double, double>> trajectory;
    for (std::size_t offset = 0; offset < total; offset += 512)
    {
        for (std::size_t i = 0; i < 512; ++i)
        {
            in[i] = noise[(offset + i) % noise.size()];
            out[i] = in[i] * (offset + i >= stepAt ? 2.0 : 1.0);
        }
        const double* inPtr[] = { in.data() };
        double* outPtr[] = { out.data() };
        autoGain.process (inPtr, outPtr, 1, 512, settings, weights, covered);
        if (offset + 512 > stepAt)
            trajectory.emplace_back (static_cast<double> (offset + 512 - stepAt) / kFs, autoGain.getCorrectionDb());
    }

    const double final = trajectory.back().second;
    REQUIRE (final == Approx (-6.02).margin (0.3));
    double crossing = -1.0;
    for (const auto& [t, c] : trajectory)
        if (crossing < 0.0 && c <= 0.632 * final)
            crossing = t;

    // The analysis frame (4096 at 48 kHz, 1024 for Very Fast) delays the reaction by about half a
    // frame plus a hop; the block granularity of the test adds up to one block.
    const double frame = speed == AutoGainSpeed::VeryFast ? 1024.0 : 4096.0;
    const double analysisDelay = (0.5 * frame + 0.25 * frame + 512.0) / kFs;
    INFO ("speed " << static_cast<int> (speed) << ": 63 % after " << crossing << " s, tau " << tau << " s, analysis delay "
                   << analysisDelay);
    REQUIRE (crossing >= 0.8 * tau);
    REQUIRE (crossing <= 1.2 * tau + analysisDelay);
}

TEST_CASE ("Stable Auto Gain settles quickly after a settings change and holds in silence (AG-4, AG-5)", "[engine][autogain][AG-4][AG-5]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = bell (3000.0, 0.0, 1.0);
    GlobalParams params;
    params.autoGain = true;

    auto left = pink (0.1, 48000 * 5), right = left;
    engine.run (left, right, params, bands);
    REQUIRE (std::abs (engine.getAutoGainDb()) < 0.05);

    bands[0].shape.gainDb = 6.0;
    std::vector<double> after;
    auto l2 = pink (0.1, 48000 * 3), r2 = l2;
    engine.run (l2, r2, params, bands, [&] (std::size_t) { after.push_back (engine.getAutoGainDb()); });
    const double final = after.back();
    REQUIRE (final < -1.0);
    const std::size_t oneSecond = 48000 / 512;
    for (std::size_t i = oneSecond; i < after.size(); ++i)
        REQUIRE (after[i] == Approx (final).margin (0.1));

    // 60 s of silence: the correction holds and never jumps.
    std::vector<double> silence (48000 * 60, 0.0), silenceR = silence;
    double previous = engine.getAutoGainDb();
    engine.run (silence, silenceR, params, bands, [&] (std::size_t) {
        const double now = engine.getAutoGainDb();
        REQUIRE (std::abs (now - previous) <= 0.5 * 512.0 / 48.0); // ≤ 0.5 dB/ms
        previous = now;
    });
    REQUIRE (engine.getAutoGainDb() == Approx (final).margin (0.1));
}

TEST_CASE ("Auto Gain leaves channels without bands untouched (AG-1, CH-6)", "[engine][autogain][AG-1]")
{
    StereoEngine engine;
    BandSettingsArray bands {};
    bands[0] = bell (3000.0, 9.0, 1.0);
    bands[0].component = StereoComponent::Left;
    GlobalParams params;
    params.autoGain = true;
    params.autoGainSpeed = AutoGainSpeed::Fast;

    auto left = pink (0.1, 48000 * 4), right = left;
    const auto input = right;
    engine.run (left, right, params, bands);
    REQUIRE (engine.getAutoGainDb() < -1.0);
    REQUIRE (right == input);
}

TEST_CASE ("Band Auto Gain gives back the average change of Dynamic and Spectral bands (AG-2)", "[engine][autogain][AG-2]")
{
    const auto mode = GENERATE (BandMode::Dynamic, BandMode::Spectral);

    auto run = [mode] (bool autoGain) {
        StereoEngine engine;
        BandSettingsArray bands {};
        bands[0] = bell (2000.0, 0.0, 2.0);
        bands[0].mode = mode;
        bands[0].dynamics.thresholdDb = -30.0;
        bands[0].dynamics.ratio = 4.0;
        bands[0].dynamics.kneeDb = 0.0;
        bands[0].dynamics.rangeDb = 12.0;
        bands[0].dynamics.selectivity = 0.8;
        bands[0].dynamics.autoGain = autoGain;
        bands[0].dynamics.autoGainSeconds = loudness::autoGainSeconds (AutoGainSpeed::Medium);

        const std::size_t total = 48000 * 20;
        auto left = pink (0.05, total);
        for (std::size_t i = 0; i < total; ++i)
            left[i] += dbToGain (-10.0) * std::sin (kTwoPi * 2000.0 * static_cast<double> (i) / kFs);
        auto right = left;
        const auto input = left;
        engine.run (left, right, GlobalParams {}, bands);

        const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
        auto tone = [&] (const std::vector<double>& x, std::size_t from) {
            double re = 0.0, im = 0.0;
            for (std::size_t i = from; i < from + 48000; ++i)
            {
                re += x[i] * std::cos (kTwoPi * 2000.0 * static_cast<double> (i) / kFs);
                im += x[i] * std::sin (kTwoPi * 2000.0 * static_cast<double> (i) / kFs);
            }
            return 10.0 * std::log10 (re * re + im * im);
        };
        const std::size_t from = total - 48000 * 2;
        return tone (left, from) - tone (input, from - latency);
    };

    const double without = run (false);
    const double with = run (true);
    INFO ("mode " << static_cast<int> (mode) << ": tone change " << without << " dB without, " << with << " dB with band Auto Gain");
    REQUIRE (without < -3.0);
    REQUIRE (std::abs (with) <= 0.5);
}
