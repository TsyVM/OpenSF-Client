// SPDX-License-Identifier: MIT
// sf1/data/msf.hpp — the texture table of a .map (the "mapping source" manifest).
//
// Plain text, CRLF lines:
//
//   [Version] :  6
//   [Mapping Source Count] :  56
//   texture\SF_M_Ground\stground01.JPG  8
//   ...
//
// Line N (0-based, blank lines skipped) is material N of the .map. Each line is a
// path ending in .jpg/.jpeg/.tga/.bmp/.png/.dds (any case) and an optional decimal
// number. The number's meaning is unverified; it is kept as native_flag.
// Lines after the declared count are ignored, and fewer lines than declared is an
// error, both as the Python reference reader does.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::msf {

struct Mapping {
    std::string                   path;          // as written, backslashes kept
    std::optional<std::uint32_t>  native_flag;   // unverified meaning
};

[[nodiscard]] Result<std::vector<Mapping>> read(std::string_view text) noexcept;
[[nodiscard]] Result<std::vector<Mapping>> read(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::msf
