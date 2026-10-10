#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/Controls.h"
#include "ui/MeterView.h"

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

/** Knob of a global parameter: the knob on the left, caption and value (click to type) to its right. */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
                   bool bipolar = false);

    juce::Slider& getSlider() noexcept { return slider; }
    void resized() override;

private:
    juce::Label caption;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
    Bottom bar (research §15 d5): what is changed often. Power | Phase (and the analog Model in
    Analog Feel) | Advanced ▴ … Delta, Auto Gain | Mix, Output. Everything set once per session
    lives in the global Advanced panel that slides up from the bar.
*/
class ControlBar final : public juce::Component
{
public:
    explicit ControlBar (juce::AudioProcessorValueTreeState& state);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** The Advanced toggle; the panel belongs to the editor. */
    bool isAdvancedOpen() const noexcept { return advancedButton.getToggleState(); }
    void setAdvancedOpen (bool open);
    std::function<void (bool open)> onAdvancedToggled;

    /** Dot on Advanced (a setting inside differs from its default, a hint or a warning) and its tooltip. */
    void setAdvancedIndicator (juce::Colour colour, const juce::String& details);

    /** Current Auto Gain correction, shown on its button while it is on. */
    void setAutoGainReading (float db);

    /** Short value texts (narrow windows). */
    void setCompact (bool compact);

private:
    void updateModelVisibility();
    void updateAutoGainText();

    juce::AudioProcessorValueTreeState& state;
    PowerButton power;
    ChoiceParameterButton phaseMode, analogModel;
    ChipButton advancedButton { "Advanced", Icon::ChevronUp };
    ChipButton deltaButton { "Delta" }, autoGainButton { "Auto Gain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttachment, autoGainAttachment;
    ParameterKnob mix, output;
    float autoGainDb = 0.0f;
    bool compact = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlBar)
};

/**
    Global Advanced panel (research §15 d5–d6): it slides up from the bottom bar. Processing:
    Quality, Oversampling (real time), Offline (render) oversampling, Max Lookahead. Auto Gain:
    weighting and speed. Meters: THD+N and aliasing (MTR-4; click for the warning thresholds).
*/
class GlobalAdvancedPanel final : public juce::Component
{
public:
    explicit GlobalAdvancedPanel (juce::AudioProcessorValueTreeState& state);

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kHeight = 84;

    MeterView& getMeterView() noexcept { return meters; }

    /** OS-5: marks the Oversampling control while a higher factor would reduce the aliasing
        (factor > 0), with the reason in its tooltip; never changes the setting. */
    void setOversamplingHint (int factor, float aliasingDb);
    int getOversamplingHint() const noexcept { return hint; }

    /** Settings that differ from their defaults, e.g. "Quality High", "Offline 8x" (empty: none). */
    juce::StringArray nonDefaultSettings() const;

    /** Short value texts (narrow windows). */
    void setCompact (bool compact);

private:
    juce::AudioProcessorValueTreeState& state;
    ChoiceParameterButton quality, oversampling, offline, weighting, speed;
    ParameterKnob maxLookahead;
    MeterView meters;
    int hint = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalAdvancedPanel)
};
} // namespace ssc::ui
