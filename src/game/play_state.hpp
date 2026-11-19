#pragma once

#include "game/addresses.hpp"

namespace game
{

// True once the title screen is up: the attract movie or any later title state, in the title room.
bool title_shown(const Addresses &game);

} // namespace game
