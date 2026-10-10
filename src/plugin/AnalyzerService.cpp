#include "plugin/AnalyzerService.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace ssc::plugin
{
namespace
{
    constexpr std::size_t kFifoSeconds = 2;
    constexpr double kPeakHoldSeconds = 2.0;
    constexpr double kPeakDecayDbPerSecond = 6.0;

    bool sameAnalysis (const AnalyzerSettings& a, const AnalyzerSettings& b) noexcept
    {
        return a.resolution == b.resolution && a.speed == b.speed && std::equal_to<float>() (a.tiltDbPerOctave, b.tiltDbPerOctave)
            && a.hintSensitivity == b.hintSensitivity;
    }
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
    {
        const std::lock_guard lock (settingsLock);
        activeSettings = pendingSettings;
        settingsChanged.store (false);
    }
    configureAnalyzers (activeSettings);

    if (wasRunning)
        startThread (juce::Thread::Priority::low);
}

void AnalyzerService::setSettings (const AnalyzerSettings& settings)
{
    const std::lock_guard lock (settingsLock);
    if (settings == pendingSettings)
        return;
    pendingSettings = settings;
    settingsChanged.store (true, std::memory_order_release);
}

AnalyzerSettings AnalyzerService::getSettings() const
{
    const std::lock_guard lock (settingsLock);
    return pendingSettings;
}

void AnalyzerService::configureAnalyzers (const AnalyzerSettings& settings)
{
    analysis::SpectrumAnalyzerConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.fftSize = analyzerFftSize (settings.resolution, sampleRate);
    // 75 % overlap, but at least one frame per ≈ 21 ms so that long windows still move smoothly.
    cfg.hopSize = std::min (cfg.fftSize / 4, std::max (256, static_cast<int> (std::lround (1024.0 * sampleRate / 48000.0))));
    cfg.smoothingMs = analyzerAveragingMs (settings.speed);
    cfg.numDisplayPoints = SpectrumFrame::kMaxPoints;
    cfg.minHz = 10.0;
    cfg.maxHz = 30000.0;
    cfg.tiltDbPerOctave = settings.tiltDbPerOctave;

    preAnalyzer.prepare (cfg);
    postAnalyzer.prepare (cfg);
    activity.prepare (SpectrumFrame::kMaxPoints, {});
    peakHold.prepare (SpectrumFrame::kMaxPoints, kPeakHoldSeconds, kPeakDecayDbPerSecond);

    analysis::ResonanceDetector::Config hints;
    hints.minProminenceDb = hintProminenceDb (settings.hintSensitivity);
    resonance.prepare (sampleRate, hints);
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
        if (settingsChanged.exchange (false, std::memory_order_acq_rel))
        {
            AnalyzerSettings next;
            {
                const std::lock_guard lock (settingsLock);
                next = pendingSettings;
            }
            if (! sameAnalysis (next, activeSettings))
                configureAnalyzers (next);
            if (next.glow && ! activeSettings.glow)
                activity.reset();
            if (next.peakHold && ! activeSettings.peakHold)
                peakHold.reset();
            if (next.resonanceHints && ! activeSettings.resonanceHints)
                resonance.reset();
            activeSettings = next;
        }

        // At most one FIFO's worth per pass, so that a producer faster than real time (offline
        // render, validators) cannot keep the thread from seeing a stop request; a full FIFO
        // drops the samples that do not fit (the newest).
        const auto& s = activeSettings;
        int preFrames = 0, postFrames = 0;
        bool hintsUpdated = false;
        const auto limit = preFifo.capacity();
        for (std::size_t done = 0; done < limit && ! threadShouldExit();)
        {
            const auto n = preFifo.read (scratch.data(), scratch.size());
            if (n == 0)
                break;
            done += n;
            if (s.showsPre())
                preFrames += preAnalyzer.push (scratch.data(), static_cast<int> (n));
        }
        for (std::size_t done = 0; done < limit && ! threadShouldExit();)
        {
            const auto n = postFifo.read (scratch.data(), scratch.size());
            if (n == 0)
                break;
            done += n;
            if (s.showsPost())
                postFrames += postAnalyzer.push (scratch.data(), static_cast<int> (n));
            if (s.resonanceHints)
                hintsUpdated = resonance.push (scratch.data(), static_cast<int> (n)) || hintsUpdated;
        }

        if (preFrames + postFrames > 0 || hintsUpdated)
        {
            const int mainFrames = s.mode == AnalyzerMode::Pre ? preFrames : postFrames;
            publishFrame (mainFrames * postAnalyzer.getConfig().hopSize / sampleRate);
        }

        wait (8);
    }
}

void AnalyzerService::publishFrame (double dtSeconds)
{
    const auto& s = activeSettings;
    auto& frame = published.writeBuffer();
    const auto& hz = postAnalyzer.displayFrequencies();
    const auto n = std::min<std::size_t> (hz.size(), static_cast<std::size_t> (SpectrumFrame::kMaxPoints));
    frame.numPoints = static_cast<int> (n);
    std::copy_n (hz.begin(), n, frame.hz.begin());

    frame.hasPre = s.showsPre();
    frame.hasPost = s.showsPost();
    if (frame.hasPre)
        std::copy_n (preAnalyzer.displayDb().begin(), n, frame.preDb.begin());
    if (frame.hasPost)
        std::copy_n (postAnalyzer.displayDb().begin(), n, frame.postDb.begin());

    // Glow and peak hold follow the main spectrum: post, or pre when only pre is shown.
    const auto& main = s.mode == AnalyzerMode::Pre ? preAnalyzer : postAnalyzer;
    if (dtSeconds > 0.0)
    {
        if (s.glow)
            activity.push (main.displayInstantDb().data(), static_cast<int> (n), dtSeconds);
        if (s.peakHold)
            peakHold.push (main.displayDb().data(), static_cast<int> (n), dtSeconds);
    }
    frame.hasActivity = s.glow && (frame.hasPre || frame.hasPost);
    frame.hasPeak = s.peakHold && (frame.hasPre || frame.hasPost);
    if (frame.hasActivity)
        std::copy_n (activity.activity().begin(), n, frame.activity.begin());
    if (frame.hasPeak)
        std::copy_n (peakHold.peaks().begin(), n, frame.peakDb.begin());

    frame.hasHints = s.resonanceHints;
    frame.numHints = 0;
    if (s.resonanceHints)
        for (const auto& hint : resonance.hints())
            if (frame.numHints < SpectrumFrame::kMaxHints)
                frame.hints[static_cast<std::size_t> (frame.numHints++)] = hint;

    frame.serial = ++serial;
    published.publish();
}
} // namespace ssc::plugin
