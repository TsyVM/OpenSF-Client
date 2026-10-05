// vangui_vector.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — vector drawing: strokes with real caps and joins,
// filleted corners, curves, fills with holes, dashes, draw-on trimming, and the
// animated treatments an interface built from drawn shapes wants (glow,
// breathing, hue cycling).
//
// Why this exists: VanDrawList::AddPolyline draws a ribbon with butt ends and
// mitred corners. That is right for a widget outline and wrong for line art —
// square ends and spiked corners are the single clearest tell that a glyph was
// assembled from primitives rather than drawn. Every published icon set
// specifies round caps and round joins on a fixed grid, and without them no
// amount of care over the shapes reads as designed.
//
// THE STROKER
//   A stroke is tessellated as ONE mesh: the outer side of every corner is swept
//   (round), cut (bevel) or pointed (miter); the inner side meets at the miter
//   point; caps are part of the same strip; the anti-aliasing fringe runs
//   around the whole outline. No triangle covers another, so a stroke drawn at
//   50% alpha is 50% everywhere — corners and ends included. That is what lets
//   drawn icons fade in and out cleanly.
//
// Draw-list only, a pure consumer of the public VanGui API. Scratch memory is
// module-owned and reused, so steady-state drawing does not allocate.
// Opt-in / zero-cost via VANGUI_ENABLE_VECTOR.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

enum VanVectorCap {
    VanVectorCap_Butt = 0,    // ends flat on the last point
    VanVectorCap_Round,       // a half disc past each end
    VanVectorCap_Square       // a half square past each end
};

enum VanVectorJoin {
    VanVectorJoin_Miter = 0,  // pointed corner (falls back to bevel past miter_limit)
    VanVectorJoin_Round,      // an arc around each corner
    VanVectorJoin_Bevel       // the corner cut flat
};

enum VanFillRule {
    VanFillRule_NonZero = 0,  // nested contours alternate filled/hole by nesting depth
    VanFillRule_EvenOdd,
};

// One stroke's appearance. Defaults are the line-art ones: round both ends.
struct VanStroke {
    VanU32 color = VAN_COL32_WHITE;
    float thickness = 2.0f;
    VanVectorCap cap = VanVectorCap_Round;
    VanVectorJoin join = VanVectorJoin_Round;
    float miter_limit = 4.0f;     // miter joins longer than this × half-width become bevels
    VanU32 color_end = 0;         // non-zero: the colour runs from `color` at the start to `color_end` at the end (comet tails, gradients)
    float trim_start = 0.0f;      // draw only this part of the path's length (0..1). Closed paths wrap:
    float trim_end = 1.0f;        //   start 0.8 / end 1.1 draws across the seam. Animate trim_end 0->1 to "draw on".

    VanStroke() = default;
    VanStroke(VanU32 c, float t) : color(c), thickness(t) {}
    VanStroke(VanU32 c, float t, VanVectorCap k, VanVectorJoin j)
        : color(c), thickness(t), cap(k), join(j) {}
};

// A reusable path: contours of line segments, curves and arcs, flattened as they
// are added. Owns its storage (reuse one across frames to avoid allocating).
struct VanPath {
    VanVector<VanVec2> Points;        // every contour's points, back to back
    VanVector<int>     Starts;        // index in Points where each contour begins
    VanVector<bool>    ClosedFlags;   // per contour
    float              Tolerance = 0.25f;   // curve flattening error, in the path's units

    void Clear();
    VanPath& MoveTo(const VanVec2& p);
    VanPath& LineTo(const VanVec2& p);
    VanPath& QuadTo(const VanVec2& c, const VanVec2& p);
    VanPath& CubicTo(const VanVec2& c1, const VanVec2& c2, const VanVec2& p);
    // Arc around `centre` from angle a0 to a1 (radians, clockwise from +X, y down).
    VanPath& ArcTo(const VanVec2& centre, float radius, float a0, float a1);
    // SVG elliptical arc from the current point to `p`.
    VanPath& SvgArcTo(float rx, float ry, float x_axis_rotation_deg, bool large_arc, bool sweep, const VanVec2& p);
    VanPath& Close();
    VanPath& Rect(const VanVec2& a, const VanVec2& b, float rounding = 0.0f);
    VanPath& Circle(const VanVec2& centre, float radius);
    VanPath& Ellipse(const VanVec2& centre, const VanVec2& radius);
    VanPath& Polygon(const VanVec2* pts, int count, bool closed = true);

    // Apply  p' = (a*x + c*y + e, b*x + d*y + f)  to every point (SVG matrix order).
    void Transform(float a, float b, float c, float d, float e, float f);
    void Translate(const VanVec2& t) { Transform(1, 0, 0, 1, t.x, t.y); }
    void Scale(float sx, float sy)  { Transform(sx, 0, 0, sy, 0, 0); }

    int   ContourCount() const { return Starts.Size; }
    int   ContourSize(int i) const { return (i + 1 < Starts.Size ? Starts[i + 1] : Points.Size) - Starts[i]; }
    float Length() const;
    bool  Empty() const { return Points.Size == 0; }

    // Stroke every contour (trim spans the whole path, contour by contour).
    void Stroke(VanDrawList* dl, const VanStroke& s) const;
    // Fill the contours together, so inner contours become holes.
    void Fill(VanDrawList* dl, VanU32 col, VanFillRule rule = VanFillRule_NonZero) const;

private:
    VanVec2 Current() const { return Points.Size ? Points.back() : VanVec2(0, 0); }
    bool    m_open = false;
};

#ifdef VANGUI_ENABLE_VECTOR

// ── Strokes ──────────────────────────────────────────────────────────────────

// The primitive the rest are built on: a polyline with the stroke's caps and joins.
VANGUI_API void StrokePath(VanDrawList* dl, const VanVec2* points, int count, const VanStroke& s,
                           bool closed = false);
VANGUI_API void StrokeLine(VanDrawList* dl, const VanVec2& a, const VanVec2& b, const VanStroke& s);
// Angles in radians, clockwise from +X, the way VanDrawList::PathArcTo takes them.
VANGUI_API void StrokeArc(VanDrawList* dl, const VanVec2& centre, float radius, float a_min, float a_max,
                          const VanStroke& s, int segments = 0);
VANGUI_API void StrokeCircle(VanDrawList* dl, const VanVec2& centre, float radius, const VanStroke& s,
                             int segments = 0);
VANGUI_API void StrokeEllipse(VanDrawList* dl, const VanVec2& centre, const VanVec2& radius, const VanStroke& s,
                              float rotation = 0.0f);
VANGUI_API void StrokeRect(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float rounding,
                           const VanStroke& s);
VANGUI_API void StrokeBezierCubic(VanDrawList* dl, const VanVec2& p0, const VanVec2& p1, const VanVec2& p2,
                                  const VanVec2& p3, const VanStroke& s);
VANGUI_API void StrokeBezierQuadratic(VanDrawList* dl, const VanVec2& p0, const VanVec2& p1, const VanVec2& p2,
                                      const VanStroke& s);

// A polyline whose corners are rounded off by `radius` before it is stroked — the rule every
// modern icon grid states as "almost every sharp corner should be rounded". Corners too tight
// for the radius keep as much of it as the two edges allow rather than overshooting.
VANGUI_API void StrokeFillet(VanDrawList* dl, const VanVec2* points, int count, float radius,
                             const VanStroke& s, bool closed = false);
// The filleted outline itself, for filling or measuring. Returns the point count written to
// `out` (at most `out_capacity`).
VANGUI_API int  FilletPoints(const VanVec2* points, int count, float radius, bool closed,
                             VanVec2* out, int out_capacity);

// A dashed run along a path, for range rings, reticles and "in flight" marks. Dashes follow
// the path round its corners. `offset` slides the pattern along the path (animate it for
// marching ants). Every dash gets the stroke's caps, so a round cap adds the stroke's
// thickness to each dash — keep `gap` above the thickness or the dashes close up.
VANGUI_API void StrokeDashed(VanDrawList* dl, const VanVec2* points, int count, float dash, float gap,
                             const VanStroke& s, bool closed = false, float offset = 0.0f);

// ── Fills ────────────────────────────────────────────────────────────────────

// A simple polygon (concave allowed, any winding), anti-aliased.
VANGUI_API void FillPath(VanDrawList* dl, const VanVec2* points, int count, VanU32 col);
// Several contours filled together: contours nested inside others become holes
// (islands inside holes fill again). Anti-aliased.
VANGUI_API void FillContours(VanDrawList* dl, const VanVec2* const* contours, const int* counts, int contour_count,
                             VanU32 col, VanFillRule rule = VanFillRule_NonZero);

// ── Measuring ────────────────────────────────────────────────────────────────

VANGUI_API float   PathLength(const VanVec2* points, int count, bool closed = false);
// The point `distance` along the path (clamped; closed paths wrap), and the direction there.
VANGUI_API VanVec2 PointAtLength(const VanVec2* points, int count, float distance, bool closed = false,
                                 VanVec2* out_direction = nullptr);

// ── Treatments ───────────────────────────────────────────────────────────────

// An outer glow around a path: `passes` widening, fading strokes under it. Cheap enough to
// put on a hovered control every frame.
VANGUI_API void GlowPath(VanDrawList* dl, const VanVec2* points, int count, const VanStroke& s,
                         VanU32 glow_color, float spread, int passes = 4, bool closed = false);

// ── Animation helpers ────────────────────────────────────────────────────────
// These read the animation clock (Anim::Phase when the substrate is on), so they
// need no state from the caller and keep an idle-sleeping app drawing while shown.

// A smooth 0..1 rise and fall at `hz`, never dropping below `floor_value`.
VANGUI_API float Breathe(float hz = 1.0f, float floor_value = 0.0f);
// A hue sweep at `hz`, returned as a colour. For accents that cycle rather than sit still.
VANGUI_API VanU32 HueCycle(float hz = 0.1f, float saturation = 0.7f, float value = 1.0f,
                           float alpha = 1.0f);
// `a` to `b` by `t`, per channel, for fading one palette colour into another.
VANGUI_API VanU32 MixColor(VanU32 a, VanU32 b, float t);

#else // ----------------------------- shims -----------------------------------

inline void StrokePath(VanDrawList*, const VanVec2*, int, const VanStroke&, bool = false) {}
inline void StrokeLine(VanDrawList*, const VanVec2&, const VanVec2&, const VanStroke&) {}
inline void StrokeArc(VanDrawList*, const VanVec2&, float, float, float, const VanStroke&, int = 0) {}
inline void StrokeCircle(VanDrawList*, const VanVec2&, float, const VanStroke&, int = 0) {}
inline void StrokeEllipse(VanDrawList*, const VanVec2&, const VanVec2&, const VanStroke&, float = 0.0f) {}
inline void StrokeRect(VanDrawList*, const VanVec2&, const VanVec2&, float, const VanStroke&) {}
inline void StrokeBezierCubic(VanDrawList*, const VanVec2&, const VanVec2&, const VanVec2&, const VanVec2&, const VanStroke&) {}
inline void StrokeBezierQuadratic(VanDrawList*, const VanVec2&, const VanVec2&, const VanVec2&, const VanStroke&) {}
inline void StrokeFillet(VanDrawList*, const VanVec2*, int, float, const VanStroke&, bool = false) {}
inline int  FilletPoints(const VanVec2*, int, float, bool, VanVec2*, int) { return 0; }
inline void StrokeDashed(VanDrawList*, const VanVec2*, int, float, float, const VanStroke&, bool = false, float = 0.0f) {}
inline void FillPath(VanDrawList*, const VanVec2*, int, VanU32) {}
inline void FillContours(VanDrawList*, const VanVec2* const*, const int*, int, VanU32, VanFillRule = VanFillRule_NonZero) {}
inline float PathLength(const VanVec2*, int, bool = false) { return 0.0f; }
inline VanVec2 PointAtLength(const VanVec2* p, int n, float, bool = false, VanVec2* = nullptr) { return n > 0 ? p[0] : VanVec2(0, 0); }
inline void GlowPath(VanDrawList*, const VanVec2*, int, const VanStroke&, VanU32, float, int = 4, bool = false) {}
inline float Breathe(float = 1.0f, float = 0.0f) { return 1.0f; }
inline VanU32 HueCycle(float = 0.1f, float = 0.7f, float = 1.0f, float = 1.0f) { return VAN_COL32_WHITE; }
inline VanU32 MixColor(VanU32 a, VanU32, float) { return a; }

// VanPath without the module: it records nothing and draws nothing.
inline void VanPath::Clear() { Points.clear(); Starts.clear(); ClosedFlags.clear(); m_open = false; }
inline VanPath& VanPath::MoveTo(const VanVec2&) { return *this; }
inline VanPath& VanPath::LineTo(const VanVec2&) { return *this; }
inline VanPath& VanPath::QuadTo(const VanVec2&, const VanVec2&) { return *this; }
inline VanPath& VanPath::CubicTo(const VanVec2&, const VanVec2&, const VanVec2&) { return *this; }
inline VanPath& VanPath::ArcTo(const VanVec2&, float, float, float) { return *this; }
inline VanPath& VanPath::SvgArcTo(float, float, float, bool, bool, const VanVec2&) { return *this; }
inline VanPath& VanPath::Close() { return *this; }
inline VanPath& VanPath::Rect(const VanVec2&, const VanVec2&, float) { return *this; }
inline VanPath& VanPath::Circle(const VanVec2&, float) { return *this; }
inline VanPath& VanPath::Ellipse(const VanVec2&, const VanVec2&) { return *this; }
inline VanPath& VanPath::Polygon(const VanVec2*, int, bool) { return *this; }
inline void  VanPath::Transform(float, float, float, float, float, float) {}
inline float VanPath::Length() const { return 0.0f; }
inline void  VanPath::Stroke(VanDrawList*, const VanStroke&) const {}
inline void  VanPath::Fill(VanDrawList*, VanU32, VanFillRule) const {}

#endif // VANGUI_ENABLE_VECTOR

} // namespace VanGui
