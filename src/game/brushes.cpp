// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "game/brushes.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace game
{

namespace
{

constexpr int kEnabledSaved = 0xc0;
constexpr int kLevelSaved = 0xc8;
constexpr int kLevel = 0x80;
constexpr int kPowerSlash = 12;
constexpr int kCherryBomb = 25;

// Brushes the game gives together with a companion bit (inferred), all in decimal:
// Bloom 4 with 2 and 3.
// Vine 19 with 20.
// Sunrise 27 with 1, the bit its menu page reads.
// Blizzard 23 has no row: its god's 24 is Icestorm, an item of its own.
struct Companions
{
    int bit;
    std::uint64_t others;
};
constexpr Companions kCompanions[] = {{4, 1ull << 2 | 1ull << 3}, {19, 1ull << 20}, {27, 1ull << 1}};

} // namespace

// FudeSys's pop copies +0xc0 and +0xc8 back over the live mask and levels, so a grant writes the saved copies too (inferred).
void grant_brush(const Addresses &game, int bit, int copy)
{
    if ((bit == kPowerSlash || bit == kCherryBomb) && copy > 1)
    {
        if (copy <= 3)
        {
            const std::uint8_t level = std::max<std::uint8_t>(game.fude[kLevel + bit], static_cast<std::uint8_t>(copy));
            game.set_technique_level(game.fude, bit, level);
            game.fude[kLevelSaved + bit] = std::max(game.fude[kLevelSaved + bit], level);
            if (bit == kCherryBomb)
                game.refresh_cherry_bomb_count(game.fude);
        }
        return;
    }
    std::uint64_t mask = 1ull << bit;
    for (const Companions &c : kCompanions)
        if (c.bit == bit)
            mask |= c.others;
    for (int b = 0; b < 64; ++b)
        if (mask >> b & 1)
            game.update_technique_bit(game.fude, static_cast<std::uint32_t>(b), 0);
    std::uint64_t saved;
    std::memcpy(&saved, game.fude + kEnabledSaved, 8);
    saved |= mask;
    std::memcpy(game.fude + kEnabledSaved, &saved, 8);
}

} // namespace game
