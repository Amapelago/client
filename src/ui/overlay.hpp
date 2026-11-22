// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "game/addresses.hpp"

struct tagRECT;

// Threading: SwapChain::swap runs inside flower_tick, cPad::Update once per frame and the game window's procedure in the game's message loop, all on the
// main thread. Everything in ui/ is main-thread state and takes no lock.
namespace ui
{

// Main thread, at the cold start, before the hook batch is applied. Keeps a copy of game.
void start(const game::Addresses &game);

// Detours SwapChain::swap: draws the overlay into the finished frame, then presents it through swap_next.
void swap_detour(void *swapchain, unsigned sync);
inline void (*swap_next)(void *swapchain, unsigned sync) = nullptr;

// Detours cPad::Update: lets the game build this frame's g_keyWork and g_joyWork, then releases every key in them while the overlay has focus.
void pad_detour(void *pad);
inline void (*pad_next)(void *pad) = nullptr;

// Detours user32's ClipCursor and ShowCursor. They run on any thread. Only okami.exe's calls made on the main thread while the overlay has focus are
// recorded instead of applied, and ending focus applies what was recorded. Every other call, and the overlay's own through the originals, passes
// straight through.
int clip_cursor_detour(const tagRECT *rect);
inline int (*clip_cursor_next)(const tagRECT *rect) = nullptr;
int show_cursor_detour(int show);
inline int (*show_cursor_next)(int show) = nullptr;

// Detours IDirectInputDevice8W::GetDeviceState. It runs on any thread. The game polls its keyboard and mouse through it once a frame on the main thread.
// While the overlay has focus, a main-thread call returns the original's result with the caller's buffer zeroed, the idle state of a keyboard array and
// of a mouse state. Every other call passes straight through.
long get_device_state_detour(void *device, unsigned long size, void *data);
inline long (*get_device_state_next)(void *device, unsigned long size, void *data) = nullptr;

// Inside an overlay frame: writes the client's status top-left, on the foreground draw list, so it never takes input.
void draw_status();

// Inside an overlay frame, at the title with a session: the window that connects to a server and disconnects from it. Without focus it also tells the
// player which key gives it focus.
void draw_login(bool focused);

// Inside an overlay frame with a session: takes the session's print messages and stacks the recent ones top-right, on the foreground draw list.
void draw_toasts();

} // namespace ui
