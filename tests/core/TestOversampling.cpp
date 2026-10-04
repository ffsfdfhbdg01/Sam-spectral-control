#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

#include "ssc/oversampling/Oversampler.h"

using namespace ssc;
using namespace ssc::oversampling;

namespace
{
constexpr int kMaxBlock = 512;

/** Magnitudes |X[k]|, k = 0 … size/2, of x zero-padded to `size` (a power of two), in double. */
std::vector<double> magnitudeSpectrum (const std::vector<double>& x, std::size_t size)
{
    std::vector<std::complex<double>> a (size);
    for (std::size_t i = 0; i < std::min (size, x.size()); ++i)
        a[i] = x[i];

    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    std::vector<std::complex<double>> twiddle (size / 2);
    for (std::size_t k = 0; k < size / 2; ++k)
        twiddle[k] = std::polar (1.0, -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (size));
    for (std::size_t len = 2; len <= size; len <<= 1)
        for (std::size_t i = 0; i < size; i += len)
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * twiddle[k * (size / len)];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }

    std::vector<double> magnitude (size / 2 + 1);
    for (std::size_t k = 0; k <= size / 2; ++k)
        magnitude[k] = std::abs (a[k]);
    return magnitude;
}

std::size_t nextPowerOfTwo (std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/** Response of the interpolator (up only) to a unit impulse at the base rate. */
std::vector<double> interpolatorResponse (Oversampler& os, int baseLength)
{
    os.reset();
    const int f = os.getFactor();
    std::vector<double> in (static_cast<std::size_t> (baseLength), 0.0), out (static_cast<std::size_t> (baseLength * f));
    in[0] = 1.0;
    for (int offset = 0; offset < baseLength; offset += kMaxBlock)
    {
        const int n = std::min (kMaxBlock, baseLength - offset);
        os.upsample (0, in.data() + offset, out.data() + offset * f, n);
    }
    return out;
}

/** Equivalent high-rate filter d of the decimator (y[n] = Σ d[F·n − m]·v[m]), from impulses at every phase. */
std::vector<double> decimatorResponse (Oversampler& os, int baseLength)
{
    const int f = os.getFactor();
    std::vector<double> d (static_cast<std::size_t> ((baseLength + 1) * f), 0.0);
    std::vector<double> high (static_cast<std::size_t> (kMaxBlock * f)), out (static_cast<std::size_t> (baseLength));
    for (int phase = 0; phase < f; ++phase)
    {
        os.reset();
        for (int offset = 0; offset < baseLength; offset += kMaxBlock)
        {
            const int n = std::min (kMaxBlock, baseLength - offset);
            std::fill (high.begin(), high.end(), 0.0);
            if (offset == 0)
                high[static_cast<std::size_t> (phase)] = 1.0;
            os.downsample (0, high.data(), out.data() + offset, n);
        }
        for (int n = 0; n < baseLength; ++n)
            d[static_cast<std::size_t> (f * n - phase + f - 1)] = out[static_cast<std::size_t> (n)];
    }
    return d;
}

/** Round trip (up, then down) of a unit impulse at the base rate. */
std::vector<double> roundTripResponse (Oversampler& os, int baseLength)
{
    os.reset();
    const int f = os.getFactor();
    std::vector<double> in (static_cast<std::size_t> (baseLength), 0.0), out (static_cast<std::size_t> (baseLength));
    std::vector<double> high (static_cast<std::size_t> (kMaxBlock * f));
    in[0] = 1.0;
    for (int offset = 0; offset < baseLength; offset += kMaxBlock)
    {
        const int n = std::min (kMaxBlock, baseLength - offset);
        os.upsample (0, in.data() + offset, high.data(), n);
        os.downsample (0, high.data(), out.data() + offset, n);
    }
    return out;
}

double maxAbs (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    double m = 0.0;
    for (std::size_t i = from; i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i]));
    return m;
}

struct BandCheck
{
    double worstPassbandDb = 0.0; ///< largest |deviation| from `gain` in the passband
    double worstStopbandDb = -400.0;
};

/**
    Evaluates a high-rate response h (sample rate = factor, base rate = 1): passband [0, p] and the
    regions [k − p, k + p] (k = 1 … factor − 1) whose content lands in the passband as images or aliases.
*/
BandCheck checkBands (const std::vector<double>& h, int factor, double passband, double gain)
{
    const std::size_t size = std::max<std::size_t> (1 << 16, nextPowerOfTwo (h.size()) * 8);
    const auto magnitude = magnitudeSpectrum (h, size);
    BandCheck result;
    for (std::size_t k = 0; k <= size / 2; ++k)
    {
        const double f = static_cast<double> (k) / static_cast<double> (size) * factor; // in units of the base rate
        const double db = 20.0 * std::log10 (std::max (magnitude[k] / gain, 1e-30));
        if (f <= passband)
            result.worstPassbandDb = std::max (result.worstPassbandDb, std::abs (db));
        for (int image = 1; image < factor; ++image)
            if (std::abs (f - image) <= passband)
                result.worstStopbandDb = std::max (result.worstStopbandDb, db);
    }
    return result;
}

const Quality kQualities[] = { Quality::Eco, Quality::Normal, Quality::High, Quality::Max };
const FilterType kTypes[] = { FilterType::PolyphaseIir, FilterType::LinearPhaseFir };
const int kFactors[] = { 2, 4, 8 };
} // namespace

TEST_CASE ("Half-band FIR design: unity DC gain, zero taps at even offsets", "[oversampling]")
{
    for (const double attenuation : { 80.0, 90.0, 120.0 })
    {
        const auto taps = designHalfbandFir (attenuation, 0.07);
        REQUIRE (taps.size() % 4 == 3);
        const std::size_t centre = (taps.size() - 1) / 2;
        CHECK (taps[centre] == 0.5);
        double sum = 0.0;
        for (std::size_t n = 0; n < taps.size(); ++n)
        {
            sum += taps[n];
            if (n != centre && (n - centre) % 2 == 0)
                CHECK (taps[n] == 0.0);
            CHECK (taps[n] == taps[taps.size() - 1 - n]); // symmetric: linear phase
        }
        CHECK (std::abs (sum - 1.0) < 1e-14);
    }
}

TEST_CASE ("Polyphase IIR design: coefficients inside (0, 1), more for a steeper filter", "[oversampling]")
{
    const auto normal = designPolyphaseHalfband (90.0, 0.068);
    const auto max = designPolyphaseHalfband (120.0, 0.046);
    CHECK (normal.size() >= 4);
    CHECK (max.size() > normal.size());
    for (const auto& coefs : { normal, max })
        for (const double c : coefs)
        {
            CHECK (c > 0.0);
            CHECK (c < 1.0);
        }
}

TEST_CASE ("[OS-2] Oversampling filters meet the passband and attenuation specification", "[oversampling][OS-2]")
{
    for (const auto type : kTypes)
        for (const auto quality : kQualities)
            for (const int factor : kFactors)
            {
                CAPTURE (static_cast<int> (type), static_cast<int> (quality), factor);
                const auto spec = filterSpecFor (quality);
                Oversampler os;
                os.prepare (1, kMaxBlock, factor, type, quality);
                REQUIRE (os.getFactor() == factor);

                const int baseLength = 4096;
                const auto up = interpolatorResponse (os, baseLength);
                // The response has decayed completely (the analysis is not truncated).
                CHECK (maxAbs (up, up.size() * 3 / 4, up.size()) < 1e-13);

                const auto upBands = checkBands (up, factor, spec.passband, factor);
                CHECK (upBands.worstPassbandDb <= spec.rippleDb);
                CHECK (upBands.worstStopbandDb <= -spec.attenuationDb);

                const auto down = decimatorResponse (os, baseLength);
                const auto downBands = checkBands (down, factor, spec.passband, 1.0);
                CHECK (downBands.worstPassbandDb <= spec.rippleDb);
                CHECK (downBands.worstStopbandDb <= -spec.attenuationDb);

                // Round trip at the base rate.
                const auto trip = roundTripResponse (os, baseLength);
                const auto tripBands = checkBands (trip, 1, spec.passband, 1.0);
                CHECK (tripBands.worstPassbandDb <= spec.rippleDb);
            }
}

TEST_CASE ("[OS-2] Passband edge: 19 kHz at 44.1 kHz (Normal) and 20 kHz (Max)", "[oversampling][OS-2]")
{
    CHECK (filterSpecFor (Quality::Normal).passband * 44100.0 >= 19000.0);
    CHECK (filterSpecFor (Quality::Max).passband * 44100.0 >= 20000.0);
    CHECK (filterSpecFor (Quality::Normal).rippleDb <= 0.05);
    CHECK (filterSpecFor (Quality::Max).rippleDb <= 0.01);
    CHECK (filterSpecFor (Quality::Normal).attenuationDb >= 90.0);
    CHECK (filterSpecFor (Quality::Max).attenuationDb >= 120.0);
}

TEST_CASE ("[OS-3] Reported latency equals the measured round-trip delay", "[oversampling][OS-3]")
{
    for (const auto type : kTypes)
        for (const auto quality : kQualities)
            for (const int factor : kFactors)
            {
                CAPTURE (static_cast<int> (type), static_cast<int> (quality), factor);
                Oversampler os;
                os.prepare (1, kMaxBlock, factor, type, quality);
                const int latency = os.getLatencySamples();
                CHECK (latency >= 1);
                CHECK (latency >= os.getFilterDelay());
                CHECK (latency - os.getFilterDelay() < 1.0);

                const auto trip = roundTripResponse (os, 4096);
                // Group delay at DC = first moment / zeroth moment of the impulse response.
                double m0 = 0.0, m1 = 0.0;
                for (std::size_t n = 0; n < trip.size(); ++n)
                {
                    m0 += trip[n];
                    m1 += static_cast<double> (n) * trip[n];
                }
                CHECK (std::abs (m0 - 1.0) < 1e-9);
                const double groupDelay = m1 / m0;

                if (type == FilterType::LinearPhaseFir)
                {
                    // Linear phase: symmetric about the reported latency, peak exactly there.
                    CHECK (std::abs (groupDelay - latency) < 1e-9);
                    const auto peak = static_cast<int> (
                        std::max_element (trip.begin(), trip.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); })
                        - trip.begin());
                    CHECK (peak == latency);
                    for (int i = 1; i <= latency; ++i)
                        CHECK (std::abs (trip[static_cast<std::size_t> (latency - i)] - trip[static_cast<std::size_t> (latency + i)])
                               < 1e-12);
                }
                else
                {
                    // The fractional IIR delay is completed to the nearest sample of the high rate.
                    CHECK (std::abs (groupDelay - latency) <= 0.5 / factor + 1e-9);
                }
            }
}

TEST_CASE ("Oversampling is independent of the block size and of other channels", "[oversampling][NFR-3]")
{
    std::mt19937 rng (7);
    std::normal_distribution<double> noise (0.0, 0.3);
    const int length = 6000;
    std::vector<double> input (static_cast<std::size_t> (length));
    for (auto& x : input)
        x = noise (rng);

    for (const auto type : kTypes)
        for (const int factor : kFactors)
        {
            CAPTURE (static_cast<int> (type), factor);
            auto run = [&] (const std::vector<int>& blocks, int channel) {
                Oversampler os;
                os.prepare (2, kMaxBlock, factor, type, Quality::Normal);
                std::vector<double> out (static_cast<std::size_t> (length)), high (static_cast<std::size_t> (kMaxBlock * factor));
                std::vector<double> other (static_cast<std::size_t> (kMaxBlock), 0.25);
                int offset = 0, b = 0;
                while (offset < length)
                {
                    const int n = std::min (
                        { blocks[static_cast<std::size_t> (b++ % static_cast<int> (blocks.size()))], length - offset, kMaxBlock });
                    // The other channel carries a different signal in between.
                    os.upsample (1 - channel, other.data(), high.data(), n);
                    os.downsample (1 - channel, high.data(), other.data(), n);
                    os.upsample (channel, input.data() + offset, high.data(), n);
                    os.downsample (channel, high.data(), out.data() + offset, n);
                    offset += n;
                }
                return out;
            };
            const auto reference = run ({ 512 }, 0);
            CHECK (run ({ 1 }, 0) == reference);
            CHECK (run ({ 64, 7, 333, 512, 1 }, 1) == reference);
        }
}

TEST_CASE ("Oversampling: reset clears the state, factor 1 is a copy", "[oversampling]")
{
    Oversampler os;
    os.prepare (1, kMaxBlock, 4, FilterType::LinearPhaseFir, Quality::Normal);
    const auto first = roundTripResponse (os, 1024);
    const auto second = roundTripResponse (os, 1024); // roundTripResponse resets first
    CHECK (first == second);

    Oversampler bypass;
    bypass.prepare (1, kMaxBlock, 1, FilterType::PolyphaseIir, Quality::Normal);
    CHECK (bypass.getLatencySamples() == 0);
    std::vector<double> in { 0.1, -0.2, 0.3 }, high (3), out (3);
    bypass.upsample (0, in.data(), high.data(), 3);
    bypass.downsample (0, high.data(), out.data(), 3);
    CHECK (out == in);
}

TEST_CASE ("Decimation gain and upsampling delay match the measured responses (MTR-2)", "[oversampling][MTR-2]")
{
    for (const auto type : kTypes)
        for (const auto quality : { Quality::Normal, Quality::Max })
            for (const int factor : kFactors)
            {
                CAPTURE (static_cast<int> (type), static_cast<int> (quality), factor);
                Oversampler os;
                os.prepare (1, kMaxBlock, factor, type, quality);

                // |D(f)|² of the equivalent high-rate decimation filter against the formula.
                const std::size_t size = 1 << 16;
                const auto down = decimatorResponse (os, 4096);
                const auto magnitude = magnitudeSpectrum (down, size);
                double worst = 0.0;
                for (std::size_t k = 0; k <= size / 2; k += 7)
                {
                    const double fraction = static_cast<double> (k) / static_cast<double> (size);
                    const double measured = magnitude[k] * magnitude[k];
                    const double predicted = os.decimationGain (fraction);
                    // Compare in dB down to −200 dB.
                    const double error = std::abs (10.0 * std::log10 (std::max (measured, 1e-20) / std::max (predicted, 1e-20)));
                    if (measured > 1e-20 || predicted > 1e-20)
                        worst = std::max (worst, error);
                }
                CHECK (worst < 0.01);
                CHECK (os.decimationGain (0.0) == Catch::Approx (1.0).margin (1e-6));
                // Half the base rate: −6 dB for the FIR (amplitude ½), −3 dB for the power-complementary IIR.
                CHECK (os.decimationGain (0.5 / factor) == Catch::Approx (type == FilterType::LinearPhaseFir ? 0.25 : 0.5).margin (1e-3));

                // Upsampling delay: the centre of the interpolator's impulse response (FIR), its DC
                // group delay (IIR).
                const auto up = interpolatorResponse (os, 4096);
                double m0 = 0.0, m1 = 0.0;
                for (std::size_t n = 0; n < up.size(); ++n)
                {
                    m0 += up[n];
                    m1 += static_cast<double> (n) * up[n];
                }
                if (type == FilterType::LinearPhaseFir)
                {
                    const auto peak =
                        std::max_element (up.begin(), up.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); })
                        - up.begin();
                    CHECK (static_cast<double> (peak) == os.getUpsampleDelay());
                    CHECK (std::abs (m1 / m0 - os.getUpsampleDelay()) < 1e-6);
                }
                else
                {
                    CHECK (std::abs (m1 / m0 - os.getUpsampleDelay()) < 0.75);
                }
            }
}
