#pragma once

namespace engine
{

/* 
    IGame represent the game logic, i.e what update the game state.
    It does not know about rendering, or any engine concept.

    Its purpose is to modify the game state, i.e in the prototype the game
    state is just ImGui, since it is a singleton it does not appear in the signature
    but in a real game engine, the game state should be an argument of the update function
*/
class IGame
{
public:
    virtual void Update(FrameData& frameData) = 0;
};

} // namespace engine
