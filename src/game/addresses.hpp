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
};

// The addresses for main.dll mapped at main_base and flower_kernel.dll mapped at kernel_base.
Addresses resolve(uintptr_t main_base, uintptr_t kernel_base);

} // namespace game
