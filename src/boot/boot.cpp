#include "boot/boot.hpp"

#include <atomic>
#include <cstdio>

#include <winternl.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include "boot/fingerprint.hpp"
#include "client.hpp"
#include "game/addresses.hpp"
#include "support/crash.hpp"
#include "support/log.hpp"
#include "support/paths.hpp"
#include "ui/overlay.hpp"
#include "version.hpp"

#include <MinHook.h>

namespace boot
{
namespace
{

using support::Level;
using support::log;

// Threading: g_main_base and g_startup_next are written under the loader lock, on whichever thread maps main.dll. g_tick_next is written by startup_detour at
// the cold start. The detours and everything they read run only on the game's main thread. The game's address table is resolved at the cold start, and
// client and ui each keep a copy; boot keeps the main.dll base to resolve it and the trampolines of its own two detours.
uintptr_t g_main_base = 0;
bool (*g_startup_next)(bool warm) = nullptr;
bool (*g_tick_next)() = nullptr;

// The data of ntdll's DLL-loaded notification (LDR_DLL_LOADED_NOTIFICATION_DATA), which the SDK headers do not declare.
struct LoadedDll
{
    ULONG flags = 0;
    const UNICODE_STRING *full_name = nullptr;
    const UNICODE_STRING *base_name = nullptr;
    void *base = nullptr;
    ULONG size_of_image = 0;
};

constexpr ULONG kDllLoaded = 1;
using DllNotification = VOID(CALLBACK *)(ULONG reason, const LoadedDll *dll, void *context);
using RegisterDllNotification = LONG(NTAPI *)(ULONG flags, DllNotification fn, void *context, const void **cookie);

// Set by whichever of attach and the loader notification sees main.dll first; the other then does nothing.
std::atomic<bool> g_main_seen{false};

char g_mismatch[256] = {};

// Shown from a thread of its own, so DllMain never waits on a window.
DWORD WINAPI show_mismatch(void *)
{
    MessageBoxA(nullptr, g_mismatch, "okami-apclient", MB_OK | MB_ICONWARNING);
    return 0;
}

// Zero until the loader maps flower_kernel.dll. main.dll imports it, so it is mapped by the cold start.
uintptr_t kernel_base()
{
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(L"flower_kernel.dll"));
}

bool is_main_dll(const UNICODE_STRING *name)
{
    return name != nullptr && name->Buffer != nullptr && CompareStringOrdinal(name->Buffer, name->Length / 2, L"main.dll", -1, TRUE) == CSTR_EQUAL;
}

// Creates a hook on target and queues it for the next apply_hooks, logging a failure by name.
bool queue_hook(const char *name, void *target, void *detour, void **original)
{
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK)
    {
        status = MH_QueueEnableHook(target);
    }
    if (status != MH_OK)
    {
        log(Level::Error, "hooking %s failed: %s", name, MH_StatusToString(status));
    }
    return status == MH_OK;
}

// Enables every queued hook in one batch, which suspends the process's other threads once.
bool apply_hooks()
{
    const MH_STATUS status = MH_ApplyQueued();
    if (status != MH_OK)
    {
        log(Level::Error, "applying hooks failed: %s", MH_StatusToString(status));
    }
    return status == MH_OK;
}

// The game's DirectInput keyboard and mouse share one IDirectInputDevice8W vtable, whose slot 9 is GetDeviceState. A keyboard device made and released
// here gives its address, or null when DirectInput can't make one.
void *device_state_function()
{
    IDirectInput8W *input = nullptr;
    if (FAILED(proxy_DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&input), nullptr)))
    {
        return nullptr;
    }
    void *function = nullptr;
    IDirectInputDevice8W *keyboard = nullptr;
    if (SUCCEEDED(input->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr)))
    {
        function = (*reinterpret_cast<void ***>(keyboard))[9];
        keyboard->Release();
    }
    input->Release();
    return function;
}

bool tick_detour()
{
    client::frame();
    return g_tick_next();
}

bool startup_detour(bool warm)
{
    // The first call is the cold one, whatever warm says. Later calls are soft resets, which leave the hooks in place.
    static bool booted = false;
    if (!booted)
    {
        booted = true;
        if (const auto dir = support::data_dir(); !dir.empty())
        {
            support::crash_start(dir / "crashes");
        }
        log(Level::Info, "cold start on thread %lu", GetCurrentThreadId());
        const game::Addresses game = game::resolve(g_main_base, kernel_base());
        ui::start(game);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll"); // the client imports it, so it is mapped
        if (queue_hook("flower_tick", reinterpret_cast<void *>(game.flower_tick), reinterpret_cast<void *>(&tick_detour),
                       reinterpret_cast<void **>(&g_tick_next)) &&
            queue_hook("SwapChain::swap", reinterpret_cast<void *>(game.swapchain_swap), reinterpret_cast<void *>(&ui::swap_detour),
                       reinterpret_cast<void **>(&ui::swap_next)) &&
            queue_hook("cPad::Update", reinterpret_cast<void *>(game.pad_update), reinterpret_cast<void *>(&ui::pad_detour),
                       reinterpret_cast<void **>(&ui::pad_next)) &&
            queue_hook("ClipCursor", reinterpret_cast<void *>(GetProcAddress(user32, "ClipCursor")), reinterpret_cast<void *>(&ui::clip_cursor_detour),
                       reinterpret_cast<void **>(&ui::clip_cursor_next)) &&
            queue_hook("ShowCursor", reinterpret_cast<void *>(GetProcAddress(user32, "ShowCursor")), reinterpret_cast<void *>(&ui::show_cursor_detour),
                       reinterpret_cast<void **>(&ui::show_cursor_next)) &&
            queue_hook("GetDeviceState", device_state_function(), reinterpret_cast<void *>(&ui::get_device_state_detour),
                       reinterpret_cast<void **>(&ui::get_device_state_next)) &&
            apply_hooks())
        {
            log(Level::Info, "flower_tick, SwapChain::swap, cPad::Update, ClipCursor, ShowCursor and GetDeviceState hooked");
        }
        client::cold_start(game);
        // Anything flower_startup sets as the unhandled-exception filter displaces the client's, so the client's goes back afterwards.
        const bool result = g_startup_next(warm);
        support::crash_reassert();
        return result;
    }
    return g_startup_next(warm);
}

// The rest of attach's work, on main.dll mapped at main, with the loader lock held.
void on_main_dll(HMODULE main)
{
    const auto found = read_fingerprint(main);
    if (found != kOkamiMain)
    {
        const Fingerprint seen = found.value_or(Fingerprint{});
        std::snprintf(g_mismatch, sizeof(g_mismatch),
                      "okami-apclient supports only the Steam release of Okami HD (main.dll 0x%08X, 0x%X). This main.dll is 0x%08X, 0x%X, so the game runs "
                      "without okami-apclient.",
                      kOkamiMain.time_date_stamp, kOkamiMain.size_of_image, seen.time_date_stamp, seen.size_of_image);
        log(Level::Error, "%s", g_mismatch);
        if (const HANDLE thread = CreateThread(nullptr, 0, &show_mismatch, nullptr, 0, nullptr); thread != nullptr)
        {
            CloseHandle(thread);
        }
        return;
    }
    log(Level::Info, "main.dll fingerprint matches");

    g_main_base = reinterpret_cast<uintptr_t>(main);
    if (const MH_STATUS status = MH_Initialize(); status != MH_OK)
    {
        log(Level::Error, "initialising MinHook failed: %s", MH_StatusToString(status));
        return;
    }
    if (queue_hook("flower_startup", reinterpret_cast<void *>(game::resolve(g_main_base, kernel_base()).flower_startup),
                   reinterpret_cast<void *>(&startup_detour), reinterpret_cast<void **>(&g_startup_next)) &&
        apply_hooks())
    {
        log(Level::Info, "flower_startup hooked");
    }
}

// Runs under the loader lock once main.dll is mapped and before any of its code runs, as DllMain does.
VOID CALLBACK on_dll_loaded(ULONG reason, const LoadedDll *dll, void *)
{
    if (reason != kDllLoaded || !is_main_dll(dll->base_name) || g_main_seen.exchange(true))
    {
        return;
    }
    log(Level::Info, "main.dll loaded");
    on_main_dll(static_cast<HMODULE>(dll->base));
}

} // namespace

void attach(HMODULE self)
{
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(self), &pinned);

    if (const auto dir = support::data_dir(); !dir.empty())
    {
        support::log_start(dir / "logs");
    }
    log(Level::Info, "okami-apclient %s (%s) attached on thread %lu", version::string.data(), version::hash.data(), GetCurrentThreadId());

    // The Steam overlay and other injectors can load dinput8.dll before the game loads main.dll, so the client also asks the loader to report main.dll.
    // The notification is registered before the check, so a main.dll that loads in between is not missed.
    const auto register_notification = reinterpret_cast<RegisterDllNotification>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    const void *cookie = nullptr; // never unregistered: the notification stays for the process
    const bool notified = register_notification != nullptr && register_notification(0, &on_dll_loaded, nullptr, &cookie) == 0;
    if (const HMODULE main = GetModuleHandleW(L"main.dll"); main != nullptr)
    {
        if (!g_main_seen.exchange(true))
        {
            log(Level::Info, "main.dll was already loaded");
            on_main_dll(main);
        }
        return;
    }
    if (notified)
    {
        log(Level::Info, "waiting for main.dll");
    }
    else
    {
        log(Level::Error, "cannot watch for main.dll; the client stays off");
    }
}

} // namespace boot
