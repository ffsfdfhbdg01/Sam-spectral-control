#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/BandProcessor.h"
#include "ssc/engine/Engine.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;

double rms (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += x[i] * x[i];
    return std::sqrt (sum / static_cast<double> (to - from));
}

BandSettings bell (double hz, double gainDb, double q, StereoComponent component = StereoComponent::All)
{
    BandSettings s;
    s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12 };
    s.component = component;
    return s;
}

/** Runs a stereo signal through one band in host-sized blocks. */
void run (BandProcessor& band, const BandSettings& s, std::vector<double>& left, std::vector<double>& right, int controlBlock = 16,
          const ChannelTopology& topology = ChannelTopology::stereo())
{
    for (std::size_t offset = 0; offset < left.size(); offset += 256)
    {
        const int n = static_cast<int> (std::min<std::size_t> (256, left.size() - offset));
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        band.process (s, topology, ptrs, 2, n, controlBlock);
    }
}

std::vector<double> toDouble (const std::vector<float>& x)
{
    return { x.begin(), x.end() };
}
} // namespace

TEST_CASE ("Steady-state band gain matches the design", "[engine][band]")
{
    BandProcessor band;
    band.prepare (kFs, 256);

    auto left = toDouble (ssc::test::sine (1000.0, 0.25, kFs, 48000));
    auto right = left;
    run (band, bell (1000.0, 6.0, 1.0), left, right);

    const double gain = 20.0 * std::log10 (rms (left, 24000, 48000) / (0.25 / std::sqrt (2.0)));
    REQUIRE (gain == Approx (6.0).margin (0.02));
}

TEST_CASE ("Frequency sweeps are zipper-free (BAND-6)", "[engine][band][BAND-6]")
{
    // Same automation with the production control block and with per-sample redesign.
    BandProcessor fast, reference;
    fast.prepare (kFs, 256);
    reference.prepare (kFs, 256);

    const int total = 48000;
    auto signal = toDouble (ssc::test::sine (997.0, std::pow (10.0, -6.0 / 20.0), kFs, total));
    auto aL = signal, aR = signal, bL = signal, bR = signal;

    for (int offset = 0; offset < total; offset += 64)
    {
        const double t = static_cast<double> (offset) / total;
        const auto s = bell (20.0 * std::pow (1000.0, t), 12.0, 2.0);
        double* pa[] = { aL.data() + offset, aR.data() + offset };
        double* pb[] = { bL.data() + offset, bR.data() + offset };
        fast.process (s, ChannelTopology::stereo(), pa, 2, 64, 16);
        reference.process (s, ChannelTopology::stereo(), pb, 2, 64, 1);
    }

    double diff = 0.0, energy = 0.0;
    for (int i = 0; i < total; ++i)
    {
        diff += std::pow (aL[static_cast<std::size_t> (i)] - bL[static_cast<std::size_t> (i)], 2.0);
        energy += std::pow (bL[static_cast<std::size_t> (i)], 2.0);
    }
    REQUIRE (10.0 * std::log10 (diff / energy) <= -60.0);
}

TEST_CASE ("Shape changes crossfade without discontinuities", "[engine][band]")
{
    BandProcessor band;
    band.prepare (kFs, 256);

    const int total = 24000;
    auto left = toDouble (ssc::test::sine (440.0, 0.5, kFs, total));
    auto right = left;

    for (int offset = 0; offset < total; offset += 256)
    {
        BandSettings s = bell (440.0, 12.0, 1.0);
        if (offset >= 12000)
            s.shape.shape = Shape::LowShelf; // different topology → crossfade
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        band.process (s, ChannelTopology::stereo(), ptrs, 2, std::min (256, total - offset), 16);
    }

    // The largest step a 440 Hz sine of amplitude ≤ 2 (with +12 dB) can make, plus margin.
    const double maxStep = 2.1 * ssc::kTwoPi * 440.0 / kFs;
    for (std::size_t i = 1; i < left.size(); ++i)
        REQUIRE (std::abs (left[i] - left[i - 1]) <= maxStep);
}

TEST_CASE ("Mid band leaves the side signal untouched, Side band the mid (CH-3)", "[engine][band][CH-3]")
{
    const int total = 24000;
    const auto mid = toDouble (ssc::test::sine (1000.0, 0.3, kFs, total));
    const auto side = toDouble (ssc::test::sine (3000.0, 0.1, kFs, total));

    for (auto component : { StereoComponent::Mid, StereoComponent::Side })
    {
        BandProcessor band;
        band.prepare (kFs, 256);
        std::vector<double> left (total), right (total);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = mid[i] + side[i];
            right[i] = mid[i] - side[i];
        }
        // Boost exactly at the frequency of the component that should change.
        const double hz = component == StereoComponent::Mid ? 1000.0 : 3000.0;
        run (band, bell (hz, 6.0, 4.0, component), left, right);

        std::vector<double> outMid (total), outSide (total);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            outMid[i] = 0.5 * (left[i] + right[i]);
            outSide[i] = 0.5 * (left[i] - right[i]);
        }

        const auto& untouchedIn = component == StereoComponent::Mid ? side : mid;
        const auto& untouchedOut = component == StereoComponent::Mid ? outSide : outMid;
        for (std::size_t i = 0; i < untouchedIn.size(); ++i)
            REQUIRE (untouchedOut[i] == Approx (untouchedIn[i]).margin (1e-12));

        const auto& changedOut = component == StereoComponent::Mid ? outMid : outSide;
        const auto& changedIn = component == StereoComponent::Mid ? mid : side;
        REQUIRE (20.0 * std::log10 (rms (changedOut, 12000, total) / rms (changedIn, 12000, total)) == Approx (6.0).margin (0.05));
    }
}

TEST_CASE ("Left target changes only the left channel; Side does nothing on mono (CH-3)", "[engine][band][CH-3]")
{
    BandProcessor band;
    band.prepare (kFs, 256);
    const auto source = toDouble (ssc::test::sine (500.0, 0.3, kFs, 12000));
    auto left = source, right = source;
    run (band, bell (500.0, 9.0, 1.0, StereoComponent::Left), left, right);
    REQUIRE (right == source);
    REQUIRE (rms (left, 6000, 12000) > 1.5 * rms (source, 6000, 12000));

    BandProcessor monoBand;
    monoBand.prepare (kFs, 256);
    auto mono = source;
    for (std::size_t offset = 0; offset < mono.size(); offset += 256)
    {
        double* ptrs[] = { mono.data() + offset };
        monoBand.process (bell (500.0, 9.0, 1.0, StereoComponent::Side), ChannelTopology::mono(), ptrs, 1,
                          static_cast<int> (std::min<std::size_t> (256, mono.size() - offset)), 16);
    }
    REQUIRE (mono == source);
}

TEST_CASE ("Lane layouts for surround: LFE excluded from All by default (CH-4)", "[engine][CH-4]")
{
    const SpeakerRole roles[] = { SpeakerRole::Left,
                                  SpeakerRole::Right,
                                  SpeakerRole::Centre,
                                  SpeakerRole::Lfe,
                                  SpeakerRole::LeftSurroundSide,
                                  SpeakerRole::RightSurroundSide };
    const auto topo = ChannelTopology::fromRoles (roles, 6);
    REQUIRE (topo.numPairs == 2);

    const auto all = makeLanes (StereoComponent::All, ~0ull, false, topo);
    REQUIRE (all.numLanes == 5);
    const auto allWithLfe = makeLanes (StereoComponent::All, ~0ull, true, topo);
    REQUIRE (allWithLfe.numLanes == 6);

    const auto side = makeLanes (StereoComponent::Side, ~0ull, false, topo);
    REQUIRE (side.numLanes == 2); // L/R and Lss/Rss pairs
    const auto mid = makeLanes (StereoComponent::Mid, ~0ull, false, topo);
    REQUIRE (mid.numLanes == 3); // two pairs + centre as a single
}

TEST_CASE ("Engine applies active bands and leaves inactive ones bit-exact", "[engine][CH-6]")
{
    Engine engine;
    EngineConfig cfg;
    cfg.sampleRate = kFs;
    cfg.maxBlockSize = 512;
    engine.prepare (cfg);

    const auto source = ssc::test::sine (2000.0, 0.2, kFs, 48000);
    std::vector<float> left = source, right = source;

    BandSettingsArray bands {};
    bands[3] = bell (2000.0, -6.0, 2.0, StereoComponent::Left);
    for (std::size_t offset = 0; offset < left.size(); offset += 512)
    {
        float* ptrs[] = { left.data() + offset, right.data() + offset };
        engine.process (ptrs, 2, static_cast<int> (std::min<std::size_t> (512, left.size() - offset)), GlobalParams {}, bands);
    }

    REQUIRE (right == source); // the right channel is not targeted
    double sumL = 0.0, sumS = 0.0;
    for (std::size_t i = 24000; i < left.size(); ++i)
    {
        sumL += static_cast<double> (left[i]) * left[i];
        sumS += static_cast<double> (source[i]) * source[i];
    }
    REQUIRE (10.0 * std::log10 (sumL / sumS) == Approx (-6.0).margin (0.02));
}

TEST_CASE ("Band solo auditions the band region and fades back (BAND-1)", "[engine][BAND-1]")
{
    // Input: a tone at the band frequency and one a decade below.
    const int total = 48000;
    const auto inBand = ssc::test::sine (1000.0, 0.2, kFs, total);
    const auto outOfBand = ssc::test::sine (100.0, 0.2, kFs, total);

    auto levelDb = [&] (bool solo, const std::vector<float>& source) {
        Engine engine;
        EngineConfig cfg;
        cfg.sampleRate = kFs;
        cfg.maxBlockSize = 512;
        engine.prepare (cfg);

        BandSettingsArray bands {};
        bands[5] = bell (1000.0, 6.0, 2.0);
        GlobalParams params;
        params.soloBand = solo ? 5 : -1;

        std::vector<float> left = source, right = source;
        for (std::size_t offset = 0; offset < left.size(); offset += 512)
        {
            float* ptrs[] = { left.data() + offset, right.data() + offset };
            engine.process (ptrs, 2, static_cast<int> (std::min<std::size_t> (512, left.size() - offset)), params, bands);
        }
        double out = 0.0, in = 0.0;
        for (std::size_t i = 24000; i < left.size(); ++i)
        {
            out += static_cast<double> (left[i]) * left[i];
            in += static_cast<double> (source[i]) * source[i];
        }
        return 10.0 * std::log10 (out / in);
    };

    // Not soloed: the bell boosts its frequency.
    REQUIRE (levelDb (false, inBand) == Approx (6.0).margin (0.05));
    // Soloed: unity at the band frequency (the band's own gain is not applied), the rest is attenuated.
    REQUIRE (levelDb (true, inBand) == Approx (0.0).margin (0.05));
    REQUIRE (levelDb (true, outOfBand) < -20.0);

    // Solo of an inactive band does nothing.
    Engine engine;
    EngineConfig cfg;
    cfg.sampleRate = kFs;
    cfg.maxBlockSize = 512;
    engine.prepare (cfg);
    BandSettingsArray bands {};
    GlobalParams params;
    params.soloBand = 2;
    auto left = outOfBand, right = outOfBand;
    for (std::size_t offset = 0; offset < left.size(); offset += 512)
    {
        float* ptrs[] = { left.data() + offset, right.data() + offset };
        engine.process (ptrs, 2, static_cast<int> (std::min<std::size_t> (512, left.size() - offset)), params, bands);
    }
    REQUIRE (left == outOfBand);
}

TEST_CASE ("Audition filters cover the band region", "[engine][BAND-1]")
{
    using filters::BandShape;
    REQUIRE (auditionShapeFor ({ Shape::LowShelf, 200.0, 6.0, 1.0, Slope::Db12 }).shape == Shape::HighCut);
    REQUIRE (auditionShapeFor ({ Shape::LowCut, 80.0, 0.0, 0.7, Slope::Db24 }).shape == Shape::HighCut);
    REQUIRE (auditionShapeFor ({ Shape::HighShelf, 8000.0, 3.0, 1.0, Slope::Db12 }).shape == Shape::LowCut);
    const auto bellAudition = auditionShapeFor ({ Shape::Bell, 2000.0, -4.0, 40.0, Slope::Db12 });
    REQUIRE (bellAudition.shape == Shape::BandPass);
    REQUIRE (bellAudition.frequencyHz == 2000.0);
    REQUIRE (bellAudition.q == 20.0); // limited so a very narrow band stays audible
}

TEST_CASE ("Non-finite audio is muted and the filters recover (PLT-5)", "[engine][PLT-5]")
{
    Engine engine;
    EngineConfig cfg;
    cfg.sampleRate = kFs;
    cfg.maxBlockSize = 512;
    engine.prepare (cfg);

    BandSettingsArray bands {};
    bands[0] = bell (1000.0, 6.0, 1.0);
    GlobalParams params;
    params.mix = 0.5; // the dry path must be protected too

    const auto source = ssc::test::sine (1000.0, 0.25, kFs, 48128);
    std::vector<float> left = source, right = source;
    left[1000] = std::numeric_limits<float>::quiet_NaN();
    right[2000] = std::numeric_limits<float>::infinity();

    for (std::size_t offset = 0; offset < left.size(); offset += 512)
    {
        float* ptrs[] = { left.data() + offset, right.data() + offset };
        engine.process (ptrs, 2, 512, params, bands);
    }

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        REQUIRE (std::isfinite (left[i]));
        REQUIRE (std::isfinite (right[i]));
    }
    REQUIRE (engine.getNonFiniteEvents() >= 2);

    // Afterwards the band works normally again: 50 % mix of +6 dB and dry.
    double out = 0.0, in = 0.0;
    for (std::size_t i = 24000; i < left.size(); ++i)
    {
        out += static_cast<double> (left[i]) * left[i];
        in += static_cast<double> (source[i]) * source[i];
    }
    const double expected = 20.0 * std::log10 (0.5 * (std::pow (10.0, 6.0 / 20.0) + 1.0));
    REQUIRE (10.0 * std::log10 (out / in) == Approx (expected).margin (0.1));
}
