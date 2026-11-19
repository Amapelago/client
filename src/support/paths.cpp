#include "support/paths.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace support
{

// The environment variable is read with kernel32 only: this runs under the loader lock, where loading shell32 for the known-folder API is unsafe.
std::filesystem::path data_dir()
{
    wchar_t base[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return std::filesystem::path(base) / L"okami-apclient";
}

} // namespace support
