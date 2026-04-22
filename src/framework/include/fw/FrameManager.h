#pragma once

#include <fnd/Job.h>
#include <fnd/JobSemaphore.h>
#include <fnd/Util.h>
#include <fnd/SpinLock.h>
#include <fw/IFramePipeline.h>

#include <stdint.h>
#include <mutex>

namespace engine
{

struct FrameUpdateResult;

/* 
    Run a IFramePipeline and interveal the stages
*/
class FrameManager
{
public:
    FrameManager(IFramePipeline& pipeline);
    void Start();
private:
    void RunFrame(FrameUpdateResult result);
    void StartFrame(FrameUpdateResult result);

    void SetFrameLatency(int maxFrameLatency);
    int64_t AllocateFrameIndex();

private:
    IFramePipeline& m_pipeline;

    Clock::time_point m_lastStartFrameTime;

    JobCounter m_handle;
    JobCounter m_renderSemaphore; // We should guarantee render stage progress in order, first in first out, so we cannot use JobSemaphore
    JobCounter m_kickSemaphore; // We should guarantee render stage progress in order, first in first out, so we cannot use JobSemaphore
    JobSemaphore m_startSemaphore;
    int64_t m_nextFrameIndex = 0;

    SpinLock m_frameLatencyLock;
    int64_t m_maxFrameLatency = 0;
};

} // namespace engine
