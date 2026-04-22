#pragma once

#include <fnd/Util.h>
#include <fnd/Job.h>
#include <fw/FrameData.h>
#include <app/Game.h>
#include <fnd/Window.h>
#include <fnd/Profiler.h>


namespace engine
{
struct FrameData;
}

namespace app
{

namespace {
const char* g_frame_strategy[4]{
    "3 Frame Latency",
    "2 Frame Latency (CPU bound)",
    "2 Frame Latency (GPU bound)",
    "1 Frame Latency"
};
}

Game::Game() {
    m_lastQuitTime = engine::Window::GetMainWindow().GetLastQuitTime();
}

// Create some 
void UpdateSubPosition(int i) {
    PROFILE_SCOPE("UpdateSubPosition");

    engine::RandomWorkload(i * 50);
}
void UpdatePosition(int i) {
    UpdateSubPosition(i);
    engine::RandomWorkload(500);
    UpdateSubPosition(2 * i);
}

void Game::Update(engine::FrameData& frameData) 
{
    engine::Time startTime = engine::Clock::now();

    {
        PROFILE_SCOPE("Game Jobs");

        engine::JobCounter handle;
        for (int i = 0; i < frameData.gamejobNumber; i++) {
            engine::Job::Dispatch("UpdatePosition Job", handle, [i] {
                UpdatePosition(i);
            });
        }
        engine::Job::Wait(handle);
    }
    frameData.result.stop = m_lastQuitTime != engine::Window::GetMainWindow().GetLastQuitTime();
    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (m_show_demo_window)
        ImGui::ShowDemoWindow(&m_show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &m_show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &m_show_another_window);

        ImGui::SliderFloat("float", &m_f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&m_clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            m_counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", m_counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::Checkbox("Fullscreen", &m_fullscreen);
        ImGui::Checkbox("Vsync", &m_vsync);
        int maxFrameLatency = frameData.maxFrameLatency;
        ImGui::SliderInt("MaxFrameLatency", &maxFrameLatency, 1, 3);
        frameData.result.maxFrameLatency = maxFrameLatency;

        int gameStageUs = frameData.gameStageUs;
        ImGui::SliderInt("Game Stade (us)", &gameStageUs, 1, 35000);
        frameData.result.gameStageUs = gameStageUs;

        int renderStageUs = frameData.renderStageUs;
        ImGui::SliderInt("Render Stage (us)", &renderStageUs, 1, 35000);
        frameData.result.renderStageUs = renderStageUs;

        int selectedStrategy = m_selectedStrategy;
        ImGui::Combo("Frame Strategy", &selectedStrategy, g_frame_strategy, IM_ARRAYSIZE(g_frame_strategy));
        if (selectedStrategy != m_selectedStrategy) {
            m_selectedStrategy = selectedStrategy;
            switch (m_selectedStrategy) {
            case 0: // 3 Frame Latency
                frameData.result.maxFrameLatency = 3;
                frameData.result.gameStageUs = engine::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = engine::FrameUpdateResult::DefaultRenderStageUs;
                break;
            case 1: // 2 Frame Latency (CPU bound)
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = engine::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = 8000;
                break;
            case 2: // 2 Frame Latency (GPU bound)
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = 8000;
                frameData.result.renderStageUs = engine::FrameUpdateResult::DefaultRenderStageUs;
                break;
            case 3: // 1 Frame Latency
                frameData.result.maxFrameLatency = 1;
                frameData.result.gameStageUs = 4000;
                frameData.result.renderStageUs = 4000;
                break;
            }
        }

        ImGui::End();
    }

    // 3. Show another simple window.
    if (m_show_another_window)
    {
        ImGui::Begin("Another Window", &m_show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            m_show_another_window = false;
        ImGui::End();
    }
    frameData.fullscreen = m_fullscreen;
    frameData.vsync = m_vsync;

    {
        engine::Time beforeWorkloadTime = engine::Clock::now();
        PROFILE_SCOPE("Game Workload");
        engine::RandomWorkload(frameData.gameStageUs - engine::to_us(beforeWorkloadTime - startTime)); // random workload of 5ms to be visible on profiler
    }
}

}