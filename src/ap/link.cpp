#include "ap/net.hpp"

#include "ap/link.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <list>
#include <set>
#include <string_view>
#include <utility>

#include "ap/slot_data.hpp"
#include "support/crash.hpp"
#include "support/log.hpp"

namespace ap
{
namespace
{

using namespace std::chrono_literals;

// The URI handed to APClient. A scheme pins one, and for a schemeless URI apclientpp tries wss first, then alternates with ws after each failure.
// Local servers rarely terminate TLS and each refused scheme costs a reconnect interval, so loopback names get ws://.
std::string plan_connection(std::string_view server)
{
    if (server.starts_with("localhost") || server.starts_with("127.0.0.1"))
        return "ws://" + std::string(server);
    return std::string(server);
}

// Keeps no data package. The client shows no item or location names, and apclientpp's own store would write a cache to disk.
struct NoDataPackageStore final : APDataPackageStore
{
    bool load(const std::string &, const std::string &, json &) override
    {
        return false;
    }
    bool save(const std::string &, const json &) override
    {
        return true;
    }
};
NoDataPackageStore no_data_package;

std::vector<NetworkItem> to_items(const std::list<APClient::NetworkItem> &items)
{
    std::vector<NetworkItem> out;
    out.reserve(items.size());
    for (const APClient::NetworkItem &item : items)
        out.push_back({item.item, item.location, item.player, static_cast<int>(item.flags)});
    return out;
}

} // namespace

Link::Link() : thread_([this] { run(); })
{
}

Link::~Link()
{
    enqueue([this] { running_ = false; });
    thread_.join();
}

void Link::enqueue(std::function<void()> command)
{
    std::lock_guard lock(commands_mutex_);
    commands_.push_back(std::move(command));
}

// Main thread: drops every undrained event and starts a new epoch, then queues command to run under it.
void Link::start_epoch(std::function<void()> command)
{
    std::uint64_t epoch;
    {
        std::lock_guard lock(events_mutex_);
        events_.clear();
        epoch = ++epoch_;
    }
    enqueue(
        [this, epoch, command = std::move(command)]
        {
            pushing_epoch_ = epoch;
            command();
        });
}

void Link::connect(std::string server, std::string slot, std::string password, std::string uuid)
{
    start_epoch([this, server = std::move(server), slot = std::move(slot), password = std::move(password), uuid = std::move(uuid)]
                { open(server, slot, password, uuid); });
}

void Link::disconnect()
{
    start_epoch([this] { close(); });
}

// Main thread: queues send for the net thread, which runs it only on an authenticated connection to the pinned seed and slot, and drops it
// otherwise. apclientpp would queue a send made before authentication and flush it into the next Connected, whichever seed that is.
// The session sends what matters again after every Connected, so a dropped send isn't lost.
void Link::enqueue_send(std::function<void(APClient &)> send)
{
    enqueue(
        [this, send = std::move(send)]
        {
            if (client_ && client_->get_state() == APClient::State::SLOT_CONNECTED && !wrong_seed())
                send(*client_);
        });
}

void Link::send_locations(std::vector<std::int64_t> locations)
{
    enqueue_send([locations = std::move(locations)](APClient &client) { client.LocationChecks(std::list<std::int64_t>(locations.begin(), locations.end())); });
}

void Link::scout(std::vector<std::int64_t> locations)
{
    enqueue_send([locations = std::move(locations)](APClient &client)
                 { client.LocationScouts(std::list<std::int64_t>(locations.begin(), locations.end()), 0); });
}

void Link::send_goal()
{
    enqueue_send([](APClient &client) { client.StatusUpdate(APClient::ClientStatus::GOAL); });
}

void Link::sync()
{
    enqueue_send([](APClient &client) { client.Sync(); });
}

std::vector<Event> Link::drain()
{
    std::lock_guard lock(events_mutex_);
    return std::exchange(events_, {});
}

void Link::run()
{
    support::crash_watch_thread();
    support::log(support::Level::Info, "net thread started");
    while (running_)
    {
        std::vector<std::function<void()>> commands;
        {
            std::lock_guard lock(commands_mutex_);
            commands.swap(commands_);
        }
        for (const std::function<void()> &command : commands)
        {
            // websocketpp's send throws while a connection closes, and an exception that left this thread would end the game. The poll sees the drop.
            try
            {
                command();
            }
            catch (const std::exception &e)
            {
                support::log(support::Level::Warn, "net command failed: %s", e.what());
            }
        }

        if (client_)
        {
            try
            {
                client_->poll();
            }
            catch (const std::exception &e)
            {
                support::log(support::Level::Error, "net poll failed: %s", e.what());
                if (client_->get_state() == APClient::State::SLOT_CONNECTED)
                    end(Disconnected{});
                else
                    end(Refused{std::string("connection failed: ") + e.what()});
            }
        }
        if (deadline_ && std::chrono::steady_clock::now() > *deadline_)
            end(Refused{"connection timed out"});
        // Closing waits for the poll to return, because apclientpp can't destroy itself from inside one of its own callbacks.
        if (ending_)
            close();
        std::this_thread::sleep_for(10ms);
    }
    close();
}

void Link::open(const std::string &server, const std::string &slot, const std::string &password, const std::string &uuid)
{
    close();
    if (server.empty() || slot.empty())
    {
        end(Refused{"a server and a slot name are required"});
        return;
    }
    const std::string uri = plan_connection(server);
    try
    {
        client_ = std::make_unique<APClient>(uuid, "Okami HD", uri, "", &no_data_package);
    }
    catch (const std::exception &e)
    {
        end(Refused{std::string("connection failed: ") + e.what()});
        return;
    }

    // apclientpp flips a schemeless URI's scheme on every socket error and never exposes the live one, so the handler tracks which leg failed.
    client_->set_socket_error_handler(
        [alternates = !uri.starts_with("ws://") && !uri.starts_with("wss://"), encrypted = !uri.starts_with("ws://")](const std::string &message) mutable
        {
            support::log(support::Level::Warn, "%s connection failed (%s)%s", encrypted ? "encrypted" : "plaintext", message.c_str(),
                         alternates ? (encrypted ? ", retrying in plaintext" : ", retrying encrypted") : "");
            if (alternates)
                encrypted = !encrypted;
        });
    // apclientpp reconnects by itself after a drop. The deadline covers only the first authentication, so once the slot has authenticated the link
    // keeps retrying while play continues.
    client_->set_socket_disconnected_handler([this] { push(Disconnected{}); });
    client_->set_room_info_handler([this, slot, password] { client_->ConnectSlot(slot, password, 0b111); });
    client_->set_slot_connected_handler(
        [this](const nlohmann::json &slot_data)
        {
            // apclientpp swallows a handler's exception, so a slot_data that throws is refused here.
            SlotDataCheck check;
            try
            {
                check = check_slot_data(slot_data);
            }
            catch (const std::exception &e)
            {
                check.refusal = std::string("invalid slot_data: ") + e.what();
            }
            if (!check.refusal.empty())
            {
                end(Refused{check.refusal});
                return;
            }
            // The first authentication after connect pins the seed and slot. The loop closes an ended connection before it runs another
            // command, so ending a reconnect to another seed here, in the poll that authenticated it, keeps every send away from it.
            if (pinned_seed_.empty())
            {
                pinned_seed_ = client_->get_seed();
                pinned_slot_ = client_->get_player_number();
            }
            else if (wrong_seed())
            {
                end(Refused{std::format("this game is playing seed {} slot {}, but the server now hosts seed {} slot {}; return to the title to switch",
                                        pinned_seed_, pinned_slot_, client_->get_seed().substr(0, 64), client_->get_player_number())});
                return;
            }
            deadline_.reset();
            const std::set<std::int64_t> missing = client_->get_missing_locations();
            const std::set<std::int64_t> checked = client_->get_checked_locations();
            std::vector<std::int64_t> locations(missing.begin(), missing.end());
            locations.insert(locations.end(), checked.begin(), checked.end());
            std::ranges::sort(locations);
            push(Connected{client_->get_seed(), client_->get_player_number(), std::move(locations), std::move(check.unknown_keys)});
        });
    client_->set_slot_refused_handler(
        [this](const std::list<std::string> &errors)
        {
            std::string reason = "the server refused the connection:";
            const char *separator = " ";
            for (const std::string &error : errors)
            {
                reason += separator + error;
                separator = ", ";
            }
            end(Refused{reason});
        });
    client_->set_items_received_handler(
        [this](const std::list<APClient::NetworkItem> &items)
        {
            if (!items.empty())
                push(ReceivedItems{items.front().index, to_items(items)});
        });
    client_->set_location_info_handler([this](const std::list<APClient::NetworkItem> &items) { push(ScoutReply{to_items(items)}); });

    deadline_ = std::chrono::steady_clock::now() + 30s;
    push(Connecting{});
    support::log(support::Level::Info, "connecting to %s", uri.c_str());
}

// Destroying the client reports nothing, because wswrap drops its connection before the close completes and its handlers ignore a dropped connection.
void Link::close()
{
    client_.reset();
    deadline_.reset();
    ending_ = false;
    pinned_seed_.clear();
}

// Net thread, with a client: whether it serves a seed or slot other than the pinned one.
bool Link::wrong_seed() const
{
    return client_->get_seed() != pinned_seed_ || client_->get_player_number() != pinned_slot_;
}

// Once a connection has reported how it ends, nothing more is pushed for it, so a late event can't overwrite the reason. An event pushed after main
// has started a newer epoch belongs to a connection main has closed, and is dropped too.
void Link::push(Event event)
{
    if (ending_)
        return;
    std::lock_guard lock(events_mutex_);
    if (pushing_epoch_ == epoch_)
        events_.push_back(std::move(event));
}

void Link::end(Event event)
{
    push(std::move(event));
    ending_ = true;
}

} // namespace ap
