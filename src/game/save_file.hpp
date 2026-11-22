// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The AP save of one seed and slot: a zip in the client's saves folder that holds one playthrough, with its .bak.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace game
{

// One OKAMI slot record, the whole OKAMI image of kSlotCount records, and the sizes of the other two files.
constexpr std::size_t kRecordSize = 0x172a0, kSlotCount = 30, kSystemSize = 12, kParamSfoSize = 0x610;

// One playthrough, as the AP save holds it.
struct SaveData
{
    std::vector<std::uint8_t> record, system, param_sfo;
};

// The zip's path, the raw seed and slot for the manifest, and whether this session may no longer move the .zip to .bak.
struct SaveFile
{
    std::filesystem::path zip;
    std::string seed;
    int slot = 0;
    bool backed_up = false;
};

// data is empty when the seed has no AP save yet. refusal is non-empty when the files must not be bound.
struct OpenedSave
{
    SaveFile file;
    std::optional<SaveData> data;
    std::string refusal;
};

// Reads the AP save for a seed and slot from dir, falling back to the .bak. Any thread; never writes.
OpenedSave open_save(const std::filesystem::path &dir, const std::string &seed, int slot);

// Writes data to the zip, keeping the .bak as the session found it. Returns why it failed, or an empty string. Failures come back as a reason, not an
// exception.
std::string commit_save(SaveFile &file, const SaveData &data);

// Whether the game's loader accepts the record: the size, the magic at +0 and the checksum at +0x08.
bool readable_record(std::span<const std::uint8_t> record);

// The slot of a written OKAMI image that differs from the served one, when exactly one differs and it holds a readable record.
std::optional<std::size_t> changed_slot(std::span<const std::uint8_t> served, std::span<const std::uint8_t> written);

} // namespace game
