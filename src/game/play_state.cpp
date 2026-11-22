// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "game/play_state.hpp"

#include <cstdint>

namespace game
{

bool title_shown(const Addresses &game)
{
    // Major state 0 is the boot logos, 1 is never dispatched, and 2 is a one-frame advance to the next major state. 3 is the attract movie, and the menu
    // states follow. Room 0x0C00 is the title room. The states after 5 are the Continue slot screen and a New Game under way, where connecting or
    // disconnecting could change what the game loads.
    const std::uint32_t major = *game.title_state & 0xFF;
    return major >= 3 && major <= 5 && *game.current_room == 0x0C00;
}

bool free_play(const Addresses &game)
{
    // RoomScript_Tick's own test for free play (inferred). The masks are literal 1 << N on the raw words; 0x180b6b2b8 is also g_stateFlagArray, whose game
    // helpers number bits from the most significant end.
    const std::uint32_t ac = game.ui_mode_flags[3], b0 = game.ui_mode_flags[4], b8 = game.ui_mode_flags[6];
    return !(ac & 1u << 26) && !(ac & 1u << 27) && !(b0 & 1u << 19) && !(b0 & 1u << 30) && !(ac & 1u << 13) && !(b0 & 1u << 26) &&
           ((ac & 1u << 14) || !(ac & 1u << 25)) && !(b8 & 1u << 4) && !(b8 & 1u << 19) && !(ac & 1u << 21) && !(ac & 1u << 17);
}

bool save_safe(const Addresses &game)
{
    // The map task has loaded the room (confirmed). No Save or Load screen is open (inferred). No soft reset or Game Over is armed (confirmed).
    // Free play holds, which covers the skill shop and the Kiba shop that SavePrompt_CanOpen misses (inferred).
    const std::uint32_t a4 = game.ui_mode_flags[1], ac = game.ui_mode_flags[3], b0 = game.ui_mode_flags[4];
    if ((*game.map_task_state & 0xFF) != 4 || (b0 & 0x40400000) || (*game.engine_flags & 1u << 15) || !free_play(game))
        return false;
    // The player exists and is released after a load: its u32 at +0xe34 reads 1 under NOW LOADING and 0x10001 in free control (confirmed).
    const void *player = *game.player;
    if (!player || *reinterpret_cast<const volatile std::uint32_t *>(static_cast<const std::uint8_t *>(player) + 0xe34) == 1)
        return false;
    // The player isn't fishing (+0x0c bit 24) or in the god-technique dojo (+0x10 bit 31), which neither free play nor SavePrompt_CanOpen refuses
    // (inferred). The pause menu isn't open (+0x04 bit 30, inferred), since free play reads true under it (confirmed).
    if ((ac & 1u << 24) || (b0 & 1u << 31) || (a4 & 1u << 30))
        return false;
    // The game's own test for opening a save prompt, which refuses cutscenes, scripted control, shops, non-free player states and room arrivals (inferred). It
    // dereferences the player, so it runs last.
    return game.save_prompt_can_open();
}

} // namespace game
