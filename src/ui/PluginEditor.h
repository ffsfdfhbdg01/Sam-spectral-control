#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/AnalyzerSettings.h"
#include "ui/BandModel.h"
#include "ui/BandPanel.h"
#include "ui/ControlBar.h"
#include "ui/Controls.h"
#include "ui/EqDisplay.h"
#include "ui/Motion.h"
#include "ui/PresetBar.h"
#include "ui/Theme.h"

namespace ssc::plugin
{
class SscAudioProcessor;
}

namespace ssc::ui
{
/** Latency and CPU in the header; flashes once when the latency changes (ADR-0011). */
class StatusChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setText (const juce::String& newText);
    void setHighlighted (bool highlighted);
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    bool highlighted = false;
};

/**
    The editor (research §15). Three levels of information:
    - always visible: header (presets, A/B, Undo/Redo, status, Analyzer, settings, help), the display
      with its curves and nodes, the bottom bar (power, Phase, Delta, Auto Gain, Mix, Output);
    - on demand: the band panel under the node of the selected band, with the knobs of its mode;
    - advanced, behind an explicit "Advanced" button: the band's flyout (detector and readouts)
      and the global panel (Quality, oversampling, Max Lookahead, Auto Gain options, meters).
    Panels slide and fade with the motion rules of research §15 d9 (Full / Reduced / Off).
*/
class SscEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SscEditor (plugin::SscAudioProcessor&);
    ~SscEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Updates all live views without a display (used by offscreen screenshot/test tools). */
    void refreshLiveViews();

    BandModel& getBandModel() noexcept { return bandModel; }

    /** The band's Advanced flyout and the global Advanced panel (both saved with the state). */
    void setBandAdvancedOpen (bool open);
    void setGlobalAdvancedOpen (bool open);
    bool isBandAdvancedOpen() const noexcept { return bandPanel.isAdvancedOpen(); }
    bool isGlobalAdvancedOpen() const noexcept { return controlBar.isAdvancedOpen(); }

    /** Animations: Full, Reduced (cross-fades only) or Off (saved with the state). */
    void setMotionLevel (motion::Level level);

    const BandPanel& getBandPanel() const noexcept { return bandPanel; }
    const BandAdvancedPanel& getBandAdvancedPanel() const noexcept { return bandAdvanced; }

    static constexpr int kDefaultWidth = 1100;
    static constexpr int kDefaultHeight = 660;
    static constexpr int kWideWidth = 1000; ///< from this width the header shows the version and long labels
    static constexpr int kHeaderHeight = 40;
    static constexpr int kControlBarHeight = 56;

private:
    void timerCallback() override;
    juce::String statusText (bool full) const;
    juce::String statusDetails() const;
    void layoutPanels (bool animate);
    void setEqRange (float rangeDb);
    void toggleAnalyzer();
    void showAnalyzerMenu();
    void showMeterMenu();
    void showSettingsMenu();
    void showHelpMenu();
    void updateAdvancedIndicator();
    /** Applies analyzer settings to the analyzer and the display; `store` also saves them in the state. */
    void applyAnalyzerSettings (const plugin::AnalyzerSettings& settings, bool store);

    plugin::SscAudioProcessor& sscProcessor;
    LookAndFeel lookAndFeel;
    StatusChip status;
    ChipButton analyzerButton { "Analyzer" }, analyzerMenuButton { {}, Icon::ChevronDown };
    IconButton settingsButton { Icon::Settings, "Settings" }, helpButton { Icon::Help, "Help" };
    ValueButton eqRangeButton;
    plugin::AnalyzerSettings analyzerSettings;
    plugin::AnalyzerMode lastAnalyzerMode = plugin::AnalyzerMode::PrePost;
    juce::TooltipWindow tooltips { this, 600 };
    BandModel bandModel;
    PresetBar presetBar;
    EqDisplay display;
    BandAdvancedPanel bandAdvanced;
    BandPanel bandPanel;
    GlobalAdvancedPanel globalAdvanced;
    ControlBar controlBar;
    motion::ComponentMotion bandPanelMotion { bandPanel }, bandAdvancedMotion { bandAdvanced }, globalAdvancedMotion { globalAdvanced };
    float eqRangeDb = 12.0f;
    int shownLatency = -1;
    int latencyHighlightTicks = 0; ///< the status flashes for a moment when the latency changes (ADR-0011)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SscEditor)
};
} // namespace ssc::ui
