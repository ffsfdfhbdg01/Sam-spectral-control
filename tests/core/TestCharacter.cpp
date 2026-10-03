#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

#include "ssc/engine/BandProcessor.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/filters/Character.h"
#include "ssc/oversampling/Oversampler.h"
#include "ssc/util/Math.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::engine;
using filters::BandShape;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

double dbOf (std::complex<double> h)
{
    return 20.0 * std::log10 (std::abs (h));
}

/** Amplitude of the component at `hz` over `length` samples (an integer number of periods: no leakage). */
double amplitudeAt (const std::vector<double>& x, std::size_t from, std::size_t length, double hz, double fs)
{
    std::complex<double> sum (0.0, 0.0);
    const double w = 2.0 * std::numbers::pi * hz / fs;
    for (std::size_t n = 0; n < length; ++n)
        sum += x[from + n] * std::polar (1.0, -w * static_cast<double> (n));
    return 2.0 * std::abs (sum) / static_cast<double> (length);
}

/** Folds a frequency into [0, fs/2]. */
double fold (double hz, double fs)
{
    double f = std::fmod (hz, fs);
    return f > 0.5 * fs ? fs - f : f;
}

BandSettings band (Shape shape, double hz, double gainDb, double q, Character character, Slope slope = Slope::Db12)
{
    BandSettings s;
    s.used = true;
    s.active = true;
    s.shape = { shape, hz, gainDb, q, slope, character };
    return s;
}

/** Mono sine through one band at sampleRate, optionally inside an oversampler (factor > 1). */
std::vector<double> processSine (const BandSettings& settings, double hz, double amplitude, int length, int factor = 1,
                                 oversampling::FilterType type = oversampling::FilterType::PolyphaseIir)
{
    BandProcessor processor;
    processor.prepare (kFs * factor, kBlock * factor);
    oversampling::Oversampler os;
    os.prepare (1, kBlock, factor, type, Quality::Normal);

    std::vector<double> y (static_cast<std::size_t> (length));
    for (int i = 0; i < length; ++i)
        y[static_cast<std::size_t> (i)] = amplitude * std::sin (2.0 * std::numbers::pi * hz * i / kFs);

    std::vector<double> high (static_cast<std::size_t> (kBlock * factor));
    for (int offset = 0; offset < length; offset += kBlock)
    {
        const int n = std::min (kBlock, length - offset);
        double* block = y.data() + offset;
        if (factor == 1)
        {
            double* ptrs[] = { block };
            processor.process (settings, ChannelTopology::mono(), ptrs, 1, n, 16);
            continue;
        }
        os.upsample (0, block, high.data(), n);
        double* ptrs[] = { high.data() };
        processor.process (settings, ChannelTopology::mono(), ptrs, 1, n * factor, 16 * factor);
        os.downsample (0, high.data(), block, n);
    }
    return y;
}

struct Harmonics
{
    double h1 = 0.0, h2 = 0.0, h3 = 0.0;
    double thd = 0.0; ///< ratio, all harmonics below Nyquist
};

Harmonics harmonicsOf (const std::vector<double>& y, double f0)
{
    const std::size_t length = 9600; // 200 periods of 1 kHz
    const std::size_t from = y.size() - length;
    Harmonics h;
    h.h1 = amplitudeAt (y, from, length, f0, kFs);
    double sum = 0.0;
    for (int k = 2; k * f0 < 0.5 * kFs; ++k)
    {
        const double a = amplitudeAt (y, from, length, k * f0, kFs);
        sum += a * a;
        if (k == 2)
            h.h2 = a;
        if (k == 3)
            h.h3 = a;
    }
    h.thd = std::sqrt (sum) / h.h1;
    return h;
}

/** Largest folded component of harmonics above Nyquist (aliasing), relative to the fundamental (dB). */
double aliasingDb (const std::vector<double>& y, double f0, double fs)
{
    const std::size_t length = 4800; // 10 Hz bins: every component below lies on a bin
    const std::size_t from = y.size() - length;
    const double h1 = amplitudeAt (y, from, length, f0, fs);
    double worst = 0.0;
    for (int k = 2; k <= 40; ++k)
    {
        if (k * f0 <= 0.5 * fs)
            continue; // a true harmonic
        const double f = fold (k * f0, fs);
        if (std::abs (f - f0) < 1.0 || std::fmod (f, f0) < 1.0)
            continue; // lands on the fundamental or a true harmonic
        worst = std::max (worst, amplitudeAt (y, from, length, f, fs));
    }
    return 20.0 * std::log10 (std::max (worst, 1e-30) / h1);
}
} // namespace

TEST_CASE ("Character curves: Warm bell has proportional Q, cuts keep their response (CHR-1)", "[character][CHR-1]")
{
    for (const double gain : { 3.0, -6.0, 12.0, 18.0 })
    {
        const BandShape warm { Shape::Bell, 1000.0, gain, 2.0, Slope::Db12, Character::Warm };
        const BandShape equivalent { Shape::Bell, 1000.0, gain, filters::proportionalQ (2.0, gain), Slope::Db12, Character::Clear };
        const auto a = filters::designBand (warm, kFs);
        const auto b = filters::designBand (equivalent, kFs);
        for (const double hz : { 100.0, 700.0, 1000.0, 3000.0 })
            CHECK (dbOf (a.digitalResponse (hz, kFs)) == Approx (dbOf (b.digitalResponse (hz, kFs))).margin (1e-9));
        CHECK (a.shapedFrom == 0);
        CHECK (a.shaper == Character::Warm);
    }
    CHECK (filters::proportionalQ (1.0, 0.0) == Approx (0.6));
    CHECK (filters::proportionalQ (1.0, 12.0) == Approx (1.0));
    CHECK (filters::proportionalQ (1.0, -24.0) == Approx (1.0));

    // Resonant cuts: Butterworth cascade + a separate resonance section, same analog response.
    for (const auto slope : { Slope::Db12, Slope::Db24, Slope::Db48 })
    {
        const BandShape clear { Shape::HighCut, 2000.0, 0.0, 3.0, slope, Character::Clear };
        auto sharp = clear;
        sharp.character = Character::Sharp;
        const auto a = filters::designBand (clear, kFs);
        const auto b = filters::designBand (sharp, kFs);
        CHECK (b.numSections == a.numSections + 1);
        CHECK (b.shapedFrom == b.numSections - 1);
        CHECK (a.shapedFrom == -1);
        for (const double hz : { 200.0, 1500.0, 2000.0, 2500.0, 6000.0 })
        {
            CHECK (dbOf (b.analogResponse (hz)) == Approx (dbOf (a.analogResponse (hz))).margin (1e-9));
            CHECK (dbOf (b.digitalResponse (hz, kFs)) == Approx (dbOf (a.digitalResponse (hz, kFs))).margin (0.05));
        }
    }

    // No resonance (Q ≤ 0.707), notch, band pass, all pass: the Character changes nothing.
    for (const auto& shape : { BandShape { Shape::LowCut, 100.0, 0.0, 0.7071, Slope::Db24, Character::Warm },
                               BandShape { Shape::Notch, 1000.0, 0.0, 4.0, Slope::Db12, Character::Sharp },
                               BandShape { Shape::BandPass, 1000.0, 0.0, 4.0, Slope::Db12, Character::Warm },
                               BandShape { Shape::AllPass, 1000.0, 0.0, 4.0, Slope::Db12, Character::Sharp } })
    {
        auto clear = shape;
        clear.character = Character::Clear;
        const auto a = filters::designBand (shape, kFs);
        const auto b = filters::designBand (clear, kFs);
        CHECK (a.shapedFrom == -1);
        CHECK (a.topology() == b.topology());
        CHECK (dbOf (a.digitalResponse (1234.0, kFs)) == dbOf (b.digitalResponse (1234.0, kFs)));
    }

    // Shelves keep their curve and shape the whole band.
    const BandShape shelf { Shape::LowShelf, 200.0, 6.0, 0.7071, Slope::Db12, Character::Sharp };
    auto clearShelf = shelf;
    clearShelf.character = Character::Clear;
    const auto s = filters::designBand (shelf, kFs);
    CHECK (s.shapedFrom == 0);
    CHECK (s.topology() != filters::designBand (clearShelf, kFs).topology()); // switching crossfades
    CHECK (dbOf (s.digitalResponse (50.0, kFs)) == dbOf (filters::designBand (clearShelf, kFs).digitalResponse (50.0, kFs)));
}

TEST_CASE ("Sharp bell: same peak and half-gain bandwidth, steeper skirts (CHR-1)", "[character][CHR-1]")
{
    for (const double q : { 0.7, 1.0, 4.0 })
        for (const double gain : { 12.0, -9.0 })
        {
            CAPTURE (q, gain);
            const BandShape clear { Shape::Bell, 1000.0, gain, q, Slope::Db12, Character::Clear };
            auto sharpShape = clear;
            sharpShape.character = Character::Sharp;
            const auto c = filters::designBand (clear, kFs);
            const auto s = filters::designBand (sharpShape, kFs);
            REQUIRE (s.numSections == 2);

            CHECK (dbOf (s.analogResponse (1000.0)) == Approx (gain).margin (1e-9));
            CHECK (dbOf (s.digitalResponse (1000.0, kFs)) == Approx (gain).margin (0.02));

            // Half-gain frequencies of the second-order bell: |1 − x²| = x/Q.
            const double half = std::sqrt (1.0 + 1.0 / (4.0 * q * q));
            for (const double x : { half - 1.0 / (2.0 * q), half + 1.0 / (2.0 * q) })
            {
                CHECK (dbOf (c.analogResponse (1000.0 * x)) == Approx (0.5 * gain).margin (1e-9));
                CHECK (dbOf (s.analogResponse (1000.0 * x)) == Approx (0.5 * gain).margin (1e-9));
            }

            // Two octaves away the Sharp bell has fallen much further.
            for (const double hz : { 250.0, 4000.0 })
                CHECK (std::abs (dbOf (s.analogResponse (hz))) < 0.6 * std::abs (dbOf (c.analogResponse (hz))));
        }
}

TEST_CASE ("Character THD at 0 dBFS through a +6 dB bell (CHR-3)", "[character][CHR-3]")
{
    const double f0 = 1000.0;
    const int length = 48000;

    const auto clear = harmonicsOf (processSine (band (Shape::Bell, f0, 6.0, 1.0, Character::Clear), f0, 1.0, length), f0);
    const auto warm = harmonicsOf (processSine (band (Shape::Bell, f0, 6.0, 1.0, Character::Warm), f0, 1.0, length), f0);
    const auto sharp = harmonicsOf (processSine (band (Shape::Bell, f0, 6.0, 1.0, Character::Sharp), f0, 1.0, length), f0);

    std::printf ("[CHR-3] THD: Clear %.1f dB, Warm %.3f %% (H2 %.1f dB, H3 %.1f dB), Sharp %.3f %% (H2 %.1f dB, H3 %.1f dB)\n",
                 20.0 * std::log10 (clear.thd), 100.0 * warm.thd, 20.0 * std::log10 (warm.h2 / warm.h1),
                 20.0 * std::log10 (warm.h3 / warm.h1), 100.0 * sharp.thd, 20.0 * std::log10 (sharp.h2 / sharp.h1 + 1e-30),
                 20.0 * std::log10 (sharp.h3 / sharp.h1));

    CHECK (20.0 * std::log10 (clear.thd) < -120.0);

    CHECK (warm.thd >= 0.001);
    CHECK (warm.thd <= 0.005);
    CHECK (20.0 * std::log10 (warm.h2 / warm.h3) >= 6.0);

    CHECK (sharp.thd >= 0.001);
    CHECK (sharp.thd <= 0.005);
    CHECK (20.0 * std::log10 (sharp.h3 / std::max (sharp.h2, 1e-30)) >= 6.0);

    // The fundamental keeps the gain of the curve (the products are small).
    CHECK (20.0 * std::log10 (warm.h1) == Approx (6.0).margin (0.1));
    CHECK (20.0 * std::log10 (sharp.h1) == Approx (6.0).margin (0.2));
}

TEST_CASE ("Character at 0 dB adds nothing; distortion grows with the band's contribution (CHR-2)", "[character][CHR-2]")
{
    const double f0 = 1000.0;
    auto input = processSine (band (Shape::Bell, f0, 0.0, 1.0, Character::Clear), f0, 1.0, 9600);
    for (const auto character : { Character::Warm, Character::Sharp })
    {
        const auto y = processSine (band (Shape::Bell, f0, 0.0, 1.0, character), f0, 1.0, 9600);
        CHECK (y == input); // bit-exact: a band at 0 dB is not processed at all

        const auto small = harmonicsOf (processSine (band (Shape::Bell, f0, 3.0, 1.0, character), f0, 1.0, 19200), f0);
        const auto large = harmonicsOf (processSine (band (Shape::Bell, f0, 12.0, 1.0, character), f0, 1.0, 19200), f0);
        CHECK (large.thd > 2.0 * small.thd);
    }
}

TEST_CASE ("Character shaper: ADAA lowers aliasing and stays finite when clipping", "[character]")
{
    // A heavily clipped 5 kHz sine: aliased harmonics with and without ADAA.
    const auto shaper = filters::makeShaper (Character::Sharp, kFs);
    const int length = 9600;
    const double f0 = 5000.0;
    std::vector<double> naive (static_cast<std::size_t> (length)), adaa (static_cast<std::size_t> (length));
    filters::ShaperState state;
    for (int i = 0; i < length; ++i)
    {
        const double u = 8.0 * std::sin (2.0 * std::numbers::pi * f0 * i / kFs);
        naive[static_cast<std::size_t> (i)] = u + filters::shaperProducts (shaper, u);
        adaa[static_cast<std::size_t> (i)] = u + filters::shapeContribution (shaper, state, u);
        REQUIRE (std::isfinite (adaa[static_cast<std::size_t> (i)]));
    }
    auto aliasPower = [&] (const std::vector<double>& y) {
        double sum = 0.0;
        for (int k = 5; k <= 59; k += 2) // odd harmonics above Nyquist
        {
            const double f = fold (k * f0, kFs);
            if (std::fmod (f, f0) < 1.0)
                continue;
            const double a = amplitudeAt (y, 4800, 4800, f, kFs);
            sum += a * a;
        }
        return 10.0 * std::log10 (sum);
    };
    const double naiveDb = aliasPower (naive), adaaDb = aliasPower (adaa);
    std::printf ("[CHR] aliasing of a clipped 5 kHz sine: naive %.1f dB, ADAA %.1f dB\n", naiveDb, adaaDb);
    CHECK (adaaDb < naiveDb - 3.0);

    // Clip levels are continuous and S is monotonic.
    for (const auto c : { Character::Warm, Character::Sharp })
    {
        const auto s = filters::makeShaper (c, kFs);
        double previous = -1e9;
        for (double u = -60.0; u <= 60.0; u += 0.01)
        {
            const double value = u + filters::shaperProducts (s, u);
            CHECK (value >= previous - 1e-12);
            previous = value;
        }
        CHECK (s.uHigh + filters::shaperProducts (s, s.uHigh) == Approx (s.sHigh));
        CHECK (s.uLow + filters::shaperProducts (s, s.uLow) == Approx (s.sLow));
    }
}

TEST_CASE ("Character aliasing: Warm +12 dB bell, 10 kHz at 0 dBFS (OS-1, CHR-1)", "[character][oversampling][OS-1]")
{
    const double f0 = 10000.0;
    const auto settings = band (Shape::Bell, f0, 12.0, 1.0, Character::Warm);
    double measured[4] {};
    const int factors[] = { 1, 2, 4, 8 };
    for (int i = 0; i < 4; ++i)
    {
        const auto y = processSine (settings, f0, 1.0, 19200, factors[i]);
        measured[i] = aliasingDb (y, f0, kFs);
    }
    std::printf ("[OS-1] aliasing (Warm, +12 dB, 10 kHz, 0 dBFS): 1x %.1f dB, 2x %.1f dB, 4x %.1f dB, 8x %.1f dB\n", measured[0],
                 measured[1], measured[2], measured[3]);
    CHECK (measured[0] <= -60.0);
    CHECK (measured[1] <= -95.0);
    CHECK (measured[2] <= -110.0);
    CHECK (measured[3] <= -120.0);
}

TEST_CASE ("Character works in Dynamic mode and in heavy drive", "[character]")
{
    for (const auto character : { Character::Warm, Character::Sharp })
    {
        auto dynamic = band (Shape::Bell, 1000.0, 0.0, 1.0, character);
        dynamic.mode = BandMode::Dynamic;
        dynamic.dynamics.thresholdDb = -30.0;
        dynamic.dynamics.ratio = 4.0;
        dynamic.dynamics.direction = Direction::Above;
        dynamic.dynamics.rangeDb = 12.0;
        dynamic.dynamics.attackMs = 1.0;
        dynamic.dynamics.kneeDb = 0.0;
        const auto y = processSine (dynamic, 1000.0, 1.0, 48000);
        const auto h = harmonicsOf (y, 1000.0);
        CHECK (h.thd > 1e-4); // the band cuts by up to 12 dB: products of its contribution

        const auto drive = processSine (band (Shape::Bell, 1000.0, 24.0, 1.0, character), 1000.0, 1.0, 9600);
        for (const double v : drive)
            REQUIRE (std::isfinite (v));
    }
}
