#pragma once

/**
    SSC_REALTIME marks the real-time entry points (the audio callback and Engine::process). In the
    RealtimeSanitizer build (SSC_ENABLE_RTSAN, Clang 20+) every allocation, lock or other blocking
    call made while one of them runs is reported and stops the program (ADR-0015, TST-4).
    It expands to nothing otherwise.
*/
#if defined(SSC_RTSAN)
#define SSC_REALTIME [[clang::nonblocking]]
#else
#define SSC_REALTIME
#endif
