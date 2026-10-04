#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ssc/engine/ChannelTopology.h"
#include "ui/BandModel.h"

namespace ssc::ui
{
/**
    Loudspeaker selection of a band in surround and immersive layouts (research §4c, CH-4).

    A top view of the room: bed speakers on the outer ring, height speakers on the inner ring,
    LFE in front of the listener, each labelled with its Dolby name and toggled by a click.
    Quick selections: All (every speaker but LFE), None, Bed, Tops, Front, Surround and an LFE
    switch. Discrete layouts (channels without roles) show numbered channels instead. The
    selection is stored by role, so it carries over to other layouts; every change is one undo step.
*/
class SpeakerPanel final : public juce::Component, private BandModel::Listener
{
public:
    SpeakerPanel (BandModel& model, int band, const engine::ChannelTopology& topology);
    ~SpeakerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Short description of a selection for a layout ("All", "Bed", "5/12", …). */
    static juce::String summary (const engine::SpeakerSelection& selection, const engine::ChannelTopology& topology);

    /** Selection with every speaker of the given groups (and the LFE state of `current`). */
    static engine::SpeakerSelection withGroups (const engine::SpeakerSelection& current,
                                                std::initializer_list<engine::SpeakerGroup> groups);

    /** Toggles one channel of a layout (by role, or by index for discrete channels). */
    static engine::SpeakerSelection toggled (const engine::SpeakerSelection& current, const engine::ChannelTopology& topology, int channel);

private:
    void bandsChanged() override;
    void apply (const engine::SpeakerSelection& selection, const juce::String& actionName);
    void updateButtons();

    juce::Rectangle<float> room() const;
    juce::Point<float> centreOf (int channel) const;
    float speakerRadius() const;
    int channelAt (juce::Point<float> p) const;

    BandModel& model;
    int band;
    engine::ChannelTopology topology;
    bool discreteLayout = false;
    int hovered = -1;

    juce::TextButton allButton { "All" }, noneButton { "None" }, bedButton { "Bed" }, topsButton { "Tops" }, frontButton { "Front" },
        surroundButton { "Surround" }, lfeButton { "LFE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerPanel)
};
} // namespace ssc::ui
