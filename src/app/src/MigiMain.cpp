// TODO
// - Why there is there latency during resize?
// - Why does it crash if compile with NDEBUG? (timing or synchronization issue?)
// - re-use fiber
// - renderer independent from the swapchain
// - Input independent from Win32
//

#include <imgui/imgui.h>

#include <fnd/Window.h>
#include <fnd/MigiMain.h>
#include <fw/DearImGuiManager.h>
#include <fw/Renderer.h>
#include <fw/FrameData.h>
#include <fw/DefaultFramePipeline.h>
#include <fw/FrameManager.h>
#include <app/Game.h>

#include <iostream>

void MigiMain()
{
    migi::WindowSetTitle("MiniEngine App");

    migi::DearImGuiManager imguiManager;
    migi::Renderer renderer(imguiManager);
    app::Game game;
    migi::DefaultFramePipeline pipeline(imguiManager, renderer, game);
    migi::FrameManager frameManager(pipeline);

    frameManager.Start();
}
