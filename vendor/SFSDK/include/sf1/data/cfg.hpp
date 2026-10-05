// SPDX-License-Identifier: MIT
// sf1/data/cfg.hpp — parser for `data/config.cfg` control-binding format.
//
// Verified syntax: *setAcontrol"DIK_KEY" ACTION
#pragma once

#include "../result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::cfg {

struct Binding {
    std::string key;      // e.g. "DIK_W", "MOUSE1", "MWHEELDOWN"
    std::string action;   // e.g. "GO", "SHOOT", "WEAPON_1"
};

[[nodiscard]] Result<std::vector<Binding>> parse_controls(std::string_view text) noexcept;

}  // namespace sf1::data::cfg
