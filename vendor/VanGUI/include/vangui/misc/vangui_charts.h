// vangui_charts.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — lightweight inline charts. Draw-list only, no
// allocations, no plotting dependency. For dashboards, debug overlays, HUDs.
// Opt-in / zero-cost via VANGUI_ENABLE_CHARTS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_CHARTS

// Minimal line trace (auto-scaled to data range). color==0 -> text color.
VANGUI_API void Sparkline(const char* id, const float* values, int count, VanVec2 size, VanU32 color = 0);
// Same data, drawn as bars.
VANGUI_API void BarChart(const char* id, const float* values, int count, VanVec2 size, VanU32 color = 0);
// Line chart with a baseline and min/max auto-scale; optional explicit range.
VANGUI_API void LineChart(const char* id, const float* values, int count, VanVec2 size,
                          float v_min = 0.0f, float v_max = 0.0f, VanU32 color = 0);
// Radial gauge for a 0..1 fraction (270-degree sweep). Draws the value in the centre.
VANGUI_API void Gauge(const char* id, float fraction, float radius, const char* center_label = nullptr, VanU32 color = 0);
// Horizontal heat strip: each value mapped low(cool)->high(warm).
VANGUI_API void HeatStrip(const char* id, const float* values, int count, VanVec2 size);

#else // ----------------------------- shims -----------------------------------

inline void Sparkline(const char*, const float*, int, VanVec2, VanU32 = 0) {}
inline void BarChart(const char*, const float*, int, VanVec2, VanU32 = 0) {}
inline void LineChart(const char*, const float*, int, VanVec2, float = 0, float = 0, VanU32 = 0) {}
inline void Gauge(const char*, float, float, const char* = nullptr, VanU32 = 0) {}
inline void HeatStrip(const char*, const float*, int, VanVec2) {}

#endif // VANGUI_ENABLE_CHARTS

} // namespace VanGui
