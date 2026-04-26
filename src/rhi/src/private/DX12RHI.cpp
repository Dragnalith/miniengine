#include <rhi/RHI.h>

#include <fnd/Assert.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include <windows.h>

namespace drgn
{

namespace
{

template<typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr))
    {
    }

    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != std::addressof(other))
        {
            Reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
        }
        return *this;
    }

    T* Get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    T** operator&()
    {
        Reset();
        return &m_ptr;
    }

    T** ReleaseAndGetAddressOf()
    {
        Reset();
        return &m_ptr;
    }

    void Reset()
    {
        if (m_ptr != nullptr)
        {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

    template<typename U>
    HRESULT As(ComPtr<U>& out) const
    {
        return m_ptr->QueryInterface(IID_PPV_ARGS(out.ReleaseAndGetAddressOf()));
    }

private:
    T* m_ptr = nullptr;
};

constexpr UINT kMaxBindlessTextures = 1024;
constexpr UINT kDefaultBackBufferCount = 2;

DXGI_FORMAT ToDxgiFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8B8A8_UNORM:       return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::R16_UINT:             return DXGI_FORMAT_R16_UINT;
    case Format::R32_UINT:             return DXGI_FORMAT_R32_UINT;
    case Format::R32_FLOAT:            return DXGI_FORMAT_R32_FLOAT;
    case Format::R32G32_FLOAT:         return DXGI_FORMAT_R32G32_FLOAT;
    case Format::R32G32B32_FLOAT:      return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::R32G32B32A32_FLOAT:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::Unknown:              return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

uint32_t FormatByteSize(Format format)
{
    switch (format)
    {
    case Format::R8G8B8A8_UNORM:       return 4;
    case Format::R16_UINT:             return 2;
    case Format::R32_UINT:             return 4;
    case Format::R32_FLOAT:            return 4;
    case Format::R32G32_FLOAT:         return 8;
    case Format::R32G32B32_FLOAT:      return 12;
    case Format::R32G32B32A32_FLOAT:   return 16;
    case Format::Unknown:              return 0;
    }
    return 0;
}

uint32_t IndexByteSize(Format format)
{
    switch (format)
    {
    case Format::R16_UINT: return 2;
    case Format::R32_UINT: return 4;
    default:               return 0;
    }
}

uint32_t Align(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

D3D12_BLEND_DESC BlendDesc(BlendMode mode)
{
    D3D12_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (mode == BlendMode::Alpha)
    {
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    else
    {
        desc.RenderTarget[0].BlendEnable = FALSE;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    return desc;
}

D3D12_RASTERIZER_DESC RasterizerDesc()
{
    D3D12_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = D3D12_CULL_MODE_NONE;
    desc.FrontCounterClockwise = FALSE;
    desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.DepthClipEnable = TRUE;
    desc.MultisampleEnable = FALSE;
    desc.AntialiasedLineEnable = FALSE;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return desc;
}

D3D12_DEPTH_STENCIL_DESC DepthStencilDesc()
{
    D3D12_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = FALSE;
    desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.StencilEnable = FALSE;
    desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.BackFace = desc.FrontFace;
    return desc;
}

void CheckResult(HRESULT hr, const char* message)
{
    MIGI_ASSERT(SUCCEEDED(hr), message);
}

} // namespace

class Dx12RHI final : public RHI
{
public:
    Dx12RHI();
    ~Dx12RHI() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle handle) override;
    GpuAddress GetBufferGpuAddress(BufferHandle handle) const override;
    void UpdateBuffer(BufferHandle handle, const void* data, uint32_t byteSize, uint32_t byteOffset) override;

    TextureIndex CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureIndex index) override;

    ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& desc) override;
    void DestroyShaderPipeline(ShaderPipelineHandle handle) override;

    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override;
    void ResizeSwapChain(SwapChainHandle handle, uint32_t width, uint32_t height, bool fullscreen) override;
    void DestroySwapChain(SwapChainHandle handle) override;

    CommandList* BeginCommandList() override;
    void Submit(CommandList* cmd) override;
    void Present(SwapChainHandle handle, uint32_t syncInterval) override;
    void WaitIdle() override;

private:
    struct BufferRecord
    {
        ComPtr<ID3D12Resource> resource;
        uint32_t byteSize = 0;
        bool alive = false;
    };

    struct TextureRecord
    {
        ComPtr<ID3D12Resource> resource;
        bool alive = false;
    };

    struct ShaderPipelineRecord
    {
        ComPtr<ID3D12PipelineState> opaqueState;
        ComPtr<ID3D12PipelineState> alphaState;
        bool alive = false;
    };

    struct SwapChainRecord
    {
        ComPtr<IDXGISwapChain3> swapChain;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        std::vector<ComPtr<ID3D12Resource>> backBuffers;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bufferCount = 0;
        uint32_t nextBackBuffer = 0;
        Format colorFormat = Format::R8G8B8A8_UNORM;
        bool fullscreen = false;
        bool alive = false;
    };

    class Dx12CommandList final : public CommandList
    {
    public:
        Dx12CommandList(Dx12RHI& owner, ID3D12Device* device);

        void ResetForRecording();
        void MarkSubmitted(uint64_t fenceValue);
        bool CanReuse(ID3D12Fence* fence) const;
        ID3D12GraphicsCommandList* Get() const { return list.Get(); }

        void BeginRenderPass(SwapChainHandle target, const ClearColor& clear) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& vp) override;
        void SetScissor(const Scissor& sc) override;
        void SetBlendMode(BlendMode mode) override;
        void BindShaderPipeline(ShaderPipelineHandle pipeline) override;
        void DrawIndexed(const DrawIndexedDesc& desc) override;

        bool recording = false;
        bool submitted = false;
        uint64_t submittedFenceValue = 0;

    private:
        void ApplyPipelineState();

        Dx12RHI& owner;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        SwapChainHandle activeSwapChain;
        uint32_t activeBackBuffer = 0;
        ShaderPipelineHandle activePipeline;
        BlendMode activeBlendMode = BlendMode::Opaque;
        bool renderPassOpen = false;
    };

    void CreateRootSignature();
    ComPtr<ID3D12PipelineState> CreatePipelineState(const ShaderPipelineDesc& desc, BlendMode blendMode);
    SwapChainRecord& GetSwapChain(SwapChainHandle handle);
    const SwapChainRecord& GetSwapChain(SwapChainHandle handle) const;
    BufferRecord& GetBuffer(BufferHandle handle);
    const BufferRecord& GetBuffer(BufferHandle handle) const;
    ShaderPipelineRecord& GetPipeline(ShaderPipelineHandle handle);
    const ShaderPipelineRecord& GetPipeline(ShaderPipelineHandle handle) const;
    void CreateSwapChainRenderTargets(SwapChainRecord& sc);
    void ReleaseSwapChainRenderTargets(SwapChainRecord& sc);
    uint64_t Signal();
    bool IsFenceComplete(uint64_t value) const;
    void WaitForFence(uint64_t value);
    void ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record);

    template<typename Record, typename Handle>
    Handle AllocateHandle(std::vector<Record>& records);

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_srvDescriptorSize = 0;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_nextFenceValue = 1;
    uint64_t m_lastSubmittedFenceValue = 0;

    std::vector<BufferRecord> m_buffers;
    std::vector<TextureRecord> m_textures;
    std::vector<uint32_t> m_freeTextureIndices;
    std::vector<ShaderPipelineRecord> m_pipelines;
    std::vector<SwapChainRecord> m_swapChains;
    std::vector<std::unique_ptr<Dx12CommandList>> m_commandLists;
};

template<typename Record, typename Handle>
Handle Dx12RHI::AllocateHandle(std::vector<Record>& records)
{
    for (uint32_t i = 0; i < records.size(); ++i)
    {
        if (!records[i].alive)
        {
            records[i].alive = true;
            return Handle(i + 1);
        }
    }

    Record record{};
    record.alive = true;
    records.push_back(std::move(record));
    return Handle(static_cast<uint32_t>(records.size()));
}

Dx12RHI::Dx12RHI()
{
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
    CheckResult(hr, "DXGI factory creation failed");

    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    CheckResult(hr, "D3D12 device creation failed");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 1;
    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    CheckResult(hr, "D3D12 command queue creation failed");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = kMaxBindlessTextures;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap));
    CheckResult(hr, "RHI bindless descriptor heap creation failed");
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    CheckResult(hr, "D3D12 fence creation failed");
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    MIGI_ASSERT(m_fenceEvent != nullptr, "D3D12 fence event creation failed");

    CreateRootSignature();
}

Dx12RHI::~Dx12RHI()
{
    WaitIdle();
    if (m_fenceEvent != nullptr)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void Dx12RHI::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kMaxBindlessTextures;
    textureRange.BaseShaderRegister = 2;
    textureRange.RegisterSpace = 0;
    textureRange.OffsetInDescriptorsFromTableStart = 0;

    std::array<D3D12_ROOT_PARAMETER, 5> params = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[3].Constants.ShaderRegister = 1;
    params[3].Constants.RegisterSpace = 0;
    params[3].Constants.Num32BitValues = 4;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable.NumDescriptorRanges = 1;
    params[4].DescriptorTable.pDescriptorRanges = &textureRange;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = static_cast<UINT>(params.size());
    desc.pParameters = params.data();
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    CheckResult(hr, "D3D12 root signature serialization failed");

    hr = m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
    CheckResult(hr, "D3D12 root signature creation failed");
}

ComPtr<ID3D12PipelineState> Dx12RHI::CreatePipelineState(const ShaderPipelineDesc& desc, BlendMode blendMode)
{
    MIGI_ASSERT(!desc.vertexShader.empty(), "Shader pipeline needs a vertex shader blob");
    MIGI_ASSERT(!desc.pixelShader.empty(), "Shader pipeline needs a pixel shader blob");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.NodeMask = 1;
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { desc.vertexShader.data(), desc.vertexShader.size_bytes() };
    psoDesc.PS = { desc.pixelShader.data(), desc.pixelShader.size_bytes() };
    psoDesc.BlendState = BlendDesc(blendMode);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = RasterizerDesc();
    psoDesc.DepthStencilState = DepthStencilDesc();
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ComPtr<ID3D12PipelineState> state;
    HRESULT hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&state));
    CheckResult(hr, "D3D12 graphics pipeline creation failed");
    return state;
}

Dx12RHI::BufferRecord& Dx12RHI::GetBuffer(BufferHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_buffers.size(), "Invalid RHI buffer handle");
    BufferRecord& record = m_buffers[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI buffer handle was destroyed");
    return record;
}

const Dx12RHI::BufferRecord& Dx12RHI::GetBuffer(BufferHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_buffers.size(), "Invalid RHI buffer handle");
    const BufferRecord& record = m_buffers[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI buffer handle was destroyed");
    return record;
}

Dx12RHI::ShaderPipelineRecord& Dx12RHI::GetPipeline(ShaderPipelineHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_pipelines.size(), "Invalid RHI pipeline handle");
    ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI pipeline handle was destroyed");
    return record;
}

const Dx12RHI::ShaderPipelineRecord& Dx12RHI::GetPipeline(ShaderPipelineHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_pipelines.size(), "Invalid RHI pipeline handle");
    const ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI pipeline handle was destroyed");
    return record;
}

Dx12RHI::SwapChainRecord& Dx12RHI::GetSwapChain(SwapChainHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_swapChains.size(), "Invalid RHI swapchain handle");
    SwapChainRecord& record = m_swapChains[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI swapchain handle was destroyed");
    return record;
}

const Dx12RHI::SwapChainRecord& Dx12RHI::GetSwapChain(SwapChainHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_swapChains.size(), "Invalid RHI swapchain handle");
    const SwapChainRecord& record = m_swapChains[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI swapchain handle was destroyed");
    return record;
}

BufferHandle Dx12RHI::CreateBuffer(const BufferDesc& desc)
{
    MIGI_ASSERT(desc.byteSize > 0, "RHI buffer byte size must be non-zero");

    BufferHandle handle = AllocateHandle<BufferRecord, BufferHandle>(m_buffers);
    BufferRecord& record = m_buffers[handle.id - 1];

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.byteSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&record.resource));
    CheckResult(hr, "D3D12 buffer creation failed");

    record.byteSize = desc.byteSize;
    if (desc.initialData != nullptr)
        UpdateBuffer(handle, desc.initialData, desc.byteSize, 0);

    return handle;
}

void Dx12RHI::DestroyBuffer(BufferHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    BufferRecord& record = GetBuffer(handle);
    record.resource.Reset();
    record.byteSize = 0;
    record.alive = false;
}

GpuAddress Dx12RHI::GetBufferGpuAddress(BufferHandle handle) const
{
    const BufferRecord& record = GetBuffer(handle);
    return record.resource->GetGPUVirtualAddress();
}

void Dx12RHI::UpdateBuffer(BufferHandle handle, const void* data, uint32_t byteSize, uint32_t byteOffset)
{
    MIGI_ASSERT(data != nullptr || byteSize == 0, "RHI buffer update data is null");
    BufferRecord& record = GetBuffer(handle);
    MIGI_ASSERT(byteOffset <= record.byteSize && byteSize <= record.byteSize - byteOffset, "RHI buffer update is out of bounds");

    if (byteSize == 0)
        return;

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = record.resource->Map(0, &readRange, &mapped);
    CheckResult(hr, "D3D12 buffer map failed");
    std::memcpy(static_cast<std::byte*>(mapped) + byteOffset, data, byteSize);
    record.resource->Unmap(0, nullptr);
}

TextureIndex Dx12RHI::CreateTexture(const TextureDesc& desc)
{
    MIGI_ASSERT(desc.width > 0 && desc.height > 0, "RHI texture dimensions must be non-zero");
    MIGI_ASSERT(desc.initialData != nullptr, "RHI texture initial data is required");
    MIGI_ASSERT(desc.format == Format::R8G8B8A8_UNORM, "Only RGBA8 textures are currently implemented");

    uint32_t index = 0;
    if (!m_freeTextureIndices.empty())
    {
        index = m_freeTextureIndices.back();
        m_freeTextureIndices.pop_back();
    }
    else
    {
        MIGI_ASSERT(m_textures.size() < kMaxBindlessTextures, "RHI bindless texture heap is full");
        index = static_cast<uint32_t>(m_textures.size());
        m_textures.push_back(TextureRecord{});
    }

    TextureRecord& record = m_textures[index];
    record.alive = true;

    D3D12_HEAP_PROPERTIES textureHeap = {};
    textureHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    textureHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    textureHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = desc.width;
    textureDesc.Height = desc.height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = ToDxgiFormat(desc.format);
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommittedResource(
        &textureHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&record.resource));
    CheckResult(hr, "D3D12 texture creation failed");

    const uint32_t srcPitch = desc.width * FormatByteSize(desc.format);
    const uint32_t uploadPitch = Align(srcPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    const uint32_t uploadSize = uploadPitch * desc.height;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = m_device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    CheckResult(hr, "D3D12 texture upload buffer creation failed");

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    hr = uploadBuffer->Map(0, &readRange, &mapped);
    CheckResult(hr, "D3D12 texture upload map failed");
    for (uint32_t y = 0; y < desc.height; ++y)
    {
        std::memcpy(
            static_cast<std::byte*>(mapped) + y * uploadPitch,
            static_cast<const std::byte*>(desc.initialData) + y * srcPitch,
            srcPitch);
    }
    uploadBuffer->Unmap(0, nullptr);

    ExecuteImmediate([&](ID3D12GraphicsCommandList* list) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = ToDxgiFormat(desc.format);
        src.PlacedFootprint.Footprint.Width = desc.width;
        src.PlacedFootprint.Footprint.Height = desc.height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = uploadPitch;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = record.resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(
            record.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &barrier);
    });

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = ToDxgiFormat(desc.format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(record.resource.Get(), &srvDesc, handle);

    return index;
}

void Dx12RHI::DestroyTexture(TextureIndex index)
{
    if (index == kInvalidTexture)
        return;

    MIGI_ASSERT(index < m_textures.size(), "Invalid RHI texture index");
    TextureRecord& record = m_textures[index];
    MIGI_ASSERT(record.alive, "RHI texture index was destroyed");
    WaitIdle();
    record.resource.Reset();
    record.alive = false;
    m_freeTextureIndices.push_back(index);
}

ShaderPipelineHandle Dx12RHI::CreateShaderPipeline(const ShaderPipelineDesc& desc)
{
    ShaderPipelineHandle handle = AllocateHandle<ShaderPipelineRecord, ShaderPipelineHandle>(m_pipelines);
    ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    record.opaqueState = CreatePipelineState(desc, BlendMode::Opaque);
    record.alphaState = CreatePipelineState(desc, BlendMode::Alpha);
    return handle;
}

void Dx12RHI::DestroyShaderPipeline(ShaderPipelineHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    ShaderPipelineRecord& record = GetPipeline(handle);
    record.opaqueState.Reset();
    record.alphaState.Reset();
    record.alive = false;
}

void Dx12RHI::CreateSwapChainRenderTargets(SwapChainRecord& sc)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = sc.bufferCount;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&sc.rtvHeap));
    CheckResult(hr, "D3D12 swapchain RTV heap creation failed");

    const UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = sc.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    sc.backBuffers.resize(sc.bufferCount);
    sc.rtvs.resize(sc.bufferCount);

    for (uint32_t i = 0; i < sc.bufferCount; ++i)
    {
        hr = sc.swapChain->GetBuffer(i, IID_PPV_ARGS(&sc.backBuffers[i]));
        CheckResult(hr, "D3D12 swapchain back buffer lookup failed");
        sc.rtvs[i] = rtvHandle;
        m_device->CreateRenderTargetView(sc.backBuffers[i].Get(), nullptr, sc.rtvs[i]);
        rtvHandle.ptr += rtvDescriptorSize;
    }
}

void Dx12RHI::ReleaseSwapChainRenderTargets(SwapChainRecord& sc)
{
    sc.backBuffers.clear();
    sc.rtvs.clear();
    sc.rtvHeap.Reset();
}

SwapChainHandle Dx12RHI::CreateSwapChain(const SwapChainDesc& desc)
{
    MIGI_ASSERT(desc.windowHandle != nullptr, "RHI swapchain needs a native window handle");

    SwapChainHandle handle = AllocateHandle<SwapChainRecord, SwapChainHandle>(m_swapChains);
    SwapChainRecord& record = m_swapChains[handle.id - 1];
    record.width = std::max(desc.width, 1u);
    record.height = std::max(desc.height, 1u);
    record.bufferCount = desc.bufferCount != 0 ? desc.bufferCount : kDefaultBackBufferCount;
    record.colorFormat = desc.colorFormat;
    record.fullscreen = desc.fullscreen;

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.BufferCount = record.bufferCount;
    swapDesc.Width = record.width;
    swapDesc.Height = record.height;
    swapDesc.Format = ToDxgiFormat(record.colorFormat);
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(
        m_queue.Get(),
        static_cast<HWND>(desc.windowHandle),
        &swapDesc,
        nullptr,
        nullptr,
        &swapChain1);
    CheckResult(hr, "DXGI swapchain creation failed");

    hr = swapChain1.As(record.swapChain);
    CheckResult(hr, "DXGI swapchain interface query failed");
    m_factory->MakeWindowAssociation(static_cast<HWND>(desc.windowHandle), DXGI_MWA_NO_WINDOW_CHANGES);
    if (record.fullscreen)
        record.swapChain->SetFullscreenState(TRUE, nullptr);

    CreateSwapChainRenderTargets(record);
    record.nextBackBuffer = record.swapChain->GetCurrentBackBufferIndex();
    return handle;
}

void Dx12RHI::ResizeSwapChain(SwapChainHandle handle, uint32_t width, uint32_t height, bool fullscreen)
{
    SwapChainRecord& record = GetSwapChain(handle);
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    if (record.width == width && record.height == height && record.fullscreen == fullscreen)
        return;

    WaitIdle();
    ReleaseSwapChainRenderTargets(record);
    if (record.fullscreen != fullscreen)
    {
        HRESULT fullscreenHr = record.swapChain->SetFullscreenState(fullscreen ? TRUE : FALSE, nullptr);
        CheckResult(fullscreenHr, "DXGI swapchain fullscreen change failed");
    }
    HRESULT hr = record.swapChain->ResizeBuffers(
        record.bufferCount,
        width,
        height,
        ToDxgiFormat(record.colorFormat),
        0);
    CheckResult(hr, "DXGI swapchain resize failed");
    record.width = width;
    record.height = height;
    record.fullscreen = fullscreen;
    CreateSwapChainRenderTargets(record);
    record.nextBackBuffer = record.swapChain->GetCurrentBackBufferIndex();
}

void Dx12RHI::DestroySwapChain(SwapChainHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    SwapChainRecord& record = GetSwapChain(handle);
    ReleaseSwapChainRenderTargets(record);
    if (record.fullscreen)
        record.swapChain->SetFullscreenState(FALSE, nullptr);
    record.swapChain.Reset();
    record.width = 0;
    record.height = 0;
    record.bufferCount = 0;
    record.alive = false;
}

bool Dx12RHI::IsFenceComplete(uint64_t value) const
{
    return value == 0 || m_fence->GetCompletedValue() >= value;
}

uint64_t Dx12RHI::Signal()
{
    const uint64_t value = m_nextFenceValue++;
    HRESULT hr = m_queue->Signal(m_fence.Get(), value);
    CheckResult(hr, "D3D12 command queue signal failed");
    m_lastSubmittedFenceValue = value;
    return value;
}

void Dx12RHI::WaitForFence(uint64_t value)
{
    if (IsFenceComplete(value))
        return;

    HRESULT hr = m_fence->SetEventOnCompletion(value, m_fenceEvent);
    CheckResult(hr, "D3D12 fence wait setup failed");
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void Dx12RHI::WaitIdle()
{
    if (m_lastSubmittedFenceValue == 0)
        return;

    WaitForFence(m_lastSubmittedFenceValue);
}

void Dx12RHI::ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList*)>& record)
{
    ComPtr<ID3D12CommandAllocator> allocator;
    HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    CheckResult(hr, "D3D12 immediate command allocator creation failed");

    ComPtr<ID3D12GraphicsCommandList> list;
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
    CheckResult(hr, "D3D12 immediate command list creation failed");

    record(list.Get());
    hr = list->Close();
    CheckResult(hr, "D3D12 immediate command list close failed");

    ID3D12CommandList* lists[] = { list.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    const uint64_t value = Signal();
    WaitForFence(value);
}

Dx12RHI::Dx12CommandList::Dx12CommandList(Dx12RHI& owner, ID3D12Device* device)
    : owner(owner)
{
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    CheckResult(hr, "D3D12 command allocator creation failed");

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
    CheckResult(hr, "D3D12 command list creation failed");
    hr = list->Close();
    CheckResult(hr, "D3D12 initial command list close failed");
}

bool Dx12RHI::Dx12CommandList::CanReuse(ID3D12Fence* fence) const
{
    return !recording && (!submitted || fence->GetCompletedValue() >= submittedFenceValue);
}

void Dx12RHI::Dx12CommandList::ResetForRecording()
{
    HRESULT hr = allocator->Reset();
    CheckResult(hr, "D3D12 command allocator reset failed");
    hr = list->Reset(allocator.Get(), nullptr);
    CheckResult(hr, "D3D12 command list reset failed");

    ID3D12DescriptorHeap* heaps[] = { owner.m_srvHeap.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    activeSwapChain = {};
    activeBackBuffer = 0;
    activePipeline = {};
    activeBlendMode = BlendMode::Opaque;
    renderPassOpen = false;
    recording = true;
    submitted = false;
    submittedFenceValue = 0;
}

void Dx12RHI::Dx12CommandList::MarkSubmitted(uint64_t fenceValue)
{
    recording = false;
    submitted = true;
    submittedFenceValue = fenceValue;
}

void Dx12RHI::Dx12CommandList::BeginRenderPass(SwapChainHandle target, const ClearColor& clear)
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(!renderPassOpen, "RHI render pass is already open");

    auto& sc = owner.GetSwapChain(target);
    activeSwapChain = target;
    activeBackBuffer = sc.nextBackBuffer;
    sc.nextBackBuffer = (sc.nextBackBuffer + 1) % sc.bufferCount;

    D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(
        sc.backBuffers[activeBackBuffer].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1, &barrier);

    const float color[4] = { clear.r, clear.g, clear.b, clear.a };
    list->ClearRenderTargetView(sc.rtvs[activeBackBuffer], color, 0, nullptr);
    list->OMSetRenderTargets(1, &sc.rtvs[activeBackBuffer], FALSE, nullptr);
    renderPassOpen = true;
}

void Dx12RHI::Dx12CommandList::EndRenderPass()
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(renderPassOpen, "RHI render pass is not open");

    auto& sc = owner.GetSwapChain(activeSwapChain);
    D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(
        sc.backBuffers[activeBackBuffer].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    list->ResourceBarrier(1, &barrier);
    renderPassOpen = false;
}

void Dx12RHI::Dx12CommandList::SetViewport(const Viewport& vp)
{
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = vp.x;
    viewport.TopLeftY = vp.y;
    viewport.Width = vp.width;
    viewport.Height = vp.height;
    viewport.MinDepth = vp.minDepth;
    viewport.MaxDepth = vp.maxDepth;
    list->RSSetViewports(1, &viewport);
}

void Dx12RHI::Dx12CommandList::SetScissor(const Scissor& sc)
{
    D3D12_RECT rect = {};
    rect.left = sc.x;
    rect.top = sc.y;
    rect.right = sc.x + static_cast<LONG>(sc.width);
    rect.bottom = sc.y + static_cast<LONG>(sc.height);
    list->RSSetScissorRects(1, &rect);
}

void Dx12RHI::Dx12CommandList::SetBlendMode(BlendMode mode)
{
    activeBlendMode = mode;
    ApplyPipelineState();
}

void Dx12RHI::Dx12CommandList::BindShaderPipeline(ShaderPipelineHandle pipeline)
{
    activePipeline = pipeline;
    list->SetGraphicsRootSignature(owner.m_rootSignature.Get());
    list->SetGraphicsRootDescriptorTable(4, owner.m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    ApplyPipelineState();
}

void Dx12RHI::Dx12CommandList::ApplyPipelineState()
{
    if (!activePipeline.IsValid())
        return;

    const ShaderPipelineRecord& pipeline = owner.GetPipeline(activePipeline);
    ID3D12PipelineState* state = activeBlendMode == BlendMode::Alpha
        ? pipeline.alphaState.Get()
        : pipeline.opaqueState.Get();
    list->SetPipelineState(state);
}

void Dx12RHI::Dx12CommandList::DrawIndexed(const DrawIndexedDesc& desc)
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(activePipeline.IsValid(), "RHI draw needs a shader pipeline");
    MIGI_ASSERT(desc.vertexBuffer != kNullGpuAddress, "RHI draw needs a vertex buffer");
    MIGI_ASSERT(desc.indexBuffer != kNullGpuAddress, "RHI draw needs an index buffer");

    const uint32_t indexSize = IndexByteSize(desc.indexFormat);
    MIGI_ASSERT(indexSize != 0, "RHI draw has unsupported index format");

    struct DrawParams
    {
        uint32_t firstIndex = 0;
        int32_t vertexOffset = 0;
        uint32_t indexFormat = 0;
        uint32_t padding = 0;
    };

    DrawParams params{};
    params.firstIndex = desc.firstIndex;
    params.vertexOffset = desc.vertexOffset;
    params.indexFormat = desc.indexFormat == Format::R32_UINT ? 1u : 0u;

    list->SetGraphicsRootShaderResourceView(0, desc.vertexBuffer);
    list->SetGraphicsRootShaderResourceView(1, desc.indexBuffer);
    if (desc.userData != kNullGpuAddress)
        list->SetGraphicsRootConstantBufferView(2, desc.userData);
    list->SetGraphicsRoot32BitConstants(3, 4, &params, 0);
    list->DrawInstanced(desc.indexCount, 1, 0, 0);
}

CommandList* Dx12RHI::BeginCommandList()
{
    for (auto& commandList : m_commandLists)
    {
        if (commandList->CanReuse(m_fence.Get()))
        {
            commandList->ResetForRecording();
            return commandList.get();
        }
    }

    auto commandList = std::make_unique<Dx12CommandList>(*this, m_device.Get());
    Dx12CommandList* result = commandList.get();
    m_commandLists.push_back(std::move(commandList));
    result->ResetForRecording();
    return result;
}

void Dx12RHI::Submit(CommandList* cmd)
{
    auto* dxCmd = static_cast<Dx12CommandList*>(cmd);
    MIGI_ASSERT(dxCmd != nullptr && dxCmd->recording, "RHI submit needs a recording command list");

    HRESULT hr = dxCmd->Get()->Close();
    CheckResult(hr, "D3D12 command list close failed");

    ID3D12CommandList* lists[] = { dxCmd->Get() };
    m_queue->ExecuteCommandLists(1, lists);
    dxCmd->MarkSubmitted(Signal());
}

void Dx12RHI::Present(SwapChainHandle handle, uint32_t syncInterval)
{
    SwapChainRecord& record = GetSwapChain(handle);
    HRESULT hr = record.swapChain->Present(syncInterval, 0);
    CheckResult(hr, "DXGI swapchain present failed");
}

std::unique_ptr<RHI> RHI::CreateDX12()
{
    return std::make_unique<Dx12RHI>();
}

} // namespace drgn
