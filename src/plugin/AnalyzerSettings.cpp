#include "plugin/AnalyzerSettings.h"

#include <algorithm>
#include <cmath>

namespace ssc::plugin
{
namespace
{
    const juce::Identifier kMode { "mode" };
    const juce::Identifier kResolution { "resolution" };
    const juce::Identifier kSpeed { "speed" };
    const juce::Identifier kTilt { "tilt" };
    const juce::Identifier kRange { "range" };
    const juce::Identifier kPeakHold { "peakHold" };
    const juce::Identifier kFreeze { "freeze" };
    const juce::Identifier kGlow { "glow" };
    const juce::Identifier kHints { "hints" };
    const juce::Identifier kHintSensitivity { "hintSensitivity" };
    const juce::Identifier kThdWarning { "thdWarning" };
    const juce::Identifier kAliasingWarning { "aliasingWarning" };
    const juce::Identifier kPhaseOverlay { "phaseOverlay" };

    template <typename Enum>
    Enum readChoice (const juce::ValueTree& tree, const juce::Identifier& id, Enum fallback, Enum last)
    {
        const int value = tree.getProperty (id, static_cast<int> (fallback));
        return value >= 0 && value <= static_cast<int> (last) ? static_cast<Enum> (value) : fallback;
    }

    template <std::size_t N>
    float readListed (const juce::ValueTree& tree, const juce::Identifier& id, const float (&allowed)[N], float fallback)
    {
        const auto value = static_cast<float> (static_cast<double> (tree.getProperty (id, fallback)));
        for (const float a : allowed)
            if (std::abs (a - value) < 1.0e-3f)
                return a;
        return fallback;
    }
} // namespace

int analyzerFftSize (AnalyzerResolution resolution, double sampleRate) noexcept
{
    const int atBase = 2048 << static_cast<int> (resolution);
    const double scaled = atBase * std::max (sampleRate, 8000.0) / 48000.0;
    int size = 1024;
    while (size < 131072 && size * 1.5 < scaled)
        size *= 2;
    return size;
}

double analyzerAveragingMs (AnalyzerSpeed speed) noexcept
{
    switch (speed)
    {
        case AnalyzerSpeed::VeryFast: return 50.0;
        case AnalyzerSpeed::Fast: return 120.0;
        case AnalyzerSpeed::Medium: return 250.0;
        case AnalyzerSpeed::Slow: return 800.0;
        case AnalyzerSpeed::VerySlow: return 2000.0;
    }
    return 250.0;
}

double hintProminenceDb (HintSensitivity sensitivity) noexcept
{
    switch (sensitivity)
    {
        case HintSensitivity::Low: return 12.0;
        case HintSensitivity::Medium: return 9.0;
        case HintSensitivity::High: return 6.0;
    }
    return 9.0;
}

AnalyzerSettings readAnalyzerSettings (const juce::ValueTree& state)
{
    AnalyzerSettings s;
    const auto tree = state.getChildWithName (kAnalyzerType);
    if (! tree.isValid())
        return s;
    s.mode = readChoice (tree, kMode, s.mode, AnalyzerMode::Off);
    s.resolution = readChoice (tree, kResolution, s.resolution, AnalyzerResolution::Max);
    s.speed = readChoice (tree, kSpeed, s.speed, AnalyzerSpeed::VerySlow);
    s.tiltDbPerOctave = readListed (tree, kTilt, kAnalyzerTilts, s.tiltDbPerOctave);
    s.rangeDb = readListed (tree, kRange, kAnalyzerRanges, s.rangeDb);
    s.peakHold = tree.getProperty (kPeakHold, s.peakHold);
    s.freeze = tree.getProperty (kFreeze, s.freeze);
    s.glow = tree.getProperty (kGlow, s.glow);
    s.resonanceHints = tree.getProperty (kHints, s.resonanceHints);
    s.hintSensitivity = readChoice (tree, kHintSensitivity, s.hintSensitivity, HintSensitivity::High);
    s.thdWarningDb = readListed (tree, kThdWarning, kThdWarnings, s.thdWarningDb);
    s.aliasingWarningDb = readListed (tree, kAliasingWarning, kAliasingWarnings, s.aliasingWarningDb);
    s.phaseOverlay = readChoice (tree, kPhaseOverlay, s.phaseOverlay, PhaseOverlay::GroupDelay);
    return s;
}

void writeAnalyzerSettings (juce::ValueTree& state, const AnalyzerSettings& s)
{
    auto tree = state.getOrCreateChildWithName (kAnalyzerType, nullptr);
    tree.setProperty (kMode, static_cast<int> (s.mode), nullptr);
    tree.setProperty (kResolution, static_cast<int> (s.resolution), nullptr);
    tree.setProperty (kSpeed, static_cast<int> (s.speed), nullptr);
    tree.setProperty (kTilt, s.tiltDbPerOctave, nullptr);
    tree.setProperty (kRange, s.rangeDb, nullptr);
    tree.setProperty (kPeakHold, s.peakHold, nullptr);
    tree.setProperty (kFreeze, s.freeze, nullptr);
    tree.setProperty (kGlow, s.glow, nullptr);
    tree.setProperty (kHints, s.resonanceHints, nullptr);
    tree.setProperty (kHintSensitivity, static_cast<int> (s.hintSensitivity), nullptr);
    tree.setProperty (kThdWarning, s.thdWarningDb, nullptr);
    tree.setProperty (kAliasingWarning, s.aliasingWarningDb, nullptr);
    tree.setProperty (kPhaseOverlay, static_cast<int> (s.phaseOverlay), nullptr);
}
} // namespace ssc::plugin
