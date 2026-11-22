// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ap/seed.hpp"
#include "game/addresses.hpp"

namespace game
{

// Threading: main thread only, at flower_tick entry. No lock, and never called from a hook.

// How the client grants an item id of the world.
enum class Route
{
    Item,
    Weapon,
    Brush,
    Unknown
};

Route route(std::int64_t id);

// Grants received[cursor..], moving the saved cursor past each item as it goes. Returns the ids it skipped as unknown, oldest first.
std::vector<std::int64_t> grant_received(const Addresses &game, std::span<const ap::NetworkItem> received);

} // namespace game
