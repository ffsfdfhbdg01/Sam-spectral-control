#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
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
    s.phaseOverlay = plugin::PhaseOverlay::GroupDelay;
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

TEST_CASE ("Analyzer publishes resonance hints of the output (VIS-4, VIS-5)", "[plugin][VIS-4]")
{
    JuceEnvironment env;
    plugin::AnalyzerService service;
    service.prepare (48000.0);
    AnalyzerSettings s;
    s.mode = AnalyzerMode::Post;
    service.setSettings (s);
    service.addClient();

    // Pink noise through a +12 dB, Q 10 resonance at 2 kHz, fed in real-time-sized chunks.
    const auto noise = test::pinkNoise (1 << 19, 21);
    std::vector<double> x (noise.begin(), noise.end());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    {
        const double a = std::pow (10.0, 12.0 / 40.0), w0 = 2.0 * juce::MathConstants<double>::pi * 2000.0 / 48000.0;
        const double alpha = std::sin (w0) / 20.0;
        const double b0 = 1.0 + alpha * a, b1 = -2.0 * std::cos (w0), b2 = 1.0 - alpha * a;
        const double a0 = 1.0 + alpha / a, a1 = b1, a2 = 1.0 - alpha / a;
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
    std::vector<float> signal (x.size());
    std::transform (x.begin(), x.end(), signal.begin(), [] (double v) { return static_cast<float> (v); });
    for (std::size_t offset = 0; offset + 4096 <= 6 * 48000; offset += 4096)
    {
        service.pushPost (signal.data() + offset, 4096);
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    const auto frame = settledFrame (service);
    service.removeClient();

    REQUIRE (frame.hasHints);
    REQUIRE (frame.numHints == 1);
    CHECK (frame.hints[0].hz == Approx (2000.0f).epsilon (0.03f));
}

TEST_CASE ("Resonance hints never change the audio (VIS-4)", "[plugin][VIS-4]")
{
    JuceEnvironment env;
    auto render = [] (bool hints) {
        plugin::SscAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        AnalyzerSettings s;
        s.resonanceHints = hints;
        p.getAnalyzer().setSettings (s);
        p.getAnalyzer().addClient(); // the analysis thread runs as with an open editor

        auto* gain = p.getValueTreeState().getParameter ("b01_gain");
        auto* used = p.getValueTreeState().getParameter ("b01_used");
        used->setValueNotifyingHost (1.0f);
        gain->setValueNotifyingHost (gain->convertTo0to1 (9.0f));

        const auto noise = test::pinkNoise (1 << 17, 4);
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int offset = 0; offset + 512 <= (1 << 17); offset += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, noise.data() + offset, 512);
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
        }
        p.getAnalyzer().removeClient();
        return out;
    };
    REQUIRE (render (true) == render (false));
}

TEST_CASE ("Band readouts follow parameter changes within 100 ms (PR-2)", "[plugin][PR-2]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto* used = p.getValueTreeState().getParameter ("b01_used");
    auto* gain = p.getValueTreeState().getParameter ("b01_gain");
    used->setValueNotifyingHost (1.0f);
    gain->setValueNotifyingHost (gain->convertTo0to1 (6.0f));

    auto& readouts = p.getReadouts();
    readouts.addClient();
    auto waitFor = [&readouts] (auto&& condition) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds (5))
        {
            auto& buffer = readouts.readouts();
            buffer.update();
            if (condition (buffer.read().bands[0]))
                return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return 1.0e9;
    };
    REQUIRE (waitFor ([] (const analysis::BandReadout& r) { return r.valid; }) < 5000.0);

    std::vector<double> delays;
    float previous = 0.0f;
    {
        auto& buffer = readouts.readouts();
        buffer.update();
        previous = buffer.read().bands[0].meanPhaseDeg;
    }
    for (const float db : { 12.0f, 3.0f, 9.0f, -6.0f, 15.0f })
    {
        gain->setValueNotifyingHost (gain->convertTo0to1 (db));
        const float before = previous;
        delays.push_back (waitFor ([before, &previous] (const analysis::BandReadout& r) {
            if (std::abs (r.meanPhaseDeg - before) < 1.0e-3f)
                return false;
            previous = r.meanPhaseDeg;
            return true;
        }));
    }
    readouts.removeClient();
    std::sort (delays.begin(), delays.end());
    std::printf ("[PR-2] readout delay after a change: median %.1f ms, max %.1f ms\n", delays[2], delays.back());
    CHECK (delays[2] <= 100.0);
}

TEST_CASE ("The readout thread measures the whole EQ while the phase overlay asks for it (PR-4)", "[plugin][PR-4]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    auto& state = p.getValueTreeState();
    auto set = [&state] (const char* paramId, float value) {
        auto* param = state.getParameter (paramId);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set ("b01_used", 1.0f);
    set ("b01_freq", 1000.0f);
    set ("b01_gain", 6.0f);

    auto& readouts = p.getReadouts();
    readouts.addClient();
    auto latest = [&readouts] {
        auto& buffer = readouts.readouts();
        buffer.update();
        return buffer.read();
    };
    auto phaseAt = [] (const analysis::ChainResponse& chain, float hz) {
        std::size_t best = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t> (chain.numPoints); ++i)
            if (std::abs (std::log (chain.hz[i] / hz)) < std::abs (std::log (chain.hz[best] / hz)))
                best = i;
        return chain.phaseDeg[best];
    };
    auto waitFor = [&] (auto&& condition) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds (5))
        {
            if (condition (latest()))
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
        return false;
    };

    // Without the overlay only the band readouts are measured.
    REQUIRE (waitFor ([] (const plugin::ReadoutSnapshot& s) { return s.bands[0].valid; }));
    std::this_thread::sleep_for (std::chrono::milliseconds (150));
    CHECK_FALSE (latest().chain.valid);

    // With it: a boost turns the phase up below the centre and down above it.
    readouts.setChainWanted (true);
    REQUIRE (waitFor ([] (const plugin::ReadoutSnapshot& s) { return s.chain.valid; }));
    CHECK (phaseAt (latest().chain, 500.0f) > 5.0f);
    CHECK (phaseAt (latest().chain, 2000.0f) < -5.0f);

    // It follows the settings: a cut turns it the other way; on Side the band leaves a centred
    // source's left channel alone.
    set ("b01_gain", -6.0f);
    CHECK (waitFor ([&] (const plugin::ReadoutSnapshot& s) { return s.chain.valid && phaseAt (s.chain, 500.0f) < -5.0f; }));
    set ("b01_target", static_cast<float> (StereoComponent::Side));
    CHECK (waitFor ([&] (const plugin::ReadoutSnapshot& s) { return s.chain.valid && std::abs (phaseAt (s.chain, 500.0f)) < 0.01f; }));
    readouts.setChainWanted (false);
    readouts.removeClient();
}

TEST_CASE ("Analyzer thread stops promptly while audio arrives faster than real time", "[plugin][ARC-4]")
{
    JuceEnvironment env;
    plugin::AnalyzerService service;
    service.prepare (48000.0);
    AnalyzerSettings s;
    s.resolution = AnalyzerResolution::Max; // the heaviest analysis, with resonance hints
    service.setSettings (s);

    // An offline render or a validator: the producer never waits.
    std::atomic<bool> producing { true };
    std::thread producer ([&] {
        const auto noise = test::pinkNoise (1 << 16, 3);
        std::size_t offset = 0;
        while (producing)
        {
            service.pushPre (noise.data() + offset, 1024);
            service.pushPost (noise.data() + offset, 1024);
            offset = (offset + 1024) % (noise.size() - 1024);
        }
    });

    double worst = 0.0;
    for (int round = 0; round < 5; ++round)
    {
        service.addClient();
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
        const auto start = std::chrono::steady_clock::now();
        service.removeClient(); // stopThread: must not time out (JUCE would kill the thread)
        worst = std::max (worst, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count());
    }
    producing = false;
    producer.join();
    std::printf ("[ARC-4] analyzer stop with a flooding producer: worst %.1f ms\n", worst);
    CHECK (worst < 1000.0);
}
