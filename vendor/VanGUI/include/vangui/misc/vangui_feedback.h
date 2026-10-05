// vangui_feedback.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — micro-interactions. Pure consumers of vangui_anim:
// count-up numbers, click ripple, hover elevation, spring-reorderable list.
// Opt-in / zero-cost via VANGUI_ENABLE_FEEDBACK (implies ANIM).
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#if defined(VANGUI_ENABLE_FEEDBACK) && !defined(VANGUI_ENABLE_ANIM)
#  error "VANGUI_ENABLE_FEEDBACK requires VANGUI_ENABLE_ANIM."
#endif

namespace VanGui {

#ifdef VANGUI_ENABLE_FEEDBACK

// A number that eases toward `value` (count-up). `fmt` formats the shown float.
VANGUI_API void AnimatedValue(const char* id, float value, const char* fmt = "%.0f");

// Button with a material-style click ripple.
VANGUI_API bool RippleButton(const char* label, VanVec2 size = VanVec2(0, 0));

// Button that lifts (animated glow) on hover.
VANGUI_API bool ElevatedButton(const char* label, VanVec2 size = VanVec2(0, 0));

// Spring-animated reorderable list. `order` holds `count` item indices; drag a
// row to reorder. Returns true if the order changed this frame.
VANGUI_API bool ReorderableList(const char* id, const char* const* labels, int* order, int count);

#else // ----------------------------- shims -----------------------------------

inline void AnimatedValue(const char*, float, const char* = "%.0f") {}
inline bool RippleButton(const char* label, VanVec2 size = VanVec2(0,0)) { return Button(label, size); }
inline bool ElevatedButton(const char* label, VanVec2 size = VanVec2(0,0)) { return Button(label, size); }
inline bool ReorderableList(const char*, const char* const*, int*, int) { return false; }

#endif // VANGUI_ENABLE_FEEDBACK

} // namespace VanGui
