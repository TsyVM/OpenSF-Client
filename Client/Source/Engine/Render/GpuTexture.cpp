#include "Engine/Render/GpuTexture.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace eng {

namespace {

u32 rd32(const u8* p) {
    u32 v;
    std::memcpy(&v, p, 4);
    return v;
}

void scan_alpha(const Image& img, GpuTexture& out) {
    out.has_alpha = false;
    out.alpha_binary = true;
    for (size_t i = 3; i < img.rgba.size(); i += 4) {
        const u8 a = img.rgba[i];
        if (a != 255) out.has_alpha = true;
        if (a != 0 && a != 255) out.alpha_binary = false;
    }
}

// One box-filtered mip level down. Alpha-weighted colour so cut-out edges do not bleed black.
Image half(const Image& in) {
    Image out;
    out.width = std::max<u32>(1, in.width / 2);
    out.height = std::max<u32>(1, in.height / 2);
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (u32 y = 0; y < out.height; ++y)
        for (u32 x = 0; x < out.width; ++x) {
            u32 sum[4] = {0, 0, 0, 0};
            u32 wsum = 0, n = 0;
            for (u32 dy = 0; dy < 2; ++dy)
                for (u32 dx = 0; dx < 2; ++dx) {
                    const u32 sx = std::min(in.width - 1, x * 2 + dx), sy = std::min(in.height - 1, y * 2 + dy);
                    const u8* p = in.rgba.data() + (size_t(sy) * in.width + sx) * 4;
                    const u32 w = p[3] + 1;
                    for (int c = 0; c < 3; ++c) sum[c] += p[c] * w;
                    sum[3] += p[3];
                    wsum += w;
                    ++n;
                }
            u8* d = out.rgba.data() + (size_t(y) * out.width + x) * 4;
            for (int c = 0; c < 3; ++c) d[c] = u8(sum[c] / wsum);
            d[3] = u8(sum[3] / n);
        }
    return out;
}

bool create_dds(Device& device, std::span<const u8> b, GpuTexture& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (b.size() < 128) return fail("dds: truncated header");
    const u32 height = rd32(b.data() + 12), width = rd32(b.data() + 16);
    u32 mips = std::max<u32>(1, rd32(b.data() + 28));
    const u32 pf_flags = rd32(b.data() + 80), fourcc = rd32(b.data() + 84);
    if (!(pf_flags & 0x4)) return fail("dds: not a compressed format");
    Format fmt;
    u32 block_bytes;
    switch (fourcc) {
        case 0x31545844: fmt = Format::BC1; block_bytes = 8; out.has_alpha = true; out.alpha_binary = true; break;
        case 0x32545844:
        case 0x33545844: fmt = Format::BC2; block_bytes = 16; out.has_alpha = true; out.alpha_binary = false; break;
        case 0x34545844:
        case 0x35545844: fmt = Format::BC3; block_bytes = 16; out.has_alpha = true; out.alpha_binary = false; break;
        default: return fail("dds: unsupported fourcc");
    }
    std::vector<TextureData> subs;
    size_t at = 128;
    u32 w = width, h = height;
    for (u32 m = 0; m < mips; ++m) {
        const u32 bw = std::max<u32>(1, (w + 3) / 4), bh = std::max<u32>(1, (h + 3) / 4);
        const size_t bytes = size_t(bw) * bh * block_bytes;
        if (at + bytes > b.size()) {
            mips = m;
            break;
        }
        subs.push_back({b.data() + at, bw * block_bytes});
        at += bytes;
        w = std::max<u32>(1, w / 2);
        h = std::max<u32>(1, h / 2);
    }
    if (mips == 0) return fail("dds: no complete mip level");
    // BC formats need the top level to be a multiple of 4 unless it is tiny; the retail DDS
    // payloads always are.
    TextureDesc td;
    td.format = fmt;
    td.width = int(width);
    td.height = int(height);
    td.mips = int(mips);
    out.tex = device.create_texture(td, subs);
    if (!out.tex) return fail("dds: the texture was not made");
    out.width = width;
    out.height = height;
    return true;
}

}  // namespace

bool create_texture_rgba(Device& device, const Image& image, GpuTexture& out, bool mips) {
    if (image.width == 0 || image.height == 0) return false;
    scan_alpha(image, out);
    std::vector<Image> chain;
    chain.push_back(image);
    if (mips)
        while (chain.back().width > 1 || chain.back().height > 1) chain.push_back(half(chain.back()));
    std::vector<TextureData> subs;
    for (const Image& m : chain) subs.push_back({m.rgba.data(), m.width * 4});
    TextureDesc td;
    td.width = int(image.width);
    td.height = int(image.height);
    td.mips = int(chain.size());
    out.tex = device.create_texture(td, subs);
    if (!out.tex) return false;
    out.width = image.width;
    out.height = image.height;
    return true;
}

bool create_texture(Device& device, std::span<const u8> bytes, GpuTexture& out, std::string* error) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) == 0) return create_dds(device, bytes, out, error);
    Image img;
    if (!decode_image(bytes, img, error)) return false;
    return create_texture_rgba(device, img, out, true);
}

}  // namespace eng
