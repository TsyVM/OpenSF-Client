// Little-endian byte writer/reader used by file formats and the network protocol.
// The reader never throws: running past the end sets a sticky failure flag and
// returns zeros, so callers check ok() once after reading a whole record.
#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Types.hpp"

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class ByteWriter {
public:
    ByteWriter() = default;
    explicit ByteWriter(size_t reserve) { data_.reserve(reserve); }

    void u8(eng::u8 v) { data_.push_back(v); }
    void u16(eng::u16 v) { raw(&v, 2); }
    void u32(eng::u32 v) { raw(&v, 4); }
    void u64(eng::u64 v) { raw(&v, 8); }
    void i8(eng::i8 v) { raw(&v, 1); }
    void i16(eng::i16 v) { raw(&v, 2); }
    void i32(eng::i32 v) { raw(&v, 4); }
    void f32(eng::f32 v) { raw(&v, 4); }
    void boolean(bool v) { u8(v ? 1 : 0); }

    void varu(eng::u64 v) {
        while (v >= 0x80) {
            u8(eng::u8(v | 0x80));
            v >>= 7;
        }
        u8(eng::u8(v));
    }
    void vari(eng::i64 v) { varu((eng::u64(v) << 1) ^ eng::u64(v >> 63)); }

    void vec3(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    // Angle in degrees packed to 16 bits.
    void angle16(float deg) { u16(eng::u16(int(std::lround(wrap_degrees(deg) / 360.0f * 65536.0f)) & 0xFFFF)); }

    void string(std::string_view s) {
        varu(s.size());
        raw(s.data(), s.size());
    }
    void fixed_string(std::string_view s, size_t width) {
        size_t n = std::min(s.size(), width);
        raw(s.data(), n);
        for (size_t i = n; i < width; ++i) u8(0);
    }

    void raw(const void* p, size_t n) {
        size_t at = data_.size();
        data_.resize(at + n);
        if (n) std::memcpy(data_.data() + at, p, n);
    }
    void bytes(std::span<const eng::u8> b) { raw(b.data(), b.size()); }

    // Overwrites a value previously written at `offset`.
    void patch_u32(size_t offset, eng::u32 v) { std::memcpy(data_.data() + offset, &v, 4); }
    void pad_to(size_t alignment) {
        while (data_.size() % alignment) u8(0);
    }

    size_t size() const { return data_.size(); }
    const std::vector<eng::u8>& data() const { return data_; }
    std::vector<eng::u8>& data() { return data_; }
    std::vector<eng::u8> take() { return std::move(data_); }
    void clear() { data_.clear(); }

private:
    std::vector<eng::u8> data_;
};

class ByteReader {
public:
    ByteReader() = default;
    ByteReader(const eng::u8* p, size_t n) : p_(p), n_(n) {}
    explicit ByteReader(std::span<const eng::u8> s) : p_(s.data()), n_(s.size()) {}

    eng::u8 u8() { eng::u8 v = 0; raw(&v, 1); return v; }
    eng::u16 u16() { eng::u16 v = 0; raw(&v, 2); return v; }
    eng::u32 u32() { eng::u32 v = 0; raw(&v, 4); return v; }
    eng::u64 u64() { eng::u64 v = 0; raw(&v, 8); return v; }
    eng::i8 i8() { eng::i8 v = 0; raw(&v, 1); return v; }
    eng::i16 i16() { eng::i16 v = 0; raw(&v, 2); return v; }
    eng::i32 i32() { eng::i32 v = 0; raw(&v, 4); return v; }
    eng::f32 f32() { eng::f32 v = 0; raw(&v, 4); return v; }
    bool boolean() { return u8() != 0; }

    eng::u64 varu() {
        eng::u64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            eng::u8 b = u8();
            v |= eng::u64(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        fail_ = true;
        return 0;
    }
    eng::i64 vari() {
        eng::u64 v = varu();
        return eng::i64(v >> 1) ^ -eng::i64(v & 1);
    }

    Vec3 vec3() { float x = f32(), y = f32(), z = f32(); return {x, y, z}; }
    float angle16() { return float(this->u16()) / 65536.0f * 360.0f; }

    std::string string(size_t max_length = 65536) {
        eng::u64 n = varu();
        if (n > max_length || n > remaining()) {
            fail_ = true;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_ + at_), size_t(n));
        at_ += size_t(n);
        return s;
    }
    std::string fixed_string(size_t width) {
        if (width > remaining()) {
            fail_ = true;
            at_ = n_;
            return {};
        }
        const char* s = reinterpret_cast<const char*>(p_ + at_);
        size_t len = 0;
        while (len < width && s[len]) ++len;
        at_ += width;
        return std::string(s, len);
    }

    bool raw(void* out, size_t n) {
        if (n > remaining()) {
            fail_ = true;
            std::memset(out, 0, n);
            at_ = n_;
            return false;
        }
        if (n) std::memcpy(out, p_ + at_, n);
        at_ += n;
        return true;
    }
    std::span<const eng::u8> view(size_t n) {
        if (n > remaining()) {
            fail_ = true;
            at_ = n_;
            return {};
        }
        std::span<const eng::u8> s(p_ + at_, n);
        at_ += n;
        return s;
    }
    // Reads `count` plain records straight into a vector (T must be trivially copyable).
    template <typename T>
    bool array(std::vector<T>& out, size_t count) {
        if (count > remaining() / sizeof(T)) {
            fail_ = true;
            return false;
        }
        out.resize(count);
        return raw(out.data(), count * sizeof(T));
    }

    void skip(size_t n) {
        if (n > remaining()) {
            fail_ = true;
            at_ = n_;
        } else {
            at_ += n;
        }
    }
    void seek(size_t offset) {
        if (offset > n_) {
            fail_ = true;
            at_ = n_;
        } else {
            at_ = offset;
        }
    }

    size_t tell() const { return at_; }
    size_t size() const { return n_; }
    size_t remaining() const { return n_ - at_; }
    bool ok() const { return !fail_; }
    bool at_end() const { return at_ >= n_; }
    const eng::u8* data() const { return p_; }

private:
    const eng::u8* p_ = nullptr;
    size_t n_ = 0;
    size_t at_ = 0;
    bool fail_ = false;
};

}  // namespace eng
