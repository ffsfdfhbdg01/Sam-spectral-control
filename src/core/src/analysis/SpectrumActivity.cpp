#include "ssc/analysis/SpectrumActivity.h"

#include <algorithm>
#include <cmath>

namespace ssc::analysis
{
namespace
{
    constexpr float kFloorDb = -160.0f;

    float smoothingFactor (double dtSeconds, double timeMs) noexcept
    {
        return timeMs > 0.0 ? static_cast<float> (std::exp (-dtSeconds / (0.001 * timeMs))) : 0.0f;
    }
} // namespace

void SpectrumActivity::prepare (int numPoints, const Config& config)
{
    cfg = config;
    const auto n = static_cast<std::size_t> (std::max (0, numPoints));
    power.assign (n, 0.0f);
    previousDb.assign (n, kFloorDb);
    flux.assign (n, 0.0f);
    values.assign (n, 0.0f);
    primed = false;
}

void SpectrumActivity::reset() noexcept
{
    std::fill (power.begin(), power.end(), 0.0f);
    std::fill (previousDb.begin(), previousDb.end(), kFloorDb);
    std::fill (flux.begin(), flux.end(), 0.0f);
    std::fill (values.begin(), values.end(), 0.0f);
    primed = false;
}

void SpectrumActivity::push (const float* levelDb, int numPoints, double dtSeconds) noexcept
{
    const auto n = std::min (static_cast<std::size_t> (std::max (0, numPoints)), values.size());
    if (n == 0)
        return;

    const float a = primed ? smoothingFactor (dtSeconds, cfg.averagingMs) : 0.0f;
    const float af = smoothingFactor (dtSeconds, cfg.fluxMs);

    // Averaged level and onsets per point; mean level of the audible points.
    double sum = 0.0;
    int audible = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float db = std::max (kFloorDb, levelDb[i]);
        const float p = std::pow (10.0f, 0.1f * db);
        power[i] = a * power[i] + (1.0f - a) * p;

        const float rise = primed ? std::max (0.0f, db - std::max (previousDb[i], static_cast<float> (cfg.floorDb))) : 0.0f;
        flux[i] = af * flux[i] + (1.0f - af) * rise;
        previousDb[i] = db;

        const float averagedDb = 10.0f * std::log10 (power[i] + 1.0e-30f);
        if (averagedDb > static_cast<float> (cfg.floorDb))
        {
            sum += averagedDb;
            ++audible;
        }
    }
    primed = true;

    const auto mean = audible > 0 ? static_cast<float> (sum / audible) : 0.0f;
    const auto span = static_cast<float> (std::max (1.0e-3, cfg.spanDb));
    const auto weight = static_cast<float> (cfg.fluxWeight);
    const auto floor = static_cast<float> (cfg.floorDb);
    const auto fade = static_cast<float> (std::max (1.0e-3, cfg.fadeDb));
    for (std::size_t i = 0; i < n; ++i)
    {
        const float averagedDb = 10.0f * std::log10 (power[i] + 1.0e-30f);
        const float gate = std::clamp ((averagedDb - floor) / fade, 0.0f, 1.0f);
        const float density = averagedDb - mean;
        values[i] = gate * std::clamp ((density + weight * flux[i]) / span, 0.0f, 1.0f);
    }
}

void PeakHold::prepare (int numPoints, double holdSeconds, double decayDbPerSecond)
{
    const auto n = static_cast<std::size_t> (std::max (0, numPoints));
    peakDb.assign (n, kFloorDb);
    holdLeft.assign (n, 0.0f);
    hold = std::max (0.0, holdSeconds);
    decay = std::max (0.0, decayDbPerSecond);
    primed = false;
}

void PeakHold::reset() noexcept
{
    std::fill (peakDb.begin(), peakDb.end(), kFloorDb);
    std::fill (holdLeft.begin(), holdLeft.end(), 0.0f);
    primed = false;
}

void PeakHold::push (const float* levelDb, int numPoints, double dtSeconds) noexcept
{
    const auto n = std::min (static_cast<std::size_t> (std::max (0, numPoints)), peakDb.size());
    const auto dt = static_cast<float> (std::max (0.0, dtSeconds));
    for (std::size_t i = 0; i < n; ++i)
    {
        const float level = levelDb[i];
        if (! primed || level >= peakDb[i])
        {
            peakDb[i] = level;
            holdLeft[i] = static_cast<float> (hold);
            continue;
        }
        float fall = dt;
        if (holdLeft[i] > 0.0f)
        {
            const float held = std::min (holdLeft[i], dt);
            holdLeft[i] -= held;
            fall -= held;
        }
        peakDb[i] = std::max (level, peakDb[i] - static_cast<float> (decay) * fall);
    }
    primed = true;
}
} // namespace ssc::analysis
