// SPDX-License-Identifier: MIT
// sf1/data/env.hpp — object placements for a map ("<map>_obj.env").
//
// The .map file is only the level shell: every barrel, barricade, crate and sign
// standing in it is placed by this file and drawn from object\env\<family>\<name>.val.
//
// Layout, verified against all 28 .env files the installed client ships (every one
// satisfies size == 4 + count * 640 exactly):
//
//   u32 count
//   count × 640 bytes {
//       char  instance[256]     "Drum_01_14_SECTOR_13"
//       char  source[256]       "Drum_01_14_SECTOR"
//       float position[3]
//       float scale[3]
//       byte  unknown_a[12]     0xCC in every shipped file — never written
//       float world[4][4]       row-major for row vectors; row 3 is the translation
//       byte  unknown_b[28]     0xCC in every shipped file
//   }
//
// position and scale are confirmed against the matrix: for a scaled placement
// (sf_m_nuclear "Drum_01_14_SECTOR", scale 0.85) the three basis rows each have
// length 0.85 and row 3 equals position. The 0xCC spans are uninitialised padding
// from whatever exported these; their meaning is unknown and nothing reads them.
//
// `source` is an instance name, not a file name: stripping its trailing
// "_<digits>_SECTOR" gives the object stem, e.g. "Drum_01". The .val mesh format
// itself is NOT decoded, so this reader reports placements, not geometry.
#pragma once

#include "../result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::env {

inline constexpr std::size_t kNameWidth  = 256;
inline constexpr std::size_t kRecordSize = 640;

struct Placement {
    std::uint64_t          offset = 0;      // file offset of the record
    std::string            instance;        // "Drum_01_14_SECTOR_13"
    std::string            source;          // "Drum_01_14_SECTOR"
    std::array<float, 3>   position{};
    std::array<float, 3>   scale{1, 1, 1};
    std::array<float, 16>  world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct Objects {
    std::vector<Placement> placements;
    std::uint64_t          size = 0;
};

// "Drum_01_14_SECTOR" -> "Drum_01"; a name with no "_<digits>_SECTOR" tail is
// returned unchanged.
[[nodiscard]] std::string_view object_stem(std::string_view source) noexcept;

[[nodiscard]] Result<Objects> read(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::env
