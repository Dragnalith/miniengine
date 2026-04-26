#include <fnd/Assert.h>
#include <fnd/MigiMain.h>
#include <fnd/PrimitiveTypes.h>
#include <fnd/Window.h>
#include <rhi/RHI.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
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

std::vector<std::byte> LoadBundledFile(const char* relativePath)
{
    std::ifstream file(relativePath, std::ios::binary | std::ios::ate);
    MIGI_ASSERT(file.is_open(), "Cannot open bundled file");

    const std::ifstream::pos_type end = file.tellg();
    MIGI_ASSERT(end >= 0, "Cannot determine bundled file size");

    std::vector<std::byte> bytes(static_cast<size_t>(end));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        MIGI_ASSERT(file.good(), "Cannot read bundled file");
    }
    return bytes;
}

} // namespace

void MigiMain()
{
    migi::WindowSetTitle("Triangle Example");

    std::unique_ptr<drgn::RHI> rhi = drgn::RHI::CreateDX12();

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

    std::vector<std::byte> vertexShader = LoadBundledFile("assets/shaders/triangle_vertex.shaderb");
    std::vector<std::byte> pixelShader = LoadBundledFile("assets/shaders/triangle_pixel.shaderb");
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
            drgn::kNullGpuAddress,
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
    rhi->DestroyBuffer(indexBuffer);
    rhi->DestroyBuffer(vertexBuffer);
    rhi->DestroySwapChain(swapChain);
}
