#include "Engine/Core/Config.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Types.hpp"

namespace eng {

bool ConfigSection::has(std::string_view key) const {
    for (const auto& kv : values)
        if (str::iequals(kv.first, key)) return true;
    return false;
}

std::string_view ConfigSection::get(std::string_view key, std::string_view fallback) const {
    for (const auto& kv : values)
        if (str::iequals(kv.first, key)) return kv.second;
    return fallback;
}

std::string ConfigSection::get_string(std::string_view key, std::string_view fallback) const {
    return std::string(get(key, fallback));
}

int ConfigSection::get_int(std::string_view key, int fallback) const {
    int v = fallback;
    if (has(key) && str::parse_int(get(key), v)) return v;
    return fallback;
}

float ConfigSection::get_float(std::string_view key, float fallback) const {
    float v = fallback;
    if (has(key) && str::parse_float(get(key), v)) return v;
    return fallback;
}

bool ConfigSection::get_bool(std::string_view key, bool fallback) const {
    bool v = fallback;
    if (has(key) && str::parse_bool(get(key), v)) return v;
    return fallback;
}

std::vector<std::string_view> ConfigSection::get_all(std::string_view key) const {
    std::vector<std::string_view> out;
    for (const auto& kv : values)
        if (str::iequals(kv.first, key)) out.push_back(kv.second);
    return out;
}

void ConfigSection::set(std::string_view key, std::string_view value) {
    for (auto& kv : values)
        if (str::iequals(kv.first, key)) {
            kv.second = std::string(value);
            return;
        }
    values.emplace_back(std::string(key), std::string(value));
}

bool ConfigFile::parse(std::string_view text, ConfigFile& out, std::string* error) {
    out.sections.clear();
    out.sections.push_back({});
    int line_no = 0;
    size_t start = 0;
    // Skip a UTF-8 byte order mark.
    if (text.size() >= 3 && u8(text[0]) == 0xEF && u8(text[1]) == 0xBB && u8(text[2]) == 0xBF) start = 3;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = str::trim(text.substr(start, end - start));
        start = end + 1;
        ++line_no;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line.starts_with("//")) continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close == std::string_view::npos) {
                if (error) *error = str::format("line %d: missing ']'", line_no);
                return false;
            }
            ConfigSection section;
            section.name = str::lower(str::trim(line.substr(1, close - 1)));
            section.line = line_no;
            out.sections.push_back(std::move(section));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            if (error) *error = str::format("line %d: expected 'key = value'", line_no);
            return false;
        }
        std::string_view key = str::trim(line.substr(0, eq));
        std::string_view value = str::trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        } else {
            // Trailing comments only on unquoted values.
            size_t hash = value.find(" #");
            if (hash != std::string_view::npos) value = str::trim(value.substr(0, hash));
            size_t slashes = value.find(" //");
            if (slashes != std::string_view::npos) value = str::trim(value.substr(0, slashes));
        }
        out.sections.back().values.emplace_back(str::lower(key), std::string(value));
    }
    return true;
}

const ConfigSection* ConfigFile::find(std::string_view name) const {
    for (const auto& s : sections)
        if (str::iequals(s.name, name)) return &s;
    return nullptr;
}

std::vector<const ConfigSection*> ConfigFile::all(std::string_view name) const {
    std::vector<const ConfigSection*> out;
    for (const auto& s : sections)
        if (str::iequals(s.name, name)) out.push_back(&s);
    return out;
}

ConfigSection& ConfigFile::section(std::string_view name) {
    for (auto& s : sections)
        if (str::iequals(s.name, name)) return s;
    if (sections.empty() && !name.empty()) sections.push_back({});
    ConfigSection s;
    s.name = str::lower(name);
    sections.push_back(std::move(s));
    return sections.back();
}

const ConfigSection& ConfigFile::root() const {
    static const ConfigSection empty;
    return (!sections.empty() && sections.front().name.empty()) ? sections.front() : empty;
}

std::string ConfigFile::serialize() const {
    std::string out;
    for (const auto& s : sections) {
        if (!s.name.empty()) {
            if (!out.empty()) out += "\r\n";
            out += "[" + s.name + "]\r\n";
        }
        for (const auto& kv : s.values) {
            bool quote = kv.second.find('#') != std::string::npos || kv.second.find("//") != std::string::npos ||
                         (!kv.second.empty() && (kv.second.front() == ' ' || kv.second.back() == ' '));
            out += kv.first + " = " + (quote ? "\"" + kv.second + "\"" : kv.second) + "\r\n";
        }
    }
    return out;
}

}  // namespace eng
