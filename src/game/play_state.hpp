#pragma once

#include "game/addresses.hpp"

namespace game
{

// True once the title screen is up: the attract movie or any later title state, in the title room.
bool title_shown(const Addresses &game);

// Main thread. True while the player is in free play: no cutscene, script or shop holds control. It stays true with the pause menu open.
bool free_play(const Addresses &game);

// Main thread. True at a moment when the game's state can be written to a save record and loaded back.
bool save_safe(const Addresses &game);

} // namespace game
