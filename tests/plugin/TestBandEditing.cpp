#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ui/BandLogic.h"
#include "ui/BandModel.h"

using Catch::Approx;
using namespace ssc;
using namespace ssc::ui;
using plugin::BandParam;

namespace
{
struct JuceEnvironment
{
    juce::ScopedJuceInitialiser_GUI init;
};

struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    int begins = 0;
    int ends = 0;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { ++(starting ? begins : ends); }
};
} // namespace

TEST_CASE ("Band shape follows the click zone (BAND-4)", "[ui][BAND-4]")
{
    using G = CreateGesture;

    // Double click: cut zones at the edges take priority, notch in the bottom strip, bell elsewhere.
    REQUIRE (shapeForGesture (0.00f, 0.5f, G::DoubleClick) == Shape::LowCut);
    REQUIRE (shapeForGesture (0.039f, 0.5f, G::DoubleClick) == Shape::LowCut);
    REQUIRE (shapeForGesture (zones::lowCutX, 0.5f, G::DoubleClick) == Shape::Bell);
    REQUIRE (shapeForGesture (0.961f, 0.5f, G::DoubleClick) == Shape::HighCut);
    REQUIRE (shapeForGesture (zones::highCutX, 0.5f, G::DoubleClick) == Shape::Bell);
    REQUIRE (shapeForGesture (0.5f, 0.881f, G::DoubleClick) == Shape::Notch);
    REQUIRE (shapeForGesture (0.5f, zones::notchY, G::DoubleClick) == Shape::Bell);
    REQUIRE (shapeForGesture (0.02f, 0.95f, G::DoubleClick) == Shape::LowCut);
    REQUIRE (shapeForGesture (0.98f, 0.95f, G::DoubleClick) == Shape::HighCut);
    REQUIRE (shapeForGesture (0.5f, 0.1f, G::DoubleClick) == Shape::Bell);

    // Dragging the composite curve: shelves near the ends, bell elsewhere, never a cut or notch.
    REQUIRE (shapeForGesture (0.0f, 0.5f, G::DragCurve) == Shape::LowShelf);
    REQUIRE (shapeForGesture (0.149f, 0.5f, G::DragCurve) == Shape::LowShelf);
    REQUIRE (shapeForGesture (zones::lowShelfX, 0.5f, G::DragCurve) == Shape::Bell);
    REQUIRE (shapeForGesture (0.851f, 0.5f, G::DragCurve) == Shape::HighShelf);
    REQUIRE (shapeForGesture (zones::highShelfX, 0.5f, G::DragCurve) == Shape::Bell);
    REQUIRE (shapeForGesture (0.5f, 0.95f, G::DragCurve) == Shape::Bell);
}

TEST_CASE ("Slope menu lists only the slopes a shape supports (BAND-5)", "[ui][BAND-5]")
{
    REQUIRE (slopesFor (Shape::Bell).empty());
    REQUIRE (slopesFor (Shape::Notch).empty());
    REQUIRE_FALSE (shapeUsesSlope (Shape::AllPass));
    REQUIRE (slopesFor (Shape::LowCut).size() == 10);
    REQUIRE (slopesFor (Shape::HighCut).back() == Slope::Brickwall);
    REQUIRE (slopesFor (Shape::LowShelf).size() == 7);
    REQUIRE (slopesFor (Shape::TiltShelf).size() == 2);

    REQUIRE (nearestAllowedSlope (Shape::LowCut, Slope::Db72) == Slope::Db72);
    REQUIRE (nearestAllowedSlope (Shape::TiltShelf, Slope::Db48) == Slope::Db12);
    REQUIRE (nearestAllowedSlope (Shape::LowShelf, Slope::Db96) == Slope::Db48);
    REQUIRE (nearestAllowedSlope (Shape::HighShelf, Slope::Brickwall) == Slope::Db48);
    REQUIRE (nearestAllowedSlope (Shape::Bell, Slope::Db24) == Slope::Db24); // no slope: unchanged

    REQUIRE (shapeUsesGain (Shape::TiltShelf));
    REQUIRE_FALSE (shapeUsesGain (Shape::LowCut));
    REQUIRE_FALSE (shapeUsesQ (Shape::FlatTilt));
}

TEST_CASE ("Creating, editing and deleting bands are single undo steps (BAND-5, BAND-6)", "[ui][BAND-5][BAND-6]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    auto& undo = processor.getUndoManager();
    BandModel model (processor.getValueTreeState(), undo);

    const int band = model.createBand (Shape::Bell, 1000.0f, 6.0f);
    REQUIRE (band == 0);
    REQUIRE (model.band (0).used);
    REQUIRE (model.band (0).on);
    REQUIRE (model.band (0).gain == Approx (6.0f));
    REQUIRE (model.band (0).q == Approx (1.0f));
    REQUIRE (model.isSelected (0));
    REQUIRE (model.getFocusedBand() == 0);

    // A whole drag is one step, and each parameter gets exactly one host gesture.
    GestureCounter gestures;
    auto& gainParam = model.parameter (0, BandParam::Gain);
    gainParam.addListener (&gestures);
    model.beginEdit ("Move Band");
    for (int i = 0; i <= 10; ++i)
    {
        model.setValue (0, BandParam::Gain, 6.0f - static_cast<float> (i));
        model.setValue (0, BandParam::Frequency, 1000.0f + 100.0f * static_cast<float> (i));
    }
    model.endEdit();
    gainParam.removeListener (&gestures);
    REQUIRE (gestures.begins == 1);
    REQUIRE (gestures.ends == 1);
    REQUIRE (model.band (0).gain == Approx (-4.0f));
    REQUIRE (model.band (0).frequency == Approx (2000.0f));

    REQUIRE (undo.undo());
    model.refresh();
    REQUIRE (model.band (0).gain == Approx (6.0f));
    REQUIRE (model.band (0).frequency == Approx (1000.0f));

    // A slope change from the menu is one step too.
    model.setShape (0, Shape::LowCut);
    REQUIRE (model.band (0).shape == Shape::LowCut);
    {
        BandModel::ScopedEdit edit (model, "Change Slope");
        model.setValue (0, BandParam::Slope, static_cast<float> (Slope::Db48));
    }
    REQUIRE (model.band (0).slope == Slope::Db48);
    REQUIRE (undo.undo());
    model.refresh();
    REQUIRE (model.band (0).slope == Slope::Db12);
    REQUIRE (model.band (0).shape == Shape::LowCut);

    // Undoing the creation removes the band and its selection; redo brings it back.
    REQUIRE (undo.undo()); // shape
    REQUIRE (undo.undo()); // creation
    model.refresh();
    REQUIRE_FALSE (model.band (0).used);
    REQUIRE_FALSE (model.isSelected (0));
    REQUIRE (model.getFocusedBand() == -1);
    REQUIRE (undo.redo());
    model.refresh();
    REQUIRE (model.band (0).used);
    REQUIRE (model.band (0).gain == Approx (6.0f));
}

TEST_CASE ("Band slots, selection, delete and shape rules", "[ui][BAND-1][BAND-6]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    BandModel model (processor.getValueTreeState(), processor.getUndoManager());

    for (int i = 0; i < kNumBands; ++i)
        REQUIRE (model.createBand (Shape::Bell, 100.0f * static_cast<float> (i + 1), 1.0f) == i);
    REQUIRE (model.numUsedBands() == kNumBands);
    REQUIRE (model.createBand (Shape::Bell, 500.0f, 3.0f) == -1); // all 24 used

    model.selectAll();
    REQUIRE (model.getSelection().count() == static_cast<std::size_t> (kNumBands));
    model.clearSelection();
    model.select (3, false);
    model.toggleSelected (7);
    REQUIRE (model.getSelection().count() == 2);
    REQUIRE (model.getFocusedBand() == 7);

    model.deleteSelected();
    model.refresh();
    REQUIRE_FALSE (model.band (3).used);
    REQUIRE_FALSE (model.band (7).used);
    REQUIRE (model.numUsedBands() == kNumBands - 2);
    REQUIRE (model.getSelection().none());

    // A deleted slot is reused and starts from defaults.
    REQUIRE (model.createBand (Shape::HighShelf, 8000.0f, -3.0f) == 3);
    REQUIRE (model.band (3).shape == Shape::HighShelf);
    REQUIRE (model.band (3).q == Approx (0.70710678f).margin (1e-4));
    REQUIRE (model.band (3).mode == BandMode::Static);

    // Dynamic mode is dropped when switching to a shape without gain; slope is adapted.
    {
        BandModel::ScopedEdit edit (model, "Change Mode");
        model.setValue (3, BandParam::Mode, static_cast<float> (BandMode::Dynamic));
        model.setValue (3, BandParam::Slope, static_cast<float> (Slope::Db48));
    }
    model.setShape (3, Shape::TiltShelf);
    model.refresh();
    REQUIRE (model.band (3).mode == BandMode::Dynamic); // tilt has gain
    REQUIRE (model.band (3).slope == Slope::Db12);
    model.setShape (3, Shape::Notch);
    model.refresh();
    REQUIRE (model.band (3).mode == BandMode::Static);

    // Gains are not applied to shapes without gain when created.
    REQUIRE (model.createBand (Shape::LowCut, 30.0f, 9.0f) == 7);
    REQUIRE (model.band (7).gain == Approx (0.0f));
    REQUIRE (model.band (7).slope == Slope::Db24);

    // Solo follows the band: switching it off releases the solo.
    int soloReported = -2;
    model.onSoloChanged = [&soloReported] (int b) { soloReported = b; };
    model.setSoloBand (7);
    REQUIRE (soloReported == 7);
    {
        BandModel::ScopedEdit edit (model, "Toggle Band");
        model.setValue (7, BandParam::On, 0.0f);
    }
    REQUIRE (model.getSoloBand() == -1);
    REQUIRE (soloReported == -1);
}

TEST_CASE ("Typed values are parsed in display units (BAND-6)", "[ui][BAND-6]")
{
    REQUIRE (plugin::format::parseFrequency ("1.5k") == Approx (1500.0f));
    REQUIRE (plugin::format::parseFrequency ("2.25 kHz") == Approx (2250.0f));
    REQUIRE (plugin::format::parseFrequency ("440") == Approx (440.0f));
    REQUIRE (plugin::format::parseFrequency ("80 Hz") == Approx (80.0f));
    REQUIRE (plugin::format::parseMilliseconds ("12 ms") == Approx (12.0f));
    REQUIRE (plugin::format::parseMilliseconds ("1.2 s") == Approx (1200.0f));
    REQUIRE (plugin::format::parseMilliseconds ("0.5") == Approx (0.5f));
}

TEST_CASE ("Mode changes move default thresholds; band lookahead raises Max Lookahead (DYN-4, DYN-8)", "[ui][DYN-4][DYN-8]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    BandModel model (processor.getValueTreeState(), processor.getUndoManager());

    const int band = model.createBand (Shape::Bell, 1000.0f, 0.0f);
    auto plain = [&model, band] (BandParam p) {
        auto& param = model.parameter (band, p);
        return param.convertFrom0to1 (param.getValue());
    };

    model.setMode (band, BandMode::Spectral);
    REQUIRE (static_cast<ThresholdMode> (juce::roundToInt (plain (BandParam::ThresholdMode))) == ThresholdMode::Relative);
    REQUIRE (plain (BandParam::Threshold) == Approx (6.0f));

    model.setMode (band, BandMode::Dynamic);
    REQUIRE (static_cast<ThresholdMode> (juce::roundToInt (plain (BandParam::ThresholdMode))) == ThresholdMode::Absolute);
    REQUIRE (plain (BandParam::Threshold) == Approx (-20.0f));

    // A threshold the user changed is kept.
    {
        BandModel::ScopedEdit edit (model, "Change Threshold");
        model.setValue (band, BandParam::Threshold, -32.0f);
    }
    model.setMode (band, BandMode::Spectral);
    REQUIRE (plain (BandParam::Threshold) == Approx (-32.0f));

    // Shapes without gain cannot become dynamic.
    const int cut = model.createBand (Shape::LowCut, 40.0f, 0.0f);
    model.setMode (cut, BandMode::Dynamic);
    model.refresh();
    REQUIRE (model.band (cut).mode == BandMode::Static);

    auto* maxLookahead = processor.getValueTreeState().getParameter (plugin::id::maxLookahead);
    REQUIRE (maxLookahead->convertFrom0to1 (maxLookahead->getValue()) == Approx (0.0f));
    {
        BandModel::ScopedEdit edit (model, "Change Lookahead");
        model.setValue (band, BandParam::Lookahead, 7.5f);
    }
    REQUIRE (maxLookahead->convertFrom0to1 (maxLookahead->getValue()) == Approx (7.5f));
}

TEST_CASE ("Max Lookahead sets the reported latency (DYN-8, ARC-3)", "[plugin][DYN-8][ARC-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    auto* maxLookahead = processor.getValueTreeState().getParameter (plugin::id::maxLookahead);
    maxLookahead->setValueNotifyingHost (maxLookahead->convertTo0to1 (10.0f));

    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    REQUIRE (processor.getLatencySamples() == 480);

    // An impulse comes out exactly the reported latency later.
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    buffer.setSample (0, 10, 1.0f);
    buffer.setSample (1, 10, 1.0f);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    for (int i = 0; i < 512; ++i)
        REQUIRE (buffer.getSample (0, i) == (i == 10 + 480 ? 1.0f : 0.0f));
}
