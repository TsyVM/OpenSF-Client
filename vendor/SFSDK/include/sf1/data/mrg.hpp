// SPDX-License-Identifier: MIT
// sf1/data/mrg.hpp — reader for the .mrg lobby archive.
//
// Verified 2026-09-15 against all 103 .mrg files in the installed client:
//   * the table is the .sff table exactly (see sff.hpp);
//   * every payload is XORed byte-wise with 0x94. That holds for all 5,700 images
//     (.tga/.bmp/.jpg/.png) and all 182 .txt entries. The one entry that looks like
//     a different key, Banner_Room_256x64.jpg, is a PNG with the wrong extension, so
//     the key must not be guessed from the extension's expected signature.
#pragma once

#include "sff.hpp"
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace sf1::data::mrg {

using Header = sff::Header;
using Entry  = sff::Entry;

inline constexpr std::byte kXorKey{0x94};

[[nodiscard]] inline Result<Header> read_header(std::span<const std::byte> bytes) noexcept {
    return sff::read_header(bytes);
}
[[nodiscard]] inline Result<std::vector<Entry>> read_entries(std::span<const std::byte> bytes) noexcept {
    return sff::read_entries(bytes);
}

// Undo the payload XOR in place. Applying it twice restores the stored bytes.
void decode(std::span<std::byte> payload) noexcept;

// Every entry with a decoded copy of its payload.
[[nodiscard]] Result<std::vector<std::pair<Entry, std::vector<std::byte>>>>
extract_all(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::mrg
