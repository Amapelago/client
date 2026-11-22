// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "game/addresses.hpp"

namespace game
{

// True while the title is at its attract movie, press start or menu (major states 3 to 5, in the title room).
bool title_shown(const Addresses &game);

// Main thread. True while the player is in free play: no cutscene, script or shop holds control. It stays true with the pause menu open.
bool free_play(const Addresses &game);

// Main thread. True at a moment when the game's state can be written to a save record and loaded back.
bool save_safe(const Addresses &game);

} // namespace game
