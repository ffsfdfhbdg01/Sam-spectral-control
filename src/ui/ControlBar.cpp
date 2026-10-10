#include "ui/ControlBar.h"

#include "plugin/Parameters.h"
#include "ui/Theme.h"

namespace ssc::ui
{
namespace
{
    constexpr int kGroupGap = 24; ///< between groups, with a divider in the middle

    void divider (juce::Graphics& g, int x, juce::Rectangle<int> bounds)
    {
        g.setColour (colours::panelEdge);
        g.fillRect (x, bounds.getY() + bounds.getHeight() / 4, 1, bounds.getHeight() / 2);
    }

    juce::RangedAudioParameter& parameter (juce::AudioProcessorValueTreeState& state, const char* paramId)
    {
        auto* p = state.getParameter (paramId);
        jassert (p != nullptr);
        return *p;
    }
} // namespace

// PowerButton ---------------------------------------------------------------------------------

PowerButton::PowerButton (juce::RangedAudioParameter& bypassParameter)
    : attachment (bypassParameter, [this] (float value) {
          bypassed = value >= 0.5f;
          repaint();
      })
{
    attachment.sendInitialUpdate();
    setTooltip ("Plugin On/Off (host bypass)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PowerButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
    const auto colour = bypassed ? colours::powerOff : colours::powerOn;

    if (! bypassed)
    {
        g.setColour (colour.withAlpha (0.16f));
        g.fillEllipse (circle.expanded (3.0f));
    }
    drawIcon (g, Icon::Power, circle.reduced (size * 0.12f), colour);
}

void PowerButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f);
}

// ParameterKnob -------------------------------------------------------------------------------

ParameterKnob::ParameterKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text,
                              bool bipolar)
{
    caption.setText (text, juce::dontSendNotification);
    caption.setFont (uiFont (11.0f));
    caption.setColour (juce::Label::textColourId, colours::textDim);
    caption.setJustificationType (juce::Justification::bottomLeft);
    caption.setInterceptsMouseClicks (false, false);
    caption.setBorderSize ({});

    slider.getProperties().set (kKnobLeftProperty, true);
    slider.getProperties().set (kBipolarProperty, bipolar);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colours::textSecondary);
    addAndMakeVisible (slider);
    addAndMakeVisible (caption);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* p = state.getParameter (paramId))
    {
        slider.setDoubleClickReturnValue (true, static_cast<double> (p->convertFrom0to1 (p->getDefaultValue())));
        slider.setTooltip (p->getName (64) + " (double-click: default; click the value to type one)");
    }
}

void ParameterKnob::resized()
{
    slider.setBounds (getLocalBounds());
    const int knob = juce::jmin (getHeight(), 36);
    caption.setBounds (getLocalBounds().withTrimmedLeft (knob + 7).withHeight (getHeight() / 2));
}

// ControlBar ----------------------------------------------------------------------------------

ControlBar::ControlBar (juce::AudioProcessorValueTreeState& s)
    : state (s),
      power (parameter (s, plugin::id::bypass)),
      phaseMode (parameter (s, plugin::id::phaseMode), "Phase"),
      analogModel (parameter (s, plugin::id::analogModel), "Model"),
      mix (s, plugin::id::mix, "Mix"),
      output (s, plugin::id::outputGain, "Output", true)
{
    for (auto* c : std::initializer_list<juce::Component*> { &power, &phaseMode, &analogModel, &advancedButton, &deltaButton,
                                                             &autoGainButton, &mix, &output })
        addAndMakeVisible (c);

    // PH-2: what each mode and Quality costs (measured at 48 kHz, stereo, 8 bands; docs/measurements).
    phaseMode.setTooltip ("Minimum Phase: no latency; minimum-phase filters.\n"
                          "Natural Phase: magnitude and phase of the analog prototypes, no cramping near Nyquist; "
                          "latency 0.3 / 0.7 / 1.3 / 2.7 ms by Quality.\n"
                          "Analog Feel: curves and saturation of the selected console model; latency at High and Max.");
    phaseMode.onIndexChanged = [this] (int) { updateModelVisibility(); };
    analogModel.onIndexChanged = [this] (int index) {
        // Descriptions without trademarks (ADR-0009).
        static constexpr const char* descriptions[] = { "Analog N: Class-A transformer console EQ",
                                                        "Analog P: Passive LC program EQ with tube make-up",
                                                        "Analog S: 1980s console EQ with proportional Q" };
        analogModel.setTooltip (juce::String (descriptions[juce::jlimit (0, 2, index)])
                                + "\nBand Character: Clear = curves only, Warm = soft saturation, Sharp = hard saturation");
    };
    analogModel.onIndexChanged (analogModel.getIndex());

    advancedButton.setClickingTogglesState (true);
    advancedButton.setOnColour (colours::textSecondary); // open, not "something else is heard"
    advancedButton.onClick = [this] {
        advancedButton.setTrailingIcon (advancedButton.getToggleState() ? Icon::ChevronDown : Icon::ChevronUp);
        if (onAdvancedToggled)
            onAdvancedToggled (advancedButton.getToggleState());
    };
    setAdvancedIndicator (juce::Colours::transparentBlack, {});

    deltaButton.setTooltip ("Delta: hear only what the plug-in changes (input minus output). Monitoring only, not saved in presets.");
    deltaButton.setClickingTogglesState (true);
    deltaAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, plugin::id::delta, deltaButton);

    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setOnColour (colours::powerOn);
    autoGainAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, plugin::id::autoGain, autoGainButton);
    autoGainButton.onStateChange = [this] { updateAutoGainText(); };

    updateModelVisibility();
    updateAutoGainText();
}

void ControlBar::setAdvancedOpen (bool open)
{
    advancedButton.setToggleState (open, juce::dontSendNotification);
    advancedButton.setTrailingIcon (open ? Icon::ChevronDown : Icon::ChevronUp);
}

void ControlBar::setAdvancedIndicator (juce::Colour colour, const juce::String& details)
{
    advancedButton.setIndicator (colour);
    juce::String tip (
        "Advanced: Quality, Oversampling, Offline render, Max Lookahead, Auto Gain options and the THD+N and aliasing meters");
    if (details.isNotEmpty())
        tip << "\n" << details;
    advancedButton.setTooltip (tip);
}

void ControlBar::setAutoGainReading (float db)
{
    if (juce::approximatelyEqual (db, autoGainDb, juce::Tolerance<float>().withAbsolute (0.05f)))
        return;
    autoGainDb = db;
    updateAutoGainText();
}

void ControlBar::updateAutoGainText()
{
    const bool on = autoGainButton.getToggleState();
    const auto reading = plugin::format::decibels (autoGainDb);
    autoGainButton.setButtonText (on && ! compact ? "Auto Gain " + reading : "Auto Gain");
    autoGainButton.setTooltip ("Auto Gain: keeps the loudness of the processed signal level with the input"
                               + (on ? juce::String (" (now ") + reading + ")" : juce::String()) + ".\nWeighting and speed: Advanced.");
}

void ControlBar::setCompact (bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;
    compact = shouldBeCompact;
    phaseMode.valueTextFor = compact ? std::function<juce::String (int)> ([] (int index) {
        static const juce::StringArray shortNames { "Minimum", "Natural", "Analog" };
        return shortNames[index];
    })
                                     : nullptr;
    phaseMode.refreshText();
    updateAutoGainText();
    resized();
}

void ControlBar::updateModelVisibility()
{
    const bool visible = phaseMode.getIndex() == static_cast<int> (PhaseMode::AnalogFeel);
    if (analogModel.isVisible() != visible)
    {
        analogModel.setVisible (visible);
        resized();
        repaint();
    }
}

void ControlBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.fillRect (0, 0, getWidth(), 1);

    // Group dividers: power | processing | … | listening | output.
    const auto bounds = getLocalBounds();
    divider (g, power.getRight() + kGroupGap / 2 - 4, bounds);
    divider (g, (analogModel.isVisible() ? analogModel.getRight() : phaseMode.getRight()) + kGroupGap / 2 - 4, bounds);
    divider (g, mix.getX() - kGroupGap / 2, bounds);
}

void ControlBar::resized()
{
    auto area = getLocalBounds().reduced (10, 0).withTrimmedTop (1);
    power.setBounds (area.removeFromLeft (34).withSizeKeepingCentre (34, 34));
    area.removeFromLeft (kGroupGap - 8);

    const int valueHeight = 40;
    phaseMode.setBounds (area.removeFromLeft (compact ? 96 : 128).withSizeKeepingCentre (compact ? 96 : 128, valueHeight));
    if (analogModel.isVisible())
    {
        area.removeFromLeft (4);
        analogModel.setBounds (area.removeFromLeft (96).withSizeKeepingCentre (96, valueHeight));
    }
    area.removeFromLeft (kGroupGap - 8);
    advancedButton.setBounds (
        area.removeFromLeft (advancedButton.getIdealWidth()).withSizeKeepingCentre (advancedButton.getIdealWidth(), 28));

    output.setBounds (area.removeFromRight (compact ? 96 : 108).withSizeKeepingCentre (compact ? 96 : 108, 40));
    area.removeFromRight (8);
    mix.setBounds (area.removeFromRight (compact ? 84 : 96).withSizeKeepingCentre (compact ? 84 : 96, 40));
    area.removeFromRight (kGroupGap);
    const int autoGainWidth = compact ? 96 : 148;
    autoGainButton.setBounds (area.removeFromRight (autoGainWidth).withSizeKeepingCentre (autoGainWidth, 28));
    area.removeFromRight (8);
    deltaButton.setBounds (area.removeFromRight (deltaButton.getIdealWidth()).withSizeKeepingCentre (deltaButton.getIdealWidth(), 28));
}

// GlobalAdvancedPanel -------------------------------------------------------------------------

GlobalAdvancedPanel::GlobalAdvancedPanel (juce::AudioProcessorValueTreeState& s)
    : state (s),
      quality (parameter (s, plugin::id::quality), "Quality"),
      oversampling (parameter (s, plugin::id::oversampling), "Oversampling"),
      offline (parameter (s, plugin::id::renderOversampling), "Offline"),
      weighting (parameter (s, plugin::id::autoGainWeighting), "Weighting"),
      speed (parameter (s, plugin::id::autoGainSpeed), "Speed"),
      maxLookahead (s, plugin::id::maxLookahead, "Max Lookahead")
{
    for (auto* c :
         std::initializer_list<juce::Component*> { &quality, &oversampling, &offline, &maxLookahead, &weighting, &speed, &meters })
        addAndMakeVisible (c);

    quality.setTooltip ("Higher Quality: more accurate, more CPU and/or latency (48 kHz, stereo, 8 bands).\n"
                        "Minimum Phase: Eco/Normal 0 ms, 0.7 % CPU; High 1.0 %, Max 1.2 % with the analog magnitude (still 0 ms).\n"
                        "Natural Phase: Eco 0.3 ms, Normal 0.7 ms, High 1.3 ms, Max 2.7 ms; 0.8 - 1.6 % CPU.\n"
                        "Analog Feel: Eco/Normal 0 ms, 0.7 - 1.3 % CPU; High 2.6 ms, Max 4.7 ms "
                        "(internal 2x / 4x oversampling), 2 - 7 % CPU.\n"
                        "Spectral bands add their frame: Eco 43 ms, Normal 85 ms, High and Max 171 ms.");
    offline.setTooltip ("Oversampling of an offline render (export, bounce). Same: as in real time.\n"
                        "Real time and render keep the same latency: the shorter one is padded.");
    maxLookahead.getSlider().setTooltip ("Max Lookahead: delay of the main path that Dynamic bands may look ahead into (latency). "
                                         "Raised automatically by a band's Lookahead; lower it by hand when no longer needed.");
    weighting.setTooltip ("Auto Gain weighting: Fletcher (equal-loudness) or Digital (flat)");
    speed.setTooltip ("Auto Gain speed: how fast the correction follows the material");
    setOversamplingHint (0, 0.0f);
    // Not opaque: it fades while it slides, so what lies behind must still be painted.
}

void GlobalAdvancedPanel::setOversamplingHint (int factor, float aliasingDb)
{
    hint = factor;
    if (factor > 0)
    {
        const auto reason =
            "Aliasing " + MeterView::formatDb (aliasingDb) + ": Oversampling " + juce::String (factor) + "x would reduce it";
        oversampling.setCaption (juce::String::fromUTF8 ("Oversampling \xe2\x96\xb4"));
        oversampling.setCaptionColour (colours::accent);
        oversampling.setTooltip (reason);
    }
    else
    {
        oversampling.setCaption ("Oversampling");
        oversampling.setCaptionColour (colours::textDim);
        oversampling.setTooltip ("Oversampling in real time: less aliasing from Character, Analog Feel and cramped curves; "
                                 "more CPU and a little latency");
    }
}

juce::StringArray GlobalAdvancedPanel::nonDefaultSettings() const
{
    juce::StringArray list;
    for (const auto* b : { &quality, &oversampling, &offline, &weighting, &speed })
        if (! b->isDefault())
        {
            const auto caption = b == &weighting || b == &speed ? "Auto Gain " + b->getName().toLowerCase() : b->getName();
            list.add (caption + " " + b->getValueText());
        }
    if (auto* p = state.getRawParameterValue (plugin::id::maxLookahead); p != nullptr && p->load() > 0.0f)
        list.add ("Max Lookahead " + plugin::format::milliseconds (p->load()));
    return list;
}

void GlobalAdvancedPanel::setCompact (bool compact)
{
    juce::ignoreUnused (compact);
    resized();
}

void GlobalAdvancedPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::panelEdge);
    g.fillRect (0, 0, getWidth(), 1);

    auto title = [&g] (const juce::String& text, int x, int width) {
        g.setFont (uiFont (10.5f, true));
        g.setColour (colours::textDim);
        g.drawText (text, x + 7, 10, width, 14, juce::Justification::centredLeft, false);
    };
    title ("PROCESSING", quality.getX(), 200);
    title ("AUTO GAIN", weighting.getX(), 160);
    title ("METERS", meters.getX() - 5, 160);
    const auto bounds = getLocalBounds();
    divider (g, weighting.getX() - kGroupGap / 2, bounds);
    divider (g, meters.getX() - kGroupGap / 2, bounds);
}

void GlobalAdvancedPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (8);

    // Widths shrink together in narrow windows.
    constexpr int widths[] = { 92, 120, 84, 136, 100, 96, 176 };
    int total = 2 * kGroupGap;
    for (const int w : widths)
        total += w;
    total += 4 * 6; // gaps inside the groups
    const float scale = juce::jlimit (0.7f, 1.0f, static_cast<float> (area.getWidth()) / static_cast<float> (total));
    int index = 0;
    auto next = [&] { return juce::roundToInt (static_cast<float> (widths[index++]) * scale); };

    auto placeFull = [&] (juce::Component& c, int height) {
        const int w = next();
        c.setBounds (area.removeFromLeft (w).withSizeKeepingCentre (w, height));
    };
    placeFull (quality, 40);
    area.removeFromLeft (6);
    placeFull (oversampling, 40);
    area.removeFromLeft (6);
    placeFull (offline, 40);
    area.removeFromLeft (6);
    placeFull (maxLookahead, 40);
    area.removeFromLeft (kGroupGap);
    placeFull (weighting, 40);
    area.removeFromLeft (6);
    placeFull (speed, 40);
    area.removeFromLeft (kGroupGap);
    meters.setBounds (area.withSizeKeepingCentre (area.getWidth(), 40)); // the rest (at least its share)
}
} // namespace ssc::ui
