#pragma once

#include <fnd/Pimpl.h>
#include <fw/FrameData.h>

namespace drgn
{
class CommandList;
class RHI;
}

namespace migi
{

class DearImGuiManager;

struct DearImGuiRendererImpl;

class DearImGuiRenderer
{
public:
	DearImGuiRenderer(drgn::RHI& rhi, DearImGuiManager& manager);
	~DearImGuiRenderer();

	void Render(drgn::CommandList& commandList, const DrawData& drawData);

private:
	Pimpl<DearImGuiRendererImpl> m_impl;
};

}
