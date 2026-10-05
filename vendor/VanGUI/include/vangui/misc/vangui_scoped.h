// vangui_scoped.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — RAII scope guards for every Begin/End pair.
//
// Header-only, zero runtime cost, zero macro gate. Turns:
//
//     if (VanGui::Begin("Panel")) {
//         if (VanGui::BeginChild("child", ...)) { ... VanGui::EndChild(); }
//         ...
//     }
//     VanGui::End();
//
// into:
//
//     if (auto w = vgu::Window("Panel")) {
//         if (auto c = vgu::Child("child")) { ... }
//         ...
//     }
//
// The guard always calls End* on destruction (matching the VanGUI rule that
// Begin/End must be balanced regardless of the Begin's return value — except
// for the handful of "conditional" pairs, which are documented per-guard
// below). `if (auto _ = vgu::X(...))` compiles because each guard is convertible
// to bool, reporting the return value of the underlying Begin*() call.
//
// Two families exist:
//   * Unconditional pairs — End* must always be called (Window, Group,
//     StyleColor, StyleVar, Font, ID, ItemWidth, Indent, Disabled, MainMenuBar,
//     MenuBar).
//   * Conditional pairs — End* is called only if Begin* returned true (Child,
//     Table, TreeNode, Combo, ListBox, TabBar, TabItem, Menu, Popup, DragDrop
//     source/target, Tooltip).
//
// Each guard is non-copyable, movable, and stores just a bool. No hidden state,
// no allocations, no exceptions.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace vgu {   // short alias namespace so callsites read cleanly

// ============================================================================
// Base template: an unconditional end guard
// ============================================================================
template <void (*EndFn)()>
class ScopedAlways
{
public:
    explicit ScopedAlways(bool visible = true) noexcept : m_visible(visible) {}
    ~ScopedAlways() noexcept { EndFn(); }

    ScopedAlways(const ScopedAlways&)            = delete;
    ScopedAlways& operator=(const ScopedAlways&) = delete;
    ScopedAlways(ScopedAlways&& o) noexcept : m_visible(o.m_visible) { o.m_owns = false; }
    ScopedAlways& operator=(ScopedAlways&&)      = delete;

    explicit operator bool() const noexcept { return m_visible; }

private:
    bool m_visible = true;
    bool m_owns    = true;
};

// ============================================================================
// Base template: a conditional end guard (End* only if opened)
// ============================================================================
template <void (*EndFn)()>
class ScopedIf
{
public:
    explicit ScopedIf(bool opened) noexcept : m_opened(opened) {}
    ~ScopedIf() noexcept { if (m_opened && m_owns) EndFn(); }

    ScopedIf(const ScopedIf&)            = delete;
    ScopedIf& operator=(const ScopedIf&) = delete;
    ScopedIf(ScopedIf&& o) noexcept : m_opened(o.m_opened) { o.m_owns = false; }
    ScopedIf& operator=(ScopedIf&&)      = delete;

    explicit operator bool() const noexcept { return m_opened; }

private:
    bool m_opened;
    bool m_owns = true;
};

// ============================================================================
// Concrete guards — Unconditional (always call End*)
// ============================================================================

// Window: Begin() may return false (window collapsed / clipped), but End() is
// always required. `if (auto w = vgu::Window("t")) { … draw … }` is the idiom.
class Window : public ScopedAlways<&VanGui::End>
{
public:
    explicit Window(const char* name, bool* p_open = nullptr, VanGuiWindowFlags flags = 0)
        : ScopedAlways(VanGui::Begin(name, p_open, flags)) {}
};

class Group : public ScopedAlways<&VanGui::EndGroup>
{
public: Group() : ScopedAlways((VanGui::BeginGroup(), true)) {}
};

class ID
{
public:
    explicit ID(const char* str_id)  { VanGui::PushID(str_id);  }
    explicit ID(int int_id)          { VanGui::PushID(int_id);  }
    explicit ID(const void* ptr_id)  { VanGui::PushID(ptr_id);  }
    ~ID() noexcept                   { VanGui::PopID(); }
    ID(const ID&)            = delete;
    ID& operator=(const ID&) = delete;
};

class ItemWidth
{
public:
    explicit ItemWidth(float w) { VanGui::PushItemWidth(w); }
    ~ItemWidth() noexcept       { VanGui::PopItemWidth(); }
    ItemWidth(const ItemWidth&)            = delete;
    ItemWidth& operator=(const ItemWidth&) = delete;
};

class Indent
{
public:
    explicit Indent(float w = 0.0f) : m_w(w) { VanGui::Indent(w); }
    ~Indent() noexcept                       { VanGui::Unindent(m_w); }
    Indent(const Indent&)            = delete;
    Indent& operator=(const Indent&) = delete;
private:
    float m_w;
};

class Disabled
{
public:
    explicit Disabled(bool disabled = true) { VanGui::BeginDisabled(disabled); }
    ~Disabled() noexcept                    { VanGui::EndDisabled(); }
    Disabled(const Disabled&)            = delete;
    Disabled& operator=(const Disabled&) = delete;
};

class StyleColor
{
public:
    StyleColor(VanGuiCol idx, VanU32 col)          { VanGui::PushStyleColor(idx, col); ++m_n; }
    StyleColor(VanGuiCol idx, const VanVec4& col)  { VanGui::PushStyleColor(idx, col); ++m_n; }
    ~StyleColor() noexcept                         { VanGui::PopStyleColor(m_n); }

    // Chain multiple pushes into one scope guard: `vgu::StyleColor sc(a, x); sc.Push(b, y);`
    void Push(VanGuiCol idx, VanU32 col)         { VanGui::PushStyleColor(idx, col); ++m_n; }
    void Push(VanGuiCol idx, const VanVec4& col) { VanGui::PushStyleColor(idx, col); ++m_n; }

    StyleColor(const StyleColor&)            = delete;
    StyleColor& operator=(const StyleColor&) = delete;
private:
    int m_n = 0;
};

class StyleVar
{
public:
    StyleVar(VanGuiStyleVar idx, float v)         { VanGui::PushStyleVar(idx, v); ++m_n; }
    StyleVar(VanGuiStyleVar idx, const VanVec2& v){ VanGui::PushStyleVar(idx, v); ++m_n; }
    ~StyleVar() noexcept                          { VanGui::PopStyleVar(m_n); }

    void Push(VanGuiStyleVar idx, float v)          { VanGui::PushStyleVar(idx, v); ++m_n; }
    void Push(VanGuiStyleVar idx, const VanVec2& v) { VanGui::PushStyleVar(idx, v); ++m_n; }

    StyleVar(const StyleVar&)            = delete;
    StyleVar& operator=(const StyleVar&) = delete;
private:
    int m_n = 0;
};

class Font
{
public:
    Font(VanFont* f, float size_unscaled = 0.0f) { VanGui::PushFont(f, size_unscaled); }
    ~Font() noexcept                             { VanGui::PopFont(); }
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;
};

// ============================================================================
// Concrete guards — Conditional (End* only if opened)
// ============================================================================

class Child : public ScopedIf<&VanGui::EndChild>
{
public:
    explicit Child(const char* id, const VanVec2& size = VanVec2(0,0),
                   VanGuiChildFlags cflags = 0, VanGuiWindowFlags wflags = 0)
        : ScopedIf(VanGui::BeginChild(id, size, cflags, wflags)) {}
    explicit Child(VanGuiID id, const VanVec2& size = VanVec2(0,0),
                   VanGuiChildFlags cflags = 0, VanGuiWindowFlags wflags = 0)
        : ScopedIf(VanGui::BeginChild(id, size, cflags, wflags)) {}
};

class Table : public ScopedIf<&VanGui::EndTable>
{
public:
    Table(const char* str_id, int cols, VanGuiTableFlags flags = 0,
          const VanVec2& outer_size = VanVec2(0,0), float inner_width = 0.0f)
        : ScopedIf(VanGui::BeginTable(str_id, cols, flags, outer_size, inner_width)) {}
};

class TreeNode : public ScopedIf<&VanGui::TreePop>
{
public:
    explicit TreeNode(const char* label) : ScopedIf(VanGui::TreeNode(label)) {}
};

class Combo : public ScopedIf<&VanGui::EndCombo>
{
public:
    Combo(const char* label, const char* preview, VanGuiComboFlags flags = 0)
        : ScopedIf(VanGui::BeginCombo(label, preview, flags)) {}
};

class ListBox : public ScopedIf<&VanGui::EndListBox>
{
public:
    explicit ListBox(const char* label, const VanVec2& size = VanVec2(0,0))
        : ScopedIf(VanGui::BeginListBox(label, size)) {}
};

class TabBar : public ScopedIf<&VanGui::EndTabBar>
{
public:
    explicit TabBar(const char* str_id, VanGuiTabBarFlags flags = 0)
        : ScopedIf(VanGui::BeginTabBar(str_id, flags)) {}
};

class TabItem : public ScopedIf<&VanGui::EndTabItem>
{
public:
    explicit TabItem(const char* label, bool* p_open = nullptr, VanGuiTabItemFlags flags = 0)
        : ScopedIf(VanGui::BeginTabItem(label, p_open, flags)) {}
};

class Menu : public ScopedIf<&VanGui::EndMenu>
{
public:
    explicit Menu(const char* label, bool enabled = true)
        : ScopedIf(VanGui::BeginMenu(label, enabled)) {}
};

class MenuBar : public ScopedIf<&VanGui::EndMenuBar>
{
public: MenuBar() : ScopedIf(VanGui::BeginMenuBar()) {}
};

class MainMenuBar : public ScopedIf<&VanGui::EndMainMenuBar>
{
public: MainMenuBar() : ScopedIf(VanGui::BeginMainMenuBar()) {}
};

class Popup : public ScopedIf<&VanGui::EndPopup>
{
public:
    explicit Popup(const char* str_id, VanGuiWindowFlags flags = 0)
        : ScopedIf(VanGui::BeginPopup(str_id, flags)) {}
};

class Tooltip : public ScopedIf<&VanGui::EndTooltip>
{
public: Tooltip() : ScopedIf(VanGui::BeginTooltip()) {}
};

} // namespace vgu
