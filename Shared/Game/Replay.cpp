#include "Game/Replay.hpp"

#include "Engine/Core/ByteStream.hpp"
#include "Engine/Core/Log.hpp"

#include <cstdlib>
#include <cstring>

// stb's zlib, compiled here and kept to this file (the image decoders do not use stb on Windows,
// and Android's copy of it in Engine/Asset/ImageDecode.cpp is a separate one).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace lsf::replay {

using eng::u32;
using eng::u8;

namespace {

constexpr size_t kBlockTarget = 256 * 1024;

void put_u32(std::FILE* f, u32 v) { std::fwrite(&v, 4, 1, f); }

}  // namespace

bool Writer::open(const std::filesystem::path& file, const Header& header) {
    close();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
#ifdef _WIN32
    file_ = _wfopen(file.c_str(), L"wb");
#else
    file_ = std::fopen(file.c_str(), "wb");
#endif
    if (!file_) return false;
    eng::ByteWriter w;
    w.u32(kMagic);
    w.u32(kVersion);
    w.u32(header.game_version);
    w.u32(header.match);
    w.u64(header.started);
    w.string(header.server);
    w.u32(u32(header.load.size()));
    w.bytes(header.load);
    w.string(header.manifest);
    w.u16(eng::u16(std::min<size_t>(header.packs.size(), 64)));
    for (size_t i = 0; i < header.packs.size() && i < 64; ++i) w.string(header.packs[i]);
    std::fwrite(w.data().data(), 1, w.data().size(), file_);
    written_ = w.data().size();
    block_.clear();
    std::fflush(file_);
    return true;
}

void Writer::add(u32 ms, std::span<const u8> message) {
    if (!file_) return;
    const u32 n = u32(message.size());
    const size_t at = block_.size();
    block_.resize(at + 8 + n);
    std::memcpy(&block_[at], &ms, 4);
    std::memcpy(&block_[at + 4], &n, 4);
    if (n) std::memcpy(&block_[at + 8], message.data(), n);
    if (block_.size() >= kBlockTarget) flush();
}

void Writer::flush() {
    if (!file_ || block_.empty()) return;
    int packed_size = 0;
    unsigned char* packed = stbi_zlib_compress(block_.data(), int(block_.size()), &packed_size, 6);
    put_u32(file_, u32(block_.size()));
    if (packed && packed_size > 0) {
        put_u32(file_, u32(packed_size));
        std::fwrite(packed, 1, size_t(packed_size), file_);
        written_ += 8 + size_t(packed_size);
    } else {
        // Stored raw: a packed size equal to the raw size says so.
        put_u32(file_, u32(block_.size()));
        std::fwrite(block_.data(), 1, block_.size(), file_);
        written_ += 8 + block_.size();
    }
    std::free(packed);
    block_.clear();
    std::fflush(file_);
}

void Writer::close() {
    if (!file_) return;
    flush();
    put_u32(file_, 0);
    put_u32(file_, 0);
    std::fclose(file_);
    file_ = nullptr;
}

namespace {

bool parse_header(eng::ByteReader& r, Header& h) {
    if (r.u32() != kMagic) return false;
    const u32 version = r.u32();
    if (version != 1 && version != kVersion) return false;
    h.game_version = r.u32();
    h.match = r.u32();
    h.started = r.u64();
    h.server = r.string(128);
    const u32 n = r.u32();
    if (!r.ok() || n > r.remaining()) return false;
    h.load.resize(n);
    if (n) r.raw(h.load.data(), n);
    h.manifest.clear();
    h.packs.clear();
    if (version >= 2) {
        h.manifest = r.string(64);
        const eng::u16 packs = r.u16();
        if (!r.ok() || packs > 64) return false;
        for (eng::u16 i = 0; i < packs; ++i) h.packs.push_back(r.string(96));
    }
    return r.ok();
}

}  // namespace

bool read_header(const std::filesystem::path& file, Header& header) {
#ifdef _WIN32
    std::FILE* f = _wfopen(file.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(file.c_str(), "rb");
#endif
    if (!f) return false;
    // The header and its MatchLoad fit in the first 64 KB.
    std::vector<u8> head(64 * 1024);
    head.resize(std::fread(head.data(), 1, head.size(), f));
    std::fclose(f);
    eng::ByteReader r(head);
    return parse_header(r, header);
}

bool read(std::span<const u8> file, Header& header, std::vector<Frame>& frames, bool& complete, std::string* error) {
    complete = false;
    frames.clear();
    eng::ByteReader r(file);
    if (!parse_header(r, header)) {
        if (error) *error = "Not a match recording.";
        return false;
    }
    std::vector<u8> raw;
    while (r.remaining() >= 8) {
        const u32 raw_size = r.u32(), packed_size = r.u32();
        if (raw_size == 0) {
            complete = true;
            break;
        }
        if (packed_size > r.remaining() || raw_size > (64u << 20)) break;   // cut short
        std::vector<u8> packed(packed_size);
        r.raw(packed.data(), packed_size);
        if (packed_size == raw_size) {
            raw = std::move(packed);
        } else {
            int got = 0;
            char* out = stbi_zlib_decode_malloc_guesssize(reinterpret_cast<const char*>(packed.data()), int(packed_size), int(raw_size), &got);
            if (!out || u32(got) != raw_size) {
                std::free(out);
                if (error) *error = "A block of the recording is damaged.";
                break;
            }
            raw.assign(reinterpret_cast<u8*>(out), reinterpret_cast<u8*>(out) + got);
            std::free(out);
        }
        size_t at = 0;
        while (at + 8 <= raw.size()) {
            Frame f;
            u32 n = 0;
            std::memcpy(&f.ms, &raw[at], 4);
            std::memcpy(&n, &raw[at + 4], 4);
            at += 8;
            if (at + n > raw.size()) break;
            f.bytes.assign(raw.begin() + std::ptrdiff_t(at), raw.begin() + std::ptrdiff_t(at + n));
            at += n;
            frames.push_back(std::move(f));
        }
    }
    return true;
}

}  // namespace lsf::replay
