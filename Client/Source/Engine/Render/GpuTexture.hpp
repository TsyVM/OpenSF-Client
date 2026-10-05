// Texture upload: the PNG and DDS (DXT1/3/5) payloads Pure3D embeds, plus plain RGBA images.
#pragma once

#include "Engine/Asset/ImageDecode.hpp"
#include "Engine/Render/Device.hpp"

#include <span>

namespace eng {

struct GpuTexture {
    TextureRef tex;
    u32 width = 0, height = 0;
    bool has_alpha = false;      // any texel below full alpha
    bool alpha_binary = true;    // alpha is only ever 0 or 255 (cut-out)
};

// Decodes an embedded image file (PNG, BMP, TGA, DDS) and uploads it with a full mip chain.
bool create_texture(Device& device, std::span<const u8> file_bytes, GpuTexture& out, std::string* error = nullptr);
bool create_texture_rgba(Device& device, const Image& image, GpuTexture& out, bool mips = true);

}  // namespace eng
