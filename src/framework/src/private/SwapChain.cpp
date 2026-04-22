#include <fw/SwapChain.h>

#include <fw/RenderDevice.h>
#include <fnd/Window.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <iostream>
static constexpr int NUM_BACK_BUFFERS = 2;

namespace engine
{

struct SwapChainImpl
{
    SwapChainImpl(RenderDevice& device) : renderDevice(device) {}
    RenderDevice& renderDevice;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    IDXGISwapChain3* swapChain = nullptr;
    HANDLE waitableObject = nullptr;
    ID3D12Resource* mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};
    int width;
    int height;
    bool fullscreen;

    void CreateRenderTarget() {
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            ID3D12Resource* pBackBuffer = NULL;
            swapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
            renderDevice.GetDevice()->CreateRenderTargetView(pBackBuffer, NULL, mainRenderTargetDescriptor[i]);
            mainRenderTargetResource[i] = pBackBuffer;
        }
    }
    void CleanupRenderTarget() {
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
            if (mainRenderTargetResource[i]) { mainRenderTargetResource[i]->Release(); mainRenderTargetResource[i] = nullptr; }
    }
};



SwapChain::SwapChain(RenderDevice& device)
    : m_impl(device)
{
    Window::GetMainWindow().GetSize(&m_impl->width, &m_impl->height);
    HRESULT result = S_OK;

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        result = device.GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_impl->descriptorHeap));
        ASSERT_MSG(result == S_OK, "SwapChain descriptor heap failed");

        SIZE_T rtvDescriptorSize = device.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_impl->descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            m_impl->mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    {
        IDXGIFactory4* dxgiFactory = NULL;
        IDXGISwapChain1* swapChain1 = NULL;
        
        result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
        ASSERT_MSG(result == S_OK, "DXGI Factory creation failed");

        result = dxgiFactory->CreateSwapChainForHwnd(device.GetCommandQueue(), Window::GetMainWindow().GetHandle(), &sd, NULL, NULL, &swapChain1);
        ASSERT_MSG(result == S_OK, "CreateSwapChainForHwnd failed");
        result = swapChain1->QueryInterface(IID_PPV_ARGS(&m_impl->swapChain));
        ASSERT_MSG(result == S_OK, "SwapChain QueryInterface failed");
        result = dxgiFactory->MakeWindowAssociation(Window::GetMainWindow().GetHandle(), DXGI_MWA_NO_WINDOW_CHANGES);
        ASSERT_MSG(result == S_OK, "Failed to make swapchain window assocation");

        swapChain1->Release();
        dxgiFactory->Release();
        m_impl->swapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
        m_impl->waitableObject = m_impl->swapChain->GetFrameLatencyWaitableObject();
    }

    BOOL fullscreenState;
    m_impl->swapChain->GetFullscreenState(&fullscreenState, nullptr);
    m_impl->fullscreen = fullscreenState != FALSE;
    m_impl->CreateRenderTarget();
}

SwapChain::~SwapChain() 
{
    m_impl->CleanupRenderTarget();
    if (m_impl->waitableObject != nullptr) { ::CloseHandle(m_impl->waitableObject); }
    if (m_impl->swapChain) { m_impl->swapChain->SetFullscreenState(false, nullptr); m_impl->swapChain->Release(); m_impl->swapChain = nullptr; }
    if (m_impl->descriptorHeap) { m_impl->descriptorHeap->Release(); m_impl->descriptorHeap = nullptr; }
}

uint32_t SwapChain::GetCurrentIndex() const {
    return m_impl->swapChain->GetCurrentBackBufferIndex();
}

uint32_t SwapChain::GetBufferCount() const {
    return NUM_BACK_BUFFERS;
}

ID3D12Resource* SwapChain::GetResource(int64_t index) const {
    return m_impl->mainRenderTargetResource[index % NUM_BACK_BUFFERS];
}

const D3D12_CPU_DESCRIPTOR_HANDLE& SwapChain::GetRenderTargetDescriptor(int64_t index) const {
    return m_impl->mainRenderTargetDescriptor[index % NUM_BACK_BUFFERS];
}

HANDLE SwapChain::GetWaitableObject() const {
    return m_impl->waitableObject;
}

void SwapChain::Present(uint32_t syncInterval) const {
    HRESULT result = m_impl->swapChain->Present(syncInterval, 0);
    ASSERT_MSG(result == S_OK, "Swapchain present has failed");
}

bool SwapChain::NeedResize(int width, int height, bool fullscreen) const {
    return m_impl->fullscreen != fullscreen || m_impl->width != width || m_impl->height != height;
}

void SwapChain::Resize(int width, int height, bool fullscreen) {
    m_impl->CleanupRenderTarget();
    if (m_impl->fullscreen != fullscreen) {
        m_impl->swapChain->SetFullscreenState(fullscreen, nullptr);
    }
    HRESULT result = m_impl->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    ASSERT_MSG(result == S_OK, "Failed to resize swapchain.");
    m_impl->CreateRenderTarget();
    m_impl->width = width;
    m_impl->height = height;
    m_impl->fullscreen = fullscreen;
}

}