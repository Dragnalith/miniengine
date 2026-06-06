#include <fnd/MigiMain.h>
#include <fnd/Profiler.h>

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
// thread once the activity is created. Non-interactive examples just run the
// engine entry point and return; returning tears the activity back down.
extern "C" void android_main(struct android_app* app)
{
    (void)app;
    MigiMain();
}
