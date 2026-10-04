#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace ssc::ui
{
/** Power button: lit when the plugin is ON, i.e. when the host bypass parameter is off. */
class PowerButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PowerButton (juce::RangedAudioParameter& bypassParameter);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::ParameterAttachment attachment;
    bool bypassed = false;
};

/**
    Bottom bar with global settings: power, phase mode, analog model, quality, oversampling,
    auto gain, mix and output.
*/
class ControlBar final : public juce::Component
{
public:
    explicit ControlBar (juce::AudioProcessorValueTreeState& state);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct LabelledCombo
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct LabelledKnob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupCombo (LabelledCombo&, const juce::String& title, const juce::String& paramId);
    void setupKnob (LabelledKnob&, const juce::String& title, const juce::String& paramId);
    void updateEnablement();
    void updateModelTooltip();

    juce::AudioProcessorValueTreeState& state;

    PowerButton power;
    LabelledCombo phaseMode, analogModel, quality, oversampling, autoGainWeighting, autoGainSpeed;
    juce::ToggleButton autoGainButton { "Auto Gain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;
    LabelledKnob mix, output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlBar)
};
} // namespace ssc::ui
