// vangui_vui.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — declarative .vui UI loader (with hot-reload).
// Parse a UI description into a node tree once, then render it each frame and
// bind its inputs to your variables. Designers/scripters edit UI without
// recompiling. Opt-in via VANGUI_ENABLE_VUI.
//
//   VanVuiDoc  doc;  LoadVui("panel.vui", doc);
//   VanVuiContext ctx;
//   ctx.bind("vsync", &cfg.vsync);
//   ctx.bind("volume", &cfg.volume);
//   ... each frame:
//   RenderVui(doc, ctx);
//   if (ctx.clicked("save")) save();
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include <memory>
#include <vector>
#include <string>
#include <cstring>

namespace VanGui {

#ifdef VANGUI_ENABLE_VUI

class VanVuiDoc
{
public:
    VanVuiDoc();
    ~VanVuiDoc();
    VanVuiDoc(VanVuiDoc&&) noexcept;
    VanVuiDoc& operator=(VanVuiDoc&&) noexcept;
    [[nodiscard]] bool loaded() const;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class VanVuiContext
{
public:
    void bind(const char* name, bool* p)  { b_.push_back({ name, Kind::B, p }); }
    void bind(const char* name, float* p) { b_.push_back({ name, Kind::F, p }); }
    void bind(const char* name, int* p)   { b_.push_back({ name, Kind::I, p }); }

    [[nodiscard]] bool clicked(const char* id) const
    { for (auto& c : clicked_) if (c == id) return true; return false; }

    // internal use by RenderVui
    enum class Kind { B, F, I };
    struct B { const char* name; Kind kind; void* ptr; };
    std::vector<B> b_;
    std::vector<std::string> clicked_;
    void* find(const char* name, Kind k) const
    { for (auto& e : b_) if (e.kind == k && std::strcmp(e.name, name) == 0) return e.ptr; return nullptr; }
};

// Parse (returns false + *err on failure; err points to static-lifetime text).
VANGUI_API bool LoadVuiFromMemory(const char* text, size_t len, VanVuiDoc& out, const char** err = nullptr);
VANGUI_API bool LoadVui(const char* path, VanVuiDoc& out, const char** err = nullptr);

// Render the document; clears + repopulates ctx.clicked_ for this frame.
VANGUI_API void RenderVui(VanVuiDoc& doc, VanVuiContext& ctx);

// Hot-reload: watch a .vui file; poll once per frame to reparse on change.
VANGUI_API void WatchVui(const char* path, VanVuiDoc& doc);
VANGUI_API void PollVuiChanges();

#else // ----------------------------- shims -----------------------------------

class VanVuiDoc { public: bool loaded() const { return false; } };
class VanVuiContext {
public:
    void bind(const char*, bool*) {} void bind(const char*, float*) {} void bind(const char*, int*) {}
    bool clicked(const char*) const { return false; }
};
inline bool LoadVuiFromMemory(const char*, size_t, VanVuiDoc&, const char** = nullptr) { return false; }
inline bool LoadVui(const char*, VanVuiDoc&, const char** = nullptr) { return false; }
inline void RenderVui(VanVuiDoc&, VanVuiContext&) {}
inline void WatchVui(const char*, VanVuiDoc&) {}
inline void PollVuiChanges() {}

#endif // VANGUI_ENABLE_VUI

} // namespace VanGui
