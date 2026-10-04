#pragma once

// Synthetic evaluation material for the resonance hints (research §11c, VIS-5): a music-like
// programme with resonances of known frequency, gain and Q added by peaking filters (ground
// truth), and clean tonal material (negatives). Used by the unit tests and tools/ResonanceEval.

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "TestSignals.h"
#include "ssc/analysis/ResonanceHints.h"

namespace ssc::test
{
struct Resonance
{
    double hz = 1000.0, gainDb = 12.0, q = 10.0;
};

struct Material
{
    std::vector<float> samples;
    std::vector<Resonance> resonances;
};

/** RBJ peaking biquad in double precision, applied in place. */
inline void applyPeaking (std::vector<double>& x, double fs, const Resonance& r)
{
    const double a = std::pow (10.0, r.gainDb / 40.0);
    const double w0 = kTwoPi * r.hz / fs;
    const double alpha = std::sin (w0) / (2.0 * r.q);
    const double b0 = 1.0 + alpha * a, b1 = -2.0 * std::cos (w0), b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha / a;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (auto& v : x)
    {
        const double y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1;
        x1 = v;
        y2 = y1;
        y1 = y;
        v = y;
    }
}

/**
    A music-like programme: pink-noise bed with a slow swell, kick and hi-hat, a melody of harmonic
    notes with vibrato and sustained chords (tonalOnly: melody and decaying chords only, like a
    solo piano). numResonances peaking filters (minResonanceHz … 8 kHz, +6 … +15 dB, Q 5 … 30) are added.
*/
inline Material makeMaterial (unsigned seed, double seconds, double fs, int numResonances, bool tonalOnly = false,
                              double minResonanceHz = 100.0)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> unit (0.0, 1.0);
    const auto length = static_cast<std::size_t> (seconds * fs);
    std::vector<double> x (length, 0.0);

    auto noteHz = [] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); };
    const int scale[] = { 0, 2, 4, 7, 9 }; // pentatonic

    auto addNote = [&] (double startS, double durationS, double f0, double level, int harmonics, double decayS, bool vibrato) {
        const auto start = static_cast<std::size_t> (startS * fs);
        const auto end = std::min (length, static_cast<std::size_t> ((startS + durationS + 0.05) * fs));
        const double tilt = 0.6 + 0.8 * unit (rng);
        double phase = 0.0;
        for (std::size_t i = start; i < end; ++i)
        {
            const double t = static_cast<double> (i - start) / fs;
            const double f = f0 * (vibrato ? 1.0 + 0.003 * std::sin (kTwoPi * 5.0 * t) : 1.0);
            phase += kTwoPi * f / fs;
            double env = std::min (1.0, t / 0.01) * (decayS > 0.0 ? std::exp (-t / decayS) : 1.0);
            if (t > durationS)
                env *= std::max (0.0, 1.0 - (t - durationS) / 0.05);
            double v = 0.0;
            for (int h = 1; h <= harmonics && h * f0 < 0.45 * fs; ++h)
                v += std::sin (h * phase) / std::pow (h, tilt);
            x[i] += level * env * v;
        }
    };

    // Melody.
    for (double t = 0.0; t < seconds;)
    {
        const double duration = 0.2 + 0.6 * unit (rng);
        const int midi = 45 + 12 * static_cast<int> (unit (rng) * 3.0) + scale[static_cast<int> (unit (rng) * 5.0) % 5];
        addNote (t, duration, noteHz (midi), tonalOnly ? 0.08 : 0.05, 10, tonalOnly ? 0.6 : 0.0, ! tonalOnly);
        t += duration;
    }
    // Chords every 2 s.
    for (double t = 0.0; t < seconds; t += 2.0)
        for (int voice = 0; voice < 3; ++voice)
        {
            const int midi = 48 + 12 * static_cast<int> (unit (rng) * 2.0) + scale[static_cast<int> (unit (rng) * 5.0) % 5];
            addNote (t, 1.95, noteHz (midi), 0.025, 8, tonalOnly ? 1.0 : 0.0, false);
        }

    if (! tonalOnly)
    {
        // Pink-noise bed with a slow swell.
        const auto noise = pinkNoise (1 << 20, seed + 17u);
        for (std::size_t i = 0; i < length; ++i)
            x[i] += 0.5 * noise[i % noise.size()] * (0.7 + 0.3 * std::sin (kTwoPi * 0.3 * static_cast<double> (i) / fs));
        // Kick every 0.5 s, hi-hat in between.
        std::normal_distribution<double> gauss (0.0, 1.0);
        for (double t = 0.0; t < seconds; t += 0.5)
        {
            for (std::size_t i = static_cast<std::size_t> (t * fs); i < std::min (length, static_cast<std::size_t> ((t + 0.4) * fs)); ++i)
            {
                const double u = static_cast<double> (i) / fs - t;
                x[i] += 0.4 * std::exp (-u / 0.12) * std::sin (kTwoPi * (50.0 * u + 70.0 * 0.03 * (1.0 - std::exp (-u / 0.03))));
            }
            double previous = 0.0;
            for (std::size_t i = static_cast<std::size_t> ((t + 0.25) * fs);
                 i < std::min (length, static_cast<std::size_t> ((t + 0.3) * fs)); ++i)
            {
                const double u = static_cast<double> (i) / fs - t - 0.25;
                const double n = gauss (rng);
                x[i] += 0.05 * std::exp (-u / 0.015) * (n - previous);
                previous = n;
            }
        }
    }

    Material m;
    std::uniform_real_distribution<double> logHz (std::log (minResonanceHz), std::log (8000.0));
    std::uniform_real_distribution<double> gain (6.0, 15.0);
    std::uniform_real_distribution<double> logQ (std::log (5.0), std::log (30.0));
    for (int r = 0; r < numResonances; ++r)
    {
        Resonance res { std::exp (logHz (rng)), gain (rng), std::exp (logQ (rng)) };
        // Keep resonances apart (≥ 1/2 octave) so that each one can be scored on its own.
        bool apart = true;
        for (const auto& other : m.resonances)
            apart = apart && std::abs (std::log2 (res.hz / other.hz)) >= 0.5;
        if (! apart)
        {
            --r;
            continue;
        }
        m.resonances.push_back (res);
        applyPeaking (x, fs, res);
    }

    double peak = 1.0e-9;
    for (const double v : x)
        peak = std::max (peak, std::abs (v));
    m.samples.resize (length);
    for (std::size_t i = 0; i < length; ++i)
        m.samples[i] = static_cast<float> (0.7 * x[i] / peak);
    return m;
}

/** Hints seen over a run of the detector, grouped by frequency (±1/24 octave). */
struct HintRun
{
    struct Cluster
    {
        double hz = 0.0;
        int frames = 0;
    };
    std::vector<Cluster> clusters; ///< every hint seen after the warm-up
    int appearances = 0;           ///< hidden → visible transitions
    int visibilityChanges = 0;
    double seconds = 0.0;
};

/** Runs the analysis chain of the plugin (analysis::ResonanceHints) over a signal. */
inline HintRun runDetector (const std::vector<float>& signal, double fs, const analysis::ResonanceDetector::Config& config = {},
                            double warmUpSeconds = 2.0)
{
    analysis::ResonanceHints hints;
    hints.prepare (fs, config);

    HintRun run;
    const auto hop = static_cast<std::size_t> (1024.0 * fs / 48000.0);
    for (std::size_t offset = 0; offset + hop <= signal.size(); offset += hop)
    {
        if (! hints.push (signal.data() + offset, static_cast<int> (hop)))
            continue;
        if (static_cast<double> (offset + hop) / fs < warmUpSeconds)
            continue;
        for (const auto& hint : hints.hints())
        {
            auto cluster = std::find_if (run.clusters.begin(), run.clusters.end(),
                                         [&] (const HintRun::Cluster& c) { return std::abs (std::log2 (c.hz / hint.hz)) < 1.0 / 24.0; });
            if (cluster == run.clusters.end())
                run.clusters.push_back ({ hint.hz, 1 });
            else
                ++cluster->frames;
        }
    }
    run.appearances = hints.appearances();
    run.visibilityChanges = hints.visibilityChanges();
    run.seconds = static_cast<double> (signal.size()) / fs;
    return run;
}

/** Precision / recall bookkeeping at ±1/12 octave (research §11c). */
struct Score
{
    int truePositives = 0, falsePositives = 0;
    int detected = 0, relevant = 0;       ///< resonances meeting the criterion (≥ +9 dB, Q ≥ 8) and found
    int detectedLow = 0, relevantLow = 0; ///< of those, below 400 Hz
    double precision() const noexcept
    {
        return truePositives + falsePositives > 0 ? double (truePositives) / (truePositives + falsePositives) : 1.0;
    }
    double recall() const noexcept { return relevant > 0 ? double (detected) / relevant : 1.0; }
};

inline void scoreRun (const HintRun& run, const std::vector<Resonance>& truth, Score& score)
{
    auto matches = [] (double a, double b) { return std::abs (std::log2 (a / b)) <= 1.0 / 12.0; };
    for (const auto& cluster : run.clusters)
    {
        const bool real = std::any_of (truth.begin(), truth.end(), [&] (const Resonance& r) { return matches (cluster.hz, r.hz); });
        (real ? score.truePositives : score.falsePositives)++;
    }
    for (const auto& r : truth)
    {
        if (r.gainDb < 9.0 || r.q < 8.0)
            continue;
        ++score.relevant;
        const bool low = r.hz < 400.0;
        score.relevantLow += low ? 1 : 0;
        if (std::any_of (run.clusters.begin(), run.clusters.end(), [&] (const HintRun::Cluster& c) { return matches (c.hz, r.hz); }))
        {
            ++score.detected;
            score.detectedLow += low ? 1 : 0;
        }
    }
}
} // namespace ssc::test
