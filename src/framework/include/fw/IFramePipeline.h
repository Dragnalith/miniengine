#pragma once

#include <fw/FrameData.h>

namespace migi
{

/* 
    IFramePipeline hold the stage making up a frame. 
    Those stages may run in paralle depending on the "mainloop strategy"
*/
class IFramePipeline
{
public:
    virtual void Update(FrameData& frameData) = 0; // Update the game state

    virtual void Render(FrameData& frame) = 0; // Prepare command buffer, does not touch the game state, does not kick command
                                               // so it can run in parallel. It should be a pure function

    virtual void Kick(const FrameData& frameData) = 0; // Kick command buffer generated in previous state
                                                       // Do not return until the frame has been presented from the GPU-side
                                                       // (CAUTION: do not block worker thread, yield to other job)

    virtual void Clean(const FrameData& frameData) = 0; // Run after the frame has been presented

    // Called by the FrameManager when the presentation surface goes away / comes
    // back (e.g. the app is backgrounded then resumed). Suspend must release any
    // window-dependent GPU resources (swapchain/surface); Resume recreates them
    // against the current window. Default no-op for pipelines that don't present.
    virtual void Suspend() {}
    virtual void Resume() {}
};

} // namespace migi
