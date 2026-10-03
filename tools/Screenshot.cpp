// Renders the plugin editor with live test audio and saves a PNG.
// Usage: ssc_screenshot <output.png> [width height] [paramId=value ...] [select=<band 1-24>]
// Run under a display server (e.g. xvfb-run on Linux).

#include <atomic>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "TestSignals.h"
#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"

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

    auto processor = std::make_unique<ssc::plugin::SscAudioProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
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
    for (; argIndex < argc; ++argIndex)
    {
        const juce::String assignment (argv[argIndex]);
        const auto paramId = assignment.upToFirstOccurrenceOf ("=", false, false);
        const auto value = assignment.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (paramId == "select")
            selectBand = juce::roundToInt (value) - 1;
        else if (auto* param = state.getParameter (paramId))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        else
            std::fprintf (stderr, "unknown parameter: %s\n", paramId.toRawUTF8());
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
        juce::AudioBuffer<float> buffer (2, blockSize);
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
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v * 0.9f);
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
