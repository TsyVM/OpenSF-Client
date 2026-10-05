// van_kit.h - amalgamated header-only VanGUI utilities (generated).
// Regenerate with: python3 tools/amalgamate.py
// NOTE: compiled modules (anim/loading/dialogs/charts/...) still need their .cpp.
#pragma once
#include "vangui.h"
#include "misc/vangui_theme_engine.h"  // for theme_gen token types
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ===== misc/vangui_signals.h =============================================
// vangui_signals.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: signals / slots (header-only).
//
// The one intentional retained-state concession in the suite — and it is fully
// opt-in, instance-based (no global mutable state), and composition-friendly. It
// bridges immediate-mode widget results to decoupled application logic the way
// Qt's signals do.
//
//   * Opt-in: define VANGUI_ENABLE_SIGNALS to make the types available. Without
//     it the header declares nothing, so accidental use fails to compile (a
//     compile-time opt-in gate, per the Constitution).
//   * Header-only: no .cpp, no link dependency, no core hook.
//   * RAII lifetime: VanConnection is move-only and disconnects in its
//     destructor — no dangling slots. Slots live in a shared control block, so a
//     connection that outlives its signal disconnects harmlessly.
//   * SBO: slot storage uses std::function, which small-buffer-optimizes the
//     common 0–2 capture case, avoiding per-connection heap churn.
// -----------------------------------------------------------------------------


#ifdef VANGUI_ENABLE_SIGNALS


namespace VanGui {

// Move-only RAII handle. Disconnects its slot when destroyed (or on disconnect()).
class VanConnection
{
public:
    VanConnection() = default;
    explicit VanConnection(std::function<void()> disconnector)
        : disc_(std::move(disconnector)), connected_(true) {}

    VanConnection(VanConnection&& o) noexcept { steal(std::move(o)); }
    VanConnection& operator=(VanConnection&& o) noexcept
    {
        if (this != &o) { disconnect(); steal(std::move(o)); }
        return *this;
    }
    VanConnection(const VanConnection&) = delete;
    VanConnection& operator=(const VanConnection&) = delete;
    ~VanConnection() { disconnect(); }

    void disconnect() noexcept
    {
        if (connected_ && disc_) disc_();
        connected_ = false;
        disc_ = nullptr;
    }
    // Keep the slot connected for the lifetime of the signal; drop this handle.
    void release() noexcept { connected_ = false; disc_ = nullptr; }

    [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
    void steal(VanConnection&& o) noexcept
    {
        disc_ = std::move(o.disc_);
        connected_ = o.connected_;
        o.connected_ = false;
        o.disc_ = nullptr;
    }
    std::function<void()> disc_;
    bool                  connected_ = false;
};

template <class... Args>
class VanSignal
{
public:
    using SlotFn = std::function<void(Args...)>;

    VanSignal() : impl_(std::make_shared<Impl>()) {}
    VanSignal(const VanSignal&) = delete;            // signals are unique endpoints
    VanSignal& operator=(const VanSignal&) = delete;
    VanSignal(VanSignal&&) noexcept = default;
    VanSignal& operator=(VanSignal&&) noexcept = default;

    // Connect a slot; returns a RAII handle that auto-disconnects.
    [[nodiscard]] VanConnection connect(SlotFn slot)
    {
        const std::uint64_t id = impl_->next++;
        impl_->slots.push_back(Slot{ id, std::move(slot), true });
        std::weak_ptr<Impl> w = impl_;
        return VanConnection([w, id]() noexcept {
            if (auto s = w.lock())
                for (auto& sl : s->slots)
                    if (sl.id == id) { sl.live = false; break; }
        });
    }

    // Call every live slot in connection order.
    void emit(Args... args) const
    {
        auto& slots = impl_->slots;
        // Index-based: tolerant of slots being disconnected during emission.
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].live)
                slots[i].fn(args...);
        compact();
    }

    void operator()(Args... args) const { emit(args...); }

    void disconnect_all() noexcept { impl_->slots.clear(); }

    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        std::size_t n = 0;
        for (const auto& s : impl_->slots) if (s.live) ++n;
        return n;
    }

private:
    struct Slot { std::uint64_t id; SlotFn fn; bool live; };
    struct Impl { std::vector<Slot> slots; std::uint64_t next = 1; };

    void compact() const
    {
        auto& v = impl_->slots;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const Slot& s) { return !s.live; }),
                v.end());
    }

    std::shared_ptr<Impl> impl_;
};

} // namespace VanGui


#endif // VANGUI_ENABLE_SIGNALS

// ===== misc/vangui_undo.h ================================================
// vangui_undo.h
// -----------------------------------------------------------------------------
// VanGUI utility — undo / redo. Header-only, no VanGUI dependency, just include.
//
// Two flavors:
//   * VanUndoStack<T> : snapshot-based. Record a copy of your state before a
//                       change; undo()/redo() swap it back. Best for small
//                       value-type documents (settings structs, small models).
//   * VanCommandStack : command-based. Pair a do/undo lambda per action; best
//                       for large models where copying the whole state is costly.
// -----------------------------------------------------------------------------



namespace VanGui {

// ----- snapshot-based --------------------------------------------------------
template <class T>
class VanUndoStack
{
public:
    explicit VanUndoStack(std::size_t capacity = 128) : cap_(capacity ? capacity : 1) {}

    // Call BEFORE mutating `current` (records the pre-change state).
    void record(const T& current)
    {
        past_.push_back(current);
        if (past_.size() > cap_) past_.erase(past_.begin());
        future_.clear();
    }

    bool undo(T& current)
    {
        if (past_.empty()) return false;
        future_.push_back(current);
        current = std::move(past_.back());
        past_.pop_back();
        return true;
    }
    bool redo(T& current)
    {
        if (future_.empty()) return false;
        past_.push_back(current);
        current = std::move(future_.back());
        future_.pop_back();
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !future_.empty(); }
    [[nodiscard]] std::size_t undo_count() const noexcept { return past_.size(); }
    [[nodiscard]] std::size_t redo_count() const noexcept { return future_.size(); }
    void clear() { past_.clear(); future_.clear(); }

private:
    std::vector<T> past_, future_;
    std::size_t    cap_;
};

// ----- command-based ---------------------------------------------------------
class VanCommandStack
{
public:
    explicit VanCommandStack(std::size_t capacity = 256) : cap_(capacity ? capacity : 1) {}

    // Execute `do_fn` now and remember how to undo/redo it.
    void perform(std::function<void()> do_fn, std::function<void()> undo_fn)
    {
        if (do_fn) do_fn();
        past_.push_back(Cmd{ std::move(undo_fn), std::move(do_fn) });
        if (past_.size() > cap_) past_.erase(past_.begin());
        future_.clear();
    }

    bool undo()
    {
        if (past_.empty()) return false;
        Cmd c = std::move(past_.back());
        past_.pop_back();
        if (c.undo) c.undo();
        future_.push_back(std::move(c));
        return true;
    }
    bool redo()
    {
        if (future_.empty()) return false;
        Cmd c = std::move(future_.back());
        future_.pop_back();
        if (c.redo) c.redo();
        past_.push_back(std::move(c));
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !past_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !future_.empty(); }
    void clear() { past_.clear(); future_.clear(); }

private:
    struct Cmd { std::function<void()> undo, redo; };
    std::vector<Cmd> past_, future_;
    std::size_t      cap_;
};

} // namespace VanGui

// ===== misc/vangui_settings.h ============================================
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
        std::sscanf(get(k), "%f,%f,%f,%f", &c.x, &c.y, &c.z, &c.w);
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
        std::FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        if (n < 0) { std::fclose(f); return false; }
        std::string buf((size_t)n, '\0');
        size_t rd = std::fread(buf.data(), 1, (size_t)n, f); std::fclose(f);
        buf.resize(rd); parse(buf.c_str(), buf.size()); return true;
    }
    bool save(const char* path) const
    {
        std::FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        const std::string s = dump();
        std::fwrite(s.data(), 1, s.size(), f); std::fclose(f); return true;
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

// ===== misc/vangui_theme_gen.h ===========================================
// vangui_theme_gen.h
// -----------------------------------------------------------------------------
// VanGUI utility — generate a full semantic theme from a single accent color.
// Header-only. Pure color math (HSL/luminance); no VanGUI runtime calls, so it
// is unit-testable in isolation. Pairs with vangui_theme_engine (it returns a
// VanThemeTokenSet you can ApplyTokenSet/TransitionToTokenSet).
//
//   VanThemeTokenSet t = VanGui::GenerateTheme(VanVec4(0.26f,0.59f,0.98f,1), /*dark=*/true);
//   ApplyTokenSet(t);
// -----------------------------------------------------------------------------



namespace VanGui {
namespace detail {

inline float VanClamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline void RgbToHsl(VanVec4 c, float& h, float& s, float& l)
{
    const float r = c.x, g = c.y, b = c.z;
    const float mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
    const float mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
    l = (mx + mn) * 0.5f;
    const float d = mx - mn;
    if (d < 1e-6f) { h = 0.f; s = 0.f; return; }
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r)      h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else              h = (r - g) / d + 4.f;
    h /= 6.f;
}

inline float Hue2Rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f / 2.f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

inline VanVec4 HslToRgb(float h, float s, float l, float a)
{
    if (s < 1e-6f) return VanVec4(l, l, l, a);
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    return VanVec4(Hue2Rgb(p, q, h + 1.f / 3.f), Hue2Rgb(p, q, h), Hue2Rgb(p, q, h - 1.f / 3.f), a);
}

// Return a copy of c with lightness set to `l` (keeps hue/sat).
inline VanVec4 WithL(VanVec4 c, float l)
{
    float h, s, ol; RgbToHsl(c, h, s, ol);
    return HslToRgb(h, s, VanClamp01(l), c.w);
}
inline VanVec4 Mix(VanVec4 a, VanVec4 b, float t)
{
    return VanVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

} // namespace detail

// Build a coherent token set from one accent. `dark` chooses a dark or light base.
inline VanThemeTokenSet GenerateTheme(VanVec4 accent, bool dark = true)
{
    using namespace detail;
    float h, s, l; RgbToHsl(accent, h, s, l);

    VanThemeTokenSet t{};
    const VanVec4 ink   = dark ? VanVec4(0.92f, 0.93f, 0.95f, 1.f) : VanVec4(0.10f, 0.11f, 0.13f, 1.f);
    const float   baseL = dark ? 0.10f : 0.96f;
    const float   surfL = dark ? 0.14f : 0.99f;

    // Neutral base tinted slightly toward the accent hue for cohesion.
    const VanVec4 tint  = HslToRgb(h, 0.18f, baseL, 1.f);
    const VanVec4 tintS = HslToRgb(h, 0.14f, surfL, 1.f);

    t.Colors[VanThemeToken_Background] = tint;
    t.Colors[VanThemeToken_Surface]    = tintS;
    t.Colors[VanThemeToken_Border]     = HslToRgb(h, 0.16f, dark ? 0.28f : 0.80f, 1.f);
    t.Colors[VanThemeToken_Primary]    = accent;
    t.Colors[VanThemeToken_Secondary]  = WithL(accent, dark ? l + 0.12f : l - 0.10f);
    t.Colors[VanThemeToken_TextPrimary]= ink;
    t.Colors[VanThemeToken_TextDim]    = Mix(ink, tint, 0.55f);
    t.Colors[VanThemeToken_Error]      = VanVec4(0.91f, 0.30f, 0.24f, 1.f);
    t.Colors[VanThemeToken_Warning]    = VanVec4(0.98f, 0.74f, 0.02f, 1.f);
    t.Colors[VanThemeToken_Success]    = VanVec4(0.20f, 0.66f, 0.33f, 1.f);
    t.Colors[VanThemeToken_Info]       = WithL(accent, 0.55f);
    t.Colors[VanThemeToken_Overlay]    = VanVec4(0.f, 0.f, 0.f, dark ? 0.55f : 0.35f);
    return t;
}

} // namespace VanGui

// ===== misc/vangui_reflect.h =============================================
// vangui_reflect.h
// -----------------------------------------------------------------------------
// VanGUI utility — reflection + auto-inspector. Header-only, no RTTI.
//
// Describe a plain struct's fields once; van::Inspect() then generates a full
// editing UI from the field TYPES (float -> slider/drag, bool -> checkbox, int
// -> drag, VanVec4 -> color picker, std::string -> text). Qt's property editor,
// but driven by a tiny hand-written describe function (or the VAN_REFLECT sugar).
//
//   struct Settings { float volume=50; bool vsync=true; int quality=1; VanVec4 accent; };
//
//   inline void van_describe(Settings& s, van::Reflector& r) {
//       r.field("Volume",  s.volume).range(0, 100).suffix("%");
//       r.field("V-Sync",  s.vsync);
//       r.field("Quality", s.quality).range(0, 3);
//       r.field("Accent",  s.accent);
//   }
//
//   if (van::Inspect("Settings", settings)) { /* something changed this frame */ }
//
// `van_describe` is found by ADL — define it in the struct's namespace. The
// VAN_REFLECT_* macros below generate a simple one for you.
// -----------------------------------------------------------------------------



#if __has_include("misc/cpp/vangui_stdlib.h")
#  include "misc/cpp/vangui_stdlib.h"
#  define VAN_REFLECT_HAS_STRING 1
#endif

namespace van {

class Reflector;

namespace detail {
// Each field proxy draws its widget in its destructor, so chained metadata
// (.range/.tooltip/.readonly) is applied first. Movable, draws once.
struct FieldBase
{
    Reflector*  r;
    const char* label;
    const char* tip = nullptr;
    bool        readonly = false;
    bool        drawn = false;
    FieldBase(Reflector* rr, const char* l) : r(rr), label(l) {}
    FieldBase(FieldBase&& o) noexcept { *this = (FieldBase&&)o; }
    FieldBase& operator=(FieldBase&& o) noexcept
    { r=o.r; label=o.label; tip=o.tip; readonly=o.readonly; drawn=o.drawn; o.drawn=true; return *this; }
    FieldBase(const FieldBase&) = delete;
};
} // namespace detail

class Reflector
{
public:
    bool changed = false;

    struct FieldF : detail::FieldBase {
        float* v; float mn=0, mx=0, spd=1.f; const char* fmt="%.3f";
        FieldF(Reflector* r, const char* l, float* p) : FieldBase(r,l), v(p) {}
        FieldF&& range(float a, float b)  && { mn=a; mx=b; return (FieldF&&)*this; }
        FieldF&& speed(float s)           && { spd=s; return (FieldF&&)*this; }
        FieldF&& suffix(const char* s)    && { static char b[24]; std::snprintf(b,sizeof b,"%%.2f%s",s); fmt=b; return (FieldF&&)*this; }
        FieldF&& tooltip(const char* t)   && { tip=t; return (FieldF&&)*this; }
        FieldF&& readonly_()              && { readonly=true; return (FieldF&&)*this; }
        ~FieldF();
    };
    struct FieldI : detail::FieldBase {
        int* v; int mn=0, mx=0; float spd=1.f;
        FieldI(Reflector* r, const char* l, int* p) : FieldBase(r,l), v(p) {}
        FieldI&& range(int a, int b) && { mn=a; mx=b; return (FieldI&&)*this; }
        FieldI&& tooltip(const char* t) && { tip=t; return (FieldI&&)*this; }
        FieldI&& readonly_() && { readonly=true; return (FieldI&&)*this; }
        ~FieldI();
    };
    struct FieldB : detail::FieldBase {
        bool* v;
        FieldB(Reflector* r, const char* l, bool* p) : FieldBase(r,l), v(p) {}
        FieldB&& tooltip(const char* t) && { tip=t; return (FieldB&&)*this; }
        FieldB&& readonly_() && { readonly=true; return (FieldB&&)*this; }
        ~FieldB();
    };
    struct FieldC : detail::FieldBase {
        VanVec4* v;
        FieldC(Reflector* r, const char* l, VanVec4* p) : FieldBase(r,l), v(p) {}
        FieldC&& tooltip(const char* t) && { tip=t; return (FieldC&&)*this; }
        FieldC&& readonly_() && { readonly=true; return (FieldC&&)*this; }
        ~FieldC();
    };

    FieldF field(const char* label, float& v)   { return FieldF(this, label, &v); }
    FieldI field(const char* label, int& v)     { return FieldI(this, label, &v); }
    FieldB field(const char* label, bool& v)    { return FieldB(this, label, &v); }
    FieldC field(const char* label, VanVec4& v) { return FieldC(this, label, &v); }

#ifdef VAN_REFLECT_HAS_STRING
    struct FieldS : detail::FieldBase {
        std::string* v;
        FieldS(Reflector* r, const char* l, std::string* p) : FieldBase(r,l), v(p) {}
        FieldS&& tooltip(const char* t) && { tip=t; return (FieldS&&)*this; }
        FieldS&& readonly_() && { readonly=true; return (FieldS&&)*this; }
        ~FieldS();
    };
    FieldS field(const char* label, std::string& v) { return FieldS(this, label, &v); }
#endif

    void separator(const char* heading = nullptr)
    { if (heading) VanGui::SeparatorText(heading); else VanGui::Separator(); }
};

namespace detail {
inline void begin_field(const FieldBase& f) { if (f.readonly) VanGui::BeginDisabled(); }
inline void end_field(const FieldBase& f, bool ch)
{
    if (f.tip && VanGui::IsItemHovered()) VanGui::SetTooltip("%s", f.tip);
    if (f.readonly) VanGui::EndDisabled();
    if (ch) f.r->changed = true;
}
} // namespace detail

inline Reflector::FieldF::~FieldF()
{
    if (drawn) return;
    drawn = true;
    detail::begin_field(*this);
    bool ch = (mx > mn) ? VanGui::SliderFloat(label, v, mn, mx, fmt)
                        : VanGui::DragFloat(label, v, spd);
    detail::end_field(*this, ch);
}
inline Reflector::FieldI::~FieldI()
{
    if (drawn) return;
    drawn = true;
    detail::begin_field(*this);
    bool ch = (mx > mn) ? VanGui::SliderInt(label, v, mn, mx)
                        : VanGui::DragInt(label, v, spd);
    detail::end_field(*this, ch);
}
inline Reflector::FieldB::~FieldB()
{
    if (drawn) return;
    drawn = true;
    detail::begin_field(*this);
    bool ch = VanGui::Checkbox(label, v);
    detail::end_field(*this, ch);
}
inline Reflector::FieldC::~FieldC()
{
    if (drawn) return;
    drawn = true;
    detail::begin_field(*this);
    bool ch = VanGui::ColorEdit4(label, &v->x);
    detail::end_field(*this, ch);
}
#ifdef VAN_REFLECT_HAS_STRING
inline Reflector::FieldS::~FieldS()
{
    if (drawn) return;
    drawn = true;
    detail::begin_field(*this);
    bool ch = VanGui::InputText(label, v);
    detail::end_field(*this, ch);
}
#endif

// Build an editor for `obj`. Returns true if any field changed this frame.
// Requires a `void van_describe(T&, van::Reflector&)` reachable by ADL.
template <class T>
inline bool Inspect(const char* title, T& obj)
{
    Reflector r;
    if (title && *title) VanGui::SeparatorText(title);
    van_describe(obj, r);   // ADL
    return r.changed;
}

} // namespace van

// --- optional boilerplate sugar ---------------------------------------------
// Generates a van_describe with one r.field() per listed member (no metadata;
// for ranges/tooltips write the describe function by hand).
#define VAN_REFLECT_BEGIN(T) inline void van_describe(T& o, van::Reflector& r) {
#define VAN_FIELD(member, label) r.field(label, o.member);
#define VAN_GROUP(label) r.separator(label);
#define VAN_REFLECT_END() }

