// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Item delivery against a fake inventory and a fake FudeSys, which clamp and write the way the game's functions do.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ap/seed.hpp"
#include "game/addresses.hpp"
#include "game/grant.hpp"

namespace
{

constexpr std::size_t kFudeSize = 0x2220;

std::array<std::uint8_t, kFudeSize> fresh_fude()
{
    std::array<std::uint8_t, kFudeSize> f{};
    std::fill_n(f.begin() + 0x80, 64, std::uint8_t{1});
    return f;
}

std::array<std::uint16_t, 256> fresh_max()
{
    std::array<std::uint16_t, 256> m{};
    m[0x06] = 1;
    for (int id = 0x10; id <= 0x1E; ++id)
        m[id] = 1;
    m[0x0E] = 99;
    return m;
}

// The game's memory as far as item delivery touches it.
struct Fake
{
    std::array<std::uint16_t, 256> counts{};
    std::array<std::uint16_t, 256> max = fresh_max();
    std::array<std::uint8_t, kFudeSize> fude = fresh_fude();
    std::int16_t pouch_max = 0;
    bool operator==(const Fake &) const = default;
} fake;

void *item_mgr = nullptr;

std::uint64_t get64(int offset)
{
    std::uint64_t v;
    std::memcpy(&v, &fake.fude[offset], 8);
    return v;
}
void put64(int offset, std::uint64_t v)
{
    std::memcpy(&fake.fude[offset], &v, 8);
}

void item_add(void *, int id, int delta)
{
    fake.counts[id] = static_cast<std::uint16_t>(std::min<int>(fake.counts[id] + delta, fake.max[id]));
}
void set_pouch(std::uint16_t capacity)
{
    fake.pouch_max = static_cast<std::int16_t>(capacity);
}
std::int16_t get_pouch()
{
    return fake.pouch_max;
}
void technique_bit(void *, std::uint32_t bit, int op)
{
    const std::uint64_t m = 1ull << bit;
    switch (op)
    {
    case 0:
        put64(0x70, get64(0x70) | m);
        put64(0x78, get64(0x78) | m);
        break;
    case 1:
        put64(0x70, get64(0x70) & ~m);
        break;
    case 2:
        put64(0x70, get64(0x70) & ~m);
        put64(0x78, get64(0x78) & ~m);
        break;
    case 3:
        put64(0x78, get64(0x78) & ~m);
        break;
    }
}
void technique_level(void *, int bit, std::uint8_t level)
{
    fake.fude[0x80 + bit] = level;
}
void refresh_bombs(void *)
{
    const std::uint8_t level = fake.fude[0x80 + 25];
    const std::int32_t count = level == 2 || level == 3 ? level : 1;
    std::memcpy(&fake.fude[0x21c8], &count, 4);
}

game::Addresses addresses()
{
    game::Addresses a;
    a.item_mgr = &item_mgr;
    a.item_add = &item_add;
    a.item_counts = fake.counts.data();
    a.set_astral_pouch_max = &set_pouch;
    a.astral_pouch_max = &get_pouch;
    a.fude = fake.fude.data();
    a.update_technique_bit = &technique_bit;
    a.set_technique_level = &technique_level;
    a.refresh_cherry_bomb_count = &refresh_bombs;
    return a;
}

std::vector<ap::NetworkItem> items(std::initializer_list<std::int64_t> ids)
{
    std::vector<ap::NetworkItem> v;
    for (std::int64_t id : ids)
        v.push_back({id, 0, 1, 0});
    return v;
}

std::uint64_t enabled()
{
    return get64(0x70);
}
std::uint64_t acquired()
{
    return get64(0x78);
}
std::uint64_t saved_enabled()
{
    return get64(0xc0);
}
int level(int bit)
{
    return fake.fude[0x80 + bit];
}
int saved_level(int bit)
{
    return fake.fude[0xc8 + bit];
}
int bomb_count()
{
    std::int32_t v;
    std::memcpy(&v, &fake.fude[0x21c8], 4);
    return v;
}

// FudeSys_PushTechniqueState and its pop, which scripted events wrap around their own loadout.
void push()
{
    put64(0xc0, enabled());
    std::memcpy(&fake.fude[0xc8], &fake.fude[0x80], 64);
}
void pop()
{
    put64(0x70, saved_enabled());
    std::memcpy(&fake.fude[0x80], &fake.fude[0xc8], 64);
}

} // namespace

TEST_CASE("a new game gets every received item exactly once, and a second call grants nothing")
{
    fake = Fake{};
    const auto received = items({0x06, 0x10, 0x0E, 0x0E, 0x104, 0x301, 0x302, 0x302});
    CHECK(game::grant_received(addresses(), received).empty());
    CHECK(fake.counts[0x06] == 1);
    CHECK(fake.pouch_max == 200);
    CHECK(fake.counts[0x10] == 1);
    CHECK(fake.counts[0x0E] == 2);
    CHECK(fake.counts[0x15] == 1);
    CHECK(fake.counts[0x1A] == 1);
    CHECK(fake.counts[0x1B] == 1);
    CHECK(enabled() == (1ull << 4 | 1ull << 2 | 1ull << 3));
    CHECK(acquired() == enabled());
    CHECK(saved_enabled() == enabled());
    CHECK(fake.counts[0xA2] == 8);
    const Fake before = fake;
    CHECK(game::grant_received(addresses(), received).empty());
    CHECK(fake == before);
}

TEST_CASE("an older save re-grants exactly the items after its cursor")
{
    fake = Fake{};
    fake.counts[0x0E] = 3;
    fake.counts[0xA2] = 3;
    game::grant_received(addresses(), items({0x0E, 0x0E, 0x0E, 0x0E, 0x0E}));
    CHECK(fake.counts[0x0E] == 5);
    CHECK(fake.counts[0xA2] == 5);
}

TEST_CASE("an older save counts a weapon's copy from the start of the received list")
{
    fake = Fake{};
    fake.counts[0x1A] = 1;
    fake.counts[0xA2] = 1;
    game::grant_received(addresses(), items({0x302, 0x302}));
    CHECK(fake.counts[0x1B] == 1);
    CHECK(fake.counts[0x1C] == 0);
}

TEST_CASE("a cursor past the end of the received list grants nothing and stays")
{
    fake = Fake{};
    fake.counts[0xA2] = 5;
    game::grant_received(addresses(), items({0x0E, 0x0E, 0x0E}));
    CHECK(fake.counts[0x0E] == 0);
    CHECK(fake.counts[0xA2] == 5);
}

TEST_CASE("an unknown item id is skipped and the cursor moves past it")
{
    fake = Fake{};
    const auto received = items({0xA2, 0x303, 0x140, 0x0E, 0x303});
    CHECK(game::grant_received(addresses(), received) == std::vector<std::int64_t>{0xA2, 0x303, 0x140, 0x303});
    CHECK(fake.counts[0x0E] == 1);
    CHECK(fake.counts[0xA2] == 5);
    CHECK(game::grant_received(addresses(), received).empty());
}

TEST_CASE("each progressive weapon copy gives its line's next instrument and nothing past the end")
{
    fake = Fake{};
    fake.counts[0x1A] = 1; // a vanilla source already gave Tsumugari
    game::grant_received(addresses(), items({0x300, 0x300, 0x300, 0x300, 0x300, 0x302, 0x302}));
    for (int id : {0x11, 0x12, 0x13, 0x14, 0x1A, 0x1B})
        CHECK(fake.counts[id] == 1);
    CHECK(fake.counts[0x15] == 0); // the fifth Mirror spills into no other line
    CHECK(fake.counts[0x1C] == 0);
}

TEST_CASE("power slash and cherry bomb copies grant the technique, then raise its level to three and no further")
{
    fake = Fake{};
    game::grant_received(addresses(), items({0x10C}));
    CHECK(enabled() == 1ull << 12);
    CHECK(level(12) == 1);
    game::grant_received(addresses(), items({0x10C, 0x10C, 0x10C, 0x10C, 0x119, 0x119}));
    CHECK(level(12) == 3);
    CHECK(saved_level(12) == 3);
    CHECK(level(25) == 2);
    CHECK(saved_level(25) == 2);
    CHECK(bomb_count() == 2);
}

TEST_CASE("a level above the copy's own is kept")
{
    fake = Fake{};
    fake.fude[0x80 + 12] = 3;
    fake.fude[0xc8 + 12] = 3;
    fake.counts[0xA2] = 1;
    game::grant_received(addresses(), items({0x10C, 0x10C}));
    CHECK(level(12) == 3);
    CHECK(saved_level(12) == 3);
}

TEST_CASE("brushes grant their companion bits, and blizzard grants only its own")
{
    fake = Fake{};
    game::grant_received(addresses(), items({0x11B, 0x113, 0x117}));
    CHECK(enabled() == (1ull << 27 | 1ull << 1 | 1ull << 0x13 | 1ull << 0x14 | 1ull << 23));
}

TEST_CASE("a brush and a level granted inside a push window survive the pop")
{
    fake = Fake{};
    game::grant_received(addresses(), items({0x10C}));
    push();
    game::grant_received(addresses(), items({0x10C, 0x106, 0x10C}));
    pop();
    CHECK(enabled() == (1ull << 12 | 1ull << 6));
    CHECK(level(12) == 2);
}

TEST_CASE("the astral pouch only ever raises its capacity to 200")
{
    fake = Fake{};
    fake.pouch_max = 400;
    game::grant_received(addresses(), items({0x06}));
    CHECK(fake.pouch_max == 400);
}
