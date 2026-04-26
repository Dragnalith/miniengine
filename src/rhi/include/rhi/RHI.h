#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>

namespace drgn
{

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------
//
// Non-bindless resources are opaque handle structs; id == 0 means "null".
// Buffers are handles for lifetime management, but shader-visible access is
// done through GPU addresses returned by GetBufferGpuAddress().
//
// Textures are not handles: a texture is just its bindless descriptor index,
// the slot reached from HLSL via ResourceDescriptorHeap[index] (SM 6.6) or an
// unbounded Texture2D array. Put the TextureIndex in user data and sample it
// from shader code by convention.

template<typename Tag>
struct Handle
{
    uint32_t id = 0;

    constexpr Handle() = default;
    explicit constexpr Handle(uint32_t value) : id(value) {}

    bool IsValid() const { return id != 0; }
    bool operator==(Handle o) const { return id == o.id; }
    bool operator!=(Handle o) const { return id != o.id; }
};

struct BufferHandleTag;
struct SwapChainHandleTag;
struct ShaderPipelineHandleTag;

using BufferHandle = Handle<BufferHandleTag>;
using SwapChainHandle = Handle<SwapChainHandleTag>;
using ShaderPipelineHandle = Handle<ShaderPipelineHandleTag>;

// Bindless descriptor index. kInvalidTexture means "no texture".
using TextureIndex = uint32_t;
constexpr TextureIndex kInvalidTexture = 0xFFFFFFFFu;

// GPU virtual address / root descriptor address used by draw calls.
// 0 means "null" and may only be used if the shader convention allows it.
using GpuAddress = uint64_t;
constexpr GpuAddress kNullGpuAddress = 0;


// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

enum class Format : uint8_t
{
    Unknown = 0,
    R8G8B8A8_UNORM,
    R16_UINT,
    R32_UINT,
    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,
};

enum class BlendMode : uint8_t
{
    Opaque,
    Alpha,
};


// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

struct ShaderPipelineDesc
{
    // Compiled shader bytecode. The asset filename is platform-neutral
    // (.shaderb); the backend interprets the content for the active API.
    //
    // There is no input-assembler vertex layout. DrawIndexed() supplies GPU
    // addresses for the vertex buffer, index buffer, and user data. The backend
    // binds those addresses at fixed shader-visible locations by convention;
    // the vertex shader pulls vertex data itself, typically indexed by
    // SV_VertexID. The vertex/user structs are defined in HLSL.
    std::span<const std::byte> vertexShader;
    std::span<const std::byte> pixelShader;
};

struct BufferDesc
{
    uint32_t    byteSize    = 0;
    const void* initialData = nullptr; // optional, may be null
};

struct TextureDesc
{
    uint32_t    width       = 0;
    uint32_t    height      = 0;
    Format      format      = Format::R8G8B8A8_UNORM;
    const void* initialData = nullptr; // tightly packed, one mip, one slice
};

struct SwapChainDesc
{
    void*    windowHandle = nullptr; // HWND on Windows
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint32_t bufferCount  = 2;
    Format   colorFormat  = Format::R8G8B8A8_UNORM;
    bool     fullscreen   = false;
};


// ---------------------------------------------------------------------------
// Small value types
// ---------------------------------------------------------------------------

struct ClearColor { float r = 0.f, g = 0.f, b = 0.f, a = 1.f; };

struct Viewport
{
    float x = 0.f, y = 0.f;
    float width = 0.f, height = 0.f;
    float minDepth = 0.f, maxDepth = 1.f;
};

struct Scissor
{
    int32_t  x = 0,     y = 0;
    uint32_t width = 0, height = 0;
};

struct DrawIndexedDesc
{
    GpuAddress vertexBuffer = kNullGpuAddress;
    GpuAddress indexBuffer = kNullGpuAddress;
    GpuAddress userData = kNullGpuAddress;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    Format indexFormat = Format::R16_UINT;
};


// ---------------------------------------------------------------------------
// CommandList
// ---------------------------------------------------------------------------

class CommandList
{
public:
    virtual ~CommandList() = default;

    // A render pass targets a single swapchain back buffer. The RHI tracks the
    // back-buffer acquisition internally; there is no explicit acquire API.
    virtual void BeginRenderPass(SwapChainHandle target, const ClearColor& clear) = 0;
    virtual void EndRenderPass() = 0;

    virtual void SetViewport(const Viewport& vp)  = 0;
    virtual void SetScissor (const Scissor&  sc)  = 0;
    virtual void SetBlendMode(BlendMode mode)     = 0;

    virtual void BindShaderPipeline(ShaderPipelineHandle pipeline) = 0;

    // vertexBuffer, indexBuffer, and userData are GPU addresses. Their shader
    // binding locations are fixed by backend convention, not described by the
    // pipeline. Backends may implement the indexed lookup as shader-pulled
    // indexing instead of an input-assembler indexed draw.
    virtual void DrawIndexed(const DrawIndexedDesc& desc) = 0;
};


// ---------------------------------------------------------------------------
// RHI
// ---------------------------------------------------------------------------
//
// Abstract device interface. All GPU work is submitted through this object.
// The concept of queue is not exposed: Submit() is the only entry point.
// The descriptor pool used for bindless textures is fully internal; callers
// only manipulate TextureIndex values.

class RHI
{
public:
    virtual ~RHI() = default;

    // Factory for the backend selected at build time.
    static std::unique_ptr<RHI> Create();

    // ---- Resources ----

    virtual BufferHandle         CreateBuffer(const BufferDesc& desc) = 0;
    virtual void                 DestroyBuffer(BufferHandle handle)   = 0;
    virtual GpuAddress           GetBufferGpuAddress(BufferHandle handle) const = 0;
    virtual void                 UpdateBuffer (BufferHandle handle,
                                               const void*  data,
                                               uint32_t     byteSize,
                                               uint32_t     byteOffset = 0) = 0;

    virtual TextureIndex         CreateTexture (const TextureDesc& desc) = 0;
    virtual void                 DestroyTexture(TextureIndex index)       = 0;

    virtual ShaderPipelineHandle CreateShaderPipeline (const ShaderPipelineDesc& desc) = 0;
    virtual void                 DestroyShaderPipeline(ShaderPipelineHandle handle)    = 0;

    virtual SwapChainHandle      CreateSwapChain (const SwapChainDesc& desc) = 0;
    virtual void                 ResizeSwapChain (SwapChainHandle handle,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  bool fullscreen = false)   = 0;
    virtual void                 DestroySwapChain(SwapChainHandle handle)    = 0;

    // ---- Submission ----
    //
    // BeginCommandList() returns a list owned by the RHI. It becomes invalid
    // once Submit() has been called on it.

    virtual CommandList* BeginCommandList()              = 0;
    virtual void         Submit(CommandList* cmd)        = 0;
    virtual void         Present(SwapChainHandle handle,
                                 uint32_t syncInterval = 1) = 0;
    virtual void         WaitIdle()                      = 0;
};

} // namespace drgn
