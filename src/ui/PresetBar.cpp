#include "ui/PresetBar.h"

#include "plugin/StateMigration.h"
#include "ui/Theme.h"

namespace ssc::ui
{
PresetNameButton::PresetNameButton()
    : juce::Button ("Preset")
{
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PresetNameButton::setPresetName (const juce::String& name)
{
    if (presetName != name)
    {
        presetName = name;
        repaint();
    }
}

void PresetNameButton::setModified (bool isModified)
{
    if (modified != isModified)
    {
        modified = isModified;
        repaint();
    }
}

void PresetNameButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel.brighter (down ? 0.08f : (highlighted ? 0.04f : 0.0f)));
    g.fillRoundedRectangle (bounds, metrics::controlRadius);
    g.setColour (highlighted ? colours::outline : colours::panelEdge);
    g.drawRoundedRectangle (bounds, metrics::controlRadius, 1.0f);

    auto area = getLocalBounds().reduced (10, 0);
    drawIcon (g, Icon::ChevronDown, area.removeFromRight (12).toFloat().withSizeKeepingCentre (11.0f, 11.0f),
              highlighted ? colours::text : colours::textDim);
    area.removeFromRight (6);

    // A modified preset: the name steps back and a dot follows it.
    const auto font = uiFont (13.0f);
    const int width = juce::jmin (area.getWidth() - (modified ? 14 : 0), juce::GlyphArrangement::getStringWidthInt (font, presetName) + 2);
    g.setFont (font);
    g.setColour (modified ? colours::textSecondary : colours::text);
    g.drawText (presetName, area.withWidth (width), juce::Justification::centredLeft, true);
    if (modified)
    {
        g.setColour (colours::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f)
                           .withCentre ({ static_cast<float> (area.getX() + width + 7), static_cast<float> (area.getCentreY()) }));
    }
    if (hasKeyboardFocus (false))
        drawFocusRing (g, bounds, metrics::controlRadius);
}

PresetBar::PresetBar (juce::AudioProcessorValueTreeState& s, juce::UndoManager& undo)
    : state (s),
      undoManager (undo)
{
    for (auto* c : std::initializer_list<juce::Component*> { &previous, &presetButton, &next, &slots, &copy, &undoButton, &redoButton })
        addAndMakeVisible (c);

    previous.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    presetButton.onClick = [this] { showMenu(); };

    slots.setTooltip ("A/B: compare two settings");
    slots.setItemTooltip (0, "Show setting A");
    slots.setItemTooltip (1, "Show setting B");
    slots.onChange = [this] (int index) {
        if (plugin::compareSlot (state.state) != index)
            plugin::switchCompareSlot (state, undoManager);
        refresh();
    };
    copy.onClick = [this] {
        plugin::copyToOtherSlot (state, undoManager);
        refresh();
    };

    undoButton.onClick = [this] {
        undoManager.undo();
        refresh();
    };
    redoButton.onClick = [this] {
        undoManager.redo();
        refresh();
    };
    undoManager.addChangeListener (this);

    refresh();
    startTimerHz (4);
}

PresetBar::~PresetBar()
{
    undoManager.removeChangeListener (this);
}

std::vector<PresetBar::Entry> PresetBar::entries() const
{
    std::vector<Entry> list;
    for (const auto& preset : plugin::factoryPresets())
        list.push_back ({ preset.name, preset.tree, {} });
    for (const auto& file : library.list())
        list.push_back ({ file.getFileNameWithoutExtension(), {}, file });
    return list;
}

juce::ValueTree PresetBar::findPreset (const juce::String& name) const
{
    if (name.isEmpty())
    {
        // Nothing loaded yet: the defaults.
        juce::ValueTree defaults (plugin::kPresetType);
        defaults.setProperty (plugin::kVersionProperty, plugin::kStateVersion, nullptr);
        return defaults;
    }
    for (const auto& preset : plugin::factoryPresets())
        if (preset.name == name)
            return preset.tree;
    for (const auto& file : library.list())
        if (file.getFileNameWithoutExtension() == name)
            return plugin::readPresetFile (file);
    return {};
}

void PresetBar::refresh()
{
    const auto name = state.state[plugin::kPresetName].toString();
    if (! referenceResolved || name != referenceName)
    {
        referenceName = name;
        reference = plugin::PresetComparison (state, findPreset (name));
        referenceResolved = true;
    }
    const bool modified = reference.isValid() && ! reference.matches (state);
    presetButton.setPresetName (name.isEmpty() ? juce::String ("Default") : name);
    presetButton.setModified (modified);
    presetButton.setTooltip (modified ? "Presets: factory and user presets, Save As.\nThe dot: the settings differ from the preset."
                                      : "Presets: factory and user presets, Save As");

    const int slot = plugin::compareSlot (state.state);
    slots.setSelectedIndex (slot, juce::dontSendNotification);
    if (const auto other = plugin::otherComparePreset (state.state); other != otherSlotTree)
    {
        otherSlotTree = other;
        otherSlot = other.isValid() ? plugin::PresetComparison (state, other) : plugin::PresetComparison();
    }
    const bool equal = ! otherSlot.isValid() || otherSlot.matches (state);
    copy.setEnabled (! equal);
    copy.setTooltip (equal ? "A and B are the same" : (slot == 0 ? "Copy A to B" : "Copy B to A"));

#if JUCE_MAC
    const juce::String shortcut ("Cmd");
#else
    const juce::String shortcut ("Ctrl");
#endif
    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());
    undoButton.setTooltip (undoManager.canUndo() ? "Undo " + undoManager.getUndoDescription() + " (" + shortcut + "+Z)"
                                                 : "Nothing to undo");
    redoButton.setTooltip (undoManager.canRedo() ? "Redo " + undoManager.getRedoDescription() + " (" + shortcut + "+Shift+Z)"
                                                 : "Nothing to redo");
}

void PresetBar::step (int direction)
{
    const auto list = entries();
    if (list.empty())
        return;
    auto current = state.state[plugin::kPresetName].toString();
    if (current.isEmpty())
        current = "Default";
    const int n = static_cast<int> (list.size());
    int index = -1;
    for (int i = 0; i < n; ++i)
        if (list[static_cast<std::size_t> (i)].name == current)
        {
            index = i;
            break;
        }
    const int target = index < 0 ? (direction > 0 ? 0 : n - 1) : (index + direction + n) % n;
    const auto& entry = list[static_cast<std::size_t> (target)];
    if (entry.factoryTree.isValid())
        load (entry.factoryTree);
    else if (const auto preset = plugin::readPresetFile (entry.file); preset.isValid())
        load (preset);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    redoButton.setBounds (area.removeFromRight (28));
    area.removeFromRight (2);
    undoButton.setBounds (area.removeFromRight (28));
    area.removeFromRight (17);
    copy.setBounds (area.removeFromRight (28));
    area.removeFromRight (4);
    slots.setBounds (area.removeFromRight (64).reduced (0, 1));
    area.removeFromRight (17);
    next.setBounds (area.removeFromRight (26));
    area.removeFromRight (2);
    previous.setBounds (area.removeFromLeft (26));
    area.removeFromLeft (2);
    presetButton.setBounds (area);
}

void PresetBar::paint (juce::Graphics& g)
{
    // Dividers between presets | comparison | history.
    g.setColour (colours::panelEdge);
    const int y = getHeight() / 2 - 8;
    g.fillRect ((next.getRight() + slots.getX()) / 2, y, 1, 16);
    g.fillRect ((copy.getRight() + undoButton.getX()) / 2, y, 1, 16);
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
                                 safe->referenceResolved = false; // the file may have been replaced
                                 safe->refresh();
                             }),
                             true);
}
} // namespace ssc::ui
