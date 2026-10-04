#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "TestSignals.h"
#include "ssc/engine/BandProcessor.h"
#include "ssc/phase/PhaseCorrector.h"
#include "ssc/util/Math.h"

using namespace ssc;
using namespace ssc::engine;
using filters::BandShape;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
constexpr int kPrime = 2048;

struct Errors
{
    double magnitudeDb = 0.0; ///< worst |magnitude error| where the target is above −40 dB
    double phaseDeg = 0.0;    ///< worst |phase error| where the target is above −40 dB
};

/** Impulse response of IIR bands (at kFs) followed by the corrector, after it has settled. */
std::vector<double> naturalPhaseResponse (const std::vector<BandShape>& shapes, Quality quality, int length, int& latency,
                                          bool brickwall = false)
{
    const auto topology = ChannelTopology::mono();
    std::vector<BandProcessor> processors (shapes.size());
    phase::PhaseCorrector corrector;
    corrector.prepare (kFs, 1, kBlock);
    corrector.setQuality (quality);
    latency = corrector.getLatencySamples();

    phase::CorrectorBands bands {};
    std::vector<BandSettings> settings (shapes.size());
    for (std::size_t b = 0; b < shapes.size(); ++b)
    {
        processors[b].prepare (kFs, kBlock);
        settings[b].used = settings[b].active = true;
        settings[b].shape = shapes[b];
        const bool fir = brickwall && shapes[b].slope == Slope::Brickwall;
        bands[b].active = true;
        bands[b].brickwall = fir;
        bands[b].shape = shapes[b];
        bands[b].lanes = makeLanes (StereoComponent::All, ~std::uint64_t { 0 }, false, topology);
        if (fir)
            settings[b].active = false; // realised by the corrector
    }
    corrector.update (bands, topology, kFs);

    std::vector<double> y (static_cast<std::size_t> (kPrime + length), 0.0);
    y[static_cast<std::size_t> (kPrime)] = 1.0;
    for (int offset = 0; offset < kPrime + length; offset += kBlock)
    {
        double* ptrs[] = { y.data() + offset };
        for (std::size_t b = 0; b < shapes.size(); ++b)
            processors[b].process (settings[b], topology, ptrs, 1, kBlock, 16);
        corrector.process (ptrs, 1, kBlock);
    }
    return { y.begin() + kPrime, y.end() };
}

std::complex<double> analogTarget (const std::vector<BandShape>& shapes, double hz)
{
    std::complex<double> t (1.0, 0.0);
    for (const auto& s : shapes)
        t *= filters::designBand (s, kFs).analogResponse (hz);
    return t;
}

Errors errorsAgainstAnalog (const std::vector<BandShape>& shapes, Quality quality)
{
    int latency = 0;
    const int length = 1 << 16;
    const auto h = naturalPhaseResponse (shapes, quality, length, latency);
    const auto spectrum = ssc::test::spectrumOf (h, static_cast<std::size_t> (length));

    Errors e;
    const double binHz = kFs / length;
    for (std::size_t k = 1; k < spectrum.size(); ++k)
    {
        const double hz = static_cast<double> (k) * binHz;
        if (hz < 20.0 || hz > 0.9 * 0.5 * kFs)
            continue;
        const auto target = analogTarget (shapes, hz) * std::polar (1.0, -kTwoPi * hz * latency / kFs);
        if (std::abs (target) < 0.01)
            continue;
        const auto ratio = spectrum[k] / target;
        e.magnitudeDb = std::max (e.magnitudeDb, std::abs (20.0 * std::log10 (std::abs (ratio))));
        e.phaseDeg = std::max (e.phaseDeg, std::abs (std::arg (ratio)) * 180.0 / kPi);
    }
    return e;
}

struct Reference
{
    std::string name;
    std::vector<BandShape> shapes;
};

std::vector<Reference> references()
{
    return {
        { "bell 1 kHz +12 dB Q1", { { Shape::Bell, 1000.0, 12.0, 1.0, Slope::Db12 } } },
        { "bell 10 kHz -9 dB Q2", { { Shape::Bell, 10000.0, -9.0, 2.0, Slope::Db12 } } },
        { "bell 16 kHz +6 dB Q3", { { Shape::Bell, 16000.0, 6.0, 3.0, Slope::Db12 } } },
        { "high shelf 8 kHz +12 dB", { { Shape::HighShelf, 8000.0, 12.0, 0.7071, Slope::Db12 } } },
        { "low shelf 100 Hz -6 dB", { { Shape::LowShelf, 100.0, -6.0, 0.7071, Slope::Db12 } } },
        { "high cut 15 kHz 12 dB/oct", { { Shape::HighCut, 15000.0, 0.0, 0.7071, Slope::Db12 } } },
        { "low cut 30 Hz 24 dB/oct", { { Shape::LowCut, 30.0, 0.0, 0.7071, Slope::Db24 } } },
        { "mix of five bands",
          { { Shape::LowCut, 40.0, 0.0, 0.7071, Slope::Db12 },
            { Shape::Bell, 250.0, -4.0, 1.5, Slope::Db12 },
            { Shape::Bell, 3000.0, 3.0, 0.8, Slope::Db12 },
            { Shape::HighShelf, 10000.0, 4.0, 0.7071, Slope::Db12 },
            { Shape::HighCut, 18000.0, 0.0, 0.7071, Slope::Db12 } } },
    };
}
} // namespace

TEST_CASE ("Natural Phase latency per Quality (PH-1)", "[phase][PH-1]")
{
    CHECK (phase::correctorDelayFor (Quality::Eco, 48000.0) == 16);
    CHECK (phase::correctorDelayFor (Quality::Normal, 48000.0) == 32);
    CHECK (phase::correctorDelayFor (Quality::High, 48000.0) == 64);
    CHECK (phase::correctorDelayFor (Quality::Max, 48000.0) == 128);
    CHECK (phase::correctorDelayFor (Quality::Max, 96000.0) == 256);
    CHECK (phase::correctorDelayFor (Quality::Normal, 44100.0) == 29);
}

TEST_CASE ("Natural Phase without bands is an exact delay", "[phase]")
{
    phase::PhaseCorrector corrector;
    corrector.prepare (kFs, 2, kBlock);
    corrector.setQuality (Quality::High);
    corrector.update ({}, ChannelTopology::stereo(), kFs);
    CHECK_FALSE (corrector.isCorrecting());

    std::vector<double> left (4096), right (4096);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = std::sin (0.01 * static_cast<double> (i)) + 1e-3 * static_cast<double> (i % 7);
        right[i] = std::cos (0.03 * static_cast<double> (i));
    }
    auto l = left, r = right;
    for (std::size_t offset = 0; offset < l.size(); offset += 300)
    {
        const int n = static_cast<int> (std::min<std::size_t> (300, l.size() - offset));
        double* ptrs[] = { l.data() + offset, r.data() + offset };
        corrector.process (ptrs, 2, n);
    }
    const int d = corrector.getLatencySamples();
    for (std::size_t i = static_cast<std::size_t> (d); i < l.size(); ++i)
    {
        REQUIRE (l[i] == left[i - static_cast<std::size_t> (d)]);
        REQUIRE (r[i] == right[i - static_cast<std::size_t> (d)]);
    }
}

TEST_CASE ("Natural Phase follows the analog prototype in magnitude and phase (PH-1)", "[phase][PH-1]")
{
    for (const auto quality : { Quality::Eco, Quality::Normal, Quality::High, Quality::Max })
    {
        for (const auto& ref : references())
        {
            const auto e = errorsAgainstAnalog (ref.shapes, quality);
            std::printf ("[PH-1] Q%d %-28s magnitude %.3f dB, phase %.2f deg\n", static_cast<int> (quality), ref.name.c_str(),
                         e.magnitudeDb, e.phaseDeg);
            CAPTURE (ref.name, static_cast<int> (quality));
            if (quality == Quality::Normal)
            {
                CHECK (e.magnitudeDb <= 0.1);
                CHECK (e.phaseDeg <= 5.0);
            }
            if (quality == Quality::Max)
            {
                CHECK (e.magnitudeDb <= 0.1);
                CHECK (e.phaseDeg <= 2.0);
            }
        }
    }
}

TEST_CASE ("Natural Phase pre-ring is limited to the latency (PH-1)", "[phase][PH-1]")
{
    for (const auto quality : { Quality::Normal, Quality::Max })
    {
        int latency = 0;
        const auto h = naturalPhaseResponse ({ { Shape::Bell, 4000.0, 12.0, 4.0, Slope::Db12 } }, quality, 8192, latency);
        double peak = 0.0;
        for (const double v : h)
            peak = std::max (peak, std::abs (v));
        int first = 0;
        while (std::abs (h[static_cast<std::size_t> (first)]) < peak * 1e-3) // −60 dB
            ++first;
        CHECK (latency - first <= latency);
        CHECK (first >= 0);
    }
}

TEST_CASE ("Natural Phase in the Mid/Side basis corrects Mid and Side separately", "[phase][PH-1]")
{
    const auto topology = ChannelTopology::stereo();
    const BandShape shape { Shape::HighShelf, 9000.0, 9.0, 0.7071, Slope::Db12 };
    BandSettings settings;
    settings.used = settings.active = true;
    settings.shape = shape;
    settings.component = StereoComponent::Mid;

    BandProcessor processor;
    processor.prepare (kFs, kBlock);
    phase::PhaseCorrector corrector;
    corrector.prepare (kFs, 2, kBlock);
    corrector.setQuality (Quality::Normal);
    phase::CorrectorBands bands {};
    bands[0].active = true;
    bands[0].shape = shape;
    bands[0].lanes = makeLanes (StereoComponent::Mid, ~std::uint64_t { 0 }, false, topology);
    corrector.update (bands, topology, kFs);

    const int length = 1 << 15;
    std::vector<double> left (static_cast<std::size_t> (kPrime + length), 0.0), right (left.size(), 0.0);
    left[static_cast<std::size_t> (kPrime)] = 1.0;
    for (int offset = 0; offset < kPrime + length; offset += kBlock)
    {
        double* ptrs[] = { left.data() + offset, right.data() + offset };
        processor.process (settings, topology, ptrs, 2, kBlock, 16);
        corrector.process (ptrs, 2, kBlock);
    }
    const std::vector<double> l (left.begin() + kPrime, left.end()), r (right.begin() + kPrime, right.end());
    const auto spectrumL = ssc::test::spectrumOf (l, static_cast<std::size_t> (length));
    const auto spectrumR = ssc::test::spectrumOf (r, static_cast<std::size_t> (length));

    // L = δ: M = S = δ/2, so L' = (T + 1)/2 and R' = (T − 1)/2 (times the delay).
    const int d = corrector.getLatencySamples();
    double worst = 0.0;
    for (std::size_t k = 1; k < spectrumL.size(); ++k)
    {
        const double hz = static_cast<double> (k) * kFs / length;
        if (hz < 20.0 || hz > 0.9 * 0.5 * kFs)
            continue;
        const auto t = filters::designBand (shape, kFs).analogResponse (hz);
        const auto delayFactor = std::polar (1.0, -kTwoPi * hz * d / kFs);
        worst = std::max (worst, std::abs (spectrumL[k] - 0.5 * (t + 1.0) * delayFactor));
        worst = std::max (worst, std::abs (spectrumR[k] - 0.5 * (t - 1.0) * delayFactor));
    }
    CHECK (worst < 0.01); // −40 dB relative to the unit response
}

TEST_CASE ("Natural Phase brickwall: linear phase, steep and flat", "[phase][BAND-3]")
{
    int latency = 0;
    const BandShape cut { Shape::LowCut, 1000.0, 0.0, 0.7071, Slope::Brickwall };
    const int length = 1 << 14;
    const auto h = naturalPhaseResponse ({ cut }, Quality::Max, length, latency, true);

    // Symmetric impulse response about the latency: linear phase.
    for (int i = 1; i <= latency; ++i)
        CHECK (std::abs (h[static_cast<std::size_t> (latency - i)] - h[static_cast<std::size_t> (latency + i)]) < 1e-6);

    const auto spectrum = ssc::test::spectrumOf (h, static_cast<std::size_t> (length));
    auto levelAt = [&] (double hz) { return 20.0 * std::log10 (std::abs (spectrum[static_cast<std::size_t> (hz * length / kFs)])); };
    CHECK (levelAt (1000.0) < -5.0);
    CHECK (levelAt (1000.0) > -7.0);
    CHECK (levelAt (400.0) < -60.0);
    CHECK (levelAt (100.0) < -60.0);
    CHECK (std::abs (levelAt (2000.0)) < 0.01);
    CHECK (std::abs (levelAt (10000.0)) < 0.01);
}
