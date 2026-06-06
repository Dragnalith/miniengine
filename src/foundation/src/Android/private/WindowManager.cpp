#include <private/WindowManager.h>

#include "AndroidApp.h"

namespace migi
{

// The Android window is owned by the NativeActivity, not created by the engine,
// so the backend is a thin adapter over the shared AndroidPlatform state filled
// in by android_main. Touch input is synthesized as a single-pointer mouse by
// the NativeActivity input callback and surfaced here through GetMouseState;
// keyboard and text input are not wired up yet.
struct WindowManagerImpl
{
};

WindowManager::WindowManager()
{
}

WindowManager::~WindowManager()
{
}

void WindowManager::CreateMainWindow(const char*)
{
}

void WindowManager::DestroyMainWindow()
{
}

Int2 WindowManager::GetSize() const
{
    const AndroidPlatform& state = AndroidPlatformState();
    return { state.width.load(), state.height.load() };
}

uint64_t WindowManager::GetLastClosePressEventIndex() const
{
    return AndroidPlatformState().closeEventIndex.load();
}

uint32_t WindowManager::GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount) const
{
    return AndroidPlatformState().ReadMouseStates(lastStateIndex, states, maxStateCount);
}

uint32_t WindowManager::GetKeyboardState(uint64_t, KeyboardState*, uint32_t) const
{
    return 0;
}

uint64_t WindowManager::ReadTextStream(uint64_t firstIndex, wchar_t*, uint32_t) const
{
    return firstIndex;
}

void WindowManager::SetTitle(const char*)
{
}

void WindowManager::SetCursorShape(CursorShape)
{
}

void* WindowManager::GetNativeHandle() const
{
    return AndroidPlatformState().window;
}

}
