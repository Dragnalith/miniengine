#pragma once

#include <fnd/Util.h>
#include <fw/IGame.h>

#include <cstdint>

namespace migi
{
struct FrameData;
}

namespace app
{

class Game : public migi::IGame
{
public:
    Game();
    virtual void Update(migi::FrameData& frameData);

    // ---- Memory Match board dimensions ----------------------------------
    static constexpr int kGridSide = 4;                         // 4x4 board
    static constexpr int kCardCount = kGridSide * kGridSide;    // 16 cards
    static constexpr int kPairCount = kCardCount / 2;           // 8 pairs

private:
    // Strictly tap-driven state machine. No timers, nothing auto-advances.
    enum class Phase
    {
        SelectFirst,   // no card revealed; tap a card to reveal the first
        SelectSecond,  // one card revealed; tap another to reveal the second
        Resolve,       // two cards revealed; tap anywhere to resolve them
        Won,           // every pair matched; tap anywhere to start a new game
    };

    void NewGame();

    Phase m_phase = Phase::SelectFirst;
    int m_symbols[kCardCount] = {};   // symbol id (0..kPairCount-1) per card
    bool m_matched[kCardCount] = {};  // permanently face-up once matched
    int m_first = -1;                 // index of first revealed card
    int m_second = -1;                // index of second revealed card
    int m_pairsFound = 0;
    int m_moves = 0;
    unsigned m_rngState = 0;          // tiny xorshift RNG state
    bool m_started = false;           // shuffle lazily on the first frame

    // ---- Debug / test ImGui windows (disabled, kept for testing) --------
    uint64_t m_lastClosePressEventIndex = 0;

    bool m_fullscreen = false;
    bool m_vsync = true;
    bool m_show_demo_window = true;
    bool m_demoWindowPlaced = false;
    bool m_show_another_window = false;
    int m_selectedStrategy = 0;
    float m_f = 0.0f;
    int m_counter = 0;
    ImVec4 m_clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

}
