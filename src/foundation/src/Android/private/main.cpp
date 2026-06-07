#include <fnd/MigiMain.h>
#include <fnd/Profiler.h>
#include <private/JobSystem.h>
#include <private/WindowManager.h>

#include "AndroidApp.h"

#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>

#include <jni.h>
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

    // Ask MigiMain to exit for good (the activity is being destroyed) and wait
    // for it to tear the RHI down. Safe to call more than once.
    void Shutdown()
    {
        if (started && !joined)
        {
            migi::AndroidPlatformState().RequestQuit();
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

// Hide the status and navigation bars so the activity runs fully immersive,
// the way games do. Apps targeting SDK 35+ are forced edge-to-edge and the
// legacy fullscreen flags are ignored, so the supported path is to drive the
// window's WindowInsetsController (API 30+) and, on older devices, fall back to
// the deprecated SYSTEM_UI_FLAG_* bits. The bars stay hidden (sticky) and only
// reappear transiently on an edge swipe, then auto-hide again.
//
// These calls touch the view hierarchy, so they must run on the UI thread.
// EnableImmersiveMode is only ever invoked from the onWindowFocusChanged
// callback below (which the framework dispatches on the UI thread); activity->env
// is the UI thread's JNIEnv there, so no thread attach is required.
void EnableImmersiveMode(ANativeActivity* activity)
{
    if (activity == nullptr || activity->env == nullptr || activity->clazz == nullptr)
        return;

    JNIEnv* env = activity->env;

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    const jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);

    jclass activityClass = env->GetObjectClass(activity->clazz);
    jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity->clazz, getWindow);
    jclass windowClass = env->GetObjectClass(window);
    jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
    jobject decorView = env->CallObjectMethod(window, getDecorView);
    jclass viewClass = env->GetObjectClass(decorView);

    if (sdkInt >= 30)
    {
        // window.setDecorFitsSystemWindows(false): opt the content into laying
        // out behind the (now-hidden) bars instead of being inset by them.
        jmethodID setDecorFits = env->GetMethodID(windowClass, "setDecorFitsSystemWindows", "(Z)V");
        env->CallVoidMethod(window, setDecorFits, JNI_FALSE);

        jmethodID getController =
            env->GetMethodID(viewClass, "getWindowInsetsController", "()Landroid/view/WindowInsetsController;");
        jobject controller = env->CallObjectMethod(decorView, getController);
        if (controller != nullptr)
        {
            jclass controllerClass = env->GetObjectClass(controller);

            jfieldID behaviorField =
                env->GetStaticFieldID(controllerClass, "BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE", "I");
            const jint behavior = env->GetStaticIntField(controllerClass, behaviorField);
            jmethodID setBehavior = env->GetMethodID(controllerClass, "setSystemBarsBehavior", "(I)V");
            env->CallVoidMethod(controller, setBehavior, behavior);

            jclass typeClass = env->FindClass("android/view/WindowInsets$Type");
            jmethodID systemBars = env->GetStaticMethodID(typeClass, "systemBars", "()I");
            const jint types = env->CallStaticIntMethod(typeClass, systemBars);
            jmethodID hide = env->GetMethodID(controllerClass, "hide", "(I)V");
            env->CallVoidMethod(controller, hide, types);
        }
    }
    else
    {
        // Pre-API-30 immersive sticky via the legacy decor-view flags.
        constexpr jint kLayoutStable = 0x00000100;
        constexpr jint kLayoutHideNavigation = 0x00000200;
        constexpr jint kLayoutFullscreen = 0x00000400;
        constexpr jint kHideNavigation = 0x00000002;
        constexpr jint kFullscreen = 0x00000004;
        constexpr jint kImmersiveSticky = 0x00001000;
        const jint flags = kLayoutStable | kLayoutHideNavigation | kLayoutFullscreen | kHideNavigation |
                           kFullscreen | kImmersiveSticky;
        jmethodID setSystemUiVisibility = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");
        env->CallVoidMethod(decorView, setSystemUiVisibility, flags);
    }

    // Never let a stray JNI exception ride back into the framework's call into
    // native (it would abort on the next JNI transition).
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

// The framework calls onWindowFocusChanged on the UI thread. android_native_app_glue
// installs its own handler (to post APP_CMD_GAINED_FOCUS); we chain it so the glue
// keeps working and re-assert immersive mode whenever the activity regains focus,
// which is also when the system has re-shown the bars after a transient swipe.
void (*g_glueOnWindowFocusChanged)(ANativeActivity*, int) = nullptr;

void OnWindowFocusChanged(ANativeActivity* activity, int hasFocus)
{
    if (g_glueOnWindowFocusChanged != nullptr)
        g_glueOnWindowFocusChanged(activity, hasFocus);
    if (hasFocus)
        EnableImmersiveMode(activity);
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
            // A window is available: on first launch this bootstraps the engine;
            // on a return from background it resumes a paused engine (which
            // recreates its swapchain against this new window).
            UpdateWindowMetrics(app->window);
            migi::AndroidPlatformState().OnWindowAvailable(app->window);
            g_windowReady = true;
        }
        break;

    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        UpdateWindowMetrics(app->window);
        break;

    case APP_CMD_TERM_WINDOW:
        // The window is being destroyed (backgrounded / locked). Tell the engine
        // to release its Vulkan surface and pause; block here (while the
        // ANativeWindow is still alive) until it confirms the surface is gone,
        // then let the system free the window. The engine itself keeps running.
        if (renderThread != nullptr && renderThread->started && !renderThread->joined)
            migi::AndroidPlatformState().OnWindowDestroyed();
        else
            migi::AndroidPlatformState().windowValid.store(false);
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

    // Chain the glue's focus callback so we can hide the system bars (immersive
    // mode) on the UI thread. Installed before the first focus event arrives.
    g_glueOnWindowFocusChanged = app->activity->callbacks->onWindowFocusChanged;
    app->activity->callbacks->onWindowFocusChanged = OnWindowFocusChanged;

    while (!g_windowReady && !app->destroyRequested)
        PumpEvents(app);

    if (app->destroyRequested)
        return;

    migi::WindowManager windowManager;
    migi::SetActiveWindowManager(&windowManager);
    renderThread.Start();

    // Keep pumping the activity event loop for the whole lifetime of the
    // activity. The engine no longer exits when the window is destroyed (it
    // pauses and releases its surface via APP_CMD_TERM_WINDOW); it only exits
    // when the activity is actually destroyed.
    while (!app->destroyRequested)
        PumpEvents(app);

    renderThread.Shutdown();
    migi::SetActiveWindowManager(nullptr);
}
