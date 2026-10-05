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

#pragma once

#include "vangui_theme_engine.h"   // VanThemeToken, VanThemeTokenSet, VanVec4
#include <cmath>

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
