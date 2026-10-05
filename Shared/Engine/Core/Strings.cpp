#include "Engine/Core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace eng::str {

#ifndef _WIN32
// UTF-8 to and from wchar_t, which is UTF-32 here (Android, Linux).
std::wstring widen(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0) cp = c & 0x07, n = 4;
        else if (c >= 0xE0) cp = c & 0x0F, n = 3;
        else if (c >= 0xC0) cp = c & 0x1F, n = 2;
        if (i + n > s.size()) n = 1, cp = 0xFFFD;
        for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(wchar_t(cp));
        i += n;
    }
    return out;
}

std::string narrow(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        const char32_t cp = char32_t(wc);
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
#else
std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}
#endif

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return out;
}

std::string_view trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n' || s[b] == '\0')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == '\0')) --e;
    return s.substr(b, e - b);
}

std::vector<std::string_view> split(std::string_view s, char separator, bool skip_empty) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(separator, start);
        if (end == std::string_view::npos) end = s.size();
        std::string_view piece = trim(s.substr(start, end - start));
        if (!piece.empty() || !skip_empty) out.push_back(piece);
        start = end + 1;
    }
    return out;
}

static char lower_char(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower_char(a[i]) != lower_char(b[i])) return false;
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

std::string thousands(unsigned long long v) {
    std::string s = std::to_string(v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

std::string format(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (n > 0) {
        out.resize(size_t(n) + 1);
        std::vsnprintf(out.data(), out.size(), fmt, args);
        out.resize(size_t(n));
    }
    va_end(args);
    return out;
}

bool parse_int(std::string_view s, int& out) {
    std::string tmp(trim(s));
    if (tmp.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(tmp.c_str(), &end, 0);
    if (end == tmp.c_str() || *end != '\0') return false;
    out = int(v);
    return true;
}

bool parse_float(std::string_view s, float& out) {
    std::string tmp(trim(s));
    if (tmp.empty()) return false;
    char* end = nullptr;
    float v = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

bool parse_bool(std::string_view s, bool& out) {
    std::string_view t = trim(s);
    if (iequals(t, "1") || iequals(t, "true") || iequals(t, "yes") || iequals(t, "on")) {
        out = true;
        return true;
    }
    if (iequals(t, "0") || iequals(t, "false") || iequals(t, "no") || iequals(t, "off")) {
        out = false;
        return true;
    }
    return false;
}

std::string normalize_path(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(lower_char(c));
    }
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    return out;
}

std::string_view file_name(std::string_view path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string_view file_stem(std::string_view path) {
    std::string_view name = file_name(path);
    size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

std::string_view parent_path(std::string_view path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

std::string sanitize_line(std::string_view s, size_t max_length) {
    std::string out;
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 32 && u < 127) out.push_back(c);
        if (out.size() >= max_length) break;
    }
    return std::string(trim(out));
}

}  // namespace eng::str
