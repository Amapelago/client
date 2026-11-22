// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ap/session.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <random>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "support/log.hpp"

namespace ap
{
namespace
{

// config.json as an object: empty when the file is missing, unreadable or not an object.
nlohmann::json read_config(const std::filesystem::path &data_dir)
{
    nlohmann::json config = nlohmann::json::parse(std::ifstream(data_dir / "config.json"), nullptr, false);
    return config.is_object() ? config : nlohmann::json::object();
}

// The string under key, or an empty one when the key is missing or holds something else.
std::string string_at(const nlohmann::json &config, const char *key)
{
    const auto it = config.find(key);
    return it != config.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Reads config.json, lets edit change it, and writes it back, so every writer keeps the other keys.
template <class Edit> void edit_config(const std::filesystem::path &data_dir, Edit edit)
{
    nlohmann::json config = read_config(data_dir);
    edit(config);
    std::ofstream out(data_dir / "config.json");
    out << config.dump();
    out.close();
    if (!out)
        support::log(support::Level::Warn, "can't write config.json, so the next launch loses what it holds");
}

// The uuid in config.json, made there when it is missing or not a string.
std::string load_uuid(const std::filesystem::path &data_dir)
{
    if (std::string uuid = string_at(read_config(data_dir), "uuid"); !uuid.empty())
        return uuid;
    std::random_device entropy;
    std::string uuid;
    for (int i = 0; i < 32; ++i)
        uuid += "0123456789abcdef"[entropy() % 16];
    edit_config(data_dir, [&](nlohmann::json &config) { config["uuid"] = uuid; });
    return uuid;
}

} // namespace

Session::Session(std::filesystem::path data_dir, std::function<std::string(const Seed &)> bind_save)
    : data_dir_(std::move(data_dir)), bind_save_(std::move(bind_save))
{
}

Session::~Session()
{
    publish(nullptr);
}

void Session::connect(const std::string &server, const std::string &slot, const std::string &password)
{
    disconnect();
    attempt_ = {server, slot};
    link_.connect(server, slot, password, load_uuid(data_dir_));
}

// The link pushes no event for a close the session asks for, so the session sets the state itself.
void Session::disconnect()
{
    link_.disconnect();
    publish(nullptr);
    goal_ = false;
    found_.clear();
    status_.unknown_keys.clear();
    set_link(LinkState::Disconnected);
}

void Session::tick()
{
    for (Event &event : link_.drain())
    {
        std::visit([this](auto &e) { on(e); }, event);
        // A refusal ends its connection, so whatever else that connection sent is dropped.
        if (status_.link == LinkState::Refused)
            break;
    }
}

void Session::found(std::int64_t location)
{
    found_.push_back(location);
    link_.send_locations({location});
}

void Session::goal()
{
    goal_ = true;
    link_.send_goal();
}

const std::vector<NetworkItem> &Session::received() const
{
    return received_;
}

Login Session::saved_login() const
{
    const nlohmann::json config = read_config(data_dir_);
    return {string_at(config, "server"), string_at(config, "slot")};
}

std::vector<std::string> Session::take_prints()
{
    return std::exchange(prints_, {});
}

void Session::print(std::string text)
{
    prints_.push_back(std::move(text));
}

const Status &Session::status() const
{
    return status_;
}

void Session::on(Connecting &)
{
    set_link(LinkState::Connecting);
}

void Session::on(Connected &connected)
{
    set_link(LinkState::Connected);
    edit_config(data_dir_,
                [this](nlohmann::json &config)
                {
                    config["server"] = attempt_.server;
                    config["slot"] = attempt_.slot;
                });
    for (const std::string &key : connected.unknown_keys)
        support::log(support::Level::Warn, "slot_data key %s is unknown to this client", key.c_str());
    status_.unknown_keys = std::move(connected.unknown_keys);
    seed_name_ = connected.seed_name;
    slot_ = connected.slot;
    if (!seed())
    {
        received_.clear();
        link_.scout(std::move(connected.locations));
    }
    // The found list goes out whole after every Connected, so a check the server missed while the link was down still reaches it.
    link_.send_locations(found_);
    if (goal_)
        link_.send_goal();
}

void Session::on(Refused &refused)
{
    refuse(std::move(refused.reason));
}

void Session::on(Print &print)
{
    prints_.push_back(std::move(print.text));
}

void Session::on(Disconnected &)
{
    set_link(LinkState::Disconnected);
}

// Index 0 is the whole list. An overlapping packet adds only what lies past the list's end. A gap asks for the whole list again, which fills it.
void Session::on(ReceivedItems &packet)
{
    if (status_.link != LinkState::Connected)
        return;
    if (packet.index == 0)
        received_.clear();
    const std::size_t have = received_.size();
    const auto index = static_cast<std::size_t>(packet.index);
    if (index > have)
    {
        link_.sync();
        return;
    }
    if (have - index < packet.items.size())
        received_.insert(received_.end(), packet.items.begin() + static_cast<std::ptrdiff_t>(have - index), packet.items.end());
}

void Session::on(ScoutReply &reply)
{
    if (status_.link != LinkState::Connected || seed())
        return;
    auto bound = std::make_unique<Seed>(Seed{seed_name_, slot_, std::move(reply.items)});
    std::ranges::sort(bound->locations, {}, &NetworkItem::location);
    if (std::string refusal = bind_save_(*bound); !refusal.empty())
    {
        refuse(std::move(refusal));
        return;
    }
    publish(bound.release());
    support::log(support::Level::Info, "bound seed %s slot %d, %zu locations", seed()->name.c_str(), seed()->slot, seed()->locations.size());
}

void Session::set_link(LinkState link)
{
    static constexpr const char *names[] = {"disconnected", "connecting", "connected", "refused"};
    if (status_.link != link)
        support::log(support::Level::Info, "link %s", names[static_cast<int>(link)]);
    status_.link = link;
}

// Every refusal closes the link. For one the link pushed, the link has already ended the connection and the close does nothing more.
void Session::refuse(std::string reason)
{
    link_.disconnect();
    support::log(support::Level::Warn, "refused: %s", reason.c_str());
    status_.reason = std::move(reason);
    set_link(LinkState::Refused);
}

} // namespace ap
