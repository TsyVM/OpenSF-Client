// SPDX-License-Identifier: MIT
// sf1/data/osf.hpp — an object's setup file ("object/env/<family>/<name>.osf").
//
// Plain text beside every prop mesh, CRLF lines, and the same shape across the 657
// the client ships:
//
//   [Version] :  6
//   [Mapping Source Count] :  1
//   drum.dds
//
//   [Object Data Name] :  indrum_01.val
//   [Object_Type] : METAL          etc WOOD GLASS CON MUD LADDER LIGHT PARTICLE billboard ...
//   [TransParent] : 0
//   [Crash_Data] : 1               1 = the prop blocks players, 0 = it does not
//   End
//
// Some files also carry LightEnable, CULLMODE/CullMode, ZWRITEENABLE, HP,
// Effect_Type, cube-map and UV-animation keys. 162 of them have a NUL byte after a
// value ("1\0"), left there by whatever wrote them.
//
// The model is the text itself. Reading a value trims spaces and NULs; changing one
// rewrites only the characters of that value, so the rest of the file — spacing,
// NULs, CRLF — comes back exactly as it was read.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::osf {

struct Document {
    std::string text;

    // The value after "[key] :", trimmed of spaces and NULs. Keys match case-insensitively.
    [[nodiscard]] std::optional<std::string> value(std::string_view key) const;
    // Replaces an existing value in place. False when the key is absent.
    bool set_value(std::string_view key, std::string_view value);

    // The texture names listed after [Mapping Source Count].
    [[nodiscard]] std::vector<std::string> textures() const;

    // [Crash_Data]: true when the prop blocks movement. Empty when the key is absent.
    [[nodiscard]] std::optional<bool> collision() const;
    bool set_collision(bool blocks) { return set_value("Crash_Data", blocks ? "1" : "0"); }
};

// Fails only when the text has no "[Object Data Name]" line.
[[nodiscard]] Result<Document> read(std::span<const std::byte> bytes);

}  // namespace sf1::data::osf
