// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "game/addresses.hpp"

namespace ap
{
class Session;
}

namespace client
{

// Main thread, on the first flower_startup, before the game initialises.
void cold_start(const game::Addresses &game);

// Main thread, on every flower_tick entry.
void frame();

// Main thread. The AP session, or null when there is no data folder.
ap::Session *session();

} // namespace client
