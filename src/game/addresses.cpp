#include "game/addresses.hpp"

namespace game
{
namespace
{

// The preferred image base of both main.dll and flower_kernel.dll, which the virtual addresses below are written against.
constexpr uintptr_t kImageBase = 0x180000000;

template <typename T> T at(uintptr_t base, uintptr_t va)
{
    return reinterpret_cast<T>(base + (va - kImageBase));
}

template <typename T> T at_rva(uintptr_t main_base, uintptr_t rva)
{
    return reinterpret_cast<T>(main_base + rva);
}

} // namespace

Addresses resolve(uintptr_t main_base, uintptr_t kernel_base)
{
    Addresses a;
    // flower_startup runs once per boot (cold) and again after a soft reset (warm).
    a.flower_startup = at<bool (*)(bool)>(main_base, 0x1804b6240);
    // flower_tick runs once per frame on the main thread.
    a.flower_tick = at<bool (*)()>(main_base, 0x1804b63b0);
    // g_currentRoomId holds the stage in the high byte and the room in the low byte.
    a.current_room = at<const volatile uint16_t *>(main_base, 0x180b6b240);
    // cTitle + 0x14 holds the title's state; the low byte is the major state.
    a.title_state = at<const volatile uint32_t *>(main_base, 0x180b74414);
    // SwapChain::swap presents the finished frame through IDXGISwapChain::Present, on the main thread inside flower_tick.
    a.swapchain_swap = at<void (*)(void *, unsigned)>(kernel_base, 0x18003e6a0);
    // SwapChain's methods work on a flower_kernel global. At +0x18 it points to the Texture2D that wraps the IDXGISwapChain's back buffer, the surface
    // Present shows. The game draws the scene into separate render targets and copies the finished frame into this one before Present.
    a.display_buffer = at<void *const *>(kernel_base, 0x18011c298);
    // Texture2D::getD3DRenderTargetView returns the texture's view without adding a reference.
    a.texture_rtv = at<ID3D11RenderTargetView *(*)(const void *)>(kernel_base, 0x18001dfb0);
    // m2::render::GetHwnd returns the game window, or null before okami.exe creates it.
    a.get_hwnd = at<HWND__ *(*)()>(kernel_base, 0x18003b460);
    return a;
}

} // namespace game
