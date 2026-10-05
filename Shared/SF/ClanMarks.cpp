#include "SF/ClanMarks.hpp"

#include "SF/Image.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

// stb_image's inflate (vendor/stb, public domain), kept to this file.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <span>

namespace sf {

using eng::u16;
using eng::u32;
using eng::u8;

namespace {

u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

constexpr const char* kFiles[] = {"clanmark_bg.dfz", "clanmark_frame.dfz", "clanmark_symbol.dfz"};

}  // namespace

std::optional<std::vector<u8>> unzip_entry(const std::vector<u8>& zip, u32 method, u32 offset, u32 packed, u32 size) {
    // The local header: 30 bytes, then its own name and extra field (which may differ from the
    // central directory's), then the data.
    if (size_t(offset) + 30 > zip.size() || rd32(&zip[offset]) != 0x04034b50) return std::nullopt;
    const size_t data = size_t(offset) + 30 + rd16(&zip[offset + 26]) + rd16(&zip[offset + 28]);
    if (data + packed > zip.size()) return std::nullopt;
    std::vector<u8> out(size);
    if (method == 0) {
        if (packed != size) return std::nullopt;
        std::memcpy(out.data(), &zip[data], size);
        return out;
    }
    if (method != 8) return std::nullopt;
    const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), int(size), reinterpret_cast<const char*>(&zip[data]), int(packed));
    if (got != int(size)) return std::nullopt;
    return out;
}

bool ClanMarks::open(const std::filesystem::path& client_data) {
    bool any = false;
    for (size_t l = 0; l < layers_.size(); ++l) {
        Archive& a = layers_[l];
        a = {};
        auto bytes = eng::fs::read_file(client_data / "clan" / kFiles[l]);
        if (!bytes || bytes->size() < 22) continue;
        a.bytes.assign(reinterpret_cast<const u8*>(bytes->data()), reinterpret_cast<const u8*>(bytes->data()) + bytes->size());
        // The end of central directory record, searched back from the end (a comment may follow it).
        const std::vector<u8>& z = a.bytes;
        size_t eocd = std::string::npos;
        for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i < 22 + 65536;)
            if (rd32(&z[i]) == 0x06054b50) {
                eocd = i;
                break;
            }
        if (eocd == std::string::npos) continue;
        const u16 n = rd16(&z[eocd + 10]);
        size_t p = rd32(&z[eocd + 16]);
        for (u16 k = 0; k < n && p + 46 <= z.size() && rd32(&z[p]) == 0x02014b50; ++k) {
            Entry e;
            e.method = rd16(&z[p + 10]);
            e.packed = rd32(&z[p + 20]);
            e.size = rd32(&z[p + 24]);
            const u16 name_len = rd16(&z[p + 28]), extra = rd16(&z[p + 30]), comment = rd16(&z[p + 32]);
            e.offset = rd32(&z[p + 42]);
            if (p + 46 + name_len > z.size()) break;
            e.name.assign(reinterpret_cast<const char*>(&z[p + 46]), name_len);
            for (char c : e.name) {
                if (!std::isdigit(static_cast<unsigned char>(c))) break;
                e.number = e.number * 10 + (c - '0');
            }
            if (e.number > 0) a.entries.push_back(std::move(e));
            p += 46 + size_t(name_len) + extra + comment;
        }
        std::sort(a.entries.begin(), a.entries.end(), [](const Entry& x, const Entry& y) { return x.number < y.number; });
        any |= !a.entries.empty();
    }
    LOG_INFO("Clan marks: %d backgrounds, %d frames, %d symbols", count(MarkLayer::Background), count(MarkLayer::Frame), count(MarkLayer::Symbol));
    return any;
}

int ClanMarks::count(MarkLayer layer) const { return layer < MarkLayer::Count ? int(layers_[size_t(layer)].entries.size()) : 0; }

std::optional<eng::Image> ClanMarks::image(MarkLayer layer, int index) const {
    if (layer >= MarkLayer::Count || index < 1 || index > count(layer)) return std::nullopt;
    std::lock_guard lock(mutex_);
    const Archive& a = layers_[size_t(layer)];
    const Entry& e = a.entries[size_t(index - 1)];
    auto file = unzip_entry(a.bytes, e.method, e.offset, e.packed, e.size);
    if (!file) {
        LOG_WARN("Clan marks: %s could not be unpacked", e.name.c_str());
        return std::nullopt;
    }
    eng::Image img;
    std::string err;
    if (!decode_image(std::as_bytes(std::span(*file)), img, &err)) {
        LOG_WARN("Clan marks: %s: %s", e.name.c_str(), err.c_str());
        return std::nullopt;
    }
    return img;
}

}  // namespace sf
