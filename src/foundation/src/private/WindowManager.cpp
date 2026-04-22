#include <private/WindowManager.h>

#include <iostream>

#include <fnd/Util.h>
#include <fnd/Window.h>
#include <fnd/Profiler.h>

#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM(), GET_Y_LPARAM()

#include <vector>
#include <functional>
#include <future>

namespace engine
{


struct WindowManagerImpl {
    class WindowWin64 : public Window {
    public:
        WindowWin64(HWND hwnd, int w, int h) : Window(hwnd, w, h) {}

        LRESULT WINAPI WndProcImpl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            {
                std::scoped_lock scope(m_lock);
                m_messageStack.emplace_back(hWnd, msg, wParam, lParam);
            }
            switch (msg)
            {
            case WM_MOUSEMOVE:
                // We need to call TrackMouseEvent in order to receive WM_MOUSELEAVE events
                if (!m_isMouseTracked)
                {
                    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                    ::TrackMouseEvent(&tme);
                    m_isMouseTracked = true;
                }
                break;
            case WM_MOUSELEAVE:
                m_isMouseTracked = false;
                break;
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
            {
                ::SetCapture(hWnd);
                return 0;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP:
            {
                ::ReleaseCapture();
                return 0;
            }
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED)
                {
                    std::scoped_lock scope(m_lock);
                    m_w = static_cast<int>(LOWORD(lParam));
                    m_h = static_cast<int>(HIWORD(lParam));
                }
                return 0;
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
                // prevent default behavior of system key, like ALT+ENTER
                return 0;
            case WM_SYSCOMMAND:
                if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                    return 0;
                break;
            case WM_DESTROY:
            {
                return 0;
            }
            case WM_CLOSE:
                {
                    std::scoped_lock scope(m_lock);
                    m_lastQuitTime = Clock::now();
                    return 0;
                }
            }

            return ::DefWindowProcA(hWnd, msg, wParam, lParam);
        }

    private:
        bool m_isMouseTracked = false;
    };

    auto FindWindow(HWND hwnd) {
        for (auto it = windows.begin(); it != windows.end(); it++) {
            if ((*it)->GetHandle() == hwnd) {
                return it;
            }
        }

        return windows.end();
    }

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LONG_PTR pLong = ::GetWindowLongPtrA(hWnd, GWLP_USERDATA);
        auto* managerImpl = reinterpret_cast<WindowManagerImpl*>(pLong);

        if (managerImpl != nullptr ) {

            auto iter = managerImpl->FindWindow(hWnd);

            if (iter != managerImpl->windows.end()) {

                if (msg == WM_SETCURSOR) {
                    if (LOWORD(lParam) == HTCLIENT) {
                        ::SetCursor(managerImpl->cursor);
                        return 0;
                    }
                    else {
                        return ::DefWindowProcA(hWnd, msg, wParam, lParam);
                    }
                }

                return (*iter)->WndProcImpl(hWnd, msg, wParam, lParam);
            }
            else {
                return ::DefWindowProcA(hWnd, msg, wParam, lParam);
            }
        }
        else {
            return ::DefWindowProcA(hWnd, msg, wParam, lParam);
        }

    }

    Window* Create(const char* windowName) {
        int w = 1280;
        int h = 800;

        HWND hwnd = ::CreateWindowExA(0, wc.lpszClassName, windowName, WS_OVERLAPPEDWINDOW, 100, 100, w, h, NULL, NULL, wc.hInstance, NULL);
        ::ShowWindow(hwnd, SW_SHOWDEFAULT);
        //::UpdateWindow(hwnd);

        ::SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        return windows.emplace_back(std::make_unique<WindowManagerImpl::WindowWin64>(hwnd, w, h)).get();
    }

    void Destroy(Window* window)
    {
        auto iter = FindWindow(window->GetHandle());

        ASSERT_MSG(iter != windows.end(), "The Window has not been found");

        ::DestroyWindow((*iter)->GetHandle());
        windows.erase(iter);
    }

    void ThreadFunc() {
        PROFILE_SET_THREADNAME("Window Manager");
        // Create application window
        ::RegisterClassExA(&wc);

        auto next = Clock::now() + std::chrono::milliseconds(4);
        while (!taskQueue.IsClosed()) {
            for (auto& task : taskQueue.PopAll()) {
                task();
            }
            MSG msg;
            while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);

            }
            if (!taskQueue.WaitUntil(next)) {
                next += std::chrono::milliseconds(4);
            }
        }

        for (auto& w : windows) {
            Destroy(w.get());
        }
        ::UnregisterClassA(wc.lpszClassName, wc.hInstance);

        ASSERT_MSG(windows.size() == 0, "All the window has not been destroyed");
    }

    WindowManagerImpl() 
        : wc({ sizeof(WNDCLASSEXA), CS_CLASSDC, WindowManagerImpl::WndProc, 0L, 0L, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "app's Window Class", NULL })
        , thread([this] { ThreadFunc(); })
    {

    }

    ~WindowManagerImpl()
    {
        taskQueue.Close();
    }

    bool m_isMouseTracked = false;
    HCURSOR cursor;
    WNDCLASSEXA wc;
    std::vector<std::unique_ptr<WindowWin64>> windows;
    TaskQueue<std::function<void()>> taskQueue;
    std::jthread thread;
};


WindowManager::WindowManager()
{

}


WindowManager::~WindowManager()
{

}

Window& WindowManager::Create(const char* windowName) {
    std::promise<Window*> promise;
    m_impl->taskQueue.Push([&] {
        promise.set_value(m_impl->Create(windowName));
    });

    return *promise.get_future().get();
}

void WindowManager::Destroy(Window& window) {
    std::promise<void> promise;
    m_impl->taskQueue.Push([&] {
        m_impl->Destroy(&window);
        promise.set_value();
    });

    promise.get_future().wait();

}

void WindowManager::SetCursor(void* cursor) {
    std::promise<void> promise;
    m_impl->taskQueue.Push([this, cursor] {
        m_impl->cursor = static_cast<HCURSOR>(cursor);
        return ::SetCursor(m_impl->cursor);
    });
}

}
