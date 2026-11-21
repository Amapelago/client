#include "ui/overlay.hpp"

#include <algorithm>
#include <cstring>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <intrin.h>

#include "client.hpp"
#include "game/pad.hpp"
#include "game/play_state.hpp"
#include "support/log.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ui
{
namespace
{

using support::Level;
using support::log;

enum class State
{
    Starting,
    Ready,
    Off
};

game::Addresses g_game;
State g_state = State::Starting;
WNDPROC g_game_proc = nullptr;
// The game's immediate context. ImGui's DX11 backend holds a reference to it for the life of the process, so the overlay uses it without one.
ID3D11DeviceContext *g_context = nullptr;
// The game's main thread, set at the cold start. Only its calls to ClipCursor and ShowCursor are recorded, and only its GetDeviceState calls read as idle.
DWORD g_main_thread = 0;
// Whether the overlay has focus: the pointer is free and shows, ImGui gets the mouse and keyboard, the game's procedure gets no input messages, the
// game's DirectInput keyboard and mouse read as idle, and pad_detour releases the game's keys. Without it the overlay only draws and the pointer is the
// game's. Main thread only, since window_proc, draw, pad_detour, the game's pointer calls and its DirectInput polls all run there.
bool g_focused = false;
// The pointer state okami.exe asks for while the overlay has focus, applied when focus ends. okami.exe's window procedure hides the pointer and clips it
// to the pixel at the centre of the window's rect on activation, and frees and shows it on deactivation. It can do the same while the overlay has focus,
// from messages the overlay passes on.
int g_game_counter = 0;        // the display counter the game believes in
RECT g_game_clip{};            // the clip the game believes in, unless g_game_unclipped
bool g_game_unclipped = false; // the game's latest clip was ClipCursor(NULL)

// Records the clip and display counter in place as the game's, then frees and shows the pointer.
void take_focus()
{
    g_focused = true;
    GetClipCursor(&g_game_clip);
    g_game_unclipped = false;
    clip_cursor_next(nullptr);
    g_game_counter = show_cursor_next(TRUE) - 1;
    ImGui::GetIO().AddFocusEvent(true);
}

// Brings the display counter to the one the game believes in, and ImGui drops its held keys and buttons. The clip is the caller's.
void end_focus()
{
    g_focused = false;
    int counter = show_cursor_next(FALSE);
    while (counter > g_game_counter)
        counter = show_cursor_next(FALSE);
    while (counter < g_game_counter)
        counter = show_cursor_next(TRUE);
    ImGui::GetIO().AddFocusEvent(false);
}

// Whether a ClipCursor or ShowCursor call returning to caller is the game's, made while the overlay has focus.
bool recorded(const void *caller)
{
    if (GetCurrentThreadId() != g_main_thread || !g_focused)
        return false;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(caller), &module);
    return module == GetModuleHandleW(nullptr);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_DEADCHAR:
        // The key left of 1 is scan code 0x29, not extended. Its messages reach neither ImGui nor the game, and a press that isn't a repeat toggles focus.
        // Its WM_CHAR, which TranslateMessage makes from the key, would otherwise land in an active text field.
        if (((lparam >> 16) & 0xff) == 0x29 && (lparam & (1 << 24)) == 0)
        {
            if (message == WM_KEYDOWN && (lparam & (1 << 30)) == 0)
            {
                if (g_focused)
                {
                    // The window stays active, so the pointer goes back to the clip the game believes in.
                    end_focus();
                    clip_cursor_next(g_game_unclipped ? nullptr : &g_game_clip);
                }
                else
                {
                    take_focus();
                }
            }
            // On layouts where the key is a dead key, TranslateMessage leaves the accent pending in the thread's keyboard state, and the next key would
            // come out combined with it. Translating a space with flags 0 takes the pending accent out of that state.
            if (message == WM_DEADCHAR)
            {
                const BYTE keys[256] = {};
                WCHAR out[4];
                ToUnicode(VK_SPACE, 0x39, keys, out, 4, 0);
            }
            return 0;
        }
        break;
    case WM_ACTIVATE:
        // Focus ends before the game's handler runs, and the handler then frees and shows the pointer as on every deactivation, so the pointer stays
        // where the player moved it.
        if (LOWORD(wparam) == WA_INACTIVE && g_focused)
            end_focus();
        break;
    }
    if (g_focused)
    {
        if (const LRESULT handled = ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam); handled != 0)
            return handled;
        // Keyboard and mouse messages, the WM_SYS ones included, skip the game's procedure, so the game can't act on them. WM_SYSKEYDOWN goes to
        // DefWindowProcW, which turns Alt+F4 into SC_CLOSE. The rest return 0: DefWindowProcW would turn Alt or F10 alone into SC_KEYMENU and WM_SYSCHAR
        // into a menu lookup, and the window's menu loop is modal, so flower_tick would stop.
        if (message == WM_SYSKEYDOWN)
            return DefWindowProcW(hwnd, message, wparam, lparam);
        if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST))
            return 0;
    }
    // The previous procedure can be a winproc handle, which only CallWindowProcW may call. Wine crashes on a direct call to one.
    return CallWindowProcW(g_game_proc, hwnd, message, wparam, lparam);
}

void turn_off(const char *what)
{
    g_state = State::Off;
    log(Level::Error, "overlay off: %s", what);
}

// Brings ImGui up on the game's window and on the device that made view. Returns with the state still Starting while the window doesn't exist yet.
void try_start(ID3D11RenderTargetView *view)
{
    const HWND hwnd = g_game.get_hwnd();
    if (hwnd == nullptr)
        return;
    ID3D11Device *device = nullptr;
    view->GetDevice(&device);
    ID3D11DeviceContext *context = nullptr;
    device->GetImmediateContext(&context);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const bool win32 = ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context); // takes references of its own
    device->Release();
    context->Release();
    if (!win32)
        return turn_off("ImGui's Win32 backend did not start");
    // Created here so a failure turns the overlay off. Left to the first frame, a failure only asserts and the backend draws without them.
    if (!ImGui_ImplDX11_CreateDeviceObjects())
        return turn_off("ImGui could not create its D3D11 objects");

    g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_proc)));
    if (g_game_proc == nullptr)
        return turn_off("subclassing the game window failed");
    g_context = context;
    g_state = State::Ready;
    log(Level::Info, "overlay ready");
}

// Draws one overlay frame into view, the surface Present shows.
void draw(ID3D11RenderTargetView *view)
{
    ID3D11Resource *surface = nullptr;
    view->GetResource(&surface);
    D3D11_TEXTURE2D_DESC desc{};
    static_cast<ID3D11Texture2D *>(surface)->GetDesc(&desc);
    surface->Release();

    // Without focus ImGui ignores the mouse, since the Win32 backend still reads the pointer's position in its NewFrame and the game's pinned point would
    // hover a window, and it leaves the cursor's shape to the game.
    constexpr ImGuiConfigFlags no_mouse = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    ImGuiIO &io = ImGui::GetIO();
    if (g_focused)
        io.ConfigFlags &= ~no_mouse;
    else
        io.ConfigFlags |= no_mouse;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame(); // the display size is the window's client area, read fresh each frame
    // DXGI stretches the surface over the client area when their sizes differ, so input stays in client coordinates while drawing covers the surface.
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
        io.DisplayFramebufferScale = ImVec2(desc.Width / io.DisplaySize.x, desc.Height / io.DisplaySize.y);
    ImGui::GetStyle().FontScaleMain = std::max(1.0f, io.DisplaySize.y / 720.0f);
    ImGui::NewFrame();
    draw_status();
    if (client::session() != nullptr && game::title_shown(g_game))
        draw_login(g_focused);
    if (client::session() != nullptr)
        draw_toasts();
    ImGui::Render();

    ID3D11RenderTargetView *targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView *depth = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);
    g_context->OMSetRenderTargets(1, &view, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
    for (ID3D11RenderTargetView *target : targets)
    {
        if (target != nullptr)
            target->Release();
    }
    if (depth != nullptr)
        depth->Release();
}

} // namespace

void start(const game::Addresses &game)
{
    g_game = game;
    g_main_thread = GetCurrentThreadId();
}

void swap_detour(void *swapchain, unsigned sync)
{
    // The game's view of the surface Present shows, borrowed without a reference. It is looked up every frame, since a resolution or anti-aliasing change
    // replaces it, and a frame where the slot or the view is empty goes without the overlay.
    const void *texture = *g_game.display_buffer;
    ID3D11RenderTargetView *view = texture != nullptr ? g_game.texture_rtv(texture) : nullptr;
    if (view != nullptr && g_state == State::Starting)
        try_start(view);
    if (view != nullptr && g_state == State::Ready)
        draw(view);
    swap_next(swapchain, sync);
}

// A Steam Input controller doesn't go through DirectInput, so only this reset keeps it out of gameplay and menus. The title's attract movie and press
// start read the devices' own banks and still see it.
void pad_detour(void *pad)
{
    pad_next(pad);
    if (g_focused)
        game::release_keys(g_game);
}

BOOL clip_cursor_detour(const RECT *rect)
{
    if (!recorded(_ReturnAddress()))
        return clip_cursor_next(rect);
    g_game_unclipped = rect == nullptr;
    if (rect != nullptr)
        g_game_clip = *rect;
    return TRUE;
}

int show_cursor_detour(BOOL show)
{
    if (!recorded(_ReturnAddress()))
        return show_cursor_next(show);
    g_game_counter += show ? 1 : -1;
    return g_game_counter;
}

long get_device_state_detour(void *device, unsigned long size, void *data)
{
    const long result = get_device_state_next(device, size, data);
    if (GetCurrentThreadId() == g_main_thread && g_focused)
        std::memset(data, 0, size);
    return result;
}

} // namespace ui
