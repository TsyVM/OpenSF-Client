// vangui_panels.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — application chrome: splitter, accordion, status bar,
// toolbar. Lightweight compositions over the core API (+ vangui_anim for the
// accordion chevron). Opt-in / zero-cost via VANGUI_ENABLE_PANELS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_PANELS

// Draggable divider between two regions. Adjusts *size1/*size2 in opposite
// directions while dragged. Returns true on the frames it moved.
VANGUI_API bool Splitter(const char* id, bool split_vertically, float thickness,
                         float* size1, float* size2, float min1 = 24.0f, float min2 = 24.0f);

// Collapsible section: the chevron turns and the content grows open / shrinks
// shut. Returns true while the content should be drawn (open, or still
// shrinking away after being shut). Draw it (indented) when true, and ALWAYS
// call AccordionEnd() afterwards, whatever this returned.
VANGUI_API bool AccordionSection(const char* label, bool* open);
VANGUI_API void AccordionEnd();

// A bar pinned to the bottom of the main viewport. Always pair Begin/End (Begin
// returns visibility but End must be called unconditionally, like Begin/End).
VANGUI_API bool BeginStatusBar();
VANGUI_API void EndStatusBar();

// Horizontal toolbar strip. Pair Begin/End unconditionally. ToolbarSeparator()
// draws a vertical divider between groups of buttons.
VANGUI_API bool BeginToolbar(const char* id, float height = 0.0f);
VANGUI_API void EndToolbar();
VANGUI_API void ToolbarSeparator();

#else // ----------------------------- shims -----------------------------------

inline bool Splitter(const char*, bool, float, float*, float*, float = 24.0f, float = 24.0f) { return false; }
inline bool AccordionSection(const char*, bool* open) { return open && *open; }
inline void AccordionEnd() {}
inline bool BeginStatusBar() { return false; }
inline void EndStatusBar() {}
inline bool BeginToolbar(const char*, float = 0.0f) { return false; }
inline void EndToolbar() {}
inline void ToolbarSeparator() {}

#endif // VANGUI_ENABLE_PANELS

} // namespace VanGui
