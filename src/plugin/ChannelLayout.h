#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ssc/engine/ChannelTopology.h"

namespace ssc::plugin
{
/** Speaker role of a JUCE channel type (Unknown for discrete and ambisonic channels). */
engine::SpeakerRole speakerRoleFor (juce::AudioChannelSet::ChannelType type) noexcept;

/** Engine channel topology (roles, symmetric pairs, LFE) for a host bus layout (research §4). */
engine::ChannelTopology topologyFor (const juce::AudioChannelSet& layout);
} // namespace ssc::plugin
