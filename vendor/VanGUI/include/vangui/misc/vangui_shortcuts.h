// vangui_shortcuts.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — declarative keyboard shortcut registry.
// Register chord -> action once; DispatchShortcuts() fires them each frame. When
// the command palette is compiled in, each shortcut is also registered as a
// palette command automatically. Opt-in via VANGUI_ENABLE_SHORTCUTS.
//
//   RegisterShortcut(VanGuiMod_Ctrl | VanGuiKey_S, "Save", [](void*){ save(); });
//   ... each frame: VanGui::DispatchShortcuts();
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_SHORTCUTS

// Register a global shortcut. `name` shows in the palette (if enabled).
VANGUI_API void RegisterShortcut(VanGuiKeyChord chord, const char* name,
                                 void (*action)(void*), void* user_data = nullptr);
VANGUI_API void ClearShortcuts();

// Check all registered chords and run matching actions. Call once per frame.
VANGUI_API void DispatchShortcuts();

// Human-readable label for a chord, e.g. "Ctrl+Shift+S". Writes into buf.
VANGUI_API void GetShortcutLabel(VanGuiKeyChord chord, char* buf, size_t buf_size);

#else // ----------------------------- shims -----------------------------------

inline void RegisterShortcut(VanGuiKeyChord, const char*, void (*)(void*), void* = nullptr) {}
inline void ClearShortcuts() {}
inline void DispatchShortcuts() {}
inline void GetShortcutLabel(VanGuiKeyChord, char* buf, size_t n) { if (n) buf[0] = '\0'; }

#endif // VANGUI_ENABLE_SHORTCUTS

} // namespace VanGui
