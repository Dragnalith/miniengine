#include <private/DearImGuiRenderer.h>

#include <fnd/Assert.h>
#include <rhi/RHI.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

namespace migi
{

namespace
{

constexpr uint32_t kFrameResourceCount = 8;
constexpr uint32_t kConstantBufferAlignment = 256;

struct alignas(kConstantBufferAlignment) DrawConstants
{
    float projection[4][4] = {};
    uint32_t textureIndex = drgn::kInvalidTexture;
    uint32_t padding[47] = {};
};
static_assert(sizeof(DrawConstants) == kConstantBufferAlignment);

struct FrameResources
{
    drgn::BufferHandle vertexBuffer;
    drgn::BufferHandle indexBuffer;
    drgn::BufferHandle constantBuffer;
    uint32_t vertexCapacity = 0;
    uint32_t indexCapacity = 0;
    uint32_t constantCapacity = 0;
};

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

drgn::TextureIndex TextureIndexFromImGui(ImTextureID textureId)
{
    return static_cast<drgn::TextureIndex>(reinterpret_cast<uintptr_t>(textureId));
}

ImTextureID ImGuiTextureIdFromIndex(drgn::TextureIndex index)
{
    return reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(index));
}

void FillProjection(const DrawData& drawData, float out[4][4])
{
    const float L = drawData.DisplayPos.x;
    const float R = drawData.DisplayPos.x + drawData.DisplaySize.x;
    const float T = drawData.DisplayPos.y;
    const float B = drawData.DisplayPos.y + drawData.DisplaySize.y;

    const float projection[4][4] =
    {
        { 2.0f / (R - L), 0.0f,           0.0f, 0.0f },
        { 0.0f,           2.0f / (T - B), 0.0f, 0.0f },
        { 0.0f,           0.0f,           0.5f, 0.0f },
        { (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f },
    };
    std::memcpy(out, projection, sizeof(projection));
}

uint32_t CountDrawCommands(const DrawData& drawData)
{
    uint32_t count = 0;
    for (const DrawList& list : drawData.DrawLists)
        count += static_cast<uint32_t>(list.CmdBuffer.Size);
    return count;
}

uint32_t ClampScissorMin(float value)
{
    return value <= 0.0f ? 0u : static_cast<uint32_t>(value);
}

} // namespace

struct DearImGuiRendererImpl
{
    DearImGuiRendererImpl(drgn::RHI& rhi)
        : rhi(rhi)
    {
    }

    drgn::RHI& rhi;
    drgn::ShaderPipelineHandle pipeline;
    drgn::TextureIndex fontTexture = drgn::kInvalidTexture;
    std::array<FrameResources, kFrameResourceCount> frames = {};
    uint64_t nextFrame = 0;
};

DearImGuiRenderer::DearImGuiRenderer(drgn::RHI& rhi, DearImGuiManager&)
    : m_impl(rhi)
{
    std::vector<std::byte> vertexShader = LoadBundledFile("assets/shaders/dear_imgui_vertex.shaderb");
    std::vector<std::byte> pixelShader = LoadBundledFile("assets/shaders/dear_imgui_pixel.shaderb");

    drgn::ShaderPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const std::byte>(vertexShader.data(), vertexShader.size());
    pipelineDesc.pixelShader = std::span<const std::byte>(pixelShader.data(), pixelShader.size());
    m_impl->pipeline = rhi.CreateShaderPipeline(pipelineDesc);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_drgn_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    drgn::TextureDesc textureDesc{};
    textureDesc.width = static_cast<uint32_t>(width);
    textureDesc.height = static_cast<uint32_t>(height);
    textureDesc.format = drgn::Format::R8G8B8A8_UNORM;
    textureDesc.initialData = pixels;
    m_impl->fontTexture = rhi.CreateTexture(textureDesc);
    io.Fonts->SetTexID(ImGuiTextureIdFromIndex(m_impl->fontTexture));
}

DearImGuiRenderer::~DearImGuiRenderer()
{
    drgn::RHI& rhi = m_impl->rhi;

    for (FrameResources& frame : m_impl->frames)
    {
        rhi.DestroyBuffer(frame.constantBuffer);
        rhi.DestroyBuffer(frame.indexBuffer);
        rhi.DestroyBuffer(frame.vertexBuffer);
    }

    rhi.DestroyTexture(m_impl->fontTexture);
    rhi.DestroyShaderPipeline(m_impl->pipeline);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->SetTexID(nullptr);
}

void DearImGuiRenderer::Render(drgn::CommandList& commandList, const DrawData& drawData)
{
    if (!drawData.Valid || drawData.DisplaySize.x <= 0.0f || drawData.DisplaySize.y <= 0.0f)
        return;

    drgn::RHI& rhi = m_impl->rhi;
    FrameResources& frame = m_impl->frames[m_impl->nextFrame++ % kFrameResourceCount];

    const uint32_t vertexCount = static_cast<uint32_t>(std::max(drawData.TotalVtxCount, 0));
    const uint32_t indexCount = static_cast<uint32_t>(std::max(drawData.TotalIdxCount, 0));
    const uint32_t drawCommandCount = CountDrawCommands(drawData);
    if (vertexCount == 0 || indexCount == 0 || drawCommandCount == 0)
        return;

    if (!frame.vertexBuffer.IsValid() || frame.vertexCapacity < vertexCount)
    {
        rhi.DestroyBuffer(frame.vertexBuffer);
        frame.vertexCapacity = vertexCount + 5000;
        drgn::BufferDesc desc{};
        desc.byteSize = frame.vertexCapacity * sizeof(ImDrawVert);
        frame.vertexBuffer = rhi.CreateBuffer(desc);
    }

    if (!frame.indexBuffer.IsValid() || frame.indexCapacity < indexCount)
    {
        rhi.DestroyBuffer(frame.indexBuffer);
        frame.indexCapacity = indexCount + 10000;
        drgn::BufferDesc desc{};
        desc.byteSize = frame.indexCapacity * sizeof(ImDrawIdx);
        frame.indexBuffer = rhi.CreateBuffer(desc);
    }

    if (!frame.constantBuffer.IsValid() || frame.constantCapacity < drawCommandCount)
    {
        rhi.DestroyBuffer(frame.constantBuffer);
        frame.constantCapacity = drawCommandCount + 256;
        drgn::BufferDesc desc{};
        desc.byteSize = frame.constantCapacity * sizeof(DrawConstants);
        frame.constantBuffer = rhi.CreateBuffer(desc);
    }

    std::vector<std::byte> vertexBytes(vertexCount * sizeof(ImDrawVert));
    std::vector<std::byte> indexBytes(indexCount * sizeof(ImDrawIdx));
    std::vector<DrawConstants> constants(drawCommandCount);

    ImDrawVert* vertexDst = reinterpret_cast<ImDrawVert*>(vertexBytes.data());
    ImDrawIdx* indexDst = reinterpret_cast<ImDrawIdx*>(indexBytes.data());
    for (const DrawList& list : drawData.DrawLists)
    {
        const size_t vertexByteCount = static_cast<size_t>(list.VtxBuffer.Size) * sizeof(ImDrawVert);
        const size_t indexByteCount = static_cast<size_t>(list.IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertexDst, list.VtxBuffer.Data, vertexByteCount);
        std::memcpy(indexDst, list.IdxBuffer.Data, indexByteCount);
        vertexDst += list.VtxBuffer.Size;
        indexDst += list.IdxBuffer.Size;
    }

    const ImVec2 clipOffset = drawData.DisplayPos;
    const ImVec2 clipScale = drawData.FramebufferScale;
    uint32_t constantIndex = 0;
    for (const DrawList& list : drawData.DrawLists)
    {
        for (int cmdIndex = 0; cmdIndex < list.CmdBuffer.Size; ++cmdIndex)
        {
            const ImDrawCmd& drawCmd = list.CmdBuffer[cmdIndex];
            MIGI_ASSERT(drawCmd.UserCallback == nullptr, "ImGui user callbacks are not supported");

            DrawConstants& drawConstants = constants[constantIndex++];
            FillProjection(drawData, drawConstants.projection);
            drawConstants.textureIndex = TextureIndexFromImGui(drawCmd.GetTexID());
        }
    }

    rhi.UpdateBuffer(frame.vertexBuffer, vertexBytes.data(), static_cast<uint32_t>(vertexBytes.size()));
    rhi.UpdateBuffer(frame.indexBuffer, indexBytes.data(), static_cast<uint32_t>(indexBytes.size()));
    rhi.UpdateBuffer(
        frame.constantBuffer,
        constants.data(),
        static_cast<uint32_t>(constants.size() * sizeof(DrawConstants)));

    commandList.SetViewport({
        0.0f,
        0.0f,
        drawData.DisplaySize.x * drawData.FramebufferScale.x,
        drawData.DisplaySize.y * drawData.FramebufferScale.y,
        0.0f,
        1.0f,
    });
    commandList.SetBlendMode(drgn::BlendMode::Alpha);
    commandList.BindShaderPipeline(m_impl->pipeline);

    const drgn::GpuAddress vertexAddress = rhi.GetBufferGpuAddress(frame.vertexBuffer);
    const drgn::GpuAddress indexAddress = rhi.GetBufferGpuAddress(frame.indexBuffer);
    const drgn::GpuAddress constantAddress = rhi.GetBufferGpuAddress(frame.constantBuffer);

    int globalVertexOffset = 0;
    int globalIndexOffset = 0;
    uint32_t drawIndex = 0;
    for (const DrawList& list : drawData.DrawLists)
    {
        for (int cmdIndex = 0; cmdIndex < list.CmdBuffer.Size; ++cmdIndex)
        {
            const ImDrawCmd& drawCmd = list.CmdBuffer[cmdIndex];

            ImVec2 clipMin(
                (drawCmd.ClipRect.x - clipOffset.x) * clipScale.x,
                (drawCmd.ClipRect.y - clipOffset.y) * clipScale.y);
            ImVec2 clipMax(
                (drawCmd.ClipRect.z - clipOffset.x) * clipScale.x,
                (drawCmd.ClipRect.w - clipOffset.y) * clipScale.y);
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
            {
                ++drawIndex;
                continue;
            }

            const uint32_t scissorX = ClampScissorMin(clipMin.x);
            const uint32_t scissorY = ClampScissorMin(clipMin.y);
            const uint32_t scissorRight = ClampScissorMin(clipMax.x);
            const uint32_t scissorBottom = ClampScissorMin(clipMax.y);
            commandList.SetScissor({
                static_cast<int32_t>(scissorX),
                static_cast<int32_t>(scissorY),
                scissorRight - scissorX,
                scissorBottom - scissorY,
            });

            drgn::DrawIndexedDesc draw{};
            draw.vertexBuffer = vertexAddress;
            draw.indexBuffer = indexAddress;
            draw.userData = constantAddress + static_cast<drgn::GpuAddress>(drawIndex) * sizeof(DrawConstants);
            draw.indexCount = drawCmd.ElemCount;
            draw.firstIndex = drawCmd.IdxOffset + globalIndexOffset;
            draw.vertexOffset = static_cast<int32_t>(drawCmd.VtxOffset + globalVertexOffset);
            draw.indexFormat = sizeof(ImDrawIdx) == 2 ? drgn::Format::R16_UINT : drgn::Format::R32_UINT;
            commandList.DrawIndexed(draw);

            ++drawIndex;
        }

        globalIndexOffset += list.IdxBuffer.Size;
        globalVertexOffset += list.VtxBuffer.Size;
    }
}

}
