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
void WindowSetCursorShape(CursorShape shape);
void* WindowGetNativeHandle();

}
