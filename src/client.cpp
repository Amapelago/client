// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "client.hpp"

#include <filesystem>

#include "ap/session.hpp"
#include "game/play_state.hpp"
#include "game/saves.hpp"
#include "support/log.hpp"
#include "support/paths.hpp"

namespace client
{
namespace
{

// Written by cold_start and read by frame, both on the main thread.
game::Addresses g_game;
bool g_title_logged = false;
// Made at cold start and never deleted: it lives as long as the process. Null when there is no data folder.
ap::Session *g_session = nullptr;

} // namespace

void cold_start(const game::Addresses &game)
{
    g_game = game;
    const std::filesystem::path dir = support::data_dir();
    game::saves_start(g_game, dir.empty() ? dir : dir / "saves");
    if (!dir.empty())
        g_session = new ap::Session(dir, &game::saves_bind);
}

void frame()
{
    if (g_session)
        g_session->tick();
    game::saves_frame();
    if (!g_title_logged && game::title_shown(g_game))
    {
        g_title_logged = true;
        support::log(support::Level::Info, "title reached");
    }
}

ap::Session *session()
{
    return g_session;
}

} // namespace client
