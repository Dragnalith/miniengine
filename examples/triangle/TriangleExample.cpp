#include <fnd/FileSystem.h>
#include <fnd/MigiMain.h>
#include <fnd/PrimitiveTypes.h>
#include <fnd/Window.h>
#include <rhi/RHI.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace
{

struct Vertex
{
    float pos[2];
    float color[3];
};

constexpr Vertex kVertices[] =
{
    { {  0.00f,  0.60f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.60f, -0.60f }, { 0.0f, 1.0f, 0.0f } },
    { { -0.60f, -0.60f }, { 0.0f, 0.0f, 1.0f } },
};

constexpr uint16_t kIndices[] = { 0, 1, 2 };

// The RHI binds per-draw "user data" (HLSL register b0) at a fixed slot for
// every draw. This triangle's vertex shader does not read it, but the backend
// still requires a valid buffer, so a small zero-initialized one is supplied.
constexpr uint32_t kUserDataByteSize = 256;

} // namespace

void MigiMain()
{
    migi::WindowSetTitle("Triangle Example");

    std::unique_ptr<drgn::RHI> rhi = drgn::RHI::Create();

    migi::Int2 windowSize = migi::WindowGetSize();
    uint32_t width = static_cast<uint32_t>(std::max(windowSize.x, 1));
    uint32_t height = static_cast<uint32_t>(std::max(windowSize.y, 1));

    drgn::SwapChainDesc swapChainDesc{};
    swapChainDesc.windowHandle = migi::WindowGetNativeHandle();
    swapChainDesc.width = width;
    swapChainDesc.height = height;
    drgn::SwapChainHandle swapChain = rhi->CreateSwapChain(swapChainDesc);

    drgn::BufferDesc vertexBufferDesc{};
    vertexBufferDesc.byteSize = sizeof(kVertices);
    vertexBufferDesc.initialData = kVertices;
    drgn::BufferHandle vertexBuffer = rhi->CreateBuffer(vertexBufferDesc);

    drgn::BufferDesc indexBufferDesc{};
    indexBufferDesc.byteSize = sizeof(kIndices);
    indexBufferDesc.initialData = kIndices;
    drgn::BufferHandle indexBuffer = rhi->CreateBuffer(indexBufferDesc);

    const std::byte userDataInit[kUserDataByteSize] = {};
    drgn::BufferDesc userDataBufferDesc{};
    userDataBufferDesc.byteSize = kUserDataByteSize;
    userDataBufferDesc.initialData = userDataInit;
    drgn::BufferHandle userDataBuffer = rhi->CreateBuffer(userDataBufferDesc);

    std::vector<std::byte> vertexShader = migi::ReadFile("shaders/triangle_vertex.shaderb");
    std::vector<std::byte> pixelShader = migi::ReadFile("shaders/triangle_pixel.shaderb");
    drgn::ShaderPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const std::byte>(vertexShader.data(), vertexShader.size());
    pipelineDesc.pixelShader = std::span<const std::byte>(pixelShader.data(), pixelShader.size());
    drgn::ShaderPipelineHandle pipeline = rhi->CreateShaderPipeline(pipelineDesc);

    const uint64_t firstCloseEvent = migi::WindowGetLastClosePressEventIndex();
    while (firstCloseEvent == migi::WindowGetLastClosePressEventIndex())
    {
        windowSize = migi::WindowGetSize();
        const uint32_t newWidth = static_cast<uint32_t>(std::max(windowSize.x, 1));
        const uint32_t newHeight = static_cast<uint32_t>(std::max(windowSize.y, 1));
        if (newWidth != width || newHeight != height)
        {
            width = newWidth;
            height = newHeight;
            rhi->ResizeSwapChain(swapChain, width, height);
        }

        drgn::CommandList* commandList = rhi->BeginCommandList();
        commandList->BeginRenderPass(swapChain, drgn::ClearColor{ 0.10f, 0.10f, 0.15f, 1.0f });
        commandList->SetViewport({ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f });
        commandList->SetScissor({ 0, 0, width, height });
        commandList->SetBlendMode(drgn::BlendMode::Opaque);
        commandList->BindShaderPipeline(pipeline);
        commandList->DrawIndexed({
            rhi->GetBufferGpuAddress(vertexBuffer),
            rhi->GetBufferGpuAddress(indexBuffer),
            rhi->GetBufferGpuAddress(userDataBuffer),
            3,
            0,
            0,
            drgn::Format::R16_UINT,
        });
        commandList->EndRenderPass();
        rhi->Submit(commandList);
        rhi->Present(swapChain, 1);

        std::this_thread::yield();
    }

    rhi->WaitIdle();
    rhi->DestroyShaderPipeline(pipeline);
    rhi->DestroyBuffer(userDataBuffer);
    rhi->DestroyBuffer(indexBuffer);
    rhi->DestroyBuffer(vertexBuffer);
    rhi->DestroySwapChain(swapChain);
}
