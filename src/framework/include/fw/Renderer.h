#pragma once

#include <fnd/Pimpl.h>

struct ImDrawData;

namespace migi
{

class RenderDevice;
class SwapChain;
class DearImGuiManager;
struct FrameData;

struct RendererImpl;

class Renderer
{
public:
	Renderer(RenderDevice& renderDevice, SwapChain& swapChain, DearImGuiManager& manager);
	~Renderer();

	void Render(FrameData& frameData);
	void Kick(const FrameData& frameData);
	void Clean(const FrameData& frameData);

private:
	Pimpl<RendererImpl> m_impl;
};

}
