#include "plugin/StateMigration.h"

#include <algorithm>

#include "plugin/Parameters.h"

namespace ssc::plugin
{
namespace
{
    const juce::Identifier kParamType { "PARAM" }, kIdProperty { "id" }, kValueProperty { "value" };

    /** Version 1 → 2 for every band of a state or preset tree (PARAM children, plain values). */
    void migrateDirections (juce::ValueTree& state)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            auto param = [&state, b] (BandParam p) {
                auto child = state.getChildWithProperty (kIdProperty, bandParamId (b, p));
                if (! child.isValid())
                {
                    child = juce::ValueTree (kParamType);
                    child.setProperty (kIdProperty, bandParamId (b, p), nullptr);
                }
                return child;
            };
            auto direction = param (BandParam::Direction);
            auto ratio = param (BandParam::Ratio);
            auto attack = param (BandParam::Attack);
            auto release = param (BandParam::Release);
            BandDirection old;
            old.direction = direction.getProperty (kValueProperty, old.direction);
            old.ratio = ratio.getProperty (kValueProperty, old.ratio);
            old.attackMs = attack.getProperty (kValueProperty, old.attackMs);
            old.releaseMs = release.getProperty (kValueProperty, old.releaseMs);

            const auto now = migrateBandDirection (old);
            auto store = [&state] (juce::ValueTree& child, float before, float after) {
                if (juce::exactlyEqual (before, after))
                    return;
                child.setProperty (kValueProperty, after, nullptr);
                if (! child.getParent().isValid())
                    state.appendChild (child, nullptr);
            };
            store (direction, old.direction, now.direction);
            store (attack, old.attackMs, now.attackMs);
            store (release, old.releaseMs, now.releaseMs);
        }
    }
} // namespace

BandDirection migrateBandDirection (const BandDirection& version1) noexcept
{
    BandDirection now = version1;
    const bool below = version1.direction >= 0.5f;
    const bool compress = version1.ratio >= 1.0f;
    now.direction = below == compress ? 1.0f : 0.0f; // Up: compression below or expansion above
    if (below)
    {
        // The ranges of the Attack and Release parameters.
        now.attackMs = std::clamp (version1.releaseMs, 0.05f, 500.0f);
        now.releaseMs = std::clamp (version1.attackMs, 1.0f, 5000.0f);
    }
    return now;
}

bool migrateState (juce::ValueTree& state)
{
    if (! state.hasType (kStateType))
        return false;

    const int version = state.getProperty (kVersionProperty, 1);
    if (version > kStateVersion)
        return false;

    if (version < 2)
        migrateDirections (state);

    state.setProperty (kVersionProperty, kStateVersion, nullptr);
    return true;
}
} // namespace ssc::plugin
