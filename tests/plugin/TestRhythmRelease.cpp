#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ssc/dynamics/NoteValues.h"
#include "ui/BandModel.h"
#include "ui/BandPanel.h"

// Rhythm mode of the Release (DYN-6): the Release of a Dynamic or Spectral band lasts a note value at
// the tempo the host reports through its play head.

using Catch::Approx;
using namespace ssc;
using plugin::BandParam;
using plugin::bandParamId;
using plugin::SscAudioProcessor;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

constexpr double kFs = 48000.0;
constexpr int kBlock = 480;

/** A host transport at a fixed tempo (or none). */
struct TempoPlayHead final : juce::AudioPlayHead
{
    std::optional<double> bpm;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        if (bpm.has_value())
            info.setBpm (*bpm);
        return info;
    }
};

void setPlain (SscAudioProcessor& p, const juce::String& paramId, float plain)
{
    auto* param = p.getValueTreeState().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

int noteIndex (const char* label)
{
    for (std::size_t i = 0; i < dynamics::kNoteValues.size(); ++i)
        if (juce::String (dynamics::kNoteValues[i].label) == label)
            return static_cast<int> (i);
    return -1;
}

/**
    A Dynamic bell at 1 kHz takes 12 dB off a loud 1 kHz tone; the tone then drops 40 dB under the
    threshold and the band lets go with its Release. Returns how much is still taken off 200 ms
    after the drop, in dB (negative).
*/
double heldAfterDrop (SscAudioProcessor& p, TempoPlayHead& host)
{
    auto set = [&p] (BandParam param, float value) { setPlain (p, bandParamId (0, param), value); };
    set (BandParam::Used, 1.0f);
    set (BandParam::Shape, static_cast<float> (Shape::Bell));
    set (BandParam::Frequency, 1000.0f);
    set (BandParam::Q, 1.0f);
    set (BandParam::Mode, static_cast<float> (BandMode::Dynamic));
    set (BandParam::Threshold, -40.0f);
    set (BandParam::Ratio, 100.0f);
    set (BandParam::Range, 12.0f);
    set (BandParam::Knee, 0.0f);
    set (BandParam::Attack, 1.0f);

    p.setPlayHead (&host);
    p.setRateAndBufferSizeDetails (kFs, kBlock);
    p.prepareToPlay (kFs, kBlock);

    const int loud = 48000, quiet = 24000;
    const auto tone = test::sine (1000.0, 1.0, kFs, loud + quiet);
    std::vector<double> in (tone.size()), out (tone.size());
    for (std::size_t i = 0; i < tone.size(); ++i)
        in[i] = tone[i] * (static_cast<int> (i) < loud ? 0.2 : 0.002);

    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < loud + quiet; offset += kBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample (ch, i, static_cast<float> (in[static_cast<std::size_t> (offset + i)]));
        p.processBlock (buffer, midi);
        for (int i = 0; i < kBlock; ++i)
            out[static_cast<std::size_t> (offset + i)] = static_cast<double> (buffer.getSample (0, i));
    }
    p.setPlayHead (nullptr);

    // Level of the quiet tone over 20 ms, 200 ms after the drop (single-bin DFT, Hann window).
    const std::size_t from = static_cast<std::size_t> (loud + 9600), length = 960;
    std::complex<double> a = 0.0, b = 0.0;
    double windowSum = 0.0;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length));
        const auto phasor = std::polar (1.0, -kTwoPi * 1000.0 * static_cast<double> (from + i) / kFs);
        a += w * out[from + i] * phasor;
        b += w * in[from + i] * phasor;
        windowSum += w;
    }
    return gainToDb (std::abs (a) / std::abs (b));
}
} // namespace

TEST_CASE ("Rhythm mode: the host's tempo from the play head sets the Release of a band (DYN-6)", "[plugin][DYN-6]")
{
    JuceEnvironment env;

    // Without a play head (or a tempo from it) the processor assumes 120 BPM.
    {
        SscAudioProcessor p;
        CHECK (p.getHostBpm() == Approx (120.0));
        TempoPlayHead none;
        heldAfterDrop (p, none);
        CHECK (p.getHostBpm() == Approx (120.0));
    }

    // A quarter note: 1000 ms at 60 BPM, 250 ms at 240 BPM.
    auto run = [] (double bpm, bool sync) {
        SscAudioProcessor p;
        setPlain (p, bandParamId (0, BandParam::ReleaseSync), sync ? 1.0f : 0.0f);
        setPlain (p, bandParamId (0, BandParam::ReleaseNote), static_cast<float> (noteIndex ("1/4")));
        setPlain (p, bandParamId (0, BandParam::Release), 100.0f);
        TempoPlayHead host;
        host.bpm = bpm;
        const double held = heldAfterDrop (p, host);
        CHECK (p.getHostBpm() == Approx (bpm));
        return held;
    };
    // The release is a time constant: 200 ms after the drop, 12·e^(−200/T) dB are still taken off.
    auto expected = [] (double releaseMs) { return -12.0 * std::exp (-200.0 / releaseMs); };
    const double slow = run (60.0, true), fast = run (240.0, true);
    CAPTURE (slow, fast);
    CHECK (slow == Approx (expected (1000.0)).margin (0.5));
    CHECK (fast == Approx (expected (250.0)).margin (0.5));

    // A release of 1000 ms set in milliseconds behaves like the quarter note at 60 BPM.
    {
        SscAudioProcessor p;
        setPlain (p, bandParamId (0, BandParam::Release), 1000.0f);
        TempoPlayHead host;
        host.bpm = 240.0;
        CHECK (heldAfterDrop (p, host) == Approx (slow).margin (0.3));
    }

    // Off: the Release knob (100 ms) counts, whatever the tempo.
    const double offSlow = run (60.0, false), offFast = run (240.0, false);
    CHECK (offSlow == Approx (offFast).margin (0.05));
    CHECK (offSlow == Approx (expected (100.0)).margin (0.5));
}

TEST_CASE ("Rhythm mode in the band panel: a note switch beside Release, note knob, tempo in the tooltip (DYN-6)", "[ui][DYN-6]")
{
    JuceEnvironment env;
    SscAudioProcessor processor;
    ui::BandModel model (processor.getValueTreeState(), processor.getUndoManager());
    ui::BandPanel panel (model);
    double bpm = 90.0;
    panel.tempoProvider = [&bpm] { return bpm; };

    const int band = model.createBand (Shape::Bell, 1000.0f, 0.0f);
    model.setMode (band, BandMode::Dynamic);
    auto shown = [&panel] (BandParam p) {
        const auto params = panel.visibleParameters();
        return std::find (params.begin(), params.end(), p) != params.end();
    };
    CHECK (shown (BandParam::Release));
    CHECK (shown (BandParam::ReleaseSync));
    CHECK_FALSE (shown (BandParam::ReleaseNote));

    juce::Button* rhythm = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getName() == "Rhythm")
            rhythm = b;
    REQUIRE (rhythm != nullptr);
    REQUIRE (rhythm->onClick != nullptr);
    rhythm->onClick();
    model.refresh();
    CHECK (model.band (band).releaseSync);
    CHECK (rhythm->getToggleState());
    CHECK (shown (BandParam::ReleaseNote));
    CHECK_FALSE (shown (BandParam::Release));
    CHECK (rhythm->getTooltip().contains ("1/16 = 166.7 ms at 90 BPM"));

    // One undo step brings the milliseconds back.
    REQUIRE (processor.getUndoManager().undo());
    model.refresh();
    CHECK_FALSE (model.band (band).releaseSync);
    CHECK (shown (BandParam::Release));

    CHECK (ui::BandPanel::rhythmReleaseText (noteIndex ("1/16"), 120.0) == "1/16 = 125.0 ms at 120 BPM");
    CHECK (ui::BandPanel::rhythmReleaseText (noteIndex ("1/4D"), 128.5) == "1/4D = 700.4 ms at 128.5 BPM");
    CHECK (ui::BandPanel::rhythmReleaseText (noteIndex ("1/1"), 60.0) == "1/1 = 4.00 s at 60 BPM");
}

TEST_CASE ("The band panel shows the wave period of the band frequency and the attack in cycles", "[ui][DYN-6]")
{
    JuceEnvironment env;
    using ui::PeriodReadout;
    CHECK (PeriodReadout::periodText (1000.0f) == "1.00 ms");
    CHECK (PeriodReadout::periodText (3150.0f) == "0.317 ms");
    CHECK (PeriodReadout::periodText (24.0f) == "41.7 ms");
    CHECK (PeriodReadout::cyclesText (20.0f, 150.0f) == "3.0 cycles");
    CHECK (PeriodReadout::cyclesText (5.0f, 3150.0f) == "16 cycles");
    CHECK (PeriodReadout::cyclesText (1.0f, 1000.0f) == "1.0 cycle");
    CHECK (PeriodReadout::cyclesText (0.5f, 1000.0f) == "0.5 cycle");

    SscAudioProcessor processor;
    ui::BandModel model (processor.getValueTreeState(), processor.getUndoManager());
    ui::BandPanel panel (model);
    const int band = model.createBand (Shape::Bell, 150.0f, -3.0f);
    const auto& readout = panel.getPeriodReadout();
    CHECK (readout.isVisible());
    CHECK (readout.getPeriodText() == "6.67 ms");
    CHECK (readout.getCyclesText().isEmpty()); // Static: no attack
    model.setMode (band, BandMode::Dynamic);
    {
        ui::BandModel::ScopedEdit edit (model, "Change Attack");
        model.setValue (band, BandParam::Attack, 20.0f);
    }
    CHECK (readout.getCyclesText() == "3.0 cycles");
    {
        ui::BandModel::ScopedEdit edit (model, "Change Freq");
        model.setValue (band, BandParam::Frequency, 3150.0f);
    }
    CHECK (readout.getPeriodText() == "0.317 ms");
    CHECK (readout.getCyclesText() == "63 cycles");
    // At the right of the EQ row, clear of the Target and Character choices.
    panel.setSize (panel.getPreferredSize().x, panel.getPreferredSize().y);
    CHECK (readout.getRight() <= panel.getWidth() - 8);
    for (auto* child : panel.getChildren())
        if (child != &readout && child->isVisible() && child->getBottom() < 100)
            CHECK_FALSE (child->getBounds().intersects (readout.getBounds()));
}
