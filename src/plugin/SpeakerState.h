#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ssc/engine/ChannelTopology.h"

/**
    Per-band loudspeaker selection in the saved state (research §4c, CH-4). It is not a host
    parameter (nobody automates speakers, and a 70-bit mask does not fit one): it lives in a
    "Speakers" child of the state tree, one property per band ("b01" … "b24") holding the role
    mask and the discrete-channel mask in hex. A missing property is the default (all but LFE).
*/
namespace ssc::plugin
{
inline const juce::Identifier kSpeakersType { "Speakers" };

juce::Identifier speakerPropertyFor (int band);

juce::String speakerSelectionToString (const engine::SpeakerSelection& selection);

/** Inverse of speakerSelectionToString(); malformed text gives the default selection. */
engine::SpeakerSelection speakerSelectionFromString (const juce::String& text);

engine::SpeakerSelection readSpeakers (const juce::ValueTree& state, int band);

/** Stores a band's selection (the default removes the property). */
void writeSpeakers (juce::ValueTree& state, int band, const engine::SpeakerSelection& selection, juce::UndoManager* undoManager);
} // namespace ssc::plugin
