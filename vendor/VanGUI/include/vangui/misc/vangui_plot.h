// vangui_plot.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — lightweight XY / real-time plot.
//
// A small step up from vangui_charts: framed axes, grid, multiple series, and a
// hover crosshair with value readout — without importing a full plotting
// library. One active plot at a time (immediate-mode). The caller owns the data
// (maintain your own ring buffer for scrolling telemetry). Opt-in / zero-cost
// via VANGUI_ENABLE_PLOT.
//
//   if (BeginPlot("cpu", {0,150}, 0, N, 0, 100)) {
//       PlotValues("load", history, N);
//       EndPlot();
//   }
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_PLOT

VANGUI_API bool BeginPlot(const char* id, VanVec2 size,
                          float x_min, float x_max, float y_min, float y_max);
VANGUI_API void PlotXY(const char* label, const float* xs, const float* ys, int count, VanU32 color = 0);
VANGUI_API void PlotValues(const char* label, const float* values, int count, VanU32 color = 0); // x = index
VANGUI_API void PlotHLine(float y, VanU32 color = 0);
VANGUI_API void EndPlot();

#else // ------------------------------- shims ---------------------------------

inline bool BeginPlot(const char*, VanVec2, float, float, float, float) { return false; }
inline void PlotXY(const char*, const float*, const float*, int, VanU32 = 0) {}
inline void PlotValues(const char*, const float*, int, VanU32 = 0) {}
inline void PlotHLine(float, VanU32 = 0) {}
inline void EndPlot() {}

#endif // VANGUI_ENABLE_PLOT

} // namespace VanGui
