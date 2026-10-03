#include "plugin/PluginProcessor.h"

#include "plugin/ChannelLayout.h"
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
    startTimerHz (10);
}

SscAudioProcessor::~SscAudioProcessor()
{
    stopTimer();
}

void SscAudioProcessor::timerCallback()
{
    const int latency = engine.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void SscAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = getTotalNumOutputChannels();

    engine::EngineConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.maxBlockSize = samplesPerBlock;
    cfg.numChannels = numChannels;
    cfg.topology = getChannelTopology();
    engine.prepare (cfg);
    readBandSettings (bandSettings);
    engine.configure (readGlobalParams (false), bandSettings);

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

engine::ChannelTopology SscAudioProcessor::getChannelTopology() const
{
    return topologyFor (getChannelLayoutOfBus (false, 0));
}

namespace
{
    /** Choice parameter value (stored as the index) as an enum, limited to [0, last]. */
    template <typename Enum>
    Enum choice (const std::atomic<float>* raw, Enum last) noexcept
    {
        const auto index = static_cast<int> (std::lround (raw->load (std::memory_order_relaxed)));
        return static_cast<Enum> (juce::jlimit (0, static_cast<int> (last), index));
    }

    bool isOn (const std::atomic<float>* raw) noexcept
    {
        return raw->load (std::memory_order_relaxed) >= 0.5f;
    }
} // namespace

engine::GlobalParams SscAudioProcessor::readGlobalParams (bool forceBypass) const noexcept
{
    engine::GlobalParams p;
    p.outputGainDb = params.outputGain->load (std::memory_order_relaxed);
    p.mix = params.mix->load (std::memory_order_relaxed) * 0.01;
    p.bypass = forceBypass || isOn (params.bypass);
    p.quality = choice (params.quality, Quality::Max);
    p.soloBand = soloBand.load (std::memory_order_relaxed);
    p.maxLookaheadMs = params.maxLookahead->load (std::memory_order_relaxed);
    return p;
}

void SscAudioProcessor::readBandSettings (engine::BandSettingsArray& bands) const noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& raw = params.bands[static_cast<std::size_t> (b)];
        auto get = [&raw] (BandParam p) { return raw[static_cast<std::size_t> (p)]; };
        auto& s = bands[static_cast<std::size_t> (b)];

        s.used = isOn (get (BandParam::Used));
        s.active = s.used && isOn (get (BandParam::On));
        s.shape.shape = choice (get (BandParam::Shape), Shape::AllPass);
        s.shape.frequencyHz = get (BandParam::Frequency)->load (std::memory_order_relaxed);
        s.shape.gainDb = get (BandParam::Gain)->load (std::memory_order_relaxed);
        s.shape.q = get (BandParam::Q)->load (std::memory_order_relaxed);
        s.shape.slope = choice (get (BandParam::Slope), Slope::Brickwall);
        s.mode = choice (get (BandParam::Mode), BandMode::Spectral);
        s.component = choice (get (BandParam::Target), StereoComponent::Side);
        s.character = choice (get (BandParam::Character), Character::Sharp);

        auto value = [&get] (BandParam p) { return static_cast<double> (get (p)->load (std::memory_order_relaxed)); };
        auto& d = s.dynamics;
        d.thresholdMode = choice (get (BandParam::ThresholdMode), ThresholdMode::Relative);
        d.thresholdDb = value (BandParam::Threshold);
        d.ratio = value (BandParam::Ratio);
        d.direction = choice (get (BandParam::Direction), Direction::Below);
        d.rangeDb = value (BandParam::Range);
        d.attackMs = value (BandParam::Attack);
        d.releaseMs = value (BandParam::Release);
        d.lookaheadMs = value (BandParam::Lookahead);
        d.kneeDb = value (BandParam::Knee);
        d.mix = 0.01 * value (BandParam::Mix);
        d.selectivity = 0.01 * value (BandParam::Selectivity);
        d.link = choice (get (BandParam::Link), DetectorLink::All);
        d.autoGain = isOn (get (BandParam::AutoGain));
    }
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

    readBandSettings (bandSettings);
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), readGlobalParams (forceBypass),
                    bandSettings);

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
