#pragma once

#include <fw/IFramePipeline.h>

namespace engine
{

class IGame;
class Renderer;
class DearImGuiManager;

/* 
    IFramePipeline hold the stage making up a frame. 
    Those stages may run in paralle depending on the "mainloop strategy"
*/
class DefaultFramePipeline : public IFramePipeline
{
public:
    DefaultFramePipeline(DearImGuiManager& imguiManager, Renderer& renderer, IGame& game);

    virtual void Update(FrameData& frameData);
    virtual void Render(FrameData& frame);
    virtual void Kick(const FrameData& frameData);
    virtual void Clean(const FrameData& frameData);

private:
    DearImGuiManager& m_imguiManager;
    Renderer& m_renderer;
    IGame& m_game;
};

} // namespace engine
