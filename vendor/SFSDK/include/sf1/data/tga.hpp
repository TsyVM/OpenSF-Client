// SPDX-License-Identifier: MIT
// sf1/data/tga.hpp — Truevision TGA decoder (textures and sector lightmaps).
//
// Decodes image types 1, 2, 3 (colour-mapped, true-colour, greyscale) and their
// run-length variants 9, 10, 11, at 8, 15, 16, 24 or 32 bits per pixel, into
// top-down RGBA8. Material textures in the shipped area archives are type 2 at
// 24 or 32 bpp with a bottom-left origin.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf1::data::tga {

struct Image {
    std::uint32_t              width = 0;
    std::uint32_t              height = 0;
    std::vector<std::uint8_t>  rgba;          // width × height × 4, first row = top
    std::uint8_t               source_bpp = 0;
    std::uint8_t               alpha_min = 255;
    std::uint8_t               alpha_max = 255;
    // True when the file stores alpha that varies or is not fully opaque. A 32-bpp
    // file whose alpha is all 0 is treated as having no alpha: several tools write that.
    [[nodiscard]] bool has_alpha() const noexcept { return alpha_min < 255 && alpha_max > 0; }
};

[[nodiscard]] Result<Image> decode(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::tga
