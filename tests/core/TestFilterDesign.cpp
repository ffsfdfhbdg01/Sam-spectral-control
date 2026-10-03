#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "ssc/fft/RealFft.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/filters/MatchedDesign.h"
#include "ssc/filters/SvfSection.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::filters;

namespace
{
double db (std::complex<double> h)
{
    return 20.0 * std::log10 (std::abs (h));
}

struct ErrorStats
{
    double local = 0.0;  ///< within an octave of the band frequency
    double global = 0.0; ///< 10 Hz … 0.45·fs
};

/** Magnitude error of the digital design against its analog prototype where the response is above −30 dB. */
ErrorStats designError (const BandShape& shape, double fs)
{
    const auto d = designBand (shape, fs);
    ErrorStats e;
    for (double f = 10.0; f <= 0.45 * fs; f *= 1.01)
    {
        const double analog = db (d.analogResponse (f));
        if (analog < -30.0)
            continue;
        const double err = std::abs (analog - db (d.digitalResponse (f, fs)));
        e.global = std::max (e.global, err);
        if (f >= shape.frequencyHz / 2.0 && f <= shape.frequencyHz * 2.0)
            e.local = std::max (e.local, err);
    }
    return e;
}
} // namespace

TEST_CASE ("Matched bell: exact centre, DC and Nyquist gain; bounded error (BAND-2)", "[filters][BAND-2]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    for (double fc = 20.0; fc <= fs / 8.0; fc *= 1.25)
        for (double q : { 0.5, 1.0, 2.0, 5.0, 10.0 })
            for (double g : { -18.0, -6.0, 6.0, 18.0 })
            {
                const BandShape s { Shape::Bell, fc, g, q, Slope::Db12 };
                const auto d = designBand (s, fs);
                INFO ("fs=" << fs << " fc=" << fc << " q=" << q << " g=" << g);
                REQUIRE (db (d.digitalResponse (fc, fs)) == Approx (g).margin (0.01));
                REQUIRE (db (d.digitalResponse (0.0, fs)) == Approx (0.0).margin (1e-6));
                REQUIRE (db (d.digitalResponse (0.5 * fs, fs)) == Approx (db (d.analogResponse (0.5 * fs))).margin (1e-4));

                const auto e = designError (s, fs);
                const bool lowerBand = fc <= fs / 16.0;
                REQUIRE (e.local <= (lowerBand ? 0.12 : 0.45));
                REQUIRE (e.global <= (lowerBand ? 0.5 : 0.8));
            }
}

TEST_CASE ("Matched shelves follow the analog prototype (BAND-2)", "[filters][BAND-2]")
{
    const double fs = 48000.0;
    const auto shape = GENERATE (Shape::LowShelf, Shape::HighShelf);
    for (auto slope : { Slope::Db6, Slope::Db12, Slope::Db18, Slope::Db24 })
        for (double fc = 20.0; fc <= fs / 8.0; fc *= 1.25)
            for (double g : { -18.0, -6.0, 6.0, 18.0 })
            {
                const BandShape s { shape, fc, g, 0.70710678, slope };
                const auto d = designBand (s, fs);
                INFO ("shape=" << static_cast<int> (shape) << " slope=" << static_cast<int> (slope) << " fc=" << fc << " g=" << g);

                // Plateau gains are exact; the gain at the shelf frequency is half the dB gain.
                const double low = db (d.digitalResponse (0.0, fs));
                REQUIRE (low == Approx (shape == Shape::LowShelf ? g : 0.0).margin (1e-6));
                REQUIRE (db (d.digitalResponse (fc, fs)) == Approx (0.5 * g).margin (0.02));

                const auto e = designError (s, fs);
                REQUIRE (e.local <= 0.2);
                REQUIRE (e.global <= 0.6);
            }
}

TEST_CASE ("Cut filters: -3 dB at the corner and the nominal slope (BAND-3)", "[filters][BAND-3]")
{
    const double fs = 48000.0;
    const auto slope =
        GENERATE (Slope::Db6, Slope::Db12, Slope::Db18, Slope::Db24, Slope::Db30, Slope::Db36, Slope::Db48, Slope::Db72, Slope::Db96);
    const int nominal = slopeDbPerOctave (slope);

    SECTION ("Low cut")
    {
        const double fc = 2000.0;
        const auto d = designBand ({ Shape::LowCut, fc, 0.0, 0.70710678, slope }, fs);
        REQUIRE (db (d.digitalResponse (fc, fs)) == Approx (-3.0103).margin (0.02));
        // Slope measured between 2 and 3 octaves below the corner.
        const double perOctave = db (d.digitalResponse (fc / 4.0, fs)) - db (d.digitalResponse (fc / 8.0, fs));
        INFO ("nominal " << nominal << " measured " << perOctave);
        REQUIRE (perOctave == Approx (nominal).margin (0.5));
    }

    SECTION ("High cut")
    {
        const double fc = 500.0;
        const auto d = designBand ({ Shape::HighCut, fc, 0.0, 0.70710678, slope }, fs);
        REQUIRE (db (d.digitalResponse (fc, fs)) == Approx (-3.0103).margin (0.02));
        const double perOctave = db (d.digitalResponse (fc * 4.0, fs)) - db (d.digitalResponse (fc * 8.0, fs));
        INFO ("nominal " << nominal << " measured " << perOctave);
        REQUIRE (perOctave == Approx (nominal).margin (0.5));
    }
}

TEST_CASE ("Notch, band pass, all pass and tilt shapes", "[filters][BAND-2]")
{
    const double fs = 48000.0;

    const auto notch = designBand ({ Shape::Notch, 1000.0, 0.0, 4.0, Slope::Db12 }, fs);
    REQUIRE (db (notch.digitalResponse (1000.0, fs)) < -120.0);
    REQUIRE (db (notch.digitalResponse (0.0, fs)) == Approx (0.0).margin (1e-9));

    const auto bp = designBand ({ Shape::BandPass, 1000.0, 0.0, 2.0, Slope::Db12 }, fs);
    REQUIRE (db (bp.digitalResponse (1000.0, fs)) == Approx (0.0).margin (0.02));
    REQUIRE (db (bp.digitalResponse (100.0, fs)) < -30.0);

    const auto ap = designBand ({ Shape::AllPass, 1000.0, 0.0, 1.0, Slope::Db12 }, fs);
    for (double f = 20.0; f < 20000.0; f *= 1.5)
        REQUIRE (std::abs (ap.digitalResponse (f, fs)) == Approx (1.0).margin (1e-9));

    const auto tilt = designBand ({ Shape::TiltShelf, 1000.0, 6.0, 0.70710678, Slope::Db12 }, fs);
    REQUIRE (db (tilt.digitalResponse (10.0, fs)) == Approx (-6.0).margin (0.05));
    REQUIRE (db (tilt.digitalResponse (1000.0, fs)) == Approx (0.0).margin (0.05));

    const auto flat = designBand ({ Shape::FlatTilt, 1000.0, 10.0, 0.70710678, Slope::Db12 }, fs);
    REQUIRE (db (flat.digitalResponse (1000.0, fs)) == Approx (0.0).margin (0.05));
    REQUIRE (db (flat.digitalResponse (20000.0, fs)) - db (flat.digitalResponse (20.0, fs)) == Approx (10.0).margin (1.0));
}

TEST_CASE ("Bells with 0 dB gain are identity; cut equals inverse boost", "[filters]")
{
    REQUIRE (isIdentity ({ Shape::Bell, 1000.0, 0.0, 1.0, Slope::Db12 }));
    REQUIRE (designBand ({ Shape::Bell, 1000.0, 0.0, 1.0, Slope::Db12 }, 48000.0).numSections == 0);

    const auto boost = designBand ({ Shape::Bell, 3000.0, 9.0, 1.5, Slope::Db12 }, 48000.0);
    const auto cut = designBand ({ Shape::Bell, 3000.0, -9.0, 1.5, Slope::Db12 }, 48000.0);
    for (double f = 20.0; f < 22000.0; f *= 1.3)
        REQUIRE (db (cut.digitalResponse (f, 48000.0)) == Approx (-db (boost.digitalResponse (f, 48000.0))).margin (1e-9));
}

TEST_CASE ("SVF realisation reproduces the biquad transfer function exactly", "[filters]")
{
    const double fs = 48000.0;
    const auto shape =
        GENERATE (Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::LowCut, Shape::HighCut, Shape::Notch, Shape::BandPass);
    const auto d = designBand ({ shape, 2500.0, 7.0, 1.3, Slope::Db24 }, fs);
    REQUIRE (d.numSections > 0);

    constexpr int n = 8192;
    std::vector<float> impulse (n, 0.0f);
    std::vector<SectionState> states (static_cast<std::size_t> (d.numSections));
    for (int i = 0; i < n; ++i)
    {
        double x = i == 0 ? 1.0 : 0.0;
        for (int s = 0; s < d.numSections; ++s)
            x = processSection (d.svf[static_cast<std::size_t> (s)], d.digital[static_cast<std::size_t> (s)].firstOrder,
                                states[static_cast<std::size_t> (s)], x);
        impulse[static_cast<std::size_t> (i)] = static_cast<float> (x);
    }

    RealFft fft (n);
    std::vector<float> spectrum (n + 2);
    fft.forward (impulse.data(), spectrum.data());
    for (int k = 1; k < n / 2; k += 37)
    {
        const double f = k * fs / n;
        const std::complex<double> measured (spectrum[static_cast<std::size_t> (2 * k)], spectrum[static_cast<std::size_t> (2 * k + 1)]);
        const auto expected = d.digitalResponse (f, fs);
        REQUIRE (std::abs (measured - expected) <= 1e-4 * std::max (1.0, std::abs (expected)));
    }
}

TEST_CASE ("Bands respond the same at 44.1 and 96 kHz (NFR-2)", "[filters][NFR-2]")
{
    const auto shape = GENERATE (Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::LowCut, Shape::HighCut, Shape::BandPass);
    for (double fc : { 30.0, 100.0, 400.0, 1000.0, 2500.0, 5000.0 })
        for (double g : { -12.0, 6.0 })
        {
            const double q = shape == Shape::Bell || shape == Shape::BandPass ? 2.0 : 0.70710678;
            const BandShape s { shape, fc, g, q, Slope::Db24 };
            const auto low = designBand (s, 44100.0);
            const auto high = designBand (s, 96000.0);

            // Pass band and transition (above −12 dB) vs skirts (−30 … −12 dB); the deep stop band
            // is not compared.
            double worstPass = 0.0, worstSkirt = 0.0, worstAt = 0.0;
            for (double f = 20.0; f <= 16000.0; f *= 1.02)
            {
                const double a = db (low.digitalResponse (f, 44100.0));
                const double b = db (high.digitalResponse (f, 96000.0));
                const double level = std::max (a, b);
                const double diff = std::abs (a - b);
                if (level >= -12.0 && diff > worstPass)
                {
                    worstPass = diff;
                    worstAt = f;
                }
                else if (level >= -30.0 && level < -12.0)
                    worstSkirt = std::max (worstSkirt, diff);
            }
            INFO ("shape=" << static_cast<int> (shape) << " fc=" << fc << " g=" << g << " pass " << worstPass << " dB at " << worstAt
                           << " Hz, skirt " << worstSkirt << " dB");
            const bool lowerBand = fc <= 2500.0; // ≤ fs/16 at 44.1 kHz
            REQUIRE (worstPass <= (lowerBand ? 0.25 : 0.6));
            REQUIRE (worstSkirt <= (lowerBand ? 0.5 : 1.0));
        }
}
