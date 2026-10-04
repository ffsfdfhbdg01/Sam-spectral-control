set(SSC_PLUGIN_SOURCES
    plugin/Parameters.cpp
    plugin/ChannelLayout.cpp
    plugin/StateMigration.cpp
    plugin/SpeakerState.cpp
    plugin/AnalyzerService.cpp
    plugin/AnalyzerSettings.cpp
    plugin/ReadoutService.cpp
    plugin/MeterService.cpp
    plugin/PluginProcessor.cpp
    ui/Theme.cpp
    ui/BandLogic.cpp
    ui/BandModel.cpp
    ui/ResponseCurves.cpp
    ui/EqDisplay.cpp
    ui/BandPanel.cpp
    ui/SpeakerPanel.cpp
    ui/MeterView.cpp
    ui/ControlBar.cpp
    ui/PluginEditor.cpp)
list(TRANSFORM SSC_PLUGIN_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(SSC_PLUGIN_SOURCES ${SSC_PLUGIN_SOURCES} PARENT_SCOPE)

set(SSC_PLUGIN_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SSC_VERSION_STRING="${PROJECT_VERSION}")
set(SSC_PLUGIN_DEFINITIONS ${SSC_PLUGIN_DEFINITIONS} PARENT_SCOPE)

set(ssc_formats VST3)
if(APPLE AND SSC_FORMAT_AU)
    list(APPEND ssc_formats AU)
endif()

# Identifiers are provisional until ADR-0016 is accepted; never change them after the first release.
juce_add_plugin(SamSpectralControl
    COMPANY_NAME "Sam Audio"
    BUNDLE_ID com.samaudio.samspectralcontrol
    PLUGIN_MANUFACTURER_CODE SamA
    PLUGIN_CODE Sspc
    FORMATS ${ssc_formats}
    PRODUCT_NAME "Sam Spectral Control"
    DESCRIPTION "Spectral and dynamic equalizer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(SamSpectralControl PRIVATE ${SSC_PLUGIN_SOURCES})
ssc_enable_werror_for_sources(${SSC_PLUGIN_SOURCES})
target_include_directories(SamSpectralControl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(SamSpectralControl PUBLIC ${SSC_PLUGIN_DEFINITIONS})
target_link_libraries(SamSpectralControl
    PRIVATE
        ssc_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
