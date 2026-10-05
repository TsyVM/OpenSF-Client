// SPDX-License-Identifier: MIT
#include "sf1/data/osf.hpp"

#include <cctype>
#include <charconv>

namespace sf1::data::osf {

namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool is_pad(char c) noexcept { return c == ' ' || c == '\t' || c == '\0'; }

// Where the value of `key` sits in `text`: [begin, end) with surrounding spaces and
// NULs excluded. False when the key is not there.
bool locate(std::string_view text, std::string_view key, std::size_t& begin, std::size_t& end) {
    std::size_t line = 0;
    while (line < text.size()) {
        std::size_t eol = text.find('\n', line);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view row = text.substr(line, eol - line);

        if (!row.empty() && row.front() == '[') {
            const auto close = row.find(']');
            if (close != std::string_view::npos && iequals(row.substr(1, close - 1), key)) {
                auto colon = row.find(':', close);
                if (colon == std::string_view::npos) return false;
                std::size_t b = colon + 1;
                std::size_t e = row.size();
                if (e > b && row[e - 1] == '\r') --e;
                while (b < e && is_pad(row[b])) ++b;
                while (e > b && is_pad(row[e - 1])) --e;
                begin = line + b;
                end = line + e;
                return true;
            }
        }
        line = eol + 1;
    }
    return false;
}

}  // namespace

std::optional<std::string> Document::value(std::string_view key) const {
    std::size_t b = 0, e = 0;
    if (!locate(text, key, b, e)) return std::nullopt;
    return text.substr(b, e - b);
}

bool Document::set_value(std::string_view key, std::string_view new_value) {
    std::size_t b = 0, e = 0;
    if (!locate(text, key, b, e)) return false;
    text.replace(b, e - b, new_value);
    return true;
}

std::vector<std::string> Document::textures() const {
    std::vector<std::string> out;
    const auto count_text = value("Mapping Source Count");
    if (!count_text) return out;
    int count = 0;
    std::from_chars(count_text->data(), count_text->data() + count_text->size(), count);

    std::size_t b = 0, e = 0;
    if (!locate(text, "Mapping Source Count", b, e)) return out;
    std::size_t line = text.find('\n', e);
    while (line != std::string::npos && static_cast<int>(out.size()) < count) {
        ++line;
        std::size_t eol = text.find('\n', line);
        std::string_view row = std::string_view(text).substr(line, (eol == std::string::npos ? text.size() : eol) - line);
        while (!row.empty() && (row.back() == '\r' || is_pad(row.back()))) row.remove_suffix(1);
        while (!row.empty() && is_pad(row.front())) row.remove_prefix(1);
        if (!row.empty() && row.front() == '[') break;
        if (!row.empty()) out.emplace_back(row);
        line = eol;
    }
    return out;
}

std::optional<bool> Document::collision() const {
    const auto v = value("Crash_Data");
    if (!v || v->empty()) return std::nullopt;
    return (*v)[0] != '0';
}

Result<Document> read(std::span<const std::byte> bytes) {
    Document d;
    d.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::size_t b = 0, e = 0;
    if (!locate(d.text, "Object Data Name", b, e)) return err(Error::Malformed);
    return d;
}

}  // namespace sf1::data::osf
