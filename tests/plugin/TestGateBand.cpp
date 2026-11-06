#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ui/BandModel.h"
#include "ui/BandPanel.h"

// The Gate tab of a band (DYN-12): a fourth band mode beside Static, Dynamic and Spectral.

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
constexpr int kBlock = 512;

void setPlain (SscAudioProcessor& p, const juce::String& paramId, float plain)
{
    auto* param = p.getValueTreeState().getParameter (paramId);
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

float plainOf (SscAudioProcessor& p, int band, BandParam param)
{
    return p.getValueTreeState().getRawParameterValue (bandParamId (band, param))->load();
}

bool contains (const std::vector<BandParam>& params, BandParam p)
{
    return std::find (params.begin(), params.end(), p) != params.end();
}

/** Change (dB) of a 1 kHz tone at `levelDb` dBFS over the second half of one second. */
double changeDb (SscAudioProcessor& p, double levelDb)
{
    p.setRateAndBufferSizeDetails (kFs, kBlock);
    p.prepareToPlay (kFs, kBlock);
    const int length = 48000;
    const auto tone = test::sine (1000.0, 1.0, kFs, length);
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    double in = 0.0, out = 0.0;
    for (int offset = 0; offset < length; offset += kBlock)
    {
        const int n = std::min (kBlock, length - offset);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, static_cast<float> (dbToGain (levelDb) * tone[static_cast<std::size_t> (offset + i)]));
        p.processBlock (buffer, midi);
        if (offset >= length / 2)
            for (int i = 0; i < n; ++i)
            {
                const double x = dbToGain (levelDb) * tone[static_cast<std::size_t> (offset + i)];
                in += x * x;
                out += static_cast<double> (buffer.getSample (0, i)) * static_cast<double> (buffer.getSample (0, i));
            }
    }
    return 10.0 * std::log10 (out / in);
}
} // namespace

TEST_CASE ("Gate is a band mode: parameters, processing and the state of 1.2 (DYN-12, GLB-4)", "[plugin][gate][DYN-12]")
{
    JuceEnvironment env;
    CHECK (plugin::labels::bandModes() == juce::StringArray { "Static", "Dynamic", "Spectral", "Gate" });

    SscAudioProcessor p;
    auto set = [&p] (BandParam param, float value) { setPlain (p, bandParamId (0, param), value); };
    set (BandParam::Used, 1.0f);
    set (BandParam::Frequency, 1000.0f);
    set (BandParam::Mode, static_cast<float> (BandMode::Gate));
    set (BandParam::Threshold, -40.0f);
    set (BandParam::Range, 24.0f);
    set (BandParam::Hold, 20.0f);
    set (BandParam::Hysteresis, 6.0f);
    CHECK (plainOf (p, 0, BandParam::Mode) == 3.0f);
    CHECK (changeDb (p, -20.0) == Approx (0.0).margin (0.1));
    CHECK (changeDb (p, -60.0) == Approx (-24.0).margin (0.5));
    CHECK (p.getLatencySamples() == 0);

    // A state saved by 1.2 (no Hold or Hysteresis) loads them at their defaults, its modes unchanged.
    SscAudioProcessor source;
    setPlain (source, bandParamId (0, BandParam::Used), 1.0f);
    setPlain (source, bandParamId (0, BandParam::Mode), static_cast<float> (BandMode::Spectral));
    juce::MemoryBlock blob;
    source.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto id = tree.getChild (i)["id"].toString();
        if (id.endsWith ("_hold") || id.endsWith ("_hysteresis"))
            tree.removeChild (i, nullptr);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
    SscAudioProcessor restored;
    setPlain (restored, bandParamId (0, BandParam::Hold), 300.0f);
    restored.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    CHECK (plainOf (restored, 0, BandParam::Mode) == static_cast<float> (BandMode::Spectral));
    CHECK (plainOf (restored, 0, BandParam::Hold) == Approx (10.0f));
    CHECK (plainOf (restored, 0, BandParam::Hysteresis) == Approx (4.0f));
}

TEST_CASE ("The Gate tab of the band panel: its knobs, its Advanced settings, its defaults (DYN-12)", "[ui][gate][DYN-12]")
{
    JuceEnvironment env;
    SscAudioProcessor processor;
    ui::BandModel model (processor.getValueTreeState(), processor.getUndoManager());
    ui::BandPanel panel (model);
    ui::BandAdvancedPanel advanced (model);

    const int band = model.createBand (Shape::Bell, 200.0f, 0.0f);
    model.setMode (band, BandMode::Gate);
    CHECK (model.band (band).mode == BandMode::Gate);
    // From Static (Dynamic's defaults) to the gate's: Absolute −40 dB, Range 24 dB.
    CHECK (plainOf (processor, band, BandParam::ThresholdMode) == static_cast<float> (ThresholdMode::Absolute));
    CHECK (plainOf (processor, band, BandParam::Threshold) == Approx (-40.0f));
    CHECK (plainOf (processor, band, BandParam::Range) == Approx (24.0f));

    auto shown = panel.visibleParameters();
    for (const auto p :
         { BandParam::Threshold, BandParam::Range, BandParam::Attack, BandParam::Hold, BandParam::Release, BandParam::ReleaseSync })
        CHECK (contains (shown, p));
    for (const auto p : { BandParam::Ratio, BandParam::Selectivity, BandParam::Direction, BandParam::AutoGain, BandParam::Hysteresis })
        CHECK_FALSE (contains (shown, p));
    CHECK (shown.size() <= 16);
    CHECK (panel.getPreferredSize().y == ui::BandPanel::kDynamicHeight);
    auto detail = advanced.visibleParameters();
    for (const auto p :
         { BandParam::ThresholdMode, BandParam::Hysteresis, BandParam::Lookahead, BandParam::Mix, BandParam::Link, BandParam::Sidechain })
        CHECK (contains (detail, p));
    CHECK_FALSE (contains (detail, BandParam::Knee));
    CHECK_FALSE (contains (detail, BandParam::Ratio));

    // Advanced marks a hysteresis away from its default.
    CHECK_FALSE (ui::BandPanel::advancedDiffersFromDefault (model, band));
    {
        ui::BandModel::ScopedEdit edit (model, "Change Hysteresis");
        model.setValue (band, BandParam::Hysteresis, 10.0f);
    }
    CHECK (ui::BandPanel::advancedDiffersFromDefault (model, band));

    // A gate's Lookahead raises Max Lookahead, as a Dynamic band's does (DYN-8).
    {
        ui::BandModel::ScopedEdit edit (model, "Change Lookahead");
        model.setValue (band, BandParam::Lookahead, 5.0f);
    }
    CHECK (processor.getValueTreeState().getRawParameterValue (plugin::id::maxLookahead)->load() == Approx (5.0f));

    // Back to Dynamic: the gate's untouched defaults become Dynamic's; a changed threshold stays.
    model.setMode (band, BandMode::Dynamic);
    CHECK (plainOf (processor, band, BandParam::Threshold) == Approx (-20.0f));
    CHECK (plainOf (processor, band, BandParam::Range) == Approx (12.0f));
    {
        ui::BandModel::ScopedEdit edit (model, "Change Threshold");
        model.setValue (band, BandParam::Threshold, -33.0f);
    }
    model.setMode (band, BandMode::Gate);
    CHECK (plainOf (processor, band, BandParam::Threshold) == Approx (-33.0f));
    CHECK (plainOf (processor, band, BandParam::Range) == Approx (24.0f));

    // Shapes without gain have no Gate.
    model.setShape (band, Shape::LowCut);
    CHECK (model.band (band).mode == BandMode::Static);
    model.setMode (band, BandMode::Gate);
    CHECK (model.band (band).mode == BandMode::Static);
}

TEST_CASE ("Band clipboard: Gate copies and pastes; texts of 1.0 to 1.2 keep their mode (DYN-12)", "[ui][gate][DYN-12]")
{
    JuceEnvironment env;
    SscAudioProcessor processor;
    ui::BandModel model (processor.getValueTreeState(), processor.getUndoManager());

    const int gate = model.createBand (Shape::Bell, 150.0f, 0.0f);
    model.setMode (gate, BandMode::Gate);
    {
        ui::BandModel::ScopedEdit edit (model, "Change Hold");
        model.setValue (gate, BandParam::Hold, 75.0f);
    }
    const auto text = model.bandText (gate);
    CHECK (text.startsWith ("SamSpectralControl.band.v4:"));
    const int other = model.createBand (Shape::Bell, 3000.0f, 0.0f);
    REQUIRE (model.pasteBandText (other, text));
    model.refresh();
    CHECK (model.band (other).mode == BandMode::Gate);
    CHECK (plainOf (processor, other, BandParam::Hold) == Approx (75.0f));

    // Texts up to v3 stored Mode normalised over three modes: Spectral (1.0) stays Spectral, Dynamic
    // (0.5) stays Dynamic.
    for (const auto mode : { BandMode::Static, BandMode::Dynamic, BandMode::Spectral })
    {
        CAPTURE (static_cast<int> (mode));
        juce::StringArray items;
        items.addTokens (text.fromFirstOccurrenceOf (":", false, false), ",", {});
        items.remove (static_cast<int> (BandParam::Hysteresis));
        items.remove (static_cast<int> (BandParam::Hold));
        items.set (static_cast<int> (BandParam::Mode), juce::String (static_cast<float> (mode) / 2.0f, 9));
        REQUIRE (model.pasteBandText (other, "SamSpectralControl.band.v3:" + items.joinIntoString (",")));
        model.refresh();
        CHECK (model.band (other).mode == mode);
        CHECK (plainOf (processor, other, BandParam::Hold) == Approx (10.0f)); // newer than the text: default
    }
}
