// vangui_curve_editor.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — curve & gradient editors.
//
// Draggable control-point editors for animation curves, response curves, color
// ramps and transfer functions. Draw-list only; the point/stop arrays are
// caller-owned (no retained state). Opt-in / zero-cost via
// VANGUI_ENABLE_CURVE_EDITOR.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

struct VanCurvePoint  { float x, y; };
struct VanGradientStop{ float t; VanVec4 color; };

#ifdef VANGUI_ENABLE_CURVE_EDITOR

// Editable piecewise-linear curve. `count` is in/out; points kept sorted by x.
// Double-click empties add a point, right-click a point removes it (min 2 kept).
// Returns true the frame a point is added / moved / removed.
VANGUI_API bool CurveEditor(const char* id, VanVec2 size,
                            VanCurvePoint* points, int* count, int max_points,
                            float x_min = 0.0f, float x_max = 1.0f,
                            float y_min = 0.0f, float y_max = 1.0f,
                            VanU32 color = 0);

// Sample the curve at x (piecewise-linear, clamped to the endpoints).
VANGUI_API float CurveValue(const VanCurvePoint* points, int count, float x);

// Editable color gradient. Double-click adds a stop, double-click a marker opens
// a color picker, right-click a marker removes it (min 2 kept).
VANGUI_API bool GradientEditor(const char* id, VanVec2 size,
                               VanGradientStop* stops, int* count, int max_stops);

// Display-only gradient bar.
VANGUI_API void GradientBar(const char* id, VanVec2 size,
                            const VanGradientStop* stops, int count);

// Sample the gradient at t in [0,1].
VANGUI_API VanVec4 GradientSample(const VanGradientStop* stops, int count, float t);

#else // ------------------------------- shims ---------------------------------

inline bool CurveEditor(const char*, VanVec2, VanCurvePoint*, int*, int,
                        float = 0, float = 1, float = 0, float = 1, VanU32 = 0) { return false; }
inline float CurveValue(const VanCurvePoint*, int, float x) { return x; }
inline bool GradientEditor(const char*, VanVec2, VanGradientStop*, int*, int) { return false; }
inline void GradientBar(const char*, VanVec2, const VanGradientStop*, int) {}
inline VanVec4 GradientSample(const VanGradientStop*, int, float) { return VanVec4(0, 0, 0, 1); }

#endif // VANGUI_ENABLE_CURVE_EDITOR

} // namespace VanGui
