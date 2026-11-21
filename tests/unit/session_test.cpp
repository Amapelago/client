// The session over a real link to a scripted Archipelago server on loopback. This thread plays the game's main thread and ticks the session.
#include "ap/net.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "ap/session.hpp"
#include "support/fixture.hpp"
#include "support/scripted_ap_server.hpp"

namespace
{

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

// A fresh data folder, removed afterwards.
struct DataDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("okami_apclient_session_" + std::to_string(std::random_device{}()));

    DataDir()
    {
        std::filesystem::create_directories(path);
    }
    ~DataDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// An item in the server's list for slot 1, found at 8000000101.
ap::NetworkItem item(std::int64_t id)
{
    return {id, 8000000101, 1, 0};
}

// Every location id in the LocationChecks the server got.
std::vector<std::int64_t> checked(const ScriptedApServer &server)
{
    std::vector<std::int64_t> ids;
    for (const json &packet : server.received("LocationChecks"))
        for (const json &id : packet["locations"])
            ids.push_back(id.get<std::int64_t>());
    return ids;
}

bool contains(const std::vector<std::int64_t> &ids, std::int64_t id)
{
    return std::ranges::find(ids, id) != ids.end();
}

// Members are destroyed in reverse order, so the session closes while the server still runs, and the folder goes last.
struct SessionTest
{
    DataDir dir;
    ScriptedApServer server;
    std::string bind_refusal;
    std::optional<ap::Session> session{std::in_place, dir.path, [this](const ap::Seed &) { return bind_refusal; }};

    static json slot_data()
    {
        return contract_fixture()["slot_data"]["default"];
    }

    // Slot 1 with 8000000101 and 8000000102 missing and 8000000103 checked, and the server's list holding item 0x10.
    void reply_connect(json data = slot_data())
    {
        server.reply_to("Connect", {connected_reply(std::move(data), {8000000101, 8000000102}, {8000000103}), received_items(0, {item(0x10)})});
    }

    // The three locations hold 0x20, 0x21 and 0x22 for slot 1, listed out of order.
    void reply_scout()
    {
        server.reply_to("LocationScouts", {location_info({{0x22, 8000000103, 1, 0}, {0x20, 8000000101, 1, 0}, {0x21, 8000000102, 1, 0}})});
    }

    void connect()
    {
        session->connect("127.0.0.1:" + std::to_string(server.port()), "Player1", "");
    }

    template <class Pred> void tick_until(Pred pred)
    {
        wait_until(
            [&]
            {
                session->tick();
                return pred();
            });
    }

    void bind()
    {
        reply_connect();
        reply_scout();
        connect();
        tick_until([] { return ap::seed() != nullptr; });
    }

    bool refused() const
    {
        return session->status().link == ap::LinkState::Refused;
    }
};

} // namespace

TEST_CASE_METHOD(SessionTest, "connecting binds the seed with every location of the slot and what it holds")
{
    bind();
    const ap::Seed *bound = ap::seed();
    CHECK(bound->name == "seed-a");
    CHECK(bound->slot == 1);
    CHECK(bound->locations == std::vector<ap::NetworkItem>{{0x20, 8000000101, 1, 0}, {0x21, 8000000102, 1, 0}, {0x22, 8000000103, 1, 0}});
    const std::vector<json> scouts = server.received("LocationScouts");
    REQUIRE(scouts.size() == 1);
    CHECK(scouts[0]["create_as_hint"] == 0);
    std::vector<std::int64_t> scouted = scouts[0]["locations"].get<std::vector<std::int64_t>>();
    std::ranges::sort(scouted);
    CHECK(scouted == std::vector<std::int64_t>{8000000101, 8000000102, 8000000103});
    CHECK(session->status().link == ap::LinkState::Connected);
}

TEST_CASE_METHOD(SessionTest, "a refusal from the server binds nothing and says why")
{
    server.reply_to("Connect", {json{{"cmd", "ConnectionRefused"}, {"errors", json::array({"InvalidSlot"})}}});
    connect();
    tick_until([&] { return refused(); });
    CHECK_THAT(session->status().reason, ContainsSubstring("InvalidSlot"));
    CHECK(ap::seed() == nullptr);
    CHECK(server.received("LocationScouts").empty());
}

TEST_CASE_METHOD(SessionTest, "slot_data the client can't play refuses the seed by name")
{
    json data = slot_data();
    data["RandomizeShops"] = 1;
    reply_connect(data);
    connect();
    tick_until([&] { return refused(); });
    CHECK_THAT(session->status().reason, ContainsSubstring("RandomizeShops"));
    CHECK(ap::seed() == nullptr);
    CHECK(server.received("LocationScouts").empty());
}

TEST_CASE_METHOD(SessionTest, "an unknown slot_data key binds and is reported")
{
    json data = slot_data();
    data["Goal"] = 0;
    reply_connect(data);
    reply_scout();
    connect();
    tick_until([] { return ap::seed() != nullptr; });
    CHECK(session->status().unknown_keys == std::vector<std::string>{"Goal"});
}

TEST_CASE_METHOD(SessionTest, "the received list follows the server across overlaps, gaps and reconnects")
{
    bind();
    server.push(received_items(1, {item(0x11)}));
    server.push(received_items(1, {item(0x11), item(0x12)}));
    tick_until([&] { return session->received().size() >= 3; });
    CHECK(session->received() == std::vector<ap::NetworkItem>{item(0x10), item(0x11), item(0x12)});

    const std::vector<ap::NetworkItem> full{item(0x10), item(0x11), item(0x12), item(0x13), item(0x14), item(0x15)};
    server.reply_to("Sync", {received_items(0, full)});
    server.push(received_items(5, {item(0x15)}));
    tick_until([&] { return session->received().size() >= 6; });
    CHECK(server.received("Sync").size() == 1);
    CHECK(session->received() == full);

    server.reply_to("Connect", {connected_reply(slot_data(), {8000000101, 8000000102}, {8000000103}), received_items(0, full)});
    server.drop();
    tick_until([&] { return server.received("Connect").size() == 2; });
    // The server sends the next item after the resend, so once it arrives the resend has been applied.
    server.push(received_items(6, {item(0x16)}));
    tick_until([&] { return session->received().size() >= 7; });
    std::vector<ap::NetworkItem> expected = full;
    expected.push_back(item(0x16));
    CHECK(session->received() == expected);

    // Index 0 is the server's whole list, so it replaces a longer one.
    server.push(received_items(0, {item(0x30)}));
    tick_until([&] { return session->received().size() == 1; });
    CHECK(session->received() == std::vector<ap::NetworkItem>{item(0x30)});
}

TEST_CASE_METHOD(SessionTest, "a seed connected after another starts with an empty received list and found list")
{
    bind();
    REQUIRE(session->received().size() == 1);
    session->found(8000000102);
    tick_until([&] { return contains(checked(server), 8000000102); });
    const std::size_t before = server.received("LocationChecks").size();
    session->disconnect();
    server.set_seed("seed-b");
    server.reply_to("Connect", {connected_reply(slot_data(), {8000000101, 8000000102}, {8000000103})});
    reply_scout();
    connect();
    tick_until([] { return ap::seed() != nullptr && ap::seed()->name == "seed-b"; });
    CHECK(session->received().empty());
    // The server records a packet on its own thread, so a LocationChecks sent just before the bind may still be on its way.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const std::vector<json> checks = server.received("LocationChecks");
    for (std::size_t i = before; i < checks.size(); ++i)
        CHECK_FALSE(contains(checks[i]["locations"].get<std::vector<std::int64_t>>(), 8000000102));
}

TEST_CASE_METHOD(SessionTest, "reconnecting to a different seed while bound refuses and keeps the bound seed")
{
    bind();
    server.set_seed("seed-b");
    reply_connect();
    server.drop();
    tick_until([&] { return refused(); });
    CHECK_THAT(session->status().reason, ContainsSubstring("seed-b"));
    REQUIRE(ap::seed() != nullptr);
    CHECK(ap::seed()->name == "seed-a");
}

TEST_CASE_METHOD(SessionTest, "a reconnect to a different seed before the bind is refused")
{
    // No scout reply is queued, so the session stays unbound.
    reply_connect();
    connect();
    tick_until([&] { return server.received("LocationScouts").size() == 1; });
    server.set_seed("seed-b");
    reply_connect();
    server.drop();
    tick_until([&] { return refused(); });
    CHECK_THAT(session->status().reason, ContainsSubstring("seed-b"));
    CHECK(ap::seed() == nullptr);
}

TEST_CASE_METHOD(SessionTest, "a location found while the link is down reaches the server after the reconnect")
{
    bind();
    // No Connect reply is queued, so the reconnect waits unauthenticated.
    server.drop();
    tick_until([&] { return server.received("Connect").size() == 2; });
    session->found(8000000102);
    // Gives the net thread time to run the send.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.push(connected_reply(slot_data(), {8000000101, 8000000102}, {8000000103}));
    tick_until([&] { return contains(checked(server), 8000000102); });
}

TEST_CASE_METHOD(SessionTest, "a location found while the link is down never reaches a different seed")
{
    bind();
    server.set_seed("seed-b");
    // No Connect reply is queued, so the reconnect waits unauthenticated.
    server.drop();
    tick_until([&] { return server.received("Connect").size() == 2; });
    session->found(8000000102);
    // Gives the net thread time to run the send.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.push(connected_reply(slot_data(), {8000000101, 8000000102}, {8000000103}));
    tick_until([&] { return refused(); });
    // The server records a packet on its own thread, so a LocationChecks the client sent just before the refusal may still be on its way.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(contains(checked(server), 8000000102));
}

TEST_CASE_METHOD(SessionTest, "the goal reaches the server and is sent again after a reconnect")
{
    bind();
    session->goal();
    tick_until([&] { return server.received("StatusUpdate").size() >= 1; });
    CHECK(server.received("StatusUpdate")[0]["status"] == 30);

    reply_connect();
    server.drop();
    tick_until([&] { return server.received("StatusUpdate").size() >= 2; });
    const std::vector<json> updates = server.received("StatusUpdate");
    CHECK(updates.size() == 2);
    CHECK(updates[1]["status"] == 30);
}

TEST_CASE_METHOD(SessionTest, "an AP save the bind can't use refuses the seed with its reason")
{
    bind_refusal = "seed-a_1.zip can't be read, and no readable .bak is beside it";
    reply_connect();
    reply_scout();
    connect();
    tick_until([&] { return refused(); });
    CHECK(session->status().reason == bind_refusal);
    CHECK(ap::seed() == nullptr);
}

TEST_CASE_METHOD(SessionTest, "the uuid stays the same across launches")
{
    connect();
    tick_until([&] { return server.received("Connect").size() == 1; });
    session.reset();
    session.emplace(dir.path, [this](const ap::Seed &) { return bind_refusal; });
    connect();
    tick_until([&] { return server.received("Connect").size() == 2; });
    const std::vector<json> connects = server.received("Connect");
    const std::string first = connects[0]["uuid"].get<std::string>();
    REQUIRE_FALSE(first.empty());
    CHECK(connects[1]["uuid"] == first);
}

TEST_CASE_METHOD(SessionTest, "a connection the server accepts remembers its server and slot and keeps the uuid")
{
    reply_connect();
    reply_scout();
    connect();
    tick_until([&] { return session->status().link == ap::LinkState::Connected; });
    session.reset();
    session.emplace(dir.path, [this](const ap::Seed &) { return bind_refusal; });
    CHECK(session->saved_login().server == "127.0.0.1:" + std::to_string(server.port()));
    CHECK(session->saved_login().slot == "Player1");
    connect();
    tick_until([&] { return server.received("Connect").size() == 2; });
    const std::vector<json> connects = server.received("Connect");
    CHECK(connects[1]["uuid"] == connects[0]["uuid"]);
}

TEST_CASE_METHOD(SessionTest, "a data folder without a config.json has no saved login")
{
    const ap::Login login = session->saved_login();
    CHECK(login.server.empty());
    CHECK(login.slot.empty());
}

TEST_CASE_METHOD(SessionTest, "a config.json whose keys hold the wrong types has no saved login and gets a fresh uuid")
{
    session.reset();
    std::ofstream(dir.path / "config.json") << R"({"uuid":5,"server":1})";
    session.emplace(dir.path, [this](const ap::Seed &) { return bind_refusal; });
    const ap::Login login = session->saved_login();
    CHECK(login.server.empty());
    CHECK(login.slot.empty());
    connect();
    tick_until([&] { return server.received("Connect").size() == 1; });
    const json uuid = server.received("Connect")[0]["uuid"];
    REQUIRE(uuid.is_string());
    CHECK_FALSE(uuid.get<std::string>().empty());
}
