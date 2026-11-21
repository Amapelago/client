#pragma once

#include "game/addresses.hpp"

namespace game
{

// Main thread, right after cPad::Update returns: puts g_keyWork in its resting state and clears g_joyWork's digital sets, so gameplay and menus read
// nothing as held or pressed this frame. The title's attract movie and press start read the devices' own banks instead, which this leaves alone.
void release_keys(const Addresses &game);

} // namespace game
