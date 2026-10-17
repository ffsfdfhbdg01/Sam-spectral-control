#include "ui/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/StateMigration.h"
#include "ui/PanelLayout.h"

namespace ssc::ui
{
namespace
{
    constexpr float kEqRanges[] = { 3.0f, 6.0f, 12.0f, 18.0f, 30.0f };
    constexpr float kDefaultEqRange = 12.0f;
    constexpr int kScaleInset = 22; ///< the frequency scale at the bottom of the display

    struct WindowSize
    {
        const char* name;
        int width, height;
    };
    constexpr WindowSize kWindowSizes[] = {
        { "Small", 860, 480 }, { "Medium", 1100, 660 }, { "Large", 1400, 840 }, { "Extra Large", 1800, 1080 }
    };

    const juce::String middleDot = juce::String::fromUTF8 ("\xc2\xb7");

    /** Where the installers put the user manual (packaging/). */
    juce::File installedManual()
    {
#if JUCE_MAC
        return juce::File ("/Library/Application Support/Sam Spectral Control/Manual.md");
#elif JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
            .getChildFile ("Sam Audio")
            .getChildFile ("Sam Spectral Control")
            .getChildFile ("Manual.md");
#else
        return {};
#endif
    }
} // namespace

// StatusChip ----------------------------------------------------------------------------------

void StatusChip::setText (const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void StatusChip::setHighlighted (bool shouldHighlight)
{
    if (highlighted != shouldHighlight)
    {
        highlighted = shouldHighlight;
        repaint();
    }
}

void StatusChip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (highlighted ? colours::accent : colours::panelEdge);
    g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);
    g.setColour (highlighted ? colours::accent : colours::textSecondary);
    g.setFont (uiFont (12.0f));
    g.drawFittedText (text, getLocalBounds().reduced (10, 0), juce::Justification::centred, 1, 0.85f);
}

// SscEditor -----------------------------------------------------------------------------------

SscEditor::SscEditor (plugin::SscAudioProcessor& p)
    : AudioProcessorEditor (p),
      sscProcessor (p),
      bandModel (p.getValueTreeState(), p.getUndoManager()),
      presetBar (p.getValueTreeState(), p.getUndoManager()),
      display (p.getAnalyzer(), bandModel),
      bandAdvanced (bandModel),
      bandPanel (bandModel),
      globalAdvanced (p.getValueTreeState()),
      controlBar (p.getValueTreeState())
{
    auto& tree = sscProcessor.getValueTreeState().state;
    motion::setLevel (*this, static_cast<motion::Level> (juce::jlimit (0, 2, static_cast<int> (tree.getProperty (plugin::kUiMotion, 0)))));

    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &status, &analyzerButton, &analyzerMenuButton, &settingsButton,
                                                             &helpButton, &display, &eqRangeButton })
        addAndMakeVisible (c);
    addChildComponent (bandAdvanced); // under the band panel: it slides out from beneath it
    addChildComponent (bandPanel);
    addChildComponent (globalAdvanced); // under the bottom bar: it slides up from behind it
    addAndMakeVisible (controlBar);

    bandModel.onSoloChanged = [this] (int band) {
        sscProcessor.setSoloBand (band);
        display.repaint(); // the audible range of the soloed band
    };
    display.dynamicGainProvider = [this] (int band) { return sscProcessor.getBandDynamicGainDb (band); };
    display.spectralViewProvider = [this] { return sscProcessor.pollSpectralView(); };
    display.chainProvider = [this]() -> const analysis::ChainResponse& {
        auto& readouts = sscProcessor.getReadouts().readouts();
        readouts.update();
        return readouts.read().chain;
    };
    auto readouts = [this] (int band) {
        auto& service = sscProcessor.getReadouts().readouts();
        service.update();
        return service.read().bands[static_cast<std::size_t> (juce::jlimit (0, kNumBands - 1, band))];
    };
    bandPanel.readoutProvider = readouts;
    bandAdvanced.readoutProvider = readouts;
    bandPanel.onLayoutChanged = [this] {
        sscProcessor.getValueTreeState().state.setProperty (plugin::kUiBandAdvanced, bandPanel.isAdvancedOpen(), nullptr);
        layoutPanels (true);
    };
    controlBar.onAdvancedToggled = [this] (bool open) {
        sscProcessor.getValueTreeState().state.setProperty (plugin::kUiGlobalAdvanced, open, nullptr);
        layoutPanels (true);
    };

    auto& meterView = globalAdvanced.getMeterView();
    meterView.readingsProvider = [this] {
        auto& readings = sscProcessor.getMeters().readings();
        readings.update();
        return readings.read();
    };
    meterView.bandDescription = [this] (int band) {
        const auto& v = bandModel.band (band);
        auto text = plugin::labels::shapes()[static_cast<int> (v.shape)] + " " + plugin::format::frequency (v.frequency);
        if (v.character != Character::Clear)
            text << ", " << plugin::labels::characters()[static_cast<int> (v.character)];
        if (v.mode != BandMode::Static)
            text << ", " << plugin::labels::bandModes()[static_cast<int> (v.mode)];
        return text;
    };
    meterView.onOversamplingHint = [this] (int factor, float aliasingDb) {
        globalAdvanced.setOversamplingHint (factor, aliasingDb);
        updateAdvancedIndicator();
    };
    meterView.onClick = [this] { showMeterMenu(); };

    // The EQ range belongs to the display: top right, next to its dB scale (research §15 d3, F10).
    eqRangeButton.setTooltip ("Visible EQ range (the view only, not the sound)");
    eqRangeButton.buildMenu = [this] (juce::PopupMenu& menu) {
        juce::Component::SafePointer<SscEditor> safe (this);
        for (const float range : kEqRanges)
            menu.addItem (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (range)) + " dB", true,
                          juce::approximatelyEqual (range, eqRangeDb), [safe, range] {
                              if (safe != nullptr)
                                  safe->setEqRange (range);
                          });
    };
    setEqRange (static_cast<float> (tree.getProperty (plugin::kUiEqRange, kDefaultEqRange)));

    // Analyzer as a split button: the left part switches the spectrum, the right part opens its settings.
    analyzerButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    analyzerMenuButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    analyzerButton.setOnColour (colours::textSecondary);
    analyzerButton.onClick = [this] { toggleAnalyzer(); };
    analyzerMenuButton.setTooltip ("Analyzer settings: spectra, resolution, speed, tilt, range, freeze, peak hold, glow, "
                                   "resonance hints, EQ phase overlay");
    analyzerMenuButton.onClick = [this] { showAnalyzerMenu(); };
    applyAnalyzerSettings (plugin::readAnalyzerSettings (tree), false);

    settingsButton.setTooltip ("Settings: animations and window size");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    helpButton.setTooltip ("Help: user manual, mouse and keyboard, about");
    helpButton.onClick = [this] { showHelpMenu(); };

    // The Advanced panels as they were left.
    bandPanel.setAdvancedOpen (static_cast<bool> (tree.getProperty (plugin::kUiBandAdvanced, false)));
    controlBar.setAdvancedOpen (static_cast<bool> (tree.getProperty (plugin::kUiGlobalAdvanced, false)));

    const int width = tree.getProperty (plugin::kUiWidth, kDefaultWidth);
    const int height = tree.getProperty (plugin::kUiHeight, kDefaultHeight);

    // After the children are added, so that every control (e.g. slider text boxes) picks it up.
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (860, 480, 2600, 1600);
    setSize (width, height);
    layoutPanels (false);

    sscProcessor.getAnalyzer().addClient();
    sscProcessor.getReadouts().addClient();
    sscProcessor.getMeters().addClient();
    startTimerHz (4);
}

SscEditor::~SscEditor()
{
    // Solo is a monitoring aid tied to the open editor.
    sscProcessor.setSoloBand (-1);
    sscProcessor.getAnalyzer().removeClient();
    sscProcessor.getReadouts().setChainWanted (false);
    sscProcessor.getReadouts().removeClient();
    sscProcessor.getMeters().removeClient();
    setLookAndFeel (nullptr);
}

void SscEditor::setEqRange (float rangeDb)
{
    bool known = false;
    for (const float range : kEqRanges)
        known = known || juce::approximatelyEqual (range, rangeDb);
    eqRangeDb = known ? rangeDb : kDefaultEqRange;
    eqRangeButton.setValueText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (eqRangeDb)) + " dB");
    display.setEqRange (eqRangeDb);
    sscProcessor.getValueTreeState().state.setProperty (plugin::kUiEqRange, eqRangeDb, nullptr);
    layoutPanels (true); // the nodes moved
}

void SscEditor::setBandAdvancedOpen (bool open)
{
    bandPanel.setAdvancedOpen (open);
    sscProcessor.getValueTreeState().state.setProperty (plugin::kUiBandAdvanced, open, nullptr);
    layoutPanels (true);
}

void SscEditor::setGlobalAdvancedOpen (bool open)
{
    controlBar.setAdvancedOpen (open);
    sscProcessor.getValueTreeState().state.setProperty (plugin::kUiGlobalAdvanced, open, nullptr);
    layoutPanels (true);
}

void SscEditor::setMotionLevel (motion::Level level)
{
    motion::setLevel (*this, level);
    sscProcessor.getValueTreeState().state.setProperty (plugin::kUiMotion, static_cast<int> (level), nullptr);
}

void SscEditor::applyAnalyzerSettings (const plugin::AnalyzerSettings& settings, bool store)
{
    analyzerSettings = settings;
    if (settings.mode != plugin::AnalyzerMode::Off)
        lastAnalyzerMode = settings.mode;
    sscProcessor.getAnalyzer().setSettings (settings);
    display.setAnalyzerSettings (settings);
    sscProcessor.getReadouts().setChainWanted (settings.phaseOverlay != plugin::PhaseOverlay::Off);
    globalAdvanced.getMeterView().setThresholds (settings.thdWarningDb, settings.aliasingWarningDb);
    analyzerButton.setToggleState (settings.mode != plugin::AnalyzerMode::Off, juce::dontSendNotification);
    analyzerButton.setButtonText (settings.freeze ? "Frozen" : "Analyzer");
    analyzerButton.setTooltip (settings.mode == plugin::AnalyzerMode::Off ? "Analyzer off: click to show the spectrum"
                                                                          : "Analyzer on: click to hide the spectrum");
    if (store)
        plugin::writeAnalyzerSettings (sscProcessor.getValueTreeState().state, settings);
}

void SscEditor::toggleAnalyzer()
{
    auto next = analyzerSettings;
    next.mode = next.mode == plugin::AnalyzerMode::Off ? lastAnalyzerMode : plugin::AnalyzerMode::Off;
    applyAnalyzerSettings (next, true);
}

void SscEditor::showAnalyzerMenu()
{
    using plugin::AnalyzerMode;
    using plugin::AnalyzerResolution;
    using plugin::AnalyzerSpeed;
    const auto s = analyzerSettings;
    juce::Component::SafePointer<SscEditor> safe (this);
    auto change = [safe] (auto&& edit) {
        return [safe, edit] {
            if (safe == nullptr)
                return;
            auto next = safe->analyzerSettings;
            edit (next);
            safe->applyAnalyzerSettings (next, true);
        };
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Spectrum");
    const std::pair<AnalyzerMode, const char*> modes[] = {
        { AnalyzerMode::PrePost, "Pre + Post" }, { AnalyzerMode::Post, "Post" }, { AnalyzerMode::Pre, "Pre" }, { AnalyzerMode::Off, "Off" }
    };
    for (const auto& [mode, name] : modes)
        menu.addItem (name, true, s.mode == mode, change ([m = mode] (plugin::AnalyzerSettings& n) { n.mode = m; }));

    juce::PopupMenu resolution, speed, tilt, range;
    const std::pair<AnalyzerResolution, const char*> resolutions[] = { { AnalyzerResolution::Low, "Low" },
                                                                       { AnalyzerResolution::Medium, "Medium" },
                                                                       { AnalyzerResolution::High, "High" },
                                                                       { AnalyzerResolution::Max, "Max" } };
    for (const auto& [value, name] : resolutions)
        resolution.addItem (name, true, s.resolution == value, change ([v = value] (plugin::AnalyzerSettings& n) { n.resolution = v; }));
    const std::pair<AnalyzerSpeed, const char*> speeds[] = { { AnalyzerSpeed::VeryFast, "Very Fast" },
                                                             { AnalyzerSpeed::Fast, "Fast" },
                                                             { AnalyzerSpeed::Medium, "Medium" },
                                                             { AnalyzerSpeed::Slow, "Slow" },
                                                             { AnalyzerSpeed::VerySlow, "Very Slow" } };
    for (const auto& [value, name] : speeds)
        speed.addItem (name, true, s.speed == value, change ([v = value] (plugin::AnalyzerSettings& n) { n.speed = v; }));
    for (const float value : plugin::kAnalyzerTilts)
        tilt.addItem (juce::String (value, 1) + " dB/oct", true, juce::approximatelyEqual (s.tiltDbPerOctave, value),
                      change ([value] (plugin::AnalyzerSettings& n) { n.tiltDbPerOctave = value; }));
    for (const float value : plugin::kAnalyzerRanges)
        range.addItem (juce::String (juce::roundToInt (value)) + " dB", true, juce::approximatelyEqual (s.rangeDb, value),
                       change ([value] (plugin::AnalyzerSettings& n) { n.rangeDb = value; }));
    menu.addSubMenu ("Resolution", resolution);
    menu.addSubMenu ("Speed", speed);
    menu.addSubMenu ("Tilt", tilt);
    menu.addSubMenu ("Range", range);

    menu.addSectionHeader ("Display");
    menu.addItem ("Freeze", true, s.freeze, change ([] (plugin::AnalyzerSettings& n) { n.freeze = ! n.freeze; }));
    menu.addItem ("Peak hold", true, s.peakHold, change ([] (plugin::AnalyzerSettings& n) { n.peakHold = ! n.peakHold; }));
    menu.addItem ("Activity glow", true, s.glow, change ([] (plugin::AnalyzerSettings& n) { n.glow = ! n.glow; }));
    menu.addItem ("Resonance hints", true, s.resonanceHints,
                  change ([] (plugin::AnalyzerSettings& n) { n.resonanceHints = ! n.resonanceHints; }));
    juce::PopupMenu sensitivity;
    const std::pair<plugin::HintSensitivity, const char*> sensitivities[] = { { plugin::HintSensitivity::Low, "Low (12 dB)" },
                                                                              { plugin::HintSensitivity::Medium, "Medium (9 dB)" },
                                                                              { plugin::HintSensitivity::High, "High (6 dB)" } };
    for (const auto& [value, name] : sensitivities)
        sensitivity.addItem (name, s.resonanceHints, s.hintSensitivity == value,
                             change ([v = value] (plugin::AnalyzerSettings& n) { n.hintSensitivity = v; }));
    menu.addSubMenu ("Hint sensitivity", sensitivity, s.resonanceHints);
    juce::PopupMenu overlay;
    const std::pair<plugin::PhaseOverlay, const char*> overlays[] = { { plugin::PhaseOverlay::Off, "Off" },
                                                                      { plugin::PhaseOverlay::Phase, "Phase" },
                                                                      { plugin::PhaseOverlay::GroupDelay, "Group delay" } };
    for (const auto& [value, name] : overlays)
        overlay.addItem (name, true, s.phaseOverlay == value, change ([v = value] (plugin::AnalyzerSettings& n) { n.phaseOverlay = v; }));
    menu.addSubMenu ("EQ phase overlay", overlay);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (analyzerMenuButton));
}

void SscEditor::showMeterMenu()
{
    // Meter warning thresholds (MTR-4), stored with the analyzer settings.
    const auto s = analyzerSettings;
    juce::Component::SafePointer<SscEditor> safe (this);
    auto change = [safe] (auto&& edit) {
        return [safe, edit] {
            if (safe == nullptr)
                return;
            auto next = safe->analyzerSettings;
            edit (next);
            safe->applyAnalyzerSettings (next, true);
        };
    };
    juce::PopupMenu menu;
    menu.addSectionHeader ("THD+N warning above");
    for (const float value : plugin::kThdWarnings)
        menu.addItem (MeterView::formatDb (value), true, juce::approximatelyEqual (s.thdWarningDb, value),
                      change ([value] (plugin::AnalyzerSettings& n) { n.thdWarningDb = value; }));
    menu.addSectionHeader ("Aliasing warning above");
    for (const float value : plugin::kAliasingWarnings)
        menu.addItem (MeterView::formatDb (value), true, juce::approximatelyEqual (s.aliasingWarningDb, value),
                      change ([value] (plugin::AnalyzerSettings& n) { n.aliasingWarningDb = value; }));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (globalAdvanced.getMeterView()));
}

void SscEditor::showSettingsMenu()
{
    juce::Component::SafePointer<SscEditor> safe (this);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Animations");
    const auto current = motion::levelFor (*this);
    static constexpr const char* levels[] = { "Full", "Reduced (fades only)", "Off" };
    for (int i = 0; i < 3; ++i)
        menu.addItem (levels[i], true, static_cast<int> (current) == i, [safe, i] {
            if (safe != nullptr)
                safe->setMotionLevel (static_cast<motion::Level> (i));
        });
    menu.addSectionHeader ("Window Size");
    for (const auto& size : kWindowSizes)
        menu.addItem (juce::String (size.name) + " (" + juce::String (size.width) + juce::String::fromUTF8 (" \xc3\x97 ")
                          + juce::String (size.height) + ")",
                      true, getWidth() == size.width && getHeight() == size.height, [safe, size] {
                          if (safe != nullptr)
                              safe->setSize (size.width, size.height);
                      });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton));
}

void SscEditor::showHelpMenu()
{
    juce::Component::SafePointer<SscEditor> safe (this);
    juce::PopupMenu menu;
    menu.addItem ("User Manual", [safe] {
        const auto manual = installedManual();
        if (manual.existsAsFile())
        {
            manual.startAsProcess();
            return;
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "User Manual",
                                                manual == juce::File()
                                                    ? juce::String ("The user manual (docs/manual.md) comes with the source code.")
                                                    : "The user manual is installed with the plug-in:\n" + manual.getFullPathName()
                                                        + "\nIt was not found on this computer.",
                                                {}, safe.getComponent());
    });
#if JUCE_MAC
    const juce::String cmd ("Cmd");
#else
    const juce::String cmd ("Ctrl");
#endif
    menu.addItem ("Mouse and Keyboard", [safe, cmd] {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Mouse and Keyboard",
                                                "Display\n"
                                                "  Double-click: new band (the shape follows the position)\n"
                                                "  Drag the curve: new bell or shelf\n"
                                                "  Drag a node: frequency and gain (Shift: fine); wheel: Q\n"
                                                "  Double-click a node: band on/off; Alt/Option + hold: solo\n"
                                                "  " + cmd
                                                    + " + click: add to the selection; right-click: band menu\n\n"
                                                    + "Keys (display focused)\n" + "  Delete: delete bands; " + cmd + "+Z: undo; " + cmd
                                                    + "+Shift+Z: redo; " + cmd + "+A: select all; Esc: deselect\n\n" + "Knobs\n"
                                                    + "  Drag (Shift: fine); double-click: default; click the value to type one",
                                                {}, safe.getComponent());
    });
    menu.addSeparator();
    menu.addItem ("About Sam Spectral Control", [safe] {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, plugin::kPluginName,
                                                "Version " SSC_VERSION_STRING "\nDynamic and spectral equaliser.\n\n"
                                                "Copyright (C) 2026 Sam Audio. This program comes with ABSOLUTELY NO "
                                                "WARRANTY. It is free software: you may redistribute and modify it under "
                                                "the GNU Affero General Public License, version 3 (LICENSE.txt).\n"
                                                "Source code: https://github.com/ffsfdfhbdg01/Sam-spectral-control\n\n"
                                                "Third-party components: THIRD-PARTY-NOTICES.txt (installed next to the "
                                                "user manual).",
                                                {}, safe.getComponent());
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (helpButton));
}

void SscEditor::updateAdvancedIndicator()
{
    const auto& meters = globalAdvanced.getMeterView();
    const auto changed = globalAdvanced.nonDefaultSettings();
    const int hint = globalAdvanced.getOversamplingHint();
    juce::StringArray details;
    if (! changed.isEmpty())
        details.add ("Changed: " + changed.joinIntoString (", "));
    if (hint > 0)
        details.add ("Aliasing: Oversampling " + juce::String (hint) + "x would reduce it");
    if (meters.hasWarning())
        details.add ("A meter is above its warning threshold");
    const auto colour = meters.hasWarning() ? colours::warning : (details.isEmpty() ? juce::Colours::transparentBlack : colours::accent);
    controlBar.setAdvancedIndicator (colour, details.joinIntoString ("\n"));
}

void SscEditor::layoutPanels (bool animate)
{
    if (display.getWidth() <= 0)
        return;
    const auto level = animate ? motion::levelFor (*this) : motion::Level::Off;
    const auto bar = controlBar.getBounds();
    const auto area = display.getBounds();

    // Global Advanced: slides up from behind the bottom bar; the frequency scale moves above it.
    const bool globalOpen = controlBar.isAdvancedOpen();
    display.setScaleInset (globalOpen ? GlobalAdvancedPanel::kHeight : 0);
    const juce::Rectangle<int> globalShown (0, bar.getY() - GlobalAdvancedPanel::kHeight, getWidth(), GlobalAdvancedPanel::kHeight);
    const auto globalHidden = globalShown.withY (bar.getY());
    if (globalOpen)
    {
        const double move = motion::moveDuration (level, 200.0);
        if (! globalAdvanced.isVisible())
            globalAdvancedMotion.snapTo (move > 0.0 ? globalHidden : globalShown, 0.0f);
        globalAdvancedMotion.moveTo (globalShown, move, motion::decelerate);
        globalAdvancedMotion.fadeTo (1.0f, motion::fadeDuration (level, 120.0), motion::decelerate);
    }
    else if (globalAdvanced.isVisible() && globalAdvancedMotion.getTargetAlpha() > 0.0f)
    {
        globalAdvancedMotion.moveTo (globalHidden, motion::moveDuration (level, 150.0), motion::accelerate);
        globalAdvancedMotion.fadeTo (0.0f, motion::fadeDuration (level, 150.0), motion::accelerate);
    }
    else if (! globalAdvanced.isVisible())
    {
        globalAdvancedMotion.snapTo (globalHidden, 0.0f);
    }

    // Band panel under the node, its Advanced flyout to the right; above the global panel when open.
    const int band = bandPanel.getBand();
    if (band < 0)
    {
        for (auto* m : { &bandPanelMotion, &bandAdvancedMotion })
            if (m->getTargetAlpha() > 0.0f)
                m->fadeTo (0.0f, motion::fadeDuration (level, 110.0), motion::accelerate);
        return;
    }

    const auto preferred = bandPanel.getPreferredSize();
    const bool flyOpen = bandPanel.isAdvancedOpen();
    BandPanelDockRequest request;
    request.area = area;
    request.panelSize = { juce::jmin (preferred.x, area.getWidth() - 16), preferred.y };
    request.flyoutSize = flyOpen ? juce::Point<int> (BandAdvancedPanel::kWidth, juce::jmax (preferred.y, bandAdvanced.getPreferredHeight()))
                                 : juce::Point<int>();
    request.node = display.nodePosition (band);
    request.bottomInset = globalOpen ? area.getBottom() - globalShown.getY() + 8 : kScaleInset;
    const auto dock = dockBandPanel (request);

    const bool appearing = ! bandPanel.isVisible() || bandPanelMotion.getTargetAlpha() < 1.0f;
    if (appearing)
    {
        if (! bandPanel.isVisible())
        {
            // It appears from the side of its node, 8 px closer to it.
            const int offset = motion::moveDuration (level, 160.0) > 0.0 ? (dock.atTop ? 8 : -8) : 0;
            bandPanelMotion.snapTo (dock.panel.translated (0, offset), 0.0f);
        }
        bandPanelMotion.moveTo (dock.panel, motion::moveDuration (level, 160.0), motion::decelerate);
        bandPanelMotion.fadeTo (1.0f, motion::fadeDuration (level, 160.0), motion::decelerate);
    }
    else
    {
        bandPanelMotion.moveTo (dock.panel, motion::moveDuration (level, 180.0), motion::standard);
    }

    if (flyOpen)
    {
        // Slides out from beneath the panel's edge.
        const auto tucked = dock.flyout.withX (dock.flyoutLeft ? dock.panel.getX() : dock.panel.getRight() - dock.flyout.getWidth());
        const double move = motion::moveDuration (level, appearing ? 160.0 : 180.0);
        if (! bandAdvanced.isVisible())
            bandAdvancedMotion.snapTo (appearing || move <= 0.0 ? dock.flyout : tucked, 0.0f);
        bandAdvancedMotion.moveTo (dock.flyout, move, appearing ? motion::decelerate : motion::standard);
        bandAdvancedMotion.fadeTo (1.0f, motion::fadeDuration (level, 140.0), motion::decelerate);
    }
    else if (bandAdvanced.isVisible() && bandAdvancedMotion.getTargetAlpha() > 0.0f)
    {
        const auto current = bandAdvancedMotion.getTargetBounds();
        const bool wasLeft = current.getCentreX() < dock.panel.getCentreX();
        bandAdvancedMotion.moveTo (current.withX (wasLeft ? dock.panel.getX() : dock.panel.getRight() - current.getWidth()),
                                   motion::moveDuration (level, 140.0), motion::accelerate);
        bandAdvancedMotion.fadeTo (0.0f, motion::fadeDuration (level, 140.0), motion::accelerate);
    }
}

void SscEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (kHeaderHeight).reduced (12, 0);
    const auto nameFont = uiFont (15.0f, true);
    g.setColour (colours::text);
    g.setFont (nameFont);
    g.drawText (plugin::kPluginName, header, juce::Justification::centredLeft);
    if (getWidth() >= kWideWidth)
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (11.5f));
        g.drawText ("v" SSC_VERSION_STRING,
                    header.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (nameFont, plugin::kPluginName) + 8),
                    juce::Justification::centredLeft);
    }

    // Identity | presets …: a divider; and the edge of the display.
    g.setColour (colours::panelEdge);
    g.fillRect (presetBar.getX() - 9, kHeaderHeight / 2 - 8, 1, 16);
    g.fillRect (0, kHeaderHeight - 1, getWidth(), 1);
}

void SscEditor::resized()
{
    auto area = getLocalBounds();
    const bool wide = getWidth() >= kWideWidth;

    auto header = area.removeFromTop (kHeaderHeight).withTrimmedBottom (1).reduced (12, 0);
    helpButton.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 28));
    header.removeFromRight (2);
    settingsButton.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 28));
    header.removeFromRight (10);
    analyzerMenuButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 28));
    const int analyzerWidth = analyzerButton.getIdealWidth();
    analyzerButton.setBounds (header.removeFromRight (analyzerWidth).withSizeKeepingCentre (analyzerWidth, 28));
    header.removeFromRight (12);
    const int statusWidth = wide ? 200 : 116;
    status.setBounds (header.removeFromRight (statusWidth).withSizeKeepingCentre (statusWidth, 24));
    header.removeFromRight (16);
    // Name and version on the left (painted; the version only in wide windows).
    header.removeFromLeft (wide ? 228 : 168);
    const int presetWidth = juce::jmin (header.getWidth(), 540);
    presetBar.setBounds (header.removeFromLeft (presetWidth).withSizeKeepingCentre (presetWidth, 28));

    controlBar.setCompact (! wide);
    globalAdvanced.setCompact (! wide);
    controlBar.setBounds (area.removeFromBottom (kControlBarHeight));
    display.setBounds (area);
    eqRangeButton.setBounds (display.getRight() - 38 - 76, display.getY() + 6, 76, 24);
    layoutPanels (false);

    auto& tree = sscProcessor.getValueTreeState().state;
    tree.setProperty (plugin::kUiWidth, getWidth(), nullptr);
    tree.setProperty (plugin::kUiHeight, getHeight(), nullptr);
}

void SscEditor::refreshLiveViews()
{
    timerCallback();
    display.refreshFromAnalyzer();
    globalAdvanced.getMeterView().refresh();
}

void SscEditor::timerCallback()
{
    // A latency change (Max Lookahead, first/last Spectral band, Quality, phase mode, oversampling)
    // is pointed out briefly.
    const int latency = sscProcessor.getLatencySamples();
    if (shownLatency >= 0 && latency != shownLatency)
        latencyHighlightTicks = 12;
    shownLatency = latency;
    status.setHighlighted (latencyHighlightTicks > 0);
    latencyHighlightTicks = std::max (0, latencyHighlightTicks - 1);
    status.setText (statusText (getWidth() >= kWideWidth));
    status.setTooltip (statusDetails());

    auto* autoGain = sscProcessor.getValueTreeState().getRawParameterValue (plugin::id::autoGain);
    controlBar.setAutoGainReading (autoGain != nullptr && autoGain->load() >= 0.5f ? sscProcessor.getAutoGainDb() : 0.0f);
    updateAdvancedIndicator();

    // Settings restored by the host (project or preset load) reach an open editor.
    if (const auto stored = plugin::readAnalyzerSettings (sscProcessor.getValueTreeState().state); ! (stored == analyzerSettings))
        applyAnalyzerSettings (stored, false);

    display.setSampleRate (sscProcessor.getSampleRate());
    display.setResponseModel (sscProcessor.getResponseModel());
    const auto topology = sscProcessor.getChannelTopology();
    display.setChannelInfo (topology.numChannels, topology.numPairs);
    bandPanel.setTopology (topology);

    // Re-dock the panel once a drag has finished (never while the user is dragging).
    if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        layoutPanels (true);
}

juce::String SscEditor::statusText (bool full) const
{
    const double sampleRate = sscProcessor.getSampleRate();
    const int latency = sscProcessor.getLatencySamples();
    const double latencyMs = sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0;
    const auto ms = juce::String (latencyMs, 1) + " ms";
    const auto cpu = juce::String (sscProcessor.getCpuLoad() * 100.0, 1) + " %";
    if (! full)
        return ms + " " + middleDot + " " + cpu;
    return "Latency " + ms + "  " + middleDot + "  CPU " + cpu;
}

juce::String SscEditor::statusDetails() const
{
    // Total latency in ms and samples (PR-4), the oversampling factor while it is on.
    const double sampleRate = sscProcessor.getSampleRate();
    const int latency = sscProcessor.getLatencySamples();
    const double latencyMs = sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0;
    juce::String text = "Latency " + juce::String (latencyMs, 1) + " ms (" + juce::String (latency)
        + " samples), reported to the host and compensated by it\nCPU " + juce::String (sscProcessor.getCpuLoad() * 100.0, 1)
        + " % of the audio processing time";
    if (const int factor = sscProcessor.getOversamplingFactor(); factor > 1)
        text << "\nOversampling " << factor << "x";
    return text;
}
} // namespace ssc::ui
