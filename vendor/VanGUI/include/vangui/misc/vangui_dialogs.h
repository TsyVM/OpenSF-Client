// vangui_dialogs.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 3: standard application dialogs.
//
// Qt-parity modal helpers: a message box and open/save file pickers. Open/close
// is animated through Pillar 1 (vangui_anim). Fallible results use std::expected
// (no exceptions), so "user cancelled" and "no backend" are distinct outcomes.
//
//   * Opt-in / zero-cost-when-off via VANGUI_ENABLE_DIALOGS.
//   * VANGUI_ENABLE_DIALOGS implies VANGUI_ENABLE_ANIM (the open/close fade).
//
// USAGE (immediate-mode):
//   if (VanGui::Button("Delete")) VanGui::OpenMessageBox("Confirm");
//   switch (VanGui::MessageBox("Confirm", "Delete the file?", VanDialogButtons_YesNo)) {
//       case VanGui::VanDialog_Yes: do_delete(); break;
//       default: break;
//   }
//
//   // File picker: call each frame while the picker should be shown.
//   auto r = VanGui::GetOpenFileName("*.txt");
//   if (r)                         use(*r);                 // a path was chosen
//   else if (r.error()==VanGui::VanDialog_Cancel) showPicker=false;
//   // r.error()==VanDialog_None  -> still open this frame
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#if defined(VANGUI_ENABLE_DIALOGS) && !defined(VANGUI_ENABLE_ANIM)
#  error "VANGUI_ENABLE_DIALOGS requires VANGUI_ENABLE_ANIM. Define VANGUI_ENABLE_ANIM."
#endif

#ifdef VANGUI_ENABLE_DIALOGS
#include <expected>
#include <string>
#endif

namespace VanGui {

enum VanDialogResult { VanDialog_None = 0, VanDialog_Ok, VanDialog_Cancel, VanDialog_Yes, VanDialog_No };
enum VanDialogButtons { VanDialogButtons_Ok = 0, VanDialogButtons_OkCancel, VanDialogButtons_YesNo };

#ifdef VANGUI_ENABLE_DIALOGS

// Mark a message box (identified by title) to open on the next MessageBox call.
VANGUI_API void OpenMessageBox(const char* title);

// Render the message box if open; returns the chosen result on the click frame,
// VanDialog_None otherwise.
VANGUI_API VanDialogResult MessageBox(const char* title, const char* message,
                                      VanDialogButtons buttons = VanDialogButtons_Ok);

// In-GUI file pickers. Call each frame while the picker should be visible.
//   success  -> the selected path
//   error == VanDialog_None    -> still open (call again next frame)
//   error == VanDialog_Cancel  -> user cancelled / closed
[[nodiscard]] VANGUI_API std::expected<std::string, VanDialogResult>
    GetOpenFileName(const char* filters = nullptr);
[[nodiscard]] VANGUI_API std::expected<std::string, VanDialogResult>
    GetSaveFileName(const char* default_name = nullptr, const char* filters = nullptr);

#else // ----------------------------- shims -----------------------------------

inline void OpenMessageBox(const char*) {}
inline VanDialogResult MessageBox(const char*, const char*, VanDialogButtons = VanDialogButtons_Ok) { return VanDialog_None; }

#endif // VANGUI_ENABLE_DIALOGS

} // namespace VanGui
