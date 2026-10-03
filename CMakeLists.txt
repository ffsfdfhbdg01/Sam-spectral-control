cmake_minimum_required(VERSION 3.25)

project(SamSpectralControl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SSC_BUILD_TESTS "Build unit tests" ON)
option(SSC_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(SSC_FORMAT_AU "Build the Audio Unit format (macOS only)" OFF)
option(SSC_ENABLE_SANITIZERS "Build tests with ASan/UBSan" OFF)
option(SSC_ENABLE_RTSAN "Build tests with RealtimeSanitizer (Clang 20+)" OFF)
option(SSC_BUILD_TOOLS "Build developer tools (screenshot, measurement)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SscCompilerOptions)

# Third-party code -------------------------------------------------------------

add_library(ssc_pffft STATIC
    external/pffft/src/pffft.c
    external/pffft/src/pffft_common.c)
target_include_directories(ssc_pffft
    PUBLIC external/pffft/include/pffft
    PRIVATE external/pffft/src)
target_compile_definitions(ssc_pffft PUBLIC PFFFT_STATIC_DEFINE)
if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    # pffft only uses NEON when asked to; the header still checks the target architecture,
    # so this is safe for universal (arm64 + x86_64) macOS builds.
    target_compile_definitions(ssc_pffft PRIVATE PFFFT_ENABLE_NEON)
endif()
set_target_properties(ssc_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)

# DSP core (no GUI, no plugin wrapper) -------------------------------------------

add_subdirectory(src/core)

# Plugin -------------------------------------------------------------------------

if(SSC_BUILD_PLUGIN)
    add_subdirectory(external/JUCE)
    add_subdirectory(src)
endif()

# Tests --------------------------------------------------------------------------

if(SSC_BUILD_TESTS)
    add_subdirectory(external/Catch2)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/Catch2/extras")
    enable_testing()
    add_subdirectory(tests)
endif()

# Developer tools ----------------------------------------------------------------

if(SSC_BUILD_TOOLS AND SSC_BUILD_PLUGIN)
    add_subdirectory(tools)
endif()
