#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ssc::plugin
{
/**
    Current version of the saved state. Bump when the layout changes and add a migration step.
    1: version 1.0. 2: the bands' Direction is Down / Up (it was the threshold side, Above / Below),
    and attack and release answer a rising and a falling level.
*/
inline constexpr int kStateVersion = 2;

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

/** The direction and time settings of one band, in plain units. */
struct BandDirection
{
    float direction = 0.0f; ///< the Direction choice index
    float ratio = 2.0f;
    float attackMs = 5.0f;
    float releaseMs = 100.0f;
};

/**
    A band of version 1 as version 2 reads it, with the same sound. Version 1 stored the threshold
    side (Above / Below) and smoothed with the attack while the gain change grew; version 2 stores
    Down / Up and uses the attack for a rising level. So the direction flips where the ratio expands
    (below 1:1), and attack and release change places where the band acted below the threshold
    (each limited to its range).
*/
BandDirection migrateBandDirection (const BandDirection& version1) noexcept;
} // namespace ssc::plugin
