#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>

#include "ssc/analysis/BandReadouts.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ssc/util/Math.h"

using Catch::Approx;
using namespace ssc;

namespace
{
constexpr double kFs = 48000.0;

engine::BandSettings bandOf (Shape shape, double hz, double gainDb, double q, Slope slope = Slope::Db12)
{
    engine::BandSettings b;
    b.used = b.active = true;
    b.shape = { shape, hz, gainDb, q, slope };
    return b;
}

engine::GlobalParams paramsFor (PhaseMode mode, Quality quality = Quality::Normal)
{
    engine::GlobalParams p;
    p.phaseMode = mode;
    p.quality = quality;
    return p;
}

/** Group delay (samples) of a cascade of digital sections at omega (rad/sample), exact. */
double analyticGroupDelay (const filters::BandDesign& design, double omega)
{
    double tau = 0.0;
    for (int s = 0; s < design.numSections; ++s)
    {
        const auto& bq = design.digital[static_cast<std::size_t> (s)];
        const std::complex<double> z1 = std::polar (1.0, -omega), z2 = z1 * z1;
        const auto b = bq.b0 + bq.b1 * z1 + bq.b2 * z2, bRamp = bq.b1 * z1 + 2.0 * bq.b2 * z2;
        const auto a = 1.0 + bq.a1 * z1 + bq.a2 * z2, aRamp = bq.a1 * z1 + 2.0 * bq.a2 * z2;
        tau += (bRamp / b).real() - (aRamp / a).real();
    }
    return tau;
}
} // namespace

TEST_CASE ("Ringing of a resonator: post-ring = 2.2 Q / f0 at -60 dB (PR-3)", "[readouts][PR-3]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    for (const auto& [hz, q] : { std::pair { 1000.0, 10.0 }, std::pair { 200.0, 5.0 }, std::pair { 3000.0, 20.0 } })
    {
        // A single second-order resonator (6 dB/oct skirts).
        const auto r = readouts.compute (bandOf (Shape::BandPass, hz, 0.0, q, Slope::Db6), paramsFor (PhaseMode::MinimumPhase), 0.0);
        REQUIRE (r.valid);
        const double expected = 1000.0 * 2.2 * q / hz;
        std::printf ("[PR-3] band pass %.0f Hz Q %.0f: post-ring %.2f ms, expected %.2f ms\n", hz, q, r.postRingMs, expected);
        CHECK (r.postRingMs == Approx (expected).epsilon (0.05));
        CHECK (r.preRingMs == 0.0f); // Minimum Phase: nothing before the peak
    }
}

TEST_CASE ("Minimum Phase bands have no pre-ring; Natural Phase pre-ring stays within D (PR-1)", "[readouts][PR-1]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    const auto mp = paramsFor (PhaseMode::MinimumPhase);
    CHECK (readouts.compute (bandOf (Shape::Bell, 1000.0, 12.0, 4.0), mp, 0.0).preRingMs == 0.0f);
    CHECK (readouts.compute (bandOf (Shape::LowCut, 100.0, 0.0, 0.7071, Slope::Db48), mp, 0.0).preRingMs == 0.0f);
    // Causal responses that rise to a later peak (a 4th-order band pass, a steep high cut) do not
    // pre-ring either: nothing comes before the system's zero time.
    CHECK (readouts.compute (bandOf (Shape::BandPass, 300.0, 0.0, 5.0, Slope::Db12), mp, 0.0).preRingMs == 0.0f);
    CHECK (readouts.compute (bandOf (Shape::HighCut, 2000.0, 0.0, 0.7071, Slope::Db48), mp, 0.0).preRingMs == 0.0f);

    for (const auto quality : { Quality::Normal, Quality::Max })
    {
        const auto np = paramsFor (PhaseMode::NaturalPhase, quality);
        const auto r = readouts.compute (bandOf (Shape::Bell, 1000.0, 12.0, 4.0), np, 0.0);
        const double d = 1000.0 * (quality == Quality::Normal ? 32.0 : 128.0) / kFs;
        std::printf ("[PR-1] Natural Phase %s: pre-ring %.3f ms (D = %.3f ms), post-ring %.2f ms\n",
                     quality == Quality::Normal ? "Normal" : "Max", r.preRingMs, d, r.postRingMs);
        CHECK (r.preRingMs <= d + 1000.0 / kFs);
        CHECK (readouts.lastLatencySamples() == static_cast<int> (std::lround (d * kFs / 1000.0)));
    }
}

TEST_CASE ("Group delay at fc matches the analytic value of the design (PR-1)", "[readouts][PR-1]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    for (const auto& [shape, hz, gain, q] :
         { std::tuple { Shape::Bell, 1000.0, 12.0, 2.0 }, std::tuple { Shape::Bell, 150.0, -9.0, 6.0 },
           std::tuple { Shape::LowShelf, 300.0, 6.0, 0.7071 }, std::tuple { Shape::BandPass, 2500.0, 0.0, 3.0 } })
    {
        const auto band = bandOf (shape, hz, gain, q);
        const auto r = readouts.compute (band, paramsFor (PhaseMode::MinimumPhase), 0.0);
        const auto design = filters::designBand (band.shape, kFs);
        const double expected = 1000.0 * analyticGroupDelay (design, kTwoPi * hz / kFs) / kFs;
        std::printf ("[PR-1] group delay at fc: %.4f ms, analytic %.4f ms\n", r.groupDelayMs, expected);
        CHECK (r.groupDelayMs == Approx (expected).epsilon (0.02).margin (0.01)); // ±2 % (±0.5 sample near zero)
        CHECK (r.meanPhaseDeg >= 0.0f);
    }

    // A wider bell boost turns the phase further: mean excess phase grows with the gain.
    const auto small = readouts.compute (bandOf (Shape::Bell, 1000.0, 3.0, 1.0), paramsFor (PhaseMode::MinimumPhase), 0.0);
    const auto large = readouts.compute (bandOf (Shape::Bell, 1000.0, 15.0, 1.0), paramsFor (PhaseMode::MinimumPhase), 0.0);
    CHECK (large.meanPhaseDeg > small.meanPhaseDeg);
    CHECK (small.meanPhaseDeg > 0.5f);
}

TEST_CASE ("Dynamic bands report the full range too; Spectral bands their STFT pre-echo (PR-1)", "[readouts][PR-1]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    auto dynamic = bandOf (Shape::Bell, 500.0, 0.0, 8.0);
    dynamic.mode = BandMode::Dynamic;
    dynamic.dynamics.ratio = 4.0;
    dynamic.dynamics.direction = Direction::Above;
    dynamic.dynamics.rangeDb = 18.0;
    const auto r = readouts.compute (dynamic, paramsFor (PhaseMode::MinimumPhase), -3.0);
    REQUIRE (r.rangePostRingMs >= 0.0f);
    CHECK (r.rangePostRingMs > r.postRingMs); // an 18 dB cut rings longer than a 3 dB one
    CHECK (r.stftPreEchoMs < 0.0f);

    auto spectral = bandOf (Shape::Bell, 2000.0, -6.0, 2.0);
    spectral.mode = BandMode::Spectral;
    const auto s = readouts.compute (spectral, paramsFor (PhaseMode::MinimumPhase, Quality::Normal), 0.0);
    CHECK (s.stftPreEchoMs == Approx (500.0 * spectral::fftSizeFor (Quality::Normal, kFs) / kFs));
    CHECK (s.meanPhaseDeg == 0.0f);
}

TEST_CASE ("Ring times of synthetic responses", "[readouts][PR-3]")
{
    // A causal decaying sine: no pre-ring; a symmetric one (linear phase) rings both ways.
    std::vector<double> h (48000, 0.0);
    for (std::size_t i = 0; i < h.size(); ++i)
        h[i] = std::exp (-static_cast<double> (i) / 480.0) * std::cos (kTwoPi * 1000.0 * static_cast<double> (i) / kFs);
    const auto causal = analysis::ringTimes (h);
    CHECK (causal.pre == 0.0);
    CHECK (causal.post == Approx (480.0 * std::log (1000.0)).epsilon (0.02));

    std::vector<double> symmetric (2001, 0.0);
    for (int i = -1000; i <= 1000; ++i)
        symmetric[static_cast<std::size_t> (i + 1000)] = std::exp (-std::abs (i) / 100.0) * std::cos (kTwoPi * 3000.0 * i / kFs);
    const auto both = analysis::ringTimes (symmetric);
    CHECK (both.pre == Approx (both.post).epsilon (0.02));
    CHECK (both.post == Approx (100.0 * std::log (1000.0)).epsilon (0.03));
}

TEST_CASE ("A readout measurement stops promptly when asked to (PR-2)", "[readouts][PR-2]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    int polls = 0;
    readouts.setAbortCheck ([&polls] { return ++polls > 3; });
    // 20 Hz, Q 40: a response of several seconds, interrupted after a few blocks.
    const auto r = readouts.compute (bandOf (Shape::Bell, 20.0, 12.0, 40.0), paramsFor (PhaseMode::NaturalPhase, Quality::Max), 0.0);
    CHECK_FALSE (r.valid);
    CHECK (polls <= 6);

    readouts.setAbortCheck ({});
    CHECK (readouts.compute (bandOf (Shape::Bell, 1000.0, 6.0, 1.0), paramsFor (PhaseMode::MinimumPhase), 0.0).valid);
}

TEST_CASE ("The response of the whole EQ: phase and group delay as heard (PR-4)", "[readouts][PR-4]")
{
    analysis::BandReadouts readouts;
    readouts.prepare (kFs);
    engine::BandSettingsArray bands {};
    bands[0] = bandOf (Shape::Bell, 1000.0, 6.0, 1.0);
    bands[3] = bandOf (Shape::HighShelf, 8000.0, -4.0, 0.7071);
    const std::array<float, kNumBands> noChange {};

    // Minimum Phase: the phase and group delay of the two digital designs.
    const auto mp = readouts.computeChain (bands, paramsFor (PhaseMode::MinimumPhase), noChange);
    REQUIRE (mp.valid);
    REQUIRE (mp.numPoints == analysis::ChainResponse::kMaxPoints);
    const auto bell = filters::designBand (bands[0].shape, kFs);
    const auto shelf = filters::designBand (bands[3].shape, kFs);
    double worstPhase = 0.0, worstDelay = 0.0, largestPhase = 0.0;
    for (int i = 0; i < mp.numPoints; ++i)
    {
        const auto p = static_cast<std::size_t> (i);
        const double hz = mp.hz[p];
        if (hz < 20.0 || hz > 20000.0)
            continue;
        REQUIRE (mp.defined[p]);
        const double phase = std::arg (bell.digitalResponse (hz, kFs) * shelf.digitalResponse (hz, kFs)) * 180.0 / kPi;
        const double difference = std::abs (mp.phaseDeg[p] - phase);
        worstPhase = std::max (worstPhase, std::min (difference, 360.0 - difference));
        largestPhase = std::max (largestPhase, std::abs (phase));
        const double omega = kTwoPi * hz / kFs;
        const double delayMs = 1000.0 * (analyticGroupDelay (bell, omega) + analyticGroupDelay (shelf, omega)) / kFs;
        worstDelay = std::max (worstDelay, std::abs (mp.groupDelayMs[p] - delayMs));
    }
    std::printf ("[PR-4] Minimum Phase chain: phase error %.3f deg (largest phase %.1f deg), group delay error %.4f ms\n", worstPhase,
                 largestPhase, worstDelay);
    CHECK (largestPhase > 10.0);
    CHECK (worstPhase < 0.5);
    CHECK (worstDelay < 0.01);

    // Natural Phase: the phase and group delay of the analog prototypes (no cramping near Nyquist).
    const auto np = readouts.computeChain (bands, paramsFor (PhaseMode::NaturalPhase, Quality::High), noChange);
    REQUIRE (np.valid);
    auto analogPhase = [&] (double hz) { return std::arg (bell.analogResponse (hz) * shelf.analogResponse (hz)); };
    double npPhase = 0.0, npDelay = 0.0;
    for (int i = 0; i < np.numPoints; ++i)
    {
        const auto p = static_cast<std::size_t> (i);
        const double hz = np.hz[p];
        if (hz < 100.0 || hz > 16000.0)
            continue;
        const double difference = std::abs (np.phaseDeg[p] - analogPhase (hz) * 180.0 / kPi);
        npPhase = std::max (npPhase, std::min (difference, 360.0 - difference));
        const double step = hz * 1.0e-4;
        const double delayMs = -1000.0 * (analogPhase (hz + step) - analogPhase (hz - step)) / (kTwoPi * 2.0 * step);
        npDelay = std::max (npDelay, std::abs (np.groupDelayMs[p] - delayMs));
    }
    std::printf ("[PR-4] Natural Phase chain vs analog prototypes: phase error %.3f deg, group delay error %.4f ms (100 Hz - 16 kHz)\n",
                 npPhase, npDelay);
    CHECK (npPhase < 1.0);
    CHECK (npDelay < 0.02);

    // The left channel of a centred source: a band on Right, or one on Side, does not act on it.
    // (Together they would: the Right band makes a side signal for the Side band - a serial chain.)
    for (const auto component : { StereoComponent::Right, StereoComponent::Side })
    {
        engine::BandSettingsArray elsewhere {};
        elsewhere[0] = bandOf (Shape::Bell, 1000.0, 9.0, 1.0);
        elsewhere[0].component = component;
        const auto none = readouts.computeChain (elsewhere, paramsFor (PhaseMode::MinimumPhase), noChange);
        REQUIRE (none.valid);
        for (int i = 0; i < none.numPoints; ++i)
            REQUIRE (std::abs (none.phaseDeg[static_cast<std::size_t> (i)]) < 1.0e-3f);
    }

    // Dynamic bands count with their current gain change.
    engine::BandSettingsArray dynamic {};
    dynamic[0] = bandOf (Shape::Bell, 1000.0, 0.0, 1.0);
    dynamic[0].mode = BandMode::Dynamic;
    std::array<float, kNumBands> change {};
    change[0] = 6.0f;
    engine::BandSettingsArray same {};
    same[0] = bandOf (Shape::Bell, 1000.0, 6.0, 1.0);
    const auto a = readouts.computeChain (dynamic, paramsFor (PhaseMode::MinimumPhase), change);
    const auto b = readouts.computeChain (same, paramsFor (PhaseMode::MinimumPhase), noChange);
    for (int i = 0; i < a.numPoints; ++i)
        REQUIRE (a.phaseDeg[static_cast<std::size_t> (i)] == Approx (b.phaseDeg[static_cast<std::size_t> (i)]).margin (1.0e-3));
}
