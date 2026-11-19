#pragma once

#include "ap/net.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "ap/seed.hpp"

// Lets the helpers at the end of this file write items as JSON.
namespace ap
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NetworkItem, item, location, player, flags)
}

// Plain-ws Archipelago server on 127.0.0.1 with an OS-chosen port. Sends RoomInfo on open, answers GetDataPackage with an empty package, answers any
// other client command from a per-command queue of scripted packets, and records every command it receives. Its io thread runs every handler, and
// the methods below are safe to call from the test's thread.
class ScriptedApServer
{
  public:
    using json = nlohmann::json;

    ScriptedApServer()
    {
        server_.clear_access_channels(websocketpp::log::alevel::all);
        server_.clear_error_channels(websocketpp::log::elevel::all);
        server_.init_asio();
        server_.set_reuse_addr(true);
        server_.set_open_handler([this](websocketpp::connection_hdl hdl) { on_open(std::move(hdl)); });
        server_.set_message_handler([this](websocketpp::connection_hdl hdl, Server::message_ptr msg) { on_message(std::move(hdl), msg->get_payload()); });
        server_.listen(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        server_.start_accept();
        asio::error_code ec;
        port_ = server_.get_local_endpoint(ec).port();
        thread_ = std::thread([this] { server_.run(); });
    }

    ~ScriptedApServer()
    {
        server_.stop();
        thread_.join();
    }

    ScriptedApServer(const ScriptedApServer &) = delete;
    ScriptedApServer &operator=(const ScriptedApServer &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return port_;
    }

    // Seed named in the RoomInfo sent to the next connection.
    void set_seed(std::string seed)
    {
        std::lock_guard<std::mutex> lk(mu_);
        seed_ = std::move(seed);
    }

    // The next time the client sends `cmd`, `commands` go back together in one packet, as the real server sends Connected and ReceivedItems.
    void reply_to(const std::string &cmd, std::vector<json> commands)
    {
        std::lock_guard<std::mutex> lk(mu_);
        replies_[cmd].push_back(std::move(commands));
    }

    // Sends one command to the most recently opened connection.
    void push(json command)
    {
        asio::post(server_.get_io_service(), [this, command = std::move(command)] { send(current(), json::array({command})); });
    }

    // Closes the most recently opened connection, as a server restart would, so the client's own reconnect runs.
    void drop()
    {
        asio::post(server_.get_io_service(),
                   [this]
                   {
                       websocketpp::lib::error_code ec;
                       server_.close(current(), websocketpp::close::status::going_away, "", ec);
                   });
    }

    [[nodiscard]] std::vector<json> received() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return received_;
    }

    [[nodiscard]] std::vector<json> received(const std::string &cmd) const
    {
        std::vector<json> out;
        for (json &c : received())
            if (c.value("cmd", "") == cmd)
                out.push_back(std::move(c));
        return out;
    }

  private:
    using Server = websocketpp::server<websocketpp::config::asio>;

    websocketpp::connection_hdl current() const
    {
        std::lock_guard<std::mutex> lk(mu_);
        return current_;
    }

    void send(const websocketpp::connection_hdl &hdl, const json &packet)
    {
        websocketpp::lib::error_code ec;
        server_.send(hdl, packet.dump(), websocketpp::frame::opcode::text, ec);
    }

    void on_open(websocketpp::connection_hdl hdl)
    {
        std::string seed;
        {
            std::lock_guard<std::mutex> lk(mu_);
            current_ = hdl;
            seed = seed_;
        }
        const json version = {{"major", 0}, {"minor", 6}, {"build", 7}, {"class", "Version"}};
        const json room_info = {{"cmd", "RoomInfo"}, {"time", 0.0},           {"version", version}, {"generator_version", version},
                                {"seed_name", seed}, {"games", json::array()}};
        send(hdl, json::array({room_info}));
    }

    void on_message(const websocketpp::connection_hdl &hdl, const std::string &payload)
    {
        const json packet = json::parse(payload, nullptr, false);
        if (!packet.is_array())
            return;
        for (const json &command : packet)
        {
            const std::string cmd = command.value("cmd", "");
            json reply = json::array();
            {
                std::lock_guard<std::mutex> lk(mu_);
                received_.push_back(command);
                if (auto it = replies_.find(cmd); it != replies_.end() && !it->second.empty())
                {
                    for (json &c : it->second.front())
                        reply.push_back(std::move(c));
                    it->second.pop_front();
                }
            }
            if (cmd == "GetDataPackage")
                reply.push_back({{"cmd", "DataPackage"}, {"data", {{"games", json::object()}}}});
            if (!reply.empty())
                send(hdl, reply);
        }
    }

    Server server_;
    std::uint16_t port_{0};
    mutable std::mutex mu_;
    std::string seed_{"seed-a"};                                   // guarded by mu_
    std::map<std::string, std::deque<std::vector<json>>> replies_; // guarded by mu_
    std::vector<json> received_;                                   // guarded by mu_
    websocketpp::connection_hdl current_;                          // guarded by mu_
    std::thread thread_;
};

// Calls pred every millisecond until it holds, and fails the test once 10 s have passed.
template <class Pred> void wait_until(Pred pred)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            FAIL("the condition still didn't hold after 10 s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// A Connected reply for slot 1 on team 0.
inline nlohmann::json connected_reply(nlohmann::json slot_data, const std::vector<std::int64_t> &missing, const std::vector<std::int64_t> &checked)
{
    return {{"cmd", "Connected"},
            {"team", 0},
            {"slot", 1},
            {"players", nlohmann::json::array({{{"team", 0}, {"slot", 1}, {"alias", "Player1"}, {"name", "Player1"}}})},
            {"missing_locations", missing},
            {"checked_locations", checked},
            {"slot_data", std::move(slot_data)},
            {"slot_info", nlohmann::json::object()},
            {"hint_points", 0}};
}

inline nlohmann::json received_items(int index, const std::vector<ap::NetworkItem> &items)
{
    return {{"cmd", "ReceivedItems"}, {"index", index}, {"items", items}};
}

// The reply to LocationScouts.
inline nlohmann::json location_info(const std::vector<ap::NetworkItem> &items)
{
    return {{"cmd", "LocationInfo"}, {"locations", items}};
}
