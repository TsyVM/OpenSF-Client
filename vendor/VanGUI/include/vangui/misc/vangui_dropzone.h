// vangui_dropzone.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — file / payload drop zone.
//
// Renders a bordered region that visually reacts to drag hover and reports
// dropped items. Two protocols supported:
//
//   * VanGUI internal drag-drop (BeginDragDropTarget / AcceptDragDropPayload) —
//     app-to-app inside the same process. Payload type is user-defined.
//   * OS-level file drops — the backend calls
//     `vgu::NotifyFilesDropped(paths, count)` from its native drop handler;
//     the next DropZone() over the frontmost frame consumes the drop.
//
// USAGE
//   const char* dropped[16]; int n = 0;
//   if (vgu::DropZone("Drop images here", dropped, 16, &n)) {
//       for (int i = 0; i < n; ++i) LoadImage(dropped[i]);
//   }
//
// From a backend (SDL/GLFW/Win32) file-drop callback:
//   const char* paths[] = { "/tmp/a.png", "/tmp/b.jpg" };
//   vgu::NotifyFilesDropped(paths, 2);
//
// Opt-in via VANGUI_ENABLE_DROPZONE. Empty TU otherwise.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace vgu {

#ifdef VANGUI_ENABLE_DROPZONE

// Draws a drop target of `size` (0 = fill available). On drop, fills up to
// `max_out` path pointers into `out_paths` and stores the count in `*out_count`.
// The pointers reference the internal drop buffer, which lives until the next
// call to this or NotifyFilesDropped(); copy them if you need to retain.
// Returns true only on the frame a drop occurred.
VANGUI_API bool DropZone(const char* label,
                         const char** out_paths, int max_out, int* out_count,
                         const VanVec2& size = VanVec2(0, 0));

// Backends call this when the OS reports a file drop. Paths are copied into
// an internal buffer (bounded), so the caller's storage does not need to
// outlive the call.
VANGUI_API void NotifyFilesDropped(const char* const* paths, int count);

// Whether a drop is currently pending consumption (for cursor / status UI).
[[nodiscard]] VANGUI_API bool HasPendingDrop();

#else  // zero-cost shims -----------------------------------------------------

inline bool DropZone(const char*, const char**, int, int* out_count, const VanVec2& = VanVec2(0,0))
    { if (out_count) *out_count = 0; return false; }
inline void NotifyFilesDropped(const char* const*, int) {}
[[nodiscard]] inline bool HasPendingDrop() { return false; }

#endif // VANGUI_ENABLE_DROPZONE

} // namespace vgu
