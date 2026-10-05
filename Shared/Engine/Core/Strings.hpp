#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace eng::str {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

std::string lower(std::string_view s);
std::string upper(std::string_view s);
std::string_view trim(std::string_view s);
std::vector<std::string_view> split(std::string_view s, char separator, bool skip_empty = true);
bool iequals(std::string_view a, std::string_view b);
bool istarts_with(std::string_view s, std::string_view prefix);
bool iends_with(std::string_view s, std::string_view suffix);

std::string format(const char* fmt, ...);
// 34500 -> "34,500"
std::string thousands(unsigned long long v);

bool parse_int(std::string_view s, int& out);
bool parse_float(std::string_view s, float& out);
bool parse_bool(std::string_view s, bool& out);

// "Maps\\Foo\\Bar.TTEX" -> "maps/foo/bar.ttex"
std::string normalize_path(std::string_view path);
std::string_view file_name(std::string_view path);
std::string_view file_stem(std::string_view path);
std::string_view parent_path(std::string_view path);

// Removes everything but printable ASCII and trims; used for nicknames, room titles and chat.
std::string sanitize_line(std::string_view s, size_t max_length);

}  // namespace eng::str
