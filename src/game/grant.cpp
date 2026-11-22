// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "game/grant.hpp"

#include <algorithm>

#include "game/brushes.hpp"
#include "game/inventory.hpp"
#include "support/log.hpp"

namespace game
{

using support::Level;
using support::log;

Route route(std::int64_t id)
{
    if (id >= 0 && id <= 0xFF && id != 0xA2)
        return Route::Item;
    if (id >= 0x300 && id <= 0x302)
        return Route::Weapon;
    if (id >= 0x100 && id <= 0x13F)
        return Route::Brush;
    return Route::Unknown;
}

std::vector<std::int64_t> grant_received(const Addresses &game, std::span<const ap::NetworkItem> received)
{
    std::vector<std::int64_t> unknown;
    for (std::size_t i = received_cursor(game); i < received.size(); ++i)
    {
        const std::int64_t id = received[i].item;
        const auto copy = static_cast<int>(
            std::count_if(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(i) + 1, [id](const ap::NetworkItem &n) { return n.item == id; }));
        const Route kind = route(id);
        switch (kind)
        {
        case Route::Item:
            add_item(game, static_cast<int>(id));
            break;
        case Route::Weapon:
            add_weapon(game, static_cast<int>(id - 0x300), copy);
            break;
        case Route::Brush:
            grant_brush(game, static_cast<int>(id - 0x100), copy);
            break;
        case Route::Unknown:
            log(Level::Warn, "item 0x%llx has no grant route", static_cast<unsigned long long>(id));
            unknown.push_back(id);
            break;
        }
        if (kind != Route::Unknown)
            log(Level::Info, "granted item 0x%llx", static_cast<unsigned long long>(id));
        set_received_cursor(game, static_cast<std::uint16_t>(i + 1));
    }
    return unknown;
}

} // namespace game
