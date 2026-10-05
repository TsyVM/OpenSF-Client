#include "Engine/Render/Device.hpp"

#include <algorithm>

namespace eng {

const char* api_name(Api api) {
    switch (api) {
        case Api::D3D12: return "DirectX 12";
        case Api::D3D11: return "DirectX 11";
#ifdef _WIN32
        case Api::GLES: return "OpenGL";
#else
        case Api::GLES: return "OpenGL ES";
#endif
        default: return "Automatic";
    }
}

u32 texel_bytes(Format f) {
    switch (f) {
        case Format::R8: return 1;
        case Format::R16F: return 2;
        case Format::RGBA16F: return 8;
        default: return 4;   // RGBA8, RG16F, R11G11B10F, R32F, D24S8, D32F
    }
}

size_t texture_bytes(const TextureDesc& d) {
    // Block-compressed formats: BC1 half a byte a texel, BC2 and BC3 a byte.
    const double per_texel = d.format == Format::BC1 ? 0.5 : is_block_compressed(d.format) ? 1.0 : double(texel_bytes(d.format));
    double total = 0;
    int w = std::max(1, d.width), h = std::max(1, d.height);
    for (int m = 0; m < std::max(1, d.mips); ++m) {
        total += double(w) * double(h) * per_texel;
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    const int layers = d.type == TextureType::Cube ? 6 : std::max(1, d.layers);
    return size_t(total * layers * std::max(1, d.samples));
}

}  // namespace eng
