// Copyright (c) 2026 Axertin
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ap
{

// What the client makes of a seed's slot_data. An empty refusal means the client can play the seed.
struct SlotDataCheck
{
    std::string refusal;                   // names the option the client can't honour, or the key with the wrong type
    std::vector<std::string> unknown_keys; // keys the client doesn't know, sorted; empty when the seed is refused
};

SlotDataCheck check_slot_data(const nlohmann::json &slot_data);

} // namespace ap
