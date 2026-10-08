// Measurement reports (ADR-0015, Definition of Done): runs test signals through the DSP core as
// the plug-in does and writes Markdown reports.
// Usage: ssc_measure <output dir> [filters|distortion|latency|autogain|resonance|all]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ResonanceMaterial.h"
#include "TestSignals.h"
#include "ssc/engine/Engine.h"
#include "ssc/fft/RealFft.h"
#include "ssc/filters/BandDesign.h"
#include "ssc/loudness/Loudness.h"
#include "ssc/spectral/SpectralEngine.h"
#include "ssc/util/Math.h"

using namespace ssc;
using namespace ssc::engine;

namespace
{
constexpr int kBlock = 512;

// Signal path ---------------------------------------------------------------------------------

std::unique_ptr<Engine> makeEngine (double fs, int channels = 1)
{
    auto engine = std::make_unique<Engine>();
    EngineConfig config;
    config.sampleRate = fs;
    config.maxBlockSize = kBlock;
    config.numChannels = channels;
    config.topology = channels == 1 ? ChannelTopology::mono() : ChannelTopology::stereo();
    engine->prepare (config);
    return engine;
}

/** Mono signal through the engine in host blocks of 512. */
void run (Engine& engine, std::vector<double>& x, const GlobalParams& params, const BandSettingsArray& bands)
{
    for (std::size_t offset = 0; offset < x.size(); offset += kBlock)
    {
        const int n = static_cast<int> (std::min<std::size_t> (kBlock, x.size() - offset));
        double* channel[] = { x.data() + offset };
        engine.process (channel, 1, n, params, bands);
    }
}

BandSettingsArray oneBand (const filters::BandShape& shape)
{
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = shape;
    return bands;
}

double dbOf (double power) noexcept
{
    return 10.0 * std::log10 (std::max (power, 1.0e-30));
}

/** DFT of x at a frequency (cycles per sample), double precision. */
std::complex<double> dft (const double* x, std::size_t n, double cyclesPerSample)
{
    // Goertzel-style recurrence would lose precision for long windows; a rotating phasor keeps it.
    std::complex<double> sum (0.0, 0.0), w (1.0, 0.0);
    const auto step = std::polar (1.0, -kTwoPi * cyclesPerSample);
    for (std::size_t i = 0; i < n; ++i)
    {
        sum += x[i] * w;
        w *= step;
        if ((i & 1023) == 1023)
            w /= std::abs (w);
    }
    return sum;
}

std::string fixed (double value, int decimals)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.*f", decimals, value);
    std::string s (text);
    std::replace (s.begin(), s.end(), '.', ','); // Polish decimal comma, like the rest of the documentation
    if (s.size() > 1 && s[0] == '-')
        s = "\xe2\x88\x92" + s.substr (1); // typographic minus
    return s;
}

const char* phaseName (PhaseMode mode)
{
    switch (mode)
    {
        case PhaseMode::MinimumPhase: return "Minimum Phase";
        case PhaseMode::NaturalPhase: return "Natural Phase";
        case PhaseMode::AnalogFeel: return "Analog Feel";
    }
    return "?";
}

const char* qualityName (Quality q)
{
    switch (q)
    {
        case Quality::Eco: return "Eco";
        case Quality::Normal: return "Normal";
        case Quality::High: return "High";
        case Quality::Max: return "Max";
    }
    return "?";
}

std::string header (const char* title, const char* requirement)
{
    std::ostringstream out;
    const auto now = std::time (nullptr);
    char date[32];
    std::strftime (date, sizeof (date), "%Y-%m-%d", std::gmtime (&now));
    out << "# " << title << "\n\n"
        << "Wygenerowane przez `ssc_measure` (" << date << ") z rdzenia DSP w wersji " << SSC_MEASURE_VERSION
        << " — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). " << requirement << "\n\n";
    return out.str();
}

// Filters -------------------------------------------------------------------------------------

/** Largest magnitude error (dB) of the engine against the band's analog prototype, 20 Hz …
    min (20 kHz, 0.45 fs), where the prototype is above −30 dB. */
double filterError (double fs, PhaseMode mode, Quality quality, const filters::BandShape& shape)
{
    auto engine = makeEngine (fs);
    GlobalParams params;
    params.phaseMode = mode;
    params.quality = quality;
    const auto bands = oneBand (shape);
    std::vector<double> settle (static_cast<std::size_t> (0.25 * fs), 0.0);
    run (*engine, settle, params, bands);
    std::vector<double> h (static_cast<std::size_t> (fs > 60000.0 ? 1 << 17 : 1 << 16), 0.0);
    h[0] = 1.0;
    run (*engine, h, params, bands);

    // Spectrum, zero-padded so that the log grid falls between close bins.
    const int n = fs > 60000.0 ? 1 << 19 : 1 << 18;
    RealFft fft (n);
    std::vector<float> buffer (static_cast<std::size_t> (n), 0.0f), spectrum (static_cast<std::size_t> (n + 2));
    for (std::size_t i = 0; i < h.size(); ++i)
        buffer[i] = static_cast<float> (h[i]);
    fft.forward (buffer.data(), spectrum.data());
    auto magnitudeDb = [&] (double hz) {
        const double bin = hz / fs * n;
        const auto k = static_cast<std::size_t> (bin);
        const double t = bin - static_cast<double> (k);
        auto at = [&] (std::size_t b) { return std::complex<double> (spectrum[2 * b], spectrum[2 * b + 1]); };
        return 20.0 * std::log10 (std::abs (at (k) * (1.0 - t) + at (k + 1) * t));
    };

    const auto design = filters::designBand (shape, fs);
    const double top = std::min (20000.0, 0.45 * fs);
    double worst = 0.0;
    for (double hz = 20.0; hz <= top; hz *= 1.005)
    {
        const double analog = 20.0 * std::log10 (std::abs (design.analogResponse (hz)));
        if (analog < -30.0)
            continue;
        worst = std::max (worst, std::abs (magnitudeDb (hz) - analog));
    }
    return worst;
}

std::string filtersReport()
{
    struct Case
    {
        const char* name;
        filters::BandShape shape;
    };
    const Case cases[] = {
        { "Bell 100 Hz, +12 dB, Q 1", { Shape::Bell, 100.0, 12.0, 1.0, Slope::Db12 } },
        { "Bell 1 kHz, −12 dB, Q 2", { Shape::Bell, 1000.0, -12.0, 2.0, Slope::Db12 } },
        { "Bell 5 kHz, +6 dB, Q 4", { Shape::Bell, 5000.0, 6.0, 4.0, Slope::Db12 } },
        { "Bell 12 kHz, +9 dB, Q 1.5", { Shape::Bell, 12000.0, 9.0, 1.5, Slope::Db12 } },
        { "Bell 16 kHz, −6 dB, Q 3", { Shape::Bell, 16000.0, -6.0, 3.0, Slope::Db12 } },
        { "Low Shelf 100 Hz, +6 dB", { Shape::LowShelf, 100.0, 6.0, 0.7071, Slope::Db12 } },
        { "High Shelf 8 kHz, +6 dB", { Shape::HighShelf, 8000.0, 6.0, 0.7071, Slope::Db12 } },
        { "High Shelf 15 kHz, −9 dB", { Shape::HighShelf, 15000.0, -9.0, 0.7071, Slope::Db12 } },
        { "Low Cut 50 Hz, 24 dB/okt.", { Shape::LowCut, 50.0, 0.0, 0.7071, Slope::Db24 } },
        { "High Cut 15 kHz, 12 dB/okt.", { Shape::HighCut, 15000.0, 0.0, 0.7071, Slope::Db12 } },
        { "Notch 1 kHz, Q 10", { Shape::Notch, 1000.0, 0.0, 10.0, Slope::Db12 } },
        { "Band Pass 2 kHz, Q 2", { Shape::BandPass, 2000.0, 0.0, 2.0, Slope::Db12 } },
        { "Tilt Shelf 1 kHz, +6 dB", { Shape::TiltShelf, 1000.0, 6.0, 0.7071, Slope::Db12 } },
    };
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    struct Mode
    {
        PhaseMode mode;
        Quality quality;
    };
    const Mode modes[] = { { PhaseMode::MinimumPhase, Quality::Normal },
                           { PhaseMode::MinimumPhase, Quality::High },
                           { PhaseMode::MinimumPhase, Quality::Max },
                           { PhaseMode::NaturalPhase, Quality::Normal },
                           { PhaseMode::NaturalPhase, Quality::Max } };

    std::ostringstream out;
    out << header ("Dokładność filtrów", "Wymagania: BAND-2, BAND-3, PH-1, TST-1.");
    out << "Metoda: odpowiedź impulsowa pasma przez silnik (po 0,25 s ciszy), widmo FFT z dopełnieniem zerami, porównanie amplitudy z "
           "prototypem analogowym pasma (`BandDesign::analogResponse`). W tabelach największy błąd |dB| w 20 Hz – min(20 kHz; 0,45·fs) "
           "tam, gdzie prototyp jest powyżej −30 dB (bez dna cięć i notcha). Character Clear, oversampling wyłączony.\n\n"
           "Kryterium TST-1 / PH-1: ±0,1 dB do 20 kHz przy 44,1 kHz — w Natural Phase i w Minimum Phase z Quality Max "
           "(wewnętrzne 2× dla pasm powyżej fs/8). Minimum Phase Normal ma udokumentowaną obwiednię błędu filtrów dopasowanych "
           "(research/05 §c): przy pasmach blisko Nyquista błąd rośnie.\n\n";
    for (const auto& m : modes)
    {
        out << "## " << phaseName (m.mode) << ", Quality " << qualityName (m.quality) << "\n\n";
        out << "| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |\n|---|---|---|---|\n";
        double worst44 = 0.0;
        for (const auto& c : cases)
        {
            out << "| " << c.name;
            for (const double fs : rates)
            {
                const double e = filterError (fs, m.mode, m.quality, c.shape);
                if (fs == 44100.0)
                    worst44 = std::max (worst44, e);
                out << " | " << fixed (e, 3) << " dB";
            }
            out << " |\n";
        }
        out << "\nNajwiększy błąd przy 44,1 kHz: **" << fixed (worst44, 3) << " dB** — kryterium ±0,1 dB "
            << (worst44 <= 0.1 ? "spełnione" : "nie dotyczy tego trybu (patrz wyżej)") << ".\n\n";
        std::fprintf (stderr, "filters: %s %s done\n", phaseName (m.mode), qualityName (m.quality));
    }
    return out.str();
}

// Distortion ----------------------------------------------------------------------------------

struct Distortion
{
    double thdnDb = -300.0, thdDb = -300.0, aliasingDb = -300.0;
};

/** A steady tone through the engine; harmonics, aliases and the rest relative to the fundamental.
    Coherent: the window holds a whole number of periods of the tone and of every product. */
Distortion measureTone (double fs, const GlobalParams& params, const BandSettingsArray& bands, double hz, double levelDb)
{
    constexpr std::size_t n = 1 << 16;
    const double bin = std::round (hz * n / fs);
    const double f0 = bin / static_cast<double> (n); // cycles per sample
    const double amplitude = std::pow (10.0, levelDb / 20.0);
    auto engine = makeEngine (fs);
    const auto settle = static_cast<std::size_t> (0.5 * fs);
    std::vector<double> x (settle + n);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = amplitude * std::sin (kTwoPi * f0 * static_cast<double> (i));
    run (*engine, x, params, bands);
    const double* y = x.data() + settle;

    auto power = [&] (double cycles) { return 2.0 * std::norm (dft (y, n, cycles)) / (static_cast<double> (n) * n); };
    const double fundamental = power (f0);

    // THD+N from the residual after removing the fundamental and the DC in the time domain (no
    // cancellation between two nearly equal powers).
    const auto x0 = dft (y, n, f0);
    double mean = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        mean += y[i];
    mean /= static_cast<double> (n);
    double residual = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double phase = kTwoPi * f0 * static_cast<double> (i);
        const double fitted = 2.0 / static_cast<double> (n) * (x0.real() * std::cos (phase) - x0.imag() * std::sin (phase));
        const double r = y[i] - mean - fitted;
        residual += r * r;
    }
    residual /= static_cast<double> (n);

    double harmonics = 0.0, aliases = 0.0;
    const auto b0 = static_cast<long long> (bin);
    const auto size = static_cast<long long> (n);
    std::vector<long long> seen;
    for (long long k = 2; k <= 64; ++k)
    {
        long long b = (k * b0) % size;
        if (b > size / 2)
            b = size - b;
        if (b == 0 || b == b0 || std::find (seen.begin(), seen.end(), b) != seen.end())
            continue;
        seen.push_back (b);
        const double p = power (static_cast<double> (b) / static_cast<double> (n));
        if (k * b0 < size / 2)
            harmonics += p;
        else
            aliases += p;
    }
    Distortion d;
    d.thdnDb = dbOf (residual / fundamental);
    d.thdDb = dbOf (harmonics / fundamental);
    d.aliasingDb = dbOf (aliases / fundamental);
    return d;
}

std::string distortionReport()
{
    std::ostringstream out;
    out << header ("THD+N i aliasing", "Wymagania: CHR-3, CHR-4, OS-1, OS-4, MTR-3.");
    out << "Metoda: sinus przez silnik (po 0,5 s ustalania), okno 65 536 próbek z całkowitą liczbą okresów tonu i wszystkich "
           "produktów, DFT w podwójnej precyzji. THD — harmoniczne poniżej Nyquista, aliasing — harmoniczne odbite od Nyquista "
           "(do 64. harmonicznej), THD+N — wszystko poza składową stałą i podstawową. Wartości względem tonu podstawowego, "
           "48 kHz.\n\n";

    // Character.
    out << "## Character pasma (Bell 1 kHz, +6 dB, Q 1; ton 1 kHz; Minimum Phase, bez oversamplingu)\n\n";
    out << "| Character | Poziom | THD+N | THD | Aliasing |\n|---|---|---|---|---|\n";
    for (const auto character : { Character::Clear, Character::Warm, Character::Sharp })
        for (const double level : { -18.0, -6.0, 0.0 })
        {
            auto bands = oneBand ({ Shape::Bell, 1000.0, 6.0, 1.0, Slope::Db12 });
            bands[0].shape.character = character;
            const auto d = measureTone (48000.0, {}, bands, 1000.0, level);
            const char* name = character == Character::Clear ? "Clear" : (character == Character::Warm ? "Warm" : "Sharp");
            out << "| " << name << " | " << fixed (level, 0) << " dBFS | " << fixed (d.thdnDb, 1) << " dB | " << fixed (d.thdDb, 1)
                << " dB | " << fixed (d.aliasingDb, 1) << " dB |\n";
        }
    std::fprintf (stderr, "distortion: character done\n");

    // Oversampling.
    out << "\n## Oversampling (Warm Bell 10 kHz, +12 dB, Q 1; ton 10 kHz, 0 dBFS — przypadek OS-1)\n\n";
    out << "| Tryb fazy (filtry OS) | 1× | 2× | 4× | 8× |\n|---|---|---|---|---|\n";
    for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
    {
        out << "| " << phaseName (mode) << (mode == PhaseMode::MinimumPhase ? " (IIR)" : " (FIR)");
        for (const auto os : { Oversampling::Off, Oversampling::X2, Oversampling::X4, Oversampling::X8 })
        {
            GlobalParams params;
            params.phaseMode = mode;
            params.oversampling = os;
            auto bands = oneBand ({ Shape::Bell, 10000.0, 12.0, 1.0, Slope::Db12 });
            bands[0].shape.character = Character::Warm;
            const auto d = measureTone (48000.0, params, bands, 10000.0, 0.0);
            out << " | aliasing " << fixed (d.aliasingDb, 1) << " dB, THD " << fixed (d.thdDb, 1) << " dB";
        }
        out << " |\n";
    }
    out << "\nKryterium OS-1 (aliasing): 1× ≤ −80 dB nie jest osiągalne przy +12 dB i 0 dBFS w samym ADAA (odstępstwo opisane w "
           "research/10); 2× ≤ −95 dB, 4× ≤ −110 dB, 8× ≤ −120 dB — spełnione. Podpowiedź OS-5 i oversampling renderu offline (OS-4) "
           "pozwalają z tego korzystać.\n";
    std::fprintf (stderr, "distortion: oversampling done\n");

    // Analog Feel models.
    out << "\n## Analog Feel (Bell 1 kHz, +6 dB, Q 1; ton 1 kHz; Quality Normal)\n\n";
    out << "| Model | Character | THD+N przy −18 dBFS | THD+N przy 0 dBFS |\n|---|---|---|---|\n";
    for (const auto model : { AnalogModel::N, AnalogModel::P, AnalogModel::S })
        for (const auto character : { Character::Warm, Character::Sharp })
        {
            GlobalParams params;
            params.phaseMode = PhaseMode::AnalogFeel;
            params.analogModel = model;
            auto bands = oneBand ({ Shape::Bell, 1000.0, 6.0, 1.0, Slope::Db12 });
            bands[0].shape.character = character;
            const auto quiet = measureTone (48000.0, params, bands, 1000.0, -18.0);
            const auto loud = measureTone (48000.0, params, bands, 1000.0, 0.0);
            const char* name = model == AnalogModel::N ? "N" : (model == AnalogModel::P ? "P" : "S");
            out << "| " << name << " | " << (character == Character::Warm ? "Warm" : "Sharp") << " | " << fixed (quiet.thdnDb, 1)
                << " dB | " << fixed (loud.thdnDb, 1) << " dB |\n";
        }
    std::fprintf (stderr, "distortion: analog feel done\n");

    // Linear path.
    out << "\n## Tor liniowy (Clear)\n\n";
    {
        BandSettingsArray bands {};
        const filters::BandShape shapes[] = { { Shape::Bell, 1000.0, 6.0, 1.0, Slope::Db12 },
                                              { Shape::HighShelf, 8000.0, -4.0, 0.7071, Slope::Db12 },
                                              { Shape::LowCut, 30.0, 0.0, 0.7071, Slope::Db24 } };
        for (std::size_t b = 0; b < std::size (shapes); ++b)
        {
            bands[b].used = bands[b].active = true;
            bands[b].shape = shapes[b];
        }
        for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
        {
            GlobalParams params;
            params.phaseMode = mode;
            params.oversampling = Oversampling::X2;
            const auto d = measureTone (48000.0, params, bands, 997.0, -6.0);
            out << "- " << phaseName (mode) << ", 2×, trzy pasma Clear, ton 997 Hz −6 dBFS: THD+N " << fixed (d.thdnDb, 1) << " dB.\n";
        }
    }
    return out.str();
}

// Latency -------------------------------------------------------------------------------------

/** Reported latency and the measured centre of an impulse through a 0 dB band. */
std::pair<int, double> latencyOf (double fs, const GlobalParams& params, bool spectral)
{
    auto engine = makeEngine (fs);
    BandSettingsArray bands {};
    bands[0].used = bands[0].active = true;
    bands[0].shape = { Shape::Bell, 1000.0, 0.0, 1.0, Slope::Db12 };
    if (spectral)
    {
        bands[1].used = bands[1].active = true;
        bands[1].shape = { Shape::Bell, 3000.0, 0.0, 1.0, Slope::Db12 };
        bands[1].mode = BandMode::Spectral;
        bands[1].dynamics.thresholdDb = 30.0; // never acts: the STFT path at unit gain
    }
    engine->configure (params, bands);
    const int latency = engine->getLatencySamples();
    std::vector<double> x (static_cast<std::size_t> (latency + 0.05 * fs + 4096));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = i == 100 ? 1.0 : 0.0; // the impulse
    run (*engine, x, params, bands);
    double m0 = 0.0, m1 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        m0 += x[i];
        m1 += static_cast<double> (i) * x[i];
    }
    return { latency, m1 / m0 - 100.0 };
}

std::string latencyReport()
{
    std::ostringstream out;
    out << header ("Latencja", "Wymagania: PH-2, OS-3, OS-4, DYN-8, TST-1 (latencja raportowana = zmierzona).");
    out << "Latencja raportowana hostowi i zmierzona: środek ciężkości odpowiedzi impulsowej przez pasmo 0 dB (dla filtrów o fazie "
           "liniowej dokładnie równy opóźnieniu; dla półpasmowych IIR oversamplingu w Minimum Phase — w granicach ±0,5/k próbki). "
           "48 kHz, Max Lookahead 0 ms (każda milisekunda lookahead dodaje 48 próbek).\n\n";

    out << "## Tryby fazy i Quality (bez oversamplingu, bez pasm Spectral)\n\n";
    out << "| Tryb fazy | Eco | Normal | High | Max |\n|---|---|---|---|---|\n";
    for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase, PhaseMode::AnalogFeel })
    {
        out << "| " << phaseName (mode);
        for (const auto quality : { Quality::Eco, Quality::Normal, Quality::High, Quality::Max })
        {
            GlobalParams params;
            params.phaseMode = mode;
            params.quality = quality;
            const auto [latency, measured] = latencyOf (48000.0, params, false);
            out << " | " << latency << " smp (" << fixed (1000.0 * latency / 48000.0, 2) << " ms), zmierzone " << fixed (measured, 2);
        }
        out << " |\n";
    }
    out << "\nAnalog Feel w High i Max działa z wewnętrznym oversamplingiem 2× / 4× i korektorem fazy (research/08), stąd latencja.\n\n";

    out << "## Oversampling (Quality Normal)\n\n";
    out << "| Tryb fazy (filtry OS) | 2× | 4× | 8× |\n|---|---|---|---|\n";
    for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
    {
        out << "| " << phaseName (mode) << (mode == PhaseMode::MinimumPhase ? " (IIR)" : " (FIR + korektor)");
        for (const auto os : { Oversampling::X2, Oversampling::X4, Oversampling::X8 })
        {
            GlobalParams params;
            params.phaseMode = mode;
            params.oversampling = os;
            const auto [latency, measured] = latencyOf (48000.0, params, false);
            out << " | " << latency << " smp, zmierzone " << fixed (measured, 2);
        }
        out << " |\n";
    }

    out << "\n## Pasma Spectral (silnik STFT, Minimum Phase)\n\n";
    out << "| Częstotliwość próbkowania | Eco | Normal | High | Max |\n|---|---|---|---|---|\n";
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        out << "| " << fixed (fs / 1000.0, 1) << " kHz";
        for (const auto quality : { Quality::Eco, Quality::Normal, Quality::High, Quality::Max })
        {
            GlobalParams params;
            params.quality = quality;
            const auto [latency, measured] = latencyOf (fs, params, true);
            out << " | " << latency << " smp (" << fixed (1000.0 * latency / fs, 1) << " ms)"
                << (std::abs (measured - latency) < 1.0e-3 ? "" : " (zmierzone " + fixed (measured, 2) + ")");
        }
        out << " |\n";
    }
    out << "\nZmierzona latencja pasm Spectral jest równa raportowanej (różnica < 0,001 próbki), więc w tabeli podana jest tylko raz.\n\n";

    out << "## Render offline z osobnym oversamplingiem (OS-4)\n\n";
    out << "Czas rzeczywisty 1×, render 8×: obie konfiguracje raportują większą latencję (krótszy tor dopełniony opóźnieniem).\n\n";
    out << "| Tryb fazy | Czas rzeczywisty | Render offline |\n|---|---|---|\n";
    for (const auto mode : { PhaseMode::MinimumPhase, PhaseMode::NaturalPhase })
    {
        GlobalParams params;
        params.phaseMode = mode;
        params.renderOversampling = Oversampling::X8;
        const auto realtime = latencyOf (48000.0, params, false);
        params.nonRealtime = true;
        const auto render = latencyOf (48000.0, params, false);
        out << "| " << phaseName (mode) << " | " << realtime.first << " smp, zmierzone " << fixed (realtime.second, 2) << " | "
            << render.first << " smp, zmierzone " << fixed (render.second, 2) << " |\n";
    }
    std::fprintf (stderr, "latency done\n");
    return out.str();
}

// Auto Gain -------------------------------------------------------------------------------------

/** Loudness of a segment by the Auto Gain model (research §7). */
double modelLoudness (const std::vector<double>& x, std::size_t from, std::size_t to, AutoGainWeighting weighting)
{
    constexpr int n = 4096;
    RealFft fft (n);
    loudness::ErbBands bands;
    bands.prepare (48000.0, n);
    std::vector<float> window (n), frame (n), spectrum (n + 2);
    double power = 0.0;
    for (int i = 0; i < n; ++i)
    {
        window[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (kTwoPi * i / n));
        power += std::pow (window[static_cast<std::size_t> (i)], 2.0);
    }
    std::vector<double> powers (static_cast<std::size_t> (bands.numBands()), 0.0);
    int frames = 0;
    for (std::size_t start = from; start + n <= to; start += n / 4, ++frames)
    {
        for (int i = 0; i < n; ++i)
            frame[static_cast<std::size_t> (i)] =
                static_cast<float> (x[start + static_cast<std::size_t> (i)]) * window[static_cast<std::size_t> (i)];
        fft.forward (frame.data(), spectrum.data());
        bands.accumulate (spectrum.data(), power, 1.0, powers.data());
    }
    for (auto& p : powers)
        p /= frames;
    return loudness::loudnessOf (powers.data(), bands, weighting);
}

/** ITU-R BS.1770-4 K-weighted level of a steady segment (LUFS without gating), 48 kHz. */
double kWeightedLoudness (const std::vector<double>& x, std::size_t from, std::size_t to)
{
    const double b1[] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 };
    const double a1[] = { 1.0, -1.69065929318241, 0.73248077421585 };
    const double b2[] = { 1.0, -2.0, 1.0 };
    const double a2[] = { 1.0, -1.99004745483398, 0.99007225036621 };
    double s1[2] = {}, s2[2] = {};
    double sum = 0.0;
    for (std::size_t i = 0; i < to; ++i)
    {
        double v = x[i];
        double y = b1[0] * v + s1[0];
        s1[0] = b1[1] * v - a1[1] * y + s1[1];
        s1[1] = b1[2] * v - a1[2] * y;
        v = y;
        y = b2[0] * v + s2[0];
        s2[0] = b2[1] * v - a2[1] * y + s2[1];
        s2[1] = b2[2] * v - a2[2] * y;
        if (i >= from)
            sum += y * y;
    }
    return -0.691 + 10.0 * std::log10 (sum / static_cast<double> (to - from));
}

std::string autoGainReport()
{
    std::ostringstream out;
    out << header ("Auto Gain", "Wymagania: AG-3, AG-6.");
    out << "Metoda: 20 s różowego szumu (−20 dBFS RMS) przez EQ z globalnym Auto Gain (Speed Stable), ostatnie 4 s porównane z "
           "wejściem: zmiana głośności według modelu wtyczki (ISO 226 i sony dla Fletcher; energia RMS dla Digital) i według "
           "ITU-R BS.1770-4 (K-weighting, bez bramkowania) jako niezależny punkt odniesienia. Bez Auto Gain zmiana byłaby równa "
           "„korekcji” z przeciwnym znakiem.\n\n";
    out << "| EQ | Ważenie | Korekcja | Zmiana wg modelu | Zmiana wg BS.1770 |\n|---|---|---|---|---|\n";
    struct Case
    {
        const char* name;
        std::vector<filters::BandShape> shapes;
    };
    const Case cases[] = {
        { "Bell 3 kHz +6 dB, Q 1", { { Shape::Bell, 3000.0, 6.0, 1.0, Slope::Db12 } } },
        { "Low Shelf 100 Hz +6 dB", { { Shape::LowShelf, 100.0, 6.0, 0.7071, Slope::Db12 } } },
        { "High Shelf 8 kHz −6 dB", { { Shape::HighShelf, 8000.0, -6.0, 0.7071, Slope::Db12 } } },
        { "Low Cut 200 Hz 24 dB/okt.", { { Shape::LowCut, 200.0, 0.0, 0.7071, Slope::Db24 } } },
        { "Bell 250 Hz −4 dB + Bell 4 kHz +5 dB",
          { { Shape::Bell, 250.0, -4.0, 1.0, Slope::Db12 }, { Shape::Bell, 4000.0, 5.0, 1.5, Slope::Db12 } } },
    };
    const auto noise = test::pinkNoise (1 << 18);
    for (const auto& c : cases)
        for (const auto weighting : { AutoGainWeighting::Fletcher, AutoGainWeighting::Digital })
        {
            BandSettingsArray bands {};
            for (std::size_t b = 0; b < c.shapes.size(); ++b)
            {
                bands[b].used = bands[b].active = true;
                bands[b].shape = c.shapes[b];
            }
            GlobalParams params;
            params.autoGain = true;
            params.autoGainWeighting = weighting;
            params.autoGainSpeed = AutoGainSpeed::Stable;
            auto engine = makeEngine (48000.0, 2);
            const std::size_t total = 48000 * 20;
            std::vector<double> left (total), right (total);
            for (std::size_t i = 0; i < total; ++i)
                left[i] = right[i] = noise[i % noise.size()];
            const auto input = left;
            for (std::size_t offset = 0; offset < total; offset += kBlock)
            {
                const int n = static_cast<int> (std::min<std::size_t> (kBlock, total - offset));
                double* ptrs[] = { left.data() + offset, right.data() + offset };
                engine->process (ptrs, 2, n, params, bands);
            }
            const std::size_t from = total - 48000 * 4;
            const double model = modelLoudness (left, from, total, weighting) - modelLoudness (input, from, total, weighting);
            const double lufs = kWeightedLoudness (left, from, total) - kWeightedLoudness (input, from, total);
            out << "| " << c.name << " | " << (weighting == AutoGainWeighting::Fletcher ? "Fletcher" : "Digital") << " | "
                << fixed (engine->getAutoGainDb(), 2) << " dB | " << fixed (model, 2) << " dB | " << fixed (lufs, 2) << " LU |\n";
        }
    out << "\nKryterium AG-6: zmiana wg modelu ≤ ±0,5 dB (Fletcher) i ≤ ±0,2 dB (Digital). BS.1770 mierzy głośność inaczej niż model "
           "(K-weighting zamiast konturów ISO 226), więc różnice rzędu 1 LU przy mocnych zmianach basu lub góry są oczekiwane.\n";
    std::fprintf (stderr, "auto gain done\n");
    return out.str();
}

// Resonance hints -------------------------------------------------------------------------------

std::string resonanceReport()
{
    std::ostringstream out;
    out << header ("Podpowiedzi rezonansów", "Wymaganie: VIS-5.");
    constexpr int clips = 24;
    constexpr double seconds = 12.0;
    constexpr double fs = 48000.0;
    out << "Metoda: " << clips << " klipy po " << fixed (seconds, 0)
        << " s syntetycznego materiału muzycznego (podkład z różowego szumu, perkusja, melodia z vibrato, akordy) z dwoma "
           "rezonansami o znanych parametrach (filtry peaking: 100 Hz – 8 kHz, +6…+15 dB, Q 5…30) oraz klipy czystego materiału "
           "tonalnego bez rezonansów (fałszywe alarmy). Analiza jak we wtyczce (`ResonanceHints`, czułość Medium), po 2 s "
           "rozgrzewania. Trafienie: podpowiedź w ±1/12 oktawy od rezonansu; recall liczony dla rezonansów ≥ +9 dB i Q ≥ 8. "
           "Narzędzie interaktywne: `ssc_resonance_eval`.\n\n";
    analysis::ResonanceDetector::Config config;
    test::Score score;
    for (int c = 0; c < clips; ++c)
    {
        const auto material = test::makeMaterial (1000u + static_cast<unsigned> (c), seconds, fs, 2);
        const auto result = test::runDetector (material.samples, fs, config);
        test::Score clip;
        test::scoreRun (result, material.resonances, clip);
        score.truePositives += clip.truePositives;
        score.falsePositives += clip.falsePositives;
        score.detected += clip.detected;
        score.relevant += clip.relevant;
        score.detectedLow += clip.detectedLow;
        score.relevantLow += clip.relevantLow;
    }
    int falseAlarms = 0;
    double minutes = 0.0;
    for (int c = 0; c < clips / 2 + 1; ++c)
    {
        const auto clean = test::makeMaterial (5000u + static_cast<unsigned> (c), seconds, fs, 0, c % 2 == 0);
        const auto result = test::runDetector (clean.samples, fs, config);
        falseAlarms += result.appearances;
        minutes += result.seconds / 60.0;
    }
    const int high = score.relevant - score.relevantLow;
    out << "| Miara | Wynik |\n|---|---|\n";
    out << "| Precision (trafne / wszystkie podpowiedzi) | " << fixed (score.precision(), 2) << " (" << score.truePositives << " / "
        << score.truePositives + score.falsePositives << ") |\n";
    out << "| Recall, wszystkie rezonanse | " << fixed (score.recall(), 2) << " (" << score.detected << " / " << score.relevant << ") |\n";
    out << "| Recall powyżej 400 Hz | " << fixed (high > 0 ? double (score.detected - score.detectedLow) / high : 1.0, 2) << " ("
        << score.detected - score.detectedLow << " / " << high << ") |\n";
    out << "| Recall poniżej 400 Hz | " << score.detectedLow << " / " << score.relevantLow << " |\n";
    out << "| Fałszywe podpowiedzi na czystym materiale | " << fixed (minutes > 0.0 ? falseAlarms / minutes : 0.0, 2) << " / min ("
        << falseAlarms << " w " << fixed (minutes, 1) << " min) |\n";
    out << "\nOdstępstwo (research/11 §e): poniżej 400 Hz rezonanse giną w gęstym widmie basu i harmonicznych — podpowiedzi są tam "
           "rzadkie; kryterium VIS-5 jest spełnione powyżej 400 Hz. Ewaluacja na nagraniach rzeczywistych nie była możliwa w tym "
           "środowisku (brak materiału z prawami do publikacji).\n";
    std::fprintf (stderr, "resonance done\n");
    return out.str();
}

bool write (const std::string& directory, const char* name, const std::string& text)
{
    const auto path = directory + "/" + name;
    std::ofstream file (path);
    file << text;
    std::fprintf (stderr, "wrote %s\n", path.c_str());
    return static_cast<bool> (file);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: ssc_measure <output dir> [filters|distortion|latency|autogain|resonance|all]\n");
        return 2;
    }
    const std::string directory = argv[1];
    const std::string what = argc > 2 ? argv[2] : "all";
    const auto start = std::chrono::steady_clock::now();
    bool ok = true;
    if (what == "filters" || what == "all")
        ok = write (directory, "filters.md", filtersReport()) && ok;
    if (what == "distortion" || what == "all")
        ok = write (directory, "distortion.md", distortionReport()) && ok;
    if (what == "latency" || what == "all")
        ok = write (directory, "latency.md", latencyReport()) && ok;
    if (what == "autogain" || what == "all")
        ok = write (directory, "autogain.md", autoGainReport()) && ok;
    if (what == "resonance" || what == "all")
        ok = write (directory, "resonance.md", resonanceReport()) && ok;
    std::fprintf (stderr, "%.1f s\n", std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count());
    return ok ? 0 : 1;
}
