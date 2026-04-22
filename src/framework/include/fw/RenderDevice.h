#pragma once

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace engine
{

class Window;

/*
 * The RenderDevice own the D3D12Device and the D3D12CommandQueue
 */
class RenderDevice
{
public:
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice();
    ~RenderDevice();

    ID3D12Device* GetDevice();
    ID3D12CommandQueue* GetCommandQueue();
private:
    ID3D12Device* m_pd3dDevice = nullptr;
    ID3D12CommandQueue* m_pd3dCommandQueue = nullptr;
};

}