// vangui_hex_editor.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — hex editor / memory viewer.
//
// Address / hex / ASCII columns, virtualized via VanGuiListClipper, optional
// click-to-edit and a highlight range. The buffer is caller-owned; the editor
// holds only tiny cursor state. Opt-in / zero-cost via VANGUI_ENABLE_HEX_EDITOR.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

#ifdef VANGUI_ENABLE_HEX_EDITOR

struct VanHexEditor
{
    bool   ReadOnly     = false;
    int    Cols         = 16;
    bool   ShowAscii    = true;
    bool   ShowAddress  = true;
    size_t HighlightMin = (size_t)-1;   // inclusive
    size_t HighlightMax = (size_t)-1;   // exclusive

    // Body only — call inside a window/child you opened.
    void DrawContents(void* mem, size_t size, size_t base_display_addr = 0);
    // Convenience full window.
    void Draw(const char* title, void* mem, size_t size, bool* p_open = nullptr,
              size_t base_display_addr = 0);

    // internal edit cursor (do not set directly)
    size_t EditingAddr = (size_t)-1;
    bool   EditTakeFocus = false;
};

#else // ------------------------------- shim ----------------------------------

struct VanHexEditor
{
    bool   ReadOnly = false;
    int    Cols = 16;
    bool   ShowAscii = true;
    bool   ShowAddress = true;
    size_t HighlightMin = (size_t)-1;
    size_t HighlightMax = (size_t)-1;
    void DrawContents(void*, size_t, size_t = 0) {}
    void Draw(const char*, void*, size_t, bool* = nullptr, size_t = 0) {}
};

#endif // VANGUI_ENABLE_HEX_EDITOR

} // namespace VanGui
