#include "ui/Theme.h"

#include <cmath>

namespace ssc::ui
{
namespace
{
    double relativeLuminance (juce::Colour c)
    {
        auto channel = [] (juce::uint8 v) {
            const double s = static_cast<double> (v) / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
    }

    juce::TextLayout tooltipLayout (const juce::String& text, juce::Colour colour)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.setLineSpacing (2.0f);
        s.append (text, uiFont (13.0f), colour);
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (s, 400.0f);
        return layout;
    }
} // namespace

double contrastRatio (juce::Colour a, juce::Colour b)
{
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

juce::Colour bandColour (int bandIndex)
{
    // Golden-angle hue stepping keeps neighbouring bands visually distinct.
    const float hue = std::fmod (0.08f + static_cast<float> (bandIndex) * 0.381966f, 1.0f);
    return juce::Colour::fromHSL (hue, 0.72f, 0.62f, 1.0f);
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colours::popover);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::popover);
    setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::background);
    setColour (juce::Slider::textBoxTextColourId, colours::textSecondary);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::outline);
    setColour (juce::TextEditor::backgroundColourId, colours::background);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, colours::popover);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::outline);
    setColour (juce::AlertWindow::backgroundColourId, colours::popover);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::outline);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle,
                                    float rotaryEndAngle, juce::Slider& slider)
{
    // A disabled knob (e.g. Gain of a cut) is drawn faded.
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float thickness = juce::jmax (2.5f, radius * 0.13f);
    const float arcRadius = radius - thickness * 0.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colours::outline.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar values (Gain, Output) grow from 0 in either direction.
    float from = rotaryStartAngle;
    if (static_cast<bool> (slider.getProperties()[kBipolarProperty]))
    {
        const auto zero = static_cast<float> (slider.valueToProportionOfLength (0.0));
        from = rotaryStartAngle + juce::jlimit (0.0f, 1.0f, zero) * (rotaryEndAngle - rotaryStartAngle);
    }
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The knob: a raised disc with a pointer.
    const float body = radius - thickness * 2.0f;
    g.setColour (colours::raised.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre));
    g.setColour (colours::text.withMultipliedAlpha (alpha));
    g.drawLine ({ centre.getPointOnCircumference (body * 0.25f, angle), centre.getPointOnCircumference (body * 0.92f, angle) },
                juce::jmax (1.5f, radius * 0.07f));
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (uiFont (12.0f));
    label->setColour (juce::Label::textColourId, colours::textSecondary);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundWhenEditingColourId, colours::background);
    label->setColour (juce::Label::textWhenEditingColourId, colours::text);
    label->setColour (juce::Label::outlineWhenEditingColourId, colours::accent);
    if (static_cast<bool> (slider.getProperties()[kKnobLeftProperty]))
        label->setJustificationType (juce::Justification::centredLeft);
    return label;
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (! static_cast<bool> (slider.getProperties()[kKnobLeftProperty]))
        return LookAndFeel_V4::getSliderLayout (slider);
    auto bounds = slider.getLocalBounds();
    const int knob = juce::jmin (bounds.getHeight(), 36);
    juce::Slider::SliderLayout layout;
    layout.sliderBounds = bounds.removeFromLeft (knob).withSizeKeepingCentre (knob, knob);
    bounds.removeFromLeft (4);
    layout.textBoxBounds = bounds.withTrimmedTop (bounds.getHeight() / 2).withHeight (juce::jmin (18, bounds.getHeight() / 2));
    return layout;
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (bounds, metrics::controlRadius);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, metrics::controlRadius, 1.0f);

    juce::Path arrow;
    const float cx = static_cast<float> (width) - 10.0f;
    const float cy = static_cast<float> (height) * 0.5f;
    arrow.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (13.0f);
}

juce::Font LookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::outline.withAlpha (0.6f));
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    g.setColour (on ? colours::accent.withAlpha (0.22f) : colours::panel);
    g.fillRoundedRectangle (bounds, metrics::controlRadius);
    g.setColour (on ? colours::accent : (highlighted ? colours::textDim : colours::outline));
    g.drawRoundedRectangle (bounds, metrics::controlRadius, 1.0f);
    g.setColour (on ? colours::accent : colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (tipText, colours::text);
    const int w = static_cast<int> (std::ceil (layout.getWidth())) + 18;
    const int h = static_cast<int> (std::ceil (layout.getHeight())) + 12;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 20,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 8, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
#if ! JUCE_MAC // macOS windows have their own outline
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRect (bounds, 1.0f);
#endif
    tooltipLayout (text, findColour (juce::TooltipWindow::textColourId)).draw (g, bounds.reduced (9.0f, 6.0f));
}
} // namespace ssc::ui
