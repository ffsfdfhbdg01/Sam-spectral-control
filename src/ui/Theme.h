#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ssc::ui
{
/** Colour and typography tokens. All UI colours come from here (VIS-7). */
namespace colours
{
    inline const juce::Colour background { 0xff0f1115 };
    inline const juce::Colour displayTop { 0xff151820 };
    inline const juce::Colour displayBottom { 0xff0c0e12 };
    inline const juce::Colour panel { 0xff171a20 };
    inline const juce::Colour panelEdge { 0xff272c35 };
    inline const juce::Colour grid { 0xff1f242c };
    inline const juce::Colour gridStrong { 0xff2c323c };
    inline const juce::Colour text { 0xffc9d1d9 };
    inline const juce::Colour textDim { 0xff79838f };
    inline const juce::Colour accent { 0xfff5a524 };
    inline const juce::Colour spectrumPre { 0xff3a414d };
    inline const juce::Colour spectrumPost { 0xffd9e4f2 };
    inline const juce::Colour powerOn { 0xff4cc38a };
    inline const juce::Colour powerOff { 0xff5a616c };
} // namespace colours

/** 24 distinct band colours, evenly spaced in hue with matched lightness. */
juce::Colour bandColour (int bandIndex);

juce::Font uiFont (float height, bool bold = false);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};
} // namespace ssc::ui
