#pragma once

#include <fnd/Input.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

struct ANativeWindow;
struct AAssetManager;

namespace migi
{

// Process-wide state shared between the NativeActivity entry point (main.cpp),
// the Android WindowManager backend, and the Android asset loader. The
// NativeActivity thread populates it as the activity lifecycle progresses; the
// engine (running on a separate thread) reads it through the Window and Asset
// APIs.
struct AndroidPlatform
{
    ANativeWindow* window = nullptr;
    AAssetManager* assetManager = nullptr;
    std::atomic<uint64_t> closeEventIndex{0};
    std::atomic<int> width{0};
    std::atomic<int> height{0};

    // Window/surface lifecycle. The engine renders to a Vulkan surface backed by
    // `window`; when the activity is backgrounded (lock screen, task switch) the
    // OS destroys that window, so the engine must release the surface and pause
    // until a new window arrives, rather than terminating.
    //
    // Coordination between the NativeActivity thread (main.cpp) and the engine's
    // render thread:
    //   - windowValid: true between APP_CMD_INIT_WINDOW and APP_CMD_TERM_WINDOW.
    //   - windowQuit: the activity is actually being destroyed; the engine should
    //     exit its loop for good.
    //   - surfaceReleased handshake: APP_CMD_TERM_WINDOW must not return (and let
    //     the OS free the ANativeWindow) until the engine has destroyed the
    //     Vulkan surface; the engine signals NotifySurfaceReleased() once done.
    std::atomic<bool> windowValid{false};
    std::atomic<bool> windowQuit{false};
    std::mutex lifecycleMutex;
    std::condition_variable lifecycleCv;
    bool surfaceReleased = false;

    // NativeActivity thread: a new window is available (INIT_WINDOW).
    void OnWindowAvailable(ANativeWindow* newWindow)
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        window = newWindow;
        windowValid.store(true);
        lifecycleCv.notify_all();
    }

    // NativeActivity thread: the window is going away (TERM_WINDOW). Mark the
    // surface invalid and block until the engine has torn the surface down, so
    // the ANativeWindow stays alive until vkDestroySurfaceKHR has run.
    void OnWindowDestroyed()
    {
        std::unique_lock<std::mutex> lock(lifecycleMutex);
        windowValid.store(false);
        surfaceReleased = false;
        lifecycleCv.notify_all();
        lifecycleCv.wait(lock, [this] { return surfaceReleased || windowQuit.load(); });
        window = nullptr;
    }

    // NativeActivity thread: the activity is being destroyed for good.
    void RequestQuit()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        windowQuit.store(true);
        lifecycleCv.notify_all();
    }

    // Engine: the Vulkan surface has been destroyed; let TERM_WINDOW return.
    void NotifySurfaceReleased()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        surfaceReleased = true;
        lifecycleCv.notify_all();
    }

    // Engine: block until a window is available again (resume) or we must quit.
    void WaitUntilValidOrQuit()
    {
        std::unique_lock<std::mutex> lock(lifecycleMutex);
        lifecycleCv.wait(lock, [this] { return windowValid.load() || windowQuit.load(); });
    }

    // Touch input, synthesized as a single-pointer mouse. Dear ImGui has no
    // touch concept, so the primary touch pointer drives the mouse: a tap is a
    // left-button click and a drag moves the cursor while the button is held.
    // The NativeActivity input callback (main.cpp) produces states; the engine
    // consumes them on its render thread through WindowManager::GetMouseState.
    // History is a small ring buffer keyed by an ever-increasing stateIndex,
    // mirroring the Win32 backend so the platform-agnostic DearImGuiManager
    // replays the same event stream on both platforms.
    static constexpr size_t kMouseHistoryCapacity = 512;
    std::mutex inputMutex;
    MouseState mouse;
    uint64_t nextMouseStateIndex = 0;
    std::deque<MouseState> mouseHistory;

    // Append the current mouse snapshot to the history (caller holds nothing;
    // this takes inputMutex internally).
    void PushMouse(int x, int y, bool leftDown, bool inWindow);

    // Copy every mouse state newer than `lastStateIndex` into `states`
    // (newest first), returning the count written. Matches the Win32 backend.
    uint32_t ReadMouseStates(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount);
};

AndroidPlatform& AndroidPlatformState();

}
