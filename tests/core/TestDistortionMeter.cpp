#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

#include "TestSignals.h"
#include "ssc/analysis/DistortionMeter.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/Math.h"

using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr double kFs = 48000.0;
constexpr std::size_t kFftSize = 65536; ///< reference analysis: 0.73 Hz bins, tones exactly on a bin

/** A frequency close to `hz` that lies exactly on a bin of the reference analysis. */
double onBin (double hz, double fs = kFs)
{
    return std::round (hz / fs * static_cast<double> (kFftSize)) * fs / static_cast<double> (kFftSize);
}

BandSettings bell (double hz, double gainDb, double q, Character character)
{
    BandSettings s;
    s.used = s.active = true;
    s.shape = { Shape::Bell, hz, gainDb, q, Slope::Db12, character };
    return s;
}

struct Readings
{
    double meterProducts = 0.0, meterAliasing = 0.0; ///< ratios to the output power, mean over the captures
    double referenceProducts = 0.0;                  ///< everything but the input tones (THD+N)
    double referenceAliasing = 0.0;                  ///< everything but the tones and their true products
    int captures = 0;
};

double db (double ratio)
{
    return 10.0 * std::log10 (std::max (ratio, 1.0e-30));
}

struct Tone
{
    double hz = 1000.0, amplitude = 1.0;
};

/** Bins of the true products of the tones: Σ aᵢ·kᵢ with Σ|aᵢ| ≤ order, below Nyquist (no folding). */
std::vector<bool> trueProductBins (const std::vector<std::size_t>& bins, int order)
{
    std::vector<bool> genuine (kFftSize / 2 + 1, false);
    if (bins.size() == 1)
    {
        for (std::size_t k = bins[0]; k <= kFftSize / 2; k += bins[0])
            genuine[k] = true;
        return genuine;
    }
    const auto k1 = static_cast<long> (bins[0]), k2 = static_cast<long> (bins[1]);
    for (long a = -order; a <= order; ++a)
        for (long b = -order; b <= order; ++b)
        {
            if (std::abs (a) + std::abs (b) > order)
                continue;
            const long k = std::abs (a * k1 + b * k2);
            if (k <= static_cast<long> (kFftSize / 2))
                genuine[static_cast<std::size_t> (k)] = true;
        }
    return genuine;
}

/**
    Tones through the engine with the meters on (mono, or stereo with the tones on the left and
    `rightGain` × them on the right). The meter: every published capture analysed as the plugin does
    (products × lanes scale × products gain + modulation, over the output power). The reference: the
    spectrum of the last kFftSize output samples per channel (rectangular window, the tones on bins:
    every component on its own bin), powers summed over the channels.
*/
Readings measure (const BandSettingsArray& bands, const GlobalParams& params, const std::vector<Tone>& tones, double seconds = 2.5,
                  int channels = 1, double rightGain = 0.0)
{
    auto engine = std::make_unique<Engine>();
    EngineConfig config;
    config.sampleRate = kFs;
    config.maxBlockSize = 512;
    config.numChannels = channels;
    config.topology = channels == 1 ? ChannelTopology::mono() : ChannelTopology::stereo();
    engine->prepare (config);
    engine->setDistortionMetering (true);

    analysis::DistortionMeter meter;
    Readings r;
    const auto length = static_cast<std::size_t> (seconds * kFs);
    std::vector<std::vector<double>> output (static_cast<std::size_t> (channels), std::vector<double> (length, 0.0));
    for (std::size_t i = 0; i < length; ++i)
    {
        double v = 0.0;
        for (const auto& t : tones)
            v += t.amplitude * std::sin (kTwoPi * t.hz * static_cast<double> (i) / kFs);
        output[0][i] = v;
        if (channels > 1)
            output[1][i] = rightGain * v;
    }

    const std::size_t settle = static_cast<std::size_t> (0.5 * kFs);
    for (std::size_t offset = 0; offset < length; offset += 512)
    {
        const int n = static_cast<int> (std::min<std::size_t> (512, length - offset));
        double* ptrs[2] = { output[0].data() + offset, channels > 1 ? output[1].data() + offset : nullptr };
        engine->process (ptrs, channels, n, params, bands);

        auto& captures = engine->distortionCaptures();
        if (! captures.update() || offset < settle)
            continue;
        const auto& c = captures.read();
        if (! meter.isPreparedFor (c.baseRate, c.osFactor, c.osType, c.osQuality))
            meter.prepare (c.baseRate, c.osFactor, c.osType, c.osQuality);
        double products = 0.0, aliasing = 0.0;
        for (const auto& b : c.bands)
        {
            products += b.modulationPower;
            if (! b.shaped)
                continue;
            const auto d = meter.analyse (b.x.data(), b.kind);
            products += d.products * b.lanesScale;
            aliasing += d.aliasing * b.lanesScale;
        }
        r.meterProducts += products * c.productsGain / c.outputPower;
        r.meterAliasing += aliasing * c.productsGain / c.outputPower;
        ++r.captures;
    }
    if (r.captures > 0)
    {
        r.meterProducts /= r.captures;
        r.meterAliasing /= r.captures;
    }

    // Reference.
    std::vector<std::size_t> toneBins;
    for (const auto& t : tones)
        toneBins.push_back (static_cast<std::size_t> (std::lround (t.hz / kFs * kFftSize)));
    const auto genuine = trueProductBins (toneBins, 9);
    double total = 0.0, rest = 0.0, inharmonic = 0.0;
    for (const auto& channel : output)
    {
        const std::vector<double> tail (channel.end() - static_cast<std::ptrdiff_t> (kFftSize), channel.end());
        const auto spectrum = test::spectrumOf (tail, kFftSize);
        for (std::size_t k = 1; k <= kFftSize / 2; ++k)
        {
            const double power = std::norm (spectrum[k]) * (k == kFftSize / 2 ? 1.0 : 2.0);
            total += power;
            if (std::find (toneBins.begin(), toneBins.end(), k) != toneBins.end())
                continue;
            rest += power;
            if (! genuine[k])
                inharmonic += power;
        }
    }
    r.referenceProducts = rest / total;
    r.referenceAliasing = inharmonic / total;
    return r;
}

Readings measure (const BandSettingsArray& bands, const GlobalParams& params, double hz, double amplitude)
{
    return measure (bands, params, { Tone { hz, amplitude } });
}

void checkAgainstReference (const Readings& r)
{
    REQUIRE (r.captures >= 15);
    CHECK (std::abs (db (r.meterProducts) - db (r.referenceProducts)) <= 3.0);
    if (db (r.referenceAliasing) > -110.0)
        CHECK (std::abs (db (r.meterAliasing) - db (r.referenceAliasing)) <= 3.0);
    else
        CHECK (db (r.meterAliasing) <= -110.0);
}
} // namespace

TEST_CASE ("THD+N meter: Warm, 1 kHz at 0 dBFS, against the harmonic analysis of the output (MTR-3)", "[meters][MTR-3]")
{
    const double f0 = onBin (1000.0);
    BandSettingsArray bands {};
    bands[0] = bell (f0, 6.0, 1.0, Character::Warm); // CHR-3 calibration: u ≈ 1 at the centre
    GlobalParams params;
    const auto r = measure (bands, params, f0, 1.0);
    std::printf ("[MTR-3] Warm 1 kHz 0 dBFS: THD+N meter %.2f dB, reference %.2f dB (aliasing %.1f / %.1f dB), %d captures\n",
                 db (r.meterProducts), db (r.referenceProducts), db (r.meterAliasing), db (r.referenceAliasing), r.captures);
    REQUIRE (r.captures >= 15);
    CHECK (std::abs (db (r.meterProducts) - db (r.referenceProducts)) <= 3.0);
}

TEST_CASE ("Aliasing meter: Warm +12 dB, 10 kHz, OS 1x/2x/4x/8x, against the reference (MTR-3)", "[meters][MTR-3]")
{
    const double f0 = onBin (10000.0);
    BandSettingsArray bands {};
    bands[0] = bell (f0, 12.0, 1.0, Character::Warm);
    for (const auto os : { Oversampling::Off, Oversampling::X2, Oversampling::X4, Oversampling::X8 })
    {
        CAPTURE (oversamplingFactor (os));
        GlobalParams params;
        params.oversampling = os;
        const auto r = measure (bands, params, f0, 1.0);
        std::printf ("[MTR-3] Warm +12 dB 10 kHz OS %dx: aliasing meter %.1f dB, reference %.1f dB; THD+N %.1f / %.1f dB\n",
                     oversamplingFactor (os), db (r.meterAliasing), db (r.referenceAliasing), db (r.meterProducts),
                     db (r.referenceProducts));
        REQUIRE (r.captures >= 15);
        if (db (r.referenceAliasing) > -110.0)
            CHECK (std::abs (db (r.meterAliasing) - db (r.referenceAliasing)) <= 3.0);
        else
            CHECK (db (r.meterAliasing) <= -110.0);
        CHECK (std::abs (db (r.meterProducts) - db (r.referenceProducts)) <= 3.0);
    }
}

TEST_CASE ("Meters of a linear configuration read below -120 dB (MTR-1)", "[meters][MTR-1]")
{
    BandSettingsArray bands {};
    bands[0] = bell (onBin (1000.0), 6.0, 1.0, Character::Clear);
    bands[1] = bell (onBin (5000.0), -9.0, 3.0, Character::Clear);
    bands[2].used = bands[2].active = true;
    bands[2].shape = { Shape::LowCut, 80.0, 0.0, 0.7071, Slope::Db24, Character::Clear };
    GlobalParams params;
    const auto r = measure (bands, params, onBin (1000.0), 0.5);
    std::printf ("[MTR-1] Clear, Static: THD+N %.1f dB, aliasing %.1f dB\n", db (r.meterProducts), db (r.meterAliasing));
    REQUIRE (r.captures >= 15);
    CHECK (db (r.meterProducts) < -120.0);
    CHECK (db (r.meterAliasing) < -120.0);
}

TEST_CASE ("Meters follow the offline analysis across Characters, Analog Feel models and oversampling (MTR-3)", "[meters][MTR-3]")
{
    struct Case
    {
        const char* name;
        Character character;
        PhaseMode mode;
        AnalogModel model;
        Quality quality;
        Oversampling os;
        double hz, gainDb, amplitude;
    };
    const Case cases[] = {
        { "Sharp 1 kHz +6 dB", Character::Sharp, PhaseMode::MinimumPhase, AnalogModel::N, Quality::Normal, Oversampling::Off, 1000.0, 6.0,
          1.0 },
        { "Sharp 7 kHz +9 dB", Character::Sharp, PhaseMode::MinimumPhase, AnalogModel::N, Quality::Normal, Oversampling::Off, 7000.0, 9.0,
          0.7 },
        { "Warm 15 kHz +6 dB", Character::Warm, PhaseMode::MinimumPhase, AnalogModel::N, Quality::Normal, Oversampling::Off, 15000.0, 6.0,
          1.0 },
        { "Warm 10 kHz +12 dB, 2x Max", Character::Warm, PhaseMode::MinimumPhase, AnalogModel::N, Quality::Max, Oversampling::X2, 10000.0,
          12.0, 1.0 },
        { "Natural Phase Sharp 9 kHz +9 dB, 4x", Character::Sharp, PhaseMode::NaturalPhase, AnalogModel::N, Quality::Normal,
          Oversampling::X4, 9000.0, 9.0, 1.0 },
        { "Analog N soft 100 Hz +6 dB", Character::Warm, PhaseMode::AnalogFeel, AnalogModel::N, Quality::Normal, Oversampling::Off, 100.0,
          6.0, 0.5 },
        { "Analog P hard 3.1 kHz +9 dB (High: 2x)", Character::Sharp, PhaseMode::AnalogFeel, AnalogModel::P, Quality::High,
          Oversampling::Off, 3100.0, 9.0, 1.0 },
        { "Analog S hard 9 kHz +9 dB", Character::Sharp, PhaseMode::AnalogFeel, AnalogModel::S, Quality::Normal, Oversampling::Off, 9000.0,
          9.0, 1.0 },
    };
    for (const auto& c : cases)
    {
        CAPTURE (c.name);
        BandSettingsArray bands {};
        bands[0] = bell (onBin (c.hz), c.gainDb, 1.0, c.character);
        GlobalParams params;
        params.phaseMode = c.mode;
        params.analogModel = c.model;
        params.quality = c.quality;
        params.oversampling = c.os;
        const auto r = measure (bands, params, onBin (c.hz), c.amplitude);
        std::printf ("[MTR-3] %-38s THD+N %7.1f / %7.1f dB, aliasing %7.1f / %7.1f dB (meter / reference)\n", c.name, db (r.meterProducts),
                     db (r.referenceProducts), db (r.meterAliasing), db (r.referenceAliasing));
        checkAgainstReference (r);
    }
}

TEST_CASE ("Meters with two tones (IMD) and with Mid/Side lanes (MTR-3)", "[meters][MTR-3]")
{
    SECTION ("Warm high shelf, 9 + 11 kHz: IMD and its aliases")
    {
        BandSettingsArray bands {};
        bands[0].used = bands[0].active = true;
        bands[0].shape = { Shape::HighShelf, 6000.0, 9.0, 0.7071, Slope::Db12, Character::Warm };
        GlobalParams params;
        const auto r = measure (bands, params, { Tone { onBin (9000.0), 0.5 }, Tone { onBin (11000.0), 0.5 } });
        std::printf ("[MTR-3] two tones 9 + 11 kHz, Warm shelf: THD+N %.1f / %.1f dB, aliasing %.1f / %.1f dB\n", db (r.meterProducts),
                     db (r.referenceProducts), db (r.meterAliasing), db (r.referenceAliasing));
        checkAgainstReference (r);
    }
    SECTION ("Sharp bell on the Mid of a stereo signal: both outputs carry the products")
    {
        BandSettingsArray bands {};
        bands[0] = bell (onBin (2000.0), 9.0, 1.0, Character::Sharp);
        bands[0].component = StereoComponent::Mid;
        GlobalParams params;
        const auto r = measure (bands, params, { Tone { onBin (2000.0), 0.7 } }, 2.5, 2, 0.5);
        std::printf ("[MTR-3] Sharp on Mid, stereo: THD+N %.1f / %.1f dB, aliasing %.1f / %.1f dB\n", db (r.meterProducts),
                     db (r.referenceProducts), db (r.meterAliasing), db (r.referenceAliasing));
        checkAgainstReference (r);
    }
}

TEST_CASE ("Gain modulation of a Dynamic band: fast dynamics count, the intended slow action does not (MTR-2)", "[meters][MTR-2]")
{
    // A 1 kHz tone into a Dynamic bell at 1 kHz: gated (50 ms on, 50 ms off: steps of level) or with
    // a slow 8 Hz envelope (below fc/8 = 125 Hz: what the dynamics are meant to follow).
    auto run = [] (double attackMs, double releaseMs, bool gated, bool dynamicBand) {
        auto engine = std::make_unique<Engine>();
        EngineConfig config;
        config.sampleRate = kFs;
        config.maxBlockSize = 512;
        config.numChannels = 1;
        config.topology = ChannelTopology::mono();
        engine->prepare (config);
        engine->setDistortionMetering (true);

        BandSettingsArray bands {};
        bands[0] = bell (1000.0, 0.0, 2.0, Character::Clear);
        if (dynamicBand)
        {
            bands[0].mode = BandMode::Dynamic;
            auto& d = bands[0].dynamics;
            d.thresholdDb = -40.0;
            d.ratio = 3.0;
            d.rangeDb = 30.0;
            d.kneeDb = 0.0;
            d.attackMs = attackMs;
            d.releaseMs = releaseMs;
        }
        GlobalParams params;
        const auto length = static_cast<std::size_t> (2.0 * kFs);
        std::vector<double> signal (length);
        for (std::size_t i = 0; i < length; ++i)
        {
            const double t = static_cast<double> (i) / kFs;
            const double envelope = gated ? (std::fmod (t, 0.1) < 0.05 ? 0.5 : 0.0) : 0.3 + 0.25 * std::sin (kTwoPi * 8.0 * t);
            signal[i] = envelope * std::sin (kTwoPi * 1000.0 * t);
        }
        double modulation = 0.0, output = 0.0;
        int captures = 0;
        for (std::size_t offset = 0; offset < length; offset += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, length - offset));
            double* ptrs[] = { signal.data() + offset };
            engine->process (ptrs, 1, n, params, bands);
            auto& c = engine->distortionCaptures();
            if (c.update() && offset > length / 4)
            {
                modulation += c.read().bands[0].modulationPower * c.read().productsGain;
                output += c.read().outputPower;
                ++captures;
            }
        }
        REQUIRE (captures > 10);
        return db (modulation / output);
    };

    const double fast = run (0.1, 5.0, true, true);
    const double slow = run (50.0, 500.0, true, true);
    const double envelope = run (0.1, 5.0, false, true);
    const double staticBand = run (0.1, 5.0, true, false);
    std::printf ("[MTR-2] gain modulation: gated fast %.1f dB, gated slow %.1f dB, 8 Hz envelope %.1f dB, static %.1f dB\n", fast, slow,
                 envelope, staticBand);
    CHECK (fast > -45.0);
    CHECK (fast - slow > 15.0);
    CHECK (fast - envelope > 15.0);
    CHECK (staticBand < -200.0);
}

TEST_CASE ("The shadow interpolator settles within the margins of a capture", "[meters][MTR-2]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const int factor : { 1, 2, 4, 8 })
        {
            if (factor > maxOversamplingFor (rate))
                continue;
            CAPTURE (rate, factor);
            analysis::DistortionMeter meter;
            meter.prepare (rate, factor, oversampling::FilterType::LinearPhaseFir, Quality::Max);
            CHECK (meter.getShadowDelay() <= analysis::DistortionMeter::kStart * meter.getShadowFactor());
            CHECK (meter.getShadowFactor() * factor >= 4);
        }
}
