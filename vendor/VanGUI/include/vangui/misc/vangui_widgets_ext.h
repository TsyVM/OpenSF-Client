// vangui_widgets_ext.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — extended "application" widgets.
// Qt-like controls built from the draw list + existing widgets. The toggle
// switch rides vangui_anim for its thumb slide (snaps when anim is off).
//
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_WIDGETS_EXT.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_WIDGETS_EXT

// Animated on/off switch. Returns true the frame the value changed.
VANGUI_API bool Toggle(const char* label, bool* v);

// Connected segmented buttons. Sets *current to the clicked segment; returns
// true if the selection changed this frame.
VANGUI_API bool SegmentedControl(const char* id, int* current, const char* const labels[], int count);

// A pill/tag. If p_open is given, draws a close 'x'; sets *p_open=false when it
// is clicked. Returns true when the chip body is clicked.
VANGUI_API bool Chip(const char* label, bool* p_open = nullptr);

// Small inline count/status badge (drawn at the cursor; advances layout).
VANGUI_API void Badge(const char* text, VanU32 color = 0);

// Clickable path: "Home / Folder / File". Returns the index clicked, else -1.
VANGUI_API int  Breadcrumb(const char* const items[], int count);

// - [value] + stepper. Returns true if the value changed.
VANGUI_API bool Stepper(const char* label, int* v, int step = 1, int v_min = -2147483647, int v_max = 2147483647);

// Clickable star rating in [0,max_stars]. Returns true if changed.
VANGUI_API bool StarRating(const char* id, int* rating, int max_stars = 5);

// Search field with hint text and a clear 'x'. Returns true if the text changed.
VANGUI_API bool SearchBox(const char* id, char* buf, size_t buf_size, const char* hint = "Search", float width = 0.0f);

#else // ----------------------------- shims -----------------------------------

inline bool Toggle(const char*, bool* v) { return false; (void)v; }
inline bool SegmentedControl(const char*, int*, const char* const[], int) { return false; }
inline bool Chip(const char*, bool* = nullptr) { return false; }
inline void Badge(const char*, VanU32 = 0) {}
inline int  Breadcrumb(const char* const[], int) { return -1; }
inline bool Stepper(const char*, int*, int = 1, int = 0, int = 0) { return false; }
inline bool StarRating(const char*, int*, int = 5) { return false; }
inline bool SearchBox(const char*, char*, size_t, const char* = "Search", float = 0.0f) { return false; }

#endif // VANGUI_ENABLE_WIDGETS_EXT

} // namespace VanGui
