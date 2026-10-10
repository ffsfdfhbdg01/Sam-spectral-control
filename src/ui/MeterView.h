#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/MeterService.h"

namespace ssc::ui
{
/**
    THD+N and aliasing meters in the global Advanced panel (research §13c, §15 d5, MTR-1, MTR-4):
    two short lines refreshed at 10 Hz, the value in the warning colour above its threshold, the
    bands that contribute in the tooltip; a click opens the warning thresholds. Values below
    −120 dB read "< −120 dB". The readings keep updating while the panel is closed, so the
    Advanced button can show a warning.
*/
class MeterView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    MeterView();

    /** Newest readings (message thread). */
    std::function<plugin::MeterReadings()> readingsProvider;
    /** Short description of a band for the tooltip, e.g. "Bell 1.2 kHz, Warm". */
    std::function<juce::String (int band)> bandDescription;
    /** Called when the oversampling suggestion changes (OS-5): the factor to suggest, 0 for none. */
    std::function<void (int factor, float aliasingDb)> onOversamplingHint;

    void setThresholds (float thdWarningDb, float aliasingWarningDb);

    /** Called on a click (the editor opens the warning thresholds). */
    std::function<void()> onClick;

    /** True while a reading is above its warning threshold. */
    bool hasWarning() const noexcept;

    /** Pulls the newest readings (the timer does this; offscreen tools call it directly). */
    void refresh();

    const plugin::MeterReadings& getReadings() const noexcept { return readings; }
    int getOversamplingHint() const noexcept { return hint; }

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** "−52.3 dB", or "< −120 dB" below the display floor. */
    static juce::String formatDb (float db);
    /** THD+N as a percentage: 100·10^(dB/20). */
    static juce::String formatPercent (float db);

private:
    void timerCallback() override { refresh(); }
    void updateTooltip();

    plugin::MeterReadings readings;
    float thdWarning = -40.0f, aliasingWarning = -80.0f;
    int hint = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterView)
};
} // namespace ssc::ui
