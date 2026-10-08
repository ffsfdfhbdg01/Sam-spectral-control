#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"

using Catch::Approx;

namespace
{
struct StereoBuffer
{
    std::vector<float> left, right;
    float* ptrs[2];

    explicit StereoBuffer (const std::vector<float>& source)
        : left (source),
          right (source)
    {
        for (auto& v : right)
            v = -v;
        ptrs[0] = left.data();
        ptrs[1] = right.data();
    }
};

/** Stereo engine at 48 kHz, prepared for the given block size. */
struct PreparedEngine : ssc::engine::Engine
{
    explicit PreparedEngine (int maxBlock = 256)
    {
        ssc::engine::EngineConfig config;
        config.sampleRate = 48000.0;
        config.maxBlockSize = maxBlock;
        config.numChannels = 2;
        prepare (config);
    }
};
} // namespace

TEST_CASE ("Engine with default parameters is a bit-exact pass-through", "[engine][CH-6]")
{
    PreparedEngine engine;
    const auto source = ssc::test::sine (997.0, 0.7, 48000.0, 4096);
    StereoBuffer buffer (source);

    for (int offset = 0; offset < 4096; offset += 256)
    {
        float* block[] = { buffer.left.data() + offset, buffer.right.data() + offset };
        engine.process (block, 2, 256, {});
    }

    for (std::size_t i = 0; i < source.size(); ++i)
    {
        REQUIRE (buffer.left[i] == source[i]);
        REQUIRE (buffer.right[i] == -source[i]);
    }
}

TEST_CASE ("Engine processes host blocks larger than the prepared block size", "[engine]")
{
    PreparedEngine engine (64);
    const auto source = ssc::test::sine (440.0, 0.5, 48000.0, 1000);
    StereoBuffer buffer (source);

    ssc::engine::GlobalParams p;
    p.outputGainDb = -6.0;
    engine.process (buffer.ptrs, 2, 1000, p);

    // After the 20 ms ramp (960 samples) the gain has settled at -6 dB.
    const double g = std::pow (10.0, -6.0 / 20.0);
    REQUIRE (buffer.left[999] == Approx (source[999] * g).margin (1e-6));
}

TEST_CASE ("Output gain and mix reach their targets after the ramp", "[engine]")
{
    PreparedEngine engine;
    const auto source = ssc::test::sine (1000.0, 0.5, 48000.0, 9600);
    StereoBuffer buffer (source);

    ssc::engine::GlobalParams p;
    p.outputGainDb = 6.0;
    p.mix = 0.0; // fully dry: the (empty) chain result is irrelevant, so output = dry * gain
    for (int offset = 0; offset < 9600; offset += 256)
    {
        const int n = std::min (256, 9600 - offset);
        float* block[] = { buffer.left.data() + offset, buffer.right.data() + offset };
        engine.process (block, 2, n, p);
    }

    const double g = std::pow (10.0, 6.0 / 20.0);
    for (int i = 2000; i < 9600; ++i)
        REQUIRE (buffer.left[static_cast<std::size_t> (i)] == Approx (source[static_cast<std::size_t> (i)] * g).margin (1e-6));
}

TEST_CASE ("Bypass crossfades without discontinuities and keeps latency (GLB-1)", "[engine][GLB-1]")
{
    PreparedEngine engine;
    const auto source = ssc::test::sine (1000.0, 0.8, 48000.0, 24000);
    StereoBuffer buffer (source);

    // Gain differs between processed and bypassed paths, so the crossfade is audible in the data.
    ssc::engine::GlobalParams p;
    p.outputGainDb = -12.0;
    for (int offset = 0; offset < 24000; offset += 240)
    {
        p.bypass = offset >= 12000;
        float* block[] = { buffer.left.data() + offset, buffer.right.data() + offset };
        engine.process (block, 2, 240, p);
    }

    // Fully bypassed at the end: identical to the input (latency is 0 in M1).
    for (int i = 23000; i < 24000; ++i)
        REQUIRE (buffer.left[static_cast<std::size_t> (i)] == source[static_cast<std::size_t> (i)]);

    // No sample-to-sample jump larger than the sine itself can produce plus a small margin.
    const double maxSineStep = 0.8 * ssc::kTwoPi * 1000.0 / 48000.0;
    for (std::size_t i = 1; i < buffer.left.size(); ++i)
        REQUIRE (std::abs (buffer.left[i] - buffer.left[i - 1]) <= maxSineStep * 1.05);
}

TEST_CASE ("Delta: the input minus the output, crossfaded; dry + delta reconstructs the output (GLB-2)", "[engine][GLB-2]")
{
    using namespace ssc;
    using namespace ssc::engine;
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::Bell, 1200.0, -6.0, 1.0, Slope::Db12 };
    bands[1].used = bands[1].active = true;
    bands[1].shape = { Shape::HighShelf, 6000.0, 4.0, 0.7, Slope::Db12 };
    GlobalParams params;
    params.phaseMode = PhaseMode::NaturalPhase;
    params.mix = 0.8;
    params.outputGainDb = -2.0;

    const auto noise = ssc::test::pinkNoise (1 << 14);
    auto render = [&] (bool delta) {
        PreparedEngine engine (512);
        auto p = params;
        p.delta = delta;
        std::vector<double> left (noise.begin(), noise.end()), right (left.size(), 0.0);
        for (std::size_t offset = 0; offset < left.size(); offset += 512)
        {
            double* block[] = { left.data() + offset, right.data() + offset };
            engine.process (block, 2, 512, p, bands);
        }
        return std::make_pair (left, engine.getLatencySamples());
    };
    const auto [output, latency] = render (false);
    const auto [delta, deltaLatency] = render (true);
    REQUIRE (latency == deltaLatency);
    REQUIRE (latency > 0);

    // After the ramps (20 ms): delta = (dry − output / g) · g, so dry · g − delta = output.
    const double g = std::pow (10.0, -2.0 / 20.0);
    double worst = 0.0, energy = 0.0;
    for (std::size_t i = 2048; i < output.size(); ++i)
    {
        const double dry = noise[i - static_cast<std::size_t> (latency)];
        worst = std::max (worst, std::abs (dry * g - delta[i] - output[i]));
        energy += delta[i] * delta[i];
    }
    CHECK (worst < 1e-12);
    CHECK (energy > 1.0); // the bands change the signal: the delta is not silence

    // Without a change (no bands), the delta is silence.
    bands = {};
    const auto [silent, unused] = render (true);
    for (std::size_t i = 2048; i < silent.size(); ++i)
        REQUIRE (std::abs (silent[i]) < 1e-12);
}

TEST_CASE ("Delta is heard without the Auto Gain correction (AG-5, GLB-2)", "[engine][GLB-2][AG-5]")
{
    using namespace ssc;
    using namespace ssc::engine;
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::Bell, 3000.0, 8.0, 1.0, Slope::Db12 };

    const auto noise = ssc::test::pinkNoise (1 << 16);
    auto render = [&] (bool autoGain, bool delta, float* correctionDb) {
        PreparedEngine engine (512);
        GlobalParams p;
        p.autoGain = autoGain;
        p.autoGainSpeed = AutoGainSpeed::VeryFast;
        p.delta = delta;
        std::vector<double> left (noise.begin(), noise.end()), right (left.size(), 0.0);
        for (std::size_t offset = 0; offset < left.size(); offset += 512)
        {
            double* block[] = { left.data() + offset, right.data() + offset };
            engine.process (block, 2, 512, p, bands);
        }
        *correctionDb = engine.getAutoGainDb();
        return left;
    };

    float correction = 0.0f, none = 0.0f;
    const auto compensated = render (true, false, &correction);
    const auto plain = render (false, false, &none);
    REQUIRE (correction < -1.0f); // the boost is compensated…
    double difference = 0.0;
    for (std::size_t i = noise.size() / 2; i < noise.size(); ++i)
        difference = std::max (difference, std::abs (compensated[i] - plain[i]));
    REQUIRE (difference > 0.01); // …and the output shows it

    // …but the delta is the same with and without Auto Gain: it holds only what the EQ changes.
    const auto deltaWithAutoGain = render (true, true, &correction);
    const auto deltaPlain = render (false, true, &none);
    REQUIRE (correction < -1.0f);
    double worst = 0.0;
    for (std::size_t i = 2048; i < noise.size(); ++i) // after the Delta ramp (20 ms)
        worst = std::max (worst, std::abs (deltaWithAutoGain[i] - deltaPlain[i]));
    CHECK (worst < 1e-12);
}
