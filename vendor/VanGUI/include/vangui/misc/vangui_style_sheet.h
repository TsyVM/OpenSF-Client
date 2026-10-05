// vangui_style_sheet.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: declarative .vss theming front-end.
//
// A small QSS-like authoring format that compiles down to a VanThemeTokenSet
// (consumed by vangui_theme_engine) plus a few style-var overrides. The theme
// engine remains the runtime — the stylesheet is just an authoring layer, so a
// saved .vss can cross-fade live through the engine's animated transition.
//
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_STYLESHEET.
//   * VANGUI_ENABLE_STYLESHEET implies the theme engine (it is the runtime).
//   * Parse failures return std::expected with a human message + line number —
//     never an exception, never a silent default.
//
// GRAMMAR (minimal subset; see docs/ENHANCEMENT_ROADMAP.md §14.2):
//   /* comments */
//   :root        { primary: #4285F4; surface: #14141A; radius: 6px; }
//   Button       { background: $primary; rounding: $radius; }
//   Button:hover { background: lighten($primary, 8%); }
//   Window       { background: $surface; }
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#if defined(VANGUI_ENABLE_STYLESHEET) && !defined(VANGUI_ENABLE_THEME_ENGINE)
// The theme engine has no single enable macro of its own in some builds; it is
// pulled in by linking vangui_theme_engine. We only hard-require its header here.
#endif

#ifdef VANGUI_ENABLE_STYLESHEET
#include "vangui_theme_engine.h"
#include <expected>
#endif

namespace VanGui {

#ifdef VANGUI_ENABLE_STYLESHEET

// Load + apply a .vss file. On success the parsed tokens are applied (optionally
// animated when transition_ms > 0, reusing the theme engine's cross-fade).
[[nodiscard]] VANGUI_API std::expected<void, const char*>
    LoadStyleSheet(const char* path, float transition_ms = 0.0f);

[[nodiscard]] VANGUI_API std::expected<void, const char*>
    LoadStyleSheetFromMemory(const char* text, size_t len, float transition_ms = 0.0f);

// Scoped overrides: push a single selector's color contribution for the widgets
// that follow, pop to restore. (Minimal v1: maps the selector to its primary
// color token.) RAII guard provided.
VANGUI_API void PushStyleSheetScope(const char* selector);
VANGUI_API void PopStyleSheetScope();

struct VanStyleSheetScope
{
    explicit VanStyleSheetScope(const char* selector) { PushStyleSheetScope(selector); }
    ~VanStyleSheetScope() { PopStyleSheetScope(); }
    VanStyleSheetScope(const VanStyleSheetScope&) = delete;
    VanStyleSheetScope& operator=(const VanStyleSheetScope&) = delete;
};

// Hot-reload: watch a .vss file; poll once per frame. Reuses theme transitions.
VANGUI_API void WatchStyleSheet(const char* path);
VANGUI_API void PollStyleSheetChanges(float transition_ms = 150.0f);

#else // ----------------------------- shims -----------------------------------

inline void PushStyleSheetScope(const char*) {}
inline void PopStyleSheetScope() {}
inline void WatchStyleSheet(const char*) {}
inline void PollStyleSheetChanges(float = 150.0f) {}
struct VanStyleSheetScope { explicit VanStyleSheetScope(const char*) {} ~VanStyleSheetScope() {} };

#endif // VANGUI_ENABLE_STYLESHEET

} // namespace VanGui
