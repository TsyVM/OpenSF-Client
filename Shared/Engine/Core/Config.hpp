// The engine's text data format: INI-like, sections may repeat.
//
//   # comment            // comment
//   top_level_key = value
//   [weapon]
//   id    = rifle_m4
//   name  = "M4 Carbine"     quotes are optional and stripped
//   [weapon]
//   id    = rifle_ak
//
// Keys are case-insensitive. Values keep their case. Order is preserved, so a file
// written back by serialize() reads the same.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng {

struct ConfigSection {
    std::string name;   // empty for keys above the first [section]
    std::vector<std::pair<std::string, std::string>> values;
    int line = 0;

    bool has(std::string_view key) const;
    std::string_view get(std::string_view key, std::string_view fallback = {}) const;
    std::string get_string(std::string_view key, std::string_view fallback = {}) const;
    int get_int(std::string_view key, int fallback = 0) const;
    float get_float(std::string_view key, float fallback = 0.0f) const;
    bool get_bool(std::string_view key, bool fallback = false) const;
    // Every value of a key that may repeat ("map = a", "map = b").
    std::vector<std::string_view> get_all(std::string_view key) const;

    void set(std::string_view key, std::string_view value);
};

struct ConfigFile {
    std::vector<ConfigSection> sections;

    static bool parse(std::string_view text, ConfigFile& out, std::string* error = nullptr);

    const ConfigSection* find(std::string_view name) const;
    std::vector<const ConfigSection*> all(std::string_view name) const;
    ConfigSection& section(std::string_view name);   // finds or appends
    const ConfigSection& root() const;               // the unnamed section, possibly empty

    std::string serialize() const;
};

}  // namespace eng
