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
inline const juce::Identifier kUiEqRange { "uiEqRange" };
inline const juce::Identifier kUiMotion { "uiMotion" };                 ///< animations: 0 Full, 1 Reduced, 2 Off
inline const juce::Identifier kUiBandAdvanced { "uiBandAdvanced" };     ///< the band's Advanced flyout is open
inline const juce::Identifier kUiGlobalAdvanced { "uiGlobalAdvanced" }; ///< the global Advanced panel is open

/**
    Upgrades a saved state tree to kStateVersion in place.
    Returns false if the tree is not a Sam Spectral Control state or comes from a newer version.
*/
bool migrateState (juce::ValueTree& state);
} // namespace ssc::plugin
