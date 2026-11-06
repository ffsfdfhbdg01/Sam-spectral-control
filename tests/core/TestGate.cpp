#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

// Gate mode of a band (DYN-12): the band's region is attenuated by Range while its level stays under
// the threshold; it opens at the threshold, closes under threshold − hysteresis after the hold time,
// opens with the attack and closes with the release.

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

BandSettings gateBell (double thresholdDb = -40.0)
{
    BandSettings s;
    s.active = s.used = true;
    s.shape = { Shape::Bell, 1000.0, 0.0, 1.0, Slope::Db12 };
    s.mode = BandMode::Gate;
    s.dynamics.thresholdDb = thresholdDb;
    s.dynamics.rangeDb = 24.0;
    s.dynamics.attackMs = 1.0;
    s.dynamics.holdMs = 50.0;
    s.dynamics.releaseMs = 20.0;
    s.dynamics.hysteresisDb = 6.0;
    return s;
}

/** A 1 kHz tone whose level (dBFS) changes per segment: {samples, level}. */
std::vector<double> toneSegments (std::initializer_list<std::pair<int, double>> segments)
{
    std::vector<double> x;
    for (const auto& [length, levelDb] : segments)
        for (int i = 0; i < length; ++i)
        {
            const auto n = x.size();
            x.push_back (dbToGain (levelDb) * std::sin (kTwoPi * 1000.0 * static_cast<double> (n) / kFs));
        }
    return x;
}

struct Run
{
    std::vector<double> in, out;

    /** Change of the tone (dB) over 5 ms starting at `ms`. */
    double changeAt (double ms) const
    {
        const auto from = static_cast<std::size_t> (ms * 0.001 * kFs);
        double a = 0.0, b = 0.0;
        for (std::size_t i = from; i < from + 240; ++i)
        {
            a += out[i] * out[i];
            b += in[i] * in[i];
        }
        return 10.0 * std::log10 (a / b);
    }
};

Run run (const BandSettings& band, std::vector<double> in, const std::vector<double>* sidechain = nullptr)
{
    Engine engine;
    EngineConfig config;
    config.sampleRate = kFs;
    config.maxBlockSize = kBlock;
    engine.prepare (config);
    BandSettingsArray bands {};
    bands[0] = band;
    Run r { in, in };
    auto right = in;
    for (std::size_t offset = 0; offset < in.size(); offset += kBlock)
    {
        const int n = static_cast<int> (std::min<std::size_t> (kBlock, in.size() - offset));
        double* io[] = { r.out.data() + offset, right.data() + offset };
        if (sidechain != nullptr)
        {
            const double* sc[] = { sidechain->data() + offset };
            engine.process (io, 2, n, GlobalParams {}, bands, sc, 1);
        }
        else
        {
            engine.process (io, 2, n, GlobalParams {}, bands);
        }
    }
    REQUIRE (engine.getLatencySamples() == 0);
    return r;
}
} // namespace

TEST_CASE ("Gate: open over the threshold, closed by Range under it (DYN-12)", "[engine][gate][DYN-12]")
{
    // −20 dBFS against −40: open, nothing changes. −60 dBFS: closed, 24 dB off.
    CHECK (run (gateBell(), toneSegments ({ { 24000, -20.0 } })).changeAt (400.0) == Approx (0.0).margin (0.05));
    CHECK (run (gateBell(), toneSegments ({ { 24000, -60.0 } })).changeAt (400.0) == Approx (-24.0).margin (0.3));

    // Mix scales the change; Gain is added to it.
    auto half = gateBell();
    half.dynamics.mix = 0.5;
    CHECK (run (half, toneSegments ({ { 24000, -60.0 } })).changeAt (400.0) == Approx (-12.0).margin (0.3));
    auto lifted = gateBell();
    lifted.shape.gainDb = 3.0;
    CHECK (run (lifted, toneSegments ({ { 24000, -20.0 } })).changeAt (400.0) == Approx (3.0).margin (0.1));
    CHECK (run (lifted, toneSegments ({ { 24000, -60.0 } })).changeAt (400.0) == Approx (-21.0).margin (0.3));

    // Only the band's region, with its shape: three octaves from a closed gate a tone changes as
    // through a Static band at −Range (the skirt of the bell), however quiet it is.
    auto far = gateBell();
    far.shape.frequencyHz = 125.0;
    auto fixed = far;
    fixed.mode = BandMode::Static;
    fixed.shape.gainDb = -24.0;
    const double skirt = run (fixed, toneSegments ({ { 24000, -60.0 } })).changeAt (400.0);
    CHECK (skirt > -1.5);
    CHECK (run (far, toneSegments ({ { 24000, -60.0 } })).changeAt (400.0) == Approx (skirt).margin (0.1));
}

TEST_CASE ("Gate: hysteresis keeps the state between the two thresholds (DYN-12)", "[engine][gate][DYN-12]")
{
    // −43 dBFS lies between the closing (−46) and the opening (−40) threshold.
    const auto fromOpen = run (gateBell(), toneSegments ({ { 9600, -20.0 }, { 24000, -43.0 } }));
    CHECK (fromOpen.changeAt (600.0) == Approx (0.0).margin (0.1));
    const auto fromClosed = run (gateBell(), toneSegments ({ { 9600, -60.0 }, { 24000, -43.0 } }));
    CHECK (fromClosed.changeAt (600.0) == Approx (-24.0).margin (0.3));

    // Without hysteresis the same level closes it.
    auto none = gateBell();
    none.dynamics.hysteresisDb = 0.0;
    CHECK (run (none, toneSegments ({ { 9600, -20.0 }, { 24000, -43.0 } })).changeAt (600.0) == Approx (-24.0).margin (0.3));
}

TEST_CASE ("Gate: hold, then the release closes it; the attack opens it (DYN-12)", "[engine][gate][DYN-12]")
{
    // Loud for 200 ms, then −70 dBFS: open through the hold (50 ms), then closing with 20 ms.
    const auto drop = run (gateBell(), toneSegments ({ { 9600, -20.0 }, { 14400, -70.0 } }));
    CHECK (drop.changeAt (230.0) == Approx (0.0).margin (0.2)); // 30 ms after the drop: held
    CHECK (drop.changeAt (275.0) < -6.0);                       // 75 ms: closing
    CHECK (drop.changeAt (400.0) == Approx (-24.0).margin (0.3));

    // A longer hold keeps it open longer.
    auto longHold = gateBell();
    longHold.dynamics.holdMs = 150.0;
    CHECK (run (longHold, toneSegments ({ { 9600, -20.0 }, { 14400, -70.0 } })).changeAt (275.0) == Approx (0.0).margin (0.2));

    // Closed, then loud: open within a few attack times.
    const auto onset = run (gateBell(), toneSegments ({ { 9600, -60.0 }, { 9600, -20.0 } }));
    CHECK (onset.changeAt (190.0) == Approx (-24.0).margin (0.3));
    CHECK (onset.changeAt (212.0) > -1.0);
    auto slow = gateBell();
    slow.dynamics.attackMs = 50.0;
    CHECK (run (slow, toneSegments ({ { 9600, -60.0 }, { 9600, -20.0 } })).changeAt (212.0) < -6.0);
}

TEST_CASE ("Gate keyed by the sidechain: the band opens with the key, not with its own signal (DYN-12, DYN-4)", "[engine][gate][sidechain]")
{
    auto keyed = gateBell();
    keyed.dynamics.sidechain = true;
    const auto quiet = toneSegments ({ { 24000, -60.0 } });
    const auto loudKey = toneSegments ({ { 24000, -20.0 } });
    const auto silentKey = std::vector<double> (quiet.size(), 0.0);
    CHECK (run (keyed, quiet, &loudKey).changeAt (400.0) == Approx (0.0).margin (0.1));
    CHECK (run (keyed, toneSegments ({ { 24000, -20.0 } }), &silentKey).changeAt (400.0) == Approx (-24.0).margin (0.3));
}

TEST_CASE ("Gate ignores Auto Gain and reports its change for the display (DYN-12, DYN-11)", "[engine][gate][DYN-11]")
{
    auto band = gateBell();
    band.dynamics.autoGain = true;
    CHECK (run (band, toneSegments ({ { 48000, -60.0 } })).changeAt (900.0) == Approx (-24.0).margin (0.3));

    BandProcessor processor;
    processor.prepare (kFs, kBlock);
    auto x = toneSegments ({ { 24000, -60.0 } });
    for (std::size_t offset = 0; offset < x.size(); offset += kBlock)
    {
        double* io[] = { x.data() + offset };
        processor.process (gateBell(), ChannelTopology::mono(), io, 1, static_cast<int> (std::min<std::size_t> (kBlock, x.size() - offset)),
                           16);
    }
    CHECK (processor.getDynamicGainDb() == Approx (-24.0).margin (0.1));
}
