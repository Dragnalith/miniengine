#include <fnd/Window.h>
#include <tchar.h>

namespace engine
{

Window::Window(HWND hwnd, int w, int h)
    : m_hwnd(hwnd)
    , m_w(w)
    , m_h(h)
{
};

void Window::GetSize(int* w, int*h) const {
    std::scoped_lock scoped(m_lock);
    *w = m_w;
    *h = m_h;
}

Clock::time_point Window::GetLastQuitTime() const {
    std::scoped_lock scoped(m_lock);
    return m_lastQuitTime;
}
std::vector<WindowMsg> Window::PopMsg() {
    std::vector<WindowMsg> result;
    {
        std::scoped_lock scoped(m_lock);
        m_messageStack.swap(result);
    }
    return result;
}

}

