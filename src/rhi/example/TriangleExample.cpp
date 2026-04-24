// Minimal application rendering a single colored triangle through drgn::RHI.
//
// The shader does not use an input layout. DrawIndexed passes GPU addresses
// for the vertex buffer, index buffer, and user data; by convention the backend
// exposes the vertex buffer at the shader-visible location used below.

#include <rhi/RHI.h>

#include <windows.h>

#include <cstdint>
#include <memory>

namespace
{

constexpr wchar_t kWndClass[] = L"DrgnRhiTriangle";
constexpr int     kWidth      = 1280;
constexpr int     kHeight     = 720;

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

constexpr char kHLSL[] = R"HLSL(
struct Vertex
{
    float2 pos;
    float3 col;
};

// RHI convention: DrawIndexed(vertexBuffer, indexBuffer, userData, indexCount)
// binds vertexBuffer here. The index buffer is consumed by the backend draw.
StructuredBuffer<Vertex> g_Vertices : register(t0);

struct VSOut { float4 pos : SV_Position; float3 col : COLOR; };

VSOut VSMain(uint vid : SV_VertexID)
{
    Vertex v = g_Vertices[vid];

    VSOut o;
    o.pos = float4(v.pos, 0.0, 1.0);
    o.col = v.col;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return float4(i.col, 1.0);
}
)HLSL";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateAppWindow(HINSTANCE hinst)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWndClass, L"drgn::RHI triangle",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                kWidth, kHeight,
                                nullptr, nullptr, hinst, nullptr);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    return hwnd;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int)
{
    HWND hwnd = CreateAppWindow(hinst);

    std::unique_ptr<drgn::RHI> rhi = drgn::RHI::CreateDX12();

    // --- Swap chain ---
    drgn::SwapChainDesc scDesc{};
    scDesc.windowHandle = hwnd;
    scDesc.width        = kWidth;
    scDesc.height       = kHeight;
    drgn::SwapChainHandle swapChain = rhi->CreateSwapChain(scDesc);

    // --- Vertex and index buffers ---
    drgn::BufferDesc vbDesc{};
    vbDesc.byteSize    = sizeof(kVertices);
    vbDesc.initialData = kVertices;
    drgn::BufferHandle vb = rhi->CreateBuffer(vbDesc);
    drgn::GpuAddress vbGpu = rhi->GetBufferGpuAddress(vb);

    drgn::BufferDesc ibDesc{};
    ibDesc.byteSize    = sizeof(kIndices);
    ibDesc.initialData = kIndices;
    drgn::BufferHandle ib = rhi->CreateBuffer(ibDesc);
    drgn::GpuAddress ibGpu = rhi->GetBufferGpuAddress(ib);

    // --- Pipeline ---
    drgn::ShaderPipelineDesc pipeDesc{};
    pipeDesc.vertexShaderHLSL = kHLSL;
    pipeDesc.pixelShaderHLSL  = kHLSL;
    drgn::ShaderPipelineHandle pipeline = rhi->CreateShaderPipeline(pipeDesc);

    // --- Main loop ---
    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const float w = static_cast<float>(rc.right  - rc.left);
        const float h = static_cast<float>(rc.bottom - rc.top);

        drgn::CommandList* cmd = rhi->BeginCommandList();
        cmd->BeginRenderPass(swapChain, drgn::ClearColor{ 0.10f, 0.10f, 0.15f, 1.0f });
        cmd->SetViewport({ 0.f, 0.f, w, h, 0.f, 1.f });
        cmd->SetScissor ({ 0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h) });
        cmd->SetBlendMode(drgn::BlendMode::Opaque);
        cmd->BindShaderPipeline(pipeline);
        cmd->DrawIndexed(vbGpu, ibGpu, drgn::kNullGpuAddress, 3);
        cmd->EndRenderPass();
        rhi->Submit(cmd);
        rhi->Present(swapChain, 1);
    }

    rhi->WaitIdle();
    rhi->DestroyShaderPipeline(pipeline);
    rhi->DestroyBuffer(ib);
    rhi->DestroyBuffer(vb);
    rhi->DestroySwapChain(swapChain);
    return 0;
}
