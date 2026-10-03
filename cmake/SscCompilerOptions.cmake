# Shared compiler settings for Sam Spectral Control targets.

add_library(ssc_options INTERFACE)

if(MSVC)
    target_compile_options(ssc_options INTERFACE /W4 /permissive- /utf-8)
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
