#include <fnd/MigiMain.h>
#include <fnd/Profiler.h>
#include <private/JobSystem.h>

#include <android_native_app_glue.h>

#include <pthread.h>

namespace fnd
{

void SetThreadName(const char* name)
{
    if (name != nullptr)
    {
        // bionic truncates names longer than 15 characters; ignore failures.
        pthread_setname_np(pthread_self(), name);
    }
}

bool IsProfilingEnabled()
{
    return false;
}

} // namespace fnd

// NativeActivity entry point, invoked by android_native_app_glue on a dedicated
// thread once the activity is created. Mirrors the desktop bootstrap: run the
// engine entry point inside the job system so job-dispatching code works.
// Returning tears the activity back down.
extern "C" void android_main(struct android_app* app)
{
    (void)app;
    migi::JobSystem::Start([] {
        MigiMain();
    });
}
