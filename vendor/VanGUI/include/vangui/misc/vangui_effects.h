// vangui_effects.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — draw-list effects.
//
//   * Opacity groups   PushOpacity / PopOpacity: fade everything drawn between
//                      the two calls as one, raw VAN_COL32 colours included.
//   * Transforms       PushTransform / PopTransform: rotate, scale, move or skew
//                      everything drawn between the two calls (spin, pop, wiggle).
//   * Shadows          soft drop shadows, glows and inner shadows for rounded
//                      rectangles and circles (a Gaussian profile built from rings).
//   * Gradients        linear, radial and conic fills with up to 8 stops, for
//                      rectangles (rounded), circles and convex polygons.
//   * Clip shapes      PushClipRoundedRect / PushClipCircle / PushClipConvex:
//                      clip anything drawn inside to the shape, anti-aliased —
//                      rounded images, avatar circles, reveal masks.
//   * Shimmer          one highlight sweeping across the whole screen, so every
//                      skeleton on a page shimmers in step.
//   * Backdrop blur    frosted-glass panels. The blur itself runs on the GPU and
//                      needs a renderer hook, installed by
//                      VanGui_ImplDX11_InitBlur / VanGui_ImplDX12_InitBlur /
//                      VanGui_ImplOpenGL3_InitBlur; without one the panel
//                      falls back to a tint.
//
// All effects work on the draw list's own vertices (a pure consumer of the
// public API). Group effects only see what goes into that draw list: a child
// window has its own list, so wrap its contents separately.
//
// Opt-in / zero-cost via VANGUI_ENABLE_EFFECTS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

// A 2-D affine transform: p' = (a*x + c*y + e, b*x + d*y + f).
struct VanTransform
{
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;

    VanVec2 Apply(const VanVec2& p) const { return VanVec2(a * p.x + c * p.y + e, b * p.x + d * p.y + f); }
    // `this` after `o`: (this * o).Apply(p) == Apply(o.Apply(p)).
    VanTransform operator*(const VanTransform& o) const
    {
        VanTransform r;
        r.a = a * o.a + c * o.b; r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d; r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e; r.f = b * o.e + d * o.f + f;
        return r;
    }
    static VanTransform Translation(const VanVec2& t) { VanTransform m; m.e = t.x; m.f = t.y; return m; }
    static VanTransform Rotation(float radians, const VanVec2& pivot = VanVec2(0, 0));
    static VanTransform Scaling(const VanVec2& s, const VanVec2& pivot = VanVec2(0, 0));
    static VanTransform Skewing(float x_radians, float y_radians, const VanVec2& pivot = VanVec2(0, 0));
};

enum VanGradientKind
{
    VanGradientKind_Linear = 0,   // P0 -> P1
    VanGradientKind_Radial,       // centre P0, radius Radius
    VanGradientKind_Conic,        // centre P0, starting at Angle (radians, clockwise from +X)
};

struct VanFillStop
{
    float  Pos = 0.0f;   // 0..1
    VanU32 Col = 0;
};

struct VanGradient
{
    VanGradientKind Kind   = VanGradientKind_Linear;
    VanVec2         P0     = VanVec2(0, 0);
    VanVec2         P1     = VanVec2(1, 0);
    float           Radius = 1.0f;
    float           Angle  = 0.0f;
    VanFillStop Stops[8];
    int             StopCount = 0;

    VanGradient& Add(float pos, VanU32 col) { if (StopCount < 8) { Stops[StopCount].Pos = pos; Stops[StopCount].Col = col; ++StopCount; } return *this; }
    static VanGradient Linear(const VanVec2& p0, const VanVec2& p1, VanU32 c0, VanU32 c1) { VanGradient g; g.Kind = VanGradientKind_Linear; g.P0 = p0; g.P1 = p1; g.Add(0.0f, c0).Add(1.0f, c1); return g; }
    static VanGradient Radial(const VanVec2& centre, float radius, VanU32 inner, VanU32 outer) { VanGradient g; g.Kind = VanGradientKind_Radial; g.P0 = centre; g.Radius = radius; g.Add(0.0f, inner).Add(1.0f, outer); return g; }
    static VanGradient Conic(const VanVec2& centre, float angle, VanU32 c0, VanU32 c1) { VanGradient g; g.Kind = VanGradientKind_Conic; g.P0 = centre; g.Angle = angle; g.Add(0.0f, c0).Add(1.0f, c1); return g; }
};

// Soft shadow parameters.
struct VanShadow
{
    VanU32  Color  = VAN_COL32(0, 0, 0, 110);
    float   Blur   = 12.0f;               // softness, pixels (about 2 sigma)
    VanVec2 Offset = VanVec2(0.0f, 4.0f);
    float   Spread = 0.0f;                // grow (+) or shrink (-) the shadow shape
    bool    Hollow = false;               // leave the area under the shape empty (for translucent shapes)
};

// Backdrop blur hook: called by the renderer while it draws the frame, at the
// point the blurred panel sits. Rectangles are in display coordinates (the same
// space as the draw list); the handler maps them to its render target with the
// draw data's DisplayPos and FramebufferScale.
struct VanBlurRequest
{
    VanVec4 Rect;       // x0, y0, x1, y1 (display coordinates)
    VanVec4 ClipRect;   // scissor at the time of the request
    float   Radius;     // blur radius, pixels
    float   Rounding;   // corner radius, pixels
    VanU32  Tint;       // composited over the blurred result
    float   Saturation; // 1 = unchanged
};
typedef void (*VanBlurHandler)(const VanDrawList* parent_list, const VanDrawCmd* cmd, const VanBlurRequest& req, void* user_data);

#ifdef VANGUI_ENABLE_EFFECTS

// ── Opacity groups ────────────────────────────────────────────────────────────
// `dl` defaults to the current window's draw list.
VANGUI_API void PushOpacity(float alpha, VanDrawList* dl = nullptr);
VANGUI_API void PopOpacity();

// ── Transforms ────────────────────────────────────────────────────────────────
VANGUI_API void PushTransform(const VanTransform& m, VanDrawList* dl = nullptr);
VANGUI_API void PopTransform();
// Shorthands around a pivot (item-centre helpers: GetItemRectMin/Max()).
inline void PushRotation(float radians, const VanVec2& pivot, VanDrawList* dl = nullptr) { PushTransform(VanTransform::Rotation(radians, pivot), dl); }
inline void PushScale(float s, const VanVec2& pivot, VanDrawList* dl = nullptr)          { PushTransform(VanTransform::Scaling(VanVec2(s, s), pivot), dl); }
// Transform vertices [vtx_begin, end) of `dl` right now (for code that tracked its own range).
VANGUI_API void TransformVertices(VanDrawList* dl, int vtx_begin, const VanTransform& m);
VANGUI_API void ScaleVertexAlpha(VanDrawList* dl, int vtx_begin, float alpha);
VANGUI_API void TintVertices(VanDrawList* dl, int vtx_begin, VanU32 multiply);

// ── Shadows ───────────────────────────────────────────────────────────────────
VANGUI_API void DrawShadowRect(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float rounding, const VanShadow& s = VanShadow());
VANGUI_API void DrawShadowCircle(VanDrawList* dl, const VanVec2& centre, float radius, const VanShadow& s = VanShadow());
// A glow is a centred shadow in a light colour.
VANGUI_API void DrawGlowRect(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float rounding, VanU32 color, float blur);
// Shading that falls inward from the edge of a rounded rectangle (inset fields, wells).
VANGUI_API void DrawInnerShadowRect(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float rounding, VanU32 color, float blur,
                                    const VanVec2& offset = VanVec2(0, 1));

// ── Gradients ─────────────────────────────────────────────────────────────────
VANGUI_API void FillRectGradient(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float rounding, const VanGradient& g);
VANGUI_API void FillCircleGradient(VanDrawList* dl, const VanVec2& centre, float radius, const VanGradient& g);
// Convex polygon, any winding.
VANGUI_API void FillConvexGradient(VanDrawList* dl, const VanVec2* points, int count, const VanGradient& g);
// The gradient's colour at a point (for matching strokes or text to a fill).
VANGUI_API VanU32 GradientColorAt(const VanGradient& g, const VanVec2& p);

// ── Clip shapes ───────────────────────────────────────────────────────────────
// Everything drawn to `dl` until PopClipShape() is clipped to the shape with an
// anti-aliased edge. Nests (the inner shape is applied first).
VANGUI_API void PushClipRoundedRect(const VanVec2& min, const VanVec2& max, float rounding, VanDrawList* dl = nullptr);
VANGUI_API void PushClipCircle(const VanVec2& centre, float radius, VanDrawList* dl = nullptr);
VANGUI_API void PushClipConvex(const VanVec2* points, int count, VanDrawList* dl = nullptr);
VANGUI_API void PopClipShape();

// ── Shimmer ───────────────────────────────────────────────────────────────────
// One highlight band sweeping across the display every `period` seconds. Fill a
// rounded rectangle with `base` and the band on top, in screen space, so every
// call on the page shimmers in step. Held still under ReduceMotion.
VANGUI_API void DrawShimmerRect(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float rounding, VanU32 base,
                                VanU32 highlight, float period = 1.4f);

// ── Backdrop blur ─────────────────────────────────────────────────────────────
// Blur what is already drawn behind [min, max] and tint it (frosted glass). Needs
// a renderer that installed a blur handler: vangui_impl_dx11_blur.h,
// vangui_impl_opengl3_blur.h, or VanGui_ImplDX12_InitBlur in vangui_impl_dx12.h
// (which also needs VanGui_ImplDX12_SetRenderTarget each frame). Otherwise draws
// `tint` at slightly higher opacity so the panel still reads.
VANGUI_API void DrawBackdropBlur(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float radius, float rounding = 0.0f,
                                 VanU32 tint = VAN_COL32(255, 255, 255, 40), float saturation = 1.0f);
VANGUI_API void SetBlurHandler(VanBlurHandler handler, void* user_data = nullptr);
VANGUI_API bool HasBlurHandler();

// RAII helpers.
struct VanOpacityScope   { explicit VanOpacityScope(float a, VanDrawList* dl = nullptr) { PushOpacity(a, dl); } ~VanOpacityScope() { PopOpacity(); } VanOpacityScope(const VanOpacityScope&) = delete; VanOpacityScope& operator=(const VanOpacityScope&) = delete; };
struct VanTransformScope { explicit VanTransformScope(const VanTransform& m, VanDrawList* dl = nullptr) { PushTransform(m, dl); } ~VanTransformScope() { PopTransform(); } VanTransformScope(const VanTransformScope&) = delete; VanTransformScope& operator=(const VanTransformScope&) = delete; };

#else // ----------------------------- shims -----------------------------------

inline VanTransform VanTransform::Rotation(float, const VanVec2&) { return VanTransform(); }
inline VanTransform VanTransform::Scaling(const VanVec2&, const VanVec2&) { return VanTransform(); }
inline VanTransform VanTransform::Skewing(float, float, const VanVec2&) { return VanTransform(); }
inline void PushOpacity(float, VanDrawList* = nullptr) {}
inline void PopOpacity() {}
inline void PushTransform(const VanTransform&, VanDrawList* = nullptr) {}
inline void PopTransform() {}
inline void PushRotation(float, const VanVec2&, VanDrawList* = nullptr) {}
inline void PushScale(float, const VanVec2&, VanDrawList* = nullptr) {}
inline void TransformVertices(VanDrawList*, int, const VanTransform&) {}
inline void ScaleVertexAlpha(VanDrawList*, int, float) {}
inline void TintVertices(VanDrawList*, int, VanU32) {}
inline void DrawShadowRect(VanDrawList*, const VanVec2&, const VanVec2&, float, const VanShadow& = VanShadow()) {}
inline void DrawShadowCircle(VanDrawList*, const VanVec2&, float, const VanShadow& = VanShadow()) {}
inline void DrawGlowRect(VanDrawList*, const VanVec2&, const VanVec2&, float, VanU32, float) {}
inline void DrawInnerShadowRect(VanDrawList*, const VanVec2&, const VanVec2&, float, VanU32, float, const VanVec2& = VanVec2(0, 1)) {}
inline void FillRectGradient(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float r, const VanGradient& g) { if (dl && g.StopCount) dl->AddRectFilled(a, b, g.Stops[0].Col, r); }
inline void FillCircleGradient(VanDrawList* dl, const VanVec2& c, float r, const VanGradient& g) { if (dl && g.StopCount) dl->AddCircleFilled(c, r, g.Stops[0].Col); }
inline void FillConvexGradient(VanDrawList* dl, const VanVec2* p, int n, const VanGradient& g) { if (dl && g.StopCount) dl->AddConvexPolyFilled(p, n, g.Stops[0].Col); }
inline VanU32 GradientColorAt(const VanGradient& g, const VanVec2&) { return g.StopCount ? g.Stops[0].Col : 0; }
inline void PushClipRoundedRect(const VanVec2&, const VanVec2&, float, VanDrawList* = nullptr) {}
inline void PushClipCircle(const VanVec2&, float, VanDrawList* = nullptr) {}
inline void PushClipConvex(const VanVec2*, int, VanDrawList* = nullptr) {}
inline void PopClipShape() {}
inline void DrawShimmerRect(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float r, VanU32 base, VanU32, float = 1.4f) { if (dl) dl->AddRectFilled(a, b, base, r); }
inline void DrawBackdropBlur(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float, float r = 0.0f, VanU32 tint = VAN_COL32(255, 255, 255, 40), float = 1.0f) { if (dl) dl->AddRectFilled(a, b, tint, r); }
inline void SetBlurHandler(VanBlurHandler, void* = nullptr) {}
inline bool HasBlurHandler() { return false; }
struct VanOpacityScope   { explicit VanOpacityScope(float, VanDrawList* = nullptr) {} };
struct VanTransformScope { explicit VanTransformScope(const VanTransform&, VanDrawList* = nullptr) {} };

#endif // VANGUI_ENABLE_EFFECTS

} // namespace VanGui
