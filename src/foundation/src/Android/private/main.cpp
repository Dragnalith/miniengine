#include <fnd/MigiMain.h>
#include <fnd/Profiler.h>
#include <private/JobSystem.h>
#include <private/WindowManager.h>

#include "AndroidApp.h"

#include <android_native_app_glue.h>
#include <android/input.h>
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

void AndroidPlatform::PushMouse(int x, int y, bool leftDown, bool inWindow)
{
    std::scoped_lock<std::mutex> lock(inputMutex);
    mouse.x = x;
    mouse.y = y;
    mouse.inWindow = inWindow;
    mouse.buttonsDown[static_cast<size_t>(MouseButton::Left)] = leftDown;
    mouse.stateIndex = ++nextMouseStateIndex;
    mouseHistory.push_back(mouse);
    while (mouseHistory.size() > kMouseHistoryCapacity)
        mouseHistory.pop_front();
}

uint32_t AndroidPlatform::ReadMouseStates(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount)
{
    if (states == nullptr || maxStateCount == 0)
        return 0;

    std::scoped_lock<std::mutex> lock(inputMutex);
    uint32_t copied = 0;
    for (auto it = mouseHistory.rbegin(); it != mouseHistory.rend() && copied < maxStateCount; ++it)
    {
        if (it->stateIndex <= lastStateIndex)
            break;
        states[copied] = *it;
        ++copied;
    }
    return copied;
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

// Translate the primary touch pointer into the single-pointer mouse model Dear
// ImGui understands. The position is pushed in the same state as the button
// change so the engine emits the move-then-click ordering ImGui expects (touch
// has no hover phase to establish the cursor position before a press). On
// release the pointer is also moved out of the window so no widget keeps a
// stale hover after the finger lifts.
int32_t OnInputEvent(android_app*, AInputEvent* event)
{
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return 0;

    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const int x = static_cast<int>(AMotionEvent_getX(event, 0));
    const int y = static_cast<int>(AMotionEvent_getY(event, 0));
    migi::AndroidPlatform& state = migi::AndroidPlatformState();

    switch (action)
    {
    case AMOTION_EVENT_ACTION_DOWN:
        state.PushMouse(x, y, true, true);
        return 1;

    case AMOTION_EVENT_ACTION_MOVE:
        state.PushMouse(x, y, true, true);
        return 1;

    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
        state.PushMouse(x, y, false, true);
        state.PushMouse(x, y, false, false);
        return 1;

    default:
        return 0;
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
    app->onInputEvent = OnInputEvent;
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
