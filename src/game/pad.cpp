#include "game/pad.hpp"

#include <cstddef>

namespace game
{

void release_keys(const Addresses &game)
{
    // cPad::Update builds g_keyWork from every device at the top of the frame and gameplay reads it from there, so a write after it returns is what this
    // frame's readers see. At rest every byte is 0 except the four stick bytes at +0x70, which sit at 0x80, the centre.
    for (std::size_t i = 0; i < 0xc8; ++i)
        game.key_work[i] = 0;
    for (std::size_t i = 0x70; i < 0x74; ++i)
        game.key_work[i] = 0x80;
    // Menus read g_joyWork, which cPad::Update rewrites at the top of every frame too. Its digital sets are 16 bytes each: on at +0x00, old at +0x10,
    // push at +0x20, release at +0x30, a second press edge at +0x48 and auto-repeat at +0x58. The analog block from +0x15a is left alone, since the
    // stick reset in g_keyWork covers play.
    for (std::size_t i = 0; i < 0x40; ++i)
        game.joy_work[i] = 0;
    for (std::size_t i = 0x48; i < 0x68; ++i)
        game.joy_work[i] = 0;
}

} // namespace game
