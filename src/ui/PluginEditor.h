#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/AnalyzerSettings.h"
#include "ui/BandModel.h"
#include "ui/BandPanel.h"
#include "ui/ControlBar.h"
#include "ui/EqDisplay.h"
#include "ui/PresetBar.h"
#include "ui/Theme.h"

namespace ssc::plugin
{
class SscAudioProcessor;
}

namespace ssc::ui
{
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

    static constexpr int kDefaultWidth = 1100;
    static constexpr int kDefaultHeight = 660;

private:
    void timerCallback() override;
    juce::String statusText() const;

    int shownLatency = -1;
    int latencyHighlightTicks = 0; ///< the status flashes for a moment when the latency changes (ADR-0011)
    void layoutBandPanel();
    void setEqRange (float rangeDb);
    void showAnalyzerMenu();
    /** Applies analyzer settings to the analyzer and the display; `store` also saves them in the state. */
    void applyAnalyzerSettings (const plugin::AnalyzerSettings& settings, bool store);

    plugin::SscAudioProcessor& sscProcessor;
    LookAndFeel lookAndFeel;
    juce::Label status;
    juce::ComboBox eqRange;
    juce::TextButton analyzerButton { "Analyzer" };
    plugin::AnalyzerSettings analyzerSettings;
    juce::TooltipWindow tooltips { this, 600 };
    BandModel bandModel;
    PresetBar presetBar;
    EqDisplay display;
    BandPanel bandPanel;
    ControlBar controlBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SscEditor)
};
} // namespace ssc::ui
