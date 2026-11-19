#include "client.hpp"

#include "ap/session.hpp"
#include "game/play_state.hpp"
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
    if (!support::data_dir().empty())
        g_session = new ap::Session(support::data_dir());
}

void frame()
{
    if (g_session)
        g_session->tick();
    if (!g_title_logged && game::title_shown(g_game))
    {
        g_title_logged = true;
        support::log(support::Level::Info, "title reached");
    }
}

} // namespace client
