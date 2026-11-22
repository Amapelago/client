// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
    // cTitle + 0x68 holds the begin and end pointers of the menu's entry vector (inferred).
    a.title_menu = at<TitleMenuEntry *const volatile *>(main_base, 0x180b74468);
    // SwapChain::swap presents the finished frame through IDXGISwapChain::Present, on the main thread inside flower_tick.
    a.swapchain_swap = at<void (*)(void *, unsigned)>(kernel_base, 0x18003e6a0);
    // SwapChain's methods work on a flower_kernel global. At +0x18 it points to the Texture2D that wraps the IDXGISwapChain's back buffer, the surface
    // Present shows. The game draws the scene into separate render targets and copies the finished frame into this one before Present.
    a.display_buffer = at<void *const *>(kernel_base, 0x18011c298);
    // Texture2D::getD3DRenderTargetView returns the texture's view without adding a reference.
    a.texture_rtv = at<ID3D11RenderTargetView *(*)(const void *)>(kernel_base, 0x18001dfb0);
    // m2::render::GetHwnd returns the game window, or null before okami.exe creates it.
    a.get_hwnd = at<HWND__ *(*)()>(kernel_base, 0x18003b460);
    // cPad::Update builds g_keyWork from every input device, once per frame on the main thread.
    a.pad_update = at<void (*)(void *)>(main_base, 0x180186650);
    // g_keyWork is the KEY_WORK that gameplay reads.
    a.key_work = at<volatile uint8_t *>(main_base, 0x180b6b0a0);
    // g_joyWork is the sJoyWork that cPad::Update fills from every device before it builds g_keyWork. Menus read its press edges directly.
    a.joy_work = at<volatile uint8_t *>(main_base, 0x180b6ad00);
    // SavePrompt_CanOpen is the game's own test for whether a save prompt may open (inferred). It dereferences g_Player.
    a.save_prompt_can_open = at<bool (*)()>(main_base, 0x180438b20);
    // g_uiModeFlags is a run of u32 words whose bits say which menus, scripts and player modes are active (inferred). The words at +0x04, +0x0c, +0x10 and
    // +0x18 are 0x180b6b2a4, 0x180b6b2ac, 0x180b6b2b0 and 0x180b6b2b8.
    a.ui_mode_flags = at<const volatile uint32_t *>(main_base, 0x180b6b2a0);
    // g_mapTaskState's low byte is the map task's state, 4 once a room has loaded (confirmed). NOW LOADING can outlast it.
    a.map_task_state = at<const volatile uint32_t *>(main_base, 0x180b661d0);
    // Bit 15 of g_engineFlagBits is set while a soft reset or Game Over is armed (confirmed).
    a.engine_flags = at<const volatile uint32_t *>(main_base, 0x180b6acc4);
    // g_Player points to the player object, and is null while Room_Change replaces the room (inferred).
    a.player = at<void *const volatile *>(main_base, 0x180b6b2d0);
    // g_saveAreaCode is the area code SaveRecord_Serialize stamps into record +0x04. It is -1 outside a save prompt (confirmed).
    a.save_area_code = at<volatile int32_t *>(main_base, 0x18079beb4);
    // The save context's usePos byte is 1 when a save prompt supplies a position to save in place of the player's (confirmed).
    a.save_use_pos = at<volatile uint8_t *>(main_base, 0x18079bed4);
    // g_cachedSaveRecord holds the record the game last loaded or saved (confirmed).
    a.cached_save_record = at<uint8_t *>(main_base, 0x1809a9f10);
    // SaveRecord_Serialize writes the live game into the record that ctx +0x220 points to, and reads nothing else from ctx (confirmed).
    a.serialize_save_record = at<void (*)(void *)>(main_base, 0x1801c3870);
    // main.dll's Steam context data. SteamInternal_ContextInit on it returns the context, which holds the ISteamRemoteStorage pointer at +0x48 (confirmed).
    a.steam_context_init_data = at<void *>(main_base, 0x180792cb0);
    return a;
}

} // namespace game
