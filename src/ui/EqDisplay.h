#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/AnalyzerService.h"

namespace ssc::ui
{
/**
    Central display: frequency/level grid and the pre/post spectrum analyzer.
    Band nodes and curves are added in M2; glow and resonance hints in M7.
*/
class EqDisplay final : public juce::Component
{
public:
    explicit EqDisplay (plugin::AnalyzerService& analyzer);

    void paint (juce::Graphics&) override;

    /** Pulls the newest analyzer frame; called on every vblank (and by offscreen tools). */
    void refreshFromAnalyzer();

    static constexpr float kMinHz = 10.0f;
    static constexpr float kMaxHz = 30000.0f;
    static constexpr float kEqRangeDb = 18.0f;
    static constexpr float kAnalyzerTopDb = 6.0f;
    static constexpr float kAnalyzerBottomDb = -96.0f;

    float xForFrequency (float hz) const noexcept;
    float frequencyForX (float x) const noexcept;
    float yForEqDb (float db) const noexcept;
    float yForAnalyzerDb (float db) const noexcept;

private:
    void paintGrid (juce::Graphics&) const;
    void paintSpectrum (juce::Graphics&, const plugin::SpectrumFrame&, bool post) const;

    plugin::AnalyzerService& analyzer;
    bool hasFrame = false;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};
} // namespace ssc::ui
