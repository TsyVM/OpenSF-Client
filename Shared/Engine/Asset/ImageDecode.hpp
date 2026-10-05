// Decodes common image files to top-down RGBA8: PNG, JPEG, BMP, GIF and TIFF through
// the Windows Imaging Component, TGA with a built-in decoder.
#pragma once

#include "Engine/Core/Types.hpp"

#include <span>
#include <string>
#include <vector>

namespace eng {

struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
};

bool decode_image(std::span<const u8> bytes, Image& out, std::string* error = nullptr);
bool decode_tga(std::span<const u8> bytes, Image& out, std::string* error = nullptr);

// Writes a PNG through WIC (screenshots, previews).
std::vector<u8> encode_png(const Image& image);

// Box-filtered resize.
Image resize_image(const Image& image, u32 width, u32 height);

}  // namespace eng
