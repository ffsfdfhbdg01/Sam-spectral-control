#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/Motion.h"

namespace ssc::ui
{
/**
    Button types of the interface (research §15 d7). Each type looks the same wherever it is used,
    and so do its states: hover, pressed, keyboard focus (ring), disabled (faded, no hover), on
    (tinted fill) and "non-default" (a dot in the top right corner, set with setIndicator()).
    Click targets are at least 24 × 24 px. Clicking never takes the keyboard focus from the display,
    so its shortcuts keep working.
*/

enum class Icon
{
    Undo,
    Redo,
    Previous,
    Next,
    Copy,
    Settings,
    Help,
    More,
    Power,
    Solo,
    ChevronDown,
    ChevronUp,
    ChevronRight,
    ChevronLeft,
    Close
};

/** Outline of an icon in a square area (16-unit grid). */
juce::Path iconPath (Icon icon, juce::Rectangle<float> area);

/** Draws an icon in a colour; the stroke scales with the area. */
void drawIcon (juce::Graphics&, Icon icon, juce::Rectangle<float> area, juce::Colour colour);

/** The "non-default" / warning dot in the top right corner of a control (nothing when transparent). */
void drawIndicator (juce::Graphics&, juce::Rectangle<float> controlBounds, juce::Colour colour);

/** Keyboard focus ring (2 px, >= 3:1). */
void drawFocusRing (juce::Graphics&, juce::Rectangle<float> bounds, float cornerSize);

/** Ghost icon button (Undo, preset arrows, copy, settings, help, band bypass, solo, more). */
class IconButton : public juce::Button
{
public:
    explicit IconButton (Icon icon, const juce::String& name = {});

    void setIcon (Icon newIcon);
    /** Icon and tint while the toggle state is on (e.g. amber for Solo). */
    void setOnColour (juce::Colour colour);
    /** Dot in the corner (e.g. the red lamp of a bypassed band); transparent hides it. */
    void setIndicator (juce::Colour colour);
    juce::Colour getIndicator() const noexcept { return indicator; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    juce::Colour onColour;
    juce::Colour indicator { juce::Colours::transparentBlack };
};

/** Text toggle (Auto Gain, Delta, Analyzer, Advanced): outlined when off, tinted when on. Can show a
    trailing icon and an indicator; joins a neighbour into a split button with setConnectedEdges(). */
class ChipButton : public juce::Button
{
public:
    explicit ChipButton (const juce::String& buttonText, std::optional<Icon> trailingIcon = std::nullopt);

    void setOnColour (juce::Colour colour);
    void setTrailingIcon (std::optional<Icon> icon);
    void setIndicator (juce::Colour colour);
    juce::Colour getIndicator() const noexcept { return indicator; }
    /** Width that shows the text and icon. */
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    std::optional<Icon> trailing;
    juce::Colour onColour;
    juce::Colour indicator { juce::Colours::transparentBlack };
};

/** Two to four mutually exclusive options with a sliding indicator (A/B, band mode, Abs/Rel). */
class SegmentedControl : public juce::Component, public juce::TooltipClient
{
public:
    explicit SegmentedControl (const juce::StringArray& items = {});

    void setItems (const juce::StringArray& items);
    int getNumItems() const noexcept { return items.size(); }
    const juce::String& getItemText (int index) const { return items.getReference (index); }

    void setSelectedIndex (int index, juce::NotificationType notification = juce::sendNotificationSync);
    int getSelectedIndex() const noexcept { return selected; }

    void setItemEnabled (int index, bool enabled);
    bool isItemEnabled (int index) const;
    void setItemTooltip (int index, const juce::String& tooltip);
    void setTooltip (const juce::String& tooltip) { generalTooltip = tooltip; }
    void setIndicatorColour (juce::Colour colour);

    /** Called with the new index after a click or a key press. */
    std::function<void (int)> onChange;

    juce::Rectangle<float> getSegmentBounds (int index) const;
    /** Left edge of the indicator now (it slides for 140 ms after a change). */
    float getIndicatorX() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    juce::String getTooltip() override;

private:
    int indexAt (juce::Point<float> position) const;
    void select (int index);
    void frame();

    juce::StringArray items;
    std::vector<bool> enabled;
    std::vector<juce::String> tooltips;
    juce::String generalTooltip;
    juce::Colour indicatorColour;
    int selected = 0;
    int hovered = -1;
    motion::Tween indicator;
    juce::TimedCallback timer { [this] { frame(); } };
};

/** A choice shown as caption, value and chevron, without a frame; a click opens its menu below it. */
class ValueButton : public juce::Button
{
public:
    explicit ValueButton (const juce::String& caption = {});

    void setCaption (const juce::String& newCaption);
    void setValueText (const juce::String& newValue);
    const juce::String& getValueText() const noexcept { return value; }
    /** Colour of the caption (e.g. amber while a hint is shown); textDim by default. */
    void setCaptionColour (juce::Colour colour);
    void setIndicator (juce::Colour colour);
    juce::Colour getIndicator() const noexcept { return indicator; }
    /** Width that shows caption and value without truncation. */
    int getIdealWidth() const;

    /** Fills the menu shown on click. */
    std::function<void (juce::PopupMenu&)> buildMenu;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

protected:
    using juce::Button::clicked;
    void clicked() override;

private:
    juce::String caption, value;
    juce::Colour captionColour;
    juce::Colour indicator { juce::Colours::transparentBlack };
};

/** ValueButton bound to a choice parameter of the processor (global settings). */
class ChoiceParameterButton : public ValueButton
{
public:
    ChoiceParameterButton (juce::RangedAudioParameter& parameter, const juce::String& caption);

    int getIndex() const noexcept { return index; }
    bool isDefault() const;

    /** Value text of a choice (default: the parameter's choice; e.g. short names in narrow windows). */
    std::function<juce::String (int index)> valueTextFor;
    /** Called when the parameter changes (from the menu, the host or a preset). */
    std::function<void (int index)> onIndexChanged;

    void refreshText();

private:
    juce::RangedAudioParameter& parameter;
    juce::StringArray choices;
    juce::ParameterAttachment attachment;
    int index = 0;
};
} // namespace ssc::ui
