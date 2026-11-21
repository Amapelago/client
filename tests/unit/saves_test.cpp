// The save redirect and the autosave against fake game memory and a fake Steam remote storage. The test thread that runs saves_frame plays the game's
// main thread, and the others play the hx::SaveDataManager worker that calls Steam.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ap/seed.hpp"
#include "game/addresses.hpp"
#include "game/save_file.hpp"
#include "game/saves.hpp"
#include "support/race.hpp"

namespace
{

// A fresh temp folder, removed afterwards.
struct Dir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("okami_apclient_saves_" + std::to_string(std::random_device{}()));

    Dir()
    {
        std::filesystem::create_directories(path);
    }
    ~Dir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// A record the game loads, every byte from +0x18 set to m.
std::vector<std::uint8_t> record(std::uint8_t m)
{
    std::vector<std::uint8_t> r(game::kRecordSize, m);
    std::fill(r.begin(), r.begin() + 0x18, std::uint8_t{0});
    const std::uint32_t magic = 0x40400000;
    std::memcpy(r.data(), &magic, 4);
    std::uint64_t sum = 0x9be6fa3b72afda1dull;
    for (std::size_t at = 0; at < r.size(); at += 8)
    {
        std::uint64_t q;
        std::memcpy(&q, &r[at], 8);
        if (at != 8)
            sum ^= q;
    }
    std::memcpy(&r[8], &sum, 8);
    return r;
}

// A copy of an OKAMI image with r in slot.
std::vector<std::uint8_t> with(std::vector<std::uint8_t> image, std::size_t slot, const std::vector<std::uint8_t> &r)
{
    std::copy(r.begin(), r.end(), image.begin() + slot * game::kRecordSize);
    return image;
}

// What the fake SaveRecord_Serialize writes: record(marker).
std::uint8_t marker = 0;

bool can_open()
{
    return true;
}

void serialize(void *ctx)
{
    const std::vector<std::uint8_t> r = record(marker);
    std::memcpy(*reinterpret_cast<std::uint8_t **>(static_cast<std::uint8_t *>(ctx) + 0x220), r.data(), r.size());
}

void *context_init(void *data)
{
    return data;
}

// Steam's remote storage before the redirect: every entry is null, so a call that passes through to Steam crashes the test.
std::array<void *, 55> null_entries{};
struct Storage
{
    void **vtable;
} storage{null_entries.data()};

// SteamInternal_ContextInit's result, with the remote storage at +0x48.
struct Context
{
    std::uint8_t head[0x48];
    Storage *storage;
} context{{}, &storage};

// Game memory at rest, with the map task in state 4.
struct Fake
{
    std::uint16_t room = 0;
    std::uint32_t title = 0;
    std::array<std::uint32_t, 8> ui{};
    std::uint32_t map_task = 4, engine = 0;
    std::vector<std::uint8_t> player_block = std::vector<std::uint8_t>(0x1000);
    void *player = player_block.data();
    std::int32_t area = 0;
    std::uint8_t use_pos = 0;
    std::vector<std::uint8_t> cache = std::vector<std::uint8_t>(game::kRecordSize);
} fake;

game::Addresses addresses()
{
    game::Addresses a;
    a.current_room = &fake.room;
    a.title_state = &fake.title;
    a.save_prompt_can_open = &can_open;
    a.ui_mode_flags = fake.ui.data();
    a.map_task_state = &fake.map_task;
    a.engine_flags = &fake.engine;
    a.player = &fake.player;
    a.save_area_code = &fake.area;
    a.save_use_pos = &fake.use_pos;
    a.cached_save_record = fake.cache.data();
    a.serialize_save_record = &serialize;
    a.steam_context_init_data = &context;
    a.steam_context_init = &context_init;
    return a;
}

template <typename F> F entry(std::size_t offset)
{
    return reinterpret_cast<F>(storage.vtable[offset / 8]);
}

// A call result as the game makes one. Run copies the result and, for a read, fetches the bytes through FileReadAsyncComplete into buffer, as the game's
// completion does.
struct CallResult
{
    void *const *vtable;
    std::vector<std::uint8_t> *buffer;
    std::array<std::uint8_t, 0x18> result{};
};

void run(void *self, void *param, bool, std::uint64_t call)
{
    CallResult &c = *static_cast<CallResult *>(self);
    std::memcpy(c.result.data(), param, c.buffer ? 0x18 : 4);
    if (c.buffer)
        entry<bool (*)(void *, std::uint64_t, void *, std::uint32_t)>(0x20)(&storage, call, c.buffer->data(), static_cast<std::uint32_t>(c.buffer->size()));
}

void *const run_vtable[] = {reinterpret_cast<void *>(&run)};

// Reads the first size bytes of OKAMI.
std::vector<std::uint8_t> read_okami(std::size_t size)
{
    std::vector<std::uint8_t> image(size);
    const std::uint64_t call =
        entry<std::uint64_t (*)(void *, const char *, std::uint32_t, std::uint32_t)>(0x18)(&storage, "./Steam/OKAMI", 0, static_cast<std::uint32_t>(size));
    CallResult result{run_vtable, &image};
    game::register_call_result_detour(&result, call);
    return image;
}

bool write_okami(const std::vector<std::uint8_t> &image)
{
    const std::uint64_t call = entry<std::uint64_t (*)(void *, const char *, const void *, std::uint32_t)>(0x10)(&storage, "./Steam/OKAMI", image.data(),
                                                                                                                 static_cast<std::uint32_t>(image.size()));
    CallResult result{run_vtable, nullptr};
    game::register_call_result_detour(&result, call);
    std::int32_t eresult;
    std::memcpy(&eresult, result.result.data(), 4);
    return eresult == 1;
}

// Whether slot 0 of an OKAMI image holds one whole record(m), rather than parts of two.
bool whole(const std::vector<std::uint8_t> &image)
{
    return std::ranges::equal(std::span(image).first(game::kRecordSize), record(image[0x18]));
}

} // namespace

TEST_CASE("captured game saves race autosaves without a torn record, a failed write, or a file that differs from what is served")
{
    Dir dir;
    game::saves_start(addresses(), dir.path);
    const ap::Seed seed{"race", 1, {}, {}};
    REQUIRE(game::saves_bind(seed).empty());
    ap::publish(&seed);
    // seed lives on this stack, so it is unpublished even when a REQUIRE ends the test early.
    struct Unpublish
    {
        ~Unpublish()
        {
            ap::publish(nullptr);
        }
    } unpublish;
    // Leaving the title arms the autosave, and the next arrival saves, so slot 0 holds a record before the race.
    for (const std::uint16_t room : {0x0C00, 0x0100, 0x0101})
    {
        fake.room = room;
        game::saves_frame();
    }

    // Main autosaves at every arrival until the writer is done. One worker reads slot 0 of OKAMI in a loop, the read the game makes when a slot is
    // confirmed, so its copy often overlaps an autosave's. The other reads the whole image and writes a new record over its slot 0. After each frame main
    // checks that the last commit went through, since two commits that overlap clash over the same files.
    std::atomic<bool> torn{false}, failed{false}, done{false};
    race_threads(3,
                 [&](int t)
                 {
                     if (t == 0)
                         for (int i = 0; !done; ++i)
                         {
                             marker = std::uint8_t(1 + i % 120);
                             fake.room = std::uint16_t(0x0100 + i % 2);
                             game::saves_frame();
                             if (!game::save_problem().empty())
                                 failed = true;
                         }
                     else if (t == 1)
                     {
                         while (!done)
                             if (!whole(read_okami(game::kRecordSize)))
                                 torn = true;
                     }
                     else
                     {
                         for (int i = 0; i < 300; ++i)
                         {
                             const std::vector<std::uint8_t> image = read_okami(game::kSlotCount * game::kRecordSize);
                             if (!whole(image))
                                 torn = true;
                             if (!write_okami(with(image, 0, record(std::uint8_t(128 + i % 120)))))
                                 failed = true;
                         }
                         done = true;
                     }
                 });
    CHECK_FALSE(torn);
    CHECK_FALSE(failed);
    const std::vector<std::uint8_t> served = read_okami(game::kRecordSize);
    const game::OpenedSave opened = game::open_save(dir.path, "race", 1);
    REQUIRE(opened.data);
    CHECK(std::ranges::equal(opened.data->record, served));
}
