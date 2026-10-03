#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "TestSignals.h"
#include "ssc/analysis/SpectrumAnalyzer.h"

using Catch::Approx;

namespace
{
float peakLevelNear (const ssc::analysis::SpectrumAnalyzer& a, double hz, double toleranceRatio, double* peakHz = nullptr)
{
    const auto& f = a.displayFrequencies();
    const auto& db = a.displayDb();
    float best = -1000.0f;
    for (std::size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] >= hz / toleranceRatio && f[i] <= hz * toleranceRatio && db[i] > best)
        {
            best = db[i];
            if (peakHz != nullptr)
                *peakHz = f[i];
        }
    }
    return best;
}
} // namespace

TEST_CASE ("Analyzer shows a sine at its true level, on and between bins (VIS-1)", "[analysis][VIS-1]")
{
    const double frequency = GENERATE (1000.0, 1003.7, 7321.9, 120.4);
    constexpr double sampleRate = 48000.0;

    ssc::analysis::SpectrumAnalyzerConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.fftSize = 4096;
    cfg.hopSize = 1024;
    cfg.smoothingMs = 0.0;
    ssc::analysis::SpectrumAnalyzer analyzer;
    analyzer.prepare (cfg);

    const auto signal = ssc::test::sine (frequency, std::pow (10.0, -20.0 / 20.0), sampleRate, 48000);
    REQUIRE (analyzer.push (signal.data(), static_cast<int> (signal.size())));

    double peakHz = 0.0;
    const float level = peakLevelNear (analyzer, frequency, 1.05, &peakHz);
    REQUIRE (level == Approx (-20.0).margin (0.5));
    REQUIRE (peakHz == Approx (frequency).epsilon (0.02));
}

TEST_CASE ("Analyzer shows pink noise flat with a 3 dB/oct tilt (VIS-1)", "[analysis][VIS-1]")
{
    constexpr double sampleRate = 48000.0;
    ssc::analysis::SpectrumAnalyzerConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.fftSize = 8192;
    cfg.hopSize = 2048;
    cfg.smoothingMs = 4000.0;
    cfg.tiltDbPerOctave = 10.0 * std::log10 (2.0);
    ssc::analysis::SpectrumAnalyzer analyzer;
    analyzer.prepare (cfg);

    const auto noise = ssc::test::pinkNoise (1 << 20);
    analyzer.push (noise.data(), static_cast<int> (noise.size()));

    // Third-octave averages of the display curve between 100 Hz and 12.5 kHz.
    const auto& f = analyzer.displayFrequencies();
    const auto& db = analyzer.displayDb();
    std::vector<double> bandMeans;
    for (double centre = 100.0; centre <= 12500.0; centre *= std::pow (2.0, 1.0 / 3.0))
    {
        double sum = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < f.size(); ++i)
        {
            if (f[i] >= centre / std::pow (2.0, 1.0 / 6.0) && f[i] < centre * std::pow (2.0, 1.0 / 6.0))
            {
                sum += db[i];
                ++count;
            }
        }
        REQUIRE (count > 0);
        bandMeans.push_back (sum / count);
    }

    const auto [lo, hi] = std::minmax_element (bandMeans.begin(), bandMeans.end());
    REQUIRE (*hi - *lo <= 2.0); // flat within ±1 dB
}

TEST_CASE ("Analyzer reports the floor for silence", "[analysis]")
{
    ssc::analysis::SpectrumAnalyzerConfig cfg;
    cfg.fftSize = 2048;
    cfg.hopSize = 512;
    ssc::analysis::SpectrumAnalyzer analyzer;
    analyzer.prepare (cfg);

    std::vector<float> silence (8192, 0.0f);
    REQUIRE (analyzer.push (silence.data(), static_cast<int> (silence.size())));
    for (auto v : analyzer.displayDb())
        REQUIRE (v <= -150.0f);
}

TEST_CASE ("Analyzer needs a full window before the first frame", "[analysis]")
{
    ssc::analysis::SpectrumAnalyzerConfig cfg;
    cfg.fftSize = 4096;
    cfg.hopSize = 1024;
    ssc::analysis::SpectrumAnalyzer analyzer;
    analyzer.prepare (cfg);

    std::vector<float> block (1024, 0.1f);
    REQUIRE_FALSE (analyzer.push (block.data(), 1024));
    REQUIRE_FALSE (analyzer.push (block.data(), 1024));
    REQUIRE_FALSE (analyzer.push (block.data(), 1024));
    REQUIRE (analyzer.push (block.data(), 1024));
}
