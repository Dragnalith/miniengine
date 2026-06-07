#pragma once

#include <fnd/Assert.h>
#include <fnd/Job.h>
#include <fnd/Window.h>
#include <fw/FrameManager.h>

#include <fnd/Profiler.h>

#include <iostream>
#include <mutex>
#include <format>

namespace migi
{


FrameManager::FrameManager(IFramePipeline& pipeline)
    : m_pipeline(pipeline)
    , m_lastStartFrameTime(TimePoint::Now() - TimeSpan::FromMilliseconds(16))
{
}

void FrameManager::Start()
{
    // Outer loop: run frames until the pipeline drains, then decide whether the
    // drain was a genuine quit (exit) or just the presentation surface going
    // away (suspend, then wait for it to come back and resume). The first
    // window is guaranteed valid before MigiMain starts, so the swapchain
    // created by the Renderer constructor is used for the first run.
    while (true)
    {
        StartFrame(FrameUpdateResult());
        Job::Wait(m_handle); // returns once frames stop scheduling successors

        // No frame jobs are in flight here, so it is safe to tear the surface
        // down. Releasing it lets a pending APP_CMD_TERM_WINDOW return.
        m_pipeline.Suspend();
        WindowNotifySurfaceReleased();

        if (m_stopRequested.load() || WindowShouldQuit())
            break;

        WindowWaitUntilValid(); // paused: block until the window comes back
        if (WindowShouldQuit())
            break;

        m_pipeline.Resume();
    }
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
    MIGI_ASSERT(maxFrameLatency == m_maxFrameLatency, "error in SetFrameLatency");
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

    // Timings (microseconds) recorded for this frame; pushed into the history
    // once the frame completes. Indexed by FrameMetric.
    float metricsUs[kFrameMetricCount] = {};
    auto elapsedUs = [](TimePoint from) {
        return static_cast<float>((TimePoint::Now() - from).ToMicroseconds());
    };

    {
        TimePoint acquireStart = TimePoint::Now();
        m_startSemaphore.Acquire();
        metricsUs[static_cast<int>(FrameMetric::StartAcquire)] = elapsedUs(acquireStart);
    }

    TimePoint now = TimePoint::Now();
    float deltatime = static_cast<float>((now - m_lastStartFrameTime).ToSeconds());
    m_lastStartFrameTime = now;

    FrameData frameData;
    frameData.frameIndex = frameIndex;
    frameData.deltatime = deltatime;
    frameData.maxFrameLatency = prevResult.maxFrameLatency;
    frameData.renderStageUs = prevResult.renderStageUs;
    frameData.gameStageUs = prevResult.gameStageUs;

    // Hand the stages the metric history as of this frame's start so they can
    // display the running averages.
    {
        std::scoped_lock<SpinLock> lock(m_historyLock);
        frameData.metrics = m_history;
    }

    PROFILE_SCOPE_DATA_COLOR("Frame", frameName.c_str(), 34, 30, 203);
    {
        PROFILE_SCOPE_DATA_COLOR("Update", frameName.c_str(), 51, 217, 21);

        TimePoint updateStart = TimePoint::Now();
        m_pipeline.Update(frameData);
        metricsUs[static_cast<int>(FrameMetric::Update)] = elapsedUs(updateStart);

        if (frameData.result.stop)
            m_stopRequested.store(true);

        // Stop scheduling successors when quitting or when the presentation
        // surface is gone (backgrounded). The in-flight frames drain, letting
        // FrameManager::Start() suspend or exit cleanly.
        if (!m_stopRequested.load() && WindowIsValid() && !WindowShouldQuit()) {
            StartFrame(frameData.result);
        }
    }

    {
        TimePoint renderWaitStart = TimePoint::Now();
        Job::Wait(m_renderSemaphore, frameData.frameIndex);
        metricsUs[static_cast<int>(FrameMetric::RenderWait)] = elapsedUs(renderWaitStart);
        PROFILE_SCOPE_DATA_COLOR("Render", frameName.c_str(), 238, 220, 0);
        TimePoint renderStart = TimePoint::Now();
        m_pipeline.Render(frameData); 
        metricsUs[static_cast<int>(FrameMetric::Render)] = elapsedUs(renderStart);
        m_renderSemaphore.Set(frameData.frameIndex + 1);
    }

    {
        TimePoint kickWaitStart = TimePoint::Now();
        Job::Wait(m_kickSemaphore, frameData.frameIndex);
        metricsUs[static_cast<int>(FrameMetric::KickWait)] = elapsedUs(kickWaitStart);
        PROFILE_SCOPE_DATA_COLOR("Kick", frameName.c_str(), 238, 0, 60);
        TimePoint kickStart = TimePoint::Now();
        m_pipeline.Kick(frameData);
        metricsUs[static_cast<int>(FrameMetric::Kick)] = elapsedUs(kickStart);
        PROFILE_DEFAULT_FRAME; // Present on screen
        m_kickSemaphore.Set(frameData.frameIndex + 1);
    }

    {
        PROFILE_SCOPE_DATA_COLOR("Clean", frameName.c_str(), 150, 150, 150);
        TimePoint cleanStart = TimePoint::Now();
        m_pipeline.Clean(frameData);
        metricsUs[static_cast<int>(FrameMetric::Clean)] = elapsedUs(cleanStart);
    }

    {
        std::scoped_lock<SpinLock> lock(m_historyLock);
        m_history.Push(metricsUs);
    }
    m_startSemaphore.Release();
}
void FrameManager::StartFrame(FrameUpdateResult result) {
    Job::Dispatch("Frame Job", m_handle, [this, result] {
        RunFrame(result);
    });
}

} // namespace migi
