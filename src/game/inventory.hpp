// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

#include "game/addresses.hpp"

namespace game
{

// Threading: main thread only, at flower_tick entry. No lock, and never called from a hook.

// The number of received items this save has already granted or skipped. It lives in the inventory row of item 0xA2, so it travels with the save (confirmed).
std::uint16_t received_cursor(const Addresses &game);
void set_received_cursor(const Addresses &game, std::uint16_t cursor);

// Adds one of item id through the game's own Item_Add. The Astral Pouch (0x06) also raises the pouch capacity to 200.
void add_item(const Addresses &game, int id);

// Adds the copy-th instrument of a progressive weapon line (0 Mirror, 1 Rosary, 2 Sword; copy from 1). Past the end of the line it adds nothing.
void add_weapon(const Addresses &game, int line, int copy);

} // namespace game
