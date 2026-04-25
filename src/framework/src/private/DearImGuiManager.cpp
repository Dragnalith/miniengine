#include <fw/DearImGuiManager.h>

#include <fnd/Util.h>
#include <fnd/Window.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>

namespace migi
{

namespace
{
constexpr float kMouseWheelUnit = 120.0f;

CursorShape ToCursorShape(ImGuiMouseCursor cursor)
{
    switch (cursor)
    {
    case ImGuiMouseCursor_None:       return CursorShape::None;
    case ImGuiMouseCursor_Arrow:      return CursorShape::Arrow;
    case ImGuiMouseCursor_TextInput:  return CursorShape::TextInput;
    case ImGuiMouseCursor_ResizeAll:  return CursorShape::ResizeAll;
    case ImGuiMouseCursor_ResizeEW:   return CursorShape::ResizeEW;
    case ImGuiMouseCursor_ResizeNS:   return CursorShape::ResizeNS;
    case ImGuiMouseCursor_ResizeNESW: return CursorShape::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE: return CursorShape::ResizeNWSE;
    case ImGuiMouseCursor_Hand:       return CursorShape::Hand;
    case ImGuiMouseCursor_NotAllowed: return CursorShape::NotAllowed;
    default:                          return CursorShape::Arrow;
    }
}
}

DearImGuiManager::DearImGuiManager()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a renderer backend!");

    io.BackendPlatformUserData = nullptr;
    io.BackendPlatformName = "imgui_impl_drgn_foundation";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    m_nextTextInputIndex = 0;
}

DearImGuiManager::~DearImGuiManager()
{
    ImGuiIO& io = ImGui::GetIO();

    io.BackendPlatformName = NULL;
    io.BackendPlatformUserData = NULL;

    ImGui::DestroyContext();
}

void DearImGuiManager::Update(float deltatime)
{
    ImGuiIO& io = ImGui::GetIO();

    Int2 windowSize = WindowGetSize();
    io.DisplaySize = ImVec2(static_cast<float>(windowSize.x), static_cast<float>(windowSize.y));
    io.DeltaTime = deltatime > 0.0f ? deltatime : (1.0f / 60.0f);

    std::array<MouseState, kDefaultInputStateReadCount> mouseStates{};
    const uint32_t mouseStateCount = GetMouseState(
        m_lastMouseState.stateIndex,
        mouseStates.data(),
        static_cast<uint32_t>(mouseStates.size()));

    for (uint32_t i = mouseStateCount; i > 0; --i)
    {
        const MouseState& mouseState = mouseStates[i - 1];
        const int dx = mouseState.x - m_lastMouseState.x;
        const int dy = mouseState.y - m_lastMouseState.y;
        const int wheel = mouseState.wheel - m_lastMouseState.wheel;
        const int horizontalWheel = mouseState.horizontalWheel - m_lastMouseState.horizontalWheel;
        const bool enteredWindow = !m_lastMouseState.inWindow && mouseState.inWindow;
        const bool leftWindow = m_lastMouseState.inWindow && !mouseState.inWindow;

        if (dx != 0 || dy != 0 || enteredWindow || leftWindow)
        {
            if (mouseState.inWindow)
                io.AddMousePosEvent(static_cast<float>(mouseState.x), static_cast<float>(mouseState.y));
            else
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }

        for (uint32_t i = 0; i < kMouseButtonCount; ++i)
        {
            if (m_lastMouseState.buttonsDown[i] != mouseState.buttonsDown[i])
                io.AddMouseButtonEvent(static_cast<int>(i), mouseState.buttonsDown[i]);
        }

        if (wheel != 0 || horizontalWheel != 0)
        {
            io.AddMouseWheelEvent(
                static_cast<float>(horizontalWheel) / kMouseWheelUnit,
                static_cast<float>(wheel) / kMouseWheelUnit);
        }

        m_lastMouseState = mouseState;
    }

    std::array<wchar_t, 256> textCharacters{};
    const uint64_t nextTextInputIndex = ReadTextStream(
        m_nextTextInputIndex,
        textCharacters.data(),
        static_cast<uint32_t>(textCharacters.size()));
    const uint64_t textCharacterCount64 = nextTextInputIndex > m_nextTextInputIndex
        ? nextTextInputIndex - m_nextTextInputIndex
        : 0;
    const uint32_t textCharacterCount = static_cast<uint32_t>(
        std::min<uint64_t>(textCharacterCount64, textCharacters.size()));
    for (uint32_t i = 0; i < textCharacterCount; ++i)
    {
        if (textCharacters[i] != 0)
            io.AddInputCharacterUTF16(static_cast<ImWchar16>(textCharacters[i]));
    }
    m_nextTextInputIndex = nextTextInputIndex;

    ImGuiMouseCursor mouseCursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (m_lastMouseCursor != mouseCursor)
    {
        m_lastMouseCursor = mouseCursor;
        if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0)
            WindowSetCursorShape(ToCursorShape(mouseCursor));
    }

    ImGui::NewFrame();
}

}
