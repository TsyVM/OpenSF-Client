// vangui_i18n.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — localization-lite (header-only).
//
// A tiny string catalog: map keys to translated strings, look them up with
// Tr("key"). This is *not* full i18n (no RTL, bidi or text shaping — those stay
// out of scope to keep VanGUI lightweight); it is label swapping for UI strings.
// No macro gate — include and use.
//
//   VanGui::GlobalCatalog().Set("file.open", "Ouvrir");
//   Button(VanGui::Tr("file.open"));   // -> "Ouvrir", or "file.open" if missing
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include <string>
#include <utility>
#include <vector>

namespace VanGui {

class VanStringCatalog
{
public:
    void Set(const char* key, const char* value)
    {
        for (auto& kv : kv_) if (kv.first == key) { kv.second = value ? value : ""; return; }
        kv_.emplace_back(std::string(key), std::string(value ? value : ""));
    }

    // Returns the translation, or `key` itself when there is no entry.
    const char* Tr(const char* key) const
    {
        for (const auto& kv : kv_) if (kv.first == key) return kv.second.c_str();
        return key;
    }

    bool Has(const char* key) const
    {
        for (const auto& kv : kv_) if (kv.first == key) return true;
        return false;
    }

    void Clear() { kv_.clear(); }

    // Parse "key = value" lines (';' / '#' comments). Returns entries added.
    int LoadFromMemory(const char* text, size_t len)
    {
        int added = 0;
        std::string line;
        auto flush = [&](std::string ln) {
            size_t a = ln.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return;
            size_t b = ln.find_last_not_of(" \t\r\n");
            ln = ln.substr(a, b - a + 1);
            if (ln.empty() || ln[0] == ';' || ln[0] == '#') return;
            size_t eq = ln.find('=');
            if (eq == std::string::npos) return;
            std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
            auto trim = [](std::string& s) {
                size_t x = s.find_first_not_of(" \t");
                size_t y = s.find_last_not_of(" \t");
                s = (x == std::string::npos) ? std::string() : s.substr(x, y - x + 1);
            };
            trim(k); trim(v);
            if (!k.empty()) { Set(k.c_str(), v.c_str()); ++added; }
        };
        for (size_t i = 0; i < len; ++i) {
            if (text[i] == '\n') { flush(line); line.clear(); }
            else line.push_back(text[i]);
        }
        flush(line);
        return added;
    }

private:
    std::vector<std::pair<std::string, std::string>> kv_;
};

// Process-wide default catalog.
inline VanStringCatalog& GlobalCatalog()
{
    static VanStringCatalog c;
    return c;
}

inline const char* Tr(const char* key) { return GlobalCatalog().Tr(key); }

} // namespace VanGui
