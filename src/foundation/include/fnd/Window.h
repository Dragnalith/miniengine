#pragma once

#include <cstdint>

#include <fnd/PrimitiveTypes.h>

namespace migi
{

enum class CursorShape : uint8_t
{
    None,
    Arrow,
    TextInput,
    ResizeAll,
    ResizeEW,
    ResizeNS,
    ResizeNESW,
    ResizeNWSE,
    Hand,
    NotAllowed,
};

Int2 WindowGetSize();
uint64_t WindowGetLastClosePressEventIndex();
void WindowSetTitle(const char* title);
void WindowSetCursorShape(CursorShape shape);
void* WindowGetNativeHandle();

// Window/surface lifecycle, used by the frame loop to survive the window being
// destroyed and recreated (e.g. Android background/resume). On platforms where
// the surface never goes away (desktop) these are trivial: always valid, never
// quitting, and the wait/notify calls are no-ops.

// True while a presentable window/surface exists.
bool WindowIsValid();

// True when the application is being torn down for good (so the frame loop
// should exit rather than wait for the window to come back).
bool WindowShouldQuit();

// Block the caller until WindowIsValid() becomes true again or WindowShouldQuit()
// becomes true. Returns immediately on platforms whose window never disappears.
void WindowWaitUntilValid();

// Signal that the engine has destroyed its surface, so the platform may release
// the underlying native window. Pairs with the platform's destroy handling.
void WindowNotifySurfaceReleased();

}
