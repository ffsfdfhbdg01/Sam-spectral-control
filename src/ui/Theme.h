#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ssc::ui
{
/** Colour and typography tokens. All UI colours come from here (VIS-7, research §15 d8). */
namespace colours
{
    // Surfaces: higher layers are lighter (dark theme elevation).
    inline const juce::Colour background { 0xff0f1115 };    ///< window, header
    inline const juce::Colour displayTop { 0xff151820 };    ///< display gradient …
    inline const juce::Colour displayBottom { 0xff0c0e12 }; ///< … to its bottom
    inline const juce::Colour panel { 0xff1b1f26 };         ///< layer 1: band panel, bottom bar, drawers
    inline const juce::Colour popover { 0xff232831 };       ///< layer 2: Advanced flyout, menus, tooltips
    inline const juce::Colour raised { 0xff323945 };        ///< selected segment, pressed controls
    inline const juce::Colour panelEdge { 0xff2b313b };     ///< dividers and panel outlines (decorative)
    inline const juce::Colour outline { 0xff4f5864 };       ///< control outlines and knob tracks: >= 2:1 on both layers
    inline const juce::Colour grid { 0xff1f242c };
    inline const juce::Colour gridStrong { 0xff2c323c };

    // Text in three levels of importance (>= 4.5:1 on panel and popover).
    inline const juce::Colour text { 0xffe6ebf0 };          ///< primary
    inline const juce::Colour textSecondary { 0xffa3adb8 }; ///< values
    inline const juce::Colour textDim { 0xff8a94a0 };       ///< captions

    // Semantic colours (always with a shape or text, never colour alone).
    inline const juce::Colour accent { 0xfff5a524 };  ///< amber: you hear something else than the normal path; notices
    inline const juce::Colour powerOn { 0xff4cc38a }; ///< processing on
    inline const juce::Colour powerOff { 0xff5a616c };
    inline const juce::Colour lamp { 0xffff5c5c };         ///< band bypassed
    inline const juce::Colour warning { 0xffff6b5a };      ///< meters above their warning threshold (MTR-4)
    inline const juce::Colour phaseOverlay { 0xff6fc9f2 }; ///< phase / group delay of the whole EQ (PR-4)

    // Display content.
    inline const juce::Colour spectrumPre { 0xff3a414d };
    inline const juce::Colour spectrumPost { 0xffd9e4f2 };
    inline const juce::Colour glowLow { 0xffff9a2e };       ///< activity glow: amber …
    inline const juce::Colour glowHigh { 0xfffff1dc };      ///< … to warm white (VIS-2)
    inline const juce::Colour resonanceHint { 0xffc08cff }; ///< resonance hints: violet, apart from the amber glow (VIS-4)
    inline const juce::Colour curve { 0xfff1f4f8 };
} // namespace colours

/** Spacing and radii (4 px grid). */
namespace metrics
{
    inline constexpr float controlRadius = 5.0f;
    inline constexpr float panelRadius = 9.0f;
    inline constexpr int targetSize = 24; ///< smallest click target (WCAG 2.2, 2.5.8)
} // namespace metrics

/** WCAG 2.x contrast ratio of two opaque colours (1…21). */
double contrastRatio (juce::Colour a, juce::Colour b);

/** 24 distinct band colours, evenly spaced in hue with matched lightness. */
juce::Colour bandColour (int bandIndex);

juce::Font uiFont (float height, bool bold = false);

/** Slider property: the value arc starts at 0 (Gain, Output) instead of the start of the range. */
inline const juce::Identifier kBipolarProperty { "sscBipolar" };

/** Slider property: the knob on the left, the value text at the lower right (Mix, Output). */
inline const juce::Identifier kKnobLeftProperty { "sscKnobLeft" };

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};
} // namespace ssc::ui
