#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"

using namespace ssc;
using namespace ssc::engine;

namespace
{
std::vector<double> render (const std::vector<double>& input, int hostBlock)
{
    EngineConfig config;
    config.sampleRate = 48000.0;
    config.maxBlockSize = 1024;
    Engine engine;
    engine.prepare (config);

    BandSettingsArray bands {};
    auto band = [&bands] (int index, Shape shape, double hz, double gain, BandMode mode) -> BandSettings& {
        auto& s = bands[static_cast<std::size_t> (index)];
        s.used = s.active = true;
        s.shape = { shape, hz, gain, 1.5, Slope::Db24 };
        s.mode = mode;
        return s;
    };
    band (0, Shape::LowCut, 40.0, 0.0, BandMode::Static);
    band (1, Shape::Bell, 300.0, -4.0, BandMode::Dynamic).dynamics.thresholdDb = -35.0;
    band (2, Shape::Bell, 3000.0, 0.0, BandMode::Spectral).dynamics.thresholdMode = ThresholdMode::Relative;
    band (3, Shape::HighShelf, 8000.0, 2.0, BandMode::Static).component = StereoComponent::Side;
    GlobalParams params;
    params.autoGain = true;
    params.autoGainSpeed = AutoGainSpeed::Fast;
    params.maxLookaheadMs = 3.0;
    bands[1].dynamics.lookaheadMs = 3.0;
    bands[1].dynamics.autoGain = true;

    std::vector<double> left = input, right (input.size());
    for (std::size_t i = 0; i < input.size(); ++i)
        right[i] = 0.7 * input[(i + 101) % input.size()];

    // Host blocks of varying size, as a DAW may use in real time or offline.
    std::size_t offset = 0;
    int step = 0;
    while (offset < input.size())
    {
        const int size = hostBlock > 0 ? hostBlock : 1 + (step++ * 7919) % 2048;
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (size), input.size() - offset));
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        engine.process (ptrs, 2, n, params, bands);
        offset += static_cast<std::size_t> (n);
    }
    left.insert (left.end(), right.begin(), right.end());
    return left;
}
} // namespace

TEST_CASE ("Output does not depend on the host block size: offline render = real time (NFR-3)", "[engine][NFR-3]")
{
    const auto noise = ssc::test::pinkNoise (1 << 17);
    std::vector<double> input (noise.begin(), noise.end());
    for (auto& x : input)
        x *= 0.2;

    const auto reference = render (input, 512);
    REQUIRE (render (input, 64) == reference);
    REQUIRE (render (input, 1024) == reference);
    REQUIRE (render (input, 3000) == reference);
    REQUIRE (render (input, 0) == reference); // irregular block sizes
}
