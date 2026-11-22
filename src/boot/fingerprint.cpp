// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "boot/fingerprint.hpp"

// NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from defining min, max and the rarely used APIs.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace boot
{

std::optional<Fingerprint> read_fingerprint(const void *image)
{
    constexpr size_t kPage = 4096;
    const auto *base = static_cast<const uint8_t *>(image);
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, base, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || static_cast<size_t>(dos.e_lfanew) > kPage - sizeof(IMAGE_NT_HEADERS64))
    {
        return std::nullopt;
    }
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, base + dos.e_lfanew, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        return std::nullopt;
    }
    return Fingerprint{nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage};
}

} // namespace boot
