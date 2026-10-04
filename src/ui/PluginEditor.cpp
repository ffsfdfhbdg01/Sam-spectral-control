#include "ui/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "plugin/StateMigration.h"

namespace ssc::ui
{
namespace
{
    constexpr int kHeaderHeight = 36;
    constexpr int kControlBarHeight = 56;
    constexpr float kEqRanges[] = { 3.0f, 6.0f, 12.0f, 18.0f, 30.0f };
    constexpr float kDefaultEqRange = 12.0f;
} // namespace

SscEditor::SscEditor (plugin::SscAudioProcessor& p)
    : AudioProcessorEditor (p),
      sscProcessor (p),
      bandModel (p.getValueTreeState(), p.getUndoManager()),
      display (p.getAnalyzer(), bandModel),
      bandPanel (bandModel),
      controlBar (p.getValueTreeState())
{
    addAndMakeVisible (display);
    addChildComponent (bandPanel);
    addAndMakeVisible (controlBar);

    bandModel.onSoloChanged = [this] (int band) { sscProcessor.setSoloBand (band); };
    display.dynamicGainProvider = [this] (int band) { return sscProcessor.getBandDynamicGainDb (band); };
    display.spectralViewProvider = [this] { return sscProcessor.pollSpectralView(); };
    bandPanel.onPreferredSizeChanged = [this] { layoutBandPanel(); };
    auto& meterView = controlBar.getMeterView();
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
    bandPanel.readoutProvider = [this] (int band) {
        auto& readouts = sscProcessor.getReadouts().readouts();
        readouts.update();
        return readouts.read().bands[static_cast<std::size_t> (juce::jlimit (0, kNumBands - 1, band))];
    };

    for (int i = 0; i < static_cast<int> (std::size (kEqRanges)); ++i)
        eqRange.addItem (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (kEqRanges[i])) + " dB", i + 1);
    eqRange.setTooltip ("Visible EQ range");
    eqRange.onChange = [this] {
        const int index = eqRange.getSelectedId() - 1;
        if (index >= 0)
            setEqRange (kEqRanges[index]);
    };
    addAndMakeVisible (eqRange);
    setEqRange (static_cast<float> (sscProcessor.getValueTreeState().state.getProperty (plugin::kUiEqRange, kDefaultEqRange)));

    analyzerButton.setTooltip (
        "Analyzer: spectra, resolution, speed, tilt, range, freeze, peak hold, glow, resonance hints, meter warnings");
    analyzerButton.onClick = [this] { showAnalyzerMenu(); };
    addAndMakeVisible (analyzerButton);
    applyAnalyzerSettings (plugin::readAnalyzerSettings (sscProcessor.getValueTreeState().state), false);

    status.setJustificationType (juce::Justification::centredRight);
    status.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (status);

    const auto& tree = sscProcessor.getValueTreeState().state;
    const int width = tree.getProperty (plugin::kUiWidth, kDefaultWidth);
    const int height = tree.getProperty (plugin::kUiHeight, kDefaultHeight);

    // After the children are added, so that every control (e.g. slider text boxes) picks it up.
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (760, 460, 2600, 1600);
    setSize (width, height);

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
    sscProcessor.getReadouts().removeClient();
    sscProcessor.getMeters().removeClient();
    setLookAndFeel (nullptr);
}

void SscEditor::setEqRange (float rangeDb)
{
    int id = 0;
    for (int i = 0; i < static_cast<int> (std::size (kEqRanges)); ++i)
        if (juce::approximatelyEqual (kEqRanges[i], rangeDb))
            id = i + 1;
    if (id == 0)
    {
        rangeDb = kDefaultEqRange;
        id = 3;
    }

    eqRange.setSelectedId (id, juce::dontSendNotification);
    display.setEqRange (rangeDb);
    sscProcessor.getValueTreeState().state.setProperty (plugin::kUiEqRange, rangeDb, nullptr);
}

void SscEditor::applyAnalyzerSettings (const plugin::AnalyzerSettings& settings, bool store)
{
    analyzerSettings = settings;
    sscProcessor.getAnalyzer().setSettings (settings);
    display.setAnalyzerSettings (settings);
    controlBar.getMeterView().setThresholds (settings.thdWarningDb, settings.aliasingWarningDb);
    analyzerButton.setToggleState (settings.freeze, juce::dontSendNotification);
    analyzerButton.setButtonText (settings.freeze ? "Frozen" : "Analyzer");
    if (store)
        plugin::writeAnalyzerSettings (sscProcessor.getValueTreeState().state, settings);
}

void SscEditor::showAnalyzerMenu()
{
    using plugin::AnalyzerMode;
    using plugin::AnalyzerResolution;
    using plugin::AnalyzerSpeed;
    const auto s = analyzerSettings;
    auto change = [this] (auto&& edit) {
        return [this, edit] {
            auto next = analyzerSettings;
            edit (next);
            applyAnalyzerSettings (next, true);
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

    // Meter warning thresholds (MTR-4).
    menu.addSectionHeader ("Meters");
    juce::PopupMenu thdWarning, aliasingWarning;
    for (const float value : plugin::kThdWarnings)
        thdWarning.addItem (MeterView::formatDb (value), true, juce::approximatelyEqual (s.thdWarningDb, value),
                            change ([value] (plugin::AnalyzerSettings& n) { n.thdWarningDb = value; }));
    for (const float value : plugin::kAliasingWarnings)
        aliasingWarning.addItem (MeterView::formatDb (value), true, juce::approximatelyEqual (s.aliasingWarningDb, value),
                                 change ([value] (plugin::AnalyzerSettings& n) { n.aliasingWarningDb = value; }));
    menu.addSubMenu ("THD+N warning above", thdWarning);
    menu.addSubMenu ("Aliasing warning above", aliasingWarning);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (analyzerButton));
}

void SscEditor::layoutBandPanel()
{
    const auto area = display.getBounds();
    const auto preferred = bandPanel.getPreferredSize();
    const int width = juce::jmin (preferred.x, area.getWidth() - 16);
    const int height = juce::jmin (preferred.y, area.getHeight() - 16);

    // Docked at the bottom like Pro-Q; moves to the top when it would hide the band's own node.
    auto bounds = juce::Rectangle<int> (area.getCentreX() - width / 2, area.getBottom() - height - 22, width, height);
    const int band = bandPanel.getBand();
    if (band >= 0)
    {
        const auto node = display.nodePosition (band).toInt() + display.getPosition();
        if (bounds.expanded (12).contains (node))
            bounds.setY (area.getY() + 8);
    }
    bandPanel.setBounds (bounds);
}

void SscEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (kHeaderHeight).reduced (12, 0);
    g.setColour (colours::text);
    g.setFont (uiFont (16.0f, true));
    g.drawText (plugin::kPluginName, header, juce::Justification::centredLeft);

    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText ("v" SSC_VERSION_STRING, header.withTrimmedLeft (190), juce::Justification::centredLeft);
}

void SscEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (12, 0);
    eqRange.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 24));
    header.removeFromRight (8);
    analyzerButton.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 24));
    header.removeFromRight (12);
    status.setBounds (header.removeFromRight (juce::jmax (0, juce::jmin (440, header.getWidth() - 230))));
    controlBar.setBounds (area.removeFromBottom (kControlBarHeight));
    display.setBounds (area);
    layoutBandPanel();

    auto& tree = sscProcessor.getValueTreeState().state;
    tree.setProperty (plugin::kUiWidth, getWidth(), nullptr);
    tree.setProperty (plugin::kUiHeight, getHeight(), nullptr);
}

void SscEditor::refreshLiveViews()
{
    timerCallback();
    display.refreshFromAnalyzer();
    controlBar.getMeterView().refresh();
}

void SscEditor::timerCallback()
{
    // A latency change (Max Lookahead, first/last Spectral band, Quality, phase mode, oversampling)
    // is pointed out briefly.
    const int latency = sscProcessor.getLatencySamples();
    if (shownLatency >= 0 && latency != shownLatency)
        latencyHighlightTicks = 12;
    shownLatency = latency;
    status.setColour (juce::Label::textColourId, latencyHighlightTicks > 0 ? colours::accent : colours::textDim);
    latencyHighlightTicks = std::max (0, latencyHighlightTicks - 1);
    status.setText (statusText(), juce::dontSendNotification);

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
        layoutBandPanel();
}

juce::String SscEditor::statusText() const
{
    const double sampleRate = sscProcessor.getSampleRate();
    const int latency = sscProcessor.getLatencySamples();
    const double latencyMs = sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0;
    juce::String text;
    auto* autoGain = sscProcessor.getValueTreeState().getRawParameterValue (plugin::id::autoGain);
    if (autoGain != nullptr && autoGain->load() >= 0.5f)
        text << "Auto Gain " << plugin::format::decibels (sscProcessor.getAutoGainDb()) << "  |  ";
    const int factor = sscProcessor.getOversamplingFactor();
    if (factor > 1)
        text << "OS " << factor << "x  |  ";
    // Total latency in ms and samples (PR-4).
    text << "Latency " << juce::String (latencyMs, 1) << " ms (" << latency << " smp)  |  CPU "
         << juce::String (sscProcessor.getCpuLoad() * 100.0, 1) << "%";
    return text;
}
} // namespace ssc::ui
