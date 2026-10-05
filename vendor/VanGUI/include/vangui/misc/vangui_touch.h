// vangui_touch.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — touch gestures, built on the core's touch scrolling
// (VanMotionConfig::TouchScroll: drag to scroll, flick to coast, rubber band).
//
//   PullToRefresh     pull a list down past its top and let go to reload
//   Swipe rows        swipe a row sideways to reveal and trigger an action
//   Long press        hold an item to get what a right-click gives with a mouse
//   Canvas            pan with momentum and zoom (wheel, touchpad pinch, or a
//                     pinch the platform reports) for node graphs and maps
//
// They work with the mouse as well (drag the list down, drag a row, hold a
// button), so desktop and phone share one code path.
//
// Opt-in / zero-cost via VANGUI_ENABLE_TOUCH.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_icons.h"   // VanIconID for swipe actions (declared with or without the icons module)

namespace VanGui {

// What a swipe reveals: `Leading` sits on the left and is revealed by swiping
// right; `Trailing` sits on the right, revealed by swiping left.
struct VanSwipeAction
{
    const char* Label = nullptr;          // nullptr = no action on that side
    VanU32      Color = 0;                // 0 = a default (green leading, red trailing)
    VanIconID   Icon  = VanIcon_None;
};

enum VanSwipe
{
    VanSwipe_None = 0,
    VanSwipe_Leading,    // swiped right far enough: the leading action
    VanSwipe_Trailing,   // swiped left far enough: the trailing action
};

// A pannable, zoomable view. Offset and Zoom are yours to keep between frames;
// Origin is set by BeginCanvas (the canvas' top-left corner on screen).
struct VanCanvasView
{
    VanVec2 Offset = VanVec2(0.0f, 0.0f);   // canvas point shown at the canvas' top-left corner
    float   Zoom   = 1.0f;
    VanVec2 Origin = VanVec2(0.0f, 0.0f);

    VanVec2 ToScreen(const VanVec2& p) const { return VanVec2(Origin.x + (p.x - Offset.x) * Zoom, Origin.y + (p.y - Offset.y) * Zoom); }
    VanVec2 ToCanvas(const VanVec2& s) const { return VanVec2(Offset.x + (s.x - Origin.x) / Zoom, Offset.y + (s.y - Origin.y) / Zoom); }
};

#ifdef VANGUI_ENABLE_TOUCH

// Call first inside the scrollable window or child. Pulled down past the top by
// `threshold` pixels (0 = 4 lines) and released, it returns true once and sets
// *refreshing; while *refreshing stays true a spinner row is shown at the top.
// Clear it when the reload is done.
//     if (VanGui::PullToRefresh("inbox", &loading)) StartReload();
VANGUI_API bool PullToRefresh(const char* str_id, bool* refreshing, float threshold = 0.0f);

// A row that can be swiped. Draw its contents between Begin and End; they slide
// with the finger and the action colour shows behind them. Past 40% of the width
// on release, End returns which side fired (the row springs back either way:
// remove it, or not). A sideways drag takes over any press on the row's widgets.
VANGUI_API void     BeginSwipeRow(const char* str_id, const VanSwipeAction& leading = VanSwipeAction(), const VanSwipeAction& trailing = VanSwipeAction());
VANGUI_API VanSwipe EndSwipeRow();

// The last item has been held for `seconds` without moving: true once, and its
// press is cancelled so letting go doesn't also click it.
VANGUI_API bool IsItemLongPressed(float seconds = 0.5f);
// BeginPopupContextItem() that a long press opens too.
VANGUI_API bool BeginPopupContextItemTouch(const char* str_id = nullptr);

// A canvas the size of `size` (0 = the space left). Drag empty space to pan (it
// keeps drifting after a flick), wheel or pinch to zoom about the pointer.
// Draw with view->ToScreen(). Returns true when visible; always call EndCanvas.
VANGUI_API bool BeginCanvas(const char* str_id, VanCanvasView* view, VanVec2 size = VanVec2(0, 0), float min_zoom = 0.1f, float max_zoom = 8.0f, bool grid = true);
VANGUI_API void EndCanvas();
// A two-finger pinch from the platform (Android MotionEvent, WM_GESTURE): the
// scale since the previous event, about `centre` (screen). The hovered canvas
// takes it next frame. Precision touchpads pinch as Ctrl+wheel and need nothing.
VANGUI_API void AddPinchEvent(float scale, VanVec2 centre);

#else // ----------------------------- shims -----------------------------------

inline bool     PullToRefresh(const char*, bool*, float = 0.0f) { return false; }
inline void     BeginSwipeRow(const char*, const VanSwipeAction& = VanSwipeAction(), const VanSwipeAction& = VanSwipeAction()) { VanGui::BeginGroup(); }
inline VanSwipe EndSwipeRow() { VanGui::EndGroup(); return VanSwipe_None; }
inline bool     IsItemLongPressed(float = 0.5f) { return false; }
inline bool     BeginPopupContextItemTouch(const char* str_id = nullptr) { return VanGui::BeginPopupContextItem(str_id); }
inline bool     BeginCanvas(const char*, VanCanvasView* view, VanVec2 size = VanVec2(0, 0), float = 0.1f, float = 8.0f, bool = true)
{ if (view) view->Origin = VanGui::GetCursorScreenPos(); VanGui::Dummy(size); return true; }
inline void     EndCanvas() {}
inline void     AddPinchEvent(float, VanVec2) {}

#endif // VANGUI_ENABLE_TOUCH

} // namespace VanGui
