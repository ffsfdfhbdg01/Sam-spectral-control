#include "ui/BandPanel.h"

#include <utility>

#include "plugin/Parameters.h"
#include "ui/BandLogic.h"
#include "ui/Theme.h"

namespace ssc::ui
{
using plugin::BandParam;

namespace
{
    constexpr int kPadding = 8;
    constexpr int kHeaderHeight = 26;
    constexpr int kKnobWidth = 62;
    constexpr int kKnobRowHeight = 78;
    constexpr int kChoiceWidth = 104;
    constexpr int kChoiceHeight = 22;
    constexpr int kGap = 6;

    const juce::StringArray& itemsFor (BandParam p)
    {
        using Labels = const juce::StringArray& (*)();
        static const std::pair<BandParam, Labels> table[] = {
            { BandParam::Shape, plugin::labels::shapes },         { BandParam::Slope, plugin::labels::slopes },
            { BandParam::Mode, plugin::labels::bandModes },       { BandParam::Target, plugin::labels::targets },
            { BandParam::Character, plugin::labels::characters }, { BandParam::ThresholdMode, plugin::labels::thresholdModes },
            { BandParam::Direction, plugin::labels::directions }, { BandParam::Link, plugin::labels::links },
        };
        for (const auto& [param, labels] : table)
            if (param == p)
                return labels();
        jassertfalse;
        static const juce::StringArray none;
        return none;
    }

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panel.brighter (0.06f));
        b.setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.85f));
        b.setColour (juce::TextButton::textColourOffId, colours::text);
        b.setColour (juce::TextButton::textColourOnId, colours::background);
    }
} // namespace

BandPanel::BandPanel (BandModel& m)
    : model (m)
{
    title.setFont (uiFont (13.0f, true));
    title.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (title);

    for (auto* b : { &onButton, &soloButton, &autoGainButton, &deleteButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    onButton.setColour (juce::TextButton::buttonOnColourId, colours::powerOn);
    onButton.setTooltip ("Switch the band on or off (double-click its node)");
    soloButton.setTooltip ("Listen to the region the band works on (Alt/Option + click its node)");
    autoGainButton.setTooltip ("Compensate the loudness change of this band");
    deleteButton.setTooltip ("Delete the band (Delete key)");

    onButton.onClick = [this] {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Toggle Band");
        model.setValue (band, BandParam::On, model.band (band).on ? 0.0f : 1.0f);
    };
    soloButton.onClick = [this] {
        if (band >= 0)
            model.setSoloBand (model.getSoloBand() == band ? -1 : band);
    };
    autoGainButton.onClick = [this] {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Toggle Auto Gain");
        model.setValue (band, BandParam::AutoGain, model.band (band).autoGain ? 0.0f : 1.0f);
    };
    deleteButton.onClick = [this] {
        if (band >= 0)
            model.deleteBand (band);
    };

    addChoice (BandParam::Shape, "Shape", false);
    addChoice (BandParam::Slope, "Slope", false);
    addChoice (BandParam::Target, "Target", false);
    addChoice (BandParam::Mode, "Mode", false);
    addChoice (BandParam::Character, "Character", false);
    addChoice (BandParam::ThresholdMode, "Threshold Mode", true);
    addChoice (BandParam::Direction, "Direction", true);
    addChoice (BandParam::Link, "Link", true);

    addKnob (BandParam::Frequency, "Freq", false);
    addKnob (BandParam::Gain, "Gain", false);
    addKnob (BandParam::Q, "Q", false);
    addKnob (BandParam::Threshold, "Threshold", true);
    addKnob (BandParam::Ratio, "Ratio", true);
    addKnob (BandParam::Range, "Range", true);
    addKnob (BandParam::Knee, "Knee", true);
    addKnob (BandParam::Attack, "Attack", true);
    addKnob (BandParam::Release, "Release", true);
    addKnob (BandParam::Lookahead, "Lookahead", true);
    addKnob (BandParam::Mix, "Mix", true);
    addKnob (BandParam::Selectivity, "Selectivity", true);

    model.addListener (this);
    selectionChanged();
}

BandPanel::~BandPanel()
{
    for (auto& k : knobs)
        if (k->dragging)
            model.endEdit();
    model.removeListener (this);
}

BandPanel::Knob& BandPanel::addKnob (BandParam param, const juce::String& name, bool dynamic)
{
    auto knob = std::make_unique<Knob>();
    knob->param = param;
    knob->title = name;
    knob->dynamic = dynamic;
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobWidth, 16);
    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setFont (uiFont (11.0f));
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (knob->slider);
    addAndMakeVisible (knob->label);

    auto* k = knob.get();
    k->slider.onDragStart = [this, k] {
        if (band < 0)
            return;
        model.beginEdit ("Change " + k->title);
        k->dragging = true;
    };
    k->slider.onDragEnd = [this, k] {
        if (! k->dragging)
            return;
        k->dragging = false;
        model.endEdit();
    };
    k->slider.onValueChange = [this, k] {
        if (updating || band < 0)
            return;
        if (k->dragging)
        {
            model.setValue (band, k->param, static_cast<float> (k->slider.getValue()));
        }
        else
        {
            BandModel::ScopedEdit edit (model, "Change " + k->title);
            model.setValue (band, k->param, static_cast<float> (k->slider.getValue()));
        }
    };

    knobs.push_back (std::move (knob));
    return *knobs.back();
}

BandPanel::Choice& BandPanel::addChoice (BandParam param, const juce::String& name, bool dynamic)
{
    auto choice = std::make_unique<Choice>();
    choice->param = param;
    choice->title = name;
    choice->dynamic = dynamic;
    choice->box.setTooltip (name);
    addAndMakeVisible (choice->box);

    auto* c = choice.get();
    c->box.onChange = [this, c] {
        const int id = c->box.getSelectedId();
        if (updating || band < 0 || id <= 0)
            return;
        if (c->param == BandParam::Shape)
        {
            model.setShape (band, static_cast<Shape> (id - 1));
        }
        else if (c->param == BandParam::Mode)
        {
            model.setMode (band, static_cast<BandMode> (id - 1));
        }
        else
        {
            BandModel::ScopedEdit edit (model, "Change " + c->title);
            model.setValue (band, c->param, static_cast<float> (id - 1));
        }
    };

    choices.push_back (std::move (choice));
    return *choices.back();
}

void BandPanel::setChannelInfo (int, int pairs)
{
    if (pairs == numPairs)
        return;
    numPairs = pairs;
    for (auto& c : choices)
        if (c->param == BandParam::Target)
            rebuildChoiceItems (*c);
}

bool BandPanel::showsDynamics() const
{
    return band >= 0 && model.band (band).mode != BandMode::Static;
}

void BandPanel::selectionChanged()
{
    const int focused = model.getFocusedBand();
    if (focused == band)
        return;

    for (auto& k : knobs)
        if (k->dragging)
        {
            k->dragging = false;
            model.endEdit();
        }

    band = focused;
    setVisible (band >= 0);
    if (band >= 0)
        bind();
    layoutChanged();
}

void BandPanel::bandsChanged()
{
    if (band < 0)
        return;
    if (! model.band (band).used)
        return; // the selection update follows
    updateFromModel();
}

void BandPanel::bind()
{
    for (auto& k : knobs)
    {
        auto& param = model.parameter (band, k->param);
        const auto& range = param.getNormalisableRange();
        const juce::ScopedValueSetter<bool> svs (updating, true);
        k->slider.setNormalisableRange (
            { static_cast<double> (range.start), static_cast<double> (range.end),
              [&param] (double, double, double n) { return static_cast<double> (param.convertFrom0to1 (static_cast<float> (n))); },
              [&param] (double, double, double v) { return static_cast<double> (param.convertTo0to1 (static_cast<float> (v))); },
              [&param] (double, double, double v) {
                  return static_cast<double> (param.getNormalisableRange().snapToLegalValue (static_cast<float> (v)));
              } });
        k->slider.textFromValueFunction = [&param] (double v) { return param.getText (param.convertTo0to1 (static_cast<float> (v)), 0); };
        k->slider.valueFromTextFunction = [&param] (const juce::String& text) {
            return static_cast<double> (param.convertFrom0to1 (param.getValueForText (text)));
        };
        k->slider.setDoubleClickReturnValue (true, static_cast<double> (param.convertFrom0to1 (param.getDefaultValue())));
        k->slider.setTooltip (param.getName (64));
        k->slider.updateText();
    }

    for (auto& c : choices)
        rebuildChoiceItems (*c);

    updateFromModel();
}

void BandPanel::rebuildChoiceItems (Choice& c)
{
    if (band < 0)
        return;

    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& v = model.band (band);
    const auto& items = itemsFor (c.param);
    c.box.clear (juce::dontSendNotification);

    if (c.param == BandParam::Slope)
    {
        for (auto slope : slopesFor (v.shape))
            c.box.addItem (items[static_cast<int> (slope)], static_cast<int> (slope) + 1);
        itemsShape = v.shape;
    }
    else
    {
        for (int i = 0; i < items.size(); ++i)
            c.box.addItem (items[i], i + 1);
    }

    if (c.param == BandParam::Mode)
        for (int i = 1; i < items.size(); ++i)
            c.box.setItemEnabled (i + 1, shapeUsesGain (v.shape));
    if (c.param == BandParam::Target)
        for (int i = 1; i < items.size(); ++i)
            c.box.setItemEnabled (i + 1, numPairs > 0);

    const int index = juce::roundToInt (model.parameter (band, c.param).convertFrom0to1 (model.parameter (band, c.param).getValue()));
    c.box.setSelectedId (index + 1, juce::dontSendNotification);
}

void BandPanel::updateFromModel()
{
    if (band < 0)
        return;

    const auto& v = model.band (band);
    const juce::ScopedValueSetter<bool> svs (updating, true);

    title.setText ("Band " + juce::String (band + 1), juce::dontSendNotification);
    onButton.setToggleState (v.on, juce::dontSendNotification);
    onButton.setButtonText (v.on ? "On" : "Off");
    soloButton.setToggleState (model.getSoloBand() == band, juce::dontSendNotification);
    soloButton.setEnabled (v.on);
    autoGainButton.setToggleState (v.autoGain, juce::dontSendNotification);

    for (auto& k : knobs)
    {
        auto& param = model.parameter (band, k->param);
        if (! k->dragging)
            k->slider.setValue (static_cast<double> (param.convertFrom0to1 (param.getValue())), juce::dontSendNotification);
        if (k->param == BandParam::Gain)
            k->slider.setEnabled (shapeUsesGain (v.shape));
        if (k->param == BandParam::Q)
            k->slider.setEnabled (shapeUsesQ (v.shape));
    }

    for (auto& c : choices)
    {
        if ((c->param == BandParam::Slope && v.shape != itemsShape) || c->param == BandParam::Mode)
        {
            rebuildChoiceItems (*c);
            continue;
        }
        auto& param = model.parameter (band, c->param);
        c->box.setSelectedId (juce::roundToInt (param.convertFrom0to1 (param.getValue())) + 1, juce::dontSendNotification);
    }

    const bool dynamics = showsDynamics();
    const bool slopeVisible = shapeUsesSlope (v.shape);
    bool layoutDirty = dynamics != lastDynamics;
    for (auto& c : choices)
        if (c->param == BandParam::Slope && c->box.isVisible() != slopeVisible)
            layoutDirty = true;
    if (layoutDirty)
        layoutChanged();
    repaint();
}

void BandPanel::layoutChanged()
{
    lastDynamics = showsDynamics();
    const bool slopeVisible = band >= 0 && shapeUsesSlope (model.band (band).shape);
    for (auto& k : knobs)
    {
        k->slider.setVisible (! k->dynamic || lastDynamics);
        k->label.setVisible (! k->dynamic || lastDynamics);
    }
    for (auto& c : choices)
        c->box.setVisible ((! c->dynamic || lastDynamics) && (c->param != BandParam::Slope || slopeVisible));

    resized();
    if (onPreferredSizeChanged)
        onPreferredSizeChanged();
}

juce::Point<int> BandPanel::getPreferredSize() const
{
    // Row 1: title, On, Solo, Shape, Slope, Delete. Row 2: Freq, Gain, Q, Target/Mode/Character, Auto Gain.
    // Row 3 (Dynamic/Spectral): nine knobs and the detector choices.
    const int row1 = 74 + 2 * (48 + kGap) + 2 * (kChoiceWidth + kGap) + 64;
    const int row2 = 3 * kKnobWidth + kGap + kChoiceWidth + kGap + 84;
    const int row3 = 9 * kKnobWidth + kGap + kChoiceWidth;
    const int width = 2 * kPadding + juce::jmax (row1, row2, showsDynamics() ? row3 : 0);
    const int height = 2 * kPadding + kHeaderHeight + kGap + kKnobRowHeight + (showsDynamics() ? kGap + kKnobRowHeight : 0);
    return { width, height };
}

void BandPanel::resized()
{
    auto area = getLocalBounds().reduced (kPadding);

    auto header = area.removeFromTop (kHeaderHeight);
    title.setBounds (header.removeFromLeft (74).withTrimmedLeft (14));
    onButton.setBounds (header.removeFromLeft (48).reduced (0, 2));
    header.removeFromLeft (kGap);
    soloButton.setBounds (header.removeFromLeft (48).reduced (0, 2));
    header.removeFromLeft (kGap);
    deleteButton.setBounds (header.removeFromRight (64).reduced (0, 2));

    auto choice = [this] (BandParam p) -> juce::ComboBox& {
        for (auto& c : choices)
            if (c->param == p)
                return c->box;
        jassertfalse;
        return choices.front()->box;
    };
    auto knob = [this] (BandParam p) -> Knob& {
        for (auto& k : knobs)
            if (k->param == p)
                return *k;
        jassertfalse;
        return *knobs.front();
    };

    choice (BandParam::Shape).setBounds (header.removeFromLeft (kChoiceWidth).reduced (0, 1));
    header.removeFromLeft (kGap);
    choice (BandParam::Slope).setBounds (header.removeFromLeft (kChoiceWidth).reduced (0, 1));

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> cell) {
        k.label.setBounds (cell.removeFromTop (14));
        k.slider.setBounds (cell);
    };

    area.removeFromTop (kGap);
    auto row2 = area.removeFromTop (kKnobRowHeight);
    for (auto p : { BandParam::Frequency, BandParam::Gain, BandParam::Q })
        placeKnob (knob (p), row2.removeFromLeft (kKnobWidth));
    row2.removeFromLeft (kGap);
    {
        auto column = row2.removeFromLeft (kChoiceWidth);
        const int spacing = (column.getHeight() - 3 * kChoiceHeight) / 2;
        for (auto p : { BandParam::Target, BandParam::Mode, BandParam::Character })
        {
            choice (p).setBounds (column.removeFromTop (kChoiceHeight));
            column.removeFromTop (spacing);
        }
    }
    row2.removeFromLeft (kGap);
    autoGainButton.setBounds (row2.removeFromLeft (84).withSizeKeepingCentre (84, 24));

    if (showsDynamics())
    {
        area.removeFromTop (kGap);
        auto row3 = area.removeFromTop (kKnobRowHeight);
        for (auto p : { BandParam::Threshold, BandParam::Ratio, BandParam::Range, BandParam::Knee, BandParam::Attack, BandParam::Release,
                        BandParam::Lookahead, BandParam::Mix, BandParam::Selectivity })
            placeKnob (knob (p), row3.removeFromLeft (kKnobWidth));
        row3.removeFromLeft (kGap);
        auto column = row3.removeFromLeft (kChoiceWidth);
        const int spacing = (column.getHeight() - 3 * kChoiceHeight) / 2;
        for (auto p : { BandParam::ThresholdMode, BandParam::Direction, BandParam::Link })
        {
            choice (p).setBounds (column.removeFromTop (kChoiceHeight));
            column.removeFromTop (spacing);
        }
    }
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto colour = band >= 0 ? bandColour (band) : colours::panelEdge;
    g.setColour (colour.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    if (band >= 0)
    {
        const auto dot = juce::Rectangle<float> (10.0f, 10.0f)
                             .withCentre ({ static_cast<float> (kPadding) + 6.0f, static_cast<float> (kPadding + kHeaderHeight / 2) });
        g.setColour (model.band (band).on ? colour : colours::powerOff);
        g.fillEllipse (dot);
    }
}
} // namespace ssc::ui
