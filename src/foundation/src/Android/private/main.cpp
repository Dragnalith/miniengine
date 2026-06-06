#include <fnd/MigiMain.h>
#include <fnd/Profiler.h>
#include <private/JobSystem.h>
#include <private/WindowManager.h>

#include "AndroidApp.h"

#include <android_native_app_glue.h>
#include <android/native_window.h>

#include <pthread.h>

#include <thread>

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

namespace migi
{

AndroidPlatform& AndroidPlatformState()
{
    static AndroidPlatform state;
    return state;
}

} // namespace migi

namespace
{

// Owns the thread the engine entry point runs on. MigiMain renders on this
// thread while the NativeActivity thread keeps draining the event loop, the
// same split as the desktop bootstrap (Win32 message pump on its own thread,
// MigiMain inside the job system).
struct RenderThread
{
    std::thread thread;
    bool started = false;
    bool joined = false;

    void Start()
    {
        thread = std::thread([] {
            migi::JobSystem::Start([] { MigiMain(); });
        });
        started = true;
    }

    // Ask MigiMain to exit (bump the close-press event the engine loops on) and
    // wait for it to tear the RHI down. Safe to call more than once. Called
    // while the ANativeWindow is still valid so the swapchain is destroyed
    // before the window is released.
    void Shutdown()
    {
        if (started && !joined)
        {
            migi::AndroidPlatformState().closeEventIndex.fetch_add(1);
            thread.join();
            joined = true;
        }
    }
};

void UpdateWindowMetrics(ANativeWindow* window)
{
    if (window == nullptr)
        return;
    migi::AndroidPlatformState().width.store(ANativeWindow_getWidth(window));
    migi::AndroidPlatformState().height.store(ANativeWindow_getHeight(window));
}

bool g_windowReady = false;

void OnAppCmd(android_app* app, int32_t cmd)
{
    auto* renderThread = static_cast<RenderThread*>(app->userData);
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        if (app->window != nullptr)
        {
            migi::AndroidPlatformState().window = app->window;
            UpdateWindowMetrics(app->window);
            g_windowReady = true;
        }
        break;

    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        UpdateWindowMetrics(app->window);
        break;

    case APP_CMD_TERM_WINDOW:
        // The window is being destroyed: shut the engine down here, while the
        // ANativeWindow is still alive, so the Vulkan surface is released
        // before the system frees the window.
        if (renderThread != nullptr)
            renderThread->Shutdown();
        break;

    default:
        break;
    }
}

void PumpEvents(android_app* app)
{
    int events = 0;
    android_poll_source* source = nullptr;
    while (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
    {
        if (source != nullptr)
            source->process(app, source);
        if (app->destroyRequested || g_windowReady)
            return;
    }
}

} // namespace

// NativeActivity entry point, invoked by android_native_app_glue on a dedicated
// thread once the activity is created. The window is delivered asynchronously,
// so wait for it before bringing up the engine on a render thread, then keep
// pumping the event loop until the activity is torn down.
extern "C" void android_main(struct android_app* app)
{
    RenderThread renderThread;
    app->userData = &renderThread;
    app->onAppCmd = OnAppCmd;
    migi::AndroidPlatformState().assetManager = app->activity->assetManager;

    while (!g_windowReady && !app->destroyRequested)
        PumpEvents(app);

    if (app->destroyRequested)
        return;

    migi::WindowManager windowManager;
    migi::SetActiveWindowManager(&windowManager);
    renderThread.Start();

    while (!app->destroyRequested && !renderThread.joined)
        PumpEvents(app);

    renderThread.Shutdown();
    migi::SetActiveWindowManager(nullptr);
}
