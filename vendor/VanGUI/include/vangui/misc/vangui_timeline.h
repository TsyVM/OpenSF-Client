// vangui_timeline.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — timeline / keyframe sequencer.
//
// A time ruler with a draggable playhead and one or more keyframe tracks
// (draggable diamonds; double-click a lane to add, right-click a key to remove).
// Key arrays are caller-owned; the widget keeps only a small active context.
// One active timeline at a time. Opt-in / zero-cost via VANGUI_ENABLE_TIMELINE.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_TIMELINE

// Begin a timeline. `current_time` is scrubbed by dragging the ruler/playhead.
VANGUI_API bool BeginTimeline(const char* id, VanVec2 size, float t_min, float t_max, float* current_time);

// One track row of keyframes (times in [t_min,t_max]). Returns true on change.
VANGUI_API bool TimelineTrack(const char* name, float* keys, int* count, int max_keys);

VANGUI_API void EndTimeline();

#else // ------------------------------- shims ---------------------------------

inline bool BeginTimeline(const char*, VanVec2, float, float, float*) { return false; }
inline bool TimelineTrack(const char*, float*, int*, int) { return false; }
inline void EndTimeline() {}

#endif // VANGUI_ENABLE_TIMELINE

} // namespace VanGui
