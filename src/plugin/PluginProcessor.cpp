#include "plugin/PluginProcessor.h"

#include <optional>

#include "plugin/ChannelLayout.h"
#include "plugin/SpeakerState.h"
#include "plugin/StateMigration.h"
#include "ui/PluginEditor.h"

namespace ssc::plugin
{
SscAudioProcessor::SscAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, kStateType, createParameterLayout())
{
    params.attach (state);
    state.state.setProperty (kVersionProperty, kStateVersion, nullptr);
    state.state.addListener (this);
    refreshSpeakers();
    startTimerHz (10);

    // Hosts create plug-ins on the message thread. Its ID is kept here because
    // MessageManager::isThisTheMessageThread() takes a lock, and the parameter listener also runs on
    // the audio thread (host automation). The listener belongs to the value tree state, which calls
    // it after the raw values the readouts read have changed.
    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating())
        messageThread = messageManager->getCurrentMessageThread();
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            state.addParameterListener (withId->getParameterID(), this);
}

SscAudioProcessor::~SscAudioProcessor()
{
    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            state.removeParameterListener (withId->getParameterID(), this);
    stopTimer();
    state.state.removeListener (this);
}

void SscAudioProcessor::parameterChanged (const juce::String&, float)
{
    // Waking a thread is not real-time safe, comparing thread IDs is: host automation on the audio
    // thread leaves the readouts to their 50 ms poll.
    if (messageThread != nullptr && juce::Thread::getCurrentThreadId() == messageThread)
        readoutService.settingsChanged();
}

void SscAudioProcessor::refreshSpeakers()
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto selection = readSpeakers (state.state, b);
        speakerRoles[static_cast<std::size_t> (b)].store (selection.roles, std::memory_order_relaxed);
        speakerDiscrete[static_cast<std::size_t> (b)].store (selection.discrete, std::memory_order_relaxed);
    }
}

engine::SpeakerSelection SscAudioProcessor::getSpeakers (int band) const noexcept
{
    engine::SpeakerSelection selection;
    selection.roles = speakerRoles[static_cast<std::size_t> (band)].load (std::memory_order_relaxed);
    selection.discrete = speakerDiscrete[static_cast<std::size_t> (band)].load (std::memory_order_relaxed);
    return selection;
}

void SscAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (kSpeakersType))
        refreshSpeakers();
}

void SscAudioProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (kSpeakersType))
        refreshSpeakers();
}

void SscAudioProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (kSpeakersType))
        refreshSpeakers();
}

void SscAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    refreshSpeakers();
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

    // Sidechain (detection of Dynamic and Spectral bands): off, or one or two channels of any kind.
    if (layouts.inputBuses.size() > 1 && layouts.getChannelSet (true, 1).size() > 2)
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
    p.delta = isOn (params.delta);
    p.quality = choice (params.quality, Quality::Max);
    p.soloBand = soloBand.load (std::memory_order_relaxed);
    p.maxLookaheadMs = params.maxLookahead->load (std::memory_order_relaxed);
    p.autoGain = isOn (params.autoGain);
    p.autoGainWeighting = choice (params.autoGainWeighting, AutoGainWeighting::Digital);
    p.autoGainSpeed = choice (params.autoGainSpeed, AutoGainSpeed::Stable);
    p.phaseMode = choice (params.phaseMode, PhaseMode::AnalogFeel);
    p.analogModel = choice (params.analogModel, AnalogModel::S);
    p.oversampling = choice (params.oversampling, Oversampling::X8);
    if (const int render = static_cast<int> (std::lround (params.renderOversampling->load (std::memory_order_relaxed))); render > 0)
        p.renderOversampling = static_cast<Oversampling> (juce::jlimit (0, static_cast<int> (Oversampling::X8), render - 1));
    p.nonRealtime = isNonRealtime();
    return p;
}

int SscAudioProcessor::getOversamplingFactor() const noexcept
{
    const double fs = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    return engine::effectiveOversampling (choice (params.oversampling, Oversampling::X8), choice (params.phaseMode, PhaseMode::AnalogFeel),
                                          choice (params.quality, Quality::Max), fs);
}

bool SscAudioProcessor::isPhaseCorrected() const noexcept
{
    return engine::usesPhaseCorrection (choice (params.phaseMode, PhaseMode::AnalogFeel), choice (params.quality, Quality::Max));
}

ui::ResponseCurves::Model SscAudioProcessor::getResponseModel() const noexcept
{
    ui::ResponseCurves::Model model;
    model.oversampling = getOversamplingFactor();
    model.analog = isPhaseCorrected();
    model.firBrickwall = model.analog && choice (params.phaseMode, PhaseMode::AnalogFeel) == PhaseMode::NaturalPhase;
    model.quality = choice (params.quality, Quality::Max);
    if (choice (params.phaseMode, PhaseMode::AnalogFeel) == PhaseMode::AnalogFeel)
        model.analogModel = choice (params.analogModel, AnalogModel::S);
    return model;
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
        // Speakers are stored by role: map them onto the current layout (CH-4); the selection
        // decides about LFE as well.
        s.channelMask = getSpeakers (b).channelMask (engine.getConfig().topology);
        s.includeLfe = true;
        s.shape.character = choice (get (BandParam::Character), Character::Sharp);

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
        d.sidechain = isOn (get (BandParam::Sidechain));
        d.autoGain = isOn (get (BandParam::AutoGain));
        d.autoGainSeconds = loudness::autoGainSeconds (choice (params.autoGainSpeed, AutoGainSpeed::Stable));
    }
}

template <typename Sample>
void SscAudioProcessor::pushAnalyzerTap (const juce::AudioBuffer<Sample>& buffer, int numChannels, bool post) noexcept
{
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
void SscAudioProcessor::processImpl (juce::AudioBuffer<Sample>& buffer, bool forceBypass) noexcept SSC_REALTIME
{
    juce::ScopedNoDenormals noDenormals;
    // Hosts may send empty blocks (e.g. to flush parameter changes); JUCE's load timer asserts on them.
    std::optional<juce::AudioProcessLoadMeasurer::ScopedTimer> timer;
    if (buffer.getNumSamples() > 0)
        timer.emplace (loadMeasurer, buffer.getNumSamples());

    // The main bus (the first channels of the buffer) is processed in place; the sidechain bus only
    // feeds the detection of bands set to it. Channel pointers, not bus buffers: a bus buffer of
    // 32 channels would allocate.
    const int numChannels = std::min (getMainBusNumOutputChannels(), buffer.getNumChannels());
    for (int ch = getMainBusNumInputChannels(); ch < numChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    const Sample* sidechain[2] = { nullptr, nullptr };
    int numSidechain = 0;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        const int channels = std::min (2, getChannelCountOfBus (true, 1));
        for (int k = 0; k < channels; ++k)
        {
            const int index = getChannelIndexInProcessBlockBuffer (true, 1, k);
            if (index >= 0 && index < buffer.getNumChannels())
                sidechain[numSidechain++] = buffer.getReadPointer (index);
        }
    }

    pushAnalyzerTap (buffer, numChannels, false);

    readBandSettings (bandSettings);
    engine.process (buffer.getArrayOfWritePointers(), numChannels, buffer.getNumSamples(), readGlobalParams (forceBypass), bandSettings,
                    numSidechain > 0 ? sidechain : nullptr, numSidechain);

    pushAnalyzerTap (buffer, numChannels, true);
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

bool SscAudioProcessor::isSidechainConnected() const
{
    return getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
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

    // A state saved before a parameter existed (the bands' Sidechain came after 1.0) sets it to its
    // default, not leaving it at the value this instance happens to have.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            ranged != nullptr && ! tree.getChildWithProperty ("id", ranged->getParameterID()).isValid())
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", ranged->getParameterID(), nullptr);
            param.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
            tree.appendChild (param, nullptr);
        }

    state.replaceState (tree);
    refreshSpeakers();
}
} // namespace ssc::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ssc::plugin::SscAudioProcessor();
}
