// SPDX-License-Identifier: MIT
#include "sf1/data/msf.hpp"
#include <array>
#include <charconv>

namespace sf1::data::msf {

namespace {

constexpr std::string_view kCountTag = "[Mapping Source Count]";
constexpr std::array<std::string_view, 6> kExtensions{"jpg", "jpeg", "tga", "bmp", "png", "dds"};

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n'; }
bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

char lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool iequals_at(std::string_view s, std::size_t at, std::string_view word) noexcept {
    if (at + word.size() > s.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i)
        if (lower(s[at + i]) != word[i]) return false;
    return true;
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

// Whitespace, then optional digits, then the end of the line.
bool valid_tail(std::string_view tail, std::optional<std::uint32_t>& flag, bool& overflow) noexcept {
    std::size_t i = 0;
    while (i < tail.size() && is_space(tail[i])) ++i;
    std::size_t digits = i;
    while (digits < tail.size() && is_digit(tail[digits])) ++digits;
    if (digits != tail.size()) return false;
    flag.reset();
    overflow = false;
    if (digits > i) {
        std::uint32_t v = 0;
        auto [p, ec] = std::from_chars(tail.data() + i, tail.data() + digits, v);
        if (ec != std::errc{}) overflow = true;
        else flag = v;
    }
    return true;
}

// The shortest "<path>.<ext>" prefix whose remainder is "<spaces><digits>".
bool parse_line(std::string_view line, Mapping& out, bool& overflow) noexcept {
    for (std::size_t dot = 1; dot < line.size(); ++dot) {
        if (line[dot] != '.') continue;
        for (auto ext : kExtensions) {
            if (!iequals_at(line, dot + 1, ext)) continue;
            const std::size_t end = dot + 1 + ext.size();
            std::optional<std::uint32_t> flag;
            if (!valid_tail(line.substr(end), flag, overflow)) continue;
            out.path.assign(line.substr(0, end));
            out.native_flag = flag;
            return true;
        }
    }
    return false;
}

}  // namespace

Result<std::vector<Mapping>> read(std::string_view text) noexcept {
    auto tag = text.find(kCountTag);
    if (tag == std::string_view::npos) return err(Error::Malformed);
    std::size_t i = tag + kCountTag.size();
    while (i < text.size() && is_space(text[i])) ++i;
    if (i >= text.size() || text[i] != ':') return err(Error::Malformed);
    ++i;
    while (i < text.size() && is_space(text[i])) ++i;
    std::size_t digits = i;
    while (digits < text.size() && is_digit(text[digits])) ++digits;
    if (digits == i) return err(Error::Malformed);
    std::uint32_t declared = 0;
    if (auto [p, ec] = std::from_chars(text.data() + i, text.data() + digits, declared); ec != std::errc{})
        return err(Error::CountOutOfRange);
    if (declared > 100000) return err(Error::CountOutOfRange);

    std::vector<Mapping> out;
    out.reserve(declared);
    std::string_view rest = text.substr(digits);
    while (!rest.empty()) {
        auto eol = rest.find_first_of("\r\n");
        auto line = trim(rest.substr(0, eol));
        rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
        if (line.empty()) continue;
        if (out.size() == declared) break;
        Mapping m;
        bool overflow = false;
        if (!parse_line(line, m, overflow)) return err(Error::Malformed);
        if (overflow) return err(Error::CountOutOfRange);
        out.push_back(std::move(m));
    }
    if (out.size() != declared) return err(Error::Malformed);
    return out;
}

Result<std::vector<Mapping>> read(std::span<const std::byte> bytes) noexcept {
    return read(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}  // namespace sf1::data::msf
