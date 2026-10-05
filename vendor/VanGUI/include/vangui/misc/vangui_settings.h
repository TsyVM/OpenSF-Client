// vangui_settings.h
// -----------------------------------------------------------------------------
// VanGUI utility — persistent settings + two-way binding. Header-only.
//
//   * VanSettings : a tiny typed key/value store with an INI-style load/save.
//   * Bindings    : register your own variables; pull() applies a loaded file to
//                   them, push() copies them back before save() — so settings
//                   round-trip with one call each way, no per-field plumbing.
//
//   VanSettings s;
//   s.bind("audio.volume", &cfg.volume);
//   s.bind("video.vsync",  &cfg.vsync);
//   if (s.load("app.ini")) s.pull();          // file -> cfg
//   ... user edits cfg via widgets ...
//   s.push(); s.save("app.ini");              // cfg -> file
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>     // VanVec4
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace VanGui {

class VanSettings
{
public:
    // ---- raw store ----
    void set(const std::string& key, const std::string& val)
    {
        for (auto& kv : kv_) if (kv.first == key) { kv.second = val; return; }
        kv_.push_back({ key, val });
    }
    const char* get(const std::string& key, const char* def = "") const
    {
        for (auto& kv : kv_) if (kv.first == key) return kv.second.c_str();
        return def;
    }
    [[nodiscard]] bool has(const std::string& key) const
    {
        for (auto& kv : kv_) if (kv.first == key) return true;
        return false;
    }

    // ---- typed helpers ----
    void set_int  (const std::string& k, int v)      { set(k, std::to_string(v)); }
    void set_float(const std::string& k, float v)    { char b[32]; std::snprintf(b, sizeof b, "%g", v); set(k, b); }
    void set_bool (const std::string& k, bool v)     { set(k, v ? "true" : "false"); }
    void set_color(const std::string& k, VanVec4 c)  { char b[64]; std::snprintf(b, sizeof b, "%g,%g,%g,%g", c.x, c.y, c.z, c.w); set(k, b); }

    int     get_int  (const std::string& k, int def = 0)      const { return has(k) ? std::atoi(get(k)) : def; }
    float   get_float(const std::string& k, float def = 0.f)  const { return has(k) ? (float)std::atof(get(k)) : def; }
    bool    get_bool (const std::string& k, bool def = false) const
    {
        if (!has(k)) return def;
        const char* v = get(k);
        return v[0]=='1' || v[0]=='t' || v[0]=='T' || v[0]=='y' || v[0]=='Y';
    }
    VanVec4 get_color(const std::string& k, VanVec4 def = VanVec4(0,0,0,1)) const
    {
        if (!has(k)) return def;
        VanVec4 c = def;
#if defined(_MSC_VER)
        sscanf_s(get(k), "%f,%f,%f,%f", &c.x, &c.y, &c.z, &c.w);
#else
        std::sscanf(get(k), "%f,%f,%f,%f", &c.x, &c.y, &c.z, &c.w);
#endif
        return c;
    }

    // ---- INI serialization ----
    // Lines: "key = value". '['section']' prefixes following keys as "section.key".
    // ';' and '#' begin comments.
    void parse(const char* text, size_t len)
    {
        kv_.clear();
        std::string section, line;
        auto flush = [&](std::string ln) {
            // trim
            size_t a = ln.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return;
            size_t b = ln.find_last_not_of(" \t\r\n");
            ln = ln.substr(a, b - a + 1);
            if (ln.empty() || ln[0] == ';' || ln[0] == '#') return;
            if (ln.front() == '[' && ln.back() == ']') { section = ln.substr(1, ln.size() - 2); return; }
            size_t eq = ln.find('=');
            if (eq == std::string::npos) return;
            std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
            auto trim = [](std::string& s){ size_t x = s.find_first_not_of(" \t"); size_t y = s.find_last_not_of(" \t");
                                            s = (x==std::string::npos) ? std::string() : s.substr(x, y - x + 1); };
            trim(k); trim(v);
            if (!section.empty()) k = section + "." + k;
            if (!k.empty()) set(k, v);
        };
        for (size_t i = 0; i < len; ++i) {
            if (text[i] == '\n') { flush(line); line.clear(); }
            else line.push_back(text[i]);
        }
        flush(line);
    }
    std::string dump() const
    {
        std::string out;
        for (auto& kv : kv_) { out += kv.first; out += " = "; out += kv.second; out += "\n"; }
        return out;
    }

    bool load(const char* path)
    {
        std::FILE* f = open_file_(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); return false; }
        std::string buf((size_t)n, '\0');
        size_t rd = std::fread(buf.data(), 1, (size_t)n, f); std::fclose(f);
        buf.resize(rd); parse(buf.c_str(), buf.size()); return true;
    }
    bool save(const char* path) const
    {
        std::FILE* f = open_file_(path, "wb");
        if (!f) return false;
        const std::string s = dump();
        std::fwrite(s.data(), 1, s.size(), f); std::fclose(f); return true;
    }

private:
    static std::FILE* open_file_(const char* path, const char* mode)
    {
#if defined(_MSC_VER)
        std::FILE* f = nullptr;
        fopen_s(&f, path, mode);
        return f;
#else
        return std::fopen(path, mode);
#endif
    }

    // ---- bindings ----
    void bind(const std::string& key, int* p)     { binds_.push_back({ key, Kind::I, p }); }
    void bind(const std::string& key, float* p)   { binds_.push_back({ key, Kind::F, p }); }
    void bind(const std::string& key, bool* p)    { binds_.push_back({ key, Kind::B, p }); }
    void bind(const std::string& key, VanVec4* p) { binds_.push_back({ key, Kind::C, p }); }

    void pull()   // store -> variables
    {
        for (auto& b : binds_) switch (b.kind) {
            case Kind::I: *(int*)b.ptr     = get_int(b.key,   *(int*)b.ptr);     break;
            case Kind::F: *(float*)b.ptr   = get_float(b.key, *(float*)b.ptr);   break;
            case Kind::B: *(bool*)b.ptr    = get_bool(b.key,  *(bool*)b.ptr);    break;
            case Kind::C: *(VanVec4*)b.ptr = get_color(b.key, *(VanVec4*)b.ptr); break;
        }
    }
    void push()   // variables -> store
    {
        for (auto& b : binds_) switch (b.kind) {
            case Kind::I: set_int(b.key,   *(int*)b.ptr);     break;
            case Kind::F: set_float(b.key, *(float*)b.ptr);   break;
            case Kind::B: set_bool(b.key,  *(bool*)b.ptr);    break;
            case Kind::C: set_color(b.key, *(VanVec4*)b.ptr); break;
        }
    }

private:
    enum class Kind { I, F, B, C };
    struct Bind { std::string key; Kind kind; void* ptr; };
    std::vector<std::pair<std::string, std::string>> kv_;
    std::vector<Bind> binds_;
};

} // namespace VanGui
