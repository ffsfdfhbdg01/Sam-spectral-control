#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/AnalyzerSettings.h"
#include "plugin/MeterService.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ssc/util/Math.h"
#include "ui/MeterView.h"

using Catch::Approx;
using namespace ssc;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

void setParam (plugin::SscAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getValueTreeState().getParameter (id);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** A Warm bell, +12 dB at 10 kHz (the OS-1 case), as band 1. */
void warmBell (plugin::SscAudioProcessor& p)
{
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Used), 1.0f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Frequency), 10000.0f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Gain), 12.0f);
    setParam (p, plugin::bandParamId (0, plugin::BandParam::Character), static_cast<float> (Character::Warm));
}

/** Plays a stereo sine for `seconds` and returns the meter readings published by then. */
plugin::MeterReadings play (plugin::SscAudioProcessor& p, double hz, double seconds, std::uint64_t& phase)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto blocks = static_cast<int> (seconds * 48000.0 / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto v = static_cast<float> (std::sin (kTwoPi * hz * static_cast<double> (phase++) / 48000.0));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        p.processBlock (buffer, midi);
        std::this_thread::sleep_for (std::chrono::microseconds (500)); // let the meter thread keep up
    }
    // The last capture analysed.
    const auto start = std::chrono::steady_clock::now();
    plugin::MeterReadings readings;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds (300))
    {
        auto& published = p.getMeters().readings();
        if (published.update())
            readings = published.read();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    auto& published = p.getMeters().readings();
    published.update();
    return published.read();
}
} // namespace

TEST_CASE ("Meters read THD+N and aliasing while shown; the suggestion follows oversampling (MTR-1, OS-5)", "[plugin][MTR-1][OS-5]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    warmBell (p);
    p.getMeters().addClient();

    std::uint64_t phase = 0;
    const auto off = play (p, 10000.0, 1.5, phase);
    std::printf ("[MTR-1] Warm +12 dB 10 kHz, OS off: THD+N %.1f dB, aliasing %.1f dB, suggestion %dx\n", off.thdDb, off.aliasingDb,
                 plugin::suggestedOversampling (off));
    REQUIRE (off.valid);
    CHECK (off.nonlinear);
    CHECK (off.bandMeasured[0]);
    CHECK (off.thdDb == Approx (-48.3f).margin (3.0f));      // the core measurement (MTR-3)
    CHECK (off.aliasingDb == Approx (-66.5f).margin (3.0f)); // … and the offline reference
    CHECK (off.bandThdDb[0] == Approx (off.thdDb).margin (0.01f));
    CHECK (plugin::suggestedOversampling (off) == 2);

    // 2×: the 300 ms average forgets the 1× aliasing (−66.5 dB) within about 3 s.
    setParam (p, plugin::id::oversampling, 1.0f);
    const auto x2 = play (p, 10000.0, 4.0, phase);
    std::printf ("[MTR-1] … OS 2x: THD+N %.1f dB, aliasing %.1f dB, suggestion %dx\n", x2.thdDb, x2.aliasingDb,
                 plugin::suggestedOversampling (x2));
    CHECK (x2.osFactor == 2);
    CHECK (x2.aliasingDb < -110.0f);
    CHECK (plugin::suggestedOversampling (x2) == 0);
    p.getMeters().removeClient();
}

TEST_CASE ("OS-5: oversampling is suggested only for an active nonlinearity with aliasing above -80 dB", "[plugin][OS-5]")
{
    plugin::MeterReadings r;
    r.valid = true;
    r.nonlinear = true;
    r.aliasingDb = -70.0f;
    r.osFactor = 1;
    r.maxOsFactor = 8;
    CHECK (plugin::suggestedOversampling (r) == 2);
    r.osFactor = 4;
    CHECK (plugin::suggestedOversampling (r) == 8);
    r.osFactor = 8;
    CHECK (plugin::suggestedOversampling (r) == 0); // already the largest
    r.osFactor = 2;
    r.maxOsFactor = 2; // 192 kHz: 2× is the limit
    CHECK (plugin::suggestedOversampling (r) == 0);
    r.maxOsFactor = 8;
    r.aliasingDb = -85.0f;
    CHECK (plugin::suggestedOversampling (r) == 0); // below the threshold
    r.aliasingDb = -70.0f;
    r.nonlinear = false;
    CHECK (plugin::suggestedOversampling (r) == 0); // no nonlinearity
    r.nonlinear = true;
    r.valid = false;
    CHECK (plugin::suggestedOversampling (r) == 0); // no signal
}

TEST_CASE ("Meters never change the audio (MTR-4)", "[plugin][MTR-4]")
{
    JuceEnvironment env;
    auto render = [] (bool meters) {
        plugin::SscAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        warmBell (p);
        setParam (p, plugin::bandParamId (1, plugin::BandParam::Used), 1.0f);
        setParam (p, plugin::bandParamId (1, plugin::BandParam::Mode), static_cast<float> (BandMode::Dynamic));
        setParam (p, plugin::bandParamId (1, plugin::BandParam::Frequency), 300.0f);
        if (meters)
            p.getMeters().addClient();
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (b * 512 + i) / 48000.0;
                const auto v = static_cast<float> (0.5 * std::sin (kTwoPi * 300.0 * t) + 0.3 * std::sin (kTwoPi * 9000.0 * t));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, -v);
            }
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
        }
        if (meters)
            p.getMeters().removeClient();
        return out;
    };
    REQUIRE (render (true) == render (false));
}

TEST_CASE ("Meter warning thresholds are saved with the analyzer settings; the display floor (MTR-3, MTR-4)", "[plugin][MTR-4]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor a;
    auto s = plugin::readAnalyzerSettings (a.getValueTreeState().state);
    CHECK (s.thdWarningDb == -40.0f);
    CHECK (s.aliasingWarningDb == -80.0f);
    s.thdWarningDb = -60.0f;
    s.aliasingWarningDb = -100.0f;
    plugin::writeAnalyzerSettings (a.getValueTreeState().state, s);
    juce::MemoryBlock data;
    a.getStateInformation (data);
    plugin::SscAudioProcessor b;
    b.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    CHECK (plugin::readAnalyzerSettings (b.getValueTreeState().state) == s);

    CHECK (ui::MeterView::formatDb (-130.0f)
           == juce::String::fromUTF8 ("< \xe2\x88\x92"
                                      "120 dB"));
    CHECK (ui::MeterView::formatDb (-52.26f)
           == juce::String::fromUTF8 ("\xe2\x88\x92"
                                      "52.3 dB"));
    CHECK (ui::MeterView::formatPercent (-52.0f) == "0.25 %");
    CHECK (ui::MeterView::formatPercent (-80.0f) == "0.010 %");
}
