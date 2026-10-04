#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "ssc/analysis/SpectrumActivity.h"

using Catch::Approx;
using ssc::analysis::PeakHold;
using ssc::analysis::SpectrumActivity;

namespace
{
constexpr int kPoints = 512;
constexpr double kDt = 1024.0 / 48000.0; // one analyzer hop

void feed (SpectrumActivity& activity, const std::vector<float>& levels, double seconds)
{
    for (double t = 0.0; t < seconds; t += kDt)
        activity.push (levels.data(), kPoints, kDt);
}

float maxIn (const std::vector<float>& v, int from, int to)
{
    return *std::max_element (v.begin() + from, v.begin() + to);
}
} // namespace

TEST_CASE ("Activity: a flat spectrum and silence do not glow; a standing-out region does (VIS-2)", "[analysis][VIS-2]")
{
    SpectrumActivity activity;
    activity.prepare (kPoints, {});

    std::vector<float> flat (kPoints, -30.0f);
    feed (activity, flat, 2.0);
    CHECK (maxIn (activity.activity(), 0, kPoints) < 1.0e-3f);

    // A region 12 dB above the rest glows fully; the rest stays dark.
    auto region = flat;
    std::fill (region.begin() + 200, region.begin() + 221, -18.0f);
    feed (activity, region, 2.0);
    CHECK (activity.activity()[210] > 0.9f);
    CHECK (maxIn (activity.activity(), 0, 190) < 0.01f);
    CHECK (maxIn (activity.activity(), 231, kPoints) < 0.01f);

    // Silence, or a region below the floor, never glows.
    activity.reset();
    std::vector<float> silence (kPoints, -160.0f);
    feed (activity, silence, 1.0);
    CHECK (maxIn (activity.activity(), 0, kPoints) == 0.0f);
    auto quiet = silence;
    std::fill (quiet.begin() + 100, quiet.begin() + 110, -100.0f);
    feed (activity, quiet, 1.0);
    CHECK (maxIn (activity.activity(), 0, kPoints) == 0.0f);
}

TEST_CASE ("Activity: onsets light up before the average catches up, then fade (VIS-2)", "[analysis][VIS-2]")
{
    SpectrumActivity activity;
    activity.prepare (kPoints, {});
    std::vector<float> flat (kPoints, -40.0f);
    feed (activity, flat, 2.0);

    // One frame later the averaged level has barely moved, but the onset shows.
    auto hit = flat;
    std::fill (hit.begin() + 300, hit.begin() + 330, -20.0f);
    activity.push (hit.data(), kPoints, kDt);
    activity.push (hit.data(), kPoints, kDt);
    const float early = activity.activity()[315];
    CHECK (early > 0.3f);

    // Back to flat: the glow fades within about a second.
    feed (activity, flat, 1.5);
    CHECK (activity.activity()[315] < 0.05f);
}

TEST_CASE ("Peak hold keeps the highest level, then falls at the decay rate", "[analysis][VIS-1]")
{
    PeakHold hold;
    hold.prepare (4, 2.0, 6.0);
    const float burst[] = { -10.0f, -20.0f, -30.0f, -40.0f };
    const float quiet[] = { -60.0f, -60.0f, -25.0f, -60.0f };
    hold.push (burst, 4, 0.05);

    double t = 0.0;
    for (; t < 1.9; t += 0.05)
        hold.push (quiet, 4, 0.05);
    CHECK (hold.peaks()[0] == Approx (-10.0f));
    CHECK (hold.peaks()[2] == Approx (-25.0f)); // a higher current level replaces the peak

    for (; t < 2.95; t += 0.05)
        hold.push (quiet, 4, 0.05);
    CHECK (hold.peaks()[0] == Approx (-16.0f).margin (0.35f)); // 1 s of decay at 6 dB/s

    for (int i = 0; i < 400; ++i)
        hold.push (quiet, 4, 0.05);
    CHECK (hold.peaks()[0] == Approx (-60.0f)); // never below the current level
}
