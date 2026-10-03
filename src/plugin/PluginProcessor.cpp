#include "plugin/PluginProcessor.h"

#include "plugin/StateMigration.h"
#include "ui/PluginEditor.h"

namespace ssc::plugin
{
SscAudioProcessor::SscAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, kStateType, createParameterLayout())
{
    params.attach (state);
    state.state.setProperty (kVersionProperty, kStateVersion, nullptr);
}

SscAudioProcessor::~SscAudioProcessor() = default;

void SscAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = getTotalNumOutputChannels();

    engine::EngineConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.maxBlockSize = samplesPerBlock;
    cfg.numChannels = numChannels;
    engine.prepare (cfg);

    tapScratch.assign (static_cast<std::size_t> (std::max (1, samplesPerBlock)), 0.0f);
    analyzer.prepare (sampleRate);
    loadMeasurer.reset (sampleRate, samplesPerBlock);

    setLatencySamples (engine.getLatencySamples());
}

void SscAudioProcessor::releaseResources() {}

bool SscAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out || in.isDisabled())
        return false;
    if (in.size() < 1 || in.size() > kMaxChannels)
        return false;
    // Per-channel processing would destroy an ambisonic sound field (research §4).
    if (in.getAmbisonicOrder() >= 0)
        return false;

    return true;
}

engine::GlobalParams SscAudioProcessor::readGlobalParams (bool forceBypass) const noexcept
{
    engine::GlobalParams p;
    p.outputGainDb = params.outputGain->load (std::memory_order_relaxed);
    p.mix = params.mix->load (std::memory_order_relaxed) * 0.01;
    p.bypass = forceBypass || params.bypass->load (std::memory_order_relaxed) >= 0.5f;
    return p;
}

template <typename Sample>
void SscAudioProcessor::pushAnalyzerTap (const juce::AudioBuffer<Sample>& buffer, bool post) noexcept
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels == 0 || tapScratch.empty())
        return;

    const auto scale = static_cast<float> (1.0 / numChannels);
    const int chunk = static_cast<int> (tapScratch.size());

    for (int offset = 0; offset < numSamples; offset += chunk)
    {
        const int n = std::min (chunk, numSamples - offset);
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += static_cast<float> (buffer.getReadPointer (ch)[offset + i]);
            tapScratch[static_cast<std::size_t> (i)] = sum * scale;
        }

        if (post)
            analyzer.pushPost (tapScratch.data(), n);
        else
            analyzer.pushPre (tapScratch.data(), n);
    }
}

template <typename Sample>
void SscAudioProcessor::processImpl (juce::AudioBuffer<Sample>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    pushAnalyzerTap (buffer, false);

    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), readGlobalParams (forceBypass));

    pushAnalyzerTap (buffer, true);
}

void SscAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer, false);
}

void SscAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer, false);
}

void SscAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer, true);
}

void SscAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    processImpl (buffer, true);
}

juce::AudioProcessorParameter* SscAudioProcessor::getBypassParameter() const
{
    return state.getParameter (id::bypass);
}

juce::AudioProcessorEditor* SscAudioProcessor::createEditor()
{
    return new ui::SscEditor (*this);
}

void SscAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty (kVersionProperty, kStateVersion, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void SscAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! migrateState (tree))
        return;

    state.replaceState (tree);
}
} // namespace ssc::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ssc::plugin::SscAudioProcessor();
}
