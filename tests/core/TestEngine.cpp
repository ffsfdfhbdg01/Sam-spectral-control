#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
