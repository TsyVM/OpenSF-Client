// SPDX-License-Identifier: MIT
// src/data/binary_reader.hpp — private bounds-checked cursor shared by the format readers.
//
// Every read checks the remaining length first and reports Error::ShortRead rather
// than reading past the span. Counts are checked against a caller-supplied ceiling,
// and the bytes they imply are checked against the span, before anything is
// allocated from them.
#pragma once

#include "sf1/result.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace sf1::data::detail {

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }
    [[nodiscard]] bool at_end() const noexcept { return pos_ == bytes_.size(); }

    [[nodiscard]] bool can(std::size_t n) const noexcept { return n <= remaining(); }

    [[nodiscard]] Result<std::span<const std::byte>> take(std::size_t n) noexcept {
        if (!can(n)) return err(Error::ShortRead);
        auto out = bytes_.subspan(pos_, n);
        pos_ += n;
        return out;
    }

    // `count` elements of `width` bytes each, with the multiplication checked.
    [[nodiscard]] Result<std::span<const std::byte>> take_array(std::size_t count, std::size_t width) noexcept {
        if (width != 0 && count > remaining() / width) return err(Error::ShortRead);
        return take(count * width);
    }

    [[nodiscard]] Result<std::uint8_t> u8() noexcept {
        if (!can(1)) return err(Error::ShortRead);
        return static_cast<std::uint8_t>(bytes_[pos_++]);
    }

    [[nodiscard]] Result<std::uint32_t> u32() noexcept {
        if (!can(4)) return err(Error::ShortRead);
        std::uint32_t v = 0;
        std::memcpy(&v, bytes_.data() + pos_, 4);
        pos_ += 4;
        return v;
    }

    // A u32 count that must not exceed `cap`.
    [[nodiscard]] Result<std::uint32_t> count(std::uint32_t cap) noexcept {
        auto n = u32();
        if (!n) return err(n.error());
        if (*n > cap) return err(Error::CountOutOfRange);
        return *n;
    }

    // A fixed-width, NUL-terminated name slot. Bytes after the NUL are padding.
    [[nodiscard]] Result<std::string> name(std::size_t width) noexcept {
        auto slot = take(width);
        if (!slot) return err(slot.error());
        return fixed_string(*slot);
    }

    template <std::size_t N>
    [[nodiscard]] Result<std::array<std::byte, N>> fixed() noexcept {
        auto s = take(N);
        if (!s) return err(s.error());
        std::array<std::byte, N> out{};
        std::memcpy(out.data(), s->data(), N);
        return out;
    }

    [[nodiscard]] static std::string fixed_string(std::span<const std::byte> slot) {
        const char* p = reinterpret_cast<const char*>(slot.data());
        std::size_t len = 0;
        while (len < slot.size() && p[len] != '\0') ++len;
        return std::string(p, len);
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t pos_ = 0;
};

// Copy `bytes` (a whole number of T) into a vector of trivially-copyable T.
template <class T>
[[nodiscard]] std::vector<T> copy_pod(std::span<const std::byte> bytes) {
    std::vector<T> out(bytes.size() / sizeof(T));
    if (!out.empty()) std::memcpy(out.data(), bytes.data(), out.size() * sizeof(T));
    return out;
}

[[nodiscard]] inline bool all_finite(const float* v, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

}  // namespace sf1::data::detail
