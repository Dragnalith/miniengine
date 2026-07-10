#include <fnd/Util.h>
#include <fnd/Job.h>
#include <fw/FrameData.h>
#include <app/Game.h>
#include <fnd/Window.h>
#include <fnd/Profiler.h>

#include <cmath>
#include <cstdio>
#include <cfloat>


namespace migi
{
struct FrameData;
}

namespace app
{

namespace {

// ---------------------------------------------------------------------------
// Symbol palette: each of the 8 pairs has a distinct shape *and* colour so it
// stays readable even for colour-blind players.
// ---------------------------------------------------------------------------
enum SymbolShape
{
    Shape_Circle,
    Shape_Square,
    Shape_Triangle,
    Shape_Star,
    Shape_Diamond,
    Shape_Plus,
    Shape_Hexagon,
    Shape_Ring,
};

struct Symbol
{
    SymbolShape shape;
    ImU32 color;
};

const Symbol g_symbols[Game::kPairCount] = {
    { Shape_Circle,   IM_COL32(231,  76,  60, 255) }, // red
    { Shape_Square,   IM_COL32( 46, 204, 113, 255) }, // green
    { Shape_Triangle, IM_COL32( 52, 152, 219, 255) }, // blue
    { Shape_Star,     IM_COL32(241, 196,  15, 255) }, // yellow
    { Shape_Diamond,  IM_COL32(155,  89, 182, 255) }, // purple
    { Shape_Plus,     IM_COL32(230, 126,  34, 255) }, // orange
    { Shape_Hexagon,  IM_COL32( 26, 188, 156, 255) }, // teal
    { Shape_Ring,     IM_COL32(233,  30,  99, 255) }, // pink
};

bool PointInRect(const ImVec2& p, const ImVec2& min, const ImVec2& max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

// Fill a 5-point star with a triangle fan around its centre (the star outline
// is concave, so AddConvexPolyFilled cannot be used directly).
void DrawStar(ImDrawList* dl, const ImVec2& center, float radius, ImU32 col)
{
    constexpr int kPoints = 5;
    const float inner = radius * 0.45f;
    ImVec2 v[kPoints * 2];
    for (int i = 0; i < kPoints * 2; ++i)
    {
        const float r = (i & 1) ? inner : radius;
        const float a = -3.14159265f * 0.5f + i * (3.14159265f / kPoints);
        v[i] = ImVec2(center.x + cosf(a) * r, center.y + sinf(a) * r);
    }
    for (int i = 0; i < kPoints * 2; ++i)
        dl->AddTriangleFilled(center, v[i], v[(i + 1) % (kPoints * 2)], col);
}

void DrawSymbol(ImDrawList* dl, int symbolId, const ImVec2& center, float halfSize)
{
    const Symbol& s = g_symbols[symbolId];
    const ImU32 col = s.color;
    const float r = halfSize;

    switch (s.shape)
    {
    case Shape_Circle:
        dl->AddCircleFilled(center, r, col, 48);
        break;
    case Shape_Square:
        dl->AddRectFilled(ImVec2(center.x - r * 0.85f, center.y - r * 0.85f),
                          ImVec2(center.x + r * 0.85f, center.y + r * 0.85f), col, r * 0.12f);
        break;
    case Shape_Triangle:
        dl->AddTriangleFilled(ImVec2(center.x, center.y - r),
                              ImVec2(center.x + r * 0.95f, center.y + r * 0.8f),
                              ImVec2(center.x - r * 0.95f, center.y + r * 0.8f), col);
        break;
    case Shape_Star:
        DrawStar(dl, center, r, col);
        break;
    case Shape_Diamond:
    {
        ImVec2 v[4] = {
            ImVec2(center.x, center.y - r),
            ImVec2(center.x + r, center.y),
            ImVec2(center.x, center.y + r),
            ImVec2(center.x - r, center.y),
        };
        dl->AddConvexPolyFilled(v, 4, col);
        break;
    }
    case Shape_Plus:
    {
        const float t = r * 0.38f;
        dl->AddRectFilled(ImVec2(center.x - t, center.y - r), ImVec2(center.x + t, center.y + r), col, t * 0.4f);
        dl->AddRectFilled(ImVec2(center.x - r, center.y - t), ImVec2(center.x + r, center.y + t), col, t * 0.4f);
        break;
    }
    case Shape_Hexagon:
        dl->AddNgonFilled(center, r, col, 6);
        break;
    case Shape_Ring:
        dl->AddCircle(center, r * 0.8f, col, 48, r * 0.42f);
        break;
    }
}

// Draw a string centred horizontally at centerX, with its top at posY.
void DrawCenteredText(ImDrawList* dl, float centerX, float posY, float fontSize, ImU32 col, const char* text)
{
    ImFont* font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    dl->AddText(font, fontSize, ImVec2(centerX - size.x * 0.5f, posY), col, text);
}

} // namespace

Game::Game() {
    m_lastClosePressEventIndex = migi::WindowGetLastClosePressEventIndex();
    const int64_t nowTicks = migi::TimePoint::Now().ToChronoDurationSinceEpoch().count();
    m_rngState = static_cast<unsigned>(nowTicks & 0xffffffffll) | 1u;
    NewGame();
}

void Game::NewGame()
{
    // Build two of each symbol id, then Fisher-Yates shuffle with a small
    // xorshift RNG so each new game has a fresh layout.
    for (int i = 0; i < kCardCount; ++i)
    {
        m_symbols[i] = i / 2;
        m_matched[i] = false;
    }
    for (int i = kCardCount - 1; i > 0; --i)
    {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        const int j = static_cast<int>(m_rngState % static_cast<unsigned>(i + 1));
        const int tmp = m_symbols[i];
        m_symbols[i] = m_symbols[j];
        m_symbols[j] = tmp;
    }

    m_phase = Phase::SelectFirst;
    m_first = -1;
    m_second = -1;
    m_pairsFound = 0;
    m_moves = 0;
    m_started = true;
}

void Game::Update(migi::FrameData& frameData)
{
    PROFILE_SCOPE("Game::Update");

    frameData.result.stop = m_lastClosePressEventIndex != migi::WindowGetLastClosePressEventIndex();
    frameData.fullscreen = m_fullscreen;
    frameData.vsync = m_vsync;

    if (!m_started)
        NewGame();

    const float screenW = static_cast<float>(frameData.width);
    const float screenH = static_cast<float>(frameData.height);

    // ---- Layout ---------------------------------------------------------
    const float margin    = screenW * 0.05f;
    const float gap       = screenW * 0.03f;
    const float gridW     = screenW - 2.0f * margin;
    const float card      = (gridW - (kGridSide - 1) * gap) / kGridSide; // square cards
    const float gridH     = card * kGridSide + gap * (kGridSide - 1);
    const float titleH    = screenH * 0.14f;
    const float buttonH   = screenH * 0.07f;
    const float buttonW   = gridW * 0.55f;
    const float availTop  = titleH;
    const float availH    = screenH - titleH - buttonH - margin;
    const float gridTop   = availTop + (availH - gridH) * 0.5f;
    const float gridLeft  = margin;

    const ImVec2 buttonMin(screenW * 0.5f - buttonW * 0.5f, screenH - buttonH - margin * 0.5f);
    const ImVec2 buttonMax(screenW * 0.5f + buttonW * 0.5f, screenH - margin * 0.5f);

    auto cardRect = [&](int idx, ImVec2& min, ImVec2& max) {
        const int col = idx % kGridSide;
        const int row = idx / kGridSide;
        min = ImVec2(gridLeft + col * (card + gap), gridTop + row * (card + gap));
        max = ImVec2(min.x + card, min.y + card);
    };

    // ---- Input (strictly tap-driven) -----------------------------------
    ImGuiIO& io = ImGui::GetIO();
    const bool tapped = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const ImVec2 mouse = io.MousePos;

    if (tapped)
    {
        const bool onNewGame = PointInRect(mouse, buttonMin, buttonMax);

        if (onNewGame)
        {
            NewGame();
        }
        else
        {
            switch (m_phase)
            {
            case Phase::SelectFirst:
            case Phase::SelectSecond:
            {
                for (int i = 0; i < kCardCount; ++i)
                {
                    ImVec2 mn, mx;
                    cardRect(i, mn, mx);
                    if (!PointInRect(mouse, mn, mx))
                        continue;
                    if (m_matched[i] || i == m_first)
                        break; // tapping a matched / already-revealed card does nothing

                    if (m_phase == Phase::SelectFirst)
                    {
                        m_first = i;
                        m_phase = Phase::SelectSecond;
                    }
                    else
                    {
                        m_second = i;
                        m_phase = Phase::Resolve;
                        ++m_moves;
                    }
                    break;
                }
                break;
            }
            case Phase::Resolve:
            {
                // Any tap resolves the two revealed cards.
                if (m_symbols[m_first] == m_symbols[m_second])
                {
                    m_matched[m_first] = true;
                    m_matched[m_second] = true;
                    ++m_pairsFound;
                }
                m_first = -1;
                m_second = -1;
                m_phase = (m_pairsFound == kPairCount) ? Phase::Won : Phase::SelectFirst;
                break;
            }
            case Phase::Won:
                // The board stays in the won state; only the New Game button resets.
                break;
            }
        }
    }

    // ---- Draw -----------------------------------------------------------
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Background.
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, screenH), IM_COL32(26, 28, 38, 255));

    // Title + status.
    const float titleFont = screenH * 0.040f;
    const float statusFont = screenH * 0.026f;
    DrawCenteredText(dl, screenW * 0.5f, screenH * 0.030f, titleFont, IM_COL32(236, 240, 245, 255), "Memory Match");

    char status[64];
    if (m_phase == Phase::Won)
        std::snprintf(status, sizeof(status), "You win!  Moves: %d", m_moves);
    else
        std::snprintf(status, sizeof(status), "Pairs %d / %d   Moves %d", m_pairsFound, kPairCount, m_moves);
    DrawCenteredText(dl, screenW * 0.5f, screenH * 0.030f + titleFont * 1.15f, statusFont,
                     IM_COL32(150, 200, 160, 255), status);

    // Cards.
    for (int i = 0; i < kCardCount; ++i)
    {
        ImVec2 mn, mx;
        cardRect(i, mn, mx);
        const ImVec2 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        const float rounding = card * 0.12f;

        const bool faceUp = m_matched[i] || i == m_first || i == m_second;

        if (!faceUp)
        {
            // Face-down: rounded card back with a subtle inner border.
            dl->AddRectFilled(mn, mx, IM_COL32(58, 80, 140, 255), rounding);
            dl->AddRect(ImVec2(mn.x + card * 0.10f, mn.y + card * 0.10f),
                        ImVec2(mx.x - card * 0.10f, mx.y - card * 0.10f),
                        IM_COL32(90, 120, 200, 255), rounding * 0.7f, 0, card * 0.03f);
        }
        else
        {
            const ImU32 faceCol = m_matched[i] ? IM_COL32(60, 70, 60, 255)
                                               : IM_COL32(238, 240, 245, 255);
            dl->AddRectFilled(mn, mx, faceCol, rounding);
            DrawSymbol(dl, m_symbols[i], center, card * 0.32f);
            if (m_matched[i])
                dl->AddRect(mn, mx, IM_COL32(46, 204, 113, 255), rounding, 0, card * 0.04f);
        }
    }

    // Win banner: large text on a flat black panel over the board, kept
    // visible until New Game is pressed.
    if (m_phase == Phase::Won)
    {
        const char* winText = "You win";
        const float winFont = screenH * 0.08f;
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(winFont, FLT_MAX, 0.0f, winText);
        const float padX = winFont * 0.5f;
        const float padY = winFont * 0.3f;
        const ImVec2 center(screenW * 0.5f, gridTop + gridH * 0.5f);
        dl->AddRectFilled(ImVec2(center.x - textSize.x * 0.5f - padX, center.y - textSize.y * 0.5f - padY),
                          ImVec2(center.x + textSize.x * 0.5f + padX, center.y + textSize.y * 0.5f + padY),
                          IM_COL32(0, 0, 0, 255), winFont * 0.25f);
        DrawCenteredText(dl, center.x, center.y - textSize.y * 0.5f,
                         winFont, IM_COL32(241, 196, 15, 255), winText);
    }

    // New Game button.
    dl->AddRectFilled(buttonMin, buttonMax, IM_COL32(52, 152, 219, 255), buttonH * 0.25f);
    DrawCenteredText(dl, screenW * 0.5f,
                     buttonMin.y + (buttonH - statusFont) * 0.5f,
                     statusFont, IM_COL32(255, 255, 255, 255), "New Game");

#if 0
    // ---- Disabled test / debug ImGui windows ---------------------------
    // These are the original sample windows. They are kept here (compiled out)
    // so they can be re-enabled for testing by switching the #if above to 1.
    static const char* g_frame_strategy[4]{
        "3 Frame Latency",
        "2 Frame Latency (CPU bound)",
        "2 Frame Latency (GPU bound)",
        "1 Frame Latency"
    };

    const migi::Int2 windowSize = migi::WindowGetSize();
    const float oneThirdY = static_cast<float>(windowSize.y) / 3.0f;
    const float marginX = static_cast<float>(windowSize.x) * 0.02f;

    if (m_show_demo_window)
    {
        ImGui::ShowDemoWindow(&m_show_demo_window);
        if (!m_demoWindowPlaced)
        {
            constexpr float kDemoWidth = 550.0f;
            const float demoX = (static_cast<float>(windowSize.x) - kDemoWidth) * 0.5f;
            ImGui::SetWindowPos("Dear ImGui Demo", ImVec2(demoX, oneThirdY));
            m_demoWindowPlaced = true;
        }
    }

    {
        ImGui::SetNextWindowPos(ImVec2(marginX, oneThirdY), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowSize.x) * 0.33f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Hello, world!");

        ImGui::Text("This is some useful text.");
        ImGui::Checkbox("Demo Window", &m_show_demo_window);
        ImGui::Checkbox("Another Window", &m_show_another_window);

        ImGui::SliderFloat("float", &m_f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", (float*)&m_clear_color);

        if (ImGui::Button("Button"))
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
            case 0:
                frameData.result.maxFrameLatency = 3;
                frameData.result.gameStageUs = migi::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = migi::FrameUpdateResult::DefaultRenderStageUs;
                break;
            case 1:
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = migi::FrameUpdateResult::DefaultGameStageUs;
                frameData.result.renderStageUs = 8000;
                break;
            case 2:
                frameData.result.maxFrameLatency = 2;
                frameData.result.gameStageUs = 8000;
                frameData.result.renderStageUs = migi::FrameUpdateResult::DefaultRenderStageUs;
                break;
            case 3:
                frameData.result.maxFrameLatency = 1;
                frameData.result.gameStageUs = 4000;
                frameData.result.renderStageUs = 4000;
                break;
            }
        }

        ImGui::End();
    }

    if (m_show_another_window)
    {
        ImGui::SetNextWindowPos(ImVec2(marginX + 40.0f, oneThirdY + 40.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Another Window", &m_show_another_window);
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            m_show_another_window = false;
        ImGui::End();
    }
#endif
}

}
