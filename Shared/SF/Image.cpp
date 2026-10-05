#include "SF/Image.hpp"

#include "sf1/data/dds.hpp"
#include "sf1/data/tga.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace sf {

namespace sfd = sf1::data;

namespace {

void colour_565(u8 lo, u8 hi, u8* out) {
    const int v = lo | (hi << 8);
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[0] = u8((r << 3) | (r >> 2));
    out[1] = u8((g << 2) | (g >> 4));
    out[2] = u8((b << 3) | (b >> 2));
    out[3] = 255;
}

// One colour block into 16 RGBA texels. `punch_through` enables BC1's 3-colour mode.
void decode_colour_block(const u8* b, u8 texels[16][4], bool punch_through) {
    u8 pal[4][4];
    colour_565(b[0], b[1], pal[0]);
    colour_565(b[2], b[3], pal[1]);
    const bool four = !punch_through || (b[0] | (b[1] << 8)) > (b[2] | (b[3] << 8));
    for (int c = 0; c < 3; ++c) {
        if (four) {
            pal[2][c] = u8((2 * pal[0][c] + pal[1][c]) / 3);
            pal[3][c] = u8((pal[0][c] + 2 * pal[1][c]) / 3);
        } else {
            pal[2][c] = u8((pal[0][c] + pal[1][c]) / 2);
            pal[3][c] = 0;
        }
    }
    pal[2][3] = 255;
    pal[3][3] = four ? 255 : 0;
    const u32 bits = u32(b[4]) | (u32(b[5]) << 8) | (u32(b[6]) << 16) | (u32(b[7]) << 24);
    for (int t = 0; t < 16; ++t) std::memcpy(texels[t], pal[(bits >> (2 * t)) & 3], 4);
}

bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}

// The next mip of an RGBA8 level by 2x2 box filter (odd edges repeat).
std::vector<u8> half(const std::vector<u8>& src, u32 w, u32 h, u32& nw, u32& nh) {
    nw = std::max<u32>(1, w / 2);
    nh = std::max<u32>(1, h / 2);
    std::vector<u8> out(size_t(nw) * nh * 4);
    for (u32 y = 0; y < nh; ++y)
        for (u32 x = 0; x < nw; ++x) {
            const u32 x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
            const u32 y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            const u8* a = &src[(size_t(y0) * w + x0) * 4];
            const u8* b = &src[(size_t(y0) * w + x1) * 4];
            const u8* c = &src[(size_t(y1) * w + x0) * 4];
            const u8* d = &src[(size_t(y1) * w + x1) * 4];
            u8* o = &out[(size_t(y) * nw + x) * 4];
            for (int k = 0; k < 4; ++k) o[k] = u8((a[k] + b[k] + c[k] + d[k] + 2) / 4);
        }
    return out;
}

bool decode_dds(std::span<const std::byte> bytes, eng::Image& out, std::string* error) {
    auto info = sfd::dds::read_info(bytes);
    if (!info) return fail(error, "DDS header");
    const auto& level = info->levels.front();
    const u8* src = reinterpret_cast<const u8*>(bytes.data()) + level.offset;
    const u32 w = level.width, h = level.height;
    using F = sfd::dds::Format;
    if (info->format == F::BGRA8 || info->format == F::BGRX8 || info->format == F::BGR8) {
        const size_t px = info->format == F::BGR8 ? 3 : 4;
        if (level.size < size_t(w) * h * px) return fail(error, "DDS: truncated level");
        out.width = w;
        out.height = h;
        out.rgba.assign(size_t(w) * h * 4, 255);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            const u8* p = src + i * px;
            u8* o = &out.rgba[i * 4];
            o[0] = p[2];
            o[1] = p[1];
            o[2] = p[0];
            o[3] = info->format == F::BGRA8 ? p[3] : 255;
        }
        return true;
    }
    const int bc = info->format == F::BC1 ? 1 : info->format == F::BC2 ? 2 : 3;
    if (!decode_bc_level(src, level.size, w, h, bc, out)) return fail(error, "DDS: truncated level");
    return true;
}

}  // namespace

u32 Texture::row_pitch(size_t mip) const {
    const u32 w = std::max<u32>(1, width >> mip);
    switch (kind) {
        case Kind::Bc1: return std::max<u32>(1, (w + 3) / 4) * 8;
        case Kind::Bc2:
        case Kind::Bc3: return std::max<u32>(1, (w + 3) / 4) * 16;
        default: return w * 4;
    }
}

size_t Texture::bytes() const {
    size_t n = 0;
    for (const auto& m : mips) n += m.size();
    return n;
}

bool decode_bc_level(const u8* src, size_t size, u32 w, u32 h, int bc, eng::Image& out) {
    const size_t block = bc == 1 ? 8 : 16;
    const u32 bw = std::max<u32>(1, (w + 3) / 4), bh = std::max<u32>(1, (h + 3) / 4);
    if (size < size_t(bw) * bh * block) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(size_t(w) * h * 4, 255);
    u8 texels[16][4];
    for (u32 by = 0; by < bh; ++by)
        for (u32 bx = 0; bx < bw; ++bx) {
            const u8* b = src + (size_t(by) * bw + bx) * block;
            if (bc == 1) {
                decode_colour_block(b, texels, true);
            } else {
                decode_colour_block(b + 8, texels, false);
                u8 alpha[16];
                if (bc == 2) {
                    for (int t = 0; t < 16; ++t) alpha[t] = u8(((b[t / 2] >> ((t & 1) * 4)) & 15) * 17);
                } else {
                    u8 a[8];
                    a[0] = b[0];
                    a[1] = b[1];
                    if (a[0] > a[1]) {
                        for (int i = 1; i < 7; ++i) a[i + 1] = u8(((7 - i) * a[0] + i * a[1]) / 7);
                    } else {
                        for (int i = 1; i < 5; ++i) a[i + 1] = u8(((5 - i) * a[0] + i * a[1]) / 5);
                        a[6] = 0;
                        a[7] = 255;
                    }
                    u64 idx = 0;
                    for (int i = 0; i < 6; ++i) idx |= u64(b[2 + i]) << (8 * i);
                    for (int t = 0; t < 16; ++t) alpha[t] = a[(idx >> (3 * t)) & 7];
                }
                for (int t = 0; t < 16; ++t) texels[t][3] = alpha[t];
            }
            for (int t = 0; t < 16; ++t) {
                const u32 x = bx * 4 + u32(t % 4), y = by * 4 + u32(t / 4);
                if (x < w && y < h) std::memcpy(out.rgba.data() + (size_t(y) * w + x) * 4, texels[t], 4);
            }
        }
    return true;
}

bool decode_image(std::span<const std::byte> bytes, eng::Image& out, std::string* error) {
    const auto* p = reinterpret_cast<const u8*>(bytes.data());
    const size_t n = bytes.size();
    if (n >= 4 && std::memcmp(p, "DDS ", 4) == 0) return decode_dds(bytes, out, error);
    const bool jpeg = n >= 3 && p[0] == 0xFF && p[1] == 0xD8;
    const bool png = n >= 8 && std::memcmp(p, "\x89PNG", 4) == 0;
    const bool bmp = n >= 2 && p[0] == 'B' && p[1] == 'M';
    if (jpeg || png || bmp) return eng::decode_image(std::span<const u8>(p, n), out, error);
    if (auto tga = sfd::tga::decode(bytes)) {
        out.width = tga->width;
        out.height = tga->height;
        out.rgba = std::move(tga->rgba);
        // Some tools write 32-bit files whose alpha is all zero; those are opaque.
        if (!tga->has_alpha())
            for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
        return true;
    }
    return eng::decode_image(std::span<const u8>(p, n), out, error);
}

void classify_alpha(const eng::Image& image, bool& cutout, bool& translucent, bool& invisible) {
    size_t opaque = 0, clear = 0, partial = 0;
    const size_t texels = image.rgba.size() / 4;
    for (size_t i = 3; i < image.rgba.size(); i += 4) {
        const u8 a = image.rgba[i];
        if (a >= 250) ++opaque;
        else if (a <= 5) ++clear;
        else ++partial;
    }
    invisible = texels > 0 && opaque == 0 && partial == 0;
    // A mask is mostly hard edges; a blend has a lot in between.
    translucent = !invisible && partial > texels / 8;
    cutout = !invisible && !translucent && (clear + partial) > 0;
}

Texture texture_from_image(const eng::Image& image, bool mips) {
    Texture t;
    t.kind = Texture::Kind::Rgba8;
    t.width = image.width;
    t.height = image.height;
    classify_alpha(image, t.cutout, t.translucent, t.invisible);
    t.mips.push_back(image.rgba);
    if (mips) {
        u32 w = image.width, h = image.height;
        while (w > 1 || h > 1) {
            u32 nw, nh;
            t.mips.push_back(half(t.mips.back(), w, h, nw, nh));
            w = nw;
            h = nh;
        }
    }
    return t;
}

bool load_texture(std::span<const std::byte> bytes, Texture& out, bool compressed_ok, std::string* error) {
    const auto* p = reinterpret_cast<const u8*>(bytes.data());
    if (compressed_ok && bytes.size() >= 4 && std::memcmp(p, "DDS ", 4) == 0) {
        auto info = sfd::dds::read_info(bytes);
        using F = sfd::dds::Format;
        if (info && (info->format == F::BC1 || info->format == F::BC2 || info->format == F::BC3)) {
            out = Texture{};
            out.kind = info->format == F::BC1 ? Texture::Kind::Bc1 : info->format == F::BC2 ? Texture::Kind::Bc2 : Texture::Kind::Bc3;
            out.width = info->width;
            out.height = info->height;
            // Only the levels that halve down cleanly; a chain that stops early is fine.
            u32 w = info->width, h = info->height;
            for (const auto& level : info->levels) {
                if (level.width != w || level.height != h) break;
                const u8* src = p + level.offset;
                out.mips.emplace_back(src, src + level.size);
                if (w == 1 && h == 1) break;
                w = std::max<u32>(1, w / 2);
                h = std::max<u32>(1, h / 2);
            }
            // Alpha from the top level, decoded once.
            eng::Image top;
            const int bc = out.kind == Texture::Kind::Bc1 ? 1 : out.kind == Texture::Kind::Bc2 ? 2 : 3;
            if (!out.mips.empty() && decode_bc_level(out.mips[0].data(), out.mips[0].size(), out.width, out.height, bc, top))
                classify_alpha(top, out.cutout, out.translucent, out.invisible);
            if (out.kind == Texture::Kind::Bc1) out.translucent = false;   // BC1 alpha is 1 bit
            // A block format wants whole blocks; below 4 texels the card pads, so the chain is fine.
            if (!out.mips.empty()) return true;
        }
    }
    eng::Image image;
    if (!decode_image(bytes, image, error)) return false;
    out = texture_from_image(image, true);
    return true;
}

void key_black(eng::Image& image, u8 threshold) {
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4)
        if (image.rgba[i] <= threshold && image.rgba[i + 1] <= threshold && image.rgba[i + 2] <= threshold) image.rgba[i + 3] = 0;
}

void key_colour(eng::Image& image, u8 r, u8 g, u8 b, u8 tolerance) {
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const int dr = std::abs(int(image.rgba[i]) - r), dg = std::abs(int(image.rgba[i + 1]) - g), db = std::abs(int(image.rgba[i + 2]) - b);
        if (dr <= tolerance && dg <= tolerance && db <= tolerance) image.rgba[i + 3] = 0;
    }
}

void key_colour_from_edges(eng::Image& image, u8 r, u8 g, u8 b, u8 tolerance) {
    const int w = int(image.width), h = int(image.height);
    if (w <= 0 || h <= 0 || image.rgba.size() < size_t(w) * h * 4) return;
    auto matches = [&](int i) {
        const u8* p = &image.rgba[size_t(i) * 4];
        return std::abs(int(p[0]) - r) <= tolerance && std::abs(int(p[1]) - g) <= tolerance && std::abs(int(p[2]) - b) <= tolerance;
    };
    std::vector<u8> keyed(size_t(w) * h, 0);
    std::vector<int> todo;
    auto seed = [&](int x, int y) {
        const int i = y * w + x;
        if (!keyed[size_t(i)] && matches(i)) keyed[size_t(i)] = 1, todo.push_back(i);
    };
    for (int x = 0; x < w; ++x) seed(x, 0), seed(x, h - 1);
    for (int y = 0; y < h; ++y) seed(0, y), seed(w - 1, y);
    while (!todo.empty()) {
        const int i = todo.back();
        todo.pop_back();
        const int x = i % w, y = i / w;
        if (x > 0) seed(x - 1, y);
        if (x + 1 < w) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y + 1 < h) seed(x, y + 1);
    }
    for (int i = 0; i < w * h; ++i) {
        if (!keyed[size_t(i)]) continue;
        u8* p = &image.rgba[size_t(i) * 4];
        const int x = i % w, y = i / w;
        int sum[3] = {0, 0, 0}, n = 0;
        for (const auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || keyed[size_t(ny * w + nx)]) continue;
            const u8* q = &image.rgba[size_t(ny * w + nx) * 4];
            sum[0] += q[0], sum[1] += q[1], sum[2] += q[2], ++n;
        }
        p[0] = n ? u8(sum[0] / n) : 0;
        p[1] = n ? u8(sum[1] / n) : 0;
        p[2] = n ? u8(sum[2] / n) : 0;
        p[3] = 0;
    }
}

}  // namespace sf
