// vangui_toolbar.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — declarative toolbar with automatic overflow.
//
// You describe the toolbar as a sequence of calls (Button / Toggle / Sep /
// Item), and the module renders as many items as fit in the current content
// width, hiding the remainder behind a ">>" popup. Two-phase layout: a first
// pass measures widths, a second pass renders. State lives inside the module
// (per Toolbar id), keyed by the string id you pass to Begin/End.
//
// USAGE
//   if (VanGui::BeginMenuBar()) {
//       vgu::Toolbar tb("main");
//       tb.Button("File",  []{ ... });
//       tb.Button("Edit",  []{ ... });
//       tb.Sep();
//       tb.Toggle("Grid", &grid_on);
//       tb.Render();   // measures + draws + adds ">>" popup if needed
//       VanGui::EndMenuBar();
//   }
//
// Opt-in via VANGUI_ENABLE_TOOLBAR. Empty TU otherwise.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace vgu {

// Item kinds recorded during the "describe" phase.
enum class ToolbarKind : unsigned char { Button, Toggle, Sep, Text };

#ifdef VANGUI_ENABLE_TOOLBAR

using ToolbarFn = void(*)(void*);

struct ToolbarItem
{
    ToolbarKind Kind;
    const char* Label;    // not copied; must outlive Render()
    ToolbarFn   Run;      // for Button
    void*       User;
    bool*       BoolPtr;  // for Toggle
    float       Width;    // measured after first Render() call
};

class Toolbar
{
public:
    // `id` selects a persistent measurement cache slot (see Render()).
    explicit VANGUI_API Toolbar(const char* id);

    VANGUI_API void Button(const char* label, ToolbarFn run, void* user = nullptr);
    VANGUI_API void Toggle(const char* label, bool* v);
    VANGUI_API void Text  (const char* text);
    VANGUI_API void Sep   ();

    // Draws items until content width is exhausted, then folds the rest into a
    // ">>" popup. Idempotent within a frame; safe to call multiple times only
    // for measurement in debug — production use is one call per frame.
    VANGUI_API void Render();

private:
    const char* m_id;
};

#else  // zero-cost shims -----------------------------------------------------

using ToolbarFn = void(*)(void*);

class Toolbar
{
public:
    explicit Toolbar(const char*) {}
    void Button(const char*, ToolbarFn, void* = nullptr) {}
    void Toggle(const char*, bool*)                      {}
    void Text  (const char*)                             {}
    void Sep   ()                                        {}
    void Render()                                        {}
};

#endif // VANGUI_ENABLE_TOOLBAR

} // namespace vgu
