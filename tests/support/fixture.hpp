// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

// tests/fixtures/contract.json, parsed once. The path comes from this header's location, so the file is found
// wherever the build tree sits.
inline const nlohmann::json &contract_fixture()
{
    static const nlohmann::json fixture = []
    {
        const auto path = std::filesystem::path(__FILE__).parent_path() / "../fixtures/contract.json";
        std::ifstream in(path);
        if (!in)
            FAIL("missing " << path << "; regenerate it with scripts/gen_contract_fixture.py");
        return nlohmann::json::parse(in);
    }();
    return fixture;
}
