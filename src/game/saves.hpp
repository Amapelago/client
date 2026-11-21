// The AP save in memory: the game's save files served from it while a seed is bound, the autosave that writes the live game into it, and the title menu
// rows it blocks.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "ap/seed.hpp"
#include "game/addresses.hpp"

// Threading: one mutex guards the AP save in memory. The served Steam entries run on the hx::SaveDataManager worker and hold it to serve and capture; main
// holds it to bind and autosave, and in title_block and save_problem. Whoever changes the record commits while holding it. No game function is called while
// holding it, and a served completion takes it inside the game's own Steam lock, never the reverse.
namespace game
{

// Main thread, at the cold start. Keeps game and the folder the AP saves live in.
void saves_start(const Addresses &game, std::filesystem::path dir);

// Main thread, before seed is published. Reads the seed's AP save into memory and, at the first bind, points Steam's remote storage at the copy of its
// vtable that serves the game's files. Returns why the seed can't be bound, or an empty string.
std::string saves_bind(const ap::Seed &seed);

// Main thread, every frame after the session's tick. At the title menu it blocks the rows title_block names, and it autosaves at a save-safe moment once a
// room arrival has made a save due.
void saves_frame();

// What the title does with New Game and Continue. NoSeed blocks both, HasSave blocks New Game and NoSave blocks Continue.
enum class TitleBlock
{
    NoSeed,
    HasSave,
    NoSave
};

// Main thread. Whether a seed is bound, and if so whether its AP save in memory holds a record.
TitleBlock title_block();

// Main thread. Why the last commit failed, or an empty string.
std::string save_problem();

// Detours steam_api64's SteamAPI_RegisterCallResult. Completes the calling thread's served async call before it returns, and passes every other call to
// Steam.
void register_call_result_detour(void *callback, std::uint64_t call);
inline void (*register_call_result_next)(void *callback, std::uint64_t call) = nullptr;

} // namespace game
