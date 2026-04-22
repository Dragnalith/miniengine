#pragma once

#include <fnd/Util.h>

namespace engine
{

class Window;
struct WindowManagerImpl;

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();
    Window& Create(const char* name);
    void Destroy(Window& window);
    void SetCursor(void* cursor);

private:
    Pimpl<WindowManagerImpl> m_impl;
};

}