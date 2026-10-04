#include "plugin/ReadoutService.h"

#include <cmath>
#include <functional>

namespace ssc::plugin
{
namespace
{
    bool sameDynamics (const engine::DynamicSettings& a, const engine::DynamicSettings& b) noexcept
    {
        constexpr std::equal_to<double> same;
        return a.direction == b.direction && same (a.thresholdDb, b.thresholdDb) && same (a.ratio, b.ratio) && same (a.rangeDb, b.rangeDb)
            && same (a.kneeDb, b.kneeDb) && same (a.mix, b.mix);
    }

    bool sameContext (const engine::GlobalParams& a, const engine::GlobalParams& b) noexcept
    {
        return a.phaseMode == b.phaseMode && a.quality == b.quality && a.oversampling == b.oversampling && a.analogModel == b.analogModel;
    }
} // namespace

ReadoutService::ReadoutService (SettingsSource settingsSource)
    : juce::Thread ("Sam Spectral Control readouts"),
      source (std::move (settingsSource))
{
}

ReadoutService::~ReadoutService()
{
    stopThread (4000);
}

void ReadoutService::addClient()
{
    if (clients++ == 0)
    {
        haveLast = false;
        startThread (juce::Thread::Priority::low);
    }
}

void ReadoutService::removeClient()
{
    jassert (clients > 0);
    if (--clients == 0)
        stopThread (4000);
}

bool ReadoutService::changed (int band, const engine::BandSettings& s, float dynamicGainDb) const noexcept
{
    const auto& last = lastBands[static_cast<std::size_t> (band)];
    if (! haveLast || s.used != last.used || s.active != last.active || ! (s.shape == last.shape) || s.mode != last.mode)
        return true;
    if (s.mode == BandMode::Dynamic
        && (! sameDynamics (s.dynamics, last.dynamics) || std::abs (dynamicGainDb - lastGains[static_cast<std::size_t> (band)]) >= 0.5f))
        return true;
    return false;
}

void ReadoutService::run()
{
    engine::GlobalParams params;
    engine::BandSettingsArray bands {};
    std::array<float, kNumBands> gains {};

    while (! threadShouldExit())
    {
        double sampleRate = 48000.0;
        source (params, bands, gains, sampleRate);
        if (sampleRate <= 0.0)
            sampleRate = 48000.0;
        if (std::abs (sampleRate - preparedRate) > 0.5)
        {
            computer.prepare (sampleRate);
            preparedRate = sampleRate;
            haveLast = false;
        }
        if (haveLast && ! sameContext (params, lastParams))
            haveLast = false;

        bool updated = false;
        for (int b = 0; b < kNumBands && ! threadShouldExit(); ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            if (! changed (b, bands[i], gains[i]))
                continue;
            current.bands[i] = bands[i].used ? computer.compute (bands[i], params, gains[i]) : analysis::BandReadout {};
            lastBands[i] = bands[i];
            lastGains[i] = gains[i];
            updated = true;
        }
        lastParams = params;
        haveLast = true;

        if (updated)
        {
            current.serial = ++serial;
            published.writeBuffer() = current;
            published.publish();
        }
        wait (kIntervalMs);
    }
}
} // namespace ssc::plugin
