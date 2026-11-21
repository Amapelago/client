#pragma once

#include <cstdint>

struct ID3D11RenderTargetView;
struct HWND__;

namespace game
{

// Where the game's functions and globals sit in this process. All of them come from the one supported build of main.dll and flower_kernel.dll.
struct Addresses
{
    bool (*flower_startup)(bool warm) = nullptr;
    bool (*flower_tick)() = nullptr;
    const volatile uint16_t *current_room = nullptr; // stage << 8 | room
    const volatile uint32_t *title_state = nullptr;  // the low byte is the title's major state
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
    void *(*steam_context_init)(void *init_data) = nullptr; // steam_api64's SteamInternal_ContextInit; boot fills it, since resolve sees only main.dll
};

// The addresses for main.dll mapped at main_base and flower_kernel.dll mapped at kernel_base.
Addresses resolve(uintptr_t main_base, uintptr_t kernel_base);

} // namespace game
