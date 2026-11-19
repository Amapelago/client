#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "ap/seed.hpp"

class APClient;

namespace ap
{

struct Connecting
{
};
struct Connected
{
    std::string seed_name;
    int slot;
    std::vector<std::int64_t> locations;   // missing plus checked: every location of the slot, sorted
    std::vector<std::string> unknown_keys; // from check_slot_data
};
// The connection is over and nothing retries until the next connect.
struct Refused
{
    std::string reason;
};
// A connection dropped that the main thread didn't close. apclientpp reconnects by itself, unless the poll itself failed, which ends the connection
// as Refused does.
struct Disconnected
{
};
struct ReceivedItems
{
    int index; // position of items[0] in the server's list
    std::vector<NetworkItem> items;
};
struct ScoutReply
{
    std::vector<NetworkItem> items;
};
using Event = std::variant<Connecting, Connected, Refused, Disconnected, ReceivedItems, ScoutReply>;

// Owns the net thread and the APClient on it. Every method runs on the main thread, and the command and event queues are the only state the two
// threads share. Once connect or disconnect returns, drain never returns an event of the connection it closed. Sends reach only an authenticated
// connection, and a connection keeps talking only to the seed and slot it first authenticated with: a reconnect to another seed is refused.
class Link
{
  public:
    Link();  // starts the net thread
    ~Link(); // stops and joins it
    // Closes any connection, then connects. Closing pushes no event.
    void connect(std::string server, std::string slot, std::string password, std::string uuid);
    // Closes the connection and pushes no event.
    void disconnect();
    // A send made while the slot isn't authenticated is dropped.
    void send_locations(std::vector<std::int64_t> locations);
    void scout(std::vector<std::int64_t> locations);
    void send_goal();
    void sync();
    std::vector<Event> drain(); // events since the last drain, oldest first

  private:
    void enqueue(std::function<void()> command);
    void enqueue_send(std::function<void(APClient &)> send);
    void start_epoch(std::function<void()> command);

    // Net thread only.
    void run();
    void open(const std::string &server, const std::string &slot, const std::string &password, const std::string &uuid);
    void close();
    bool wrong_seed() const;
    void push(Event event);
    void end(Event event);

    // The two queues, each behind its own mutex.
    std::mutex commands_mutex_;
    std::vector<std::function<void()>> commands_; // main appends, the net thread takes them all
    std::mutex events_mutex_;
    std::vector<Event> events_; // the net thread appends, main takes them all
    // Guarded by events_mutex_. connect and disconnect clear events_ and bump the epoch, and the net thread pushes only for the current one. So once connect or
    // disconnect returns, drain never returns an event of the connection it closed, even one pushed before the net thread runs the command.
    std::uint64_t epoch_ = 0;

    // Net thread only.
    std::unique_ptr<APClient> client_;
    std::uint64_t pushing_epoch_ = 0;                               // the epoch of the last connect or disconnect the net thread ran
    std::optional<std::chrono::steady_clock::time_point> deadline_; // set by connect, cleared once the slot authenticates
    std::string pinned_seed_;                                       // set by the first authentication after connect, cleared by close
    int pinned_slot_ = 0;                                           // set with pinned_seed_
    bool ending_ = false;                                           // the connection reported how it ends, and the loop closes it after the poll
    bool running_ = true;                                           // cleared by the command the destructor queues

    std::thread thread_; // last, so the thread starts once every other member exists
};

} // namespace ap
