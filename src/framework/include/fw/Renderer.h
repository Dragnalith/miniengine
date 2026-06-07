#pragma once

#include <fnd/Pimpl.h>

struct ImDrawData;

namespace migi
{

class DearImGuiManager;
struct FrameData;

struct RendererImpl;

class Renderer
{
public:
	Renderer(DearImGuiManager& manager);
	~Renderer();

	void Render(FrameData& frameData);
	void Kick(const FrameData& frameData);
	void Clean(const FrameData& frameData);

	// Release the swapchain/surface (window gone) and recreate it against the
	// current window (window back). Used to survive background/resume.
	void Suspend();
	void Resume();

private:
	Pimpl<RendererImpl> m_impl;
};

}
