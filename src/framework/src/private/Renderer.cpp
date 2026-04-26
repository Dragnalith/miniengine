#include <fw/Renderer.h>

#include <fnd/Assert.h>
#include <fnd/Job.h>
#include <fnd/Profiler.h>
#include <fnd/Util.h>
#include <fnd/Window.h>
#include <fw/FrameData.h>
#include <private/DearImGuiRenderer.h>
#include <rhi/RHI.h>

#include <algorithm>
#include <memory>

namespace migi
{

struct RenderContextManaged : public RenderContext
{
    bool isUsed = false;
};

struct RendererImpl
{
    static constexpr int NUM_FRAMES_IN_FLIGHT = 8;

    RendererImpl(DearImGuiManager& manager)
        : rhi(drgn::RHI::Create())
        , imguiRenderer(*rhi, manager)
    {
        Int2 windowSize = WindowGetSize();
        width = std::max(windowSize.x, 1);
        height = std::max(windowSize.y, 1);

        drgn::SwapChainDesc desc{};
        desc.windowHandle = WindowGetNativeHandle();
        desc.width = static_cast<uint32_t>(width);
        desc.height = static_cast<uint32_t>(height);
        desc.bufferCount = 2;
        desc.fullscreen = fullscreen;
        swapChain = rhi->CreateSwapChain(desc);

        for (uint32_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
            frameContext[i].index = i;
    }

    ~RendererImpl()
    {
        rhi->WaitIdle();
        rhi->DestroySwapChain(swapChain);
    }

    std::unique_ptr<drgn::RHI> rhi;
    drgn::SwapChainHandle swapChain;
    int width = 0;
    int height = 0;
    bool fullscreen = false;
    DearImGuiRenderer imguiRenderer;
    RenderContextManaged frameContext[NUM_FRAMES_IN_FLIGHT] = {};
    JobCounter renderStarted;

    bool NeedResize(int newWidth, int newHeight, bool newFullscreen) const
    {
        return width != std::max(newWidth, 1) ||
            height != std::max(newHeight, 1) ||
            fullscreen != newFullscreen;
    }

    void Resize(int newWidth, int newHeight, bool newFullscreen)
    {
        width = std::max(newWidth, 1);
        height = std::max(newHeight, 1);
        fullscreen = newFullscreen;
        rhi->ResizeSwapChain(
            swapChain,
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            fullscreen);
    }

    RenderContext* Allocate(uint64_t frameIndex)
    {
        RenderContextManaged* frameCtx = &frameContext[frameIndex % NUM_FRAMES_IN_FLIGHT];
        MIGI_ASSERT(frameCtx->isUsed == false, "RenderContext is already used");
        frameCtx->isUsed = true;
        frameCtx->frameIndex = frameIndex;
        return frameCtx;
    }

    void Free(RenderContext* ctx)
    {
        for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        {
            if (ctx == &frameContext[i])
            {
                MIGI_ASSERT(ctx->frameIndex == frameContext[i].frameIndex, "RenderContext free issue");
                frameContext[i].isUsed = false;
                frameContext[i].frameIndex = 0xdeadbeef;
                frameContext[i].commandList = nullptr;
            }
        }
    }
};

Renderer::Renderer(DearImGuiManager& manager)
    : m_impl(manager)
{
}

Renderer::~Renderer()
{
    m_impl->rhi->WaitIdle();
}

void RenderOneObject(int i)
{
    PROFILE_SCOPE("RenderOneObject");
    migi::RandomWorkload(150, (i * 10) % 80);
}

void RenderMultipleObject(int N)
{
    PROFILE_SCOPE("RenderMultipleObject");

    migi::RandomWorkload(200);
    for (int i = 0; i < N; i++)
        RenderOneObject(100);
    migi::RandomWorkload(100);
}

void Renderer::Render(FrameData& frameData)
{
    migi::TimePoint startTime = migi::TimePoint::Now();

    if (m_impl->NeedResize(frameData.width, frameData.height, frameData.fullscreen))
    {
        Job::Wait(m_impl->renderStarted);
        m_impl->Resize(frameData.width, frameData.height, frameData.fullscreen);
    }

    m_impl->renderStarted.Add(1);
    RenderContext* renderCtx = m_impl->Allocate(frameData.frameIndex);
    frameData.renderContext = renderCtx;

    drgn::RHI& rhi = *m_impl->rhi;
    drgn::CommandList* commandList = rhi.BeginCommandList();
    renderCtx->commandList = commandList;

    const uint32_t width = static_cast<uint32_t>(std::max(frameData.width, 1));
    const uint32_t height = static_cast<uint32_t>(std::max(frameData.height, 1));

    commandList->BeginRenderPass(m_impl->swapChain, drgn::ClearColor{ 0.45f, 0.55f, 0.60f, 1.0f });
    commandList->SetViewport({
        0.0f,
        0.0f,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0f,
        1.0f,
    });
    commandList->SetScissor({ 0, 0, width, height });

    m_impl->imguiRenderer.Render(*commandList, frameData.drawData);
    commandList->EndRenderPass();

    {
        PROFILE_SCOPE("Renderer Jobs");
        migi::JobCounter handle;
        for (int i = 0; i < frameData.rendererjobNumber; i++)
        {
            migi::Job::Dispatch("RenderObject Job", handle, [i] {
                RenderMultipleObject(i % 3);
            });
        }
        migi::Job::Wait(handle);
    }

    {
        migi::TimePoint beforeWorkloadTime = migi::TimePoint::Now();
        PROFILE_SCOPE("Renderer Workload");
        RandomWorkload(frameData.renderStageUs - static_cast<int>((beforeWorkloadTime - startTime).ToMicroseconds()));
    }
}

void Renderer::Kick(const FrameData& frameData)
{
    m_impl->rhi->Submit(frameData.renderContext->commandList);
    m_impl->rhi->Present(m_impl->swapChain, frameData.vsync ? 1 : 0);
    PROFILE_FRAME("CPU Present");

    m_impl->Free(frameData.renderContext);
    PROFILE_FRAME("GPU Present");
    m_impl->renderStarted.Sub(1);
}

void Renderer::Clean(const FrameData&)
{
}

}
