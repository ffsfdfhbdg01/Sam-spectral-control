#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Presets.h"
#include "ui/Controls.h"

namespace ssc::ui
{
/** The preset name as a field: a click opens the preset menu; a dot follows the name while the
    settings differ from the preset (research §15 d3, F8). */
class PresetNameButton final : public juce::Button
{
public:
    PresetNameButton();

    void setPresetName (const juce::String& name);
    void setModified (bool modified);
    bool isModified() const noexcept { return modified; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String presetName;
    bool modified = false;
};

/**
    Presets, A/B and Undo/Redo in the header (research §14c, §15 d3, GLB-3, VIS-7). ‹ › step
    through the presets in menu order (factory, then user presets); the name opens the preset menu
    (factory presets by category, user presets, Save As, Delete, Show Folder) and gets a dot while
    the settings differ from the preset. A | B shows either of two settings; the copy button copies
    the one shown to the other and is disabled while both are equal. Loading, switching and copying
    are single undo steps; the arrows undo and redo them and every band edit.
*/
class PresetBar final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    PresetBar (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager);
    ~PresetBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Re-reads the preset name, the modified state and the A/B slots (the timer does this). */
    void refresh();

    bool isModified() const noexcept { return presetButton.isModified(); }
    bool canCopyToOtherSlot() const noexcept { return copy.isEnabled(); }

    /** Loads the previous (−1) or next (+1) preset in menu order. */
    void step (int direction);

private:
    struct Entry
    {
        juce::String name;
        juce::ValueTree factoryTree; ///< factory presets
        juce::File file;             ///< user presets
    };

    void timerCallback() override { refresh(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void showMenu();
    void saveAs();
    void load (const juce::ValueTree& preset);
    std::vector<Entry> entries() const;
    juce::ValueTree findPreset (const juce::String& name) const;

    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager& undoManager;
    plugin::PresetLibrary library;

    IconButton previous { Icon::Previous, "Previous preset" }, next { Icon::Next, "Next preset" };
    PresetNameButton presetButton;
    SegmentedControl slots { { "A", "B" } };
    IconButton copy { Icon::Copy, "Copy" };
    IconButton undoButton { Icon::Undo, "Undo" }, redoButton { Icon::Redo, "Redo" };

    juce::String referenceName;
    bool referenceResolved = false;
    plugin::PresetComparison reference; ///< the preset last loaded or saved
    juce::ValueTree otherSlotTree;      ///< the A/B slot not shown …
    plugin::PresetComparison otherSlot; ///< … and its values

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
} // namespace ssc::ui
