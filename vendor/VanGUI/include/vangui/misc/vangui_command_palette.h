// VanGUI: command palette widget (Ctrl+K style)
// Usage:
//   1. Include this header and the corresponding .cpp in your project.
//   2. Register commands once at startup (or whenever your command set changes):
//        VanGui::RegisterCommand({ "File: Open", "open load import", MyOpenCallback, nullptr });
//   3. Call VanGui::RenderCommandPalette() once per frame, after VanGui::NewFrame().
//      The palette opens automatically on Ctrl+K, or you can call OpenCommandPalette().
//
// No STL dependencies. Fixed-capacity arrays (512 commands max).

#pragma once

#ifndef VANGUI_DISABLE

#include <vangui/vangui.h>

namespace VanGui
{
    // A single registered command.
    struct VanCommand
    {
        const char* Name;       // Display name shown in the palette, e.g. "File: Open"
        const char* Keywords;   // Extra search terms, e.g. "open load import" (may be nullptr)
        void (*Action)(void*);  // Callback invoked when the command is selected
        void* UserData;         // Passed verbatim to Action
    };

    // Register a command. Commands persist until ClearCommands() is called.
    // Registering more than 512 commands triggers VAN_ASSERT.
    VANGUI_API void RegisterCommand(const VanCommand& cmd);

    // Remove all registered commands.
    VANGUI_API void ClearCommands();

    // Programmatically open or close the palette.
    VANGUI_API void OpenCommandPalette();
    VANGUI_API void CloseCommandPalette();

    // Returns true if the palette is currently open.
    [[nodiscard]] VANGUI_API bool IsCommandPaletteOpen();

    // Call once per frame from your main loop, AFTER VanGui::NewFrame().
    // Automatically opens on Ctrl+K. Renders the palette popup if open.
    VANGUI_API void RenderCommandPalette();

} // namespace VanGui

#endif // #ifndef VANGUI_DISABLE
