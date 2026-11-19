#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ap
{

// One item as the server describes it. In ReceivedItems, player is the slot that found it; in a scout reply, the slot that receives it.
struct NetworkItem
{
    std::int64_t item;
    std::int64_t location;
    int player;
    int flags;

    bool operator==(const NetworkItem &) const = default;
};

// Everything about the bound seed that hooks and the save redirect read. Immutable once published.
struct Seed
{
    std::string name;
    int slot;
    std::filesystem::path folder;       // <data dir>\seeds\<name>_<slot>
    std::vector<NetworkItem> locations; // every location of the slot with what it holds, sorted by location
};

// Written only by publish. ponytail: published seeds are never freed, one leak of a few KB per bind; reclaim them at a point no reader can still
// hold one if binds ever become frequent.
inline std::atomic<const Seed *> published_seed{nullptr};

// Any thread: the bound seed, or null.
inline const Seed *seed()
{
    return published_seed.load(std::memory_order_acquire);
}

// Main thread only: publishes s, or unbinds with null. A published seed is never freed, so no reader sees one disappear.
inline void publish(const Seed *s)
{
    published_seed.store(s, std::memory_order_release);
}

} // namespace ap
