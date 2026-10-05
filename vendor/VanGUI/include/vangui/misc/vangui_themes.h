#pragma once

// vangui_themes.h
// Named theme preset system with INI-like serialization for VanGUI.
// See vangui_themes.cpp for implementation.

#ifndef VANGUI_THEMES_H
#define VANGUI_THEMES_H

#include <vangui/vangui.h>

namespace VanGui
{
    // Enumeration of all built-in named themes.
    enum VanThemeID
    {
        VanTheme_Dark = 0,      // VanGUI default dark
        VanTheme_Light,         // VanGUI default light
        VanTheme_Classic,       // VanGUI classic
        VanTheme_Dracula,       // purple/dark
        VanTheme_Nord,          // arctic blue-grey
        VanTheme_Monokai,       // dark + vivid accents
        VanTheme_GruvboxDark,   // warm retro dark
        VanTheme_COUNT
    };

    // Apply a built-in named theme to the current context's style.
    VANGUI_API void        LoadTheme(VanThemeID theme);
    VANGUI_API void        LoadTheme(const char* name); // "dark", "light", "classic", "dracula", "nord", "monokai", "gruvbox_dark"

    // Return the canonical name string for a theme ID.
    VANGUI_API const char* GetThemeName(VanThemeID theme);

    // Serialize the current VanGuiStyle to a heap-allocated INI-like text buffer.
    // The returned buffer is allocated with VanGui's internal allocator.
    // Free it with VanGui::MemFree() when done.
    // out_size receives the byte count (not including the null terminator).
    VANGUI_API char*       SaveThemeToMemory(size_t* out_size);

    // Parse a style from the INI-like text produced by SaveThemeToMemory and apply it
    // to the current context's style.  Returns true on success.
    VANGUI_API bool        LoadThemeFromMemory(const char* data, size_t data_size);

    // Convenience file I/O wrappers around the memory functions above.
    VANGUI_API void        SaveThemeToFile(const char* filename);
    VANGUI_API bool        LoadThemeFromFile(const char* filename);

} // namespace VanGui

#endif // VANGUI_THEMES_H
