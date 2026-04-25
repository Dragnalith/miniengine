#pragma once

#include <fnd/Pimpl.h>
#include <fnd/Window.h>
#include <fw/FrameData.h>

struct ID3D12GraphicsCommandList;
struct ImDrawData;

namespace migi
{

class RenderDevice;
class DearImGuiManager;

struct DearImGuiRendererImpl;

class DearImGuiRenderer
{
public:
	DearImGuiRenderer(RenderDevice& renderDevice, DearImGuiManager& manager);
	~DearImGuiRenderer();

	void Render(ID3D12GraphicsCommandList* commandList, const DrawData& drawData);

private:
	Pimpl<DearImGuiRendererImpl> m_impl;
};

}
