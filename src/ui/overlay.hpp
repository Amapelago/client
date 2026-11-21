#pragma once

#include "game/addresses.hpp"

// Threading: SwapChain::swap runs inside flower_tick and the game window's procedure runs in the game's message loop, both on the main thread. Everything in
// ui/ is main-thread state and takes no lock.
namespace ui
{

// Main thread, at the cold start, before the hook batch is applied. Keeps a copy of game.
void start(const game::Addresses &game);

// Detours SwapChain::swap: draws the overlay into the finished frame, then presents it through swap_next.
void swap_detour(void *swapchain, unsigned sync);
inline void (*swap_next)(void *swapchain, unsigned sync) = nullptr;

// Inside an overlay frame: writes the client's status top-left, on the foreground draw list, so it never takes input.
void draw_status();

} // namespace ui
