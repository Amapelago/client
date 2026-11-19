#pragma once

#include "game/addresses.hpp"

namespace client
{

// Main thread, on the first flower_startup, before the game initialises.
void cold_start(const game::Addresses &game);

// Main thread, on every flower_tick entry.
void frame();

} // namespace client
