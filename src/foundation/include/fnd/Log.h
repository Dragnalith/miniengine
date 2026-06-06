#pragma once

#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace migi
{

inline void Log(const char* level, const char* message)
{
#if defined(__ANDROID__)
    // Desktop stdout is not visible on Android, so route logs to logcat.
    __android_log_print(
        ANDROID_LOG_INFO,
        "miniengine",
        "[%s] %s",
        level != nullptr ? level : "LOG",
        message != nullptr ? message : "");
#else
    std::fprintf(stdout, "[%s] %s\n", level != nullptr ? level : "LOG", message != nullptr ? message : "");
    std::fflush(stdout);
#endif
}

}

#define MIGI_LOG_INFO(message) ::migi::Log("info", (message))
#define MIGI_LOG_WARNING(message) ::migi::Log("warning", (message))
#define MINI_LOG_ERROR(message) ::migi::Log("error", (message))
