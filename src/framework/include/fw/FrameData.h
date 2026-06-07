#pragma once

#include <imgui/imgui.h>

#include <stdint.h>
#include <vector>

namespace drgn
{
class CommandList;
}

namespace migi
{

// This data is read of the FrameManager to change its behavior
struct FrameUpdateResult
{
    static constexpr int DefaultMaxFrameLatency = 3;
    static constexpr int DefaultRenderStageUs = 15000;
    static constexpr int DefaultGameStageUs = 15500;
    bool stop = false; // Stop the FrameManager, i.e stop scheduling new frame, i.e stop the app
    int maxFrameLatency = DefaultMaxFrameLatency; // Number of frame which can be interleaved at the same time
    int renderStageUs = DefaultRenderStageUs;
    int gameStageUs = DefaultGameStageUs;
};

struct RenderContext
{
    int index = -1;
    uint64_t frameIndex = 0xdeadbeef;
    drgn::CommandList* commandList = nullptr;
};

struct DrawList
{
    ImVector<ImDrawCmd>     CmdBuffer;
    ImVector<ImDrawIdx>     IdxBuffer;
    ImVector<ImDrawVert>    VtxBuffer;
    ImDrawListFlags         Flags = 0;
};

struct DrawData
{
    bool            Valid = false;
    int             CmdListsCount = 0;
    int             TotalIdxCount = 0;
    int             TotalVtxCount = 0;
    ImVec2          DisplayPos;
    ImVec2          DisplaySize;
    ImVec2          FramebufferScale;
    std::vector<DrawList> DrawLists;
};

// Per-frame timings the FrameManager records for each pipeline stage. Kept in
// this fixed order; kFrameMetricNames mirrors it.
enum class FrameMetric : int
{
    Update,      // m_pipeline.Update
    Render,      // m_pipeline.Render
    Kick,        // m_pipeline.Kick
    Clean,       // m_pipeline.Clean
    Count,
};

inline constexpr int kFrameMetricCount = static_cast<int>(FrameMetric::Count);

inline constexpr const char* kFrameMetricNames[kFrameMetricCount] = {
    "Update",
    "Render",
    "Kick",
    "Clean",
};

// Circular buffer of the last kCapacity frames' timings (microseconds), one
// row of kFrameMetricCount values per frame. Owned by the FrameManager and
// snapshotted into FrameData each frame so any stage can read the averages.
struct FrameMetricHistory
{
    static constexpr int kCapacity = 128;

    float samplesUs[kCapacity][kFrameMetricCount] = {};
    int count = 0; // number of valid rows (<= kCapacity)
    int next = 0;  // next row to overwrite

    void Push(const float values[kFrameMetricCount])
    {
        for (int i = 0; i < kFrameMetricCount; ++i)
            samplesUs[next][i] = values[i];
        next = (next + 1) % kCapacity;
        if (count < kCapacity)
            ++count;
    }

    // Average of a metric over the recorded history, in microseconds.
    float AverageUs(FrameMetric metric) const
    {
        if (count == 0)
            return 0.0f;
        const int m = static_cast<int>(metric);
        float sum = 0.0f;
        for (int i = 0; i < count; ++i)
            sum += samplesUs[i][m];
        return sum / static_cast<float>(count);
    }
};

struct FrameData
{
    int64_t frameIndex = 0;
    int maxFrameLatency = 3;
    int renderStageUs = FrameUpdateResult::DefaultRenderStageUs;
    int rendererjobNumber = 10;
    int gameStageUs = FrameUpdateResult::DefaultGameStageUs;
    int gamejobNumber = 10;
    float deltatime = 0.0166f;
    bool fullscreen = false;
    bool vsync = true;
    int width = 1280;
    int height = 800;
    DrawData drawData;
    RenderContext* renderContext;
    FrameUpdateResult result;
    // Snapshot of the FrameManager's metric history as of this frame's start.
    FrameMetricHistory metrics;
};

}
