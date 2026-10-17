#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/AnalyzerService.h"
#include "plugin/MeterService.h"
#include "plugin/Parameters.h"
#include "plugin/ReadoutService.h"
#include "ssc/engine/Engine.h"
#include "ssc/util/Realtime.h"
#include "ui/ResponseCurves.h"

namespace ssc::plugin
{
inline constexpr const char* kPluginName = "Sam Spectral Control";

class SscAudioProcessor final : public juce::AudioProcessor,
                                private juce::Timer,
                                private juce::ValueTree::Listener,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    SscAudioProcessor();
    ~SscAudioProcessor() override;

    // AudioProcessor
    const juce::String getName() const override { return kPluginName; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; } // one program; presets have their own menu
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Sam Spectral Control
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return state; }
    AnalyzerService& getAnalyzer() noexcept { return analyzer; }
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }

    /** Band auditioned on its own (−1 = none). A monitoring aid: not a parameter, not saved. */
    void setSoloBand (int band) noexcept { soloBand.store (band, std::memory_order_relaxed); }
    int getSoloBand() const noexcept { return soloBand.load (std::memory_order_relaxed); }

    /** Channel topology of the current main bus layout (message thread). */
    engine::ChannelTopology getChannelTopology() const;

    /** Undo history of band edits; lives here so it survives closing the editor. */
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Current dynamic gain change of a band in dB (DYN-11); any thread. */
    float getBandDynamicGainDb (int band) const noexcept { return engine.getBandGainChangeDb (band); }

    /** Global Auto Gain correction currently applied (dB); any thread. */
    float getAutoGainDb() const noexcept { return engine.getAutoGainDb(); }

    /** Blocks in which non-finite audio was replaced by silence (PLT-5); any thread. */
    std::uint32_t getNonFiniteEvents() const noexcept { return engine.getNonFiniteEvents(); }

    /** Oversampling factor of the band chain from the parameters, limited by the sample rate; any thread. */
    int getOversamplingFactor() const noexcept;
    /** True while the host feeds the Sidechain input bus (message thread). */
    bool isSidechainConnected() const;

    /** True when the Natural Phase corrector runs (Natural Phase, Analog Feel at High/Max); any thread. */
    bool isPhaseCorrected() const noexcept;

    /** How the bands are heard, for the display; any thread. */
    ui::ResponseCurves::Model getResponseModel() const noexcept;

    /** Loudspeaker selection of a band (CH-4), as the audio thread uses it; any thread. */
    engine::SpeakerSelection getSpeakers (int band) const noexcept;

    /** Band readouts (phase, group delay, ringing; research §12c), computed in the background. */
    ReadoutService& getReadouts() noexcept { return readoutService; }

    /** THD+N and aliasing meters (research §13c), computed in the background while shown. */
    MeterService& getMeters() noexcept { return meterService; }

    /** Newest gain view of the Spectral bands, or nullptr if nothing new (one UI reader). */
    const spectral::SpectralEngine::MaskView* pollSpectralView() noexcept { return engine.pollSpectralView(); }

private:
    /** Reports latency changes (Max Lookahead, spectral engine) to the host from the message thread. */
    void timerCallback() override;

    template <typename Sample>
    void processImpl (juce::AudioBuffer<Sample>& buffer, bool forceBypass) noexcept SSC_REALTIME;

    /** Mono sum of the main bus (numChannels first channels) for the analyzer. */
    template <typename Sample>
    void pushAnalyzerTap (const juce::AudioBuffer<Sample>& buffer, int numChannels, bool post) noexcept;

    engine::GlobalParams readGlobalParams (bool forceBypass) const noexcept;
    void readBandSettings (engine::BandSettingsArray& bands) const noexcept;

    // The speaker selections live in the state tree; the audio thread reads atomic copies.
    void refreshSpeakers();
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeRedirected (juce::ValueTree& tree) override;

    // Changes made on the message thread (the editor) wake the band readouts at once (PR-2).
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    juce::Thread::ThreadID messageThread = nullptr;

    juce::UndoManager undoManager { 0, 100 };
    juce::AudioProcessorValueTreeState state;
    ParameterPointers params;
    engine::Engine engine;
    engine::BandSettingsArray bandSettings {};
    std::atomic<int> soloBand { -1 };
    std::array<std::atomic<std::uint64_t>, kNumBands> speakerRoles {};
    std::array<std::atomic<std::uint32_t>, kNumBands> speakerDiscrete {};
    AnalyzerService analyzer;
    std::vector<float> tapScratch;
    juce::AudioProcessLoadMeasurer loadMeasurer;

    MeterService meterService { engine }; // after the engine: stops before it goes

    // Last member: its thread reads the parameters above and stops first.
    ReadoutService readoutService { [this] (engine::GlobalParams& p, engine::BandSettingsArray& b, std::array<float, kNumBands>& gains,
                                            double& sampleRate) {
        p = readGlobalParams (false);
        readBandSettings (b);
        for (int i = 0; i < kNumBands; ++i)
            gains[static_cast<std::size_t> (i)] = getBandDynamicGainDb (i);
        sampleRate = getSampleRate();
    } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SscAudioProcessor)
};
} // namespace ssc::plugin
