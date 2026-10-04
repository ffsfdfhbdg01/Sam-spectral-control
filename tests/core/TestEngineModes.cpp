#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <memory>
#include <numbers>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;

std::unique_ptr<Engine> makeEngine (double sampleRate = kFs, int maxBlock = 512, int channels = 2)
{
    auto engine = std::make_unique<Engine>();
    EngineConfig config;
    config.sampleRate = sampleRate;
    config.maxBlockSize = maxBlock;
    config.numChannels = channels;
    engine->prepare (config);
    return engine;
}

BandSettings bell (double hz, double gainDb, double q, StereoComponent component = StereoComponent::All)
{
    BandSettings s;
    s.used = s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12 };
    s.component = component;
    return s;
}

/** Runs stereo signals through the engine in blocks of `block` (0 = irregular sizes). */
void render (Engine& engine, std::vector<double>& left, std::vector<double>& right, const GlobalParams& params,
             const BandSettingsArray& bands, int block = 512)
{
    std::size_t offset = 0;
    int step = 0;
    while (offset < left.size())
    {
        const int size = block > 0 ? block : 1 + (step++ * 7919) % 1500;
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (size), left.size() - offset));
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        engine.process (ptrs, 2, n, params, bands);
        offset += static_cast<std::size_t> (n);
    }
}

const Oversampling kFactors[] = { Oversampling::X2, Oversampling::X4, Oversampling::X8 };
} // namespace

TEST_CASE ("Oversampling factor is capped at 400 kHz internally", "[engine][oversampling]")
{
    CHECK (maxOversamplingFor (44100.0) == 8);
    CHECK (maxOversamplingFor (48000.0) == 8);
    CHECK (maxOversamplingFor (96000.0) == 4);
    CHECK (maxOversamplingFor (192000.0) == 2);
    CHECK (maxOversamplingFor (384000.0) == 1);

    auto engine = makeEngine (192000.0);
    GlobalParams params;
    params.oversampling = Oversampling::X8;
    engine->configure (params, {});
    CHECK (engine->getOversamplingFactor() == 2);
}

TEST_CASE ("[OS-3] Engine latency with oversampling equals the measured delay", "[engine][oversampling][OS-3]")
{
    for (const auto phaseMode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
        for (const auto factor : kFactors)
        {
            CAPTURE (static_cast<int> (phaseMode), static_cast<int> (factor));
            auto engine = makeEngine();
            GlobalParams params;
            params.phaseMode = phaseMode;
            params.oversampling = factor;
            BandSettingsArray bands {};
            bands[0] = bell (1000.0, 0.0, 1.0); // a band at 0 dB: the channels take the oversampled path

            engine->configure (params, bands);
            const int latency = engine->getLatencySamples();
            const int correction = phaseMode == PhaseMode::NaturalPhase ? 32 : 0;
            CHECK (latency > correction);

            std::vector<double> left (8192, 0.0), right (8192, 0.0);
            left[100] = 1.0;
            render (*engine, left, right, params, bands);

            double m0 = 0.0, m1 = 0.0;
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                m0 += left[i];
                m1 += static_cast<double> (i) * left[i];
            }
            const double delay = m1 / m0 - 100.0;
            if (phaseMode == PhaseMode::NaturalPhase)
                CHECK (std::abs (delay - latency) < 1e-6); // linear phase: exact
            else
                CHECK (std::abs (delay - latency) <= 0.5 / oversamplingFactor (factor) + 1e-6);
        }
}

TEST_CASE ("Channels outside every band bypass oversampling exactly (OS-3, CH-6)", "[engine][oversampling][CH-6]")
{
    for (const auto phaseMode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
    {
        auto engine = makeEngine();
        GlobalParams params;
        params.phaseMode = phaseMode;
        params.oversampling = Oversampling::X4;
        params.quality = Quality::High;
        BandSettingsArray bands {};
        bands[0] = bell (2000.0, 6.0, 1.0, StereoComponent::Left);
        bands[0].shape.character = Character::Warm;

        const auto noise = ssc::test::pinkNoise (1 << 15);
        std::vector<double> input (noise.begin(), noise.end());
        auto left = input, right = input;
        render (*engine, left, right, params, bands, 0);

        const auto latency = static_cast<std::size_t> (engine->getLatencySamples());
        for (std::size_t i = latency; i < right.size(); ++i)
            REQUIRE (right[i] == input[i - latency]);
        double difference = 0.0;
        for (std::size_t i = latency; i < left.size(); ++i)
            difference = std::max (difference, std::abs (left[i] - input[i - latency]));
        CHECK (difference > 0.01); // the left channel is processed
    }
}

TEST_CASE ("Natural Phase without bands is an exact delay of D", "[engine][PH-1]")
{
    auto engine = makeEngine();
    GlobalParams params;
    params.phaseMode = PhaseMode::NaturalPhase;
    params.quality = Quality::Max;
    engine->configure (params, {});
    CHECK (engine->getLatencySamples() == 128);
    CHECK (engine->isPhaseCorrectionActive());

    const auto noise = ssc::test::pinkNoise (1 << 14);
    std::vector<double> input (noise.begin(), noise.end());
    auto left = input, right = input;
    render (*engine, left, right, params, {}, 333);
    for (std::size_t i = 128; i < left.size(); ++i)
    {
        REQUIRE (left[i] == input[i - 128]);
        REQUIRE (right[i] == input[i - 128]);
    }

    // Analog Feel uses the corrector only in High and Max.
    params.phaseMode = PhaseMode::AnalogFeel;
    params.quality = Quality::Normal;
    engine->configure (params, {});
    CHECK_FALSE (engine->isPhaseCorrectionActive());
    CHECK (engine->getLatencySamples() == 0);
}

TEST_CASE ("Natural Phase through the engine follows the analog prototype (PH-1)", "[engine][PH-1]")
{
    for (const auto factor : { Oversampling::Off, Oversampling::X2 })
    {
        CAPTURE (static_cast<int> (factor));
        auto engine = makeEngine();
        GlobalParams params;
        params.phaseMode = PhaseMode::NaturalPhase;
        params.quality = Quality::Normal;
        params.oversampling = factor;
        BandSettingsArray bands {};
        bands[0] = bell (12000.0, 9.0, 1.5);
        bands[1].used = bands[1].active = true;
        bands[1].shape = { Shape::HighShelf, 6000.0, -4.0, 0.7071, Slope::Db12 };

        // Let the corrector settle on its update grid, then measure the impulse response.
        const int settle = 4096, length = 1 << 15;
        std::vector<double> left (static_cast<std::size_t> (settle + length), 0.0), right (left.size(), 0.0);
        left[static_cast<std::size_t> (settle)] = 1.0;
        render (*engine, left, right, params, bands);
        const std::vector<double> h (left.begin() + settle, left.end());
        const auto spectrum = ssc::test::spectrumOf (h, static_cast<std::size_t> (length));

        // With oversampling the response is flat up to the passband edge of its filters (OS-2).
        const double top = factor == Oversampling::Off ? 0.9 * 0.5 * kFs : oversampling::filterSpecFor (params.quality).passband * kFs;
        const int latency = engine->getLatencySamples();
        double worstDb = 0.0, worstDeg = 0.0;
        for (std::size_t k = 1; k < spectrum.size(); ++k)
        {
            const double hz = static_cast<double> (k) * kFs / length;
            if (hz < 20.0 || hz > top)
                continue;
            std::complex<double> target = std::polar (1.0, -kTwoPi * hz * latency / kFs);
            for (int b = 0; b < 2; ++b)
                target *= filters::designBand (bands[static_cast<std::size_t> (b)].shape, kFs).analogResponse (hz);
            const auto ratio = spectrum[k] / target;
            worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (std::abs (ratio))));
            worstDeg = std::max (worstDeg, std::abs (std::arg (ratio)) * 180.0 / kPi);
        }
        CHECK (worstDb <= 0.1);
        CHECK (worstDeg <= 5.0);
    }
}

TEST_CASE ("Natural Phase brickwall cut through the engine is linear phase", "[engine][PH-1][BAND-3]")
{
    CHECK (phase::minimumFirBrickwallHz (Quality::Max, kFs) < 1500.0);
    CHECK (phase::minimumFirBrickwallHz (Quality::Normal, kFs) < 6000.0);

    auto engine = makeEngine();
    GlobalParams params;
    params.phaseMode = PhaseMode::NaturalPhase;
    params.quality = Quality::Max;
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::LowCut, 3000.0, 0.0, 0.7071, Slope::Brickwall };

    const int settle = 4096, length = 1 << 14;
    std::vector<double> left (static_cast<std::size_t> (settle + length), 0.0), right (left.size(), 0.0);
    left[static_cast<std::size_t> (settle)] = 1.0;
    render (*engine, left, right, params, bands);
    const std::vector<double> h (left.begin() + settle, left.end());

    const int latency = engine->getLatencySamples();
    REQUIRE (latency == 128);
    for (int i = 1; i <= latency; ++i)
        CHECK (std::abs (h[static_cast<std::size_t> (latency - i)] - h[static_cast<std::size_t> (latency + i)]) < 1e-6);
    const auto spectrum = ssc::test::spectrumOf (h, static_cast<std::size_t> (length));
    auto levelAt = [&] (double hz) {
        return 20.0 * std::log10 (std::abs (spectrum[static_cast<std::size_t> (hz * length / kFs)]) + 1e-30);
    };
    CHECK (levelAt (2000.0) < -60.0);
    CHECK (levelAt (500.0) < -60.0);
    CHECK (std::abs (levelAt (4000.0)) < 0.01);
    CHECK (std::abs (levelAt (15000.0)) < 0.01);

    // In Minimum Phase the same band is the 96 dB/oct IIR (not symmetric, no latency).
    params.phaseMode = PhaseMode::MinimumPhase;
    engine->configure (params, bands);
    CHECK (engine->getLatencySamples() == 0);
}

TEST_CASE ("Natural Phase brickwall below the FIR limit stays a steep minimum-phase IIR", "[engine][PH-1][BAND-3]")
{
    auto engine = makeEngine();
    GlobalParams params;
    params.phaseMode = PhaseMode::NaturalPhase;
    params.quality = Quality::Max;
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::LowCut, 300.0, 0.0, 0.7071, Slope::Brickwall };

    const int settle = 4096, length = 1 << 15;
    std::vector<double> left (static_cast<std::size_t> (settle + length), 0.0), right (left.size(), 0.0);
    left[static_cast<std::size_t> (settle)] = 1.0;
    render (*engine, left, right, params, bands);
    const std::vector<double> h (left.begin() + settle, left.end());
    const auto spectrum = ssc::test::spectrumOf (h, static_cast<std::size_t> (length));
    auto levelAt = [&] (double hz) {
        return 20.0 * std::log10 (std::abs (spectrum[static_cast<std::size_t> (hz * length / kFs)]) + 1e-30);
    };
    CHECK (levelAt (150.0) < -80.0); // one octave below: 96 dB/oct
    CHECK (std::abs (levelAt (2000.0)) < 0.05);
}

TEST_CASE ("Oversampling and Natural Phase do not depend on the host block size (NFR-3)", "[engine][NFR-3]")
{
    const auto noise = ssc::test::pinkNoise (1 << 15);
    std::vector<double> input (noise.begin(), noise.end());
    for (auto& x : input)
        x *= 0.3;

    BandSettingsArray bands {};
    bands[0] = bell (300.0, -4.0, 1.0);
    bands[0].mode = BandMode::Dynamic;
    bands[0].dynamics.thresholdDb = -35.0;
    bands[0].dynamics.lookaheadMs = 2.0;
    bands[1] = bell (5000.0, 4.0, 2.0, StereoComponent::Side);
    bands[1].shape.character = Character::Sharp;
    bands[2].used = bands[2].active = true;
    bands[2].shape = { Shape::LowCut, 50.0, 0.0, 0.7071, Slope::Brickwall };

    GlobalParams params;
    params.phaseMode = PhaseMode::NaturalPhase;
    params.oversampling = Oversampling::X2;
    params.maxLookaheadMs = 2.0;

    auto renderWith = [&] (int block) {
        auto engine = makeEngine();
        auto left = input;
        std::vector<double> right (input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            right[i] = 0.7 * input[(i + 101) % input.size()];
        render (*engine, left, right, params, bands, block);
        left.insert (left.end(), right.begin(), right.end());
        return left;
    };
    const auto reference = renderWith (512);
    CHECK (renderWith (64) == reference);
    CHECK (renderWith (1000) == reference);
    CHECK (renderWith (0) == reference);
}

TEST_CASE ("Character aliasing through the engine with oversampling (OS-1)", "[engine][oversampling][OS-1]")
{
    auto engine = makeEngine();
    GlobalParams params;
    params.oversampling = Oversampling::X2;
    BandSettingsArray bands {};
    bands[0] = bell (10000.0, 12.0, 1.0);
    bands[0].shape.character = Character::Warm;

    const int length = 19200;
    std::vector<double> left (static_cast<std::size_t> (length)), right (left.size(), 0.0);
    for (int i = 0; i < length; ++i)
        left[static_cast<std::size_t> (i)] = std::sin (2.0 * std::numbers::pi * 10000.0 * i / kFs);
    render (*engine, left, right, params, bands);

    // 10 Hz bins over the last 4800 samples: the aliases of the 3rd … 9th harmonics.
    auto amplitudeAt = [&] (double hz) {
        std::complex<double> sum (0.0, 0.0);
        for (int i = 0; i < 4800; ++i)
            sum += left[static_cast<std::size_t> (length - 4800 + i)] * std::polar (1.0, -2.0 * std::numbers::pi * hz * i / kFs);
        return 2.0 * std::abs (sum) / 4800.0;
    };
    const double fundamental = amplitudeAt (10000.0);
    double worst = 0.0;
    for (const double alias : { 18000.0, 8000.0, 2000.0, 12000.0, 22000.0, 6000.0, 4000.0 })
        worst = std::max (worst, amplitudeAt (alias));
    CHECK (20.0 * std::log10 (worst / fundamental) < -95.0);
}
