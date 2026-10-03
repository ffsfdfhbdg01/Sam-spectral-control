#include "plugin/StateMigration.h"

namespace ssc::plugin
{
bool migrateState (juce::ValueTree& state)
{
    if (! state.hasType (kStateType))
        return false;

    const int version = state.getProperty (kVersionProperty, 1);
    if (version > kStateVersion)
        return false;

    // Migration steps go here, e.g.:
    // if (version < 2) { ...rename or convert properties...; }

    state.setProperty (kVersionProperty, kStateVersion, nullptr);
    return true;
}
} // namespace ssc::plugin
