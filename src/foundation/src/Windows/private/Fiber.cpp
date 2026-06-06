#include <private/Fiber.h>
#include <fnd/Profiler.h>

#include <windows.h>

namespace fnd
{

void SwitchFiber(void* fiber, const char*)
{
    ::SwitchToFiber(fiber);
}

void* FiberConvertThread()
{
    return ::ConvertThreadToFiber(nullptr);
}

void FiberConvertToThread()
{
    ::ConvertFiberToThread();
}

void* FiberCreate(void (*entry)(void*), void* arg)
{
    return ::CreateFiber(64 * 1024, reinterpret_cast<LPFIBER_START_ROUTINE>(entry), arg);
}

void FiberDelete(void* fiber)
{
    ::DeleteFiber(fiber);
}

} // namespace fnd
