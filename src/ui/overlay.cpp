#include "ui/overlay.hpp"

#include <algorithm>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

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

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (const LRESULT handled = ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam); handled != 0)
        return handled;
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
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // the cursor stays the game's
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

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame(); // the display size is the window's client area, read fresh each frame
    ImGuiIO &io = ImGui::GetIO();
    // DXGI stretches the surface over the client area when their sizes differ, so input stays in client coordinates while drawing covers the surface.
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
        io.DisplayFramebufferScale = ImVec2(desc.Width / io.DisplaySize.x, desc.Height / io.DisplaySize.y);
    ImGui::GetStyle().FontScaleMain = std::max(1.0f, io.DisplaySize.y / 720.0f);
    ImGui::NewFrame();
    draw_status();
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

} // namespace ui
