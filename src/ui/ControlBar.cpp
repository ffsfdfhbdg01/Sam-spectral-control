#include "ui/ControlBar.h"

#include "plugin/Parameters.h"
#include "ui/Theme.h"

namespace ssc::ui
{
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
        g.setColour (colour.withAlpha (0.18f));
        g.fillEllipse (circle.expanded (3.0f));
    }

    juce::Path arc;
    arc.addCentredArc (circle.getCentreX(), circle.getCentreY(), size * 0.36f, size * 0.36f, 0.0f, 0.65f,
                       juce::MathConstants<float>::twoPi - 0.65f, true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (circle.getCentreX(), circle.getY() + size * 0.08f, circle.getCentreX(), circle.getCentreY(), 2.0f);
}

void PowerButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f);
}

ControlBar::ControlBar (juce::AudioProcessorValueTreeState& s)
    : state (s),
      power (*s.getParameter (plugin::id::bypass))
{
    addAndMakeVisible (power);

    setupCombo (phaseMode, "Phase", plugin::id::phaseMode);
    setupCombo (analogModel, "Model", plugin::id::analogModel);
    setupCombo (quality, "Quality", plugin::id::quality);
    setupCombo (oversampling, "Oversampling", plugin::id::oversampling);
    setupCombo (renderOversampling, "Offline", plugin::id::renderOversampling);
    renderOversampling.box.setTooltip ("Oversampling of an offline render (export, bounce). Same: as in real time.\n"
                                       "Real time and render keep the same latency: the shorter one is padded.");
    setupCombo (autoGainWeighting, "Weighting", plugin::id::autoGainWeighting);
    setupCombo (autoGainSpeed, "Speed", plugin::id::autoGainSpeed);

    addAndMakeVisible (autoGainButton);
    autoGainAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, plugin::id::autoGain, autoGainButton);

    setupKnob (mix, "Mix", plugin::id::mix);
    setupKnob (output, "Output", plugin::id::outputGain);

    addAndMakeVisible (meters);
    meters.onOversamplingHint = [this] (int factor, float aliasingDb) { setOversamplingHint (factor, aliasingDb); };

    phaseMode.box.onChange = [this] { updateEnablement(); };
    analogModel.box.onChange = [this] { updateModelTooltip(); };
    autoGainButton.onStateChange = [this] { updateEnablement(); };
    updateEnablement();
    updateModelTooltip();
}

void ControlBar::setupCombo (LabelledCombo& c, const juce::String& title, const juce::String& paramId)
{
    c.label.setText (title, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, colours::textDim);
    c.label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (c.label);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        c.box.addItemList (choice->choices, 1);
    addAndMakeVisible (c.box);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, c.box);
}

void ControlBar::setupKnob (LabelledKnob& k, const juce::String& title, const juce::String& paramId)
{
    k.label.setText (title, juce::dontSendNotification);
    k.label.setColour (juce::Label::textColourId, colours::textDim);
    k.label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (k.label);

    k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, k.slider);
}

void ControlBar::updateEnablement()
{
    analogModel.box.setEnabled (phaseMode.box.getSelectedItemIndex() == static_cast<int> (PhaseMode::AnalogFeel));
    const bool autoGainOn = autoGainButton.getToggleState();
    autoGainWeighting.box.setEnabled (autoGainOn);
    autoGainSpeed.box.setEnabled (autoGainOn);
}

void ControlBar::updateModelTooltip()
{
    // Descriptions without trademarks (ADR-0009).
    static constexpr const char* descriptions[] = { "Analog N: Class-A transformer console EQ",
                                                    "Analog P: Passive LC program EQ with tube make-up",
                                                    "Analog S: 1980s console EQ with proportional Q" };
    const int index = juce::jlimit (0, 2, analogModel.box.getSelectedItemIndex());
    analogModel.box.setTooltip (juce::String (descriptions[index])
                                + "\nBand Character: Clear = curves only, Warm = soft saturation, Sharp = hard saturation");
}

void ControlBar::setOversamplingHint (int factor, float aliasingDb)
{
    if (factor > 0)
    {
        const auto reason =
            "Aliasing " + MeterView::formatDb (aliasingDb) + ": Oversampling " + juce::String (factor) + "x would reduce it";
        oversampling.label.setText (juce::String::fromUTF8 ("Oversampling \xe2\x96\xb4"), juce::dontSendNotification);
        oversampling.label.setColour (juce::Label::textColourId, colours::accent);
        oversampling.label.setTooltip (reason);
        oversampling.box.setTooltip (reason);
    }
    else
    {
        oversampling.label.setText ("Oversampling", juce::dontSendNotification);
        oversampling.label.setColour (juce::Label::textColourId, colours::textDim);
        oversampling.label.setTooltip ({});
        oversampling.box.setTooltip ({});
    }
}

void ControlBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void ControlBar::resized()
{
    constexpr int gap = 8;
    auto area = getLocalBounds().reduced (gap, 6);
    power.setBounds (area.removeFromLeft (area.getHeight()));
    area.removeFromLeft (gap);

    // Mix and Output keep their place on the right; the settings share the rest and shrink
    // proportionally in narrow windows.
    auto right = area.removeFromRight (juce::jmin (250, area.getWidth() / 4));
    const int knobWidth = (right.getWidth() - gap) / 2;
    auto placeKnob = [&right, knobWidth] (LabelledKnob& k) {
        auto column = right.removeFromLeft (knobWidth);
        k.label.setBounds (column.removeFromTop (14));
        k.slider.setBounds (column.removeFromTop (30));
        right.removeFromLeft (gap);
    };
    placeKnob (mix);
    placeKnob (output);
    area.removeFromRight (gap);

    // The meters (MTR-4): small, between the settings and Mix/Output.
    meters.setBounds (area.removeFromRight (juce::jmin (150, area.getWidth() / 5)));
    area.removeFromRight (gap);

    constexpr int widths[] = { 130, 96, 84, 96, 72, 92, 90, 96 };
    int total = 0;
    for (int w : widths)
        total += w + gap;
    const float scale = juce::jlimit (0.5f, 1.0f, static_cast<float> (area.getWidth()) / static_cast<float> (total));
    int index = 0;
    auto nextWidth = [&] { return juce::roundToInt (static_cast<float> (widths[index++]) * scale); };

    auto placeCombo = [&] (LabelledCombo& c) {
        auto column = area.removeFromLeft (nextWidth());
        c.label.setBounds (column.removeFromTop (14));
        c.box.setBounds (column.removeFromTop (24));
        area.removeFromLeft (gap);
    };

    placeCombo (phaseMode);
    placeCombo (analogModel);
    placeCombo (quality);
    placeCombo (oversampling);
    placeCombo (renderOversampling);
    {
        auto column = area.removeFromLeft (nextWidth());
        column.removeFromTop (14);
        autoGainButton.setBounds (column.removeFromTop (24));
        area.removeFromLeft (gap);
    }
    placeCombo (autoGainWeighting);
    placeCombo (autoGainSpeed);
}
} // namespace ssc::ui
