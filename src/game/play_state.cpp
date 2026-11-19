#include "game/play_state.hpp"

namespace game
{

bool title_shown(const Addresses &game)
{
    // Major state 0 is the boot logos, 1 is never dispatched, and 2 is a one-frame advance to the next major state. 3 is the attract movie, and the menu
    // states follow. Room 0x0C00 is the title room.
    return (*game.title_state & 0xFF) >= 3 && *game.current_room == 0x0C00;
}

} // namespace game
