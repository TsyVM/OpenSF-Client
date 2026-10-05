// .ttex — the engine's texture format.
//
//   u32 magic 'TTEX'   u32 version (1)
//   u16 width          u16 height
//   u8  format         0 RGBA8, 1 BC1, 2 BC3, 3 R8
//   u8  mip_count      >= 1, level 0 first
//   u16 flags          TextureFlag bits
//   mip_count x { u32 size; size bytes }
//
// RGBA8 rows are top-down. BC blocks follow the usual 4x4 block layout, and levels
// smaller than 4 pixels still store whole blocks.
#pragma once

#include "Engine/Core/Types.hpp"

#include <span>
#include <string>
#include <vector>

namespace eng {

inline constexpr u32 kTextureMagic = fourcc('T', 'T', 'E', 'X');
inline constexpr u32 kTextureVersion = 1;

enum class TextureFormat : u8 { RGBA8 = 0, BC1 = 1, BC3 = 2, R8 = 3 };

enum TextureFlag : u16 {
    kTexHasAlpha = 1 << 0,    // alpha varies; needs blending or testing
    kTexAlphaTest = 1 << 1,   // cut-out: alpha is a mask
    kTexClamp = 1 << 2,       // clamp addressing (UI, sky faces)
    kTexNoMips = 1 << 3,      // stored without a chain on purpose
};

struct TextureData {
    u32 width = 0;
    u32 height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    u16 flags = 0;
    std::vector<std::vector<u8>> mips;
};

size_t texture_level_size(TextureFormat format, u32 width, u32 height);

bool read_texture(std::span<const u8> bytes, TextureData& out, std::string* error = nullptr);
std::vector<u8> write_texture(const TextureData& texture);

}  // namespace eng
