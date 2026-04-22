#pragma once

#ifndef _AMD64_
#define _AMD64_
#endif
#include <windef.h>
#include <stdint.h>
#include <mutex>
#include <vector>

#include <fnd/SpinLock.h>
#include <fnd/Util.h>

namespace engine
{

struct WindowMsg {
    WindowMsg(HWND h, UINT m, WPARAM w, LPARAM l) : hWnd(h), msg(m), wParam(w), lParam(l) {}
    HWND hWnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
};

class Window
{
public:
    static Window& GetMainWindow();
    static void SetCursor(void* cursor);

    Window(HWND hwnd, int w, int h);

    auto GetHandle() const { return m_hwnd; };
    void GetSize(int* w, int*h) const;
    Clock::time_point GetLastQuitTime() const;

    std::vector<WindowMsg> PopMsg();


protected:
    mutable SpinLock m_lock;
    HWND m_hwnd;
    int m_w = 0;
    int m_h = 0;
    Clock::time_point m_lastQuitTime = Clock::now();

    std::vector<WindowMsg> m_messageStack;
};

}