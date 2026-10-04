// Evaluation of the resonance hints (research §11c, VIS-5) on synthetic material with known
// resonances, and on clean tonal material (false alarms).
// Usage: ssc_resonance_eval [clips] [seconds] [minProminenceDb] [maxWidthErb] [lineDropDb] [unused] [minWidthHz] [minWidthErb]
// [audibleRangeDb] [seedBase] [showScore]

#include <cstdio>
#include <cstdlib>

#include "ResonanceMaterial.h"

int main (int argc, char** argv)
{
    const int clips = argc > 1 ? std::atoi (argv[1]) : 24;
    const double seconds = argc > 2 ? std::atof (argv[2]) : 12.0;
    ssc::analysis::ResonanceDetector::Config config;
    if (argc > 3)
        config.minProminenceDb = std::atof (argv[3]);
    if (argc > 4)
        config.maxWidthErb = std::atof (argv[4]);
    if (argc > 5)
        config.lineDropDb = std::atof (argv[5]);
    if (argc > 7)
        config.minWidthHz = std::atof (argv[7]);
    if (argc > 8)
        config.minWidthErb = std::atof (argv[8]);
    if (argc > 9)
        config.audibleRangeDb = std::atof (argv[9]);
    const unsigned seedBase = argc > 10 ? static_cast<unsigned> (std::atoi (argv[10])) : 1000u;
    if (argc > 11)
        config.showScore = std::atof (argv[11]);
    constexpr double fs = 48000.0;

    ssc::test::Score score;
    for (int c = 0; c < clips; ++c)
    {
        const auto material = ssc::test::makeMaterial (seedBase + static_cast<unsigned> (c), seconds, fs, 2);
        const auto run = ssc::test::runDetector (material.samples, fs, config);
        ssc::test::Score clip;
        ssc::test::scoreRun (run, material.resonances, clip);
        score.truePositives += clip.truePositives;
        score.falsePositives += clip.falsePositives;
        score.detected += clip.detected;
        score.relevant += clip.relevant;
        score.detectedLow += clip.detectedLow;
        score.relevantLow += clip.relevantLow;
        std::printf ("clip %2d:", c);
        for (const auto& r : material.resonances)
            std::printf (" [%6.0f Hz %+5.1f dB Q %4.1f]", r.hz, r.gainDb, r.q);
        std::printf ("  hints:");
        for (const auto& h : run.clusters)
            std::printf (" %.0f(%d)", h.hz, h.frames);
        std::printf ("\n");
    }

    int falseAlarms = 0;
    double minutes = 0.0;
    for (int c = 0; c < clips / 2 + 1; ++c)
    {
        const auto clean = ssc::test::makeMaterial (seedBase + 4000u + static_cast<unsigned> (c), seconds, fs, 0, c % 2 == 0);
        const auto run = ssc::test::runDetector (clean.samples, fs, config);
        falseAlarms += run.appearances;
        minutes += run.seconds / 60.0;
    }

    std::printf ("\nresonances >= +9 dB, Q >= 8: recall %.3f (%d/%d), precision %.3f (%d TP, %d FP)\n", score.recall(), score.detected,
                 score.relevant, score.precision(), score.truePositives, score.falsePositives);
    const int high = score.relevant - score.relevantLow;
    std::printf ("  above 400 Hz: recall %.3f (%d/%d); below: %d/%d\n", high > 0 ? double (score.detected - score.detectedLow) / high : 1.0,
                 score.detected - score.detectedLow, high, score.detectedLow, score.relevantLow);
    std::printf ("clean material: %.2f false hints per minute (%d in %.1f min)\n", falseAlarms / minutes, falseAlarms, minutes);
    return 0;
}
