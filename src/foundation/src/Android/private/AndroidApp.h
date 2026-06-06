#pragma once

#include <atomic>
#include <cstdint>

struct ANativeWindow;
struct AAssetManager;

namespace migi
{

// Process-wide state shared between the NativeActivity entry point (main.cpp),
// the Android WindowManager backend, and the Android asset loader. The
// NativeActivity thread populates it as the activity lifecycle progresses; the
// engine (running on a separate thread) reads it through the Window and Asset
// APIs.
struct AndroidPlatform
{
    ANativeWindow* window = nullptr;
    AAssetManager* assetManager = nullptr;
    std::atomic<uint64_t> closeEventIndex{0};
    std::atomic<int> width{0};
    std::atomic<int> height{0};
};

AndroidPlatform& AndroidPlatformState();

}
