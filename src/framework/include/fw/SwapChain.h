#pragma once

#include <fnd/Util.h>

#include <windows.h>
#include <stdint.h>

struct IDXGISwapChain3;
struct ID3D12Resource;
struct D3D12_CPU_DESCRIPTOR_HANDLE;

namespace engine
{

class RenderDevice;
class Window;

struct SwapChainImpl;

/*
 * Given a window, own swapchain object and related resources
 * 
 * The swapchain has the ability to be resized, but be careful about
 * synchronization. It should not be resized while it is used by the GPU
 * for some rendering
 */
class SwapChain
{
public:
    SwapChain(RenderDevice& device);
    ~SwapChain();

    uint32_t GetBufferCount() const;
    uint32_t GetCurrentIndex() const;
    ID3D12Resource* GetResource(int64_t index) const;
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetRenderTargetDescriptor(int64_t) const;
    HANDLE GetWaitableObject() const;

    void Present(uint32_t syncInterval) const;
    bool NeedResize(int width, int height, bool fullscreen) const;

    void Resize(int width, int height, bool fullscreen);
private:
    Pimpl<SwapChainImpl> m_impl;
};

}