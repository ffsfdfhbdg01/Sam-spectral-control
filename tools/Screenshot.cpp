// Renders the plugin editor with live test audio and saves a PNG.
// Usage: ssc_screenshot <output.png> [width height] [paramId=value ...] [select=<band 1-24>]
//        [layout=stereo|5.1|7.1.4|9.1.6] [speakers=<band 1-24>:<roles hex>] [speakerpanel=<band 1-24>]
// speakerpanel renders that band's loudspeaker panel instead of the editor.
// Run under a display server (e.g. xvfb-run on Linux).

#include <atomic>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/ChannelLayout.h"
#include "plugin/PluginProcessor.h"
#include "plugin/SpeakerState.h"
#include "ui/PluginEditor.h"
#include "ui/SpeakerPanel.h"

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: ssc_screenshot <output.png> [width height] [paramId=value ...]\n");
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI init;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // The layout must be known before prepareToPlay.
    juce::AudioChannelSet layout = juce::AudioChannelSet::stereo();
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (! arg.startsWith ("layout="))
            continue;
        const auto name = arg.fromFirstOccurrenceOf ("=", false, false);
        if (name == "5.1")
            layout = juce::AudioChannelSet::create5point1();
        else if (name == "7.1.4")
            layout = juce::AudioChannelSet::create7point1point4();
        else if (name == "9.1.6")
            layout = juce::AudioChannelSet::create9point1point6();
    }

    auto processor = std::make_unique<ssc::plugin::SscAudioProcessor>();
    juce::AudioProcessor::BusesLayout buses;
    buses.inputBuses.add (layout);
    buses.outputBuses.add (layout);
    processor->setBusesLayout (buses);
    const int numChannels = layout.size();
    processor->setPlayConfigDetails (numChannels, numChannels, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);

    int argIndex = 2;
    int width = 0, height = 0;
    if (argc >= 4 && juce::String (argv[2]).containsOnly ("0123456789"))
    {
        width = juce::String (argv[2]).getIntValue();
        height = juce::String (argv[3]).getIntValue();
        argIndex = 4;
    }

    auto& state = processor->getValueTreeState();
    int selectBand = -1;
    int speakerPanelBand = -1;
    for (; argIndex < argc; ++argIndex)
    {
        const juce::String assignment (argv[argIndex]);
        const auto paramId = assignment.upToFirstOccurrenceOf ("=", false, false);
        const auto value = assignment.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (paramId == "select")
            selectBand = juce::roundToInt (value) - 1;
        else if (paramId == "layout")
            continue;
        else if (paramId == "speakerpanel")
            speakerPanelBand = juce::roundToInt (value) - 1;
        else if (paramId == "speakers")
        {
            const auto text = assignment.fromFirstOccurrenceOf ("=", false, false);
            const int band = text.upToFirstOccurrenceOf (":", false, false).getIntValue() - 1;
            auto selection = ssc::engine::SpeakerSelection {};
            selection.roles = static_cast<std::uint64_t> (text.fromFirstOccurrenceOf (":", false, false).getHexValue64());
            ssc::plugin::writeSpeakers (state.state, band, selection, nullptr);
        }
        else if (auto* param = state.getParameter (paramId))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        else
            std::fprintf (stderr, "unknown parameter: %s\n", paramId.toRawUTF8());
    }

    if (speakerPanelBand >= 0)
    {
        // The loudspeaker panel on its own (it normally opens in a call-out box).
        juce::UndoManager undo;
        ssc::ui::BandModel model (state, undo);
        ssc::ui::LookAndFeel lookAndFeel;
        ssc::ui::SpeakerPanel panel (model, speakerPanelBand, ssc::plugin::topologyFor (layout));
        panel.setLookAndFeel (&lookAndFeel);
        const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 2.0f);
        panel.setLookAndFeel (nullptr);
        juce::File output (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        output.deleteFile();
        juce::FileOutputStream stream (output);
        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;
        std::printf ("wrote %s (%dx%d)\n", output.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        return 0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    if (width > 0 && height > 0)
        editor->setSize (width, height);
    if (auto* ssc = dynamic_cast<ssc::ui::SscEditor*> (editor.get()); ssc != nullptr && selectBand >= 0)
    {
        ssc->getBandModel().refresh();
        ssc->getBandModel().select (selectBand, false);
    }

    // Rendered offscreen: no window manager or vblank needed.
    editor->setVisible (true);

    // Test programme: pink noise plus a few tones, fed in real time from an "audio" thread.
    const auto noise = ssc::test::pinkNoise (1 << 18);
    std::atomic<bool> running { true };
    std::thread audio ([&] {
        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        juce::MidiBuffer midi;
        std::size_t position = 0;
        long long sample = 0;
        while (running)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = static_cast<double> (sample++) / sampleRate;
                const float tones =
                    static_cast<float> (0.05 * std::sin (ssc::kTwoPi * 220.0 * t) + 0.03 * std::sin (ssc::kTwoPi * 3150.0 * t));
                const float v = noise[position] * 0.8f + tones;
                position = (position + 1) % noise.size();
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample (ch, i, v * (ch % 2 == 0 ? 1.0f : 0.9f));
            }
            processor->processBlock (buffer, midi);
            std::this_thread::sleep_for (std::chrono::microseconds (static_cast<int> (1.0e6 * blockSize / sampleRate)));
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
    running = false;
    audio.join();

    if (auto* ssc = dynamic_cast<ssc::ui::SscEditor*> (editor.get()))
        ssc->refreshLiveViews();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File output (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    output.deleteFile();
    juce::FileOutputStream stream (output);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "could not write %s\n", output.getFullPathName().toRawUTF8());
        return 1;
    }

    editor.reset();
    std::printf ("wrote %s (%dx%d)\n", output.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
