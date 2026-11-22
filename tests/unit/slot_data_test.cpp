// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "ap/slot_data.hpp"
#include "support/fixture.hpp"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("an option the client can't honour refuses the seed by name")
{
    auto slot_data = contract_fixture()["slot_data"]["default"];
    slot_data["RandomizeShops"] = 1;
    CHECK_THAT(ap::check_slot_data(slot_data).refusal, ContainsSubstring("RandomizeShops"));
}

TEST_CASE("a known key with the wrong type refuses the seed by name")
{
    auto slot_data = contract_fixture()["slot_data"]["default"];
    slot_data["ProgressiveWeapons"] = "1";
    CHECK_THAT(ap::check_slot_data(slot_data).refusal, ContainsSubstring("ProgressiveWeapons"));
}

TEST_CASE("an unknown key is reported without refusing the seed")
{
    auto slot_data = contract_fixture()["slot_data"]["default"];
    slot_data["Goal"] = 0;
    const auto check = ap::check_slot_data(slot_data);
    CHECK(check.refusal.empty());
    CHECK(check.unknown_keys == std::vector<std::string>{"Goal"});
}
