#pragma once

#include <fnd/Util.h>
#include <fw/FrameData.h>
#include <fw/IFramePipeline.h>

#include <stdint.h>

namespace migi
{

struct FrameUpdateResult;

/* 
    Run a IFramePipeline, executing each frame's stages sequentially: Update,
    Render, Kick and Clean run back to back in a single loop, and the next
    frame's Update only starts once the previous frame's Clean has returned.
*/
class FrameManager
{
public:
    FrameManager(IFramePipeline& pipeline);
    void Start();
private:
    FrameUpdateResult RunFrame(FrameUpdateResult prevResult);
    int64_t AllocateFrameIndex();

private:
    IFramePipeline& m_pipeline;

    TimePoint m_lastStartFrameTime;

    int64_t m_nextFrameIndex = 0;

    // Set once a frame reports a genuine quit; stops the Start() loop from
    // resuming after the pipeline drains.
    bool m_stopRequested = false;

    // Rolling timing history for the pipeline stages, snapshotted into
    // FrameData at the start of each frame so any stage can read the averages.
    FrameMetricHistory m_history;
};

} // namespace migi
