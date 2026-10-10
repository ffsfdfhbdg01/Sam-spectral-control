#include "ui/BandPanel.h"

#include <utility>

#include "plugin/Parameters.h"
#include "ui/BandLogic.h"
#include "ui/SpeakerPanel.h"
#include "ui/Theme.h"

namespace ssc::ui
{
using plugin::BandParam;

namespace
{
    // Band panel (research §15 d4).
    constexpr int kPad = 12;
    constexpr int kTop = 10;
    constexpr int kHeaderHeight = 28;
    constexpr int kTitleWidth = 64;
    constexpr int kRowGap = 8;
    constexpr int kEqRowHeight = 76;
    constexpr int kChoiceHeight = 36;
    constexpr int kChoiceWidth = 108;
    constexpr int kKnobCell = 66;
    constexpr int kRightWidth = 130;
    constexpr int kSpeakersWidth = 104;
    constexpr int kSectionGap = 6;
    constexpr int kSectionHeight = 22;
    constexpr int kDynamicsRowHeight = 68;
    constexpr int kStereoWidth = 488;

    // Advanced flyout.
    constexpr int kFlyPadX = 12;
    constexpr int kFlyPadY = 10;
    constexpr int kSegmentRowHeight = 36;
    constexpr int kFlyKnobRowHeight = 62;
    constexpr int kReadoutLine = 14;

    static_assert (BandPanel::kStaticHeight == kTop + kHeaderHeight + kRowGap + kEqRowHeight + 10);
    static_assert (BandPanel::kDynamicHeight == BandPanel::kStaticHeight + kSectionGap + kSectionHeight + 4 + kDynamicsRowHeight);

    const juce::StringArray& choicesFor (BandParam p)
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

    float plainValue (const BandModel& model, int band, BandParam p)
    {
        const auto& param = model.parameter (band, p);
        return param.convertFrom0to1 (param.getValue());
    }

    int choiceIndex (const BandModel& model, int band, BandParam p)
    {
        return juce::roundToInt (plainValue (model, band, p));
    }

    bool differsFromDefault (const BandModel& model, int band, BandParam p)
    {
        const auto& param = model.parameter (band, p);
        return std::abs (param.getValue() - param.getDefaultValue()) > 1.0e-4f;
    }

    void captionText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
    {
        g.setFont (uiFont (11.0f));
        g.setColour (colours::textDim);
        g.drawText (text, area, juce::Justification::centredLeft, true);
    }
} // namespace

// BandKnob ------------------------------------------------------------------------------------

BandKnob::BandKnob (BandModel& m, BandParam p, const juce::String& text, bool bipolar)
    : model (m),
      param (p),
      caption (text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (uiFont (11.0f));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, colours::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    slider.getProperties().set (kBipolarProperty, bipolar);
    addAndMakeVisible (slider);

    slider.onDragStart = [this] {
        if (band < 0)
            return;
        model.beginEdit ("Change " + caption);
        dragging = true;
    };
    slider.onDragEnd = [this] { endDrag(); };
    slider.onValueChange = [this] {
        if (updating || band < 0)
            return;
        if (dragging)
        {
            model.setValue (band, param, static_cast<float> (slider.getValue()));
        }
        else
        {
            BandModel::ScopedEdit edit (model, "Change " + caption);
            model.setValue (band, param, static_cast<float> (slider.getValue()));
        }
    };
}

BandKnob::~BandKnob()
{
    endDrag();
}

void BandKnob::endDrag()
{
    if (! dragging)
        return;
    dragging = false;
    model.endEdit();
}

void BandKnob::setBand (int newBand)
{
    endDrag();
    band = newBand;
    if (band < 0)
        return;

    auto& p = model.parameter (band, param);
    const auto& range = p.getNormalisableRange();
    const juce::ScopedValueSetter<bool> svs (updating, true);
    slider.setNormalisableRange (
        { static_cast<double> (range.start), static_cast<double> (range.end),
          [&p] (double, double, double n) { return static_cast<double> (p.convertFrom0to1 (static_cast<float> (n))); },
          [&p] (double, double, double v) { return static_cast<double> (p.convertTo0to1 (static_cast<float> (v))); },
          [&p] (double, double, double v) {
              return static_cast<double> (p.getNormalisableRange().snapToLegalValue (static_cast<float> (v)));
          } });
    slider.textFromValueFunction = [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    slider.valueFromTextFunction = [&p] (const juce::String& text) {
        return static_cast<double> (p.convertFrom0to1 (p.getValueForText (text)));
    };
    slider.setDoubleClickReturnValue (true, static_cast<double> (p.convertFrom0to1 (p.getDefaultValue())));
    slider.setTooltip (p.getName (64) + " (double-click: default; click the value to type one)");
    slider.setValue (static_cast<double> (p.convertFrom0to1 (p.getValue())), juce::dontSendNotification);
    slider.updateText();
}

void BandKnob::refresh()
{
    if (band < 0 || dragging)
        return;
    auto& p = model.parameter (band, param);
    const juce::ScopedValueSetter<bool> svs (updating, true);
    slider.setValue (static_cast<double> (p.convertFrom0to1 (p.getValue())), juce::dontSendNotification);
}

void BandKnob::setColour (juce::Colour colour)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
}

void BandKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (14));
    slider.setBounds (area);
}

// BandChoiceButton ----------------------------------------------------------------------------

BandChoiceButton::BandChoiceButton (BandModel& m, BandParam p, const juce::String& captionText)
    : ValueButton (captionText),
      model (m),
      param (p),
      caption (captionText)
{
    setTooltip (captionText);
    buildMenu = [this] (juce::PopupMenu& menu) {
        if (band < 0)
            return;
        const auto& labels = choicesFor (param);
        std::vector<int> indices;
        if (itemsToShow)
            indices = itemsToShow();
        else
            for (int i = 0; i < labels.size(); ++i)
                indices.push_back (i);

        const int current = getIndex();
        const int forBand = band;
        juce::Component::SafePointer<BandChoiceButton> safe (this);
        for (const int i : indices)
            menu.addItem (labels[i], isItemEnabled == nullptr || isItemEnabled (i), i == current, [safe, i, forBand] {
                if (safe != nullptr && safe->band == forBand)
                    safe->choose (i);
            });
    };
}

int BandChoiceButton::getIndex() const
{
    return band < 0 ? 0 : choiceIndex (model, band, param);
}

void BandChoiceButton::setBand (int newBand)
{
    band = newBand;
    refresh();
}

void BandChoiceButton::refresh()
{
    if (band >= 0)
        setValueText (choicesFor (param)[getIndex()]);
}

void BandChoiceButton::choose (int index)
{
    if (param == BandParam::Shape)
    {
        model.setShape (band, static_cast<Shape> (index));
        return;
    }
    BandModel::ScopedEdit edit (model, "Change " + caption);
    model.setValue (band, param, static_cast<float> (index));
}

// BandPanel -----------------------------------------------------------------------------------

BandPanel::BandPanel (BandModel& m)
    : model (m),
      shape (m, BandParam::Shape, "Shape"),
      slope (m, BandParam::Slope, "Slope"),
      target (m, BandParam::Target, "Target"),
      character (m, BandParam::Character, "Character"),
      freq (m, BandParam::Frequency, "Freq"),
      gain (m, BandParam::Gain, "Gain", true),
      q (m, BandParam::Q, "Q"),
      threshold (m, BandParam::Threshold, "Threshold"),
      ratio (m, BandParam::Ratio, "Ratio"),
      range (m, BandParam::Range, "Range"),
      attack (m, BandParam::Attack, "Attack"),
      release (m, BandParam::Release, "Release"),
      selectivity (m, BandParam::Selectivity, "Selectivity")
{
    setOpaque (false);
    for (auto* c : std::initializer_list<juce::Component*> {
             &modeSelector, &advancedButton, &bypassButton, &soloButton, &moreButton, &shape,      &slope,
             &target,       &character,      &speakers,     &freq,       &gain,       &q,          &autoGainButton,
             &threshold,    &ratio,          &range,        &attack,     &release,    &selectivity })
        addAndMakeVisible (c);
    speakers.setVisible (false);

    modeSelector.setItemTooltip (0, "Static: a fixed gain, like a classic EQ");
    modeSelector.setItemTooltip (1, "Dynamic: the gain follows the level in the band (e.g. a de-esser)");
    modeSelector.setItemTooltip (2,
                                 "Spectral: acts only on the frequencies that exceed the threshold (e.g. resonances); "
                                 "adds the latency of the STFT frame");
    modeSelector.onChange = [this] (int index) {
        if (band >= 0)
            model.setMode (band, static_cast<BandMode> (index));
    };

    advancedButton.setClickingTogglesState (true);
    advancedButton.setOnColour (colours::textSecondary); // open, not "something else is heard"
    advancedButton.onClick = [this] {
        advancedButton.setTrailingIcon (advancedButton.getToggleState() ? Icon::ChevronLeft : Icon::ChevronRight);
        notifyLayout();
    };

    bypassButton.setTooltip ("Band on/off (or double-click its node)");
    bypassButton.setOnColour (colours::lamp);
    bypassButton.onClick = [this] {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Toggle Band");
        model.setValue (band, BandParam::On, model.band (band).on ? 0.0f : 1.0f);
    };

    // Solo: a click latches it, holding the button listens only while pressed (FabFilter style).
    soloButton.setTooltip ("Solo: listen to the region of the band. Click to latch; hold to listen while pressed "
                           "(also Alt/Option + hold its node)");
    soloButton.onStateChange = [this] {
        const bool down = soloButton.getState() == juce::Button::buttonDown;
        if (down == soloHeld || band < 0)
            return;
        soloHeld = down;
        if (down)
        {
            soloPressMs = motion::now();
            soloWasOn = model.getSoloBand() == band;
            if (! soloWasOn)
                model.setSoloBand (band);
        }
        else if (soloWasOn || motion::now() - soloPressMs > 400.0)
        {
            model.setSoloBand (-1);
        }
    };

    moreButton.setTooltip ("Copy, paste, reset or delete the band");
    moreButton.onClick = [this] { showMoreMenu(); };

    autoGainButton.setTooltip ("Band Auto Gain: compensates the average gain change of this band, "
                               "so it changes the tone but not the average level");
    autoGainButton.onClick = [this] {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Toggle Auto Gain");
        model.setValue (band, BandParam::AutoGain, model.band (band).autoGain ? 0.0f : 1.0f);
    };

    speakers.setTooltip ("Choose the speakers this band works on");
    speakers.onClick = [this] { openSpeakerPanel(); };

    slope.itemsToShow = [this] {
        std::vector<int> items;
        if (band >= 0)
            for (const auto s : slopesFor (model.band (band).shape))
                items.push_back (static_cast<int> (s));
        return items;
    };
    target.isItemEnabled = [this] (int index) { return index == 0 || numPairs > 0; };
    character.setTooltip ("Character: Clear = clean, Warm = soft saturation, Sharp = hard saturation");
    target.setTooltip ("Target: the stereo component the band works on");

    readoutTimer.startTimerHz (4);
    model.addListener (this);
    selectionChanged();
}

BandPanel::~BandPanel()
{
    model.removeListener (this);
}

void BandPanel::setAdvancedOpen (bool open)
{
    advancedButton.setToggleState (open, juce::dontSendNotification);
    advancedButton.setTrailingIcon (open ? Icon::ChevronLeft : Icon::ChevronRight);
}

void BandPanel::notifyLayout()
{
    if (onLayoutChanged)
        onLayoutChanged();
}

void BandPanel::setTopology (const engine::ChannelTopology& newTopology)
{
    const bool surround = newTopology.numChannels > 2;
    const bool changed = newTopology.numChannels != topology.numChannels || newTopology.roles != topology.roles;
    topology = newTopology;
    numPairs = newTopology.numPairs;
    if (speakers.isVisible() != surround)
    {
        speakers.setVisible (surround);
        resized();
        notifyLayout();
    }
    if (changed)
        updateFromModel();
}

void BandPanel::openSpeakerPanel()
{
    if (band < 0)
        return;
    auto panel = std::make_unique<SpeakerPanel> (model, band, topology);
    juce::CallOutBox::launchAsynchronously (std::move (panel), speakers.getScreenBounds(), nullptr);
}

void BandPanel::showMoreMenu()
{
    if (band < 0)
        return;
    const int b = band;
    juce::Component::SafePointer<BandPanel> safe (this);
    juce::PopupMenu menu;
    menu.addItem ("Copy Band", [safe, b] {
        if (safe != nullptr)
            safe->model.copyBand (b);
    });
    menu.addItem ("Paste Band", model.canPaste(), false, [safe, b] {
        if (safe != nullptr)
            safe->model.pasteToBand (b);
    });
    menu.addItem ("Reset Band", [safe, b] {
        if (safe != nullptr)
            safe->model.resetBand (b);
    });
    menu.addSeparator();
    juce::PopupMenu::Item remove ("Delete Band");
    remove.setColour (colours::warning);
    remove.shortcutKeyDescription = "Delete";
    remove.setAction ([safe, b] {
        if (safe != nullptr)
            safe->model.deleteBand (b);
    });
    menu.addItem (std::move (remove));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton));
}

void BandPanel::selectionChanged()
{
    const int focused = model.getFocusedBand();
    if (focused == band)
        return;
    band = focused;
    if (band >= 0)
        bindBand();
    notifyLayout();
}

void BandPanel::bandsChanged()
{
    if (band < 0 || ! model.band (band).used)
        return; // the selection update follows
    updateFromModel();
}

void BandPanel::bindBand()
{
    const auto colour = bandColour (band);
    for (auto* k : { &freq, &gain, &q, &threshold, &ratio, &range, &attack, &release, &selectivity })
    {
        k->setBand (band);
        k->setColour (colour);
    }
    for (auto* c : { &shape, &slope, &target, &character })
        c->setBand (band);
    modeSelector.setIndicatorColour (colours::raised);
    readoutWarning = false;
    updateFromModel();
}

void BandPanel::updateFromModel()
{
    if (band < 0)
        return;

    const auto& v = model.band (band);
    const bool gainShape = shapeUsesGain (v.shape);
    modeSelector.setSelectedIndex (static_cast<int> (v.mode), juce::dontSendNotification);
    for (int i = 1; i < 3; ++i)
    {
        modeSelector.setItemEnabled (i, gainShape);
        modeSelector.setItemTooltip (i,
                                     gainShape ? (i == 1 ? "Dynamic: the gain follows the level in the band (e.g. a de-esser)"
                                                         : "Spectral: acts only on the frequencies that exceed the threshold "
                                                           "(e.g. resonances); adds the latency of the STFT frame")
                                               : "Dynamic and Spectral need a shape with gain (Bell, shelves, tilts)");
    }

    bypassButton.setToggleState (! v.on, juce::dontSendNotification);
    bypassButton.setIndicator (v.on ? juce::Colours::transparentBlack : colours::lamp);
    bypassButton.setTooltip (v.on ? "Band on: click to bypass it (or double-click its node)"
                                  : "Band bypassed: click to switch it on (or double-click its node)");
    soloButton.setToggleState (model.getSoloBand() == band, juce::dontSendNotification);
    soloButton.setEnabled (v.on);
    autoGainButton.setToggleState (v.autoGain, juce::dontSendNotification);
    speakers.setValueText (SpeakerPanel::summary (v.speakers, topology));

    for (auto* c : { &shape, &slope, &target, &character })
        c->refresh();
    for (auto* k : { &freq, &gain, &q, &threshold, &ratio, &range, &attack, &release, &selectivity })
        k->refresh();
    gain.setEnabled (gainShape);
    q.setEnabled (shapeUsesQ (v.shape));

    const bool slopeNow = shapeUsesSlope (v.shape);
    const bool layoutChanged = v.mode != shownMode || slopeNow != slopeShown || ! freq.isVisible();
    shownMode = v.mode;
    slopeShown = slopeNow;
    const bool dynamics = shownMode != BandMode::Static;
    for (auto* k : { &threshold, &range, &attack, &release })
        k->setVisible (dynamics);
    ratio.setVisible (shownMode == BandMode::Dynamic);
    selectivity.setVisible (shownMode == BandMode::Spectral);
    autoGainButton.setVisible (dynamics);
    slope.setVisible (slopeShown);
    freq.setVisible (true);

    updateIndicator();
    if (layoutChanged)
    {
        resized();
        notifyLayout();
    }
    repaint();
}

bool BandPanel::advancedDiffersFromDefault (const BandModel& model, int band)
{
    if (band < 0)
        return false;
    const auto mode = model.band (band).mode;
    if (mode == BandMode::Static)
        return false;
    // The threshold mode defaults with the band mode (DYN-4).
    const auto modeDefault = mode == BandMode::Spectral ? ThresholdMode::Relative : ThresholdMode::Absolute;
    if (choiceIndex (model, band, BandParam::ThresholdMode) != static_cast<int> (modeDefault))
        return true;
    for (const auto p : { BandParam::Direction, BandParam::Knee, BandParam::Mix, BandParam::Link })
        if (differsFromDefault (model, band, p))
            return true;
    return differsFromDefault (model, band, mode == BandMode::Dynamic ? BandParam::Lookahead : BandParam::Ratio);
}

void BandPanel::updateIndicator()
{
    const bool differs = advancedDiffersFromDefault (model, band);
    advancedButton.setIndicator (differs || readoutWarning ? colours::accent : juce::Colours::transparentBlack);
    juce::String tip = shownMode == BandMode::Static ? "Advanced: readouts of the band (phase, group delay, ringing)"
                                                     : "Advanced: detector settings (threshold mode, direction, knee, "
            + juce::String (shownMode == BandMode::Dynamic ? "lookahead" : "ratio") + ", mix, link) and readouts of the band";
    if (differs)
        tip << "\nThe dot: a detector setting differs from its default.";
    if (readoutWarning)
        tip << "\nThe dot: long ringing, may be audible.";
    advancedButton.setTooltip (tip);
}

void BandPanel::pollReadout()
{
    if (band < 0 || ! readoutProvider || ! isVisible())
        return;
    const bool warns = BandAdvancedPanel::readoutWarns (readoutProvider (band));
    if (warns != readoutWarning)
    {
        readoutWarning = warns;
        updateIndicator();
    }
}

std::vector<BandParam> BandPanel::visibleParameters() const
{
    std::vector<BandParam> params { BandParam::Mode, BandParam::On };
    for (const auto* c : { &shape, &slope, &target, &character })
        if (c->isVisible())
            params.push_back (c->getParam());
    for (const auto* k : { &freq, &gain, &q, &threshold, &ratio, &range, &attack, &release, &selectivity })
        if (k->isVisible())
            params.push_back (k->getParam());
    if (autoGainButton.isVisible())
        params.push_back (BandParam::AutoGain);
    return params;
}

juce::Point<int> BandPanel::getPreferredSize() const
{
    const int width = kStereoWidth + (topology.numChannels > 2 ? 8 + kSpeakersWidth : 0);
    return { width, shownMode == BandMode::Static ? kStaticHeight : kDynamicHeight };
}

void BandPanel::resized()
{
    auto area = getLocalBounds().reduced (kPad, 0).withTrimmedTop (kTop);

    auto header = area.removeFromTop (kHeaderHeight);
    header.removeFromLeft (kTitleWidth); // dot and title (painted)
    moreButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (2);
    soloButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (2);
    bypassButton.setBounds (header.removeFromRight (28));
    header.removeFromRight (8);
    advancedButton.setBounds (header.removeFromRight (advancedButton.getIdealWidth()).reduced (0, 2));
    header.removeFromRight (8);
    modeSelector.setBounds (header.removeFromLeft (juce::jmin (header.getWidth(), 198)).reduced (0, 1));

    area.removeFromTop (kRowGap);
    auto eq = area.removeFromTop (kEqRowHeight);
    {
        auto column = eq.removeFromLeft (kChoiceWidth);
        shape.setBounds (column.removeFromTop (kChoiceHeight));
        column.removeFromTop (4);
        slope.setBounds (column.removeFromTop (kChoiceHeight));
    }
    eq.removeFromLeft (14);
    for (auto* k : { &freq, &gain, &q })
        k->setBounds (eq.removeFromLeft (kKnobCell));
    eq.removeFromLeft (14);
    {
        auto column = eq.removeFromLeft (kRightWidth);
        target.setBounds (column.removeFromTop (kChoiceHeight));
        column.removeFromTop (4);
        character.setBounds (column.removeFromTop (kChoiceHeight));
    }
    if (speakers.isVisible())
    {
        eq.removeFromLeft (8);
        speakers.setBounds (eq.removeFromLeft (kSpeakersWidth).removeFromTop (kChoiceHeight));
    }

    // Dynamics (Dynamic and Spectral bands): the knobs follow the mode.
    area.removeFromTop (kSectionGap);
    auto section = area.removeFromTop (kSectionHeight);
    autoGainButton.setBounds (section.removeFromRight (autoGainButton.getIdealWidth()));
    area.removeFromTop (4);
    auto row = area.removeFromTop (kDynamicsRowHeight).withWidth (kStereoWidth - 2 * kPad);
    auto& second = shownMode == BandMode::Spectral ? selectivity : ratio;
    const int cell = row.getWidth() / 5;
    for (auto* k : { &threshold, &second, &range, &attack, &release })
        k->setBounds (row.removeFromLeft (cell).withSizeKeepingCentre (juce::jmin (cell, 80), kDynamicsRowHeight));
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::panelRadius);
    const auto colour = band >= 0 ? bandColour (band) : colours::outline;
    g.setColour (colour.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, metrics::panelRadius, 1.0f);
    if (band < 0)
        return;

    const auto title = juce::Rectangle<int> (kPad, kTop, kTitleWidth, kHeaderHeight);
    g.setColour (model.band (band).on ? colour : colours::powerOff);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ static_cast<float> (kPad) + 5.0f, title.toFloat().getCentreY() }));
    g.setColour (colours::text);
    g.setFont (uiFont (13.5f, true));
    g.drawText ("Band " + juce::String (band + 1), title.withTrimmedLeft (16), juce::Justification::centredLeft, false);

    if (shownMode != BandMode::Static)
    {
        const int y = kTop + kHeaderHeight + kRowGap + kEqRowHeight + kSectionGap;
        const auto text = juce::String (shownMode == BandMode::Spectral ? "SPECTRAL DYNAMICS" : "DYNAMICS");
        const auto font = uiFont (10.5f, true);
        const auto line = juce::Rectangle<int> (kPad, y, autoGainButton.getX() - 10 - kPad, kSectionHeight);
        g.setFont (font);
        g.setColour (colours::textDim);
        g.drawText (text, line, juce::Justification::centredLeft, false);
        g.setColour (colours::panelEdge);
        g.fillRect (juce::Rectangle<float> (static_cast<float> (line.getX()) + juce::GlyphArrangement::getStringWidth (font, text) + 8.0f,
                                            static_cast<float> (line.getCentreY()), 0.0f, 1.0f)
                        .withRight (static_cast<float> (line.getRight())));
    }
}

// BandAdvancedPanel ---------------------------------------------------------------------------

BandAdvancedPanel::BandAdvancedPanel (BandModel& m)
    : model (m),
      knee (m, BandParam::Knee, "Knee"),
      lookahead (m, BandParam::Lookahead, "Lookahead"),
      ratio (m, BandParam::Ratio, "Ratio"),
      mix (m, BandParam::Mix, "Mix"),
      link (m, BandParam::Link, "Link")
{
    setOpaque (false);
    for (auto* c : std::initializer_list<juce::Component*> { &thresholdMode, &direction, &knee, &lookahead, &ratio, &mix, &link })
        addAndMakeVisible (c);

    thresholdMode.setTooltip ("Threshold Mode. Absolute: in dBFS. Relative: above the neighbouring spectrum, "
                              "nearly independent of the loudness of the material");
    direction.setTooltip ("Direction. Above: acts while the level exceeds the threshold. Below: while it stays under it");
    lookahead.getSlider().setTooltip ("Lookahead: the band reacts before a transient; raises Max Lookahead (latency) when needed");
    link.setTooltip ("Link: channels (or M/S components) that share the detection. Independent, Pair, Group (default) or All");

    thresholdMode.onChange = [this] (int index) {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Change Threshold Mode");
        model.setValue (band, BandParam::ThresholdMode, static_cast<float> (index));
    };
    direction.onChange = [this] (int index) {
        if (band < 0)
            return;
        BandModel::ScopedEdit edit (model, "Change Direction");
        model.setValue (band, BandParam::Direction, static_cast<float> (index));
    };

    model.addListener (this);
    selectionChanged();
}

BandAdvancedPanel::~BandAdvancedPanel()
{
    model.removeListener (this);
}

bool BandAdvancedPanel::showsDetector() const
{
    return band >= 0 && shownMode != BandMode::Static;
}

void BandAdvancedPanel::selectionChanged()
{
    const int focused = model.getFocusedBand();
    if (focused == band)
        return;
    band = focused;
    readout = {};
    if (band >= 0)
        bindBand();
}

void BandAdvancedPanel::bandsChanged()
{
    if (band >= 0 && model.band (band).used)
        updateFromModel();
}

void BandAdvancedPanel::bindBand()
{
    const auto colour = bandColour (band);
    for (auto* k : { &knee, &lookahead, &ratio, &mix })
    {
        k->setBand (band);
        k->setColour (colour);
    }
    link.setBand (band);
    shownMode = model.band (band).mode;
    updateFromModel();
    resized();
    pollReadout();
}

void BandAdvancedPanel::updateFromModel()
{
    if (band < 0)
        return;
    thresholdMode.setSelectedIndex (choiceIndex (model, band, BandParam::ThresholdMode), juce::dontSendNotification);
    direction.setSelectedIndex (choiceIndex (model, band, BandParam::Direction), juce::dontSendNotification);
    for (auto* k : { &knee, &lookahead, &ratio, &mix })
        k->refresh();
    link.refresh();

    const auto mode = model.band (band).mode;
    const bool modeChanged = mode != shownMode;
    shownMode = mode;
    const bool detector = showsDetector();
    for (auto* c : std::initializer_list<juce::Component*> { &thresholdMode, &direction, &knee, &mix, &link })
        c->setVisible (detector);
    lookahead.setVisible (shownMode == BandMode::Dynamic);
    ratio.setVisible (shownMode == BandMode::Spectral);
    if (modeChanged)
        resized();
    repaint();
}

int BandAdvancedPanel::getPreferredHeight() const
{
    const int lines = static_cast<int> (readoutLines (readout).size());
    if (! showsDetector())
        return 2 * kFlyPadY + 16 + 4 * kReadoutLine + 18; // caption, lines, note
    return 2 * kFlyPadY + kSegmentRowHeight + 8 + kFlyKnobRowHeight + 6 + kChoiceHeight + 7 + juce::jmax (lines, 4) * kReadoutLine;
}

std::vector<BandParam> BandAdvancedPanel::visibleParameters() const
{
    std::vector<BandParam> params;
    if (thresholdMode.isVisible())
        params.push_back (BandParam::ThresholdMode);
    if (direction.isVisible())
        params.push_back (BandParam::Direction);
    for (const auto* k : { &knee, &lookahead, &ratio, &mix })
        if (k->isVisible())
            params.push_back (k->getParam());
    if (link.isVisible())
        params.push_back (BandParam::Link);
    return params;
}

void BandAdvancedPanel::resized()
{
    auto area = getLocalBounds().reduced (kFlyPadX, kFlyPadY);
    if (! showsDetector())
        return;

    auto segments = area.removeFromTop (kSegmentRowHeight);
    auto left = segments.removeFromLeft ((segments.getWidth() - 8) / 2);
    segments.removeFromLeft (8);
    thresholdMode.setBounds (left.withTrimmedTop (13));
    direction.setBounds (segments.withTrimmedTop (13));

    area.removeFromTop (8);
    auto knobs = area.removeFromTop (kFlyKnobRowHeight);
    const int cell = knobs.getWidth() / 3;
    knee.setBounds (knobs.removeFromLeft (cell));
    (shownMode == BandMode::Dynamic ? lookahead : ratio).setBounds (knobs.removeFromLeft (cell));
    mix.setBounds (knobs);

    area.removeFromTop (6);
    link.setBounds (area.removeFromTop (kChoiceHeight).removeFromLeft (128));
}

void BandAdvancedPanel::visibilityChanged()
{
    if (isVisible())
    {
        pollReadout();
        readoutTimer.startTimerHz (10);
    }
    else
    {
        readoutTimer.stopTimer();
    }
}

void BandAdvancedPanel::pollReadout()
{
    if (band < 0 || ! readoutProvider)
        return;
    const auto next = readoutProvider (band);
    const auto same = [] (const analysis::BandReadout& a, const analysis::BandReadout& b) {
        return a.valid == b.valid && juce::exactlyEqual (a.meanPhaseDeg, b.meanPhaseDeg)
            && juce::exactlyEqual (a.groupDelayMs, b.groupDelayMs) && juce::exactlyEqual (a.preRingMs, b.preRingMs)
            && juce::exactlyEqual (a.postRingMs, b.postRingMs) && juce::exactlyEqual (a.rangePreRingMs, b.rangePreRingMs)
            && juce::exactlyEqual (a.rangePostRingMs, b.rangePostRingMs) && juce::exactlyEqual (a.stftPreEchoMs, b.stftPreEchoMs);
    };
    if (same (next, readout))
        return;
    readout = next;
    juce::String tip = next.stftPreEchoMs >= 0.0f
        ? "Spectral band: the STFT mask is zero-phase; it smears transients by about half a frame each way (pre-echo)."
        : "Mean excess phase where the band acts, group delay at its frequency, ringing before (pre) and after (post) "
          "an impulse down to -60 dB.";
    if (readoutWarns (next))
        tip << "\nAmber: likely audible (post-ring > 50 ms or pre-ring > 5 ms).";
    setTooltip (tip);
    repaint();
}

std::vector<std::pair<juce::String, juce::String>> BandAdvancedPanel::readoutLines (const analysis::BandReadout& r)
{
    const auto ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
    if (! r.valid)
        return { { "Excess phase", ellipsis }, { "Group delay", ellipsis }, { "Ringing", ellipsis } };

    auto ms = [] (float v) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; };
    const auto degree = juce::String::fromUTF8 ("\xc2\xb0");
    if (r.stftPreEchoMs >= 0.0f)
        return { { "Excess phase", "0" + degree }, { "STFT pre-echo", juce::String::fromUTF8 ("\xc2\xb1") + ms (r.stftPreEchoMs) } };

    std::vector<std::pair<juce::String, juce::String>> lines { { "Excess phase",
                                                                 juce::String (juce::roundToInt (r.meanPhaseDeg)) + degree },
                                                               { "Group delay", juce::String (r.groupDelayMs, 2) + " ms" },
                                                               { "Ringing", "pre " + ms (r.preRingMs) + "  post " + ms (r.postRingMs) } };
    if (r.rangePostRingMs >= 0.0f)
        lines.push_back ({ "At full Range", "pre " + ms (r.rangePreRingMs) + "  post " + ms (r.rangePostRingMs) });
    return lines;
}

bool BandAdvancedPanel::readoutWarns (const analysis::BandReadout& r) noexcept
{
    // Pre-echo is heard more easily than a tail of the same length (research §12c).
    const float post = juce::jmax (r.postRingMs, r.rangePostRingMs);
    const float pre = juce::jmax (r.preRingMs, r.rangePreRingMs);
    return r.valid && (post > 50.0f || pre > 5.0f);
}

void BandAdvancedPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::popover);
    g.fillRoundedRectangle (bounds, metrics::panelRadius);
    g.setColour ((band >= 0 ? bandColour (band) : colours::outline).withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, metrics::panelRadius, 1.0f);
    if (band < 0)
        return;

    auto area = getLocalBounds().reduced (kFlyPadX, kFlyPadY);
    if (showsDetector())
    {
        captionText (g, "Threshold", thresholdMode.getBounds().withY (thresholdMode.getY() - 14).withHeight (13));
        captionText (g, "Direction", direction.getBounds().withY (direction.getY() - 14).withHeight (13));
        area.setTop (link.getBottom() + 4);
        g.setColour (colours::panelEdge);
        g.fillRect (area.removeFromTop (1).toFloat());
        area.removeFromTop (2);
    }
    else
    {
        g.setFont (uiFont (10.5f, true));
        g.setColour (colours::textDim);
        g.drawText ("READOUTS", area.removeFromTop (16), juce::Justification::centredLeft, false);
    }

    // Ringing lines (and the STFT pre-echo) in amber when they are likely audible.
    const bool spectral = readout.stftPreEchoMs >= 0.0f;
    const auto warnLine = [spectral] (const analysis::BandReadout& r, int index) {
        if (index == (spectral ? 1 : 2))
            return r.valid && (r.postRingMs > 50.0f || r.preRingMs > 5.0f);
        if (index == 3)
            return r.valid && (r.rangePostRingMs > 50.0f || r.rangePreRingMs > 5.0f);
        return false;
    };
    const auto lines = readoutLines (readout);
    for (int i = 0; i < static_cast<int> (lines.size()); ++i)
    {
        auto line = area.removeFromTop (kReadoutLine);
        g.setFont (uiFont (11.0f));
        g.setColour (colours::textDim);
        g.drawText (lines[static_cast<std::size_t> (i)].first, line.removeFromLeft (84), juce::Justification::centredLeft, false);
        g.setFont (uiFont (11.5f));
        g.setColour (warnLine (readout, i) ? colours::accent : colours::textSecondary);
        g.drawText (lines[static_cast<std::size_t> (i)].second, line, juce::Justification::centredRight, true);
    }

    if (! showsDetector())
    {
        g.setFont (uiFont (10.5f));
        g.setColour (colours::textDim);
        g.drawFittedText ("Detector settings: Dynamic and Spectral bands",
                          getLocalBounds().reduced (kFlyPadX, kFlyPadY).removeFromBottom (14), juce::Justification::centredLeft, 1, 0.9f);
    }
}
} // namespace ssc::ui
