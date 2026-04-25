#pragma once

#include <fnd/Input.h>
#include <fnd/Window.h>
#include <imgui/imgui.h>

namespace migi
{
class DearImGuiManager
{
public:
	DearImGuiManager();
	~DearImGuiManager();

	void Update(float delattime);

protected:
	ImGuiMouseCursor m_lastMouseCursor = ImGuiMouseCursor_COUNT;
	MouseState m_lastMouseState = {};
	uint64_t m_nextTextInputIndex = 0;
};

}
