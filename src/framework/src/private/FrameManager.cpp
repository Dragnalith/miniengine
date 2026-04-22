#pragma once

#include <fnd/Job.h>
#include <fw/FrameManager.h>

#include <fnd/Profiler.h>

#include <iostream>
#include <mutex>
#include <format>

namespace engine
{


FrameManager::FrameManager(IFramePipeline& pipeline)
    : m_pipeline(pipeline)
    , m_lastStartFrameTime(engine::Clock::now() - std::chrono::milliseconds(16))
{
}

void FrameManager::Start()
{
    StartFrame(FrameUpdateResult());
    Job::Wait(m_handle);
}

void FrameManager::SetFrameLatency(int maxFrameLatency) 
{
    std::scoped_lock<SpinLock> lock(m_frameLatencyLock);
    while (m_maxFrameLatency < maxFrameLatency) {
        m_startSemaphore.Release();
        m_maxFrameLatency += 1;
    }
    while (m_maxFrameLatency > maxFrameLatency) {
        m_startSemaphore.Acquire();
        m_maxFrameLatency -= 1;
    }
    ASSERT_MSG(maxFrameLatency == m_maxFrameLatency, "error in SetFrameLatency");
}

int64_t FrameManager::AllocateFrameIndex() {
    int64_t frameIndex = m_nextFrameIndex;
    m_nextFrameIndex += 1;
    return frameIndex;
}
void FrameManager::RunFrame(FrameUpdateResult prevResult) {
    SetFrameLatency(prevResult.maxFrameLatency);

    int64_t frameIndex = AllocateFrameIndex();
    
    std::string frameName = std::format("Index = {}", frameIndex);

    m_startSemaphore.Acquire();

    auto now = Clock::now();
    float deltatime = std::chrono::duration<float>(now - m_lastStartFrameTime).count();
    m_lastStartFrameTime = now;

    FrameData frameData;
    frameData.frameIndex = frameIndex;
    frameData.deltatime = deltatime;
    frameData.maxFrameLatency = prevResult.maxFrameLatency;
    frameData.renderStageUs = prevResult.renderStageUs;
    frameData.gameStageUs = prevResult.gameStageUs;

    PROFILE_SCOPE_DATA_COLOR("Frame", frameName.c_str(), 34, 30, 203);
    {
        PROFILE_SCOPE_DATA_COLOR("Update", frameName.c_str(), 51, 217, 21);

        m_pipeline.Update(frameData);
        if (!frameData.result.stop) {
            StartFrame(frameData.result);
        }
    }

    {
        Job::Wait(m_renderSemaphore, frameData.frameIndex);
        PROFILE_SCOPE_DATA_COLOR("Render", frameName.c_str(), 238, 220, 0);
        m_pipeline.Render(frameData); 
        m_renderSemaphore.Set(frameData.frameIndex + 1);
    }

    {
        Job::Wait(m_kickSemaphore, frameData.frameIndex);
        PROFILE_SCOPE_DATA_COLOR("Kick", frameName.c_str(), 238, 0, 60);
        m_pipeline.Kick(frameData);
        PROFILE_DEFAULT_FRAME; // Present on screen
        m_kickSemaphore.Set(frameData.frameIndex + 1);
    }

    {
        PROFILE_SCOPE_DATA_COLOR("Clean", frameName.c_str(), 150, 150, 150);
        m_pipeline.Clean(frameData);
    }
    m_startSemaphore.Release();
}
void FrameManager::StartFrame(FrameUpdateResult result) {
    Job::Dispatch("Frame Job", m_handle, [this, result] {
        RunFrame(result);
    });
}

} // namespace engine
