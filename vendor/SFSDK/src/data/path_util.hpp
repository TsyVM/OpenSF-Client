// SPDX-License-Identifier: MIT
// src/data/path_util.hpp — path to text without the narrow-codepage conversion.
//
// path::string() converts through the active code page on Windows and throws when a
// character has no mapping (a folder named in another script). The readers are
// noexcept, so they compare extensions and names through UTF-8 instead.
#pragma once

#include <filesystem>
#include <string>

namespace sf1::data::detail {

[[nodiscard]] inline std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}  // namespace sf1::data::detail
