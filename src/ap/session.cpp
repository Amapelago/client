#include "ap/session.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <random>
#include <system_error>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "support/log.hpp"

namespace ap
{
namespace
{

// The seed name comes from the server and names the seed's folder, so it must be at most 64 ASCII letters, digits, '-' and '_', which can't point
// outside seeds\. Archipelago's own seed names are digit strings.
bool plain_folder_name(const std::string &name)
{
    return !name.empty() && name.size() <= 64 &&
           std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

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

// Every id in found.txt. A crash during an append can leave the last line without its newline, and a cut-off id may name another location, so
// that line is skipped.
std::vector<std::int64_t> read_found(const std::filesystem::path &folder)
{
    std::vector<std::int64_t> ids;
    std::ifstream in(folder / "found.txt");
    std::string line;
    // getline sets eof only when the file ends before the line's newline.
    while (std::getline(in, line) && !in.eof())
    {
        std::int64_t id;
        const char *end = line.data() + line.size();
        if (const auto [stop, error] = std::from_chars(line.data(), end, id); error == std::errc{} && stop == end)
            ids.push_back(id);
    }
    return ids;
}

// Whether found.txt ends in a line without its newline, as a crash during an append leaves it.
bool ends_cut_off(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    return in.tellg() > 0 && in.seekg(-1, std::ios::end).get() != '\n';
}

// UTF-8, because path::string() throws on Windows for a character the ANSI code page lacks.
std::string utf8(const std::filesystem::path &path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

} // namespace

Session::Session(std::filesystem::path data_dir) : data_dir_(std::move(data_dir))
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
    const std::filesystem::path path = seed()->folder / "found.txt";
    // An id appended to a cut-off line would read as one longer id, which can name another location, so "x\n" first ends that line as one
    // read_found skips.
    const bool cut_off = ends_cut_off(path);
    std::ofstream out(path, std::ios::app);
    if (cut_off)
        out << "x\n";
    out << location << '\n';
    out.close();
    if (!out)
        support::log(support::Level::Error, "can't append %lld to found.txt, so it is sent now but not after a restart", location);
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
    if (!plain_folder_name(connected.seed_name))
    {
        refuse("the server's seed name \"" + connected.seed_name.substr(0, 64) + "\" can't be used as a folder name");
        return;
    }
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
    // The found list goes out whole after every Connected, so a check the server missed while offline or before a restart still reaches it.
    link_.send_locations(read_found(folder()));
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
    const std::filesystem::path dir = folder();
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error)
    {
        refuse("can't create " + utf8(dir) + ": " + error.message());
        return;
    }
    auto *bound = new Seed{seed_name_, slot_, dir, std::move(reply.items)};
    std::ranges::sort(bound->locations, {}, &NetworkItem::location);
    publish(bound);
    support::log(support::Level::Info, "bound seed %s slot %d, %zu locations", bound->name.c_str(), bound->slot, bound->locations.size());
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

std::filesystem::path Session::folder() const
{
    return data_dir_ / "seeds" / (seed_name_ + "_" + std::to_string(slot_));
}

} // namespace ap
