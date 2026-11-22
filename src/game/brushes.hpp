// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "game/addresses.hpp"

namespace game
{

// Threading: main thread only, at flower_tick entry. No lock, and never called from a hook.

// Grants brush technique bit and its companions (copy from 1). Power Slash (bit 12) and Cherry Bomb (bit 25) are progressive: copies 2 and 3 raise the
// technique's level to that number, and later copies do nothing.
void grant_brush(const Addresses &game, int bit, int copy);

} // namespace game
