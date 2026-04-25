#pragma once

#include <fnd/Input.h>
#include <fnd/Pimpl.h>
#include <fnd/Window.h>

namespace migi
{

struct WindowManagerImpl;

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    void CreateMainWindow(const char* name);
    void DestroyMainWindow();

    Int2 GetSize() const;
    uint64_t GetLastClosePressEventIndex() const;

    uint32_t GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount) const;
    uint32_t GetKeyboardState(uint64_t lastStateIndex, KeyboardState* states, uint32_t maxStateCount) const;
    uint64_t ReadTextStream(uint64_t firstIndex, wchar_t* characters, uint32_t maxCharacterCount) const;

    void SetCursorShape(CursorShape shape);
    void* GetNativeHandle() const;

private:
    Pimpl<WindowManagerImpl> m_impl;
};

void SetActiveWindowManager(WindowManager* manager);

}
