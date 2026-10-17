#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

// External sidechain of Dynamic and Spectral bands (DYN-4): the band detects the sidechain input,
// mapped onto its target, and processes its own signal.

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kLength = 72000;
constexpr std::size_t kFrom = 36000, kWindow = 32768;

std::vector<double> tone (double hz, double amplitude, int length = kLength)
{
    std::vector<double> x (static_cast<std::size_t> (length));
    for (int i = 0; i < length; ++i)
        x[static_cast<std::size_t> (i)] = amplitude * std::sin (kTwoPi * hz * i / kFs);
    return x;
}

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

BandSettings dynamicBell (BandMode mode = BandMode::Dynamic, StereoComponent component = StereoComponent::All)
{
    BandSettings s;
    s.used = s.active = true;
    s.shape = { Shape::Bell, 1000.0, 0.0, 2.0, Slope::Db12 };
    s.component = component;
    s.mode = mode;
    s.dynamics.thresholdDb = -40.0;
    s.dynamics.ratio = 4.0;
    s.dynamics.rangeDb = 12.0;
    s.dynamics.kneeDb = 0.0;
    s.dynamics.selectivity = 0.9;
    s.dynamics.sidechain = true;
    return s;
}

/** Runs the main stereo signal with a sidechain (0, 1 or 2 channels) through the engine. */
std::vector<std::vector<double>> render (const BandSettings& band, std::vector<std::vector<double>> main,
                                         const std::vector<std::vector<double>>& sidechain, Oversampling oversampling = Oversampling::Off)
{
    Engine engine;
    EngineConfig config;
    config.sampleRate = kFs;
    config.maxBlockSize = 512;
    engine.prepare (config);
    BandSettingsArray bands {};
    bands[0] = band;
    GlobalParams params;
    params.oversampling = oversampling;
    for (std::size_t offset = 0; offset < main[0].size(); offset += 512)
    {
        const int n = static_cast<int> (std::min<std::size_t> (512, main[0].size() - offset));
        double* io[] = { main[0].data() + offset, main[1].data() + offset };
        const double* sc[2] = { nullptr, nullptr };
        for (std::size_t k = 0; k < sidechain.size(); ++k)
            sc[k] = sidechain[k].data() + offset;
        engine.process (io, 2, n, params, bands, sidechain.empty() ? nullptr : sc, static_cast<int> (sidechain.size()));
    }
    return main;
}

/** Level change of the 1 kHz tone in a channel, in dB (input amplitude 0.2). */
double changeDb (const std::vector<double>& out)
{
    return gainToDb (toneAmplitude (out, 1000.0, kFrom, kWindow) / 0.2);
}
} // namespace

TEST_CASE ("A Dynamic band set to the sidechain detects the sidechain, not its own signal (DYN-4)", "[engine][sidechain]")
{
    const std::vector<std::vector<double>> main { tone (1000.0, 0.2), tone (1000.0, 0.2) };
    const auto silence = std::vector<double> (static_cast<std::size_t> (kLength), 0.0);

    // Its own signal (-14 dBFS against a -40 dBFS threshold) would reach the 12 dB range…
    auto internal = dynamicBell();
    internal.dynamics.sidechain = false;
    REQUIRE (changeDb (render (internal, main, {})[0]) == Approx (-12.0).margin (0.5));

    // …but with a silent sidechain, or none connected, nothing happens.
    REQUIRE (changeDb (render (dynamicBell(), main, { silence, silence })[0]) == Approx (0.0).margin (0.05));
    REQUIRE (changeDb (render (dynamicBell(), main, {})[0]) == Approx (0.0).margin (0.05));

    // A loud sidechain in the band's region does it; one well outside it (the detection filter takes
    // 36 dB off at 30 Hz) does not.
    REQUIRE (changeDb (render (dynamicBell(), main, { tone (1000.0, 0.2), tone (1000.0, 0.2) })[0]) == Approx (-12.0).margin (0.5));
    REQUIRE (changeDb (render (dynamicBell(), main, { tone (30.0, 0.2), tone (30.0, 0.2) })[0]) > -0.5);

    // A quiet main signal is reduced by a loud sidechain just the same.
    const std::vector<std::vector<double>> quiet { tone (1000.0, 0.002), tone (1000.0, 0.002) };
    const auto out = render (dynamicBell(), quiet, { tone (1000.0, 0.2), tone (1000.0, 0.2) });
    REQUIRE (gainToDb (toneAmplitude (out[0], 1000.0, kFrom, kWindow) / 0.002) == Approx (-12.0).margin (0.5));

    // The same with oversampling: the sidechain goes through the same upsampling.
    REQUIRE (changeDb (render (dynamicBell(), main, { tone (1000.0, 0.2), tone (1000.0, 0.2) }, Oversampling::X4)[0])
             == Approx (-12.0).margin (0.5));
    REQUIRE (changeDb (render (dynamicBell(), main, { silence, silence }, Oversampling::X4)[0]) == Approx (0.0).margin (0.05));
}

TEST_CASE ("The sidechain maps onto the band's target: left, right, mid; mono feeds both (DYN-4, CH-3)", "[engine][sidechain]")
{
    const std::vector<std::vector<double>> main { tone (1000.0, 0.2), tone (1000.0, 0.2) };
    const auto silence = std::vector<double> (static_cast<std::size_t> (kLength), 0.0);
    const auto loud = tone (1000.0, 0.2);

    // Only the sidechain's right channel is loud.
    const std::vector<std::vector<double>> rightOnly { silence, loud };
    REQUIRE (changeDb (render (dynamicBell (BandMode::Dynamic, StereoComponent::Left), main, rightOnly)[0]) == Approx (0.0).margin (0.05));
    REQUIRE (changeDb (render (dynamicBell (BandMode::Dynamic, StereoComponent::Right), main, rightOnly)[1])
             == Approx (-12.0).margin (0.5));
    // The mid of the sidechain is half of it, still far over the threshold.
    const auto mid = render (dynamicBell (BandMode::Dynamic, StereoComponent::Mid), main, rightOnly);
    REQUIRE (changeDb (mid[0]) == Approx (-12.0).margin (0.5));

    // A mono sidechain keys both sides.
    const auto both = render (dynamicBell (BandMode::Dynamic, StereoComponent::All), main, { loud });
    REQUIRE (changeDb (both[0]) == Approx (-12.0).margin (0.5));
    REQUIRE (changeDb (both[1]) == Approx (-12.0).margin (0.5));
}

TEST_CASE ("A Spectral band set to the sidechain takes its levels from the sidechain's spectrum (DYN-4)", "[engine][sidechain][spectral]")
{
    const std::vector<std::vector<double>> main { tone (1000.0, 0.2), tone (1000.0, 0.2) };
    const auto silence = std::vector<double> (static_cast<std::size_t> (kLength), 0.0);
    auto band = dynamicBell (BandMode::Spectral);
    band.dynamics.thresholdDb = -30.0;
    band.dynamics.ratio = 100.0;
    band.dynamics.rangeDb = 18.0;

    auto internal = band;
    internal.dynamics.sidechain = false;
    const double own = changeDb (render (internal, main, {})[0]);
    REQUIRE (own < -6.0); // its own loud tone is reduced…

    // …with the sidechain only when the sidechain is loud in that region.
    REQUIRE (changeDb (render (band, main, { silence, silence })[0]) == Approx (0.0).margin (0.2));
    REQUIRE (changeDb (render (band, main, { tone (1000.0, 0.2), tone (1000.0, 0.2) })[0]) == Approx (own).margin (1.0));
    REQUIRE (changeDb (render (band, main, { tone (5000.0, 0.2), tone (5000.0, 0.2) })[0]) == Approx (0.0).margin (0.2));
}

TEST_CASE ("Keying: the band follows the sidechain's envelope (DYN-4)", "[engine][sidechain]")
{
    // The sidechain is loud for the first half and silent for the second.
    auto key = tone (1000.0, 0.2);
    std::fill (key.begin() + kLength / 2, key.end(), 0.0);
    const auto out = render (dynamicBell(), { tone (1000.0, 0.2), tone (1000.0, 0.2) }, { key, key });
    const double during = gainToDb (toneAmplitude (out[0], 1000.0, 12000, 16384) / 0.2);
    const double after = gainToDb (toneAmplitude (out[0], 1000.0, 52000, 16384) / 0.2);
    REQUIRE (during == Approx (-12.0).margin (0.5));
    REQUIRE (after == Approx (0.0).margin (0.1));
}

TEST_CASE ("A band switched back to External starts from silence, not from the sidechain it last heard (DYN-4)", "[engine][sidechain]")
{
    // 1: External, loud sidechain (12 dB off); 2: Internal, its own quiet signal (back to 0 dB);
    // 3: External again, silent sidechain: the gain stays at 0 dB from the first sample.
    for (const auto mode : { BandMode::Dynamic, BandMode::Spectral })
        for (const auto oversampling : { Oversampling::Off, Oversampling::X2 })
        {
            CAPTURE (static_cast<int> (mode), static_cast<int> (oversampling));
            auto band = dynamicBell (mode);
            band.dynamics.thresholdMode = ThresholdMode::Absolute;
            Engine engine;
            EngineConfig config;
            config.sampleRate = kFs;
            config.maxBlockSize = 512;
            engine.prepare (config);
            GlobalParams params;
            params.maxLookaheadMs = 20.0;
            params.oversampling = oversampling;
            BandSettingsArray bands {};

            constexpr int kPhase = 48000;
            auto main = tone (1000.0, 0.002, 3 * kPhase);
            auto loud = tone (1000.0, 0.2, 3 * kPhase);
            std::fill (loud.begin() + kPhase, loud.end(), 0.0);
            std::vector<double> right = main;
            for (int offset = 0; offset < 3 * kPhase; offset += 500)
            {
                band.dynamics.sidechain = offset < kPhase || offset >= 2 * kPhase;
                bands[0] = band;
                double* io[] = { main.data() + offset, right.data() + offset };
                const double* sc[] = { loud.data() + offset, loud.data() + offset };
                engine.process (io, 2, 500, params, bands, sc, 2);
            }
            const auto latency = static_cast<std::size_t> (engine.getLatencySamples());

            REQUIRE (gainToDb (toneAmplitude (main, 1000.0, 24000 + latency, 16384) / 0.002) == Approx (-12.0).margin (0.5));
            // From the switch on, 10 ms at a time.
            double worst = 0.0;
            for (std::size_t from = 2 * kPhase + latency; from + 480 <= static_cast<std::size_t> (3 * kPhase); from += 480)
                worst = std::min (worst, gainToDb (toneAmplitude (main, 1000.0, from, 480) / 0.002));
            CHECK (worst > -0.2);
        }
}
