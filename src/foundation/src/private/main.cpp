#include <fnd/AppMain.h>
#include <fnd/Util.h>
#include <fnd/Window.h>
#include <fnd/Job.h>
#include <private/JobSystem.h>

#include <private/WindowManager.h>

#include <fnd/Profiler.h>
#include <windows.h>
#include <iostream>

namespace
{
engine::Window* g_mainWindow = nullptr;
engine::WindowManager* g_windowManager = nullptr;
}

namespace engine
{

Window& Window::GetMainWindow()
{
    ASSERT_MSG(g_mainWindow != nullptr, "Error: window manager not initialized");
    return *g_mainWindow;
}
void Window::SetCursor(void* cursor)
{
    ASSERT_MSG(g_windowManager != nullptr, "Error: window manager not initialized");
    g_windowManager->SetCursor(cursor);
}

}

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
}

int main(int argc, char** argv) {

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    PROFILE_SET_THREADNAME("The Main Thread");

#if 0
    engine::JobSystem::Start([]() {
        std::cout << "Start\n";
        engine::JobCounter handle;
        for (int i = 0; i < 10; i++) {
            engine::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job1\n";
            });
        }
        engine::Job::Wait(handle);
        for (int i = 0; i < 10; i++) {
            engine::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job2\n";
                });
        }
        engine::Job::Wait(handle);
        for (int i = 0; i < 10; i++) {
            engine::Job::Dispatch("Some Job", handle, [] {
                std::cout << "Job3\n";
                });
        }
        engine::Job::Wait(handle);
    });
    return 0;
    engine::JobSystem::Start([]() {
        std::cout << "Hello World\n";
        engine::JobCounter handle;
        for (int i = 0; i < 10; i++) {
            engine::Job::Dispatch("Some Job", handle, [i] {
                std::cout << "Some Job: " << i << "\n";
                });
        }
        engine::Job::Wait(handle);
        std::cout << "Good Night World\n";
        engine::JobCounter counter1;
        engine::JobCounter counter2;
        engine::Job::Dispatch("Some Job1", handle, [&counter1, &counter2] {
            std::cout << "Some Job1: 1\n";
            counter1.Set(1);
            engine::Job::Wait(counter2, 1);
            std::cout << "Some Job1: 2\n";
            counter1.Set(2);
            engine::Job::Wait(counter2, 2);
            std::cout << "Some Job1: 3\n";
            counter1.Set(3);
            engine::Job::Wait(counter2, 3);
            std::cout << "Some Job1: 4\n";
            counter1.Set(4);
        });            
        engine::Job::Dispatch("Some Job2", handle, [&counter2, &counter1] {
            engine::Job::Wait(counter1, 1);
            std::cout << "Some Job2: 1\n";
            counter2.Set(1);
            engine::Job::Wait(counter1, 2);
            std::cout << "Some Job2: 2\n";
            counter2.Set(2);
            engine::Job::Wait(counter1, 3);
            std::cout << "Some Job2: 3\n";
            counter2.Set(3);
            engine::Job::Wait(counter1, 4);
            std::cout << "Some Job2: 4\n";
        });
        engine::Job::Wait(handle);
    });
    return 0;
#endif

    engine::WindowManager windowManager;
    g_windowManager = &windowManager;
    g_mainWindow = &windowManager.Create("Fiber Game");
    engine::JobSystem::Start([]{
        AppMain();
    });


    return 0;
}