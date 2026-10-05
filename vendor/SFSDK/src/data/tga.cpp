// SPDX-License-Identifier: MIT
#include "sf1/data/tga.hpp"
#include "binary_reader.hpp"
#include <algorithm>
#include <array>

namespace sf1::data::tga {

namespace {

struct Header {
    std::uint8_t  id_length, colormap_type, image_type;
    std::uint16_t colormap_first, colormap_length;
    std::uint8_t  colormap_bpp;
    std::uint16_t width, height;
    std::uint8_t  bpp, descriptor;
};

std::uint16_t u16_at(std::span<const std::byte> b, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(b[at]) | (std::to_integer<unsigned>(b[at + 1]) << 8));
}

// One stored pixel of `bpp` bits (little-endian BGR[A]) to RGBA.
std::array<std::uint8_t, 4> unpack(const std::byte* p, unsigned bpp) noexcept {
    auto at = [&](int i) { return std::to_integer<std::uint8_t>(p[i]); };
    switch (bpp) {
        case 8:  return {at(0), at(0), at(0), 255};
        case 15:
        case 16: {
            const unsigned v = at(0) | (at(1) << 8);
            auto expand = [](unsigned c) { return static_cast<std::uint8_t>((c << 3) | (c >> 2)); };
            const std::uint8_t a = (bpp == 16 && !(v & 0x8000)) ? 0 : 255;
            return {expand((v >> 10) & 31), expand((v >> 5) & 31), expand(v & 31), bpp == 16 ? a : std::uint8_t{255}};
        }
        case 24: return {at(2), at(1), at(0), 255};
        default: return {at(2), at(1), at(0), at(3)};
    }
}

}  // namespace

Result<Image> decode(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 18) return err(Error::ShortRead);
    Header h{
        std::to_integer<std::uint8_t>(bytes[0]), std::to_integer<std::uint8_t>(bytes[1]), std::to_integer<std::uint8_t>(bytes[2]),
        u16_at(bytes, 3), u16_at(bytes, 5), std::to_integer<std::uint8_t>(bytes[7]),
        u16_at(bytes, 12), u16_at(bytes, 14),
        std::to_integer<std::uint8_t>(bytes[16]), std::to_integer<std::uint8_t>(bytes[17]),
    };

    const bool rle = h.image_type >= 9 && h.image_type <= 11;
    const std::uint8_t base_type = rle ? static_cast<std::uint8_t>(h.image_type - 8) : h.image_type;
    if (base_type < 1 || base_type > 3) return err(Error::UnsupportedVersion);
    if (h.width == 0 || h.height == 0) return err(Error::Malformed);
    if (base_type == 1) {
        if (h.colormap_type != 1 || h.bpp != 8) return err(Error::UnsupportedVersion);
        if (h.colormap_bpp != 15 && h.colormap_bpp != 16 && h.colormap_bpp != 24 && h.colormap_bpp != 32)
            return err(Error::UnsupportedVersion);
    } else if (base_type == 3) {
        if (h.bpp != 8) return err(Error::UnsupportedVersion);
    } else if (h.bpp != 15 && h.bpp != 16 && h.bpp != 24 && h.bpp != 32) {
        return err(Error::UnsupportedVersion);
    }

    detail::BinaryReader r(bytes);
    (void)r.take(18);
    if (auto id = r.take(h.id_length); !id) return err(id.error());

    std::vector<std::array<std::uint8_t, 4>> palette;
    if (h.colormap_type == 1) {
        const std::size_t entry = (h.colormap_bpp + 7u) / 8u;
        auto raw = r.take_array(h.colormap_length, entry);
        if (!raw) return err(raw.error());
        if (base_type == 1) {
            palette.resize(h.colormap_length);
            for (std::size_t i = 0; i < palette.size(); ++i) palette[i] = unpack(raw->data() + i * entry, h.colormap_bpp);
        }
    }

    const std::size_t pixel_bytes = (h.bpp + 7u) / 8u;
    const std::size_t count = std::size_t{h.width} * h.height;
    std::vector<std::uint8_t> bottom_up(count * 4);

    auto emit = [&](const std::byte* p, std::size_t index) -> bool {
        std::array<std::uint8_t, 4> px;
        if (base_type == 1) {
            const std::size_t i = std::to_integer<std::size_t>(p[0]);
            if (i < h.colormap_first || i - h.colormap_first >= palette.size()) return false;
            px = palette[i - h.colormap_first];
        } else {
            px = unpack(p, h.bpp);
        }
        std::copy(px.begin(), px.end(), bottom_up.begin() + static_cast<std::ptrdiff_t>(index * 4));
        return true;
    };

    if (!rle) {
        auto raw = r.take_array(count, pixel_bytes);
        if (!raw) return err(raw.error());
        for (std::size_t i = 0; i < count; ++i)
            if (!emit(raw->data() + i * pixel_bytes, i)) return err(Error::IndexOutOfRange);
    } else {
        std::size_t i = 0;
        while (i < count) {
            auto packet = r.u8();
            if (!packet) return err(packet.error());
            const std::size_t run = (*packet & 0x7Fu) + 1u;
            if (run > count - i) return err(Error::Malformed);
            if (*packet & 0x80) {
                auto p = r.take(pixel_bytes);
                if (!p) return err(p.error());
                for (std::size_t k = 0; k < run; ++k)
                    if (!emit(p->data(), i++)) return err(Error::IndexOutOfRange);
            } else {
                auto p = r.take_array(run, pixel_bytes);
                if (!p) return err(p.error());
                for (std::size_t k = 0; k < run; ++k)
                    if (!emit(p->data() + k * pixel_bytes, i++)) return err(Error::IndexOutOfRange);
            }
        }
    }

    Image img;
    img.width = h.width;
    img.height = h.height;
    img.source_bpp = base_type == 1 ? h.colormap_bpp : h.bpp;
    img.rgba.resize(count * 4);

    const bool top_origin = (h.descriptor & 0x20) != 0;
    const bool right_origin = (h.descriptor & 0x10) != 0;
    for (std::size_t y = 0; y < h.height; ++y) {
        const std::size_t src_row = top_origin ? y : (h.height - 1 - y);
        for (std::size_t x = 0; x < h.width; ++x) {
            const std::size_t src_col = right_origin ? (h.width - 1 - x) : x;
            const auto* s = &bottom_up[(src_row * h.width + src_col) * 4];
            auto* d = &img.rgba[(y * h.width + x) * 4];
            std::copy(s, s + 4, d);
        }
    }

    const bool stores_alpha = img.source_bpp == 32 || img.source_bpp == 16;
    if (stores_alpha) {
        img.alpha_min = 255;
        img.alpha_max = 0;
        for (std::size_t i = 3; i < img.rgba.size(); i += 4) {
            img.alpha_min = std::min(img.alpha_min, img.rgba[i]);
            img.alpha_max = std::max(img.alpha_max, img.rgba[i]);
        }
        if (img.alpha_max == 0) {   // all-zero alpha: treat as opaque
            for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
            img.alpha_min = img.alpha_max = 255;
        }
    }
    return img;
}

}  // namespace sf1::data::tga
