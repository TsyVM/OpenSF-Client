// vangui_console.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — log / console panel.
//
// A retained, filterable, virtualized log view. The VanConsole object owns its
// buffer (the caller decides its scope/lifetime); every draw is O(visible rows)
// via VanGuiListClipper. Opt-in / zero-cost via VANGUI_ENABLE_CONSOLE.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include <cstdarg>

namespace VanGui {

enum VanLogLevel : int
{
    VanLog_Trace = 0, VanLog_Debug, VanLog_Info, VanLog_Warn, VanLog_Error,
    VanLog_COUNT
};

#ifdef VANGUI_ENABLE_CONSOLE

class VanConsole
{
public:
    VanConsole();
    ~VanConsole();

    void Clear();
    void AddLog(VanLogLevel level, const char* fmt, ...);
    void AddLogV(VanLogLevel level, const char* fmt, va_list args);

    // Full window (with menu bar: clear / copy / autoscroll / level filter).
    void Draw(const char* title, bool* p_open = nullptr);
    // Body only — embed inside a window/child you already opened.
    void DrawContents();

    bool  AutoScroll   = true;
    bool  ShowLevelMask[VanLog_COUNT] = { true, true, true, true, true };
    float RowMinHeight = 0.0f;   // 0 => text line height

private:
    struct Line { int level; int offset; int size; };   // offset/size into Buf_
    void* Impl_;   // opaque (holds text buffer + line index + filter)
};

#else // ------------------------------- shim ----------------------------------

class VanConsole
{
public:
    void Clear() {}
    void AddLog(VanLogLevel, const char*, ...) {}
    void Draw(const char*, bool* = nullptr) {}
    void DrawContents() {}
    bool AutoScroll = true;
};

#endif // VANGUI_ENABLE_CONSOLE

} // namespace VanGui
