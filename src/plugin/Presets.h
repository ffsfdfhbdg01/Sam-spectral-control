#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace ssc::plugin
{
/**
    Presets and A/B (research §14c, GLB-3).

    A preset holds the sound: every parameter except Bypass and Delta (by its stable ID, in plain
    units) and the loudspeaker selections. Analyzer and window settings, the A/B slot, Bypass and
    Delta stay as they are. Presets carry the state version and go through the same migration as saved sessions.
    Parameters missing from a preset take their defaults; unknown IDs are ignored.
*/
inline const juce::Identifier kPresetType { "SamSpectralControlPreset" };
inline const juce::Identifier kPresetName { "presetName" }; ///< in the plugin state: the preset last loaded or saved
inline const juce::Identifier kCompareType { "Compare" };   ///< A/B: the slot shown and the other slot's preset

/** The current sound settings as a preset tree. */
juce::ValueTree capturePreset (juce::AudioProcessorValueTreeState& state, const juce::String& name = {}, const juce::String& category = {});

/** Sets every parameter (but Bypass and Delta) and the speakers from a preset, each change as a host gesture.
    Returns false (and changes nothing) for a tree that is not a preset or comes from a newer version. */
bool applyPreset (juce::AudioProcessorValueTreeState& state, const juce::ValueTree& preset);

/** Applies a preset as one undo step named `transactionName`; also records its name in the state. */
bool loadPreset (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager, const juce::ValueTree& preset,
                 const juce::String& transactionName = "Load Preset");

juce::String presetName (const juce::ValueTree& preset);
juce::String presetCategory (const juce::ValueTree& preset);

// Files -------------------------------------------------------------------------------------

inline constexpr const char* kPresetExtension = ".sscpreset";

bool writePresetFile (const juce::ValueTree& preset, const juce::File& file);
/** An invalid tree when the file is missing, not a preset or from a newer version. */
juce::ValueTree readPresetFile (const juce::File& file);

/** User presets: one XML file per preset in a folder (by default "Sam Spectral Control/Presets" in
    the user's documents). */
class PresetLibrary
{
public:
    PresetLibrary();
    explicit PresetLibrary (juce::File folder);

    const juce::File& getFolder() const noexcept { return folder; }

    /** Preset files, sorted by name. */
    juce::Array<juce::File> list() const;

    /** Saves under a file name made from `name` (replacing a preset of that name); returns the file. */
    juce::File save (const juce::ValueTree& preset, const juce::String& name) const;

    static juce::File defaultFolder();

private:
    juce::File folder;
};

// Factory presets ------------------------------------------------------------------------------

struct FactoryPreset
{
    juce::String name, category;
    juce::ValueTree tree;
};

const std::vector<FactoryPreset>& factoryPresets();

// A/B ---------------------------------------------------------------------------------------

/** The slot shown: 0 = A, 1 = B. */
int compareSlot (const juce::ValueTree& state);

/** Shows the other slot (one undo step); the first switch starts the other slot as a copy. */
void switchCompareSlot (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager);

/** Copies the slot shown to the other one (one undo step). */
void copyToOtherSlot (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager);
} // namespace ssc::plugin
