// SPDX-License-Identifier: MIT
#include "sf1/data/dds.hpp"
#include <algorithm>
#include <cstring>

namespace sf1::data::dds {

namespace {

constexpr std::uint32_t kFourCC_DXT1 = 0x31545844;   // "DXT1"
constexpr std::uint32_t kFourCC_DXT3 = 0x33545844;
constexpr std::uint32_t kFourCC_DXT5 = 0x35545844;
constexpr std::uint32_t kPF_AlphaPixels = 0x1;
constexpr std::uint32_t kPF_FourCC = 0x4;
constexpr std::uint32_t kPF_RGB = 0x40;
constexpr std::uint32_t kCaps2_Cubemap = 0x200;
constexpr std::uint32_t kCaps2_Volume = 0x200000;

std::uint32_t u32_at(std::span<const std::byte> b, std::size_t at) noexcept {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

std::uint64_t level_size(Format f, std::uint32_t w, std::uint32_t h) noexcept {
    const std::uint64_t bw = std::max<std::uint64_t>(1, (w + 3u) / 4u);
    const std::uint64_t bh = std::max<std::uint64_t>(1, (h + 3u) / 4u);
    switch (f) {
        case Format::BC1:   return bw * bh * 8;
        case Format::BC2:
        case Format::BC3:   return bw * bh * 16;
        case Format::BGRA8:
        case Format::BGRX8: return std::uint64_t{w} * h * 4;
        case Format::BGR8:  return std::uint64_t{w} * h * 3;
    }
    return 0;
}

}  // namespace

Result<Info> read_info(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < 128) return err(Error::ShortRead);
    if (u32_at(bytes, 0) != 0x20534444) return err(Error::BadMagic);     // "DDS "
    if (u32_at(bytes, 4) != 124 || u32_at(bytes, 76) != 32) return err(Error::Malformed);

    Info info;
    info.height = u32_at(bytes, 12);
    info.width = u32_at(bytes, 16);
    info.declared_mips = u32_at(bytes, 28);
    if (info.width == 0 || info.height == 0 || info.width > 16384 || info.height > 16384) return err(Error::Malformed);
    if (u32_at(bytes, 112) & (kCaps2_Cubemap | kCaps2_Volume)) return err(Error::UnsupportedVersion);

    const std::uint32_t pf_flags = u32_at(bytes, 80);
    const std::uint32_t fourcc = u32_at(bytes, 84);
    const std::uint32_t bits = u32_at(bytes, 88);
    if (pf_flags & kPF_FourCC) {
        if (fourcc == kFourCC_DXT1) info.format = Format::BC1;
        else if (fourcc == kFourCC_DXT3) info.format = Format::BC2;
        else if (fourcc == kFourCC_DXT5) info.format = Format::BC3;
        else return err(Error::UnsupportedVersion);
    } else if ((pf_flags & kPF_RGB) && bits == 32 && u32_at(bytes, 92) == 0x00FF0000 &&
               u32_at(bytes, 96) == 0x0000FF00 && u32_at(bytes, 100) == 0x000000FF) {
        const bool alpha = (pf_flags & kPF_AlphaPixels) && u32_at(bytes, 104) == 0xFF000000;
        info.format = alpha ? Format::BGRA8 : Format::BGRX8;
    } else if ((pf_flags & kPF_RGB) && bits == 24 && u32_at(bytes, 92) == 0x00FF0000 &&
               u32_at(bytes, 96) == 0x0000FF00 && u32_at(bytes, 100) == 0x000000FF) {
        info.format = Format::BGR8;
    } else {
        return err(Error::UnsupportedVersion);
    }

    std::uint64_t offset = 128;
    std::uint32_t w = info.width, h = info.height;
    const std::uint32_t mips = std::max<std::uint32_t>(1, std::min<std::uint32_t>(info.declared_mips, 16));
    for (std::uint32_t i = 0; i < mips; ++i) {
        const auto size = level_size(info.format, w, h);
        if (offset + size > bytes.size()) break;
        info.levels.push_back(Level{w, h, offset, size});
        offset += size;
        w = std::max<std::uint32_t>(1, w / 2);
        h = std::max<std::uint32_t>(1, h / 2);
    }
    if (info.levels.empty()) return err(Error::HeaderOutOfRange);
    return info;
}

bool bc1_has_transparency(std::span<const std::byte> level) noexcept {
    for (std::size_t at = 0; at + 8 <= level.size(); at += 8) {
        std::uint16_t c0 = 0, c1 = 0;
        std::uint32_t bits = 0;
        std::memcpy(&c0, level.data() + at, 2);
        std::memcpy(&c1, level.data() + at + 2, 2);
        std::memcpy(&bits, level.data() + at + 4, 4);
        if (c0 > c1) continue;
        for (int t = 0; t < 16; ++t)
            if (((bits >> (2 * t)) & 3u) == 3u) return true;
    }
    return false;
}

}  // namespace sf1::data::dds
