#include "plugin/MeterService.h"

#include <algorithm>
#include <cmath>

namespace ssc::plugin
{
namespace
{
    constexpr double kSilence = 1.0e-10; ///< output mean square below which nothing is measured (−100 dBFS)

    float ratioDb (double numerator, double denominator) noexcept
    {
        const double ratio = denominator > 0.0 ? numerator / denominator : 0.0;
        return static_cast<float> (std::max (-200.0, 10.0 * std::log10 (std::max (ratio, 1.0e-20))));
    }
} // namespace

int suggestedOversampling (const MeterReadings& r, float thresholdDb) noexcept
{
    if (! r.valid || ! r.nonlinear || r.aliasingDb <= thresholdDb || r.osFactor >= r.maxOsFactor)
        return 0;
    return std::min (r.osFactor * 2, r.maxOsFactor);
}

MeterService::MeterService (engine::Engine& e)
    : juce::Thread ("Sam Spectral Control meters"),
      engine (e)
{
}

MeterService::~MeterService()
{
    stopThread (4000);
    engine.setDistortionMetering (false);
}

void MeterService::addClient()
{
    if (clients++ == 0)
    {
        primed = false;
        engine.setDistortionMetering (true);
        startThread (juce::Thread::Priority::low);
    }
}

void MeterService::removeClient()
{
    jassert (clients > 0);
    if (--clients == 0)
    {
        stopThread (4000);
        engine.setDistortionMetering (false);
    }
}

const MeterReadings& MeterService::process (const engine::DistortionCapture& c)
{
    if (! meter.isPreparedFor (c.baseRate, c.osFactor, c.osType, c.osQuality))
        meter.prepare (c.baseRate, c.osFactor, c.osType, c.osQuality);

    // Powers reaching the output, averaged with a 300 ms time constant (research §13c). A busy
    // machine may skip captures: the time since the last one analysed counts.
    const auto periods = primed && c.serial > lastCapture ? static_cast<double> (c.serial - lastCapture) : 1.0;
    const double keep = primed ? std::exp (-periods * c.periodSeconds / kAveragingSeconds) : 0.0;
    lastCapture = c.serial;
    auto average = [keep] (double& state, double value) { state = keep * state + (1.0 - keep) * value; };

    // The shaped bands are analysed in turn, kMaxAnalysesPerCapture at most per capture; the
    // others keep the result of their last analysis (research §13c: the meter thread's budget).
    int analyses = 0;
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto b = static_cast<std::size_t> ((nextBand + i) % kNumBands);
        const auto& band = c.bands[b];
        if (! band.shaped)
        {
            lastProducts[b] = lastAliasing[b] = 0.0;
            continue;
        }
        if (analyses == kMaxAnalysesPerCapture && analysedAt[b] != 0)
            continue;
        const auto d = meter.analyse (band.x.data(), band.kind);
        lastProducts[b] = d.products * band.lanesScale;
        lastAliasing[b] = d.aliasing * band.lanesScale;
        analysedAt[b] = c.serial;
        if (++analyses == kMaxAnalysesPerCapture)
            nextBand = static_cast<int> ((b + 1) % kNumBands);
    }

    current.nonlinear = false;
    double thd = 0.0, aliasing = 0.0;
    for (std::size_t b = 0; b < c.bands.size(); ++b)
    {
        const auto& band = c.bands[b];
        if (! band.shaped)
            analysedAt[b] = 0;
        current.nonlinear = current.nonlinear || band.shaped;
        average (thdPower[b], (band.modulationPower + lastProducts[b]) * c.productsGain);
        average (aliasingPower[b], lastAliasing[b] * c.productsGain);
        thd += thdPower[b];
        aliasing += aliasingPower[b];
        current.bandMeasured[b] = band.shaped || band.modulationPower > 0.0;
    }
    average (outputPower, c.outputPower);
    primed = true;

    current.valid = outputPower > kSilence;
    current.thdDb = ratioDb (thd, outputPower);
    current.aliasingDb = ratioDb (aliasing, outputPower);
    for (std::size_t b = 0; b < c.bands.size(); ++b)
    {
        current.bandThdDb[b] = ratioDb (thdPower[b], outputPower);
        current.bandAliasingDb[b] = ratioDb (aliasingPower[b], outputPower);
    }
    current.osFactor = c.osFactor;
    current.maxOsFactor = engine::maxOversamplingFor (c.baseRate);
    current.serial = ++serial;
    return current;
}

void MeterService::run()
{
    auto& captures = engine.distortionCaptures();
    while (! threadShouldExit())
    {
        if (captures.update())
        {
            published.writeBuffer() = process (captures.read());
            published.publish();
        }
        wait (kPollMs);
    }
}
} // namespace ssc::plugin
