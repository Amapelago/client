// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "game/save_file.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <system_error>

#include <zlib.h>

#include <nlohmann/json.hpp>

namespace game
{
namespace
{

constexpr std::uint32_t kRecordMagic = 0x40400000;
constexpr std::uint64_t kChecksumSeed = 0x9be6fa3b72afda1dull;
constexpr std::size_t kManifestMax = 4096;

constexpr std::uint32_t kLocalSig = 0x04034b50, kCentralSig = 0x02014b50, kEndSig = 0x06054b50;
constexpr std::size_t kLocalSize = 30, kCentralSize = 46, kEndSize = 22;

// The entries in the order they are written. The manifest's size varies, so its entry holds 0 and is checked against kManifestMax.
constexpr std::array<const char *, 4> kNames = {"manifest.json", "record", "SYSTEM", "PARAM.SFO"};
constexpr std::array<std::size_t, 4> kSizes = {0, kRecordSize, kSystemSize, kParamSfoSize};

using Bytes = std::vector<std::uint8_t>;

void put(Bytes &out, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint64_t get(const Bytes &in, std::size_t at, int width)
{
    std::uint64_t value = 0;
    for (int i = 0; i < width; ++i)
        value |= std::uint64_t{in[at + i]} << (8 * i);
    return value;
}

std::uint32_t crc_of(const std::uint8_t *data, std::size_t size)
{
    return static_cast<std::uint32_t>(crc32(0, data, static_cast<uInt>(size)));
}

// The entries' contents in kNames order.
std::array<Bytes, 4> entries_of(const SaveFile &file, const SaveData &data)
{
    const std::string manifest =
        nlohmann::json{{"format", 1}, {"seed", file.seed}, {"slot", file.slot}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return {Bytes(manifest.begin(), manifest.end()), data.record, data.system, data.param_sfo};
}

// A zip of stored entries. The same data gives the same bytes: the DOS date is 1980-01-01 and the time 0.
Bytes write_zip(const std::array<Bytes, 4> &entries)
{
    constexpr std::uint16_t kDosDate = 0x0021;
    Bytes out;
    std::array<std::uint32_t, 4> offsets{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        const std::string name = kNames[i];
        offsets[i] = static_cast<std::uint32_t>(out.size());
        put(out, kLocalSig, 4);
        put(out, 10, 2);
        put(out, 0, 2);
        put(out, 0, 2);
        put(out, 0, 2);
        put(out, kDosDate, 2);
        put(out, crc_of(entries[i].data(), entries[i].size()), 4);
        put(out, entries[i].size(), 4);
        put(out, entries[i].size(), 4);
        put(out, name.size(), 2);
        put(out, 0, 2);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), entries[i].begin(), entries[i].end());
    }
    const std::size_t central_at = out.size();
    for (std::size_t i = 0; i < 4; ++i)
    {
        const std::string name = kNames[i];
        put(out, kCentralSig, 4);
        put(out, 10, 2); // version made by
        put(out, 10, 2); // version needed
        put(out, 0, 2);
        put(out, 0, 2);
        put(out, 0, 2);
        put(out, kDosDate, 2);
        put(out, crc_of(entries[i].data(), entries[i].size()), 4);
        put(out, entries[i].size(), 4);
        put(out, entries[i].size(), 4);
        put(out, name.size(), 2);
        put(out, 0, 2); // extra
        put(out, 0, 2); // comment
        put(out, 0, 2); // disk
        put(out, 0, 2); // internal attributes
        put(out, 0, 4); // external attributes
        put(out, offsets[i], 4);
        out.insert(out.end(), name.begin(), name.end());
    }
    const std::size_t central_size = out.size() - central_at;
    put(out, kEndSig, 4);
    put(out, 0, 2);
    put(out, 0, 2);
    put(out, 4, 2);
    put(out, 4, 2);
    put(out, central_size, 4);
    put(out, central_at, 4);
    put(out, 0, 2);
    return out;
}

// The four entries of a zip this client wrote, or nothing if the file is anything else. Every header field the writer sets is checked.
std::optional<std::array<Bytes, 4>> read_zip(const Bytes &in)
{
    if (in.size() < kEndSize || in.size() > 0xffffffffu)
        return std::nullopt;
    const std::size_t end = in.size() - kEndSize;
    const bool one_disk = get(in, end + 4, 2) == 0 && get(in, end + 6, 2) == 0;
    const bool four_entries = get(in, end + 8, 2) == 4 && get(in, end + 10, 2) == 4;
    if (get(in, end, 4) != kEndSig || !one_disk || !four_entries || get(in, end + 20, 2) != 0) // the last is the comment length
        return std::nullopt;
    const std::size_t central_size = get(in, end + 12, 4), central_at = get(in, end + 16, 4);
    if (central_at > end || central_size != end - central_at)
        return std::nullopt;

    std::array<Bytes, 4> found;
    std::array<bool, 4> seen{};
    std::size_t at = central_at;
    for (int n = 0; n < 4; ++n)
    {
        if (end - at < kCentralSize || get(in, at, 4) != kCentralSig)
            return std::nullopt;
        const std::size_t name_size = get(in, at + 28, 2);
        if (get(in, at + 8, 2) != 0 || get(in, at + 10, 2) != 0 || get(in, at + 30, 2) != 0 || get(in, at + 32, 2) != 0 || get(in, at + 34, 2) != 0 ||
            end - at - kCentralSize < name_size)
            return std::nullopt;
        const std::string name(reinterpret_cast<const char *>(&in[at + kCentralSize]), name_size);
        const auto which = std::find(kNames.begin(), kNames.end(), name) - kNames.begin();
        if (which == 4 || seen[which])
            return std::nullopt;
        seen[which] = true;

        const std::uint32_t crc = static_cast<std::uint32_t>(get(in, at + 16, 4));
        const std::size_t size = get(in, at + 20, 4), local = get(in, at + 42, 4);
        if (size != get(in, at + 24, 4) || (which == 0 ? size == 0 || size > kManifestMax : size != kSizes[which]))
            return std::nullopt;

        // The local header matches its central entry, and its data lies before the central directory.
        if (central_at < kLocalSize || local > central_at - kLocalSize || get(in, local, 4) != kLocalSig || get(in, local + 6, 2) != 0 ||
            get(in, local + 8, 2) != 0 || get(in, local + 14, 4) != crc || get(in, local + 18, 4) != size || get(in, local + 22, 4) != size ||
            get(in, local + 26, 2) != name_size || get(in, local + 28, 2) != 0)
            return std::nullopt;
        const std::size_t data_at = local + kLocalSize + name_size;
        if (data_at > central_at || central_at - data_at < size || std::memcmp(&in[local + kLocalSize], name.data(), name_size) != 0 ||
            crc_of(in.data() + data_at, size) != crc)
            return std::nullopt;
        found[which].assign(in.begin() + data_at, in.begin() + data_at + size);
        at += kCentralSize + name_size;
    }
    if (at != end)
        return std::nullopt;
    return found;
}

struct Parsed
{
    std::string seed;
    int slot = 0;
    SaveData data;
};

enum class Read
{
    Missing,
    Unreadable,
    Ok
};

Read read_file(const std::filesystem::path &path, Parsed &parsed)
{
    std::ifstream stream(path, std::ios::binary);
    std::error_code ec;
    if (!stream)
        return std::filesystem::exists(path, ec) || ec ? Read::Unreadable : Read::Missing;
    const Bytes bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto entries = read_zip(bytes);
    if (!entries)
        return Read::Unreadable;
    const nlohmann::json manifest = nlohmann::json::parse(entries->at(0).begin(), entries->at(0).end(), nullptr, false);
    if (!manifest.is_object() || !manifest.contains("format") || !manifest["format"].is_number_integer() || manifest["format"] != 1 ||
        !manifest.contains("seed") || !manifest["seed"].is_string() || !manifest.contains("slot") || !manifest["slot"].is_number_integer() ||
        !readable_record(entries->at(1)))
        return Read::Unreadable;
    parsed.seed = manifest["seed"].get<std::string>();
    parsed.slot = manifest["slot"].get<int>();
    parsed.data = {entries->at(1), entries->at(2), entries->at(3)};
    return Read::Ok;
}

// Keeps [A-Za-z0-9_-] and turns anything else into '_'.
std::string clean(const std::string &text)
{
    std::string out = text;
    for (char &c : out)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            c = '_';
    return out;
}

// Paths in messages are UTF-8, so a profile folder outside the ANSI code page stays readable.
std::string utf8(const std::filesystem::path &path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

bool move_over(const std::filesystem::path &from, const std::filesystem::path &to)
{
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

} // namespace

bool readable_record(std::span<const std::uint8_t> record)
{
    if (record.size() != kRecordSize)
        return false;
    std::uint32_t magic;
    std::memcpy(&magic, record.data(), 4);
    std::uint64_t sum = kChecksumSeed, stored;
    std::memcpy(&stored, record.data() + 8, 8);
    for (std::size_t at = 0; at < kRecordSize; at += 8)
    {
        std::uint64_t q;
        std::memcpy(&q, record.data() + at, 8);
        if (at != 8)
            sum ^= q;
    }
    return magic == kRecordMagic && sum == stored;
}

std::optional<std::size_t> changed_slot(std::span<const std::uint8_t> served, std::span<const std::uint8_t> written)
{
    if (served.size() != kSlotCount * kRecordSize || written.size() != served.size())
        return std::nullopt;
    std::optional<std::size_t> changed;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot)
    {
        const std::size_t at = slot * kRecordSize;
        if (std::memcmp(&served[at], &written[at], kRecordSize) == 0)
            continue;
        if (changed)
            return std::nullopt;
        changed = slot;
    }
    if (changed && !readable_record(written.subspan(*changed * kRecordSize, kRecordSize)))
        return std::nullopt;
    return changed;
}

OpenedSave open_save(const std::filesystem::path &dir, const std::string &seed, int slot)
{
    OpenedSave opened;
    opened.file.seed = seed;
    opened.file.slot = slot;
    const std::string part = clean(seed.substr(0, 64));
    opened.file.zip = dir / ((part.empty() ? "unnamed" : part) + "_" + std::to_string(slot) + ".zip");

    bool any_file = false;
    for (const bool is_bak : {false, true})
    {
        const std::filesystem::path path = is_bak ? std::filesystem::path(opened.file.zip).concat(".bak") : opened.file.zip;
        Parsed parsed;
        const Read read = read_file(path, parsed);
        if (read == Read::Missing)
            continue;
        any_file = true;
        if (read == Read::Unreadable)
            continue;
        if (parsed.seed != seed || parsed.slot != slot)
        {
            opened.refusal = utf8(path.filename()) + " belongs to seed " + parsed.seed + " slot " + std::to_string(parsed.slot);
            return opened;
        }
        opened.data = std::move(parsed.data);
        opened.file.backed_up = is_bak;
        return opened;
    }
    if (any_file)
        opened.refusal = utf8(opened.file.zip.filename()) + " can't be read, and no readable .bak is beside it";
    return opened;
}

std::string commit_save(SaveFile &file, const SaveData &data)
{
    const std::filesystem::path tmp = std::filesystem::path(file.zip).concat(".tmp"), bak = std::filesystem::path(file.zip).concat(".bak");
    std::error_code ec;
    std::filesystem::create_directories(file.zip.parent_path(), ec);
    if (ec)
        return "can't create " + utf8(file.zip.parent_path()) + ": " + ec.message();

    const Bytes zip = write_zip(entries_of(file, data));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(zip.data()), static_cast<std::streamsize>(zip.size()));
        out.close();
        if (!out)
            return "can't write " + utf8(tmp);
    }
    if (!file.backed_up)
    {
        const bool zip_exists = std::filesystem::exists(file.zip, ec);
        if (ec)
            return "can't look for " + utf8(file.zip) + ": " + ec.message();
        if (zip_exists && !move_over(file.zip, bak))
            return "can't move " + utf8(file.zip) + " to " + utf8(bak);
        file.backed_up = true;
    }
    if (!move_over(tmp, file.zip))
        return "can't move " + utf8(tmp) + " to " + utf8(file.zip);
    return {};
}

} // namespace game
