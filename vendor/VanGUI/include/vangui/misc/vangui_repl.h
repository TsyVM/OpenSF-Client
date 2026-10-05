// vangui_repl.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — command console / REPL.
//
// A scrolling output area plus an input line with command history (Up/Down) and
// Tab completion. Pairs naturally with vangui_console / the actions registry.
// The VanRepl object owns its buffers. Opt-in / zero-cost via VANGUI_ENABLE_REPL.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include <cstdarg>

namespace VanGui {

typedef void (*VanReplExecFn)(const char* line, void* user_data);

#ifdef VANGUI_ENABLE_REPL

class VanRepl
{
public:
    VanRepl();
    ~VanRepl();

    void AddOutput(const char* fmt, ...);
    void AddOutputV(const char* fmt, va_list args);
    void Clear();
    // Words used for Tab completion (pointer must stay valid until replaced).
    void SetCompletions(const char* const* words, int count);

    // Draw the console. `on_exec` is called with each entered line.
    void Draw(const char* id, VanVec2 size, VanReplExecFn on_exec, void* user_data);

private:
    void* Impl_;
};

#else // ------------------------------- shim ----------------------------------

class VanRepl
{
public:
    void AddOutput(const char*, ...) {}
    void Clear() {}
    void SetCompletions(const char* const*, int) {}
    void Draw(const char*, VanVec2, VanReplExecFn, void*) {}
};

#endif // VANGUI_ENABLE_REPL

} // namespace VanGui
