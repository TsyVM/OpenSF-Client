// vangui_svg.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — SVG import for icons and line art.
//
// Parses SVG path data and the subset of SVG documents icon sets are published
// in, and draws them through the vector module (real round caps/joins, fills
// with holes, anti-aliased, crisp at any size, tintable).
//
// Supported: <svg viewBox width height>, <g>, <path d>, <circle>, <ellipse>,
// <rect rx ry>, <line>, <polyline>, <polygon>; presentation attributes and
// style="..." for fill, stroke, stroke-width, stroke-linecap, stroke-linejoin,
// stroke-miterlimit, fill-rule, opacity, fill-opacity, stroke-opacity;
// transform (matrix, translate, scale, rotate, skewX, skewY), inherited through
// groups; colours #rgb #rgba #rrggbb #rrggbbaa rgb() rgba() named, none,
// currentColor (drawn with the tint you pass). Path data: every command,
// absolute and relative (M L H V C S Q T A Z).
//
// Not supported (ignored): gradients, patterns, masks, clip paths, text,
// <use>, CSS stylesheets, filters.
//
// Opt-in / zero-cost via VANGUI_ENABLE_SVG (requires VANGUI_ENABLE_VECTOR).
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_vector.h"

#if defined(VANGUI_ENABLE_SVG) && !defined(VANGUI_ENABLE_VECTOR)
#  error "VANGUI_ENABLE_SVG requires VANGUI_ENABLE_VECTOR."
#endif

namespace VanGui {

// One drawable element, flattened. Points live in the owning VanSvg.
struct VanSvgShape
{
    int           FirstContour = 0;
    int           ContourCount = 0;
    VanU32        Fill = 0;                    // straight RGBA; 0 alpha = no fill
    VanU32        Stroke = 0;
    bool          FillCurrent = false;         // fill = currentColor (use the tint)
    bool          StrokeCurrent = false;
    float         StrokeWidth = 1.0f;
    float         MiterLimit = 4.0f;
    VanVectorCap  Cap = VanVectorCap_Butt;
    VanVectorJoin Join = VanVectorJoin_Miter;
    VanFillRule   FillRule = VanFillRule_NonZero;
};

// A parsed SVG document (or a single path), in its own viewBox units.
struct VanSvg
{
    VanVec4                  ViewBox = VanVec4(0, 0, 24, 24);   // x, y, w, h
    VanVector<VanVec2>       Points;
    VanVector<int>           ContourStart;
    VanVector<unsigned char> ContourClosed;
    VanVector<VanSvgShape>   Shapes;

    bool  Empty() const { return Shapes.Size == 0; }
    void  Clear() { Points.clear(); ContourStart.clear(); ContourClosed.clear(); Shapes.clear(); ViewBox = VanVec4(0, 0, 24, 24); }
    int   ContourSize(int c) const { return (c + 1 < ContourStart.Size ? ContourStart[c + 1] : Points.Size) - ContourStart[c]; }
};

struct VanSvgDrawParams
{
    VanU32 CurrentColor = VAN_COL32_WHITE;   // what currentColor means (the tint)
    float  Opacity      = 1.0f;              // multiplies every fill and stroke
    float  StrokeScale  = 1.0f;              // thicken/thin every stroke (1 = as authored)
    float  TrimEnd      = 1.0f;              // draw-on: 0..1 of every stroke's length
    bool   PixelSnap    = false;             // snap the drawing origin to whole pixels
};

#ifdef VANGUI_ENABLE_SVG

// Parse a whole SVG document. Returns false (and leaves `out` empty) on malformed input.
VANGUI_API bool ParseSvg(const char* text, VanSvg& out);
// Parse bare path data ("M4 4 L20 20 ...") as one stroked-and/or-filled shape
// in a viewBox of the given size. The style comes from `style`.
VANGUI_API bool ParseSvgPath(const char* d, VanSvg& out, const VanSvgShape& style = VanSvgShape(),
                             const VanVec4& view_box = VanVec4(0, 0, 24, 24));
// Append path data to a VanPath (in path units).
VANGUI_API bool AppendSvgPath(const char* d, VanPath& path);

// Draw into the rectangle [pos, pos + size], fitted (aspect kept, centred).
VANGUI_API void DrawSvg(VanDrawList* dl, const VanSvg& svg, const VanVec2& pos, const VanVec2& size,
                        const VanSvgDrawParams& params = VanSvgDrawParams());
// Widget: reserve `size` at the cursor and draw the SVG tinted with `tint`
// (0 = the current text colour).
VANGUI_API void SvgImage(const VanSvg& svg, const VanVec2& size, VanU32 tint = 0);

#else // ----------------------------- shims -----------------------------------

inline bool ParseSvg(const char*, VanSvg& out) { out.Clear(); return false; }
inline bool ParseSvgPath(const char*, VanSvg& out, const VanSvgShape& = VanSvgShape(), const VanVec4& = VanVec4(0, 0, 24, 24)) { out.Clear(); return false; }
inline bool AppendSvgPath(const char*, VanPath&) { return false; }
inline void DrawSvg(VanDrawList*, const VanSvg&, const VanVec2&, const VanVec2&, const VanSvgDrawParams& = VanSvgDrawParams()) {}
inline void SvgImage(const VanSvg&, const VanVec2& size, VanU32 = 0) { VanGui::Dummy(size); }

#endif // VANGUI_ENABLE_SVG

} // namespace VanGui
