# Shared compiler settings for Sam Spectral Control targets.

add_library(ssc_options INTERFACE)

if(MSVC)
    # C4324: padding added by alignas() (intended, e.g. cache-line separation in lock-free queues).
    target_compile_options(ssc_options INTERFACE /W4 /permissive- /utf-8 /wd4324)
    target_compile_definitions(ssc_options INTERFACE _USE_MATH_DEFINES NOMINMAX)
else()
    target_compile_options(ssc_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference)
endif()

if(SSC_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(ssc_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ssc_options INTERFACE -fsanitize=address,undefined)
endif()

if(SSC_ENABLE_RTSAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ssc_options INTERFACE -fsanitize=realtime)
    target_link_options(ssc_options INTERFACE -fsanitize=realtime)
    target_compile_definitions(ssc_options INTERFACE SSC_RTSAN=1)
endif()

# Warnings as errors (NFR-4) for the project's own sources only, so third-party code (JUCE modules,
# pffft, Catch2) cannot break the build. Enabled in CI.
if(MSVC)
    set(SSC_WERROR_FLAG /WX)
else()
    set(SSC_WERROR_FLAG -Werror)
endif()

function(ssc_enable_werror target)
    if(SSC_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE ${SSC_WERROR_FLAG})
    endif()
endfunction()

function(ssc_enable_werror_for_sources)
    if(SSC_WARNINGS_AS_ERRORS)
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "${SSC_WERROR_FLAG}")
    endif()
endfunction()
