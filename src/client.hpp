#pragma once

#include "game/addresses.hpp"

namespace ap
{
class Session;
}

namespace client
{

// Main thread, on the first flower_startup, before the game initialises.
void cold_start(const game::Addresses &game);

// Main thread, on every flower_tick entry.
void frame();

// Main thread. The AP session, or null when there is no data folder.
ap::Session *session();

} // namespace client
