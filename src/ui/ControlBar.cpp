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
    arc.addCentredArc (circle.getCentreX(), circle.getCentreY(), size * 0.36f, size * 0.36f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
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
    setupCombo (autoGainWeighting, "Weighting", plugin::id::autoGainWeighting);
    setupCombo (autoGainSpeed, "Speed", plugin::id::autoGainSpeed);

    addAndMakeVisible (autoGainButton);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, plugin::id::autoGain, autoGainButton);

    setupKnob (mix, "Mix", plugin::id::mix);
    setupKnob (output, "Output", plugin::id::outputGain);

    phaseMode.box.onChange = [this] { updateEnablement(); };
    autoGainButton.onStateChange = [this] { updateEnablement(); };
    updateEnablement();
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

void ControlBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void ControlBar::resized()
{
    auto area = getLocalBounds().reduced (8, 6);
    power.setBounds (area.removeFromLeft (area.getHeight()));
    area.removeFromLeft (8);

    auto placeCombo = [&area] (LabelledCombo& c, int width) {
        auto column = area.removeFromLeft (width);
        c.label.setBounds (column.removeFromTop (14));
        c.box.setBounds (column.removeFromTop (24));
        area.removeFromLeft (8);
    };

    placeCombo (phaseMode, 130);
    placeCombo (analogModel, 96);
    placeCombo (quality, 84);
    placeCombo (oversampling, 96);

    {
        auto column = area.removeFromLeft (92);
        column.removeFromTop (14);
        autoGainButton.setBounds (column.removeFromTop (24));
        area.removeFromLeft (8);
    }
    placeCombo (autoGainWeighting, 90);
    placeCombo (autoGainSpeed, 96);

    auto right = area.removeFromRight (250);
    auto placeKnob = [&right] (LabelledKnob& k) {
        auto column = right.removeFromLeft (120);
        k.label.setBounds (column.removeFromTop (14));
        k.slider.setBounds (column.removeFromTop (30));
        right.removeFromLeft (8);
    };
    placeKnob (mix);
    placeKnob (output);
}
} // namespace ssc::ui
