// TODO
// - Why there is there latency during resize?
// - Why does it crash if compile with NDEBUG? (timing or synchronization issue?)
// - re-use fiber
// - renderer independent from the swapchain
// - Input independent from Win32
//

#include <imgui/imgui.h>

#include <fnd/Window.h>
#include <fnd/AppMain.h>
#include <fw/RenderDevice.h>
#include <fw/SwapChain.h>
#include <fw/DearImGuiManager.h>
#include <fw/Renderer.h>
#include <fw/FrameData.h>
#include <fw/DefaultFramePipeline.h>
#include <fw/FrameManager.h>
#include <app/Game.h>

#include <iostream>

void AppMain()
{
    engine::RenderDevice renderDevice;
    engine::SwapChain swapChain(renderDevice);
    engine::DearImGuiManager imguiManager;
    engine::Renderer renderer(renderDevice, swapChain, imguiManager);
    app::Game game;
    engine::DefaultFramePipeline pipeline(imguiManager, renderer, game);
    engine::FrameManager frameManager(pipeline);

    frameManager.Start();
}