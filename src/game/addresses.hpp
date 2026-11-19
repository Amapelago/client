#pragma once

#include <cstdint>

namespace game
{

// Where the game's functions and globals sit in this process. All of them come from the one supported main.dll build.
struct Addresses
{
    bool (*flower_startup)(bool warm) = nullptr;
    bool (*flower_tick)() = nullptr;
    const volatile uint16_t *current_room = nullptr; // stage << 8 | room
    const volatile uint32_t *title_state = nullptr;  // the low byte is the title's major state
};

// The addresses for main.dll mapped at main_base.
Addresses resolve(uintptr_t main_base);

} // namespace game
