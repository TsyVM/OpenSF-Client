// vangui_perf_hud.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — performance HUD.
//
// A tiny FPS / frame-time overlay with a rolling graph. Samples
// GetIO().DeltaTime; keeps a fixed ring (no per-frame allocation). Opt-in /
// zero-cost via VANGUI_ENABLE_PERF_HUD.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_PERF_HUD

// Corner overlay window (top-right of the main viewport).
VANGUI_API void PerfHUD(bool* p_open = nullptr);
// Body only — embed in your own window.
VANGUI_API void PerfHUDContents();

#else // ------------------------------- shims ---------------------------------

inline void PerfHUD(bool* = nullptr) {}
inline void PerfHUDContents() {}

#endif // VANGUI_ENABLE_PERF_HUD

} // namespace VanGui
