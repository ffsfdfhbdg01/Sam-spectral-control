#pragma once

#include <functional>

#include <juce_data_structures/juce_data_structures.h>

namespace ssc::plugin
{
/** Which spectra the analyzer shows (VIS-1). */
enum class AnalyzerMode : int
{
    PrePost,
    Post,
    Pre,
    Off
};

/** FFT length: 2048 / 4096 / 8192 / 16384 points at 48 kHz, scaled with the sample rate. */
enum class AnalyzerResolution : int
{
    Low,
    Medium,
    High,
    Max
};

/** Averaging time constant: 50 / 120 / 250 / 800 / 2000 ms. */
enum class AnalyzerSpeed : int
{
    VeryFast,
    Fast,
    Medium,
    Slow,
    VerySlow
};

/** Resonance hint sensitivity: a peak must stand 12 / 9 / 6 dB above its surroundings (VIS-5). */
enum class HintSensitivity : int
{
    Low,
    Medium,
    High
};

/**
    Analyzer and display settings (research §11c, VIS-1, VIS-2, VIS-4). They do not change the sound, so
    they are not host parameters: they are saved with the state in the "Analyzer" child of the
    state tree and are not part of Undo.
*/
struct AnalyzerSettings
{
    AnalyzerMode mode = AnalyzerMode::PrePost;
    AnalyzerResolution resolution = AnalyzerResolution::Medium;
    AnalyzerSpeed speed = AnalyzerSpeed::Medium;
    float tiltDbPerOctave = 4.5f;
    float rangeDb = 90.0f; ///< display range below +6 dBFS
    bool peakHold = false;
    bool freeze = false;
    bool glow = true;
    bool resonanceHints = true;
    HintSensitivity hintSensitivity = HintSensitivity::Medium;
    float thdWarningDb = -40.0f;      ///< THD+N meter turns to the warning colour above this (MTR-4)
    float aliasingWarningDb = -80.0f; ///< aliasing meter turns to the warning colour above this (MTR-4)

    bool operator== (const AnalyzerSettings& other) const noexcept
    {
        constexpr std::equal_to<float> same; // exact: the values come from fixed lists
        return mode == other.mode && resolution == other.resolution && speed == other.speed && same (tiltDbPerOctave, other.tiltDbPerOctave)
            && same (rangeDb, other.rangeDb) && peakHold == other.peakHold && freeze == other.freeze && glow == other.glow
            && resonanceHints == other.resonanceHints && hintSensitivity == other.hintSensitivity && same (thdWarningDb, other.thdWarningDb)
            && same (aliasingWarningDb, other.aliasingWarningDb);
    }

    bool showsPre() const noexcept { return mode == AnalyzerMode::PrePost || mode == AnalyzerMode::Pre; }
    bool showsPost() const noexcept { return mode == AnalyzerMode::PrePost || mode == AnalyzerMode::Post; }
};

inline constexpr float kAnalyzerTilts[] = { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };
inline constexpr float kAnalyzerRanges[] = { 60.0f, 90.0f, 120.0f };
inline constexpr float kThdWarnings[] = { -20.0f, -30.0f, -40.0f, -50.0f, -60.0f };
inline constexpr float kAliasingWarnings[] = { -60.0f, -70.0f, -80.0f, -90.0f, -100.0f };

int analyzerFftSize (AnalyzerResolution resolution, double sampleRate) noexcept;
double analyzerAveragingMs (AnalyzerSpeed speed) noexcept;
double hintProminenceDb (HintSensitivity sensitivity) noexcept;

inline const juce::Identifier kAnalyzerType { "Analyzer" };

/** Settings stored in the state (defaults for missing or invalid values). */
AnalyzerSettings readAnalyzerSettings (const juce::ValueTree& state);
void writeAnalyzerSettings (juce::ValueTree& state, const AnalyzerSettings& settings);
} // namespace ssc::plugin
