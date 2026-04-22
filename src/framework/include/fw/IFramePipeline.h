#pragma once

#include <fw/FrameData.h>

namespace engine
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
};

} // namespace engine
