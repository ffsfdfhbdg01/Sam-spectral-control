#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/StateMigration.h"
#include "ui/BandModel.h"
#include "ui/BandPanel.h"
#include "ui/Controls.h"
#include "ui/Motion.h"
#include "ui/PanelLayout.h"
#include "ui/PluginEditor.h"
#include "ui/Theme.h"

// Interface rules of research §15 (acceptance criteria E3, E6, E7 and the information levels).

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

/** Reference for an easing: dense sampling of the Bézier parameter, linear interpolation in x. */
float bruteForce (const motion::Easing& e, float t)
{
    auto bezier = [] (double a, double b, double s) { return 3.0 * a * s * (1 - s) * (1 - s) + 3.0 * b * s * s * (1 - s) + s * s * s; };
    double previousX = 0.0, previousY = 0.0;
    for (int i = 1; i <= 200000; ++i)
    {
        const double s = i / 200000.0;
        const double x = bezier (e.x1, e.x2, s), y = bezier (e.y1, e.y2, s);
        if (x >= t)
        {
            const double f = x > previousX ? (t - previousX) / (x - previousX) : 0.0;
            return static_cast<float> (previousY + f * (y - previousY));
        }
        previousX = x;
        previousY = y;
    }
    return 1.0f;
}

bool contains (const std::vector<BandParam>& params, BandParam p)
{
    return std::find (params.begin(), params.end(), p) != params.end();
}
} // namespace

TEST_CASE ("Easing curves match the cubic Beziers of the motion rules (research 15 d9, E7)", "[ui][UX]")
{
    for (const auto& easing : { motion::decelerate, motion::accelerate, motion::standard })
    {
        CHECK (easing (0.0f) == 0.0f);
        CHECK (easing (1.0f) == 1.0f);
        CHECK (easing (-0.5f) == 0.0f);
        CHECK (easing (1.5f) == 1.0f);
        float previous = 0.0f;
        for (int i = 1; i <= 40; ++i)
        {
            const float t = static_cast<float> (i) / 40.0f;
            const float v = easing (t);
            CHECK (v >= previous - 1.0e-6f); // no overshoot back: monotonic
            CHECK (v <= 1.0f + 1.0e-6f);     // no overshoot past the target
            CHECK (v == Approx (bruteForce (easing, t)).margin (2.0e-4));
            previous = v;
        }
    }
    // Entering elements decelerate (fast start), leaving ones accelerate (slow start).
    CHECK (motion::decelerate (0.25f) > 0.5f);
    CHECK (motion::accelerate (0.25f) < 0.1f);
}

TEST_CASE ("Tweens are interruptible and the motion level shortens or removes them (E7)", "[ui][UX]")
{
    motion::Tween tween;
    tween.snap (0.0f);
    CHECK_FALSE (tween.isRunning (0.0));
    tween.animateTo (100.0f, 1000.0, 200.0, motion::standard);
    CHECK (tween.isRunning (1100.0));
    CHECK (tween.valueAt (1000.0) == 0.0f);
    const float halfway = tween.valueAt (1100.0);
    CHECK (halfway > 50.0f); // the standard curve front-loads the motion
    CHECK (halfway < 100.0f);

    // Retargeting starts from where the value is now: no jump.
    tween.animateTo (-50.0f, 1100.0, 200.0, motion::standard);
    CHECK (tween.valueAt (1100.0) == Approx (halfway));
    CHECK (tween.valueAt (1300.0) == -50.0f);
    CHECK_FALSE (tween.isRunning (1300.0));

    // The same target again does not restart a running glide.
    tween.animateTo (10.0f, 2000.0, 100.0, motion::decelerate);
    const float early = tween.valueAt (2050.0);
    tween.animateTo (10.0f, 2050.0, 100.0, motion::decelerate);
    CHECK (tween.valueAt (2050.0) == Approx (early));
    CHECK (tween.valueAt (2100.0) == 10.0f);

    CHECK (motion::moveDuration (motion::Level::Full, 180.0) == 180.0);
    CHECK (motion::moveDuration (motion::Level::Reduced, 180.0) == 0.0);
    CHECK (motion::moveDuration (motion::Level::Off, 180.0) == 0.0);
    CHECK (motion::fadeDuration (motion::Level::Full, 160.0) == 160.0);
    CHECK (motion::fadeDuration (motion::Level::Reduced, 160.0) == 100.0);
    CHECK (motion::fadeDuration (motion::Level::Reduced, 80.0) == 80.0);
    CHECK (motion::fadeDuration (motion::Level::Off, 160.0) == 0.0);

    // The level is set on the editor and read by every component inside it.
    JuceEnvironment env;
    juce::Component root, child;
    root.addAndMakeVisible (child);
    CHECK (motion::levelFor (child) == motion::Level::Full);
    motion::setLevel (root, motion::Level::Reduced);
    CHECK (motion::levelFor (child) == motion::Level::Reduced);

    // With motion off, a component jumps to its target at once and hides when transparent.
    motion::ComponentMotion move (child);
    move.snapTo ({ 0, 0, 10, 10 }, 1.0f);
    CHECK (child.isVisible());
    move.moveTo ({ 50, 40, 20, 10 }, 0.0, motion::standard);
    CHECK (child.getBounds() == juce::Rectangle<int> (50, 40, 20, 10));
    move.fadeTo (0.0f, 0.0, motion::accelerate);
    CHECK_FALSE (child.isVisible());
    CHECK_FALSE (move.isAnimating());
}

TEST_CASE ("Colour tokens meet the contrast rules (WCAG 1.4.3, 1.4.11; research 15 E3)", "[ui][UX]")
{
    for (const auto layer : { colours::panel, colours::popover })
    {
        CHECK (contrastRatio (colours::text, layer) >= 4.5);
        CHECK (contrastRatio (colours::textSecondary, layer) >= 4.5);
        CHECK (contrastRatio (colours::textDim, layer) >= 4.5);
        CHECK (contrastRatio (colours::outline, layer) >= 2.0); // knob tracks and control outlines
        CHECK (contrastRatio (colours::accent, layer) >= 3.0);
        CHECK (contrastRatio (colours::lamp, layer) >= 3.0);
        CHECK (contrastRatio (colours::powerOn, layer) >= 3.0);
        CHECK (contrastRatio (colours::warning, layer) >= 3.0);
    }
    CHECK (contrastRatio (colours::textDim, colours::background) >= 4.5);
    CHECK (contrastRatio (colours::textDim, colours::displayTop) >= 4.5);
    // Knob value arcs are drawn in the band colours.
    for (int band = 0; band < kNumBands; ++band)
        CHECK (contrastRatio (bandColour (band), colours::panel) >= 3.0);
    CHECK (contrastRatio (juce::Colours::white, juce::Colours::black) == Approx (21.0));
}

TEST_CASE ("The band panel docks under its node and never covers it (research 15 d4, E6)", "[ui][UX]")
{
    // The display of the default 1100 x 660 window: 40 px header, 56 px bottom bar.
    const juce::Rectangle<int> area (0, 40, 1100, 564);
    for (const int panelHeight : { BandPanel::kStaticHeight, BandPanel::kDynamicHeight })
        for (const bool flyout : { false, true })
            for (const bool globalPanel : { false, true })
            {
                if (globalPanel && flyout)
                    continue; // checked separately below
                for (int ix = 0; ix <= 40; ++ix)
                    for (int iy = 0; iy <= 30; ++iy)
                    {
                        BandPanelDockRequest r;
                        r.area = area;
                        r.panelSize = { 488, panelHeight };
                        r.flyoutSize = flyout ? juce::Point<int> (BandAdvancedPanel::kWidth, panelHeight) : juce::Point<int>();
                        r.node = { static_cast<float> (ix) * 1100.0f / 40.0f, static_cast<float> (iy) * 564.0f / 30.0f };
                        r.bottomInset = globalPanel ? 92 : 22;
                        const auto dock = dockBandPanel (r);
                        const auto node = r.node + area.getPosition().toFloat();
                        INFO ("node " << node.x << ", " << node.y << " panel " << dock.panel.toString() << " flyout "
                                      << dock.flyout.toString());
                        CHECK_FALSE (dock.coversNode);
                        CHECK_FALSE (dock.panel.toFloat().expanded (r.nodeClearance).contains (node));
                        CHECK_FALSE (dock.flyout.toFloat().expanded (r.nodeClearance).contains (node));
                        CHECK (area.reduced (r.margin, 0).contains (dock.panel.withY (area.getY() + 10).withHeight (1)));
                        CHECK (dock.flyout.isEmpty() != flyout);
                        CHECK (dock.panel.getBottom() <= area.getBottom() - r.bottomInset);
                    }
            }

    // Centred under the node when nothing is in the way; the flyout opens to the right.
    BandPanelDockRequest r;
    r.area = area;
    r.panelSize = { 488, BandPanel::kStaticHeight };
    r.flyoutSize = { BandAdvancedPanel::kWidth, BandPanel::kStaticHeight };
    r.node = { 400.0f, 80.0f };
    auto dock = dockBandPanel (r);
    CHECK (dock.panel.getCentreX() == 400);
    CHECK_FALSE (dock.atTop);
    CHECK_FALSE (dock.flyoutLeft);
    CHECK (dock.flyout.getX() == dock.panel.getRight() + r.gap);
    CHECK (dock.flyout.getBottom() == dock.panel.getBottom());

    // A node low in the display sends the pair to the top.
    r.node = { 400.0f, 500.0f };
    dock = dockBandPanel (r);
    CHECK (dock.atTop);
    CHECK (dock.panel.getY() == area.getY() + r.topInset);

    // Near the right edge the pair moves left; the flyout opens to the left when it would cover the node.
    r.node = { 1050.0f, 300.0f };
    r.panelSize = { 488, BandPanel::kDynamicHeight };
    r.flyoutSize = { BandAdvancedPanel::kWidth, BandPanel::kDynamicHeight };
    dock = dockBandPanel (r);
    CHECK_FALSE (dock.coversNode);
    CHECK (dock.flyout.getRight() <= area.getRight() - r.margin);

    // With the global Advanced panel open and the flyout open, only nodes in the middle band and centre
    // may be covered (both docks overlap there); nothing leaves the display.
    r.bottomInset = 92;
    r.node = { 550.0f, 240.0f };
    dock = dockBandPanel (r);
    CHECK (area.contains (dock.panel));
    CHECK (area.contains (dock.flyout));
}

TEST_CASE ("The band panel shows the knobs of the band's mode; details live in Advanced (research 15 d4, E2)", "[ui][UX]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    BandModel model (processor.getValueTreeState(), processor.getUndoManager());
    BandPanel panel (model);
    BandAdvancedPanel advanced (model);

    const int band = model.createBand (Shape::Bell, 1000.0f, 3.0f);
    REQUIRE (band == 0);
    REQUIRE (panel.getBand() == 0);

    // Static: the EQ only; Advanced has no detector settings (readouts only).
    auto shown = panel.visibleParameters();
    for (const auto p : { BandParam::Mode, BandParam::On, BandParam::Shape, BandParam::Frequency, BandParam::Gain, BandParam::Q,
                          BandParam::Target, BandParam::Character })
        CHECK (contains (shown, p));
    for (const auto p : { BandParam::Threshold, BandParam::Ratio, BandParam::Range, BandParam::Attack, BandParam::Release,
                          BandParam::Selectivity, BandParam::AutoGain, BandParam::Slope, BandParam::Knee, BandParam::Lookahead })
        CHECK_FALSE (contains (shown, p));
    CHECK (shown.size() <= 11);
    CHECK (advanced.visibleParameters().empty());
    CHECK (panel.getPreferredSize().y == BandPanel::kStaticHeight);

    // Dynamic: Threshold, Ratio, Range, Attack, Release and Auto Gain; knee, lookahead, mix, link,
    // threshold mode and direction in Advanced.
    model.setMode (band, BandMode::Dynamic);
    shown = panel.visibleParameters();
    for (const auto p :
         { BandParam::Threshold, BandParam::Ratio, BandParam::Range, BandParam::Attack, BandParam::Release, BandParam::AutoGain })
        CHECK (contains (shown, p));
    for (const auto p : { BandParam::Selectivity, BandParam::Knee, BandParam::Lookahead, BandParam::Mix, BandParam::Link,
                          BandParam::ThresholdMode, BandParam::Direction })
        CHECK_FALSE (contains (shown, p));
    CHECK (shown.size() <= 14);
    auto detail = advanced.visibleParameters();
    for (const auto p :
         { BandParam::ThresholdMode, BandParam::Direction, BandParam::Knee, BandParam::Lookahead, BandParam::Mix, BandParam::Link })
        CHECK (contains (detail, p));
    CHECK_FALSE (contains (detail, BandParam::Ratio));
    CHECK (panel.getPreferredSize().y == BandPanel::kDynamicHeight);
    CHECK (advanced.getPreferredHeight() <= BandPanel::kDynamicHeight);

    // Spectral: Selectivity takes the place of Ratio, which moves to Advanced; no Lookahead.
    model.setMode (band, BandMode::Spectral);
    shown = panel.visibleParameters();
    CHECK (contains (shown, BandParam::Selectivity));
    CHECK_FALSE (contains (shown, BandParam::Ratio));
    detail = advanced.visibleParameters();
    CHECK (contains (detail, BandParam::Ratio));
    CHECK_FALSE (contains (detail, BandParam::Lookahead));

    // Shapes with a slope show it; the panel keeps its width when the mode changes.
    const int width = panel.getPreferredSize().x;
    model.setShape (band, Shape::LowShelf);
    CHECK (contains (panel.visibleParameters(), BandParam::Slope));
    model.setMode (band, BandMode::Static);
    CHECK (panel.getPreferredSize().x == width);
}

TEST_CASE ("The Advanced button marks detector settings that differ from the mode's defaults (research 15 d1.7)", "[ui][UX]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    BandModel model (processor.getValueTreeState(), processor.getUndoManager());
    const int band = model.createBand (Shape::Bell, 1000.0f, 0.0f);

    CHECK_FALSE (BandPanel::advancedDiffersFromDefault (model, band)); // Static: nothing to hide
    model.setMode (band, BandMode::Dynamic);
    CHECK_FALSE (BandPanel::advancedDiffersFromDefault (model, band));
    {
        BandModel::ScopedEdit edit (model, "Change Knee");
        model.setValue (band, BandParam::Knee, 12.0f);
    }
    CHECK (BandPanel::advancedDiffersFromDefault (model, band));
    REQUIRE (processor.getUndoManager().undo());
    model.refresh();
    CHECK_FALSE (BandPanel::advancedDiffersFromDefault (model, band));

    // Spectral moves the untouched threshold to Relative +6 dB: still the default for that mode.
    model.setMode (band, BandMode::Spectral);
    CHECK_FALSE (BandPanel::advancedDiffersFromDefault (model, band));
    {
        BandModel::ScopedEdit edit (model, "Change Threshold Mode");
        model.setValue (band, BandParam::ThresholdMode, static_cast<float> (ThresholdMode::Absolute));
    }
    CHECK (BandPanel::advancedDiffersFromDefault (model, band));

    // A band Lookahead counts only where it acts (Dynamic).
    {
        BandModel::ScopedEdit edit (model, "Reset");
        model.setValue (band, BandParam::ThresholdMode, static_cast<float> (ThresholdMode::Relative));
        model.setValue (band, BandParam::Lookahead, 5.0f);
    }
    CHECK_FALSE (BandPanel::advancedDiffersFromDefault (model, band));
    model.setMode (band, BandMode::Dynamic);
    CHECK (BandPanel::advancedDiffersFromDefault (model, band));
}

TEST_CASE ("Preset comparisons: modified dot and an A/B copy only when the slots differ (research 15 d3, GLB-3)", "[ui][UX][GLB-3]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor p;
    auto& s = p.getValueTreeState();
    auto& undo = p.getUndoManager();

    // A fresh instance equals the defaults ("Default" factory preset).
    const auto& factory = plugin::factoryPresets();
    REQUIRE_FALSE (factory.empty());
    const plugin::PresetComparison defaults (s, factory.front().tree);
    REQUIRE (defaults.isValid());
    CHECK (defaults.matches (s));

    // Loading a preset: equal until something changes, again after undo.
    const auto& vocal = factory[1];
    REQUIRE (plugin::loadPreset (s, undo, vocal.tree));
    const plugin::PresetComparison loaded (s, vocal.tree);
    CHECK (loaded.matches (s));
    CHECK_FALSE (defaults.matches (s));
    auto* gain = s.getParameter (plugin::bandParamId (1, BandParam::Gain));
    REQUIRE (gain != nullptr);
    undo.beginNewTransaction();
    gain->setValueNotifyingHost (gain->convertTo0to1 (-5.0f));
    CHECK_FALSE (loaded.matches (s));
    gain->setValueNotifyingHost (gain->convertTo0to1 (-2.0f));
    CHECK (loaded.matches (s));

    // Monitoring states are not part of a preset.
    s.getParameter (plugin::id::delta)->setValueNotifyingHost (1.0f);
    CHECK (loaded.matches (s));
    s.getParameter (plugin::id::delta)->setValueNotifyingHost (0.0f);

    // A/B: no other slot before the first switch (it would start as a copy).
    CHECK_FALSE (plugin::otherComparePreset (s.state).isValid());
    plugin::switchCompareSlot (s, undo);
    auto other = plugin::otherComparePreset (s.state);
    REQUIRE (other.isValid());
    CHECK (plugin::PresetComparison (s, other).matches (s));
    gain->setValueNotifyingHost (gain->convertTo0to1 (4.0f));
    CHECK_FALSE (plugin::PresetComparison (s, other).matches (s));
    plugin::copyToOtherSlot (s, undo);
    CHECK (plugin::PresetComparison (s, plugin::otherComparePreset (s.state)).matches (s));

    CHECK_FALSE (plugin::PresetComparison (s, juce::ValueTree ("NotAPreset")).isValid());
}

TEST_CASE ("Segmented control: one option at a time, disabled options are skipped (research 15 d7)", "[ui][UX]")
{
    JuceEnvironment env;
    SegmentedControl control ({ "Static", "Dynamic", "Spectral" });
    control.setBounds (0, 0, 198, 26);
    int changes = 0, last = -1;
    control.onChange = [&] (int index) {
        ++changes;
        last = index;
    };
    control.setSelectedIndex (2);
    CHECK (changes == 1);
    CHECK (last == 2);
    control.setSelectedIndex (2);
    CHECK (changes == 1); // no change, no notification
    control.setSelectedIndex (0, juce::dontSendNotification);
    CHECK (changes == 1);
    CHECK (control.getSelectedIndex() == 0);
    CHECK (control.getIndicatorX() == Approx (control.getSegmentBounds (0).getX())); // not shown: no glide

    control.setItemEnabled (1, false);
    CHECK (control.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (control.getSelectedIndex() == 2); // skipped the disabled option
    CHECK (control.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (control.getSelectedIndex() == 0);
    CHECK (control.getSegmentBounds (1).getWidth() == Approx (control.getSegmentBounds (0).getWidth()));

    // Click targets of the buttons are at least 24 x 24 px (WCAG 2.2, 2.5.8; E4).
    CHECK (control.getSegmentBounds (0).getHeight() + 4.0f >= static_cast<float> (metrics::targetSize));
}

TEST_CASE ("The editor docks its panels clear of each other and closes while they move (research 15 d6-d8)", "[ui][UX]")
{
    JuceEnvironment env;
    plugin::SscAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* ssc = dynamic_cast<SscEditor*> (editor.get());
        REQUIRE (ssc != nullptr);
        ssc->setMotionLevel (motion::Level::Off); // every layout is final at once
        editor->setSize (SscEditor::kDefaultWidth, SscEditor::kDefaultHeight);
        const int barTop = SscEditor::kDefaultHeight - SscEditor::kControlBarHeight;

        auto& model = ssc->getBandModel();
        const auto& panel = ssc->getBandPanel();
        const auto& flyout = ssc->getBandAdvancedPanel();
        CHECK_FALSE (panel.isVisible());
        const int band = model.createBand (Shape::Bell, 1000.0f, 3.0f);
        REQUIRE (band >= 0);
        CHECK (panel.isVisible());
        CHECK (panel.getWidth() == panel.getPreferredSize().x);
        CHECK (panel.getHeight() == BandPanel::kStaticHeight);
        CHECK (panel.getBottom() <= barTop);

        model.setMode (band, BandMode::Dynamic);
        CHECK (panel.getHeight() == BandPanel::kDynamicHeight);
        ssc->setBandAdvancedOpen (true);
        CHECK (ssc->isBandAdvancedOpen());
        CHECK (flyout.isVisible());
        CHECK_FALSE (flyout.getBounds().intersects (panel.getBounds()));
        CHECK (flyout.getHeight() >= panel.getHeight());

        // The global panel slides up from the bottom bar; the band panel docks above it.
        ssc->setGlobalAdvancedOpen (true);
        CHECK (panel.getBottom() <= barTop - GlobalAdvancedPanel::kHeight);
        CHECK (flyout.getBottom() <= barTop - GlobalAdvancedPanel::kHeight);

        model.clearSelection();
        CHECK_FALSE (panel.isVisible());
        CHECK_FALSE (flyout.isVisible());

        // With motion on, the panels start moving; closing the editor meanwhile is safe.
        ssc->setMotionLevel (motion::Level::Full);
        model.select (band, false);
        ssc->setGlobalAdvancedOpen (false);
    }

    // The interface state stays with the processor (saved with the session).
    const auto& state = processor.getValueTreeState().state;
    CHECK (static_cast<bool> (state[plugin::kUiBandAdvanced]));
    CHECK_FALSE (static_cast<bool> (state[plugin::kUiGlobalAdvanced]));
    CHECK (static_cast<int> (state[plugin::kUiMotion]) == static_cast<int> (motion::Level::Full));
    processor.releaseResources();
}
