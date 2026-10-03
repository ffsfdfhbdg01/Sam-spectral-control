#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ssc::plugin
{
/** Current version of the saved state. Bump when the layout changes and add a migration step. */
inline constexpr int kStateVersion = 1;

inline const juce::Identifier kStateType { "SamSpectralControl" };
inline const juce::Identifier kVersionProperty { "version" };
inline const juce::Identifier kUiWidth { "uiWidth" };
inline const juce::Identifier kUiHeight { "uiHeight" };

/**
    Upgrades a saved state tree to kStateVersion in place.
    Returns false if the tree is not a Sam Spectral Control state or comes from a newer version.
*/
bool migrateState (juce::ValueTree& state);
} // namespace ssc::plugin
