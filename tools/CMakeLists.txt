# Developer tools (not shipped).

# CPU benchmark of the DSP core (NFR-1); needs no JUCE.
add_executable(ssc_benchmark Benchmark.cpp)
target_include_directories(ssc_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(ssc_benchmark PRIVATE ssc_core ssc_options)

# Evaluation of the resonance hints on synthetic material (VIS-5).
add_executable(ssc_resonance_eval ResonanceEval.cpp)
target_include_directories(ssc_resonance_eval PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(ssc_resonance_eval PRIVATE ssc_core ssc_options)

if(SSC_BUILD_PLUGIN)
    juce_add_console_app(ssc_screenshot PRODUCT_NAME "SSC Screenshot")
    target_sources(ssc_screenshot PRIVATE Screenshot.cpp ${SSC_PLUGIN_SOURCES})
    target_include_directories(ssc_screenshot PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(ssc_screenshot PRIVATE ${SSC_PLUGIN_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ssc_screenshot PRIVATE
        ssc_core
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
