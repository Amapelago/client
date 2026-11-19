#pragma once

// NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from defining min, max and the rarely used APIs.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace boot
{

// DllMain, DLL_PROCESS_ATTACH, with the loader lock held. Opens the log and, as soon as main.dll is mapped (now, or when the loader reports it, since an
// overlay can load dinput8.dll first), checks that it is the supported build and hooks flower_startup, whose cold call boots the client.
void attach(HMODULE self);

} // namespace boot
