#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/AnalyzerService.h"
#include "plugin/AnalyzerSettings.h"
#include "plugin/PluginProcessor.h"

using Catch::Approx;
using namespace ssc;
using plugin::AnalyzerMode;
using plugin::AnalyzerResolution;
using plugin::AnalyzerSettings;
using plugin::AnalyzerSpeed;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

/** Index of the display point closest to a frequency. */
std::size_t pointNear (const plugin::SpectrumFrame& frame, float hz)
{
    std::size_t best = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t> (frame.numPoints); ++i)
        if (std::abs (std::log (frame.hz[i] / hz)) < std::abs (std::log (frame.hz[best] / hz)))
            best = i;
    return best;
}

/** Waits until the service has published every frame for the data pushed so far. */
plugin::SpectrumFrame settledFrame (plugin::AnalyzerService& service)
{
    plugin::SpectrumFrame frame;
    const auto start = std::chrono::steady_clock::now();
    auto lastNew = start;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds (10))
    {
        if (service.frames().update())
        {
            frame = service.frames().read();
            lastNew = std::chrono::steady_clock::now();
        }
        else if (frame.serial > 0 && std::chrono::steady_clock::now() - lastNew > std::chrono::milliseconds (300))
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return frame;
}
} // namespace

TEST_CASE ("Analyzer settings are saved with the state; invalid values fall back (VIS-1)", "[plugin][VIS-1]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor a;
    REQUIRE (plugin::readAnalyzerSettings (a.getValueTreeState().state) == AnalyzerSettings {});

    AnalyzerSettings s;
    s.mode = AnalyzerMode::Post;
    s.resolution = AnalyzerResolution::High;
    s.speed = AnalyzerSpeed::Slow;
    s.tiltDbPerOctave = 3.0f;
    s.rangeDb = 120.0f;
    s.peakHold = true;
    s.glow = false;
    plugin::writeAnalyzerSettings (a.getValueTreeState().state, s);

    juce::MemoryBlock data;
    a.getStateInformation (data);
    plugin::SscAudioProcessor b;
    b.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    REQUIRE (plugin::readAnalyzerSettings (b.getValueTreeState().state) == s);

    auto tree = b.getValueTreeState().state.getChildWithName (plugin::kAnalyzerType);
    tree.setProperty ("tilt", 2.0, nullptr);
    tree.setProperty ("mode", 17, nullptr);
    const auto fallback = plugin::readAnalyzerSettings (b.getValueTreeState().state);
    REQUIRE (fallback.tiltDbPerOctave == Approx (4.5f));
    REQUIRE (fallback.mode == AnalyzerMode::PrePost);
    REQUIRE (fallback.resolution == AnalyzerResolution::High);

    // FFT lengths keep the frequency resolution at every sample rate.
    REQUIRE (plugin::analyzerFftSize (AnalyzerResolution::Low, 48000.0) == 2048);
    REQUIRE (plugin::analyzerFftSize (AnalyzerResolution::Medium, 44100.0) == 4096);
    REQUIRE (plugin::analyzerFftSize (AnalyzerResolution::Max, 48000.0) == 16384);
    REQUIRE (plugin::analyzerFftSize (AnalyzerResolution::Max, 192000.0) == 65536);
}

TEST_CASE ("Analyzer publishes the main spectrum with its glow and peak hold (VIS-1, VIS-2)", "[plugin][VIS-2]")
{
    JuceEnvironment env;
    plugin::AnalyzerService service;
    service.prepare (48000.0);
    AnalyzerSettings s;
    s.mode = AnalyzerMode::Post;
    s.peakHold = true;
    s.glow = true;
    s.tiltDbPerOctave = 0.0f;
    service.setSettings (s);
    service.addClient();

    const auto tone = test::sine (1000.0, std::pow (10.0, -20.0 / 20.0), 48000.0, 48000);
    service.pushPost (tone.data(), static_cast<int> (tone.size()));
    service.pushPre (tone.data(), static_cast<int> (tone.size()));
    const auto frame = settledFrame (service);
    service.removeClient();

    REQUIRE (frame.serial > 0);
    REQUIRE (frame.hasPost);
    REQUIRE_FALSE (frame.hasPre); // Post mode: the pre analyzer does not run
    REQUIRE (frame.hasPeak);
    REQUIRE (frame.hasActivity);

    const auto k = pointNear (frame, 1000.0f);
    CHECK (frame.postDb[k] == Approx (-20.0f).margin (0.5f));
    CHECK (frame.peakDb[k] == Approx (-20.0f).margin (0.5f));
    CHECK (frame.activity[k] > 0.5f);
    CHECK (frame.activity[pointNear (frame, 100.0f)] < 0.05f);
}
