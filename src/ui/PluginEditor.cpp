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
    startTimerHz (4);
}

SscEditor::~SscEditor()
{
    // Solo is a monitoring aid tied to the open editor.
    sscProcessor.setSoloBand (-1);
    sscProcessor.getAnalyzer().removeClient();
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
    header.removeFromRight (12);
    status.setBounds (header.removeFromRight (440));
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
}

void SscEditor::timerCallback()
{
    // A latency change (Max Lookahead, first/last Spectral band, Quality) is pointed out briefly.
    const int latency = sscProcessor.getLatencySamples();
    if (shownLatency >= 0 && latency != shownLatency)
        latencyHighlightTicks = 12;
    shownLatency = latency;
    status.setColour (juce::Label::textColourId, latencyHighlightTicks > 0 ? colours::accent : colours::textDim);
    latencyHighlightTicks = std::max (0, latencyHighlightTicks - 1);
    status.setText (statusText(), juce::dontSendNotification);

    display.setSampleRate (sscProcessor.getSampleRate());
    const auto topology = sscProcessor.getChannelTopology();
    display.setChannelInfo (topology.numChannels, topology.numPairs);
    bandPanel.setChannelInfo (topology.numChannels, topology.numPairs);

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
    text << "Latency " << juce::String (latencyMs, 1) << " ms  |  CPU " << juce::String (sscProcessor.getCpuLoad() * 100.0, 1) << "%";
    return text;
}
} // namespace ssc::ui
