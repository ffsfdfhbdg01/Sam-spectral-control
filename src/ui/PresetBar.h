#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/Presets.h"

namespace ssc::ui
{
/** Undo or redo arrow (VIS-7), drawn as a path; dimmed while disabled. */
class UndoArrowButton final : public juce::Button
{
public:
    explicit UndoArrowButton (bool redo);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool redo;
};

/**
    Presets, A/B and Undo/Redo in the header (research §14c, GLB-3, VIS-7): the name of the preset
    last loaded or saved opens the preset menu (factory presets by category, user presets, Save
    As, Delete, Show Folder); A and B show either of two settings, the arrow copies the one shown
    to the other. Loading, switching and copying are single undo steps; the arrows undo and redo
    them and every band edit.
*/
class PresetBar final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    PresetBar (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undoManager);
    ~PresetBar() override;

    void resized() override;

    /** Re-reads the preset name and the A/B slot (the timer does this). */
    void refresh();

private:
    void timerCallback() override { refresh(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void showMenu();
    void saveAs();
    void load (const juce::ValueTree& preset);

    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager& undoManager;
    plugin::PresetLibrary library;

    juce::TextButton presetButton;
    juce::TextButton slotA { "A" }, slotB { "B" }, copy;
    UndoArrowButton undoButton { false }, redoButton { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};
} // namespace ssc::ui
