#include "ui/PluginEditor.h"

#include "plugin/PluginProcessor.h"
#include "plugin/StateMigration.h"

namespace ssc::ui
{
namespace
{
    constexpr int kHeaderHeight = 36;
    constexpr int kControlBarHeight = 56;
} // namespace

SscEditor::SscEditor (plugin::SscAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      display (p.getAnalyzer()),
      controlBar (p.getValueTreeState())
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (display);
    addAndMakeVisible (controlBar);

    status.setJustificationType (juce::Justification::centredRight);
    status.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (status);

    const auto& tree = processor.getValueTreeState().state;
    const int width = tree.getProperty (plugin::kUiWidth, kDefaultWidth);
    const int height = tree.getProperty (plugin::kUiHeight, kDefaultHeight);

    setResizable (true, true);
    setResizeLimits (760, 460, 2600, 1600);
    setSize (width, height);

    processor.getAnalyzer().addClient();
    startTimerHz (4);
}

SscEditor::~SscEditor()
{
    processor.getAnalyzer().removeClient();
    setLookAndFeel (nullptr);
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
    status.setBounds (area.removeFromTop (kHeaderHeight).reduced (12, 0).removeFromRight (320));
    controlBar.setBounds (area.removeFromBottom (kControlBarHeight));
    display.setBounds (area);

    auto& tree = processor.getValueTreeState().state;
    tree.setProperty (plugin::kUiWidth, getWidth(), nullptr);
    tree.setProperty (plugin::kUiHeight, getHeight(), nullptr);
}

void SscEditor::refreshLiveViews()
{
    display.refreshFromAnalyzer();
    timerCallback();
}

void SscEditor::timerCallback()
{
    status.setText (statusText(), juce::dontSendNotification);
}

juce::String SscEditor::statusText() const
{
    const double sampleRate = processor.getSampleRate();
    const int latency = processor.getLatencySamples();
    const double latencyMs = sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0;
    return "Latency " + juce::String (latencyMs, 1) + " ms  |  CPU " + juce::String (processor.getCpuLoad() * 100.0, 1) + "%";
}
} // namespace ssc::ui
