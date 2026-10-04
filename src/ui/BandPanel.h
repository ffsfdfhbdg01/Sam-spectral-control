#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ssc/analysis/BandReadouts.h"
#include "ssc/engine/ChannelTopology.h"
#include "ui/BandModel.h"

namespace ssc::ui
{
/**
    Floating panel with every parameter of the focused band (BAND-7). Dynamic parameters appear
    when the band mode is Dynamic or Spectral. All edits go through BandModel, so each knob drag,
    typed value or menu choice is one undo step. The bottom line shows the band's readouts
    (research §12c, PR-1): mean excess phase, group delay at fc, pre-/post-ring at −60 dB (and at
    the full range for Dynamic bands, the STFT pre-echo for Spectral bands), highlighted when the
    ringing is likely audible (post > 50 ms, pre > 5 ms).
*/
class BandPanel final : public juce::Component, private BandModel::Listener, private juce::Timer
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

    /** Source of the readouts of a band (polled 10 times a second). */
    std::function<analysis::BandReadout (int band)> readoutProvider;

    /** Readout line for a band ("" while it is being computed). */
    static juce::String readoutText (const analysis::BandReadout& readout);
    static bool readoutWarns (const analysis::BandReadout& readout) noexcept;

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
    void timerCallback() override;

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
    juce::Label readoutLabel;
    juce::TextButton onButton { "On" }, soloButton { "Solo" }, autoGainButton { "Auto Gain" }, deleteButton { "Delete" },
        speakersButton { "Speakers" };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};
} // namespace ssc::ui
