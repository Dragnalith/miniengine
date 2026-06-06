#pragma once

#include <fnd/Input.h>

#include <atomic>
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
