#pragma once

#include <array>
#include <cstdint>

namespace migi
{

enum class MouseButton : uint8_t
{
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
};

constexpr uint32_t kMouseButtonCount = 5;
constexpr uint32_t kKeyboardKeyCount = 256;
constexpr uint32_t kDefaultInputStateReadCount = 512;

struct MouseState
{
    uint64_t stateIndex = 0;
    int x = 0;
    int y = 0;
    int wheel = 0;
    int horizontalWheel = 0;
    bool inWindow = false;
    std::array<bool, kMouseButtonCount> buttonsDown = {};
};

struct KeyboardState
{
    uint64_t stateIndex = 0;
    std::array<bool, kKeyboardKeyCount> keysDown = {};
};

uint32_t GetMouseState(uint64_t lastStateIndex, MouseState* states, uint32_t maxStateCount);
uint32_t GetKeyboardState(uint64_t lastStateIndex, KeyboardState* states, uint32_t maxStateCount);
uint64_t ReadTextStream(uint64_t firstIndex, wchar_t* characters, uint32_t maxCharacterCount);

}
