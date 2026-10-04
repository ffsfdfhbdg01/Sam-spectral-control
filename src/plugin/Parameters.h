#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ssc/engine/Types.h"

namespace ssc::plugin
{
/** Stable parameter identifiers. Never rename or reuse an ID after a release. */
namespace id
{
    inline constexpr const char* bypass = "bypass";
    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* phaseMode = "phase_mode";
    inline constexpr const char* analogModel = "analog_model";
    inline constexpr const char* quality = "quality";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* autoGain = "auto_gain";
    inline constexpr const char* autoGainWeighting = "auto_gain_weighting";
    inline constexpr const char* autoGainSpeed = "auto_gain_speed";
    inline constexpr const char* maxLookahead = "max_lookahead";
    inline constexpr const char* renderOversampling = "render_oversampling";
} // namespace id

enum class BandParam
{
    Used,
    On,
    Shape,
    Frequency,
    Gain,
    Q,
    Slope,
    Mode,
    Target,
    Character,
    AutoGain,
    ThresholdMode,
    Threshold,
    Ratio,
    Direction,
    Range,
    Attack,
    Release,
    Lookahead,
    Knee,
    Mix,
    Selectivity,
    Link,
    Count
};

inline constexpr int kNumBandParams = static_cast<int> (BandParam::Count);

/** "b01_freq" style identifiers; bandIndex is zero-based. */
juce::String bandParamId (int bandIndex, BandParam param);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Choice labels, in enum order (see ssc/engine/Types.h). */
namespace labels
{
    const juce::StringArray& shapes();
    const juce::StringArray& slopes();
    const juce::StringArray& bandModes();
    const juce::StringArray& targets();
    const juce::StringArray& characters();
    const juce::StringArray& thresholdModes();
    const juce::StringArray& directions();
    const juce::StringArray& links();
    const juce::StringArray& phaseModes();
    const juce::StringArray& analogModels();
    const juce::StringArray& qualities();
    const juce::StringArray& oversampling();
    const juce::StringArray& renderOversampling(); ///< "Same", then the oversampling() choices
    const juce::StringArray& autoGainWeightings();
    const juce::StringArray& autoGainSpeeds();
} // namespace labels

/** Text formatting shared by parameters and UI. */
namespace format
{
    juce::String frequency (float hz);
    juce::String decibels (float db, bool showPlus = true);
    juce::String ratio (float ratio);
    juce::String milliseconds (float ms);
    juce::String percent (float value);

    /** Inverse of frequency(): accepts "1500", "1.5k", "1.5 kHz". */
    float parseFrequency (const juce::String& text);
    /** Inverse of milliseconds(): accepts "12", "12 ms", "1.2 s". */
    float parseMilliseconds (const juce::String& text);
} // namespace format

/** Raw atomic values for lock-free reads on the audio thread. */
struct ParameterPointers
{
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* phaseMode = nullptr;
    std::atomic<float>* analogModel = nullptr;
    std::atomic<float>* quality = nullptr;
    std::atomic<float>* oversampling = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* autoGainWeighting = nullptr;
    std::atomic<float>* autoGainSpeed = nullptr;
    std::atomic<float>* maxLookahead = nullptr;
    std::atomic<float>* renderOversampling = nullptr;
    std::array<std::array<std::atomic<float>*, kNumBandParams>, kNumBands> bands {};

    void attach (juce::AudioProcessorValueTreeState& state);

    float band (int bandIndex, BandParam p) const noexcept
    {
        return bands[static_cast<std::size_t> (bandIndex)][static_cast<std::size_t> (p)]->load (std::memory_order_relaxed);
    }
};
} // namespace ssc::plugin
