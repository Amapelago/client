#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ap/link.hpp"
#include "ap/seed.hpp"

namespace ap
{

enum class LinkState
{
    Disconnected,
    Connecting,
    Connected,
    Refused
};

struct Status
{
    LinkState link = LinkState::Disconnected;
    std::string reason;                    // why the last connection was refused
    std::vector<std::string> unknown_keys; // slot_data keys of the current seed the client doesn't know
};

// The server and slot the player last connected with.
struct Login
{
    std::string server;
    std::string slot;
};

// Main thread only. Owns the link, the status, the received list, the found list and the bind.
class Session
{
  public:
    explicit Session(std::filesystem::path data_dir);
    ~Session(); // unbinds
    // Closes any connection and unbinds, then connects. The caller allows this only at the title.
    void connect(const std::string &server, const std::string &slot, const std::string &password);
    // Closes the connection, unbinds and clears the goal and the unknown keys. The caller allows this only at the title.
    void disconnect();
    // Once per frame: applies the link's events.
    void tick();
    // Main thread, with a seed bound: a location of that seed was found. Appends it to found.txt, then sends it.
    void found(std::int64_t location);
    // The goal holds: sends it now and after every later Connected until the next connect or disconnect.
    void goal();
    const std::vector<NetworkItem> &received() const; // the server's list from index 0
    const Status &status() const;
    // The server and slot in config.json, written when a server accepts a slot, or empty strings when none are saved.
    Login saved_login() const;
    // The print messages that arrived since the last call, oldest first.
    std::vector<std::string> take_prints();

  private:
    void on(Connecting &);
    void on(Connected &connected);
    void on(Refused &refused);
    void on(Disconnected &);
    void on(ReceivedItems &packet);
    void on(ScoutReply &reply);
    void on(Print &print);
    void set_link(LinkState link);
    void refuse(std::string reason);
    std::filesystem::path folder() const;

    std::filesystem::path data_dir_;
    Link link_;
    Status status_;
    std::vector<NetworkItem> received_;
    std::vector<std::string> prints_;
    Login attempt_;         // what connect was given, saved once the server accepts it
    std::string seed_name_; // the seed and slot of the last Connected, which a scout reply binds
    int slot_ = 0;
    bool goal_ = false; // set by goal(), cleared by unbinding
};

} // namespace ap
