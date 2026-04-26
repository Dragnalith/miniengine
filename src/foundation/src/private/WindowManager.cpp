#include <private/WindowManager.h>

#include <fnd/Profiler.h>
#include <fnd/SpinLock.h>
#include <fnd/TaskQueue.h>
#include <fnd/Util.h>

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <deque>
#include <future>
#include <string>
#include <thread>

namespace migi
{

namespace
{
constexpr size_t kInputHistoryCapacity = 512;
constexpr size_t kTextInputCapacity = 512;

struct TextInputCharacter
{
    uint64_t index = 0;
    wchar_t character = 0;
};

uint32_t MouseButtonIndex(UINT message, WPARAM wParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
        return static_cast<uint32_t>(MouseButton::Left);
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
        return static_cast<uint32_t>(MouseButton::Right);
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
        return static_cast<uint32_t>(MouseButton::Middle);
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
        return GET_XBUTTON_WPARAM(wParam) == XBUTTON1
            ? static_cast<uint32_t>(MouseButton::X1)
            : static_cast<uint32_t>(MouseButton::X2);
    default:
        return kMouseButtonCount;
    }
}

HCURSOR LoadCursorForShape(CursorShape shape)
{
    switch (shape)
    {
    case CursorShape::None:       return nullptr;
    case CursorShape::Arrow:      return ::LoadCursor(nullptr, IDC_ARROW);
    case CursorShape::TextInput:  return ::LoadCursor(nullptr, IDC_IBEAM);
    case CursorShape::ResizeAll:  return ::LoadCursor(nullptr, IDC_SIZEALL);
    case CursorShape::ResizeEW:   return ::LoadCursor(nullptr, IDC_SIZEWE);
    case CursorShape::ResizeNS:   return ::LoadCursor(nullptr, IDC_SIZENS);
    case CursorShape::ResizeNESW: return ::LoadCursor(nullptr, IDC_SIZENESW);
    case CursorShape::ResizeNWSE: return ::LoadCursor(nullptr, IDC_SIZENWSE);
    case CursorShape::Hand:       return ::LoadCursor(nullptr, IDC_HAND);
    case CursorShape::NotAllowed: return ::LoadCursor(nullptr, IDC_NO);
    }
    return ::LoadCursor(nullptr, IDC_ARROW);
}

template<typename T>
uint32_t CopyStatesNewerThan(const std::deque<T>& history, uint64_t lastStateIndex, T* states, uint32_t maxStateCount)
{
    if (states == nullptr || maxStateCount == 0)
        return 0;

    uint32_t copied = 0;
    for (auto it = history.rbegin(); it != history.rend() && copied < maxStateCount; ++it)
    {
        if (it->stateIndex <= lastStateIndex)
            break;

        states[copied] = *it;
        ++copied;
    }
    return copied;
}
}

struct WindowManagerImpl
{
    WindowManagerImpl()
        : wc({
            sizeof(WNDCLASSEXA),
            CS_CLASSDC,
            WindowManagerImpl::WndProc,
            0L,
            0L,
            GetModuleHandleA(nullptr),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            "app's Window Class",
            nullptr,
        })
        , thread([this] { ThreadFunc(); })
    {
    }

    ~WindowManagerImpl()
    {
        taskQueue.Close();
    }

    template<typename T>
    static void TrimHistory(std::deque<T>& history)
    {
        while (history.size() > kInputHistoryCapacity)
            history.pop_front();
    }

    void PushMouseState()
    {
        mouse.stateIndex = ++nextMouseStateIndex;
        mouseHistory.push_back(mouse);
        TrimHistory(mouseHistory);
    }

    void PushKeyboardState()
    {
        keyboard.stateIndex = ++nextKeyboardStateIndex;
        keyboardHistory.push_back(keyboard);
        TrimHistory(keyboardHistory);
    }

    void SetMousePosition(int x, int y, bool inWindow)
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        if (mouse.x == x && mouse.y == y && mouse.inWindow == inWindow)
            return;

        mouse.x = x;
        mouse.y = y;
        mouse.inWindow = inWindow;
        PushMouseState();
    }

    void SetMouseInWindow(bool value)
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        if (mouse.inWindow == value)
            return;

        mouse.inWindow = value;
        PushMouseState();
    }

    void SetMouseButton(uint32_t button, bool down, int x, int y)
    {
        if (button >= kMouseButtonCount)
            return;

        std::scoped_lock<SpinLock> lock(stateLock);
        bool changed = false;
        if (mouse.x != x || mouse.y != y || !mouse.inWindow)
        {
            mouse.x = x;
            mouse.y = y;
            mouse.inWindow = true;
            changed = true;
        }
        if (mouse.buttonsDown[button] != down)
        {
            mouse.buttonsDown[button] = down;
            changed = true;
        }
        if (changed)
            PushMouseState();
    }

    void AddMouseWheel(int wheelDelta, int horizontalWheelDelta)
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        mouse.wheel += wheelDelta;
        mouse.horizontalWheel += horizontalWheelDelta;
        PushMouseState();
    }

    void SetKey(uint32_t key, bool down)
    {
        if (key >= kKeyboardKeyCount)
            return;

        std::scoped_lock<SpinLock> lock(stateLock);
        if (keyboard.keysDown[key] == down)
            return;

        keyboard.keysDown[key] = down;
        PushKeyboardState();
    }

    void ClearKeyboard()
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        bool changed = false;
        for (bool& key : keyboard.keysDown)
        {
            changed = changed || key;
            key = false;
        }
        if (changed)
            PushKeyboardState();
    }

    void AddTextInput(wchar_t character)
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        textInput[nextTextInputIndex % kTextInputCapacity] = { nextTextInputIndex, character };
        ++nextTextInputIndex;
    }

    void PushClosePressEvent()
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        ++lastClosePressEventIndex;
    }

    void Create(const char* windowName)
    {
        MIGI_ASSERT(hwnd == nullptr, "Only one window is supported");

        const int w = 1280;
        const int h = 800;

        hwnd = ::CreateWindowExA(
            0,
            wc.lpszClassName,
            windowName,
            WS_OVERLAPPEDWINDOW,
            100,
            100,
            w,
            h,
            nullptr,
            nullptr,
            wc.hInstance,
            nullptr);
        MIGI_ASSERT(hwnd != nullptr, "Window creation failed");

        ::SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ::ShowWindow(hwnd, SW_SHOWDEFAULT);

    }

    void Destroy()
    {
        if (hwnd == nullptr)
            return;

        HWND oldHwnd = hwnd;
        hwnd = nullptr;
        ::DestroyWindow(oldHwnd);
    }

    void ThreadFunc()
    {
        PROFILE_SET_THREADNAME("Window Manager");

        ::RegisterClassExA(&wc);

        TimePoint nextPoll = TimePoint::Now() + TimeSpan::FromMilliseconds(5);
        while (!taskQueue.IsClosed())
        {
            for (auto& task : taskQueue.PopAll())
                task();

            MSG msg{};
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }

            if (!taskQueue.WaitUntil(nextPoll))
                nextPoll = TimePoint::Now() + TimeSpan::FromMilliseconds(5);
            else
                nextPoll = TimePoint::Now();
        }

        Destroy();
        ::UnregisterClassA(wc.lpszClassName, wc.hInstance);
    }

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* impl = reinterpret_cast<WindowManagerImpl*>(::GetWindowLongPtrA(hWnd, GWLP_USERDATA));
        if (impl == nullptr)
            return ::DefWindowProcA(hWnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                ::SetCursor(impl->cursor);
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (!impl->mouseTracked)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                ::TrackMouseEvent(&tme);
                impl->mouseTracked = true;
            }
            impl->SetMousePosition(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
            return 0;

        case WM_MOUSELEAVE:
            impl->mouseTracked = false;
            impl->SetMouseInWindow(false);
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            ::SetCapture(hWnd);
            impl->SetMouseButton(MouseButtonIndex(msg, wParam), true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            ::ReleaseCapture();
            impl->SetMouseButton(MouseButtonIndex(msg, wParam), false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEWHEEL:
            impl->AddMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), 0);
            return 0;

        case WM_MOUSEHWHEEL:
            impl->AddMouseWheel(0, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            impl->SetKey(static_cast<uint32_t>(wParam), true);
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            impl->SetKey(static_cast<uint32_t>(wParam), false);
            return 0;

        case WM_KILLFOCUS:
            impl->ClearKeyboard();
            return 0;

        case WM_CHAR:
            if (wParam > 0 && wParam < 0x10000)
                impl->AddTextInput(static_cast<wchar_t>(wParam));
            return 0;

        case WM_SIZE:
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;

        case WM_CLOSE:
            impl->PushClosePressEvent();
            return 0;

        case WM_DESTROY:
            return 0;
        }

        return ::DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    Int2 GetSize() const
    {
        RECT rect{};
        HWND localHwnd = hwnd;
        if (localHwnd != nullptr)
            ::GetClientRect(localHwnd, &rect);

        return {
            static_cast<int>(rect.right - rect.left),
            static_cast<int>(rect.bottom - rect.top),
        };
    }

    uint64_t GetLastClosePressEventIndex() const
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        return lastClosePressEventIndex;
    }

    uint32_t GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount) const
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        return CopyStatesNewerThan(mouseHistory, lastStateIndex, states, maxStateCount);
    }

    uint32_t GetKeyboardState(uint64_t lastStateIndex, KeyboardState* states, uint32_t maxStateCount) const
    {
        std::scoped_lock<SpinLock> lock(stateLock);
        return CopyStatesNewerThan(keyboardHistory, lastStateIndex, states, maxStateCount);
    }

    uint64_t ReadTextStream(uint64_t firstIndex, wchar_t* characters, uint32_t maxCharacterCount) const
    {
        std::scoped_lock<SpinLock> lock(stateLock);

        if (characters == nullptr || maxCharacterCount == 0)
            return firstIndex;

        const uint64_t firstAvailable = nextTextInputIndex > kTextInputCapacity
            ? nextTextInputIndex - kTextInputCapacity
            : 0;
        const uint64_t start = std::max(firstIndex, firstAvailable);
        const uint64_t end = std::min<uint64_t>(nextTextInputIndex, start + maxCharacterCount);

        uint32_t characterIndex = 0;
        for (uint64_t index = start; index < end; ++index)
        {
            characters[characterIndex] = textInput[index % kTextInputCapacity].character;
            ++characterIndex;
        }

        return end;
    }

    void SetCursorShape(CursorShape shape)
    {
        cursorShape = shape;
        cursor = LoadCursorForShape(shape);
        if (hwnd != nullptr)
            ::SetCursor(cursor);
    }

    void SetTitle(const char* title)
    {
        if (hwnd != nullptr)
            ::SetWindowTextA(hwnd, title != nullptr ? title : "");
    }

    void* GetNativeHandle() const
    {
        return hwnd;
    }

    HWND hwnd = nullptr;
    HCURSOR cursor = ::LoadCursor(nullptr, IDC_ARROW);
    CursorShape cursorShape = CursorShape::Arrow;
    WNDCLASSEXA wc{};
    TaskQueue<std::function<void()>> taskQueue;
    std::jthread thread;

    mutable SpinLock stateLock;
    uint64_t lastClosePressEventIndex = 0;
    bool mouseTracked = false;
    MouseState mouse{};
    KeyboardState keyboard{};
    uint64_t nextMouseStateIndex = 0;
    uint64_t nextKeyboardStateIndex = 0;
    std::deque<MouseState> mouseHistory;
    std::deque<KeyboardState> keyboardHistory;
    std::array<TextInputCharacter, kTextInputCapacity> textInput = {};
    uint64_t nextTextInputIndex = 0;
};

WindowManager::WindowManager()
{
}

WindowManager::~WindowManager()
{
}

void WindowManager::CreateMainWindow(const char* windowName)
{
    std::promise<void> promise;
    m_impl->taskQueue.Push([&, windowName] {
        m_impl->Create(windowName);
        promise.set_value();
    });
    promise.get_future().wait();
}

void WindowManager::DestroyMainWindow()
{
    std::promise<void> promise;
    m_impl->taskQueue.Push([&] {
        m_impl->Destroy();
        promise.set_value();
    });
    promise.get_future().wait();
}

Int2 WindowManager::GetSize() const
{
    return m_impl->GetSize();
}

uint64_t WindowManager::GetLastClosePressEventIndex() const
{
    return m_impl->GetLastClosePressEventIndex();
}

uint32_t WindowManager::GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount) const
{
    return m_impl->GetMouseState(lastStateIndex, states, maxStateCount);
}

uint32_t WindowManager::GetKeyboardState(uint64_t lastStateIndex, KeyboardState* states, uint32_t maxStateCount) const
{
    return m_impl->GetKeyboardState(lastStateIndex, states, maxStateCount);
}

uint64_t WindowManager::ReadTextStream(uint64_t firstIndex, wchar_t* characters, uint32_t maxCharacterCount) const
{
    return m_impl->ReadTextStream(firstIndex, characters, maxCharacterCount);
}

void WindowManager::SetTitle(const char* title)
{
    std::string copiedTitle = title != nullptr ? title : "";
    m_impl->taskQueue.Push([this, title = std::move(copiedTitle)] {
        m_impl->SetTitle(title.c_str());
    });
}

void WindowManager::SetCursorShape(CursorShape shape)
{
    m_impl->taskQueue.Push([this, shape] {
        m_impl->SetCursorShape(shape);
    });
}

void* WindowManager::GetNativeHandle() const
{
    return m_impl->GetNativeHandle();
}

}
