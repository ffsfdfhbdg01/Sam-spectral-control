#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

// Band solo (BAND-1, BAND-6): the band's region of what is heard, with the band's own processing
// and on the band's target only.

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

/** Engine at 48 kHz for a channel topology, run in 512-sample blocks. */
struct SoloEngine : Engine
{
    explicit SoloEngine (const ChannelTopology& topology = ChannelTopology::stereo())
    {
        EngineConfig config;
        config.sampleRate = kFs;
        config.maxBlockSize = 512;
        config.numChannels = topology.numChannels;
        config.topology = topology;
        prepare (config);
    }

    void run (std::vector<std::vector<double>>& channels, const GlobalParams& params, const BandSettingsArray& bands)
    {
        const auto length = channels[0].size();
        for (std::size_t offset = 0; offset < length; offset += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, length - offset));
            double* pointers[kMaxChannels];
            for (std::size_t c = 0; c < channels.size(); ++c)
                pointers[c] = channels[c].data() + offset;
            process (pointers, static_cast<int> (channels.size()), n, params, bands);
        }
    }
};

/** Amplitude of a tone in a segment (single-bin DFT with a Hann window). */
double toneAmplitude (const std::vector<double>& x, double hz, std::size_t from, std::size_t length)
{
    std::complex<double> sum = 0.0;
    double windowSum = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length));
        sum += w * x[from + i] * std::polar (1.0, -kTwoPi * hz * static_cast<double> (from + i) / kFs);
        windowSum += w;
    }
    return 2.0 * std::abs (sum) / windowSum;
}

std::vector<double> tone (double hz, double amplitude, int length, double phase = 0.0)
{
    std::vector<double> x (static_cast<std::size_t> (length));
    for (int i = 0; i < length; ++i)
        x[static_cast<std::size_t> (i)] = amplitude * std::sin (kTwoPi * hz * i / kFs + phase);
    return x;
}

BandSettings bell (double hz, double gainDb, double q, StereoComponent component = StereoComponent::All)
{
    BandSettings s;
    s.used = s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12 };
    s.component = component;
    return s;
}

constexpr int kLength = 72000;
constexpr std::ptrdiff_t kFrom = 36000; // after the solo crossfade and every ramp
constexpr std::size_t kWindow = 32768;

/** Level of a tone at the output, in dB relative to its input amplitude. */
double levelDb (const std::vector<double>& output, double hz, double inputAmplitude)
{
    return gainToDb (toneAmplitude (output, hz, static_cast<std::size_t> (kFrom), kWindow) / inputAmplitude);
}
} // namespace

TEST_CASE ("Solo plays the band region as heard: a boost is louder, a cut quieter (BAND-1)", "[engine][solo][BAND-1]")
{
    auto render = [] (double gainDb, bool solo, double hz) {
        SoloEngine engine;
        BandSettingsArray bands {};
        bands[5] = bell (1000.0, gainDb, 2.0);
        GlobalParams params;
        params.soloBand = solo ? 5 : -1;
        std::vector<std::vector<double>> channels { tone (hz, 0.2, kLength), tone (hz, 0.2, kLength) };
        engine.run (channels, params, bands);
        return levelDb (channels[0], hz, 0.2);
    };

    REQUIRE (render (6.0, false, 1000.0) == Approx (6.0).margin (0.05));
    // The band's own gain is heard in the solo: the region filter is unity at the band frequency.
    REQUIRE (render (6.0, true, 1000.0) == Approx (6.0).margin (0.05));
    REQUIRE (render (-12.0, true, 1000.0) == Approx (-12.0).margin (0.05));
    REQUIRE (render (0.0, true, 1000.0) == Approx (0.0).margin (0.05));
    // Outside the region the solo is attenuated.
    REQUIRE (render (6.0, true, 100.0) < -20.0);
    REQUIRE (render (6.0, true, 10000.0) < -20.0);

    // Solo of an inactive band does nothing.
    SoloEngine engine;
    BandSettingsArray bands {};
    GlobalParams params;
    params.soloBand = 2;
    std::vector<std::vector<double>> channels { tone (100.0, 0.2, kLength), tone (100.0, 0.2, kLength) };
    const auto input = channels;
    engine.run (channels, params, bands);
    REQUIRE (channels == input);
}

TEST_CASE ("Solo of a Mid, Side, Left or Right band plays only that part (BAND-1, CH-3)", "[engine][solo][CH-3]")
{
    // Left = mid + side, right = mid - side: a mid tone and a side tone in the band region.
    const auto mid = tone (1000.0, 0.2, kLength);
    const auto side = tone (1100.0, 0.1, kLength);
    std::vector<double> left (mid.size()), right (mid.size());
    for (std::size_t i = 0; i < mid.size(); ++i)
    {
        left[i] = mid[i] + side[i];
        right[i] = mid[i] - side[i];
    }

    auto render = [&] (StereoComponent component) {
        SoloEngine engine;
        BandSettingsArray bands {};
        bands[0] = bell (1050.0, 0.0, 1.0, component);
        GlobalParams params;
        params.soloBand = 0;
        std::vector<std::vector<double>> channels { left, right };
        engine.run (channels, params, bands);
        return channels;
    };
    auto components = [] (const std::vector<std::vector<double>>& out) {
        std::vector<double> m (out[0].size()), s (out[0].size());
        for (std::size_t i = 0; i < m.size(); ++i)
        {
            m[i] = 0.5 * (out[0][i] + out[1][i]);
            s[i] = 0.5 * (out[0][i] - out[1][i]);
        }
        return std::pair { m, s };
    };

    // Mid: the mid tone at its level, nothing of the side.
    {
        const auto out = render (StereoComponent::Mid);
        const auto [m, s] = components (out);
        REQUIRE (levelDb (m, 1000.0, 0.2) == Approx (0.0).margin (0.3));
        REQUIRE (levelDb (s, 1100.0, 0.1) < -100.0);
        REQUIRE (levelDb (m, 1100.0, 0.1) < -100.0);
    }
    // Side: the side tone, nothing of the mid.
    {
        const auto out = render (StereoComponent::Side);
        const auto [m, s] = components (out);
        REQUIRE (levelDb (s, 1100.0, 0.1) == Approx (0.0).margin (0.3));
        REQUIRE (levelDb (m, 1000.0, 0.2) < -100.0);
        REQUIRE (levelDb (s, 1000.0, 0.2) < -100.0);
    }
    // Left: the left channel only; the right one is silent.
    {
        const auto out = render (StereoComponent::Left);
        REQUIRE (std::all_of (out[1].begin() + kFrom, out[1].end(), [] (double v) { return v == 0.0; }));
        REQUIRE (levelDb (out[0], 1000.0, 0.2) == Approx (0.0).margin (0.3));
        REQUIRE (levelDb (out[0], 1100.0, 0.1) == Approx (0.0).margin (0.3));
    }
    // Right: the right channel only.
    {
        const auto out = render (StereoComponent::Right);
        REQUIRE (std::all_of (out[0].begin() + kFrom, out[0].end(), [] (double v) { return v == 0.0; }));
        REQUIRE (levelDb (out[1], 1000.0, 0.2) == Approx (0.0).margin (0.3));
    }
}

TEST_CASE ("Solo follows a Dynamic band's gain and a Spectral band's reduction (BAND-1, DYN-11)", "[engine][solo][BAND-1]")
{
    auto render = [] (const BandSettings& band, bool solo) {
        SoloEngine engine;
        BandSettingsArray bands {};
        bands[3] = band;
        GlobalParams params;
        params.soloBand = solo ? 3 : -1;
        std::vector<std::vector<double>> channels { tone (1000.0, 0.2, kLength), tone (1000.0, 0.2, kLength) };
        engine.run (channels, params, bands);
        return levelDb (channels[0], 1000.0, 0.2);
    };

    // Dynamic: -14 dBFS against a -40 dBFS threshold at 4:1 reaches the 12 dB range.
    auto dynamic = bell (1000.0, 0.0, 2.0);
    dynamic.mode = BandMode::Dynamic;
    dynamic.dynamics.thresholdDb = -40.0;
    dynamic.dynamics.ratio = 4.0;
    dynamic.dynamics.rangeDb = 12.0;
    dynamic.dynamics.kneeDb = 0.0;
    const double dynamicOut = render (dynamic, false);
    REQUIRE (dynamicOut == Approx (-12.0).margin (0.5));
    REQUIRE (render (dynamic, true) == Approx (dynamicOut).margin (0.1));

    // Spectral: the tone over the threshold is reduced, and the solo is reduced as much.
    auto spectral = bell (1000.0, 0.0, 1.0);
    spectral.mode = BandMode::Spectral;
    spectral.dynamics.thresholdDb = -30.0;
    spectral.dynamics.ratio = 100.0;
    spectral.dynamics.rangeDb = 18.0;
    spectral.dynamics.kneeDb = 0.0;
    spectral.dynamics.selectivity = 0.9;
    const double spectralOut = render (spectral, false);
    REQUIRE (spectralOut < -6.0);
    REQUIRE (render (spectral, true) == Approx (spectralOut).margin (0.1));
}

TEST_CASE ("Solo is heard without the Auto Gain correction, and with Delta plays the change (AG-5, GLB-2)", "[engine][solo][AG-5]")
{
    auto render = [] (bool autoGain, bool delta, bool solo, float* correctionDb = nullptr) {
        SoloEngine engine;
        BandSettingsArray bands {};
        bands[0] = bell (1000.0, 8.0, 1.0);
        GlobalParams params;
        params.autoGain = autoGain;
        params.autoGainSpeed = AutoGainSpeed::VeryFast;
        params.delta = delta;
        params.soloBand = solo ? 0 : -1;
        std::vector<std::vector<double>> channels { tone (1000.0, 0.2, kLength), tone (1000.0, 0.2, kLength) };
        engine.run (channels, params, bands);
        if (correctionDb != nullptr)
            *correctionDb = engine.getAutoGainDb();
        return levelDb (channels[0], 1000.0, 0.2);
    };

    float correction = 0.0f;
    const double compensated = render (true, false, false, &correction);
    REQUIRE (correction < -1.0f);
    REQUIRE (compensated < 8.0 - 1.0); // the output is compensated…
    // …the solo is not: it plays the band's +8 dB.
    REQUIRE (render (true, false, true) == Approx (8.0).margin (0.05));

    // Delta + Solo: the band region of what the EQ changes, |1 - 10^(8/20)| at the band frequency.
    const double change = gainToDb (dbToGain (8.0) - 1.0);
    REQUIRE (render (false, true, true) == Approx (change).margin (0.05));
    REQUIRE (render (true, true, true) == Approx (change).margin (0.05));
}

TEST_CASE ("Solo of a band on one speaker plays that speaker only; the LFE stays silent (BAND-1, CH-4)", "[engine][solo][CH-4]")
{
    const SpeakerRole roles[] = { SpeakerRole::Left, SpeakerRole::Right,        SpeakerRole::Centre,
                                  SpeakerRole::Lfe,  SpeakerRole::LeftSurround, SpeakerRole::RightSurround };
    const auto topology = ChannelTopology::fromRoles (roles, 6);
    SoloEngine engine (topology);
    BandSettingsArray bands {};
    bands[1] = bell (1000.0, -6.0, 1.0);
    bands[1].channelMask = 1ull << 2; // the centre
    GlobalParams params;
    params.soloBand = 1;

    std::vector<std::vector<double>> channels;
    for (int c = 0; c < 6; ++c)
        channels.push_back (tone (1000.0, 0.2, kLength, 0.3 * c));
    engine.run (channels, params, bands);

    for (int c = 0; c < 6; ++c)
    {
        INFO ("channel " << c);
        const auto& x = channels[static_cast<std::size_t> (c)];
        if (c == 2)
            REQUIRE (levelDb (x, 1000.0, 0.2) == Approx (-6.0).margin (0.05));
        else
            REQUIRE (std::all_of (x.begin() + kFrom, x.end(), [] (double v) { return v == 0.0; }));
    }
}

TEST_CASE ("Solo fades in and out without a jump", "[engine][solo]")
{
    SoloEngine engine;
    BandSettingsArray bands {};
    bands[0] = bell (1000.0, 3.0, 1.0);
    GlobalParams params;
    std::vector<std::vector<double>> channels { tone (1000.0, 0.2, kLength), tone (1000.0, 0.2, kLength) };
    double worst = 0.0;
    for (std::size_t offset = 0; offset < static_cast<std::size_t> (kLength); offset += 512)
    {
        params.soloBand = (offset / 512) % 40 < 20 ? 0 : -1; // toggles every 213 ms
        const int n = static_cast<int> (std::min<std::size_t> (512, static_cast<std::size_t> (kLength) - offset));
        double* pointers[] = { channels[0].data() + offset, channels[1].data() + offset };
        engine.process (pointers, 2, n, params, bands);
    }
    for (std::size_t i = 1; i < channels[0].size(); ++i)
        worst = std::max (worst, std::abs (channels[0][i] - channels[0][i - 1]));
    // A 1 kHz tone at 0.28 changes by at most 0.037 per sample: no step on top of that.
    REQUIRE (worst < 0.05);
}
