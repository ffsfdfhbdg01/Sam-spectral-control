juce_add_console_app(ssc_plugin_tests PRODUCT_NAME "SSC Plugin Tests")

target_sources(ssc_plugin_tests PRIVATE
    TestProcessor.cpp
    TestBandEditing.cpp
    TestSpeakers.cpp
    TestAnalyzer.cpp
    TestMeters.cpp
    TestPresets.cpp
    TestStress.cpp
    ../AllocationCounter.cpp
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

# RealtimeSanitizer build: the audio callback (SSC_REALTIME) is checked while the tests run.
if(SSC_ENABLE_RTSAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ssc_plugin_tests PRIVATE -fsanitize=realtime -Wno-function-effects)
    target_link_options(ssc_plugin_tests PRIVATE -fsanitize=realtime)
    target_compile_definitions(ssc_plugin_tests PRIVATE SSC_RTSAN=1)
endif()

catch_discover_tests(ssc_plugin_tests DISCOVERY_MODE PRE_TEST)
