// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

// DIRECTINPUT_VERSION must precede dinput.h.
#define DIRECTINPUT_VERSION 0x0800
// NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from defining min, max and the rarely used APIs.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dinput.h>

namespace
{

std::filesystem::path exe_dir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

} // namespace

TEST_CASE("the proxy forwards DirectInput8Create to the system dinput8")
{
    const std::filesystem::path ours = exe_dir() / L"dinput8.dll";
    // Loading the proxy runs boot::attach, which opens the log under LOCALAPPDATA. With the variable unset it opens none.
    wchar_t saved[MAX_PATH] = {};
    const DWORD saved_length = GetEnvironmentVariableW(L"LOCALAPPDATA", saved, MAX_PATH);
    SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    HMODULE proxy = LoadLibraryW(ours.c_str());
    if (saved_length != 0 && saved_length < MAX_PATH)
    {
        SetEnvironmentVariableW(L"LOCALAPPDATA", saved);
    }
    REQUIRE(proxy != nullptr);

    wchar_t loaded[MAX_PATH] = {};
    GetModuleFileNameW(proxy, loaded, MAX_PATH);
    CHECK(_wcsicmp(loaded, ours.c_str()) == 0);

    using Create = HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);
    auto create = reinterpret_cast<Create>(GetProcAddress(proxy, "DirectInput8Create"));
    REQUIRE(create != nullptr);

    IDirectInput8W *input = nullptr;
    CHECK(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&input), nullptr) == DI_OK);
    REQUIRE(input != nullptr);
    input->Release();
}
