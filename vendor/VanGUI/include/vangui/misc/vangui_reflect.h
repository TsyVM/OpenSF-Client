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

#pragma once

#include <vangui/vangui.h>

#if __has_include("misc/cpp/vangui_stdlib.h")
#  include "misc/cpp/vangui_stdlib.h"
#  include <string>
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
