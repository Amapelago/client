#include "game/saves.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "game/play_state.hpp"
#include "game/save_file.hpp"
#include "support/log.hpp"

namespace game
{
namespace
{

using support::Level;
using support::log;

// The served files by the name the game passes after "./", in the order GetFileNameAndSize lists them.
struct Served
{
    const char *name;
    std::size_t size;
};
constexpr std::array<Served, 3> kServed = {{{"Steam/OKAMI", (kSlotCount * kRecordSize)}, {"Steam/SYSTEM", kSystemSize}, {"Steam/PARAM.SFO", kParamSfoSize}}};
constexpr int kOkami = 0, kSystem = 1, kParamSfo = 2;

// ISteamRemoteStorage (STEAMREMOTESTORAGE_INTERFACE_VERSION014) has 55 entries. These are the byte offsets of the served ones.
constexpr std::size_t kEntries = 55, kFileWriteAsync = 0x10, kFileReadAsync = 0x18, kFileReadAsyncComplete = 0x20, kFileDelete = 0x30, kFileExists = 0x68,
                      kGetFileSize = 0x78, kGetFileCount = 0x90, kGetFileNameAndSize = 0x98;

// The handle every served FileReadAsync and FileWriteAsync returns. The calling thread's pending call says whether a call with it is served.
constexpr std::uint64_t kServedCall = ~std::uint64_t{0};

// k_EResultOK and k_EResultFail. The game reports a write that completes with kFail as a failed save.
constexpr std::int32_t kOk = 1, kFail = 2;

constexpr std::uint16_t kTitleRoom = 0x0C00, kPresentsRoom = 0x0C02;

// The title's major state while its menu is up (cTitle + 0x14, low byte).
constexpr std::uint32_t kTitleMenuState = 5;

// RemoteStorageFileReadAsyncComplete_t and RemoteStorageFileWriteAsyncComplete_t, the results Steam passes to a call result's Run.
struct ReadComplete
{
    std::uint64_t call;
    std::int32_t result;
    std::uint32_t offset, read;
};
static_assert(sizeof(ReadComplete) == 0x18);
struct WriteComplete
{
    std::int32_t result;
};

// Written by saves_start at the cold start and only read afterwards.
Addresses g_game;
std::filesystem::path g_dir;

// Main thread only: the last room the frame saw, and whether autosaves are armed and one is due.
std::uint16_t g_room = 0;
bool g_armed = false, g_due = false;

// The copy of the remote storage's vtable that serves the files, and the vtable it replaces. Main fills both at the first bind, before the one pointer
// store that installs the copy, and nothing writes them again.
std::array<void *, kEntries> g_vtable{};
void *const *g_original = nullptr;

// The AP save in memory, guarded by g_mutex: the three served files, whether OKAMI's slot 0 holds a record, the zip, and why its last commit failed.
std::mutex g_mutex;
std::array<std::vector<std::uint8_t>, 3> g_files;
bool g_has_record = false;
SaveFile g_file;
std::string g_problem;

// The served async call this thread made last, which the thread's next SteamAPI_RegisterCallResult completes. A read keeps the range
// FileReadAsyncComplete copies, and a write keeps its result.
struct Pending
{
    bool active = false, read = false;
    int file = 0;
    std::uint32_t offset = 0, size = 0;
    std::int32_t result = 0;
};
thread_local Pending g_pending;

// The index in kServed of the file name refers to, or -1 when no seed is bound or name is another file. The game passes "./Steam/<name>" (confirmed).
int served(const char *name)
{
    if (!ap::seed())
        return -1;
    std::string_view rest = name;
    if (rest.starts_with("./"))
        rest.remove_prefix(2);
    for (int i = 0; i < 3; ++i)
        if (rest == kServed[i].name)
            return i;
    return -1;
}

// The remote storage's own entry at offset, typed as the served entry that stands in for it.
template <typename F> F original(F, std::size_t offset)
{
    return reinterpret_cast<F>(g_original[offset / 8]);
}

// Runs with g_mutex held. Writes the AP save to its zip and keeps the reason a commit fails for save_problem.
void commit()
{
    const std::vector<std::uint8_t> &okami = g_files[kOkami];
    g_problem = commit_save(g_file, {{okami.begin(), okami.begin() + kRecordSize}, g_files[kSystem], g_files[kParamSfo]});
    if (!g_problem.empty())
        log(Level::Error, "AP save not written: %s", g_problem.c_str());
}

// Runs with g_mutex held. Takes the game's write of a served file into the AP save and commits it. Returns false when it refuses the write, which leaves
// the AP save as it was.
bool capture(int file, std::span<const std::uint8_t> written)
{
    std::vector<std::uint8_t> &current = g_files[file];
    if (std::ranges::equal(current, written))
        return true;
    if (file == kOkami)
    {
        // A save changes one slot. Its record moves to slot 0, where the served OKAMI keeps the AP save's record, and the other slots stay empty.
        const std::optional<std::size_t> slot = changed_slot(current, written);
        if (!slot)
        {
            log(Level::Warn, "refused a %zu-byte write to OKAMI that isn't one changed slot holding a loadable record", written.size());
            return false;
        }
        log(Level::Info, "the game saved into slot %zu", *slot);
        std::copy_n(written.begin() + *slot * kRecordSize, kRecordSize, current.begin());
        g_has_record = true;
    }
    else
    {
        if (written.size() != current.size())
        {
            log(Level::Warn, "refused a %zu-byte write to %s", written.size(), kServed[file].name);
            return false;
        }
        std::ranges::copy(written, current.begin());
        // A zip needs a record, so with none yet the first record's commit carries this file.
        if (!g_has_record)
            return true;
    }
    commit();
    return true;
}

// The served entries. Each takes the interface object as this, and passes every call that isn't for a served file to the original entry.

std::uint64_t file_write_async(void *self, const char *name, const void *data, std::uint32_t size)
{
    const int file = served(name);
    if (file < 0)
        return original(&file_write_async, kFileWriteAsync)(self, name, data, size);
    std::lock_guard lock(g_mutex);
    g_pending = {true, false, file, 0, 0, capture(file, {static_cast<const std::uint8_t *>(data), size}) ? kOk : kFail};
    return kServedCall;
}

std::uint64_t file_read_async(void *self, const char *name, std::uint32_t offset, std::uint32_t size)
{
    const int file = served(name);
    if (file < 0)
        return original(&file_read_async, kFileReadAsync)(self, name, offset, size);
    const auto file_size = static_cast<std::uint32_t>(kServed[file].size);
    offset = std::min(offset, file_size);
    g_pending = {true, true, file, offset, std::min(size, file_size - offset), kOk};
    return kServedCall;
}

bool file_read_async_complete(void *self, std::uint64_t call, void *buffer, std::uint32_t size)
{
    if (call != kServedCall || !g_pending.active)
        return original(&file_read_async_complete, kFileReadAsyncComplete)(self, call, buffer, size);
    std::lock_guard lock(g_mutex);
    std::memcpy(buffer, g_files[g_pending.file].data() + g_pending.offset, std::min(size, g_pending.size));
    return true;
}

bool file_delete(void *self, const char *name)
{
    if (served(name) < 0)
        return original(&file_delete, kFileDelete)(self, name);
    log(Level::Warn, "kept %s, which the game asked Steam to delete", name);
    return true;
}

bool file_exists(void *self, const char *name)
{
    return served(name) >= 0 || original(&file_exists, kFileExists)(self, name);
}

std::int32_t get_file_size(void *self, const char *name)
{
    const int file = served(name);
    return file >= 0 ? static_cast<std::int32_t>(kServed[file].size) : original(&get_file_size, kGetFileSize)(self, name);
}

std::int32_t get_file_count(void *self)
{
    return ap::seed() ? 3 : original(&get_file_count, kGetFileCount)(self);
}

// The game strips "./" from "./Steam" and matches the listed names against "Steam/<name>" (inferred).
const char *get_file_name_and_size(void *self, int index, std::int32_t *size)
{
    if (!ap::seed())
        return original(&get_file_name_and_size, kGetFileNameAndSize)(self, index, size);
    const bool listed = index >= 0 && index < 3;
    *size = listed ? static_cast<std::int32_t>(kServed[index].size) : 0;
    return listed ? kServed[index].name : "";
}

} // namespace

void saves_start(const Addresses &game, std::filesystem::path dir)
{
    g_game = game;
    g_dir = std::move(dir);
}

std::string saves_bind(const ap::Seed &seed)
{
    OpenedSave opened = open_save(g_dir, seed.name, seed.slot);
    if (!opened.refusal.empty())
        return opened.refusal;

    if (!g_original)
    {
        // The copy goes in with one aligned pointer store, so a call on the worker runs through either the original vtable or the copy, and it stays for
        // the process. While no seed is bound, every served entry passes through.
        void *const context = g_game.steam_context_init ? g_game.steam_context_init(g_game.steam_context_init_data) : nullptr;
        void *const storage = context ? *reinterpret_cast<void **>(static_cast<std::uint8_t *>(context) + 0x48) : nullptr;
        if (!storage)
            return "can't reach Steam's remote storage to serve the AP save";
        void **&vtable = *static_cast<void ***>(storage);
        g_original = vtable;
        std::copy_n(vtable, kEntries, g_vtable.begin());
        g_vtable[kFileWriteAsync / 8] = reinterpret_cast<void *>(&file_write_async);
        g_vtable[kFileReadAsync / 8] = reinterpret_cast<void *>(&file_read_async);
        g_vtable[kFileReadAsyncComplete / 8] = reinterpret_cast<void *>(&file_read_async_complete);
        g_vtable[kFileDelete / 8] = reinterpret_cast<void *>(&file_delete);
        g_vtable[kFileExists / 8] = reinterpret_cast<void *>(&file_exists);
        g_vtable[kGetFileSize / 8] = reinterpret_cast<void *>(&get_file_size);
        g_vtable[kGetFileCount / 8] = reinterpret_cast<void *>(&get_file_count);
        g_vtable[kGetFileNameAndSize / 8] = reinterpret_cast<void *>(&get_file_name_and_size);
        std::atomic_ref(vtable).store(g_vtable.data(), std::memory_order_release);
    }

    std::lock_guard lock(g_mutex);
    // OKAMI holds the record in slot 0, and every other slot is empty: zeros with 0xff at +0x04 to +0x07 (confirmed).
    std::vector<std::uint8_t> &okami = g_files[kOkami];
    okami.assign(kSlotCount * kRecordSize, 0);
    for (std::size_t slot = 0; slot < kSlotCount; ++slot)
        std::fill_n(okami.begin() + slot * kRecordSize + 4, 4, std::uint8_t{0xff});
    g_has_record = opened.data.has_value();
    if (opened.data)
    {
        std::ranges::copy(opened.data->record, okami.begin());
        g_files[kSystem] = std::move(opened.data->system);
        g_files[kParamSfo] = std::move(opened.data->param_sfo);
    }
    else
    {
        // SYSTEM and PARAM.SFO as the game creates them for a new save (confirmed). The game's PARAM.SFO reader checks only the size.
        g_files[kSystem] = {0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0};
        g_files[kParamSfo].assign(kParamSfoSize, 0);
        std::ranges::copy(std::string_view("\xd3KAMI HD"), g_files[kParamSfo].begin());
        std::ranges::copy(std::string_view("Saved Data"), g_files[kParamSfo].begin() + 0x80);
    }
    g_file = std::move(opened.file);
    g_problem.clear();
    log(Level::Info, "serving the saves of %s slot %d from %s", seed.name.c_str(), seed.slot,
        g_has_record ? (g_file.backed_up ? "its .bak" : "its .zip") : "no AP save yet");
    return {};
}

TitleBlock title_block()
{
    if (!ap::seed())
        return TitleBlock::NoSeed;
    std::lock_guard lock(g_mutex);
    return g_has_record ? TitleBlock::HasSave : TitleBlock::NoSave;
}

void saves_frame()
{
    // Arriving at the title disarms. Leaving it for anything but Presents while a seed is bound arms with nothing due, after New Game as after Continue.
    // Any other arrival while armed makes one save due.
    const std::uint16_t room = *g_game.current_room;
    if (room != g_room)
    {
        if (room == kTitleRoom)
            g_armed = g_due = false;
        else if (g_room == kTitleRoom)
            g_armed = room != kPresentsRoom && ap::seed();
        else
            g_due = g_due || g_armed;
        g_room = room;
    }
    // At the title menu (major state 5) every entry's action gets bit 8 set when its row is blocked, which keeps the confirm dispatch at 0x180600433 from
    // matching it: that dispatch reads the cursor's entry as a 32-bit action and acts only on 0 to 4 (inferred), so a blocked row plays the confirm cue and
    // does nothing. The labels come from the row widgets, so they don't change. TitleMenu_BuildEntries rebuilds the vector each time the menu opens, so
    // this runs every frame.
    if (room == kTitleRoom && (*g_game.title_state & 0xFF) == kTitleMenuState)
    {
        const TitleBlock block = title_block();
        for (TitleMenuEntry *entry = g_game.title_menu[0]; entry != g_game.title_menu[1]; ++entry)
        {
            const std::int32_t action = entry->action & 0xff;
            const bool blocked = block == TitleBlock::NoSeed ? action <= 1 : action == (block == TitleBlock::HasSave ? 0 : 1);
            entry->action = blocked ? action | 0x100 : action;
        }
    }
    if (!g_armed || !g_due || !ap::seed() || !save_safe(g_game))
        return;
    g_due = false;

    // SaveRecord_Serialize writes the live game into g_cachedSaveRecord, which also keeps the options write-back from reverting the autosave, since that
    // write-back flushes this cache. It stamps g_saveAreaCode into the record: -1 would make the slot read as empty and the AreaReentryTable codes would
    // move the save to a fixed room, while 0x0f keeps the saved room and position. usePos 0 saves the player's own position.
    const std::int32_t area = *g_game.save_area_code;
    *g_game.save_area_code = 0x0f;
    *g_game.save_use_pos = 0;
    struct
    {
        std::uint8_t unused[0x220];
        std::uint8_t *record;
    } scratch{{}, g_game.cached_save_record};
    g_game.serialize_save_record(&scratch);
    *g_game.save_area_code = area;

    std::lock_guard lock(g_mutex);
    std::copy_n(g_game.cached_save_record, kRecordSize, g_files[kOkami].begin());
    g_has_record = true;
    log(Level::Info, "autosave in room 0x%04x", room);
    commit();
}

std::string save_problem()
{
    std::lock_guard lock(g_mutex);
    return g_problem;
}

void register_call_result_detour(void *callback, std::uint64_t call)
{
    if (call != kServedCall || !g_pending.active)
    {
        register_call_result_next(callback, call);
        return;
    }
    // The game spins on OSYieldThread until the call result's completion clears a busy flag it set before the call, so a served call completes here, on
    // the calling thread. Run, slot 0 of the call result's vtable, takes the result struct.
    const auto run = reinterpret_cast<void (*)(void *, void *, bool, std::uint64_t)>((*static_cast<void ***>(callback))[0]);
    if (g_pending.read)
    {
        ReadComplete done{call, g_pending.result, g_pending.offset, g_pending.size};
        run(callback, &done, false, call);
    }
    else
    {
        WriteComplete done{g_pending.result};
        run(callback, &done, false, call);
    }
    g_pending = {};
}

} // namespace game
