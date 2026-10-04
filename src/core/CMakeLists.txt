add_library(ssc_core STATIC
    src/fft/RealFft.cpp
    src/analysis/SpectrumAnalyzer.cpp
    src/analysis/SpectrumActivity.cpp
    src/analysis/PitchTracker.cpp
    src/analysis/ResonanceDetector.cpp
    src/analysis/ResonanceHints.cpp
    src/engine/Engine.cpp
    src/engine/BandProcessor.cpp
    src/engine/GainTable.cpp
    src/engine/ChannelTopology.cpp
    src/spectral/SpectralEngine.cpp
    src/loudness/Loudness.cpp
    src/loudness/AutoGain.cpp
    src/oversampling/Oversampler.cpp
    src/phase/PhaseCorrector.cpp
    src/filters/MatchedDesign.cpp
    src/filters/BandDesign.cpp
    src/filters/Character.cpp)

target_include_directories(ssc_core PUBLIC include)
target_compile_features(ssc_core PUBLIC cxx_std_20)
target_link_libraries(ssc_core
    PUBLIC ssc_pffft
    PRIVATE ssc_options)
set_target_properties(ssc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
ssc_enable_werror(ssc_core)
