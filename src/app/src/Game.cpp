#pragma once

#include <fnd/Util.h>
#include <fnd/Job.h>
#include <fw/FrameData.h>
#include <app/Game.h>
#include <fnd/Window.h>
#include <fnd/Profiler.h>


namespace migi
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
    m_lastClosePressEventIndex = migi::WindowGetLastClosePressEventIndex();
}

// Create some
void UpdateSubPosition(int i) {
    PROFILE_SCOPE("UpdateSubPosition");

    migi::RandomWorkload(i * 50);
}
void UpdatePosition(int i) {
    UpdateSubPosition(i);
    migi::RandomWorkload(500);
    UpdateSubPosition(2 * i);
}

void Game::Update(migi::FrameData& frameData)
{
    migi::TimePoint startTime = migi::TimePoint::Now();

    frameData.result.stop = m_lastClosePressEventIndex != migi::WindowGetLastClosePressEventIndex();

    // Default the tool windows to one third down the screen, independent of the
    // resolution. FirstUseEver only seeds the initial placement, so the user can
    // still drag the windows afterwards.
    const migi::Int2 windowSize = migi::WindowGetSize();
    const float oneThirdY = static_cast<float>(windowSize.y) / 3.0f;
    const float marginX = static_cast<float>(windowSize.x) * 0.02f;

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (m_show_demo_window)
    {
        ImGui::ShowDemoWindow(&m_show_demo_window);
        // ShowDemoWindow seeds its own FirstUseEver position internally, so a
        // SetNextWindowPos before it would be overwritten. Override the placement
        // once, right after the demo window has been created this frame.
        if (!m_demoWindowPlaced)
        {
            // ShowDemoWindow seeds the demo window to 550px wide (FirstUseEver),
            // so center it horizontally on the screen while keeping the
            // one-third-down vertical anchor.
            constexpr float kDemoWidth = 550.0f;
            const float demoX = (static_cast<float>(windowSize.x) - kDemoWidth) * 0.5f;
            ImGui::SetWindowPos("Dear ImGui Demo", ImVec2(demoX, oneThirdY));
            m_demoWindowPlaced = true;
        }
    }

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        ImGui::SetNextWindowPos(ImVec2(marginX, oneThirdY), ImGuiCond_FirstUseEver);
        // Default to a third of the screen width (auto height) so the window
        // isn't squished flat, independent of resolution.
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowSize.x) * 0.33f, 0.0f), ImGuiCond_FirstUseEver);
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

        ImGui::Separator();
        ImGui::Text("Frame Timings (avg ms)");
        if (ImGui::BeginTable("FrameMetrics", 2,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Stage");
            ImGui::TableSetupColumn("Avg (ms)");
            ImGui::TableHeadersRow();
            for (int i = 0; i < migi::kFrameMetricCount; ++i)
            {
                const float avgMs =
                    frameData.metrics.AverageUs(static_cast<migi::FrameMetric>(i)) / 1000.0f;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(migi::kFrameMetricNames[i]);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", avgMs);
            }
            ImGui::EndTable();
        }

        int selectedStrategy = m_selectedStrategy;
        ImGui::Combo("Frame Strategy", &selectedStrategy, g_frame_strategy, IM_ARRAYSIZE(g_frame_strategy));
        if (selectedStrategy != m_selectedStrategy) {
            m_selectedStrategy = selectedStrategy;
            switch (m_selectedStrategy) {
            case 0: // 3 Frame Latency
                frameData.result.maxFrameLatency = 3;
                frameData.result.gameStageUs = migi::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = migi::FrameUpdateResult::DefaultRenderStageUs;
                break;
            case 1: // 2 Frame Latency (CPU bound)
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = migi::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = 8000;
                break;
            case 2: // 2 Frame Latency (GPU bound)
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = 8000;
                frameData.result.renderStageUs = migi::FrameUpdateResult::DefaultRenderStageUs;
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
        ImGui::SetNextWindowPos(ImVec2(marginX + 40.0f, oneThirdY + 40.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Another Window", &m_show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            m_show_another_window = false;
        ImGui::End();
    }
    frameData.fullscreen = m_fullscreen;
    frameData.vsync = m_vsync;
}

}
