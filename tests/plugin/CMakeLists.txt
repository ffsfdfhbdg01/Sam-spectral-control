juce_add_console_app(ssc_plugin_tests PRODUCT_NAME "SSC Plugin Tests")

target_sources(ssc_plugin_tests PRIVATE
    TestProcessor.cpp
    TestBandEditing.cpp
    TestSpeakers.cpp
    TestAnalyzer.cpp
    ${SSC_PLUGIN_SOURCES})

target_include_directories(ssc_plugin_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests)

target_compile_definitions(ssc_plugin_tests PRIVATE ${SSC_PLUGIN_DEFINITIONS})

target_link_libraries(ssc_plugin_tests PRIVATE
    ssc_core
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    Catch2::Catch2WithMain)

catch_discover_tests(ssc_plugin_tests DISCOVERY_MODE PRE_TEST)
