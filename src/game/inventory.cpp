// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "game/inventory.hpp"

namespace game
{

namespace
{

constexpr int kCursorRow = 0xA2;
constexpr int kPouch = 0x06;
constexpr int kPouchCapacity = 200;

// The first instrument's item id and the length of each progressive weapon line.
struct Line
{
    int first, length;
};
constexpr Line kLines[3] = {{0x11, 4}, {0x15, 5}, {0x1A, 5}};

} // namespace

std::uint16_t received_cursor(const Addresses &game)
{
    return game.item_counts[kCursorRow];
}

void set_received_cursor(const Addresses &game, std::uint16_t cursor)
{
    game.item_counts[kCursorRow] = cursor;
}

void add_item(const Addresses &game, int id)
{
    game.item_add(*game.item_mgr, id, 1);
    if (id == kPouch && game.astral_pouch_max() < kPouchCapacity)
        game.set_astral_pouch_max(kPouchCapacity);
}

void add_weapon(const Addresses &game, int line, int copy)
{
    if (copy <= kLines[line].length)
        add_item(game, kLines[line].first + copy - 1);
}

} // namespace game
