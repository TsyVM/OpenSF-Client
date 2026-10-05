// vangui_actions.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Actions registry.
//
// One retained-metadata registry that unifies the three places an app action
// normally shows up:
//
//   * a menu item / toolbar button
//   * a global keyboard shortcut
//   * a command-palette entry
//
// You describe the action once — id, label, shortcut, callback — and every
// consumer subsystem (menus, shortcuts, palette) reads the same source. This is
// the Qt / VSCode "actions" pattern in ~300 lines and one std::vector-shaped
// pool.
//
// DESIGN CONTRACT
//   * Opt-in / zero-cost when off. Define VANGUI_ENABLE_ACTIONS to compile the
//     real implementation. Otherwise the public functions are inline no-ops.
//   * Retained *metadata*, not retained UI. Only the description of the action
//     is stored; rendering stays immediate-mode.
//   * No per-frame allocations. Actions are registered at startup; the pool
//     grows once. Steady-state ProcessShortcuts() is O(actions), no allocs.
//   * Function pointer + void* — no std::function, no std::string. Callers
//     that want captures bind them into a small struct and pass by pointer.
//     Strings passed to Register() must outlive the registration (usually
//     string literals; that's the intended usage).
//   * Fallible nothing. Register/Invoke/etc. cannot throw and return by value.
//
// USAGE
//     using namespace VanGui::Actions;
//     Register({ .Id="file.save", .Label="Save", .Category="File",
//                .Shortcut="Ctrl+S", .Run=+[](void*){ SaveDocument(); } });
//
//     // once per frame, before drawing:
//     ProcessShortcuts();
//
//     // command-palette feed:
//     for (int i = 0; i < Count(); ++i) { const Action* a = At(i); ... }
//
//     // menu / toolbar:
//     if (VanGui::MenuItem(Find("file.save")->Label, "Ctrl+S")) Invoke("file.save");
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>   // VanGuiKeyChord, VANGUI_API

namespace VanGui {
namespace Actions {

// Signature is (void* user) — bind captures into a small user struct if needed.
using ActionFn = void(*)(void* user);

struct Action
{
    const char* Id;         // stable string id, e.g. "file.save"; not copied.
    const char* Label;      // display label; not copied.
    const char* Category;   // optional grouping ("File", "Edit"); may be nullptr.
    const char* Icon;       // optional icon glyph / emoji; may be nullptr.
    const char* Shortcut;   // e.g. "Ctrl+Shift+P"; nullptr / "" = none.
    ActionFn    Run;        // invoked when the action fires.
    void*       UserData;   // passed to Run.
    bool        Enabled;    // false disables invocation + shortcut matching.
};

#ifdef VANGUI_ENABLE_ACTIONS

VANGUI_API void            Register(const Action& a);      // adds / replaces by Id
VANGUI_API bool            Invoke  (const char* id);       // returns true if fired
VANGUI_API void            SetEnabled(const char* id, bool enabled);
[[nodiscard]] VANGUI_API const Action* Find(const char* id);
[[nodiscard]] VANGUI_API int            Count();
[[nodiscard]] VANGUI_API const Action*  At(int index);
VANGUI_API void            Clear();

// Call once per frame (before or after your UI, up to you). Iterates registered
// actions, parses each Shortcut string once (cached), and invokes any whose
// chord fired this frame. Uses VanGui::IsKeyChordPressed() under the hood.
VANGUI_API void            ProcessShortcuts();

// Parse "Ctrl+Shift+S" into a VanGuiKeyChord ( VanGuiMod_* | VanGuiKey_* ).
// Returns 0 for empty / unparseable input. Exposed so palettes/menus can show
// or match a chord without re-implementing parsing. Case-insensitive tokens;
// separator is '+' with optional whitespace.
[[nodiscard]] VANGUI_API VanGuiKeyChord ParseShortcut(const char* s);

#else  // zero-cost shims -----------------------------------------------------

inline void            Register(const Action&) {}
inline bool            Invoke  (const char*) { return false; }
inline void            SetEnabled(const char*, bool) {}
[[nodiscard]] inline const Action* Find(const char*) { return nullptr; }
[[nodiscard]] inline int            Count() { return 0; }
[[nodiscard]] inline const Action*  At(int) { return nullptr; }
inline void            Clear() {}
inline void            ProcessShortcuts() {}
[[nodiscard]] inline VanGuiKeyChord ParseShortcut(const char*) { return 0; }

#endif // VANGUI_ENABLE_ACTIONS

} // namespace Actions
} // namespace VanGui
