#pragma once

#include <cstdio>

namespace migi
{

inline void Log(const char* level, const char* message)
{
    std::fprintf(stdout, "[%s] %s\n", level != nullptr ? level : "LOG", message != nullptr ? message : "");
    std::fflush(stdout);
}

}

#define MIGI_LOG_INFO(message) ::migi::Log("info", (message))
#define MIGI_LOG_WARNING(message) ::migi::Log("warning", (message))
#define MINI_LOG_ERROR(message) ::migi::Log("error", (message))
