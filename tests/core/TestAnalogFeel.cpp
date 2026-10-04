#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/BandProcessor.h"
#include "ssc/engine/Engine.h"
#include "ssc/filters/BandDesign.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;
using filters::BandShape;

namespace
{
constexpr double kFs = 48000.0;

double dbOf (std::complex<double> h)
{
    return 20.0 * std::log10 (std::abs (h));
}

/** Largest response (dB) of an analog design over 10 Hz … 20 kHz. */
double peakDb (const filters::BandDesign& d)
{
    double peak = -1e9;
    for (double hz = 10.0; hz < 20000.0; hz *= 1.01)
        peak = std::max (peak, dbOf (d.analogResponse (hz)));
    return peak;
}

double troughDb (const filters::BandDesign& d)
{
    double trough = 1e9;
    for (double hz = 10.0; hz < 20000.0; hz *= 1.01)
        trough = std::min (trough, dbOf (d.analogResponse (hz)));
    return trough;
}

BandShape shape (Shape s, double hz, double gain, double q, AnalogModel model, Character character = Character::Clear,
                 Slope slope = Slope::Db12)
{
    BandShape b { s, hz, gain, q, slope, character };
    b.analog = model;
    return b;
}

double amplitudeAt (const std::vector<double>& x, std::size_t from, std::size_t length, double hz)
{
    std::complex<double> sum (0.0, 0.0);
    const double w = 2.0 * std::numbers::pi * hz / kFs;
    for (std::size_t n = 0; n < length; ++n)
        sum += x[from + n] * std::polar (1.0, -w * static_cast<double> (n));
    return 2.0 * std::abs (sum) / static_cast<double> (length);
}

/** THD (dB) of a sine at f0 through one band (mono, base rate). */
double thdDb (const BandShape& s, double f0, double amplitudeDb)
{
    BandProcessor band;
    band.prepare (kFs, 256);
    BandSettings settings;
    settings.used = settings.active = true;
    settings.shape = s;

    const int length = 96000;
    const double amplitude = std::pow (10.0, amplitudeDb / 20.0);
    std::vector<double> y (static_cast<std::size_t> (length));
    for (int i = 0; i < length; ++i)
        y[static_cast<std::size_t> (i)] = amplitude * std::sin (2.0 * std::numbers::pi * f0 * i / kFs);
    for (int offset = 0; offset < length; offset += 256)
    {
        double* ptrs[] = { y.data() + offset };
        band.process (settings, ChannelTopology::mono(), ptrs, 1, 256, 16);
    }

    const std::size_t window = 48000; // f0 divides 48000: exact bins
    const std::size_t from = y.size() - window;
    const double h1 = amplitudeAt (y, from, window, f0);
    double sum = 0.0;
    for (int k = 2; k * f0 < 0.5 * kFs; ++k)
    {
        const double a = amplitudeAt (y, from, window, k * f0);
        sum += a * a;
    }
    return 10.0 * std::log10 (sum / (h1 * h1) + 1e-40);
}
} // namespace

TEST_CASE ("Analog Feel curves: N, P and S (PH-1, ADR-0009)", "[analog][PH-1]")
{
    // N: gently proportional Q; high bells pulled towards unity (up to −25 % at 20 kHz).
    {
        const auto low = filters::designBand (shape (Shape::Bell, 4000.0, 8.0, 1.0, AnalogModel::N), kFs);
        CHECK (dbOf (low.analogResponse (4000.0)) == Approx (8.0).margin (1e-6));
        const auto high = filters::designBand (shape (Shape::Bell, 20000.0, 8.0, 1.0, AnalogModel::N), 96000.0);
        CHECK (dbOf (high.analogResponse (20000.0)) == Approx (6.0).margin (1e-6));
        CHECK (filters::analogCurve (shape (Shape::Bell, 1000.0, 3.0, 2.0, AnalogModel::N), AnalogModel::N).q == Approx (2.0 * 0.8));
        const auto shelf = filters::designBand (shape (Shape::LowShelf, 100.0, 6.0, 0.7071, AnalogModel::N), kFs);
        CHECK (peakDb (shelf) > 6.1); // a small bump at the corner
        CHECK (peakDb (shelf) < 8.0);
    }

    // P: resonant LC boost; the attenuator is a wider first-order shelf, lower for the bass.
    {
        const auto boost = filters::designBand (shape (Shape::LowShelf, 100.0, 6.0, 0.7071, AnalogModel::P), kFs);
        CHECK (peakDb (boost) > 6.5);
        CHECK (peakDb (boost) < 9.0);
        const auto cut = filters::designBand (shape (Shape::LowShelf, 100.0, -6.0, 0.7071, AnalogModel::P), kFs);
        CHECK (troughDb (cut) > -6.05);                                          // no overshoot
        CHECK (dbOf (cut.analogResponse (70.0)) == Approx (-3.0).margin (0.05)); // half the cut at 0.7 · fc
        const auto curve = filters::analogCurve (shape (Shape::HighShelf, 8000.0, -4.0, 2.0, AnalogModel::P), AnalogModel::P);
        CHECK (curve.q == Approx (0.5)); // never resonant
        CHECK (curve.frequencyHz == Approx (8000.0));

        // Boost and cut at the same frequency (two bands): a bass bump with a dip above it.
        double peak = -1e9, peakHz = 0.0, dip = 1e9;
        for (double hz = 10.0; hz < 2000.0; hz *= 1.01)
        {
            const double db = dbOf (boost.analogResponse (hz) * cut.analogResponse (hz));
            if (db > peak)
            {
                peak = db;
                peakHz = hz;
            }
            if (hz > 100.0)
                dip = std::min (dip, db);
        }
        CHECK (peak > 3.0);
        CHECK (peakHz < 100.0);
        CHECK (dip < -1.5);
    }

    // S: Q proportional to |gain| (strongest of the three).
    {
        const auto small = filters::analogCurve (shape (Shape::Bell, 1000.0, 3.0, 2.0, AnalogModel::S), AnalogModel::S);
        const auto large = filters::analogCurve (shape (Shape::Bell, 1000.0, 15.0, 2.0, AnalogModel::S), AnalogModel::S);
        CHECK (small.q == Approx (2.0 * 0.52));
        CHECK (large.q == Approx (2.0));
    }

    // In Analog Feel the Character only chooses the nonlinearity (CHR-4); the curves are the model's.
    const auto clear = filters::designBand (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::N, Character::Clear), kFs);
    const auto warm = filters::designBand (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::N, Character::Warm), kFs);
    const auto sharp = filters::designBand (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::N, Character::Sharp), kFs);
    CHECK (clear.shapedFrom == -1);
    CHECK (warm.shaper == filters::ShaperKind::NSoft);
    CHECK (sharp.shaper == filters::ShaperKind::NHard);
    CHECK (sharp.numSections == 1); // not the fourth-order Sharp bell
    CHECK (dbOf (warm.digitalResponse (700.0, kFs)) == dbOf (clear.digitalResponse (700.0, kFs)));
}

TEST_CASE ("Analog Feel distortion: Clear is clean, models are calibrated (CHR-4, PH-1)", "[analog][CHR-4]")
{
    // Analog Feel + Clear: the model's curves only (CHR-4).
    CHECK (thdDb (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::N, Character::Clear), 1000.0, 0.0) < -120.0);
    CHECK (thdDb (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::P, Character::Clear), 1000.0, 0.0) < -120.0);

    // Calibration points (−18 dBFS = +4 dBu, 0 dBFS = +22 dBu; sine at the centre of a +6 dB bell).
    struct Target
    {
        AnalogModel model;
        Character character;
        double at18, at0; ///< THD in dB
    };
    const Target targets[] = {
        { AnalogModel::N, Character::Warm, -66.0, -43.0 },  { AnalogModel::N, Character::Sharp, -60.0, -36.0 },
        { AnalogModel::P, Character::Warm, -56.0, -38.0 },  { AnalogModel::P, Character::Sharp, -50.0, -32.0 },
        { AnalogModel::S, Character::Warm, -106.0, -49.5 }, { AnalogModel::S, Character::Sharp, -97.0, -40.0 },
    };
    for (const auto& t : targets)
    {
        const auto s = shape (Shape::Bell, 1000.0, 6.0, 1.0, t.model, t.character);
        const double low = thdDb (s, 1000.0, -18.0), high = thdDb (s, 1000.0, 0.0);
        std::printf ("[CHR-4] model %d %s: THD %.1f dB at -18 dBFS, %.1f dB at 0 dBFS\n", static_cast<int> (t.model),
                     t.character == Character::Warm ? "Warm" : "Sharp", low, high);
        CAPTURE (static_cast<int> (t.model), static_cast<int> (t.character));
        CHECK (std::abs (low - t.at18) <= 3.0);
        CHECK (std::abs (high - t.at0) <= 3.0);
    }

    // N: the transformer saturates more in the bass.
    const double bass = thdDb (shape (Shape::Bell, 100.0, 6.0, 1.0, AnalogModel::N, Character::Warm), 100.0, -6.0);
    const double mid = thdDb (shape (Shape::Bell, 1000.0, 6.0, 1.0, AnalogModel::N, Character::Warm), 1000.0, -6.0);
    std::printf ("[CHR-4] N Warm at -6 dBFS: THD %.1f dB at 100 Hz, %.1f dB at 1 kHz\n", bass, mid);
    CHECK (bass > mid + 3.0);
}

TEST_CASE ("Analog Feel oversamples at High and Max and leaves other channels untouched (CH-6)", "[analog][engine][CH-6]")
{
    CHECK (effectiveOversampling (Oversampling::Off, PhaseMode::AnalogFeel, Quality::Normal, kFs) == 1);
    CHECK (effectiveOversampling (Oversampling::Off, PhaseMode::AnalogFeel, Quality::High, kFs) == 2);
    CHECK (effectiveOversampling (Oversampling::Off, PhaseMode::AnalogFeel, Quality::Max, kFs) == 4);
    CHECK (effectiveOversampling (Oversampling::X8, PhaseMode::AnalogFeel, Quality::High, kFs) == 8);
    CHECK (effectiveOversampling (Oversampling::Off, PhaseMode::AnalogFeel, Quality::Max, 192000.0) == 2);
    CHECK (effectiveOversampling (Oversampling::Off, PhaseMode::NaturalPhase, Quality::Max, kFs) == 1);

    auto engine = std::make_unique<Engine>();
    EngineConfig config;
    config.sampleRate = kFs;
    config.maxBlockSize = 512;
    engine->prepare (config);

    GlobalParams params;
    params.phaseMode = PhaseMode::AnalogFeel;
    params.analogModel = AnalogModel::N;
    params.quality = Quality::Max;
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::Bell, 200.0, 6.0, 1.0, Slope::Db12, Character::Sharp };
    bands[0].component = StereoComponent::Left;
    engine->configure (params, bands);
    CHECK (engine->getOversamplingFactor() == 4);
    CHECK (engine->isPhaseCorrectionActive());

    const auto noise = ssc::test::pinkNoise (1 << 15);
    std::vector<double> input (noise.begin(), noise.end());
    auto left = input, right = input;
    for (std::size_t offset = 0; offset < left.size(); offset += 512)
    {
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        engine->process (ptrs, 2, 512, params, bands);
    }
    const auto latency = static_cast<std::size_t> (engine->getLatencySamples());
    for (std::size_t i = latency; i < right.size(); ++i)
        REQUIRE (right[i] == input[i - latency]);
    double difference = 0.0;
    for (std::size_t i = latency; i < left.size(); ++i)
        difference = std::max (difference, std::abs (left[i] - input[i - latency]));
    CHECK (difference > 0.01);
}

TEST_CASE ("Analog Feel: a Dynamic band keeps its curve through 0 dB (PH-1, DYN-4)", "[analog][dynamic][PH-1]")
{
    // Every model keeps a band's topology when its gain changes sign, so the gain table of a
    // Dynamic band (designed at its set gain) also holds at the dynamic gain.
    for (const auto model : { AnalogModel::N, AnalogModel::P, AnalogModel::S })
        for (const auto s : { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf })
            for (const auto character : { Character::Clear, Character::Warm, Character::Sharp })
                for (const auto slope : { Slope::Db6, Slope::Db12, Slope::Db24 })
                {
                    CAPTURE (static_cast<int> (model), static_cast<int> (s), static_cast<int> (character), static_cast<int> (slope));
                    const auto up = filters::designBand (shape (s, 150.0, 3.0, 0.7071, model, character, slope), kFs);
                    const auto down = filters::designBand (shape (s, 150.0, -3.0, 0.7071, model, character, slope), kFs);
                    CHECK (up.topology() == down.topology());
                }

    // A P low shelf boost compressed below 0 dB sounds like the static P design at the total gain.
    auto engine = std::make_unique<Engine>();
    EngineConfig config;
    config.sampleRate = kFs;
    config.maxBlockSize = 512;
    engine->prepare (config);
    GlobalParams params;
    params.phaseMode = PhaseMode::AnalogFeel;
    params.analogModel = AnalogModel::P;
    BandSettingsArray bands {};
    auto& band = bands[0];
    band.used = band.active = true;
    band.shape = { Shape::LowShelf, 150.0, 3.0, 0.7071, Slope::Db12 };
    band.mode = BandMode::Dynamic;
    band.dynamics.thresholdDb = -40.0;
    band.dynamics.ratio = 4.0;
    band.dynamics.direction = Direction::Above;
    band.dynamics.kneeDb = 0.0;
    band.dynamics.rangeDb = 12.0;

    const int length = 96 * 512;
    std::vector<double> left (static_cast<std::size_t> (length)), right;
    for (int i = 0; i < length; ++i)
        left[static_cast<std::size_t> (i)] = 0.3 * std::sin (2.0 * std::numbers::pi * 60.0 * i / kFs);
    right = left;
    const auto input = left;
    for (std::size_t offset = 0; offset < left.size(); offset += 512)
    {
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        engine->process (ptrs, 2, 512, params, bands);
    }
    const double total = band.shape.gainDb + engine->getBandGainChangeDb (0);
    REQUIRE (total < -6.0); // the band crossed 0 dB

    const std::size_t window = 9600; // 60 Hz: whole periods
    const double measured = 20.0
        * std::log10 (amplitudeAt (left, left.size() - window, window, 60.0) / amplitudeAt (input, input.size() - window, window, 60.0));
    auto heard = band.shape;
    heard.gainDb = total;
    heard.analog = AnalogModel::P;
    const double expected = dbOf (filters::designBand (heard, kFs).digitalResponse (60.0, kFs));
    CHECK (measured == Approx (expected).margin (0.1));
}
