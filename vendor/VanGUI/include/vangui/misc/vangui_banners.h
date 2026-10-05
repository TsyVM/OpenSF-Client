// vangui_banners.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — inline banners + snackbars.
//
// In-flow alert banners (persistent, colored by severity, optional action/close)
// and transient bottom-anchored snackbars with an optional action button. A
// lightweight companion to vangui_notify's corner toasts. Opt-in / zero-cost via
// VANGUI_ENABLE_BANNERS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

enum VanBannerType : int { VanBanner_Info = 0, VanBanner_Success, VanBanner_Warning, VanBanner_Error };

#ifdef VANGUI_ENABLE_BANNERS

// Inline, in-flow banner. Returns true the frame the action button is clicked.
// If p_open is set, a close button clears *p_open.
VANGUI_API bool Banner(const char* id, VanBannerType type, const char* text,
                       const char* action_label = nullptr, bool* p_open = nullptr);

// Queue a transient snackbar (bottom-center) and return its id. Call
// RenderSnackbars() each frame. Snackbars rise in and fade, pause their timer
// while hovered, and the rest settle into the space one leaves.
VANGUI_API int  Snackbar(const char* text, const char* action_label = nullptr, float duration = 4.0f);
// Start a snackbar's exit early.
VANGUI_API void DismissSnackbar(int id);
// Draw + expire queued snackbars. Returns the id of a snackbar whose action was
// clicked this frame, or -1.
VANGUI_API int  RenderSnackbars();

#else // ------------------------------- shims ---------------------------------

inline bool Banner(const char*, VanBannerType, const char*, const char* = nullptr, bool* = nullptr) { return false; }
inline int  Snackbar(const char*, const char* = nullptr, float = 4.0f) { return 0; }
inline void DismissSnackbar(int) {}
inline int  RenderSnackbars() { return -1; }

#endif // VANGUI_ENABLE_BANNERS

} // namespace VanGui
