#include <fnd/Window.h>

#include <private/WindowManager.h>
#include <fnd/Util.h>

namespace
{
migi::WindowManager* g_windowManager = nullptr;
}

namespace migi
{

void SetActiveWindowManager(WindowManager* manager)
{
    g_windowManager = manager;
}

static WindowManager& ActiveWindowManager()
{
    MIGI_ASSERT(g_windowManager != nullptr, "Error: window manager not initialized");
    return *g_windowManager;
}

Int2 WindowGetSize()
{
    return ActiveWindowManager().GetSize();
}

uint64_t WindowGetLastClosePressEventIndex()
{
    return ActiveWindowManager().GetLastClosePressEventIndex();
}

uint32_t GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount)
{
    return ActiveWindowManager().GetMouseState(lastStateIndex, states, maxStateCount);
}

uint32_t GetKeyboardState(uint64_t lastStateIndex, KeyboardState* states, uint32_t maxStateCount)
{
    return ActiveWindowManager().GetKeyboardState(lastStateIndex, states, maxStateCount);
}

uint64_t ReadTextStream(uint64_t firstIndex, wchar_t* characters, uint32_t maxCharacterCount)
{
    return ActiveWindowManager().ReadTextStream(firstIndex, characters, maxCharacterCount);
}

void WindowSetCursorShape(CursorShape shape)
{
    ActiveWindowManager().SetCursorShape(shape);
}

void* WindowGetNativeHandle()
{
    return ActiveWindowManager().GetNativeHandle();
}

}
