// The link against a scripted Archipelago server over loopback. Each race runs the link's net thread against this thread and the server's io thread.
#include "ap/net.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ap/link.hpp"
#include "support/fixture.hpp"
#include "support/scripted_ap_server.hpp"

namespace
{

template <class T> bool holds(const std::vector<ap::Event> &events)
{
    return std::ranges::any_of(events, [](const ap::Event &e) { return std::holds_alternative<T>(e); });
}

// The link is declared after the server, so it closes while the server still runs.
struct ConnectedLink
{
    ScriptedApServer server;
    ap::Link link;

    ConnectedLink()
    {
        server.reply_to("Connect", {connected_reply(contract_fixture()["slot_data"]["default"], {}, {})});
        link.connect("127.0.0.1:" + std::to_string(server.port()), "Player1", "", "uuid");
        wait_until([&] { return holds<ap::Connected>(link.drain()); });
    }
};

} // namespace

TEST_CASE_METHOD(ConnectedLink, "items the server sends while main drains arrive whole and in order")
{
    for (int i = 0; i < 2000; ++i)
        server.push(received_items(i, {{100 + i, 1, 1, 0}}));
    std::vector<ap::ReceivedItems> got;
    wait_until(
        [&]
        {
            for (ap::Event &e : link.drain())
                if (auto *r = std::get_if<ap::ReceivedItems>(&e))
                    got.push_back(std::move(*r));
            return got.size() >= 2000;
        });
    REQUIRE(got.size() == 2000);
    for (int i = 0; i < 2000; ++i)
    {
        REQUIRE(got[i].index == i);
        REQUIRE(got[i].items == std::vector<ap::NetworkItem>{{100 + i, 1, 1, 0}});
    }
}

TEST_CASE_METHOD(ConnectedLink, "locations main sends while the net thread drains all reach the server")
{
    for (int i = 0; i < 2000; ++i)
        link.send_locations({1000 + i});
    std::vector<std::int64_t> ids;
    wait_until(
        [&]
        {
            ids.clear();
            for (const nlohmann::json &checks : server.received("LocationChecks"))
                for (const nlohmann::json &id : checks["locations"])
                    ids.push_back(id.get<std::int64_t>());
            return ids.size() >= 2000;
        });
    std::vector<std::int64_t> expected(2000);
    std::iota(expected.begin(), expected.end(), 1000);
    std::ranges::sort(ids);
    CHECK(ids == expected);
}

TEST_CASE_METHOD(ConnectedLink, "events of a closed connection never arrive after disconnect returns")
{
    for (int i = 0; i < 2000; ++i)
        server.push(received_items(i, {{100 + i, 1, 1, 0}}));
    // What arrived during the loop is dropped and the drain then spins without a sleep, so the item it sees comes from a poll that is still pushing
    // the rest of its run when disconnect lands.
    link.drain();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!holds<ap::ReceivedItems>(link.drain()))
        if (std::chrono::steady_clock::now() > deadline)
            FAIL("no ReceivedItems arrived within 10 s");
    link.disconnect();
    // Disconnect pushes no event, so anything drained from here on leaked from the closed connection.
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < end)
    {
        REQUIRE(link.drain().empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
