#include "ui/PresetBar.h"

#include <map>

#include "ui/Theme.h"

namespace ssc::ui
{
PresetBar::PresetBar (juce::AudioProcessorValueTreeState& s, juce::UndoManager& undo)
    : state (s),
      undoManager (undo)
{
    presetButton.setTooltip ("Presets: factory and user presets, Save As");
    presetButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (presetButton);

    for (auto* button : { &slotA, &slotB })
    {
        button->setClickingTogglesState (false);
        button->setTooltip ("A/B: compare two settings");
        // The slot shown in the accent colour.
        button->setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.85f));
        button->setColour (juce::TextButton::textColourOnId, colours::background);
        addAndMakeVisible (*button);
    }
    slotA.onClick = [this] {
        if (plugin::compareSlot (state.state) != 0)
            plugin::switchCompareSlot (state, undoManager);
        refresh();
    };
    slotB.onClick = [this] {
        if (plugin::compareSlot (state.state) != 1)
            plugin::switchCompareSlot (state, undoManager);
        refresh();
    };
    copy.onClick = [this] {
        plugin::copyToOtherSlot (state, undoManager);
        refresh();
    };
    addAndMakeVisible (copy);

    refresh();
    startTimerHz (4);
}

void PresetBar::refresh()
{
    const auto name = state.state[plugin::kPresetName].toString();
    presetButton.setButtonText (name.isEmpty() ? juce::String ("Default") : name);
    const int slot = plugin::compareSlot (state.state);
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    const auto arrow = juce::String::fromUTF8 ("\xe2\x86\x92");
    copy.setButtonText (slot == 0 ? "A" + arrow + "B" : "B" + arrow + "A");
    copy.setTooltip (slot == 0 ? "Copy A to B" : "Copy B to A");
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    copy.setBounds (area.removeFromRight (40));
    area.removeFromRight (4);
    slotB.setBounds (area.removeFromRight (26));
    area.removeFromRight (2);
    slotA.setBounds (area.removeFromRight (26));
    area.removeFromRight (6);
    presetButton.setBounds (area);
}

void PresetBar::load (const juce::ValueTree& preset)
{
    plugin::loadPreset (state, undoManager, preset, "Load Preset " + plugin::presetName (preset));
    refresh();
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu;
    const auto current = state.state[plugin::kPresetName].toString();

    // Factory presets by category, in the order of the list.
    std::vector<std::pair<juce::String, juce::PopupMenu>> categories;
    for (const auto& preset : plugin::factoryPresets())
    {
        auto it = std::find_if (categories.begin(), categories.end(), [&] (const auto& c) { return c.first == preset.category; });
        if (it == categories.end())
            it = categories.insert (categories.end(), { preset.category, juce::PopupMenu {} });
        it->second.addItem (preset.name, true, preset.name == current, [this, tree = preset.tree] { load (tree); });
    }
    menu.addSectionHeader ("Factory");
    for (auto& [category, sub] : categories)
        menu.addSubMenu (category, sub);

    menu.addSectionHeader ("User");
    const auto files = library.list();
    juce::File currentFile;
    for (const auto& file : files)
    {
        const auto name = file.getFileNameWithoutExtension();
        if (name == current)
            currentFile = file;
        menu.addItem (name, true, name == current, [this, file] {
            if (const auto preset = plugin::readPresetFile (file); preset.isValid())
                load (preset);
        });
    }
    if (files.isEmpty())
        menu.addItem ("(no user presets yet)", false, false, [] {});
    menu.addSeparator();
    menu.addItem ("Save As...", [this] { saveAs(); });
    menu.addItem ("Delete \"" + currentFile.getFileNameWithoutExtension() + "\"", currentFile.existsAsFile(), false, [this, currentFile] {
        currentFile.deleteFile();
        state.state.setProperty (plugin::kPresetName, juce::String(), nullptr);
        refresh();
    });
    menu.addItem ("Show Preset Folder", [this] {
        library.getFolder().createDirectory();
        library.getFolder().startAsProcess();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton));
}

void PresetBar::saveAs()
{
    auto* window = new juce::AlertWindow ("Save Preset", "Name of the user preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", state.state[plugin::kPresetName].toString());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
                                 if (safe == nullptr || result != 1)
                                     return;
                                 const auto name = window->getTextEditorContents ("name").trim();
                                 if (name.isEmpty())
                                     return;
                                 if (safe->library.save (plugin::capturePreset (safe->state, name), name).existsAsFile())
                                     safe->state.state.setProperty (plugin::kPresetName, name, nullptr);
                                 safe->refresh();
                             }),
                             true);
}
} // namespace ssc::ui
