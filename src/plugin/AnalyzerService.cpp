#include "plugin/AnalyzerService.h"

#include <algorithm>
#include <cmath>

namespace ssc::plugin
{
namespace
{
    constexpr std::size_t kFifoSeconds = 2;
} // namespace

AnalyzerService::AnalyzerService()
    : juce::Thread ("Sam Spectral Control analyzer")
{
    prepare (sampleRate);
}

AnalyzerService::~AnalyzerService()
{
    stopThread (2000);
}

void AnalyzerService::prepare (double newSampleRate)
{
    const bool wasRunning = isThreadRunning();
    if (wasRunning)
        stopThread (2000);

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const auto capacity = static_cast<std::size_t> (sampleRate) * kFifoSeconds;
    preFifo.allocate (capacity);
    postFifo.allocate (capacity);
    configureAnalyzers();

    if (wasRunning)
        startThread (juce::Thread::Priority::low);
}

void AnalyzerService::configureAnalyzers()
{
    analysis::SpectrumAnalyzerConfig cfg;
    cfg.sampleRate = sampleRate;
    // ≈ 4096 points at 48 kHz, scaled with the sample rate to keep the frequency resolution.
    cfg.fftSize = std::clamp (nextPowerOfTwo (static_cast<int> (std::lround (4096.0 * sampleRate / 48000.0))), 1024, 65536);
    cfg.hopSize = cfg.fftSize / 4;
    cfg.smoothingMs = 250.0;
    cfg.numDisplayPoints = SpectrumFrame::kMaxPoints;
    cfg.minHz = 10.0;
    cfg.maxHz = 30000.0;
    cfg.tiltDbPerOctave = 4.5;

    preAnalyzer.prepare (cfg);
    postAnalyzer.prepare (cfg);
}

void AnalyzerService::addClient()
{
    if (clients++ == 0)
    {
        preFifo.discardAll();
        postFifo.discardAll();
        startThread (juce::Thread::Priority::low);
    }
}

void AnalyzerService::removeClient()
{
    jassert (clients > 0);
    if (--clients == 0)
        stopThread (2000);
}

void AnalyzerService::run()
{
    while (! threadShouldExit())
    {
        bool updated = false;

        for (;;)
        {
            const auto n = preFifo.read (scratch.data(), scratch.size());
            if (n == 0)
                break;
            updated = preAnalyzer.push (scratch.data(), static_cast<int> (n)) || updated;
        }

        for (;;)
        {
            const auto n = postFifo.read (scratch.data(), scratch.size());
            if (n == 0)
                break;
            updated = postAnalyzer.push (scratch.data(), static_cast<int> (n)) || updated;
        }

        if (updated)
            publishFrame();

        wait (8);
    }
}

void AnalyzerService::publishFrame()
{
    auto& frame = published.writeBuffer();
    const auto& hz = postAnalyzer.displayFrequencies();
    const auto& pre = preAnalyzer.displayDb();
    const auto& post = postAnalyzer.displayDb();

    const auto n = std::min<std::size_t> ({ hz.size(), pre.size(), post.size(), static_cast<std::size_t> (SpectrumFrame::kMaxPoints) });
    frame.numPoints = static_cast<int> (n);
    std::copy_n (hz.begin(), n, frame.hz.begin());
    std::copy_n (pre.begin(), n, frame.preDb.begin());
    std::copy_n (post.begin(), n, frame.postDb.begin());
    frame.serial = ++serial;
    published.publish();
}
} // namespace ssc::plugin
