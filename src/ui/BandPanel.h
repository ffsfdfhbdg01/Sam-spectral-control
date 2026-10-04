#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ssc/engine/ChannelTopology.h"
#include "ui/BandModel.h"

namespace ssc::ui
{
/**
    Floating panel with every parameter of the focused band (BAND-7). Dynamic parameters appear
    when the band mode is Dynamic or Spectral. All edits go through BandModel, so each knob drag,
    typed value or menu choice is one undo step. Readouts of section 12 (phase, ringing) are
    added in M7.
*/
class BandPanel final : public juce::Component, private BandModel::Listener
{
public:
    explicit BandPanel (BandModel& model);
    ~BandPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Size the panel wants for the focused band. */
    juce::Point<int> getPreferredSize() const;

    /** Called when the preferred size changes (band, shape or mode changed). */
    std::function<void()> onPreferredSizeChanged;

    /** Channel layout of the host bus: Target choices and the speaker panel follow it (CH-4). */
    void setTopology (const engine::ChannelTopology& topology);

    int getBand() const noexcept { return band; }

private:
    struct Knob
    {
        plugin::BandParam param;
        juce::String title;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        bool dragging = false;
        bool dynamic = false;
    };

    struct Choice
    {
        plugin::BandParam param;
        juce::String title;
        juce::ComboBox box;
        bool dynamic = false;
    };

    void bandsChanged() override;
    void selectionChanged() override;

    Knob& addKnob (plugin::BandParam, const juce::String& title, bool dynamic);
    Choice& addChoice (plugin::BandParam, const juce::String& title, bool dynamic);
    void bind();
    void updateFromModel();
    void rebuildChoiceItems (Choice&);
    bool showsDynamics() const;
    void layoutChanged();

    BandModel& model;
    int band = -1;
    bool updating = false;
    int numPairs = 1;
    engine::ChannelTopology topology = engine::ChannelTopology::stereo();
    void openSpeakerPanel();
    Shape itemsShape = Shape::Bell;
    bool lastDynamics = false;

    juce::Label title;
    juce::TextButton onButton { "On" }, soloButton { "Solo" }, autoGainButton { "Auto Gain" }, deleteButton { "Delete" },
        speakersButton { "Speakers" };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};
} // namespace ssc::ui
