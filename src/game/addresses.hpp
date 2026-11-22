// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

struct ID3D11RenderTargetView;
struct HWND__;

namespace game
{

// One row of the title menu's entry vector.
struct TitleMenuEntry
{
    int32_t action; // 0 New Game, 1 Continue, 2 to 4 the other rows; the confirm dispatch acts only on 0 to 4
    int32_t cursor;
    int32_t row;
};

// Where the game's functions and globals sit in this process. All of them come from the one supported build of main.dll and flower_kernel.dll.
struct Addresses
{
    bool (*flower_startup)(bool warm) = nullptr;
    bool (*flower_tick)() = nullptr;
    const volatile uint16_t *current_room = nullptr;      // stage << 8 | room
    const volatile uint32_t *title_state = nullptr;       // the low byte is the title's major state
    TitleMenuEntry *const volatile *title_menu = nullptr; // cTitle + 0x68: [0] the first entry, [1] one past the last
    void (*swapchain_swap)(void *swapchain, unsigned sync) = nullptr;
    void *const *display_buffer = nullptr; // the game's slot for the Texture2D that wraps the swap chain's back buffer; read it every frame
    ID3D11RenderTargetView *(*texture_rtv)(const void *texture) = nullptr;
    HWND__ *(*get_hwnd)() = nullptr;
    void (*pad_update)(void *pad) = nullptr;
    volatile uint8_t *key_work = nullptr; // KEY_WORK, 0xc8 bytes
    volatile uint8_t *joy_work = nullptr; // sJoyWork, 0x1d0 bytes
    bool (*save_prompt_can_open)() = nullptr;
    const volatile uint32_t *ui_mode_flags = nullptr;  // u32 words; +0x04, +0x0c, +0x10 and +0x18 are the ones play_state reads
    const volatile uint32_t *map_task_state = nullptr; // the low byte is the state
    const volatile uint32_t *engine_flags = nullptr;
    void *const volatile *player = nullptr; // holds the player object's address, or null
    volatile int32_t *save_area_code = nullptr;
    volatile uint8_t *save_use_pos = nullptr;
    uint8_t *cached_save_record = nullptr; // one OKAMI slot record
    void (*serialize_save_record)(void *ctx) = nullptr;
    void *steam_context_init_data = nullptr;
    void *const volatile *item_mgr = nullptr;                 // holds the item manager's address, which Item_Add takes and never dereferences
    void (*item_add)(void *mgr, int id, int delta) = nullptr; // clamps the count to the item's maxCount
    volatile uint16_t *item_counts = nullptr;                 // count[256], by item id
    void (*set_astral_pouch_max)(uint16_t capacity) = nullptr;
    int16_t (*astral_pouch_max)() = nullptr;
    uint8_t *fude = nullptr; // FudeSys: enabled u64 +0x70, acquired u64 +0x78, level u8[64] +0x80, saved enabled u64 +0xc0, saved level u8[64] +0xc8
    // op 0 sets enabled and acquired, 1 clears enabled, 2 clears both, 3 clears acquired
    void (*update_technique_bit)(void *fude, uint32_t bit, int op) = nullptr;
    void (*set_technique_level)(void *fude, int bit, uint8_t level) = nullptr; // writes only the level byte
    void (*refresh_cherry_bomb_count)(void *fude) = nullptr;                   // derives the bomb count from the Cherry Bomb level
    void *(*steam_context_init)(void *init_data) = nullptr; // steam_api64's SteamInternal_ContextInit; boot fills it, since resolve sees only main.dll
};

// The addresses for main.dll mapped at main_base and flower_kernel.dll mapped at kernel_base.
Addresses resolve(uintptr_t main_base, uintptr_t kernel_base);

} // namespace game
