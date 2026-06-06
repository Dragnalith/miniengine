#pragma once

// Platform fiber primitives backing the cooperative job scheduler. Win32 maps
// these directly to the fiber API; POSIX implements them with ucontext. The
// switch primitive itself is fnd::SwitchFiber, declared in <fnd/Profiler.h>.
//
// A "fiber handle" is an opaque void*. Threads that run jobs first promote
// themselves to a fiber with FiberConvertThread() and restore themselves with
// FiberConvertToThread() before exiting.

namespace fnd
{

// Promote the calling thread to a fiber and return its handle. The handle is
// the target to switch back to once a job fiber yields.
void* FiberConvertThread();

// Tear down the fiber created for the calling thread by FiberConvertThread().
void FiberConvertToThread();

// Create a job fiber that runs `entry(arg)` when first switched to. `entry`
// is expected never to return (the job loop switches back to the owning
// thread fiber instead).
void* FiberCreate(void (*entry)(void*), void* arg);

// Destroy a fiber previously returned by FiberCreate().
void FiberDelete(void* fiber);

} // namespace fnd
