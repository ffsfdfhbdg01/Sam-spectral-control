#include "ui/Controls.h"

#include <cmath>

#include "ui/Theme.h"

namespace ssc::ui
{
namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    /** Maps the 16-unit icon grid into a square area. */
    struct Grid
    {
        explicit Grid (juce::Rectangle<float> area)
        {
            const float size = juce::jmin (area.getWidth(), area.getHeight());
            origin = area.withSizeKeepingCentre (size, size).getTopLeft();
            scale = size / 16.0f;
        }
        juce::Point<float> operator() (float x, float y) const { return origin + juce::Point<float> (x * scale, y * scale); }
        float operator() (float length) const { return length * scale; }

        juce::Point<float> origin;
        float scale = 1.0f;
    };

    void chevron (juce::Path& p, const Grid& g, juce::Point<float> a, juce::Point<float> b, juce::Point<float> c)
    {
        p.startNewSubPath (g (a.x, a.y));
        p.lineTo (g (b.x, b.y));
        p.lineTo (g (c.x, c.y));
    }

    void solidCups (juce::Path& p, const Grid& g)
    {
        p.addRoundedRectangle (juce::Rectangle<float> (g (1.6f, 9.2f), g (5.0f, 14.4f)), g (1.0f));
        p.addRoundedRectangle (juce::Rectangle<float> (g (11.0f, 9.2f), g (14.4f, 14.4f)), g (1.0f));
    }

    juce::Path roundedOutline (juce::Rectangle<float> b, float radius, int connectedEdges)
    {
        using CF = juce::Button::ConnectedEdgeFlags;
        const bool left = (connectedEdges & CF::ConnectedOnLeft) != 0;
        const bool right = (connectedEdges & CF::ConnectedOnRight) != 0;
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), radius, radius, ! left, ! right, ! left, ! right);
        return p;
    }

    void setUpButton (juce::Button& b)
    {
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
} // namespace

juce::Path iconPath (Icon icon, juce::Rectangle<float> area)
{
    const Grid g (area);
    juce::Path p;
    switch (icon)
    {
        case Icon::Undo:
        case Icon::Redo:
        {
            p.startNewSubPath (g (4.0f, 6.0f));
            p.lineTo (g (10.0f, 6.0f));
            p.cubicTo (g (14.6f, 6.0f), g (14.6f, 13.0f), g (10.0f, 13.0f));
            p.lineTo (g (6.5f, 13.0f));
            chevron (p, g, { 6.8f, 3.2f }, { 4.0f, 6.0f }, { 6.8f, 8.8f });
            if (icon == Icon::Redo)
                p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, area.getCentreX(), area.getCentreY()));
            break;
        }
        case Icon::Previous: chevron (p, g, { 10.0f, 3.5f }, { 5.5f, 8.0f }, { 10.0f, 12.5f }); break;
        case Icon::Next: chevron (p, g, { 6.0f, 3.5f }, { 10.5f, 8.0f }, { 6.0f, 12.5f }); break;
        case Icon::ChevronDown: chevron (p, g, { 4.5f, 6.2f }, { 8.0f, 9.8f }, { 11.5f, 6.2f }); break;
        case Icon::ChevronUp: chevron (p, g, { 4.5f, 9.8f }, { 8.0f, 6.2f }, { 11.5f, 9.8f }); break;
        case Icon::ChevronRight: chevron (p, g, { 6.2f, 4.5f }, { 9.8f, 8.0f }, { 6.2f, 11.5f }); break;
        case Icon::ChevronLeft: chevron (p, g, { 9.8f, 4.5f }, { 6.2f, 8.0f }, { 9.8f, 11.5f }); break;
        case Icon::Copy:
        {
            p.addRoundedRectangle (juce::Rectangle<float> (g (5.6f, 5.6f), g (13.6f, 13.6f)), g (1.6f));
            p.startNewSubPath (g (2.6f, 10.4f));
            p.lineTo (g (2.6f, 4.2f));
            p.quadraticTo (g (2.6f, 2.6f), g (4.2f, 2.6f));
            p.lineTo (g (10.4f, 2.6f));
            break;
        }
        case Icon::Settings:
        {
            constexpr int teeth = 8;
            const float segment = kTwoPi / static_cast<float> (teeth);
            auto polar = [&g] (float radius, float angle) {
                return g (8.0f + radius * std::sin (angle), 8.0f - radius * std::cos (angle));
            };
            for (int i = 0; i < teeth; ++i)
            {
                const float a = static_cast<float> (i) * segment - segment * 0.5f;
                const auto first = polar (5.0f, a + segment * 0.2f);
                if (i == 0)
                    p.startNewSubPath (first);
                else
                    p.lineTo (first);
                p.lineTo (polar (7.0f, a + segment * 0.32f));
                p.lineTo (polar (7.0f, a + segment * 0.68f));
                p.lineTo (polar (5.0f, a + segment * 0.8f));
            }
            p.closeSubPath();
            p.addEllipse (juce::Rectangle<float> (g (5.6f, 5.6f), g (10.4f, 10.4f)));
            break;
        }
        case Icon::Help: p.addEllipse (juce::Rectangle<float> (g (1.4f, 1.4f), g (14.6f, 14.6f))); break;
        case Icon::More:
            for (const float x : { 3.4f, 8.0f, 12.6f })
                p.addEllipse (juce::Rectangle<float> (g (2.6f), g (2.6f)).withCentre (g (x, 8.0f)));
            break;
        case Icon::Power:
        {
            const auto c = g (8.0f, 8.8f);
            p.addCentredArc (c.x, c.y, g (5.6f), g (5.6f), 0.0f, 0.7f, kTwoPi - 0.7f, true);
            p.startNewSubPath (g (8.0f, 1.6f));
            p.lineTo (g (8.0f, 7.6f));
            break;
        }
        case Icon::Solo:
        {
            const auto c = g (8.0f, 9.6f);
            p.addCentredArc (c.x, c.y, g (5.6f), g (5.6f), 0.0f, -1.95f, 1.95f, true);
            solidCups (p, g);
            break;
        }
        case Icon::Close:
            p.startNewSubPath (g (4.0f, 4.0f));
            p.lineTo (g (12.0f, 12.0f));
            p.startNewSubPath (g (12.0f, 4.0f));
            p.lineTo (g (4.0f, 12.0f));
            break;
    }
    return p;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const Grid grid (area);
    const float stroke = juce::jmax (1.25f, grid (1.45f));
    const juce::PathStrokeType type (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colour);

    switch (icon)
    {
        case Icon::More:
        {
            for (const float x : { 3.4f, 8.0f, 12.6f })
                g.fillEllipse (juce::Rectangle<float> (grid (2.6f), grid (2.6f)).withCentre (grid (x, 8.0f)));
            return;
        }
        case Icon::Solo:
        {
            juce::Path band;
            const auto c = grid (8.0f, 9.6f);
            band.addCentredArc (c.x, c.y, grid (5.6f), grid (5.6f), 0.0f, -1.95f, 1.95f, true);
            g.strokePath (band, type);
            juce::Path cups;
            solidCups (cups, grid);
            g.fillPath (cups);
            return;
        }
        case Icon::Help:
        {
            g.strokePath (iconPath (icon, area), type);
            g.setFont (uiFont (grid (11.0f), true));
            g.drawText ("?", juce::Rectangle<float> (grid (0.0f, 0.4f), grid (16.0f, 16.4f)), juce::Justification::centred, false);
            return;
        }
        case Icon::Undo:
        case Icon::Redo:
        case Icon::Previous:
        case Icon::Next:
        case Icon::Copy:
        case Icon::Settings:
        case Icon::Power:
        case Icon::ChevronDown:
        case Icon::ChevronUp:
        case Icon::ChevronRight:
        case Icon::ChevronLeft:
        case Icon::Close: break;
    }
    g.strokePath (iconPath (icon, area), type);
}

void drawIndicator (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    if (colour.isTransparent())
        return;
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getRight() - 4.5f, bounds.getY() + 4.5f });
    g.setColour (colours::background);
    g.fillEllipse (dot.expanded (1.5f));
    g.setColour (colour);
    g.fillEllipse (dot);
}

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerSize)
{
    g.setColour (colours::text.withAlpha (0.75f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), cornerSize, 2.0f);
}

// IconButton ----------------------------------------------------------------------------------

IconButton::IconButton (Icon i, const juce::String& name)
    : juce::Button (name),
      icon (i),
      onColour (colours::accent)
{
    setUpButton (*this);
}

void IconButton::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void IconButton::setOnColour (juce::Colour colour)
{
    onColour = colour;
    repaint();
}

void IconButton::setIndicator (juce::Colour colour)
{
    if (indicator != colour)
    {
        indicator = colour;
        repaint();
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool enabled = isEnabled();
    const bool on = getToggleState();
    if (on)
    {
        g.setColour (onColour.withAlpha (0.16f));
        g.fillRoundedRectangle (bounds, metrics::controlRadius);
    }
    if (enabled && (down || highlighted))
    {
        g.setColour (colours::text.withAlpha (down ? 0.12f : 0.07f));
        g.fillRoundedRectangle (bounds, metrics::controlRadius);
    }

    const auto colour =
        ! enabled ? colours::textDim.withAlpha (0.45f) : (on ? onColour : (highlighted ? colours::text : colours::textSecondary));
    const float size = juce::jlimit (12.0f, 18.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.6f);
    drawIcon (g, icon, bounds.withSizeKeepingCentre (size, size), colour);
    drawIndicator (g, bounds, indicator);
    if (hasKeyboardFocus (false))
        drawFocusRing (g, bounds, metrics::controlRadius);
}

// ChipButton ----------------------------------------------------------------------------------

ChipButton::ChipButton (const juce::String& buttonText, std::optional<Icon> trailingIcon)
    : juce::Button (buttonText),
      trailing (trailingIcon),
      onColour (colours::accent)
{
    setUpButton (*this);
}

void ChipButton::setOnColour (juce::Colour colour)
{
    onColour = colour;
    repaint();
}

void ChipButton::setTrailingIcon (std::optional<Icon> icon)
{
    if (trailing != icon)
    {
        trailing = icon;
        repaint();
    }
}

void ChipButton::setIndicator (juce::Colour colour)
{
    if (indicator != colour)
    {
        indicator = colour;
        repaint();
    }
}

int ChipButton::getIdealWidth() const
{
    const int textWidth = getButtonText().isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (uiFont (13.0f), getButtonText());
    return textWidth + (trailing.has_value() ? (textWidth > 0 ? 30 : 24) : 22);
}

void ChipButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool enabled = isEnabled();
    const bool on = getToggleState();
    const auto shape = roundedOutline (bounds, metrics::controlRadius, getConnectedEdgeFlags());

    if (on)
    {
        g.setColour (onColour.withAlpha (0.2f));
        g.fillPath (shape);
    }
    if (enabled && (down || highlighted))
    {
        g.setColour (colours::text.withAlpha (down ? 0.12f : 0.06f));
        g.fillPath (shape);
    }
    g.setColour (on ? onColour.withAlpha (enabled ? 1.0f : 0.5f) : colours::outline.withAlpha (enabled ? 1.0f : 0.5f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    const auto colour = ! enabled ? colours::textDim.withAlpha (0.5f) : (on ? onColour : colours::text);
    auto area = getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    if (trailing.has_value())
    {
        const auto iconArea = area.removeFromRight (12.0f).withSizeKeepingCentre (12.0f, 12.0f);
        drawIcon (g, *trailing, getButtonText().isEmpty() ? bounds.withSizeKeepingCentre (12.0f, 12.0f) : iconArea,
                  on ? onColour : colours::textSecondary.withAlpha (enabled ? 1.0f : 0.5f));
        area.removeFromRight (4.0f);
    }
    if (getButtonText().isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (getButtonText(), area.toNearestInt(),
                          trailing.has_value() ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.8f);
    }
    drawIndicator (g, bounds, indicator);
    if (hasKeyboardFocus (false))
        drawFocusRing (g, bounds, metrics::controlRadius);
}

// SegmentedControl ----------------------------------------------------------------------------

SegmentedControl::SegmentedControl (const juce::StringArray& newItems)
    : indicatorColour (colours::raised)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setItems (newItems);
}

void SegmentedControl::setItems (const juce::StringArray& newItems)
{
    items = newItems;
    enabled.assign (static_cast<std::size_t> (items.size()), true);
    tooltips.assign (static_cast<std::size_t> (items.size()), {});
    selected = juce::jlimit (0, juce::jmax (0, items.size() - 1), selected);
    indicator.snap (getSegmentBounds (selected).getX());
    repaint();
}

void SegmentedControl::setSelectedIndex (int index, juce::NotificationType notification)
{
    index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);
    if (index == selected)
        return;
    selected = index;
    const double duration = isShowing() ? motion::moveDuration (motion::levelFor (*this), 140.0) : 0.0;
    indicator.animateTo (getSegmentBounds (selected).getX(), motion::now(), duration, motion::standard);
    if (duration > 0.0)
        timer.startTimerHz (60);
    repaint();
    if (notification != juce::dontSendNotification && onChange)
        onChange (selected);
}

void SegmentedControl::setItemEnabled (int index, bool isEnabled)
{
    if (index >= 0 && index < items.size() && enabled[static_cast<std::size_t> (index)] != isEnabled)
    {
        enabled[static_cast<std::size_t> (index)] = isEnabled;
        repaint();
    }
}

bool SegmentedControl::isItemEnabled (int index) const
{
    return index >= 0 && index < items.size() && enabled[static_cast<std::size_t> (index)];
}

void SegmentedControl::setItemTooltip (int index, const juce::String& tooltip)
{
    if (index >= 0 && index < items.size())
        tooltips[static_cast<std::size_t> (index)] = tooltip;
}

void SegmentedControl::setIndicatorColour (juce::Colour colour)
{
    indicatorColour = colour;
    repaint();
}

juce::Rectangle<float> SegmentedControl::getSegmentBounds (int index) const
{
    const int n = juce::jmax (1, items.size());
    const auto inner = getLocalBounds().toFloat().reduced (2.0f);
    const float width = inner.getWidth() / static_cast<float> (n);
    return { inner.getX() + width * static_cast<float> (index), inner.getY(), width, inner.getHeight() };
}

float SegmentedControl::getIndicatorX() const
{
    return indicator.valueAt (motion::now());
}

void SegmentedControl::resized()
{
    indicator.snap (getSegmentBounds (selected).getX());
}

int SegmentedControl::indexAt (juce::Point<float> position) const
{
    for (int i = 0; i < items.size(); ++i)
        if (getSegmentBounds (i).expanded (1.0f, 2.0f).contains (position))
            return i;
    return -1;
}

void SegmentedControl::select (int index)
{
    if (isEnabled() && isItemEnabled (index))
        setSelectedIndex (index, juce::sendNotificationSync);
}

void SegmentedControl::frame()
{
    repaint();
    if (! indicator.isRunning (motion::now()))
        timer.stopTimer();
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool active = isEnabled();
    g.setColour (colours::background.withAlpha (0.7f));
    g.fillRoundedRectangle (bounds, metrics::controlRadius);
    g.setColour (colours::outline.withAlpha (active ? 1.0f : 0.5f));
    g.drawRoundedRectangle (bounds, metrics::controlRadius, 1.0f);

    if (items.isEmpty())
        return;

    auto segment = getSegmentBounds (selected);
    segment.setX (getIndicatorX());
    g.setColour (indicatorColour.withMultipliedAlpha (active ? 1.0f : 0.5f));
    g.fillRoundedRectangle (segment, metrics::controlRadius - 1.5f);

    g.setFont (uiFont (12.5f));
    for (int i = 0; i < items.size(); ++i)
    {
        const bool on = i == selected;
        const bool itemEnabled = active && enabled[static_cast<std::size_t> (i)];
        auto colour = on ? colours::text : (i == hovered && itemEnabled ? colours::text : colours::textSecondary);
        if (! itemEnabled)
            colour = colours::textDim.withAlpha (on ? 0.7f : 0.45f);
        g.setColour (colour);
        g.drawFittedText (items[i], getSegmentBounds (i).reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
    }
    if (hasKeyboardFocus (false))
        drawFocusRing (g, bounds, metrics::controlRadius);
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    select (indexAt (e.position));
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int index = indexAt (e.position);
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

bool SegmentedControl::keyPressed (const juce::KeyPress& key)
{
    const int step = key.isKeyCode (juce::KeyPress::rightKey) ? 1 : (key.isKeyCode (juce::KeyPress::leftKey) ? -1 : 0);
    if (step == 0)
        return false;
    for (int i = selected + step; i >= 0 && i < items.size(); i += step)
        if (isItemEnabled (i))
        {
            select (i);
            break;
        }
    return true;
}

juce::String SegmentedControl::getTooltip()
{
    const int index = indexAt (getMouseXYRelative().toFloat());
    if (index >= 0 && tooltips[static_cast<std::size_t> (index)].isNotEmpty())
        return tooltips[static_cast<std::size_t> (index)];
    return generalTooltip;
}

// ValueButton ---------------------------------------------------------------------------------

ValueButton::ValueButton (const juce::String& captionText)
    : juce::Button (captionText),
      caption (captionText),
      captionColour (colours::textDim)
{
    setUpButton (*this);
}

void ValueButton::setCaption (const juce::String& newCaption)
{
    if (caption != newCaption)
    {
        caption = newCaption;
        repaint();
    }
}

void ValueButton::setValueText (const juce::String& newValue)
{
    if (value != newValue)
    {
        value = newValue;
        repaint();
    }
}

void ValueButton::setCaptionColour (juce::Colour colour)
{
    if (captionColour != colour)
    {
        captionColour = colour;
        repaint();
    }
}

void ValueButton::setIndicator (juce::Colour colour)
{
    if (indicator != colour)
    {
        indicator = colour;
        repaint();
    }
}

int ValueButton::getIdealWidth() const
{
    const int captionWidth = caption.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f), caption);
    const int valueWidth = juce::GlyphArrangement::getStringWidthInt (uiFont (13.0f), value) + 16;
    return juce::jmax (captionWidth, valueWidth) + 14;
}

void ValueButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool enabled = isEnabled();
    if (enabled && (down || highlighted))
    {
        g.setColour (colours::text.withAlpha (down ? 0.12f : 0.06f));
        g.fillRoundedRectangle (bounds, metrics::controlRadius);
    }

    const float fade = enabled ? 1.0f : 0.4f;
    auto area = getLocalBounds().reduced (7, 0);
    if (caption.isNotEmpty())
    {
        auto captionArea = area.removeFromTop (juce::jmin (15, area.getHeight() / 2)).withTrimmedTop (2);
        g.setColour (captionColour.withMultipliedAlpha (fade));
        g.setFont (uiFont (11.0f));
        g.drawFittedText (caption, captionArea, juce::Justification::bottomLeft, 1, 0.9f);
    }

    // The chevron follows the value, so the pair reads as one control.
    const auto font = uiFont (13.0f);
    const int textWidth = juce::jmin (area.getWidth() - 14, juce::GlyphArrangement::getStringWidthInt (font, value) + 1);
    g.setFont (font);
    g.setColour (colours::text.withMultipliedAlpha (fade));
    g.drawText (value, area.withWidth (textWidth), juce::Justification::centredLeft, true);
    const auto chevronArea =
        juce::Rectangle<float> (11.0f, 11.0f)
            .withCentre ({ static_cast<float> (area.getX() + textWidth) + 8.5f, static_cast<float> (area.getCentreY()) });
    drawIcon (g, Icon::ChevronDown, chevronArea, (highlighted && enabled ? colours::text : colours::textDim).withMultipliedAlpha (fade));

    drawIndicator (g, bounds, indicator);
    if (hasKeyboardFocus (false))
        drawFocusRing (g, bounds, metrics::controlRadius);
}

void ValueButton::clicked()
{
    if (! buildMenu)
        return;
    juce::PopupMenu menu;
    buildMenu (menu);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()));
}

// ChoiceParameterButton -----------------------------------------------------------------------

ChoiceParameterButton::ChoiceParameterButton (juce::RangedAudioParameter& p, const juce::String& captionText)
    : ValueButton (captionText),
      parameter (p),
      attachment (p, [this] (float plain) {
          index = juce::roundToInt (plain);
          refreshText();
          if (onIndexChanged)
              onIndexChanged (index);
      })
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&p))
        choices = choice->choices;
    buildMenu = [this] (juce::PopupMenu& menu) {
        juce::Component::SafePointer<ChoiceParameterButton> safe (this);
        for (int i = 0; i < choices.size(); ++i)
            menu.addItem (choices[i], true, i == index, [safe, i] {
                if (safe != nullptr)
                    safe->attachment.setValueAsCompleteGesture (static_cast<float> (i));
            });
    };
    attachment.sendInitialUpdate();
}

bool ChoiceParameterButton::isDefault() const
{
    return index == juce::roundToInt (parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

void ChoiceParameterButton::refreshText()
{
    setValueText (valueTextFor ? valueTextFor (index) : choices[index]);
}
} // namespace ssc::ui
