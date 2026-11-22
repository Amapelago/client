// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The AP save file against a temp folder: the zip, its manifest, the .bak and the slot capture.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "game/save_file.hpp"

namespace
{

// A fresh temp folder, removed afterwards.
struct Dir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("okami_apclient_save_" + std::to_string(std::random_device{}()));

    Dir()
    {
        std::filesystem::create_directories(path);
    }
    ~Dir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// A record the game loads, every byte from +0x18 set to m.
std::vector<std::uint8_t> record(std::uint8_t m)
{
    std::vector<std::uint8_t> r(game::kRecordSize, m);
    std::fill(r.begin(), r.begin() + 0x18, std::uint8_t{0});
    const std::uint32_t magic = 0x40400000;
    std::memcpy(r.data(), &magic, 4);
    std::uint64_t sum = 0x9be6fa3b72afda1dull;
    for (std::size_t at = 0; at < r.size(); at += 8)
    {
        std::uint64_t q;
        std::memcpy(&q, &r[at], 8);
        if (at != 8)
            sum ^= q;
    }
    std::memcpy(&r[8], &sum, 8);
    return r;
}

game::SaveData save(std::uint8_t m)
{
    return {record(m), std::vector<std::uint8_t>(game::kSystemSize, m), std::vector<std::uint8_t>(game::kParamSfoSize, m)};
}

std::string bytes(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void commit_in_new_session(const Dir &dir, const game::SaveData &data, const std::string &seed = "seed")
{
    game::OpenedSave opened = game::open_save(dir.path, seed, 1);
    REQUIRE(opened.refusal.empty());
    REQUIRE(game::commit_save(opened.file, data).empty());
}

// A copy of an OKAMI image with r in slot.
std::vector<std::uint8_t> with(std::vector<std::uint8_t> image, std::size_t slot, const std::vector<std::uint8_t> &r)
{
    std::copy(r.begin(), r.end(), image.begin() + slot * game::kRecordSize);
    return image;
}

// record(1) in slot 0 and 29 empty slots: zeros with 0xff at +4 to +7.
std::vector<std::uint8_t> make_served()
{
    std::vector<std::uint8_t> image(game::kSlotCount * game::kRecordSize);
    for (std::size_t slot = 0; slot < game::kSlotCount; ++slot)
        std::fill_n(image.begin() + slot * game::kRecordSize + 4, 4, std::uint8_t{0xff});
    return with(image, 0, record(1));
}

const std::vector<std::uint8_t> served = make_served();

} // namespace

TEST_CASE("an AP save round-trips, and its zip sits in the given folder whatever the seed name holds")
{
    Dir dir;
    const std::string name = "..\\../" + std::string(300, 'x');
    game::OpenedSave opened = game::open_save(dir.path, name, 3);
    REQUIRE(opened.refusal.empty());
    REQUIRE(game::commit_save(opened.file, save(1)).empty());
    CHECK(opened.file.zip.parent_path() == dir.path);
    const game::OpenedSave again = game::open_save(dir.path, name, 3);
    REQUIRE(again.data);
    CHECK(again.data->record == save(1).record);
    CHECK(again.data->system == save(1).system);
    CHECK(again.data->param_sfo == save(1).param_sfo);
}

TEST_CASE("a seed with no zip and no .bak has no AP save yet")
{
    Dir dir;
    const game::OpenedSave opened = game::open_save(dir.path, "seed", 1);
    CHECK(opened.refusal.empty());
    CHECK_FALSE(opened.data);
    CHECK(std::filesystem::is_empty(dir.path));
}

TEST_CASE("an unreadable zip gives way to its .bak")
{
    Dir dir;
    commit_in_new_session(dir, save(1));
    SECTION("bytes that aren't a zip")
    {
        commit_in_new_session(dir, save(2));
        std::ofstream(dir.path / "seed_1.zip", std::ios::binary) << std::string(100, 'Z');
    }
    SECTION("a record that fails its checksum")
    {
        game::SaveData broken = save(2);
        broken.record[0x100] ^= 1;
        commit_in_new_session(dir, broken);
    }
    const game::OpenedSave opened = game::open_save(dir.path, "seed", 1);
    CHECK(opened.refusal.empty());
    REQUIRE(opened.data);
    CHECK(opened.data->record == save(1).record);
}

TEST_CASE("a manifest naming another seed refuses the bind and leaves the files byte-identical")
{
    Dir dir;
    // "a b" and "a_b" share the file name a_b_1.zip.
    commit_in_new_session(dir, save(1), "a b");
    commit_in_new_session(dir, save(2), "a b");
    const auto zip = bytes(dir.path / "a_b_1.zip"), bak = bytes(dir.path / "a_b_1.zip.bak");
    const game::OpenedSave opened = game::open_save(dir.path, "a_b", 1);
    CHECK_FALSE(opened.refusal.empty());
    CHECK_FALSE(opened.data);
    CHECK(bytes(dir.path / "a_b_1.zip") == zip);
    CHECK(bytes(dir.path / "a_b_1.zip.bak") == bak);
}

TEST_CASE("a commit that fails leaves the zip and .bak as they were, and the next one goes through")
{
    Dir dir;
    commit_in_new_session(dir, save(1));
    commit_in_new_session(dir, save(2));
    const auto zip = bytes(dir.path / "seed_1.zip"), bak = bytes(dir.path / "seed_1.zip.bak");
    game::OpenedSave opened = game::open_save(dir.path, "seed", 1);
    std::filesystem::create_directory(dir.path / "seed_1.zip.tmp"); // the tmp file can't be written
    CHECK_FALSE(game::commit_save(opened.file, save(3)).empty());
    CHECK(bytes(dir.path / "seed_1.zip") == zip);
    CHECK(bytes(dir.path / "seed_1.zip.bak") == bak);
    std::filesystem::remove(dir.path / "seed_1.zip.tmp");
    REQUIRE(game::commit_save(opened.file, save(3)).empty());
    CHECK(game::open_save(dir.path, "seed", 1).data->record == save(3).record);
    CHECK(bytes(dir.path / "seed_1.zip.bak") == zip);
}

TEST_CASE("the .bak holds the AP save as the session found it through every later commit")
{
    Dir dir;
    commit_in_new_session(dir, save(1));
    const auto found = bytes(dir.path / "seed_1.zip");
    game::OpenedSave opened = game::open_save(dir.path, "seed", 1);
    REQUIRE(game::commit_save(opened.file, save(2)).empty());
    REQUIRE(game::commit_save(opened.file, save(3)).empty());
    CHECK(bytes(dir.path / "seed_1.zip.bak") == found);
    CHECK(game::open_save(dir.path, "seed", 1).data->record == save(3).record);
}

TEST_CASE("a zip that was unreadable at bind is never rotated over a good .bak")
{
    Dir dir;
    commit_in_new_session(dir, save(1));
    commit_in_new_session(dir, save(2));
    std::ofstream(dir.path / "seed_1.zip", std::ios::binary) << std::string(100, 'Z');
    const auto bak = bytes(dir.path / "seed_1.zip.bak");
    game::OpenedSave opened = game::open_save(dir.path, "seed", 1);
    REQUIRE(opened.data);
    REQUIRE(game::commit_save(opened.file, save(3)).empty());
    CHECK(bytes(dir.path / "seed_1.zip.bak") == bak);
    CHECK(game::open_save(dir.path, "seed", 1).data->record == save(3).record);
}

TEST_CASE("a written OKAMI image gives the one slot that changed when it holds a loadable record")
{
    CHECK(game::changed_slot(served, with(served, 7, record(2))) == 7);
    CHECK(game::changed_slot(served, with(served, 0, record(2))) == 0);
    CHECK_FALSE(game::changed_slot(served, served));
    CHECK_FALSE(game::changed_slot(served, with(with(served, 7, record(2)), 8, record(3))));
    std::vector<std::uint8_t> broken = record(2);
    broken[0x100] ^= 1;
    CHECK_FALSE(game::changed_slot(served, with(served, 7, broken)));
    CHECK_FALSE(game::changed_slot(served, std::span(served).first(game::kRecordSize)));
}
