#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ssc/analysis/BandReadouts.h"
#include "ssc/engine/ChannelTopology.h"
#include "ui/BandModel.h"
#include "ui/Controls.h"

namespace ssc::ui
{
/** Rotary control of one band parameter: caption above, value below (click it to type a value).
    A drag, a typed value or a double click (default value) is one undo step. */
class BandKnob final : public juce::Component
{
public:
    BandKnob (BandModel& model, plugin::BandParam param, const juce::String& caption, bool bipolar = false);
    ~BandKnob() override;

    plugin::BandParam getParam() const noexcept { return param; }
    /** Binds range, text conversion and default value of the band's parameter. */
    void setBand (int band);
    /** Shows the model's value (not while dragging). */
    void refresh();
    /** Ends an edit left open by a drag (the band changed meanwhile). */
    void endDrag();
    void setColour (juce::Colour colour);
    juce::Slider& getSlider() noexcept { return slider; }

    void resized() override;

private:
    BandModel& model;
    plugin::BandParam param;
    juce::String caption;
    int band = -1;
    bool dragging = false;
    bool updating = false;
    juce::Label label;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
};

/** ValueButton bound to a choice parameter of a band (Shape, Slope, Target, Character, Link);
    each choice is one undo step. */
class BandChoiceButton final : public ValueButton
{
public:
    BandChoiceButton (BandModel& model, plugin::BandParam param, const juce::String& caption);

    plugin::BandParam getParam() const noexcept { return param; }
    void setBand (int band);
    void refresh();
    int getIndex() const;

    /** Choices offered, in menu order (default: all). */
    std::function<std::vector<int>()> itemsToShow;
    /** Whether a choice can be picked (default: all). */
    std::function<bool (int)> isItemEnabled;

private:
    void choose (int index);

    BandModel& model;
    plugin::BandParam param;
    juce::String caption;
    int band = -1;
};

/**
    Panel of the focused band (BAND-7, research §15 d4), shown under its node.

    Header: band, mode (Static | Dynamic | Spectral), Advanced, bypass, solo and a menu (copy,
    paste, reset, delete). EQ row: shape and slope, Freq / Gain / Q, target and Character (and the
    speakers in surround). Dynamic and Spectral bands add a dynamics row whose knobs follow the
    mode: Dynamic — Threshold, Ratio, Range, Attack, Release; Spectral — Threshold, Selectivity,
    Range, Attack, Release; with the band's Direction (Down | Up: cut or boost, e.g. upward
    compression) and Auto Gain on its title line. The detector details and the readouts live in
    the Advanced flyout (BandAdvancedPanel). All edits go through BandModel (undo).
*/
class BandPanel final : public juce::Component, private BandModel::Listener
{
public:
    explicit BandPanel (BandModel& model);
    ~BandPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Size for the focused band: the width follows the channel layout, the height the band mode. */
    juce::Point<int> getPreferredSize() const;

    /** Called when the band, its preferred size or the Advanced state changes. */
    std::function<void()> onLayoutChanged;

    /** Channel layout of the host bus: Target choices and the speakers follow it (CH-4). */
    void setTopology (const engine::ChannelTopology& topology);

    int getBand() const noexcept { return band; }

    /** The Advanced toggle (the flyout belongs to the editor). */
    bool isAdvancedOpen() const noexcept { return advancedButton.getToggleState(); }
    void setAdvancedOpen (bool open);

    /** Source of the readouts of a band (polled for the Advanced indicator). */
    std::function<analysis::BandReadout (int band)> readoutProvider;
    /** Whether the host feeds the Sidechain input (polled with the readouts). */
    std::function<bool()> sidechainConnected;

    /** Band parameters with a control in the panel (research §15 e, E2). */
    std::vector<plugin::BandParam> visibleParameters() const;

    /** True when a setting of the Advanced flyout differs from its default for the band's mode. */
    static bool advancedDiffersFromDefault (const BandModel& model, int band);

    /** True while the section title points out a band on External without a Sidechain input. */
    bool showsMissingSidechain() const noexcept { return sidechainMissing; }

    /** What a Dynamic or Spectral band does, for its section title: "COMPRESSION" (Down, ratio over
        1:1), "UPWARD COMPRESSION" (Up), "EXPANSION" (Down, ratio under 1:1) or "UPWARD EXPANSION". */
    static juce::String dynamicsKind (bool upward, bool expand);

    static constexpr int kStaticHeight = 132;
    static constexpr int kDynamicHeight = 232;

private:
    void bandsChanged() override;
    void selectionChanged() override;
    void bindBand();
    void updateFromModel();
    void updateIndicator();
    void showMoreMenu();
    void openSpeakerPanel();
    void pollReadout();
    void updateSidechainState();
    void notifyLayout();

    BandModel& model;
    int band = -1;
    BandMode shownMode = BandMode::Static;
    bool slopeShown = false;
    engine::ChannelTopology topology = engine::ChannelTopology::stereo();
    int numPairs = 1;
    bool readoutWarning = false;
    bool sidechainMissing = false; ///< the band is set to External, the host feeds no Sidechain input
    bool soloHeld = false;
    bool soloWasOn = false;
    double soloPressMs = 0.0;

    SegmentedControl modeSelector { { "Static", "Dynamic", "Spectral" } };
    ChipButton advancedButton { "Advanced", Icon::ChevronRight };
    IconButton bypassButton { Icon::Power, "Bypass" }, soloButton { Icon::Solo, "Solo" }, moreButton { Icon::More, "More" };
    BandChoiceButton shape, slope, target, character;
    ValueButton speakers { "Speakers" };
    BandKnob freq, gain, q;
    ChipButton autoGainButton { "Auto Gain" };
    SegmentedControl directionSelector { { "Down", "Up" } };
    BandKnob threshold, ratio, range, attack, release, selectivity;
    juce::TimedCallback readoutTimer { [this] { pollReadout(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};

/**
    Advanced flyout of the focused band (research §15 d4): it slides out to the right of the band
    panel. Dynamic and Spectral bands: threshold mode, knee, lookahead (Dynamic) or ratio (Spectral),
    mix, detector link and sidechain (Internal or External, DYN-4). Every band: its readouts (research §12c, PR-1) — mean excess
    phase, group delay at fc, pre-/post-ring at −60 dB (and at the full Range for Dynamic bands, the
    STFT pre-echo for Spectral bands), highlighted when the ringing is likely audible
    (post > 50 ms, pre > 5 ms).
*/
class BandAdvancedPanel final : public juce::Component, public juce::SettableTooltipClient, private BandModel::Listener
{
public:
    explicit BandAdvancedPanel (BandModel& model);
    ~BandAdvancedPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int kWidth = 248;
    /** Height of the content for the focused band. */
    int getPreferredHeight() const;

    /** Source of the readouts of a band (polled 10 times a second while shown). */
    std::function<analysis::BandReadout (int band)> readoutProvider;
    /** Whether the host feeds the Sidechain input (polled with the readouts); without it a band set
        to External hears silence, which the flyout points out. */
    std::function<bool()> sidechainConnected;

    /** Band parameters with a control in the flyout (tests). */
    std::vector<plugin::BandParam> visibleParameters() const;
    /** True while the Sidechain choice points out that the host feeds no Sidechain input. */
    bool showsSidechainHint() const noexcept { return sidechainHint; }

    /** Readout lines: label and value, e.g. {"Group delay", "0.12 ms"}. */
    static std::vector<std::pair<juce::String, juce::String>> readoutLines (const analysis::BandReadout& readout);
    static bool readoutWarns (const analysis::BandReadout& readout) noexcept;

private:
    void bandsChanged() override;
    void selectionChanged() override;
    void bindBand();
    void updateFromModel();
    void pollReadout();
    void updateSidechainHint();
    void showSidechainHint (bool hint);
    bool showsDetector() const;

    BandModel& model;
    int band = -1;
    BandMode shownMode = BandMode::Static;
    analysis::BandReadout readout;
    bool sidechainHint = false;

    SegmentedControl thresholdMode { { "Absolute", "Relative" } };
    BandKnob knee, lookahead, ratio, mix;
    BandChoiceButton link, sidechain;
    juce::TimedCallback readoutTimer { [this] { pollReadout(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandAdvancedPanel)
};
} // namespace ssc::ui
