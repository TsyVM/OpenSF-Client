#include "Engine/Asset/TextureFile.hpp"

#include "Engine/Core/ByteStream.hpp"

#include <algorithm>

namespace eng {

size_t texture_level_size(TextureFormat format, u32 width, u32 height) {
    width = std::max<u32>(width, 1);
    height = std::max<u32>(height, 1);
    switch (format) {
        case TextureFormat::RGBA8: return size_t(width) * height * 4;
        case TextureFormat::R8: return size_t(width) * height;
        case TextureFormat::BC1: return size_t((width + 3) / 4) * ((height + 3) / 4) * 8;
        case TextureFormat::BC3: return size_t((width + 3) / 4) * ((height + 3) / 4) * 16;
    }
    return 0;
}

bool read_texture(std::span<const u8> bytes, TextureData& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    ByteReader r(bytes);
    if (r.u32() != kTextureMagic) return fail("not a texture");
    if (r.u32() != kTextureVersion) return fail("unsupported texture version");
    out.width = r.u16();
    out.height = r.u16();
    u8 format = r.u8();
    u8 mips = r.u8();
    out.flags = r.u16();
    if (format > u8(TextureFormat::R8)) return fail("unknown texture format");
    out.format = TextureFormat(format);
    if (out.width == 0 || out.height == 0 || mips == 0) return fail("empty texture");
    out.mips.clear();
    u32 w = out.width, h = out.height;
    for (u8 i = 0; i < mips; ++i) {
        u32 size = r.u32();
        if (size != texture_level_size(out.format, w, h)) return fail("mip size mismatch");
        auto view = r.view(size);
        if (!r.ok()) return fail("truncated texture");
        out.mips.emplace_back(view.begin(), view.end());
        w = std::max<u32>(w / 2, 1);
        h = std::max<u32>(h / 2, 1);
    }
    return true;
}

std::vector<u8> write_texture(const TextureData& t) {
    ByteWriter w;
    w.u32(kTextureMagic);
    w.u32(kTextureVersion);
    w.u16(u16(t.width));
    w.u16(u16(t.height));
    w.u8(u8(t.format));
    w.u8(u8(t.mips.size()));
    w.u16(t.flags);
    for (const auto& level : t.mips) {
        w.u32(u32(level.size()));
        w.raw(level.data(), level.size());
    }
    return w.take();
}

}  // namespace eng
