#include "plugin/Presets.h"

#include <cmath>

#include "plugin/Parameters.h"
#include "plugin/SpeakerState.h"
#include "plugin/StateMigration.h"

namespace ssc::plugin
{
namespace
{
    const juce::Identifier kParamType { "PARAM" };
    const juce::Identifier kIdProperty { "id" };
    const juce::Identifier kValueProperty { "value" };
    const juce::Identifier kNameProperty { "name" };
    const juce::Identifier kCategoryProperty { "category" };
    const juce::Identifier kSlotProperty { "slot" };

    /** Brings a preset to the current state version: its parameters and speakers have the layout of
        the plugin state, so the same migration steps apply. */
    bool migratePreset (juce::ValueTree& preset)
    {
        if (! preset.hasType (kPresetType))
            return false;
        juce::ValueTree asState (kStateType);
        asState.copyPropertiesFrom (preset, nullptr);
        for (const auto& child : preset)
            asState.appendChild (child.createCopy(), nullptr);
        if (! migrateState (asState))
            return false;
        preset.copyPropertiesFrom (asState, nullptr);
        preset.removeAllChildren (nullptr);
        for (const auto& child : asState)
            preset.appendChild (child.createCopy(), nullptr);
        return true;
    }

    template <typename Fn>
    void forEachSoundParameter (juce::AudioProcessorValueTreeState& state, Fn&& fn)
    {
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() != id::bypass)
                fn (*ranged);
    }

    /** Undo step of a preset load: applies `after`, undo applies `before`. */
    class PresetAction final : public juce::UndoableAction
    {
    public:
        PresetAction (juce::AudioProcessorValueTreeState& s, juce::ValueTree beforeTree, juce::ValueTree afterTree)
            : state (s),
              before (std::move (beforeTree)),
              after (std::move (afterTree))
        {
        }

        bool perform() override { return apply (after); }
        bool undo() override { return apply (before); }
        int getSizeInUnits() override { return 64; }

    private:
        bool apply (const juce::ValueTree& preset)
        {
            if (! applyPreset (state, preset))
                return false;
            state.state.setProperty (kPresetName, presetName (preset), nullptr);
            return true;
        }

        juce::AudioProcessorValueTreeState& state;
        juce::ValueTree before, after;
    };

    /** A/B: the slot shown and the stored one change places; its own inverse. */
    void swapCompareSlots (juce::AudioProcessorValueTreeState& state)
    {
        auto compare = state.state.getOrCreateChildWithName (kCompareType, nullptr);
        const auto stored = compare.getChildWithName (kPresetType).createCopy();
        const int slot = compare.getProperty (kSlotProperty, 0);
        compare.removeAllChildren (nullptr);
        compare.appendChild (capturePreset (state, state.state[kPresetName].toString()), nullptr);
        if (stored.isValid() && applyPreset (state, stored))
            state.state.setProperty (kPresetName, presetName (stored), nullptr);
        compare.setProperty (kSlotProperty, 1 - slot, nullptr);
    }

    class SwapAction final : public juce::UndoableAction
    {
    public:
        explicit SwapAction (juce::AudioProcessorValueTreeState& s)
            : state (s)
        {
        }
        bool perform() override
        {
            swapCompareSlots (state);
            return true;
        }
        bool undo() override { return perform(); }
        int getSizeInUnits() override { return 64; }

    private:
        juce::AudioProcessorValueTreeState& state;
    };

    class CopySlotAction final : public juce::UndoableAction
    {
    public:
        explicit CopySlotAction (juce::AudioProcessorValueTreeState& s)
            : state (s)
        {
        }
        bool perform() override
        {
            auto compare = state.state.getOrCreateChildWithName (kCompareType, nullptr);
            previous = compare.getChildWithName (kPresetType).createCopy();
            compare.removeAllChildren (nullptr);
            compare.appendChild (capturePreset (state, state.state[kPresetName].toString()), nullptr);
            return true;
        }
        bool undo() override
        {
            auto compare = state.state.getOrCreateChildWithName (kCompareType, nullptr);
            compare.removeAllChildren (nullptr);
            if (previous.isValid())
                compare.appendChild (previous.createCopy(), nullptr);
            return true;
        }
        int getSizeInUnits() override { return 64; }

    private:
        juce::AudioProcessorValueTreeState& state;
        juce::ValueTree previous;
    };
} // namespace

juce::ValueTree capturePreset (juce::AudioProcessorValueTreeState& state, const juce::String& name, const juce::String& category)
{
    juce::ValueTree preset (kPresetType);
    preset.setProperty (kVersionProperty, kStateVersion, nullptr);
    if (name.isNotEmpty())
        preset.setProperty (kNameProperty, name, nullptr);
    if (category.isNotEmpty())
        preset.setProperty (kCategoryProperty, category, nullptr);
    forEachSoundParameter (state, [&preset] (juce::RangedAudioParameter& p) {
        juce::ValueTree param (kParamType);
        param.setProperty (kIdProperty, p.getParameterID(), nullptr);
        param.setProperty (kValueProperty, p.convertFrom0to1 (p.getValue()), nullptr);
        preset.appendChild (param, nullptr);
    });
    if (const auto speakers = state.state.getChildWithName (kSpeakersType); speakers.isValid())
        preset.appendChild (speakers.createCopy(), nullptr);
    return preset;
}

bool applyPreset (juce::AudioProcessorValueTreeState& state, const juce::ValueTree& source)
{
    auto preset = source.createCopy();
    if (! migratePreset (preset))
        return false;

    forEachSoundParameter (state, [&preset] (juce::RangedAudioParameter& p) {
        const auto stored = preset.getChildWithProperty (kIdProperty, p.getParameterID());
        const float target =
            stored.isValid() ? p.convertTo0to1 (static_cast<float> (stored.getProperty (kValueProperty))) : p.getDefaultValue();
        if (std::abs (target - p.getValue()) < 1.0e-7f)
            return;
        p.beginChangeGesture();
        p.setValueNotifyingHost (target);
        p.endChangeGesture();
    });

    auto speakers = state.state.getOrCreateChildWithName (kSpeakersType, nullptr);
    if (const auto stored = preset.getChildWithName (kSpeakersType); stored.isValid())
        speakers.copyPropertiesFrom (stored, nullptr);
    else
        speakers.removeAllProperties (nullptr);
    return true;
}

bool loadPreset (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager, const juce::ValueTree& preset,
                 const juce::String& transactionName)
{
    auto checked = preset.createCopy();
    if (! migratePreset (checked))
        return false;
    auto before = capturePreset (state, state.state[kPresetName].toString());
    undoManager.beginNewTransaction (transactionName);
    return undoManager.perform (new PresetAction (state, std::move (before), std::move (checked)), transactionName);
}

juce::String presetName (const juce::ValueTree& preset)
{
    return preset[kNameProperty].toString();
}

juce::String presetCategory (const juce::ValueTree& preset)
{
    return preset[kCategoryProperty].toString();
}

bool writePresetFile (const juce::ValueTree& preset, const juce::File& file)
{
    const auto xml = preset.createXml();
    return xml != nullptr && file.getParentDirectory().createDirectory().wasOk() && xml->writeTo (file);
}

juce::ValueTree readPresetFile (const juce::File& file)
{
    const auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return {};
    auto preset = juce::ValueTree::fromXml (*xml);
    if (! migratePreset (preset))
        return {};
    if (presetName (preset).isEmpty())
        preset.setProperty (kNameProperty, file.getFileNameWithoutExtension(), nullptr);
    return preset;
}

PresetLibrary::PresetLibrary()
    : folder (defaultFolder())
{
}

PresetLibrary::PresetLibrary (juce::File f)
    : folder (std::move (f))
{
}

juce::File PresetLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Sam Spectral Control")
        .getChildFile ("Presets");
}

juce::Array<juce::File> PresetLibrary::list() const
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExtension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });
    return files;
}

juce::File PresetLibrary::save (const juce::ValueTree& preset, const juce::String& name) const
{
    auto named = preset.createCopy();
    named.setProperty (kNameProperty, name, nullptr);
    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + kPresetExtension);
    return writePresetFile (named, file) ? file : juce::File {};
}

int compareSlot (const juce::ValueTree& state)
{
    return juce::jlimit (0, 1, static_cast<int> (state.getChildWithName (kCompareType).getProperty (kSlotProperty, 0)));
}

void switchCompareSlot (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager)
{
    undoManager.beginNewTransaction (compareSlot (state.state) == 0 ? "Switch to B" : "Switch to A");
    undoManager.perform (new SwapAction (state));
}

void copyToOtherSlot (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager)
{
    undoManager.beginNewTransaction (compareSlot (state.state) == 0 ? "Copy A to B" : "Copy B to A");
    undoManager.perform (new CopySlotAction (state));
}
} // namespace ssc::plugin
