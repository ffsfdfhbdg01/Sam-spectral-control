include(Catch)

add_executable(ssc_core_tests
    core/TestUtils.cpp
    core/TestSpscRingBuffer.cpp
    core/TestTripleBuffer.cpp
    core/TestRealFft.cpp
    core/TestSpectrumAnalyzer.cpp
    core/TestEngine.cpp
    core/TestFilterDesign.cpp
    core/TestBandProcessor.cpp
    core/TestDynamics.cpp
    core/TestDynamicBands.cpp
    core/TestSpectral.cpp
    core/TestAutoGain.cpp
    core/TestDeterminism.cpp
    core/TestOversampling.cpp
    core/TestCharacter.cpp
    core/TestNaturalPhase.cpp
    core/TestEngineModes.cpp
    core/TestSpeakers.cpp)

target_include_directories(ssc_core_tests PRIVATE .)
target_link_libraries(ssc_core_tests PRIVATE ssc_core ssc_options Catch2::Catch2WithMain)
ssc_enable_werror(ssc_core_tests)
catch_discover_tests(ssc_core_tests DISCOVERY_MODE PRE_TEST)

if(SSC_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
