#include <fw/DefaultFramePipeline.h>

#include <fnd/Window.h>
#include <fw/IFramePipeline.h>
#include <fw/DearImGuiManager.h>
#include <fw/IGame.h>
#include <fw/Renderer.h>

#include <imgui/imgui.h>

namespace engine
{

namespace {

void CopyImDrawData(const ImDrawData* fromData, engine::FrameData* frameData) {
    frameData->drawData.Valid = fromData->Valid;
    frameData->drawData.CmdListsCount = fromData->CmdListsCount;
    frameData->drawData.TotalIdxCount = fromData->TotalIdxCount;
    frameData->drawData.TotalVtxCount = fromData->TotalVtxCount;
    frameData->drawData.DisplayPos = fromData->DisplayPos;
    frameData->drawData.DisplaySize = fromData->DisplaySize;
    frameData->drawData.FramebufferScale = fromData->FramebufferScale;

    for (size_t i = 0; i < frameData->drawData.CmdListsCount; ++i) {
        frameData->drawData.DrawLists.emplace_back();

        ImDrawList& fromList = *fromData->CmdLists[i];
        engine::DrawList& toList = frameData->drawData.DrawLists.back();
        toList.Flags = fromList.Flags;
        toList.CmdBuffer = fromList.CmdBuffer;
        toList.IdxBuffer = fromList.IdxBuffer;
        toList.VtxBuffer = fromList.VtxBuffer;
        ASSERT_MSG(toList.CmdBuffer.size() == fromList.CmdBuffer.size(), "ImGUI cmdbuffer data copy failed");
        ASSERT_MSG(toList.IdxBuffer.size() == fromList.IdxBuffer.size(), "ImGUI idxbuffer data copy failed");
        ASSERT_MSG(toList.VtxBuffer.size() == fromList.VtxBuffer.size(), "ImGUI vtxbuffer data copy failed");
    }
    ASSERT_MSG(frameData->drawData.DrawLists.size() == frameData->drawData.CmdListsCount, "ImGUI data copy failed");
}

}


DefaultFramePipeline::DefaultFramePipeline(DearImGuiManager& imguiManager, Renderer& renderer, IGame& game) 
    : m_imguiManager(imguiManager)
    , m_renderer(renderer)
    , m_game(game)
{

}
void DefaultFramePipeline::Update(FrameData& frameData) {
    // UPDATE
    Window::GetMainWindow().GetSize(&frameData.width, &frameData.height);

    // Update ImGUI
    for (auto& msg : Window::GetMainWindow().PopMsg()) {
        m_imguiManager.WndProcHandler(msg.hWnd, msg.msg, msg.wParam, msg.lParam);
    }

    // Start the Dear ImGui frame
    m_imguiManager.Update(frameData.deltatime);

    // GAME
    m_game.Update(frameData);

    ImGui::Render(); // Prepare ImDrawData for the current frame
    ImDrawData* drawData = ImGui::GetDrawData(); // Valid until DearImGuiManager::Update()
    CopyImDrawData(drawData, &frameData);
}

void DefaultFramePipeline::Render(FrameData& frameData) {
    m_renderer.Render(frameData);
}
void DefaultFramePipeline::Kick(const FrameData& frameData) {
    m_renderer.Kick(frameData);

}
void DefaultFramePipeline::Clean(const FrameData& frameData) {
    m_renderer.Clean(frameData);
    RandomWorkload(1000);
}
} // namespace engine
