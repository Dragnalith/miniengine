#pragma once

#include <fnd/Util.h>
#include <fw/IGame.h>

namespace engine
{
struct FrameData;
}

namespace app
{

class Game : public engine::IGame
{
public:
    Game();
    virtual void Update(engine::FrameData& frameData);

private:
    engine::Clock::time_point m_lastQuitTime;

    bool m_fullscreen = false;
    bool m_vsync = true;
    bool m_show_demo_window = true;
    bool m_show_another_window = false;
    int m_selectedStrategy = 0;
    float m_f = 0.0f;
    int m_counter = 0;
    ImVec4 m_clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

}