#pragma once

// vangui_theme_engine.h
// Semantic token system, animated theme transitions, per-scope token overrides,
// and hot-reload support for VanGUI.
// See vangui_theme_engine.cpp for implementation.

#ifndef VANGUI_THEME_ENGINE_H
#define VANGUI_THEME_ENGINE_H

#include <vangui/vangui.h>
#include "vangui_themes.h"

namespace VanGui
{

// ---------------------------------------------------------------------------
// Semantic color token enumeration
// ---------------------------------------------------------------------------

enum VanThemeToken
{
    VanThemeToken_Background = 0,  // main window background
    VanThemeToken_Surface,         // panels, child windows, popups
    VanThemeToken_Border,          // window/widget borders
    VanThemeToken_Primary,         // buttons, selection, active elements
    VanThemeToken_Secondary,       // hovered states, secondary highlights
    VanThemeToken_TextPrimary,     // main text
    VanThemeToken_TextDim,         // disabled/hint text
    VanThemeToken_Error,           // error state
    VanThemeToken_Warning,         // warning state
    VanThemeToken_Success,         // success state
    VanThemeToken_Info,            // informational state
    VanThemeToken_Overlay,         // modal dimming, drag-drop overlay
    VanThemeToken_COUNT
};

// ---------------------------------------------------------------------------
// Token set: one color per semantic token
// ---------------------------------------------------------------------------

struct VanThemeTokenSet
{
    VanVec4 Colors[VanThemeToken_COUNT];
};

// ---------------------------------------------------------------------------
// Token -> VanGuiCol_ mapping
// ---------------------------------------------------------------------------

// Apply a token set to the current VanGuiStyle — fills all VanGuiStyle::Colors[].
VANGUI_API void             ApplyTokenSet(const VanThemeTokenSet& tokens);

// Extract a token set from the current VanGuiStyle (reverse-map best-fit).
VANGUI_API VanThemeTokenSet ExtractTokenSet();

// ---------------------------------------------------------------------------
// Built-in token sets
// ---------------------------------------------------------------------------

// Return a pre-built token set that matches the named preset.
VANGUI_API VanThemeTokenSet GetBuiltinTokenSet(VanThemeID theme);

// ---------------------------------------------------------------------------
// Animated transitions
// ---------------------------------------------------------------------------

// Begin an animated transition to a new theme over duration_ms milliseconds.
// Interpolates all VanGuiStyle::Colors[] entries per-frame.
VANGUI_API void TransitionToTheme(VanThemeID target, float duration_ms = 300.0f);
VANGUI_API void TransitionToTheme(const char* name,  float duration_ms = 300.0f);
VANGUI_API void TransitionToTokenSet(const VanThemeTokenSet& target, float duration_ms = 300.0f);

// Must be called once per frame (before Begin or RenderNotifications).
// Applies the current interpolated style. No-op when no transition is active.
VANGUI_API void RenderThemeTransition();

// True while a transition is playing.
VANGUI_API bool IsThemeTransitioning();

// ---------------------------------------------------------------------------
// Per-scope token overrides
// ---------------------------------------------------------------------------

// Push a token color override for subsequent widgets in this scope.
// Internally calls VanGui::PushStyleColor for all VanGuiCol_* entries
// that map to this token.
VANGUI_API void PushThemeToken(VanThemeToken token, VanVec4 color);
VANGUI_API void PopThemeToken(int count = 1);

// Push/pop a complete token set override.
VANGUI_API void PushThemeTokenSet(const VanThemeTokenSet& tokens);
VANGUI_API void PopThemeTokenSet();

// ---------------------------------------------------------------------------
// Hot-reload
// ---------------------------------------------------------------------------

// Watch a theme file. If its modification time changes, automatically reload it.
// Pass nullptr to stop watching.
VANGUI_API void WatchThemeFile(const char* filepath);

// Poll for file changes — call once per frame alongside RenderThemeTransition().
// When a change is detected, reloads and optionally transitions.
// transition_ms = 0 means instant apply (no animation).
VANGUI_API void PollThemeFileChanges(float transition_ms = 150.0f);

} // namespace VanGui

#endif // VANGUI_THEME_ENGINE_H
