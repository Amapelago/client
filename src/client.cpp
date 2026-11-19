#include "client.hpp"

#include "game/play_state.hpp"
#include "support/log.hpp"

namespace client
{
namespace
{

// Written by cold_start and read by frame, both on the main thread.
game::Addresses g_game;
bool g_title_logged = false;

} // namespace

void cold_start(const game::Addresses &game)
{
    g_game = game;
}

void frame()
{
    if (!g_title_logged && game::title_shown(g_game))
    {
        g_title_logged = true;
        support::log(support::Level::Info, "title reached");
    }
}

} // namespace client
