#pragma once

// vangui_theme_editor.h
// Embedded panel and floating window for live-editing VanGUI semantic theme tokens,
// switching named presets, and importing/exporting theme files.
// See vangui_theme_editor.cpp for implementation.

#ifndef VANGUI_THEME_EDITOR_H
#define VANGUI_THEME_EDITOR_H

#include <vangui/vangui.h>
// Forward-include the engine header so users only need one include.
#include "vangui_theme_engine.h"

namespace VanGui
{
    // Render the theme editor as an embedded panel (no Begin/End — call inside
    // an existing VanGui window).
    VANGUI_API void ShowThemeEditor(bool* p_open = nullptr);

    // Render the theme editor as a standalone floating window.
    VANGUI_API void ShowThemeEditorWindow(bool* p_open = nullptr);

} // namespace VanGui

#endif // VANGUI_THEME_EDITOR_H
