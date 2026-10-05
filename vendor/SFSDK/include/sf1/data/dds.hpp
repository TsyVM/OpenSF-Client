// SPDX-License-Identifier: MIT
// sf1/data/dds.hpp — DirectDraw Surface header reader (no pixel decoding).
//
// Enough to hand a texture to a GPU: size, format and where each mip level's bytes
// are. The shipped area archives hold 2D DXT1 textures (254), one DXT3 and one DXT5;
// uncompressed 32-bit and 24-bit surfaces are also recognised. Cube maps and
// volume textures are reported as unsupported.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf1::data::dds {

enum class Format : std::uint8_t {
    BC1,        // DXT1
    BC2,        // DXT3
    BC3,        // DXT5
    BGRA8,      // 32-bit with alpha mask
    BGRX8,      // 32-bit without alpha
    BGR8,       // 24-bit
};

struct Level {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t offset = 0;   // into the file
    std::uint64_t size = 0;
};

struct Info {
    std::uint32_t        width = 0;
    std::uint32_t        height = 0;
    Format               format = Format::BC1;
    std::uint32_t        declared_mips = 0;   // from the header (0 treated as 1)
    std::vector<Level>   levels;              // the declared levels that fit in the file (at least 1)
};

[[nodiscard]] Result<Info> read_info(std::span<const std::byte> bytes) noexcept;

// True when any BC1 block of `level` uses the 3-colour + transparent mode with a
// transparent texel, i.e. the texture needs alpha testing.
[[nodiscard]] bool bc1_has_transparency(std::span<const std::byte> level) noexcept;

}  // namespace sf1::data::dds
