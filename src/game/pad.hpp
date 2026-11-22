// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "game/addresses.hpp"

namespace game
{

// Main thread, right after cPad::Update returns: puts g_keyWork in its resting state and clears g_joyWork's digital sets, so gameplay and menus read
// nothing as held or pressed this frame. The title's attract movie and press start read the devices' own banks instead, which this leaves alone.
void release_keys(const Addresses &game);

} // namespace game
