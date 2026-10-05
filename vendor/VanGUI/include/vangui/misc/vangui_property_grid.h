// vangui_property_grid.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — property grid (2-column inspector).
//
// A Qt-style property editor: label column + editor column, collapsible
// categories, per-row reset. Built on core tables; no retained state of its own.
// Opt-in / zero-cost via VANGUI_ENABLE_PROPERTY_GRID.
//
//   if (BeginPropertyGrid("settings")) {
//       if (PropertyCategory("Display")) {
//           PropertyFloat("Gamma", &gamma, 0.5f, 2.5f);
//           PropertyBool ("V-Sync", &vsync);
//       }
//       EndPropertyGrid();
//   }
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_PROPERTY_GRID

VANGUI_API bool BeginPropertyGrid(const char* id, float label_width = 0.0f);
VANGUI_API void EndPropertyGrid();

// Collapsible group header spanning both columns. Emit rows only when it returns true.
VANGUI_API bool BeginPropertyCategory(const char* label, bool default_open = true);
VANGUI_API void EndPropertyCategory();   // optional symmetric no-op

// Typed rows — return true the frame the value changes.
VANGUI_API bool PropertyFloat(const char* label, float* v, float mn = 0.0f, float mx = 0.0f, const char* fmt = "%.3f");
VANGUI_API bool PropertyInt  (const char* label, int* v, int mn = 0, int mx = 0);
VANGUI_API bool PropertyBool (const char* label, bool* v);
VANGUI_API bool PropertyColor(const char* label, float col[4]);
VANGUI_API bool PropertyText (const char* label, char* buf, size_t buf_size);
VANGUI_API bool PropertyCombo(const char* label, int* current, const char* const items[], int count);
VANGUI_API void PropertyLabel(const char* label, const char* value_fmt, ...);   // read-only row

#else // ------------------------------- shims ---------------------------------

inline bool BeginPropertyGrid(const char*, float = 0.0f) { return false; }
inline void EndPropertyGrid() {}
inline bool BeginPropertyCategory(const char*, bool = true) { return false; }
inline void EndPropertyCategory() {}
inline bool PropertyFloat(const char*, float*, float = 0, float = 0, const char* = "%.3f") { return false; }
inline bool PropertyInt  (const char*, int*, int = 0, int = 0) { return false; }
inline bool PropertyBool (const char*, bool*) { return false; }
inline bool PropertyColor(const char*, float[4]) { return false; }
inline bool PropertyText (const char*, char*, size_t) { return false; }
inline bool PropertyCombo(const char*, int*, const char* const[], int) { return false; }
inline void PropertyLabel(const char*, const char*, ...) {}

#endif // VANGUI_ENABLE_PROPERTY_GRID

} // namespace VanGui
