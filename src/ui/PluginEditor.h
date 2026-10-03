#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/ControlBar.h"
#include "ui/EqDisplay.h"
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

    static constexpr int kDefaultWidth = 1100;
    static constexpr int kDefaultHeight = 660;

private:
    void timerCallback() override;
    juce::String statusText() const;

    plugin::SscAudioProcessor& processor;
    LookAndFeel lookAndFeel;
    juce::Label status;
    juce::TooltipWindow tooltips { this, 600 };
    EqDisplay display;
    ControlBar controlBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SscEditor)
};
} // namespace ssc::ui
