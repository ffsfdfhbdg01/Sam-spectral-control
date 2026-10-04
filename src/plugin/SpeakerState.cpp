#include "plugin/SpeakerState.h"

namespace ssc::plugin
{
juce::Identifier speakerPropertyFor (int band)
{
    return juce::Identifier ("b" + juce::String (band + 1).paddedLeft ('0', 2));
}

juce::String speakerSelectionToString (const engine::SpeakerSelection& selection)
{
    return juce::String::toHexString (static_cast<juce::int64> (selection.roles)) + ":"
        + juce::String::toHexString (static_cast<juce::int64> (selection.discrete));
}

engine::SpeakerSelection speakerSelectionFromString (const juce::String& text)
{
    const auto roles = text.upToFirstOccurrenceOf (":", false, false).trim();
    const auto discrete = text.fromFirstOccurrenceOf (":", false, false).trim();
    if (roles.isEmpty() || discrete.isEmpty() || ! roles.containsOnly ("0123456789abcdefABCDEF")
        || ! discrete.containsOnly ("0123456789abcdefABCDEF"))
        return {};

    engine::SpeakerSelection selection;
    selection.roles = static_cast<std::uint64_t> (roles.getHexValue64());
    selection.discrete = static_cast<std::uint32_t> (discrete.getHexValue64());
    return selection;
}

engine::SpeakerSelection readSpeakers (const juce::ValueTree& state, int band)
{
    const auto speakers = state.getChildWithName (kSpeakersType);
    if (! speakers.isValid())
        return {};
    const auto* value = speakers.getPropertyPointer (speakerPropertyFor (band));
    return value != nullptr ? speakerSelectionFromString (value->toString()) : engine::SpeakerSelection {};
}

void writeSpeakers (juce::ValueTree& state, int band, const engine::SpeakerSelection& selection, juce::UndoManager* undoManager)
{
    auto speakers = state.getOrCreateChildWithName (kSpeakersType, undoManager);
    const auto property = speakerPropertyFor (band);
    if (selection.isDefault())
        speakers.removeProperty (property, undoManager);
    else
        speakers.setProperty (property, speakerSelectionToString (selection), undoManager);
}
} // namespace ssc::plugin
