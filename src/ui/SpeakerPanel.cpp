#include "ui/SpeakerPanel.h"

#include <cmath>

#include "ui/Theme.h"

namespace ssc::ui
{
namespace
{
    using engine::SpeakerGroup;
    using engine::SpeakerRole;

    constexpr int kWidth = 440;
    constexpr int kRoomHeight = 330;
    constexpr int kButtonRow = 30;
    constexpr int kPadding = 10;
    constexpr int kDiscreteColumns = 8;

    /** Position in the top view: azimuth (degrees, 0 = front, positive = right) and radius (0 … 1). */
    struct Placement
    {
        float azimuth;
        float radius;
    };

    Placement placementOf (SpeakerRole role) noexcept
    {
        constexpr float bed = 0.86f, top = 0.5f, bottom = 0.99f;
        using R = SpeakerRole;
        switch (role)
        {
            case R::Unknown:
            case R::Mono:
            case R::Centre: return { 0.0f, bed };
            case R::Left: return { -30.0f, bed };
            case R::Right: return { 30.0f, bed };
            case R::LeftCentre: return { -15.0f, bed };
            case R::RightCentre: return { 15.0f, bed };
            case R::LeftWide: return { -60.0f, bed };
            case R::RightWide: return { 60.0f, bed };
            case R::LeftSurround: return { -110.0f, bed };
            case R::RightSurround: return { 110.0f, bed };
            case R::LeftSurroundSide: return { -90.0f, bed };
            case R::RightSurroundSide: return { 90.0f, bed };
            case R::LeftSurroundRear: return { -145.0f, bed };
            case R::RightSurroundRear: return { 145.0f, bed };
            case R::CentreSurround: return { 180.0f, bed };
            case R::Lfe: return { -12.0f, 0.66f };
            case R::Lfe2: return { 12.0f, 0.66f };
            case R::TopFrontLeft: return { -40.0f, top };
            case R::TopFrontRight: return { 40.0f, top };
            case R::TopFrontCentre: return { 0.0f, top };
            case R::TopMiddleLeft: return { -90.0f, 0.42f };
            case R::TopMiddleRight: return { 90.0f, 0.42f };
            case R::TopMiddleCentre: return { 0.0f, 0.0f };
            case R::TopRearLeft: return { -140.0f, top };
            case R::TopRearRight: return { 140.0f, top };
            case R::TopRearCentre: return { 180.0f, top };
            case R::BottomFrontLeft: return { -38.0f, bottom };
            case R::BottomFrontRight: return { 38.0f, bottom };
            case R::BottomFrontCentre: return { 8.0f, bottom };
            case R::ProximityLeft: return { -70.0f, 0.25f };
            case R::ProximityRight: return { 70.0f, 0.25f };
            case R::BottomSideLeft: return { -78.0f, bottom };
            case R::BottomSideRight: return { 78.0f, bottom };
            case R::BottomRearLeft: return { -128.0f, bottom };
            case R::BottomRearRight: return { 128.0f, bottom };
            case R::BottomRearCentre: return { 172.0f, bottom };
        }
        return { 0.0f, bed };
    }

    bool hasGroup (const engine::ChannelTopology& t, std::initializer_list<SpeakerGroup> groups) noexcept
    {
        for (int ch = 0; ch < t.numChannels; ++ch)
            for (const auto g : groups)
                if (t.groups[static_cast<std::size_t> (ch)] == g)
                    return true;
        return false;
    }

    int lfeChannels (const engine::ChannelTopology& t) noexcept
    {
        int n = 0;
        for (int ch = 0; ch < t.numChannels; ++ch)
            n += t.isLfe (ch) ? 1 : 0;
        return n;
    }

    constexpr std::initializer_list<SpeakerGroup> kBed { SpeakerGroup::Front, SpeakerGroup::Centre, SpeakerGroup::SideSurround,
                                                         SpeakerGroup::RearSurround, SpeakerGroup::Wides };
    constexpr std::initializer_list<SpeakerGroup> kTops { SpeakerGroup::TopFront, SpeakerGroup::TopMiddle, SpeakerGroup::TopRear };
    constexpr std::initializer_list<SpeakerGroup> kFront { SpeakerGroup::Front, SpeakerGroup::Centre, SpeakerGroup::Wides };
    constexpr std::initializer_list<SpeakerGroup> kSurround { SpeakerGroup::SideSurround, SpeakerGroup::RearSurround };
} // namespace

SpeakerPanel::SpeakerPanel (BandModel& m, int b, const engine::ChannelTopology& t)
    : model (m),
      band (b),
      topology (t)
{
    for (int ch = 0; ch < topology.numChannels; ++ch)
        discreteLayout = discreteLayout || topology.roles[static_cast<std::size_t> (ch)] == SpeakerRole::Unknown;

    for (auto* button : { &allButton, &noneButton, &bedButton, &topsButton, &frontButton, &surroundButton, &lfeButton })
    {
        button->setColour (juce::TextButton::buttonColourId, colours::panel.brighter (0.06f));
        button->setColour (juce::TextButton::buttonOnColourId, bandColour (band).withAlpha (0.85f));
        button->setColour (juce::TextButton::textColourOffId, colours::text);
        button->setColour (juce::TextButton::textColourOnId, colours::background);
        addAndMakeVisible (*button);
    }

    allButton.setTooltip ("Every speaker except LFE");
    noneButton.setTooltip ("No speaker: the band does nothing");
    bedButton.setTooltip ("Ear-level speakers (no heights)");
    topsButton.setTooltip ("Height speakers only");
    frontButton.setTooltip ("Front speakers (L, C, R, wides)");
    surroundButton.setTooltip ("Side and rear surrounds");
    lfeButton.setTooltip ("Include the LFE channel(s)");

    allButton.onClick = [this] {
        auto s = model.speakers (band);
        s.roles =
            (engine::SpeakerSelection::kAllRoles & ~engine::SpeakerSelection::kLfeRoles) | (s.roles & engine::SpeakerSelection::kLfeRoles);
        s.discrete = engine::SpeakerSelection::kAllDiscrete;
        apply (s, "Speakers: All");
    };
    noneButton.onClick = [this] { apply ({ 0, 0 }, "Speakers: None"); };
    bedButton.onClick = [this] { apply (withGroups (model.speakers (band), kBed), "Speakers: Bed"); };
    topsButton.onClick = [this] { apply (withGroups (model.speakers (band), kTops), "Speakers: Tops"); };
    frontButton.onClick = [this] { apply (withGroups (model.speakers (band), kFront), "Speakers: Front"); };
    surroundButton.onClick = [this] { apply (withGroups (model.speakers (band), kSurround), "Speakers: Surround"); };
    lfeButton.onClick = [this] {
        auto s = model.speakers (band);
        s.roles = s.includesLfe() ? (s.roles & ~engine::SpeakerSelection::kLfeRoles) : (s.roles | engine::SpeakerSelection::kLfeRoles);
        apply (s, "Speakers: LFE");
    };

    // Only the quick selections that mean something in this layout.
    bedButton.setVisible (! discreteLayout && hasGroup (topology, kTops));
    topsButton.setVisible (! discreteLayout && hasGroup (topology, kTops));
    frontButton.setVisible (! discreteLayout && hasGroup (topology, kSurround));
    surroundButton.setVisible (! discreteLayout && hasGroup (topology, kSurround));
    lfeButton.setVisible (lfeChannels (topology) > 0);
    lfeButton.setClickingTogglesState (false);

    const int rows = discreteLayout ? (topology.numChannels + kDiscreteColumns - 1) / kDiscreteColumns : 0;
    setSize (kWidth, (discreteLayout ? rows * 46 + 20 : kRoomHeight) + kButtonRow + 3 * kPadding);
    model.addListener (this);
    updateButtons();
}

SpeakerPanel::~SpeakerPanel()
{
    model.removeListener (this);
}

engine::SpeakerSelection SpeakerPanel::withGroups (const engine::SpeakerSelection& current, std::initializer_list<SpeakerGroup> groups)
{
    engine::SpeakerSelection s = current;
    s.roles = current.roles & engine::SpeakerSelection::kLfeRoles;
    for (int r = 0; r <= static_cast<int> (SpeakerRole::BottomRearCentre); ++r)
    {
        const auto role = static_cast<SpeakerRole> (r);
        for (const auto g : groups)
            if (engine::groupOf (role) == g && role != SpeakerRole::Unknown)
                s.roles |= engine::speakerBit (role);
    }
    return s;
}

engine::SpeakerSelection SpeakerPanel::toggled (const engine::SpeakerSelection& current, const engine::ChannelTopology& t, int channel)
{
    auto s = current;
    if (channel < 0 || channel >= t.numChannels)
        return s;
    const auto role = t.roles[static_cast<std::size_t> (channel)];
    if (role == SpeakerRole::Unknown)
        s.discrete ^= std::uint32_t { 1 } << channel;
    else
        s.roles ^= engine::speakerBit (role);
    return s;
}

juce::String SpeakerPanel::summary (const engine::SpeakerSelection& selection, const engine::ChannelTopology& t)
{
    const int total = t.numChannels;
    const int lfe = lfeChannels (t);
    const int selected = selection.count (t);
    if (selected == 0)
        return "None";
    if (selected == total)
        return lfe > 0 ? "All + LFE" : "All";
    if (selected == total - lfe && ! selection.includesLfe())
        return "All";
    return juce::String (selected) + "/" + juce::String (total);
}

void SpeakerPanel::apply (const engine::SpeakerSelection& selection, const juce::String& actionName)
{
    BandModel::ScopedEdit edit (model, actionName);
    model.setSpeakers (band, selection);
}

void SpeakerPanel::bandsChanged()
{
    updateButtons();
    repaint();
}

void SpeakerPanel::updateButtons()
{
    lfeButton.setToggleState (model.speakers (band).includesLfe(), juce::dontSendNotification);
}

juce::Rectangle<float> SpeakerPanel::room() const
{
    auto area = getLocalBounds().reduced (kPadding).toFloat();
    area.removeFromBottom (static_cast<float> (kButtonRow + kPadding));
    return area;
}

float SpeakerPanel::speakerRadius() const
{
    return 15.0f;
}

juce::Point<float> SpeakerPanel::centreOf (int channel) const
{
    const auto area = room();
    if (discreteLayout)
    {
        const int column = channel % kDiscreteColumns, row = channel / kDiscreteColumns;
        const float cell = area.getWidth() / kDiscreteColumns;
        return { area.getX() + (static_cast<float> (column) + 0.5f) * cell, area.getY() + 26.0f + static_cast<float> (row) * 46.0f };
    }
    const auto placement = placementOf (topology.roles[static_cast<std::size_t> (channel)]);
    const float radius = 0.5f * std::min (area.getWidth(), area.getHeight()) - speakerRadius() - 2.0f;
    const float angle = juce::degreesToRadians (placement.azimuth);
    return area.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (placement.radius * radius);
}

int SpeakerPanel::channelAt (juce::Point<float> p) const
{
    for (int ch = 0; ch < topology.numChannels; ++ch)
        if (centreOf (ch).getDistanceFrom (p) <= speakerRadius() + 2.0f)
            return ch;
    return -1;
}

void SpeakerPanel::mouseUp (const juce::MouseEvent& e)
{
    const int channel = channelAt (e.position);
    if (channel >= 0)
        apply (toggled (model.speakers (band), topology, channel), "Toggle Speaker");
}

void SpeakerPanel::mouseMove (const juce::MouseEvent& e)
{
    const int channel = channelAt (e.position);
    if (channel != hovered)
    {
        hovered = channel;
        setMouseCursor (channel >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpeakerPanel::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void SpeakerPanel::resized()
{
    auto row = getLocalBounds().reduced (kPadding).removeFromBottom (kButtonRow).reduced (0, 3);
    juce::Array<juce::TextButton*> visible;
    for (auto* button : { &allButton, &noneButton, &bedButton, &topsButton, &frontButton, &surroundButton, &lfeButton })
        if (button->isVisible())
            visible.add (button);
    const int width = (row.getWidth() - 4 * (visible.size() - 1)) / std::max (1, visible.size());
    for (auto* button : visible)
    {
        button->setBounds (row.removeFromLeft (width));
        row.removeFromLeft (4);
    }
}

void SpeakerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto colour = bandColour (band);
    const auto selection = model.speakers (band);

    g.setColour (colours::text);
    g.setFont (uiFont (12.0f, true));
    g.drawText ("Band " + juce::String (band + 1) + " speakers", getLocalBounds().reduced (kPadding).removeFromTop (16),
                juce::Justification::centredLeft);

    const auto area = room();
    if (! discreteLayout)
    {
        // Room outline, the listener and the bed / height rings.
        const float radius = 0.5f * std::min (area.getWidth(), area.getHeight()) - speakerRadius() - 2.0f;
        const auto centre = area.getCentre();
        g.setColour (colours::grid);
        g.drawEllipse (juce::Rectangle<float> (2.0f * radius * 0.86f, 2.0f * radius * 0.86f).withCentre (centre), 1.0f);
        g.drawEllipse (juce::Rectangle<float> (2.0f * radius * 0.5f, 2.0f * radius * 0.5f).withCentre (centre), 1.0f);
        g.setColour (colours::textDim);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (centre));
        g.setFont (uiFont (10.0f));
        g.drawText ("front", juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ centre.x, area.getY() + 6.0f }),
                    juce::Justification::centred);
    }

    for (int ch = 0; ch < topology.numChannels; ++ch)
    {
        const auto role = topology.roles[static_cast<std::size_t> (ch)];
        const bool on = selection.selects (topology, ch);
        const auto bounds = juce::Rectangle<float> (2.0f * speakerRadius(), 2.0f * speakerRadius()).withCentre (centreOf (ch));
        const bool lfe = topology.isLfe (ch);
        const bool top = engine::groupOf (role) == SpeakerGroup::TopFront || engine::groupOf (role) == SpeakerGroup::TopMiddle
            || engine::groupOf (role) == SpeakerGroup::TopRear;

        g.setColour (on ? colour.withAlpha (ch == hovered ? 1.0f : 0.85f) : colours::panel.brighter (ch == hovered ? 0.22f : 0.1f));
        if (lfe)
            g.fillRoundedRectangle (bounds, 4.0f);
        else
            g.fillEllipse (bounds);
        g.setColour (on ? colour.brighter (0.3f) : colours::panelEdge.brighter (0.3f));
        if (lfe)
        {
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }
        else
        {
            if (top)
                g.drawEllipse (bounds.reduced (2.0f), 1.0f); // heights: a double ring
            g.drawEllipse (bounds, 1.0f);
        }

        const juce::String label = role == SpeakerRole::Unknown ? juce::String (ch + 1) : juce::String (engine::speakerLabel (role));
        g.setColour (on ? colours::background : colours::text);
        g.setFont (uiFont (label.length() > 3 ? 9.0f : 10.5f, on));
        g.drawText (label, bounds.expanded (6.0f, 0.0f), juce::Justification::centred);
    }
}
} // namespace ssc::ui
