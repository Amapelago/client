// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The client checked against the apworld's own output (tests/fixtures/contract.json).
#include <catch2/catch_test_macros.hpp>

#include "ap/slot_data.hpp"
#include "support/fixture.hpp"

TEST_CASE("the client plays the world's default and non-default slot_data and knows every key")
{
    for (const char *variant : {"default", "non_default"})
    {
        INFO(variant);
        const auto check = ap::check_slot_data(contract_fixture()["slot_data"][variant]);
        CHECK(check.refusal.empty());
        CHECK(check.unknown_keys.empty());
    }
}
