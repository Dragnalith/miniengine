#include <fnd/MigiMain.h>
#include <fnd/Util.h>
#include <fnd/Window.h>
#include <fnd/Job.h>
#include <private/JobSystem.h>

#include <private/WindowManager.h>

#include <fnd/Profiler.h>
#include <windows.h>
#include <iostream>

namespace fnd
{

void SetThreadName(const char* name) {
wchar_t thread_name[64];
size_t result;
mbstowcs_s(&result, thread_name, std::size(thread_name), name, std::size(thread_name) - 1);
::SetThreadDescription(::GetCurrentThread(), thread_name);
}

bool IsProfilingEnabled() {
    return false;
}

void SwitchFiber(void* fiber, const char*) {
    ::SwitchToFiber(fiber);
}
}

int main(int argc, char** argv) {

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    PROFILE_SET_THREADNAME("The Main Thread");

#if 0
    migi::JobSystem::Start([]() {
        std::cout << "Start\n";
        migi::JobCounter handle;
        for (int i = 0; i < 10; i++) {
            migi::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job1\n";
            });
        }
        migi::Job::Wait(handle);
        for (int i = 0; i < 10; i++) {
            migi::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job2\n";
                });
        }
        migi::Job::Wait(handle);
        for (int i = 0; i < 10; i++) {
            migi::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job3\n";
                });
        }
        migi::Job::Wait(handle);
    });
    return 0;
    migi::JobSystem::Start([]() {
        std::cout << "Hello World\n";
        migi::JobCounter handle;
        for (int i = 0; i < 10; i++) {
            migi::Job::Dispatch("Some Job", handle, [i] {
                std::cout << "Some Job: " << i << "\n";
                });
        }
        migi::Job::Wait(handle);
        std::cout << "Good Night World\n";
        migi::JobCounter counter1;
        migi::JobCounter counter2;
        migi::Job::Dispatch("Some Job1", handle, [&counter1, &counter2] {
            std::cout << "Some Job1: 1\n";
            counter1.Set(1);
            migi::Job::Wait(counter2, 1);
            std::cout << "Some Job1: 2\n";
            counter1.Set(2);
            migi::Job::Wait(counter2, 2);
            std::cout << "Some Job1: 3\n";
            counter1.Set(3);
            migi::Job::Wait(counter2, 3);
            std::cout << "Some Job1: 4\n";
            counter1.Set(4);
        });            
        migi::Job::Dispatch("Some Job2", handle, [&counter2, &counter1] {
            migi::Job::Wait(counter1, 1);
            std::cout << "Some Job2: 1\n";
            counter2.Set(1);
            migi::Job::Wait(counter1, 2);
            std::cout << "Some Job2: 2\n";
            counter2.Set(2);
            migi::Job::Wait(counter1, 3);
            std::cout << "Some Job2: 3\n";
            counter2.Set(3);
            migi::Job::Wait(counter1, 4);
            std::cout << "Some Job2: 4\n";
        });
        migi::Job::Wait(handle);
    });
    return 0;
#endif

    migi::WindowManager windowManager;
    migi::SetActiveWindowManager(&windowManager);
    windowManager.CreateMainWindow("Fiber Game");
    migi::JobSystem::Start([]{
        MigiMain();
    });
    migi::SetActiveWindowManager(nullptr);


    return 0;
}
