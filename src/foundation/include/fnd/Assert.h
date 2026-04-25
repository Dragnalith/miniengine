#pragma once

#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace migi
{
namespace detail
{

[[noreturn]] inline void Abort(const char* file, int line, const char* message)
{
    std::fprintf(stderr, "MIGI_ABORT: %s:%d: %s\n", file, line, message != nullptr ? message : "");
    std::fflush(stderr);
#if defined(_MSC_VER)
    __debugbreak();
#endif
    std::abort();
}

inline void Assert(bool condition, const char* conditionText, const char* file, int line, const char* message)
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "MIGI_ASSERT: %s:%d: %s: %s\n",
            file,
            line,
            conditionText != nullptr ? conditionText : "",
            message != nullptr ? message : "");
        std::fflush(stderr);
        Abort(file, line, "assert failed");
    }
}

}
}

#define MIGI_ABORT(message) ::migi::detail::Abort(__FILE__, __LINE__, (message))
#define MIGI_ASSERT(condition, message) ::migi::detail::Assert(static_cast<bool>(condition), #condition, __FILE__, __LINE__, (message))
