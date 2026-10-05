// van.h  —  VanGUI fluent facade ("one include is all you need")
// =============================================================================
// The ergonomic, scriptable front door to VanGUI. Inspired by the VanHooks API
// style: a single include, a single short namespace (`van`), RAII lifetime for
// every scope, std::expected for fallible calls, designated-initializer option
// structs, and method chaining on widget results.
//
//   #include "van.h"
//   using namespace van;
//
//   if (auto w = window("Settings", { .size = {420, 300} })) {
//       row([&]{
//           button("Save", { .primary = true }).on_click(save);
//           button("Cancel").on_click([&]{ open = false; });
//       });
//       checkbox("V-Sync", cfg.vsync);
//       slider("Volume", cfg.volume, 0.0f, 100.0f).tooltip("Master volume");
//   }
//
// This is a thin, header-only layer over the existing VanGUI API and the
// enhancement pillars. It adds ZERO new state of its own beyond a tiny layout
// cursor stack, allocates nothing per frame, and compiles away to direct VanGui
// calls. It deliberately does not reimplement any widget — it wraps them.
//
// Optional integrations light up automatically when you compiled the matching
// module (their enable macro is defined): toasts (notify), file pickers and
// message boxes (dialogs). std::expected helpers appear when <expected> exists.
// =============================================================================

#pragma once

#include "vangui.h"

#if defined(__cpp_lib_expected) && __has_include(<expected>)
#  include <expected>
#  define VAN_HAS_EXPECTED 1
#endif

// Pull in the optional pillar headers whose integrations we expose below.
#ifdef VANGUI_ENABLE_NOTIFY
#  include "misc/vangui_notify.h"
#endif
#ifdef VANGUI_ENABLE_DIALOGS
#  include "misc/vangui_dialogs.h"
#endif
// Header-only utilities (available whenever their headers are present).
#if __has_include("misc/vangui_reflect.h")
#  include "misc/vangui_reflect.h"
#endif
#if __has_include("misc/vangui_undo.h")
#  include "misc/vangui_undo.h"
#endif
#if __has_include("misc/vangui_settings.h")
#  include "misc/vangui_settings.h"
#endif
#if __has_include("misc/vangui_theme_gen.h") && defined(VANGUI_ENABLE_THEME_ENGINE)
#  include "misc/vangui_theme_gen.h"
#endif
// Compiled extension modules light up their facade wrappers when enabled.
#ifdef VANGUI_ENABLE_WIDGETS_EXT
#  include "misc/vangui_widgets_ext.h"
#endif
#ifdef VANGUI_ENABLE_CHARTS
#  include "misc/vangui_charts.h"
#endif
#ifdef VANGUI_ENABLE_FEEDBACK
#  include "misc/vangui_feedback.h"
#endif
#ifdef VANGUI_ENABLE_LOADING
#  include "misc/vangui_loading.h"
#endif
#ifdef VANGUI_ENABLE_ICONS
#  include "misc/vangui_icons.h"
#endif
#ifdef VANGUI_ENABLE_GALLERY
#  include "misc/vangui_gallery.h"
#endif
#ifdef VANGUI_ENABLE_FORMS
#  include "misc/vangui_forms.h"
#endif
#ifdef VANGUI_ENABLE_PANELS
#  include "misc/vangui_panels.h"
#endif
#ifdef VANGUI_ENABLE_VIEWS
#  include "misc/vangui_views.h"
#endif
#ifdef VANGUI_ENABLE_THEMES
#  include "misc/vangui_themes.h"
#endif
#ifdef VANGUI_ENABLE_NODE_GRAPH
#  include "misc/vangui_node_graph.h"
#endif
#ifdef VANGUI_ENABLE_THEME_EDITOR
#  include "misc/vangui_theme_editor.h"
#endif
#ifdef VANGUI_ENABLE_THREAD
#  include "misc/vangui_thread.h"
#endif

// --- Tier 1-3 tooling & application modules -------------------------------
#ifdef VANGUI_ENABLE_CONSOLE
#  include "misc/vangui_console.h"
#endif
#ifdef VANGUI_ENABLE_PROPERTY_GRID
#  include "misc/vangui_property_grid.h"
#endif
#ifdef VANGUI_ENABLE_CURVE_EDITOR
#  include "misc/vangui_curve_editor.h"
#endif
#ifdef VANGUI_ENABLE_DATETIME
#  include "misc/vangui_datetime.h"
#endif
#ifdef VANGUI_ENABLE_MARKDOWN
#  include "misc/vangui_markdown.h"
#endif
#ifdef VANGUI_ENABLE_HEX_EDITOR
#  include "misc/vangui_hex_editor.h"
#endif
#ifdef VANGUI_ENABLE_PLOT
#  include "misc/vangui_plot.h"
#endif
#ifdef VANGUI_ENABLE_TIMELINE
#  include "misc/vangui_timeline.h"
#endif
#ifdef VANGUI_ENABLE_DATA_GRID
#  include "misc/vangui_data_grid.h"
#endif
#ifdef VANGUI_ENABLE_BANNERS
#  include "misc/vangui_banners.h"
#endif
#ifdef VANGUI_ENABLE_REPL
#  include "misc/vangui_repl.h"
#endif
#ifdef VANGUI_ENABLE_RICHTEXT
#  include "misc/vangui_richtext.h"
#endif
#ifdef VANGUI_ENABLE_PERF_HUD
#  include "misc/vangui_perf_hud.h"
#endif
#if __has_include("misc/vangui_reorder.h")
#  include "misc/vangui_reorder.h"
#endif
#if __has_include("misc/vangui_i18n.h")
#  include "misc/vangui_i18n.h"
#endif
// Motion switches and transitions, everyday widgets, formatting. Always included:
// without their modules they fall back to inline shims (content drawn plainly).
#if __has_include("misc/vangui_anim.h")
#  include "misc/vangui_anim.h"
#  define VAN_HAS_ANIM_HEADER 1
#endif
#if __has_include("misc/vangui_extras.h")
#  include "misc/vangui_extras.h"
#  define VAN_HAS_EXTRAS_HEADER 1
#endif
#if __has_include("misc/vangui_format.h")
#  include "misc/vangui_format.h"
#  define VAN_HAS_FORMAT_HEADER 1
#endif
#if __has_include("misc/vangui_touch.h")
#  include "misc/vangui_touch.h"
#  define VAN_HAS_TOUCH_HEADER 1
#endif
#if __has_include("misc/vangui_gizmo.h")
#  include "misc/vangui_gizmo.h"
#  define VAN_HAS_GIZMO_HEADER 1
#endif

#if __has_include("misc/cpp/vangui_stdlib.h")
#  include "misc/cpp/vangui_stdlib.h"   // VanGui::InputText(const char*, std::string*)
#  define VAN_HAS_STD_STRING 1
#  include <string>
#endif

namespace van {

// --- type aliases (use these or the Van* originals interchangeably) ----------
using Vec2  = VanVec2;
using Vec4  = VanVec4;
using Color = VanColor;
using Id    = VanGuiID;

// ===========================================================================
// Layout cursor — lets row()/column() auto-arrange van:: widgets with no manual
// SameLine. Only van:: widgets participate (they call lay_item()); raw VanGui::
// calls are unaffected.
// ===========================================================================
namespace detail {

struct LayFrame { bool row; bool first; };
inline LayFrame g_lay[32];
inline int      g_layN = 0;

inline void lay_push(bool row) { if (g_layN < 32) g_lay[g_layN++] = { row, true }; }
inline void lay_pop()          { if (g_layN > 0)  --g_layN; }
inline void lay_item()
{
    if (g_layN == 0) return;
    LayFrame& f = g_lay[g_layN - 1];
    if (f.row && !f.first) VanGui::SameLine();
    f.first = false;
}

inline void pop_color()  { VanGui::PopStyleColor(1); }
inline void pop_var()    { VanGui::PopStyleVar(1); }
inline void pop_indent() { VanGui::Unindent(); }

} // namespace detail

// ===========================================================================
// Scope — one movable RAII guard for every Begin/End and Push/Pop pair.
// `if (auto s = van::window(...)) { ... }` — the bool is the open/visible state.
// ===========================================================================
class Scope
{
public:
    Scope() = default;
    Scope(bool open, void (*end)(), bool always_end)
        : end_(end), open_(open), always_(always_end), active_(true) {}

    Scope(Scope&& o) noexcept { steal(o); }
    Scope& operator=(Scope&& o) noexcept { if (this != &o) { close(); steal(o); } return *this; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { close(); }

    explicit operator bool() const { return open_; }
    [[nodiscard]] bool open() const { return open_; }

    // Run a body only when the scope is open; the scope still closes via RAII.
    template <class F> Scope& then(F&& f) { if (open_) f(); return *this; }

    void close() noexcept
    {
        if (active_ && end_ && (open_ || always_)) end_();
        active_ = false;
    }

private:
    void steal(Scope& o) { end_ = o.end_; open_ = o.open_; always_ = o.always_; active_ = o.active_; o.active_ = false; }
    void (*end_)() = nullptr;
    bool open_ = false, always_ = false, active_ = false;
};

// ===========================================================================
// Containers (Begin/End scopes)
// ===========================================================================

struct WindowOpts
{
    Vec2   size{0, 0};            // 0 => auto / remembered
    Vec2   pos{0, 0};            // (0,0) => leave to VanGui
    bool   has_pos = false;
    bool*  p_open  = nullptr;     // optional close button -> sets *p_open=false
    VanGuiWindowFlags flags = 0;
    VanGuiCond cond = VanGuiCond_FirstUseEver;
};

inline Scope window(const char* title, const WindowOpts& o = {})
{
    detail::lay_item();
    if (o.size.x != 0 || o.size.y != 0) VanGui::SetNextWindowSize(o.size, o.cond);
    if (o.has_pos)                       VanGui::SetNextWindowPos(o.pos, o.cond);
    const bool open = VanGui::Begin(title, o.p_open, o.flags);
    return Scope(open, &VanGui::End, /*always_end=*/true);   // End() is unconditional
}

struct ChildOpts { Vec2 size{0, 0}; VanGuiChildFlags child_flags = VanGuiChildFlags_Borders; VanGuiWindowFlags window_flags = 0; };
inline Scope child(const char* id, const ChildOpts& o = {})
{
    detail::lay_item();
    const bool open = VanGui::BeginChild(id, o.size, o.child_flags, o.window_flags);
    return Scope(open, &VanGui::EndChild, /*always_end=*/true);
}

inline Scope group()           { detail::lay_item(); VanGui::BeginGroup();  return Scope(true, &VanGui::EndGroup, true); }
inline Scope disabled(bool on = true) { VanGui::BeginDisabled(on); return Scope(true, &VanGui::EndDisabled, true); }

inline Scope popup(const char* id, VanGuiWindowFlags f = 0)
{ const bool open = VanGui::BeginPopup(id, f); return Scope(open, &VanGui::EndPopup, false); }

inline Scope modal(const char* title, bool* p_open = nullptr, VanGuiWindowFlags f = 0)
{ const bool open = VanGui::BeginPopupModal(title, p_open, f); return Scope(open, &VanGui::EndPopup, false); }

inline Scope menu(const char* label, bool enabled = true)
{ const bool open = VanGui::BeginMenu(label, enabled); return Scope(open, &VanGui::EndMenu, false); }

inline Scope menu_bar()        { const bool o = VanGui::BeginMenuBar();      return Scope(o, &VanGui::EndMenuBar, false); }
inline Scope main_menu_bar()   { const bool o = VanGui::BeginMainMenuBar();  return Scope(o, &VanGui::EndMainMenuBar, false); }
inline Scope tab_bar(const char* id, VanGuiTabBarFlags f = 0) { const bool o = VanGui::BeginTabBar(id, f); return Scope(o, &VanGui::EndTabBar, false); }
inline Scope tab(const char* label, bool* p_open = nullptr, VanGuiTabItemFlags f = 0) { const bool o = VanGui::BeginTabItem(label, p_open, f); return Scope(o, &VanGui::EndTabItem, false); }
inline Scope tooltip()         { const bool o = VanGui::BeginTooltip();      return Scope(o, &VanGui::EndTooltip, false); }
inline Scope combo(const char* label, const char* preview, VanGuiComboFlags f = 0) { const bool o = VanGui::BeginCombo(label, preview, f); return Scope(o, &VanGui::EndCombo, false); }
inline Scope list_box(const char* label, Vec2 size = {0, 0}) { const bool o = VanGui::BeginListBox(label, size); return Scope(o, &VanGui::EndListBox, false); }

struct TableOpts { VanGuiTableFlags flags = VanGuiTableFlags_Borders | VanGuiTableFlags_RowBg; Vec2 outer_size{0, 0}; float inner_width = 0.0f; };
inline Scope table(const char* id, int columns, const TableOpts& o = {})
{ detail::lay_item(); const bool open = VanGui::BeginTable(id, columns, o.flags, o.outer_size, o.inner_width); return Scope(open, &VanGui::EndTable, false); }

// --- table row/column helpers (wrap the nodiscard core calls) --------------
inline void table_setup(const char* label, VanGuiTableColumnFlags f = 0) { VanGui::TableSetupColumn(label, f); }
inline void table_headers() { VanGui::TableHeadersRow(); }
inline void table_row(float min_height = 0.0f) { VanGui::TableNextRow(0, min_height); }
inline bool table_col() { return VanGui::TableNextColumn(); }
inline bool table_col(int index) { return VanGui::TableSetColumnIndex(index); }

inline Scope tree(const char* label) { detail::lay_item(); const bool o = VanGui::TreeNode(label); return Scope(o, &VanGui::TreePop, false); }

// --- style / id / width / indent scopes ------------------------------------
inline Scope style_color(VanGuiCol idx, Vec4 col)  { VanGui::PushStyleColor(idx, col); return Scope(true, &detail::pop_color, true); }
inline Scope style_color(VanGuiCol idx, VanU32 col){ VanGui::PushStyleColor(idx, col); return Scope(true, &detail::pop_color, true); }
inline Scope style_var(VanGuiStyleVar idx, float v){ VanGui::PushStyleVar(idx, v);     return Scope(true, &detail::pop_var, true); }
inline Scope style_var(VanGuiStyleVar idx, Vec2 v) { VanGui::PushStyleVar(idx, v);     return Scope(true, &detail::pop_var, true); }
inline Scope id(const char* s) { VanGui::PushID(s); return Scope(true, &VanGui::PopID, true); }
inline Scope id(int i)         { VanGui::PushID(i); return Scope(true, &VanGui::PopID, true); }
inline Scope id(const void* p) { VanGui::PushID(p); return Scope(true, &VanGui::PopID, true); }
inline Scope item_width(float w) { VanGui::PushItemWidth(w); return Scope(true, &VanGui::PopItemWidth, true); }
inline Scope indent()            { VanGui::Indent();        return Scope(true, &detail::pop_indent, true); }


// ===========================================================================
// Response — fluent result of a widget. `clicked()/changed()` are the same bit
// (true the frame the widget fired); the chaining methods make call sites read
// like intent: button("Save").on_click(save).tooltip("write to disk").
// ===========================================================================
class Response
{
public:
    explicit Response(bool fired) : fired_(fired) {}

    [[nodiscard]] bool clicked() const { return fired_; }
    [[nodiscard]] bool changed() const { return fired_; }
    [[nodiscard]] bool hovered() const { return VanGui::IsItemHovered(); }
    [[nodiscard]] bool active()  const { return VanGui::IsItemActive(); }
    explicit operator bool() const { return fired_; }

    template <class F> const Response& on_click (F&& f) const { if (fired_)    f(); return *this; }
    template <class F> const Response& on_change(F&& f) const { if (fired_)    f(); return *this; }
    template <class F> const Response& on_hover (F&& f) const { if (hovered()) f(); return *this; }

    const Response& tooltip(const char* text) const
    {
        if (text && VanGui::IsItemHovered()) VanGui::SetTooltip("%s", text);
        return *this;
    }

private:
    bool fired_;
};

// ===========================================================================
// Widgets — every one calls lay_item() first so row()/column() can arrange it.
// References (not pointers) for bound values; designated-init option structs.
// ===========================================================================

struct ButtonOpts { Vec2 size{0, 0}; bool primary = false; bool disabled = false; const char* tooltip = nullptr; };

inline Response button(const char* label, const ButtonOpts& o = {})
{
    detail::lay_item();
    if (o.disabled) VanGui::BeginDisabled();
    int pushed = 0;
    if (o.primary)
    {
        const Vec4 acc = VanGui::GetStyleColorVec4(VanGuiCol_ButtonActive);
        VanGui::PushStyleColor(VanGuiCol_Button,        acc);
        VanGui::PushStyleColor(VanGuiCol_ButtonHovered, acc);
        VanGui::PushStyleColor(VanGuiCol_Text,          Vec4(1, 1, 1, 1));
        pushed = 3;
    }
    const bool c = VanGui::Button(label, o.size);
    if (pushed)     VanGui::PopStyleColor(pushed);
    if (o.disabled) VanGui::EndDisabled();
    Response r(c);
    r.tooltip(o.tooltip);
    return r;
}

inline Response small_button(const char* label) { detail::lay_item(); return Response(VanGui::SmallButton(label)); }

inline Response checkbox(const char* label, bool& v) { detail::lay_item(); return Response(VanGui::Checkbox(label, &v)); }

inline Response radio(const char* label, int& v, int btn)
{ detail::lay_item(); const bool c = VanGui::RadioButton(label, v == btn); if (c) v = btn; return Response(c); }

inline Response selectable(const char* label, bool selected = false, Vec2 size = {0, 0})
{ detail::lay_item(); return Response(VanGui::Selectable(label, selected, 0, size)); }

inline Response selectable(const char* label, bool& selected)
{ detail::lay_item(); return Response(VanGui::Selectable(label, &selected)); }

struct MenuItemOpts { const char* shortcut = nullptr; bool enabled = true; bool selected = false; };
inline Response menu_item(const char* label, const MenuItemOpts& o = {})
{ return Response(VanGui::MenuItem(label, o.shortcut, o.selected, o.enabled)); }
inline Response menu_item(const char* label, bool& checked, const char* shortcut = nullptr)
{ return Response(VanGui::MenuItem(label, shortcut, &checked)); }

inline Response slider(const char* label, float& v, float mn, float mx, const char* fmt = "%.3f")
{ detail::lay_item(); return Response(VanGui::SliderFloat(label, &v, mn, mx, fmt)); }
inline Response slider(const char* label, int& v, int mn, int mx)
{ detail::lay_item(); return Response(VanGui::SliderInt(label, &v, mn, mx)); }

inline Response drag(const char* label, float& v, float speed = 1.0f, float mn = 0.0f, float mx = 0.0f)
{ detail::lay_item(); return Response(VanGui::DragFloat(label, &v, speed, mn, mx)); }
inline Response drag(const char* label, int& v, float speed = 1.0f, int mn = 0, int mx = 0)
{ detail::lay_item(); return Response(VanGui::DragInt(label, &v, speed, mn, mx)); }

inline Response input(const char* label, float& v) { detail::lay_item(); return Response(VanGui::InputFloat(label, &v)); }
inline Response input(const char* label, int& v)   { detail::lay_item(); return Response(VanGui::InputInt(label, &v)); }
inline Response input_text(const char* label, char* buf, size_t cap, VanGuiInputTextFlags f = 0)
{ detail::lay_item(); return Response(VanGui::InputText(label, buf, cap, f)); }
#ifdef VAN_HAS_STD_STRING
inline Response input_text(const char* label, std::string& s, VanGuiInputTextFlags f = 0)
{ detail::lay_item(); return Response(VanGui::InputText(label, &s, f)); }
#endif

inline Response color_edit(const char* label, Vec4& c, VanGuiColorEditFlags f = 0)
{ detail::lay_item(); return Response(VanGui::ColorEdit4(label, &c.x, f)); }
inline Response color_edit(const char* label, Color& c, VanGuiColorEditFlags f = 0)
{ detail::lay_item(); return Response(VanGui::ColorEdit4(label, &c.Value.x, f)); }

inline Response combo(const char* label, int& current, const char* const items[], int count)
{ detail::lay_item(); return Response(VanGui::Combo(label, &current, items, count)); }
inline Response combo(const char* label, int& current, const char* zero_separated)
{ detail::lay_item(); return Response(VanGui::Combo(label, &current, zero_separated)); }

// --- text / static helpers --------------------------------------------------
inline void text(const char* s)                 { detail::lay_item(); VanGui::TextUnformatted(s); }
inline void text_colored(Vec4 col, const char* s){ detail::lay_item(); VanGui::PushStyleColor(VanGuiCol_Text, col); VanGui::TextUnformatted(s); VanGui::PopStyleColor(); }
inline void text_disabled(const char* s)         { detail::lay_item(); VanGui::TextDisabled("%s", s); }
inline void heading(const char* s)               { detail::lay_item(); VanGui::SeparatorText(s); }
inline void bullet(const char* s)                { detail::lay_item(); VanGui::BulletText("%s", s); }

// --- spacing / flow ---------------------------------------------------------
inline void separator()                  { VanGui::Separator(); }
inline void spacing()                    { VanGui::Spacing(); }
inline void same_line(float off = 0.0f, float spacing = -1.0f) { VanGui::SameLine(off, spacing); }
inline void new_line()                   { VanGui::NewLine(); }
inline void dummy(Vec2 size)             { detail::lay_item(); VanGui::Dummy(size); }

// --- layout lambdas (auto-arrange van:: widgets) ----------------------------
template <class F> void row(F&& f)
{ detail::lay_item(); VanGui::BeginGroup(); detail::lay_push(true);  f(); detail::lay_pop(); VanGui::EndGroup(); }
template <class F> void column(F&& f)
{ detail::lay_item(); VanGui::BeginGroup(); detail::lay_push(false); f(); detail::lay_pop(); VanGui::EndGroup(); }

// Fixed-column grid: calls f(i) for i in [0,count), wrapping every `columns`.
template <class F> void grid(int columns, int count, F&& f)
{
    if (columns < 1) columns = 1;
    for (int i = 0; i < count; ++i) { if (i % columns != 0) VanGui::SameLine(); f(i); }
}

// ===========================================================================
// std::expected helpers (appear only when <expected> is available)
// ===========================================================================
#ifdef VAN_HAS_EXPECTED
template <class T, class E, class F>
const std::expected<T, E>& on_value(const std::expected<T, E>& r, F&& f) { if (r)  f(*r);        return r; }
template <class T, class E, class F>
const std::expected<T, E>& on_error(const std::expected<T, E>& r, F&& f) { if (!r) f(r.error()); return r; }
template <class T, class E>
T value_or(const std::expected<T, E>& r, T fallback) { return r ? *r : fallback; }
#endif

// ===========================================================================
// Optional integrations — light up when the matching module was compiled.
// ===========================================================================
#ifdef VANGUI_ENABLE_NOTIFY
inline void toast_info   (const char* msg) { VanGui::NotifyInfo("%s", msg); }
inline void toast_success(const char* msg) { VanGui::NotifySuccess("%s", msg); }
inline void toast_warning(const char* msg) { VanGui::NotifyWarning("%s", msg); }
inline void toast_error  (const char* msg) { VanGui::NotifyError("%s", msg); }
#  if defined(VAN_HAS_EXPECTED)
template <class T, class E>
const std::expected<T, E>& toast_on_error(const std::expected<T, E>& r, const char* msg = "Operation failed")
{ if (!r) VanGui::NotifyError("%s", msg); return r; }
#  endif
#endif

#ifdef VANGUI_ENABLE_DIALOGS
inline void confirm(const char* title) { VanGui::OpenMessageBox(title); }
#endif



// ===========================================================================
// Extended widgets (when vangui_widgets_ext is compiled in). Reference-binding
// wrappers that also participate in row()/column() auto-layout.
// ===========================================================================
#ifdef VANGUI_ENABLE_WIDGETS_EXT
inline Response toggle(const char* label, bool& v)        { detail::lay_item(); return Response(VanGui::Toggle(label, &v)); }
inline Response segmented(const char* id, int& current, const char* const labels[], int count)
                                                          { detail::lay_item(); return Response(VanGui::SegmentedControl(id, &current, labels, count)); }
inline Response chip(const char* label, bool* p_open = nullptr) { detail::lay_item(); return Response(VanGui::Chip(label, p_open)); }
inline void     badge(const char* text, VanU32 col = 0)   { detail::lay_item(); VanGui::Badge(text, col); }
inline int      breadcrumb(const char* const items[], int count) { detail::lay_item(); return VanGui::Breadcrumb(items, count); }
inline Response stepper(const char* label, int& v, int step = 1) { detail::lay_item(); return Response(VanGui::Stepper(label, &v, step)); }
inline Response rating(const char* id, int& stars, int max_stars = 5) { detail::lay_item(); return Response(VanGui::StarRating(id, &stars, max_stars)); }
inline Response search(const char* id, char* buf, size_t cap, const char* hint = "Search", float width = 0.0f)
                                                          { detail::lay_item(); return Response(VanGui::SearchBox(id, buf, cap, hint, width)); }
#endif

#ifdef VANGUI_ENABLE_FEEDBACK
inline void     counter(const char* id, float value, const char* fmt = "%.0f") { detail::lay_item(); VanGui::AnimatedValue(id, value, fmt); }
inline Response ripple_button(const char* label, Vec2 size = {0,0}) { detail::lay_item(); return Response(VanGui::RippleButton(label, size)); }
inline Response elevated_button(const char* label, Vec2 size = {0,0}) { detail::lay_item(); return Response(VanGui::ElevatedButton(label, size)); }
#endif

#ifdef VANGUI_ENABLE_CHARTS
inline void sparkline(const char* id, const float* v, int n, Vec2 size, VanU32 c = 0) { detail::lay_item(); VanGui::Sparkline(id, v, n, size, c); }
inline void bars(const char* id, const float* v, int n, Vec2 size, VanU32 c = 0)      { detail::lay_item(); VanGui::BarChart(id, v, n, size, c); }
inline void gauge(const char* id, float frac, float radius, const char* label = nullptr) { detail::lay_item(); VanGui::Gauge(id, frac, radius, label); }
#endif

// ===========================================================================
// Loading / busy-state effects (when vangui_loading is compiled in). Spinners
// are stateless; the overlay is a RAII dim-and-cover scope.
// ===========================================================================
#ifdef VANGUI_ENABLE_LOADING
inline void spinner(const char* id, float radius, float thickness, VanU32 col = 0, float speed = 1.0f)
                                                          { detail::lay_item(); VanGui::Spinner(id, radius, thickness, col, speed); }
inline void spinner_dots(const char* id, float radius, int count = 8, VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::SpinnerDots(id, radius, count, col); }
inline void spinner_bars(const char* id, Vec2 size, VanU32 col = 0) { detail::lay_item(); VanGui::SpinnerBars(id, size, col); }
inline void indeterminate_bar(const char* id, Vec2 size, VanU32 col = 0) { detail::lay_item(); VanGui::IndeterminateBar(id, size, col); }
inline void progress_ring(const char* id, float fraction, float radius, float thickness, VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::ProgressRing(id, fraction, radius, thickness, col); }
inline void skeleton(Vec2 size, float rounding = 4.0f)         { detail::lay_item(); VanGui::Skeleton(size, rounding); }
inline void skeleton_text(int lines, float line_height = 0.0f){ detail::lay_item(); VanGui::SkeletonText(lines, line_height); }
inline void spinner_comet(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 col = 0, float speed = 1.0f)
                                                          { detail::lay_item(); VanGui::SpinnerComet(id, radius, thickness, col, speed); }
inline void spinner_pulse(const char* id, float radius = 0.0f, VanU32 col = 0) { detail::lay_item(); VanGui::SpinnerPulse(id, radius, col); }
inline void typing_dots(const char* id, Vec2 size = Vec2(0, 0), VanU32 col = 0) { detail::lay_item(); VanGui::TypingDots(id, size, col); }
inline void spinner_orbit(const char* id, float radius = 0.0f, VanU32 col = 0, VanU32 col2 = 0)
                                                          { detail::lay_item(); VanGui::SpinnerOrbit(id, radius, col, col2); }
inline void spinner_dual_ring(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 col = 0, VanU32 col2 = 0)
                                                          { detail::lay_item(); VanGui::SpinnerDualRing(id, radius, thickness, col, col2); }
inline void progress_bar(const char* id, float fraction, Vec2 size = Vec2(0, 0), const char* overlay = nullptr)
                                                          { detail::lay_item(); VanGui::ProgressBarSmooth(id, fraction, size, overlay); }
inline void progress_buffered(const char* id, float value, float buffer, Vec2 size = Vec2(0, 0), VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::ProgressBarBuffered(id, value, buffer, size, col); }
inline void progress_segments(const char* id, float value, int segments, Vec2 size = Vec2(0, 0), VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::ProgressSegments(id, value, segments, size, col); }
inline void countdown_ring(const char* id, float remaining, float total, float radius = 0.0f, float thickness = 0.0f, VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::CountdownRing(id, remaining, total, radius, thickness, col); }
using BusyState = VanGui::VanBusyState;
inline void spinner_status(const char* id, BusyState state, float radius = 0.0f, VanU32 col = 0)
                                                          { detail::lay_item(); VanGui::SpinnerStatus(id, state, radius, col); }
inline bool busy_button(const char* label, BusyState state, Vec2 size = Vec2(0, 0))
                                                          { detail::lay_item(); return VanGui::BusyButton(label, state, size); }
// RAII dim-and-cover overlay: `if (auto o = van::loading_overlay("busy", busy)) { ... }`.
// End() runs unconditionally (it wraps Begin/EndChild), so always_end=true.
inline Scope loading_overlay(const char* id, bool busy)
{ const bool vis = VanGui::BeginLoadingOverlay(id, busy); return Scope(vis, +[] { VanGui::EndLoadingOverlay(); }, /*always_end=*/true); }
#endif

// ===========================================================================
// Drawn icons (when vangui_icons is compiled in). Every icon is VanGui::VanIcon_*
// or a name: van::icon(van::find_icon("save")). With the icon font merged in,
// labels can carry them too: van::button(VANICON_SAVE " Save").
// ===========================================================================
#ifdef VANGUI_ENABLE_ICONS
using IconID     = VanGui::VanIconID;
using IconStyle  = VanGui::VanIconStyle;
using IconMorph  = VanGui::VanIconMorph;
using IconMotion = VanGui::VanIconMotion;
using IconParams = VanGui::VanIconParams;
inline void   icon(IconID id, float size = 0.0f, VanU32 col = 0)          { detail::lay_item(); VanGui::Icon(id, size, col); }
inline bool   icon_button(const char* id, IconID icon_id, float size = 0.0f, const char* tooltip = nullptr)
                                                                            { detail::lay_item(); return VanGui::IconButton(id, icon_id, size, tooltip); }
inline bool   icon_label_button(IconID icon_id, const char* label, Vec2 size = Vec2(0, 0))
                                                                            { detail::lay_item(); return VanGui::IconLabelButton(icon_id, label, size); }
inline bool   icon_toggle(const char* id, IconMorph morph, bool& state, float size = 0.0f)
                                                                            { detail::lay_item(); return VanGui::IconToggleButton(id, morph, &state, size); }
inline void   animated_icon(const char* id, IconID icon_id, IconMotion motion, float size = 0.0f, VanU32 col = 0, bool active = true)
                                                                            { detail::lay_item(); VanGui::AnimatedIcon(id, icon_id, motion, size, col, active); }
inline IconID find_icon(const char* name)                                   { return VanGui::FindIcon(name); }
#endif

#ifdef VANGUI_ENABLE_GALLERY
// The whole suite live on one page (vangui_gallery): van::gallery(&show_gallery).
inline void gallery(bool* open = nullptr) { VanGui::ShowEnhanceGallery(open); }
#endif

// ===========================================================================
// Motion (vangui_anim): transitions, glides and the global switches. Without the
// substrate the scopes draw their contents plainly.
//   if (auto a = van::appear("card")) { ... }            fades and rises in when first shown
//   if (auto t = van::transition("page", page)) { ... }  a new page slides in from its side
//   { auto g = van::glide(row.id); ... }                 a reordered row glides to its slot
// ===========================================================================
#ifdef VAN_HAS_ANIM_HEADER
using TransitionParams = VanGui::Anim::VanTransitionParams;
inline Scope appear(const char* id, const TransitionParams& p = {})
{ VanGui::Anim::BeginAppear(id, p); return Scope(true, &VanGui::Anim::EndAppear, /*always_end=*/true); }
inline Scope transition(const char* id, int key, const TransitionParams& p = {})
{ VanGui::Anim::BeginTransition(id, key, p); return Scope(true, &VanGui::Anim::EndTransition, /*always_end=*/true); }
inline Scope glide(const char* id, float duration = 0.22f)
{ VanGui::Anim::BeginGlide(id, duration); return Scope(true, &VanGui::Anim::EndGlide, /*always_end=*/true); }
inline Scope glide(int id, float duration = 0.22f)
{ VanGui::Anim::BeginGlide(VanGui::GetID(id), duration); return Scope(true, &VanGui::Anim::EndGlide, /*always_end=*/true); }
// Everything snaps (fades stay, shortened) for people who get motion sick.
inline void reduce_motion(bool on = true)        { VanGui::Anim::GetMotionConfig().ReduceMotion = on; }
// Keep reduce_motion in step with the OS "Show animations" setting (Windows).
inline void follow_system_motion(bool on = true) { VanGui::Anim::GetMotionConfig().FollowSystem = on; }
inline bool system_prefers_reduced_motion()      { return VanGui::Anim::SystemPrefersReducedMotion(); }

// --- Frame pacing, power and quality ------------------------------------------
// The idle loop:  while (running) { van::wait_for_input(); pump messages; draw a frame; }
// It returns at once while something moves, and otherwise sleeps until input
// arrives (Windows). Elsewhere, give van::idle_timeout() to your platform's wait.
inline bool  wants_redraw()
{
    bool w = VanGui::Anim::IsAnimating();
#ifdef VANGUI_ENABLE_THREAD
    w = w || VanGui::PendingMainThreadCallbacks() > 0;
#endif
    return w;
}
inline bool  wait_for_input()                { return VanGui::Anim::WaitForEvents(wants_redraw()); }
inline float idle_timeout()                  { return VanGui::Anim::IdleWaitTimeout(wants_redraw()); }
// Low power: what moves is drawn at LowPowerFps. `auto` = on battery or in the background.
inline void  low_power(bool on)              { VanGui::Anim::GetPerfConfig().Power = on ? VanGui::Anim::VanPowerMode_Low : VanGui::Anim::VanPowerMode_Normal; }
inline void  low_power_auto()                { VanGui::Anim::GetPerfConfig().Power = VanGui::Anim::VanPowerMode_Auto; }
inline bool  is_low_power()                  { return VanGui::Anim::IsLowPower(); }
using Quality = VanGui::Anim::VanQuality;
inline Quality quality()                     { return VanGui::Anim::GetQuality(); }
#endif

// Background windows: keep the next window's last drawing unless it is hovered or
// focused (or `interval` seconds passed). `if (auto w = van::window(...))` already
// skips the contents when Begin() returns false.
inline void refresh_when_idle(float interval = 0.0f) { VanGui::SetNextWindowRefreshWhenIdle(interval); }
// Programmatic scrolling that eases like the wheel.
inline void scroll_y_smooth(float y)       { VanGui::SetScrollYSmooth(y); }
inline void scroll_here_smooth(float ratio = 0.5f) { VanGui::SetScrollHereYSmooth(ratio); }
inline void scroll_to_item_smooth()        { VanGui::ScrollToItemSmooth(); }

// ===========================================================================
// Everyday widgets and lightweight helpers (vangui_extras; shims without it).
// ===========================================================================
#ifdef VAN_HAS_EXTRAS_HEADER
inline Response copy_button(const char* id, const char* text, float size = 0.0f)
{ detail::lay_item(); return Response(VanGui::CopyButton(id, text, size)); }
inline void kbd(const char* keys)                { detail::lay_item(); VanGui::Keycap(keys); }
inline void avatar(const char* name, float size = 0.0f, VanTextureID image = VanTextureID_Invalid, VanU32 ring = 0)
{ detail::lay_item(); VanGui::Avatar(name, size, image, ring); }
inline Response empty_state(VanGui::VanIconID icon, const char* title, const char* hint = nullptr, const char* action = nullptr)
{ detail::lay_item(); return Response(VanGui::EmptyState(icon, title, hint, action)); }
inline Response password_input(const char* label, char* buf, size_t cap, VanGuiInputTextFlags f = 0)
{ detail::lay_item(); return Response(VanGui::PasswordInput(label, buf, cap, f)); }
inline void text_ellipsis(const char* s, float width = 0.0f) { detail::lay_item(); VanGui::TextEllipsis(s, width); }
inline void key_value(const char* key, const char* value, float key_width = 0.0f) { detail::lay_item(); VanGui::KeyValue(key, value, key_width); }
inline void divider_text(const char* s)          { detail::lay_item(); VanGui::DividerText(s); }
// Decorates the item drawn just before it, so it takes no layout slot.
inline void badge_dot(int count = 0, VanU32 col = 0) { VanGui::BadgeDot(count, col); }
inline void rolling_number(const char* id, double value, int decimals = 0, const char* prefix = nullptr, const char* suffix = nullptr)
{ detail::lay_item(); VanGui::RollingNumber(id, value, decimals, prefix, suffix); }
// `if (auto c = van::cached("stats", changed)) { ... }` -- the body runs only when
// the region has to be redrawn; otherwise last frame's drawing is replayed.
inline Scope cached(const char* id, bool dirty = false)
{ detail::lay_item(); const bool live = VanGui::BeginCached(id, dirty); return Scope(live, &VanGui::EndCached, /*always_end=*/false); }
// Only the rows in view are drawn: van::virtual_list(n, 0, [&](int i) { text(names[i]); }).
template <class F> inline void virtual_list(int count, float row_height, F&& draw_row)
{ detail::lay_item(); VanGui::VirtualList(count, row_height, static_cast<F&&>(draw_row)); }
// After Render(): the window (with its child windows) or the whole frame as a PNG.
inline bool capture_window(const char* window_name, const char* png_path, VanU32 background = 0)
{ return VanGui::CaptureWindow(window_name, png_path, background); }
inline bool capture_screen(const char* png_path) { return VanGui::CaptureScreen(png_path); }
// `if (auto l = van::lazy("advanced", 400)) { ... }` -- skipped entirely while off-screen.
inline Scope lazy(const char* id, float estimated_height = 0.0f)
{ detail::lay_item(); const bool live = VanGui::BeginLazy(id, estimated_height); return Scope(live, &VanGui::EndLazy, /*always_end=*/false); }
// `{ auto h = van::auto_height("details"); ... }` -- the region's height eases to its contents'.
inline Scope auto_height(const char* id, float duration = 0.18f)
{ detail::lay_item(); VanGui::BeginAutoHeight(id, duration); return Scope(true, &VanGui::EndAutoHeight, /*always_end=*/true); }
// `if (auto s = van::loading_swap("feed", loaded)) { ... }` -- skeleton until ready, then a crossfade.
inline Scope loading_swap(const char* id, bool ready, int skeleton_lines = 3)
{ detail::lay_item(); const bool show = VanGui::BeginLoadingSwap(id, ready, skeleton_lines); return Scope(show, &VanGui::EndLoadingSwap, /*always_end=*/false); }
inline int  prebake_glyphs(const char* text) { return VanGui::PrebakeGlyphs(text); }
inline Response range_slider(const char* label, float& lo, float& hi, float mn, float mx, const char* fmt = "%.2f")
{ detail::lay_item(); return Response(VanGui::SliderRange(label, &lo, &hi, mn, mx, fmt)); }
inline Response range_slider(const char* label, int& lo, int& hi, int mn, int mx)
{ detail::lay_item(); return Response(VanGui::SliderRangeInt(label, &lo, &hi, mn, mx)); }
using Tags = VanGui::VanTags;
inline Response tag_input(const char* id, Tags& tags, const char* hint = "Add a tag")
{ detail::lay_item(); return Response(VanGui::TagInput(id, &tags, hint)); }
inline void stat_card(const char* label, const char* value, float delta = FLT_MAX, const char* suffix = "%", bool up_is_good = true, float width = 0.0f)
{ detail::lay_item(); VanGui::StatCard(label, value, delta, suffix, up_is_good, width); }
inline bool highlight_match(const char* text, const char* query) { detail::lay_item(); return VanGui::TextHighlight(text, query); }
// A PNG from disk, loaded in the background and cached; a skeleton until it shows.
inline bool image(const char* path, Vec2 size = Vec2(0, 0), float rounding = 0.0f)
{ detail::lay_item(); return VanGui::ImageFile(path, size, rounding); }
#endif

// ===========================================================================
// Touch gestures (vangui_touch; with the module off they do nothing).
// ===========================================================================
#ifdef VAN_HAS_TOUCH_HEADER
using SwipeAction = VanGui::VanSwipeAction;
using Swipe       = VanGui::VanSwipe;
using CanvasView  = VanGui::VanCanvasView;
inline bool pull_to_refresh(const char* id, bool& refreshing, float threshold = 0.0f) { return VanGui::PullToRefresh(id, &refreshing, threshold); }
// Draws `f` as a swipeable row; returns the side that fired, if any.
template <class F> inline Swipe swipe_row(const char* id, const SwipeAction& leading, const SwipeAction& trailing, F&& f)
{ detail::lay_item(); VanGui::BeginSwipeRow(id, leading, trailing); f(); return VanGui::EndSwipeRow(); }
inline bool long_pressed(float seconds = 0.5f) { return VanGui::IsItemLongPressed(seconds); }
// Right-click or long-press menu on the last item.
inline Scope context_menu(const char* id = nullptr)
{ const bool o = VanGui::BeginPopupContextItemTouch(id); return Scope(o, &VanGui::EndPopup, /*always_end=*/false); }
inline Scope canvas(const char* id, CanvasView& view, Vec2 size = Vec2(0, 0))
{ detail::lay_item(); const bool v = VanGui::BeginCanvas(id, &view, size); return Scope(v, &VanGui::EndCanvas, /*always_end=*/true); }
#endif

// ===========================================================================
// Transform gizmos (vangui_gizmo; with the module off they draw nothing).
//   if (van::gizmo("move", view, proj, object, van::Gizmo::Translate)) ApplyToSelection(object);
// ===========================================================================
#ifdef VAN_HAS_GIZMO_HEADER
using GizmoOptions   = VanGui::VanGizmoOptions;
using GizmoSpace     = VanGui::VanGizmoSpace;
using GizmoPart      = VanGui::VanGizmoPart;
using GizmoMatrix    = VanGui::VanGizmoMatrix;
using Transform2D    = VanGui::VanGizmoTransform2D;
namespace Gizmo {
inline constexpr VanGui::VanGizmoOp Translate = VanGui::VanGizmoOp_Translate;
inline constexpr VanGui::VanGizmoOp Rotate    = VanGui::VanGizmoOp_Rotate;
inline constexpr VanGui::VanGizmoOp Scale     = VanGui::VanGizmoOp_Scale;
inline constexpr VanGui::VanGizmoOp Universal = VanGui::VanGizmoOp_Universal;
inline constexpr VanGui::VanGizmoOp Bounds    = VanGui::VanGizmoOp_Bounds;
} // namespace Gizmo
inline bool gizmo(const char* id, const float* view, const float* proj, float* matrix, VanGui::VanGizmoOp ops = VanGui::VanGizmoOp_Translate, const GizmoOptions& o = GizmoOptions())
{ return VanGui::Gizmo(id, view, proj, matrix, ops, o); }
inline bool gizmo_2d(const char* id, Transform2D& t, VanGui::VanGizmoOp ops = VanGui::VanGizmoOp_Translate, const VanGui::VanCanvasView* canvas = nullptr, const GizmoOptions& o = GizmoOptions())
{ return VanGui::Gizmo2D(id, &t, ops, canvas, o); }
inline bool view_cube(const char* id, float* view, const float* proj, float pivot_distance, Vec2 pos, float size = 0.0f, VanGui::VanViewCubeFlags flags = 0)
{ return VanGui::ViewCube(id, view, proj, pivot_distance, pos, size, flags); }
inline bool gizmo_toolbar(const char* id, VanGui::VanGizmoOp& ops, GizmoSpace* space = nullptr, bool* snap = nullptr, bool vertical = false)
{ detail::lay_item(); return VanGui::GizmoToolbar(id, &ops, space, snap, vertical); }
inline bool gizmo_over()  { return VanGui::IsGizmoOver(); }
inline bool gizmo_using() { return VanGui::IsGizmoUsing(); }
#endif

// ===========================================================================
// Human-readable numbers (vangui_format.h, header-only). Each returns a string
// from a small rotating pool: van::text(van::format_bytes(size)).
// ===========================================================================
#ifdef VAN_HAS_FORMAT_HEADER
inline const char* format_bytes(double bytes, bool binary = false, int decimals = 1) { return VanGui::FormatBytes(bytes, binary, decimals); }
inline const char* format_duration(double seconds, int parts = 2)                   { return VanGui::FormatDuration(seconds, parts); }
// seconds_ago > 0 is the past ("3 min ago"), < 0 the future ("in 3 min").
inline const char* relative_time(double seconds_ago)                                { return VanGui::FormatRelativeTime(seconds_ago); }
inline const char* relative_time_since(time_t when)                                 { return VanGui::FormatRelativeTimeSince(when); }
#endif

// ===========================================================================
// Forms — aligned label/field rows + inline validation (vangui_forms).
//   if (auto f = van::form("settings")) {
//       form_row("Name"); input_text("##n", name, sizeof name);
//       { auto s = van::invalid_if(!van::valid_not_empty(name)); ... }
//   }
// ===========================================================================
#ifdef VANGUI_ENABLE_FORMS
inline Scope form(const char* id, float label_width = 0.0f)
{ detail::lay_item(); VanGui::BeginForm(id, label_width); return Scope(true, &VanGui::EndForm, /*always_end=*/true); }
inline void form_row(const char* label)  { VanGui::FormRow(label); }
inline void field_error(const char* msg) { VanGui::FieldError(msg); }
inline void field_hint(const char* msg)  { VanGui::FieldHint(msg); }
// Scoped invalid-field tint; pops only if it pushed (always_end=false).
inline Scope invalid_if(bool invalid)
{ if (invalid) VanGui::PushInvalid(); return Scope(invalid, &VanGui::PopInvalid, /*always_end=*/false); }
inline bool valid_not_empty(const char* s)              { return VanGui::ValidNotEmpty(s); }
inline bool valid_in_range(float v, float lo, float hi) { return VanGui::ValidInRange(v, lo, hi); }
inline bool valid_in_range(int v, int lo, int hi)       { return VanGui::ValidInRange(v, lo, hi); }
#endif

// ===========================================================================
// Panels — app chrome: splitter, accordion, status bar, toolbar (vangui_panels).
// All scopes End() unconditionally.
// ===========================================================================
#ifdef VANGUI_ENABLE_PANELS
inline bool splitter(const char* id, bool vertical, float thickness, float& a, float& b, float min_a = 24.0f, float min_b = 24.0f)
{ detail::lay_item(); return VanGui::Splitter(id, vertical, thickness, &a, &b, min_a, min_b); }
inline Scope accordion(const char* label, bool& open)
{ detail::lay_item(); const bool show = VanGui::AccordionSection(label, &open); return Scope(show, &VanGui::AccordionEnd, /*always_end=*/true); }
inline Scope status_bar()                  { const bool o = VanGui::BeginStatusBar();    return Scope(o, &VanGui::EndStatusBar, /*always_end=*/true); }
inline Scope toolbar(const char* id, float height = 0.0f) { const bool o = VanGui::BeginToolbar(id, height); return Scope(o, &VanGui::EndToolbar, /*always_end=*/true); }
inline void toolbar_separator()            { VanGui::ToolbarSeparator(); }
#endif

// ===========================================================================
// Model/View adapters — virtualized list & tree (vangui_views). Begin draws the
// model's rows and End is unconditional, so each is a single call returning
// whether the selection changed this frame.
// ===========================================================================
#ifdef VANGUI_ENABLE_VIEWS
using ListModel = VanGui::VanListModel;
using TreeModel = VanGui::VanTreeModel;
inline bool list_view(const char* id, const ListModel& model, int& selected, Vec2 size = {0, 0})
{ detail::lay_item(); const bool changed = VanGui::BeginListView(id, model, &selected, size); VanGui::EndListView(); return changed; }
inline bool tree_view(const char* id, const TreeModel& model, int& selected, Vec2 size = {0, 0})
{ detail::lay_item(); const bool changed = VanGui::BeginTreeView(id, model, &selected, size); VanGui::EndTreeView(); return changed; }
#endif

// ===========================================================================
// Named theme presets + serialization (vangui_themes).
// ===========================================================================
#ifdef VANGUI_ENABLE_THEMES
using ThemeID = VanGui::VanThemeID;
inline void load_theme(ThemeID id)            { VanGui::LoadTheme(id); }
inline void load_theme(const char* name)      { VanGui::LoadTheme(name); }
inline const char* theme_name(ThemeID id)     { return VanGui::GetThemeName(id); }
inline void save_theme_to_file(const char* path)    { VanGui::SaveThemeToFile(path); }
inline bool load_theme_from_file(const char* path)  { return VanGui::LoadThemeFromFile(path); }
#endif

// ===========================================================================
// Node graph / node editor (vangui_node_graph).
// Lifetime: create one NodeGraphContext* per canvas with node_graph_context(),
// destroy it with destroy_node_graph_context(). Each frame call node_graph()
// which returns a Scope; inside it, call node_pin(), node_link(), etc.
// ===========================================================================
#ifdef VANGUI_ENABLE_NODE_GRAPH
using NodeGraphContext = VanGui::VanNodeGraphContext;
inline NodeGraphContext* node_graph_context()                  { return VanGui::CreateNodeGraphContext(); }
inline void destroy_node_graph_context(NodeGraphContext* ctx)  { VanGui::DestroyNodeGraphContext(ctx); }
inline Scope node_graph(const char* id, NodeGraphContext* ctx, Vec2 size = {0, 0})
{ detail::lay_item(); const bool open = VanGui::BeginNodeGraph(id, ctx, size); return Scope(open, &VanGui::EndNodeGraph, /*always_end=*/true); }
inline void node_begin(int id, Vec2& pos)     { VanGui::BeginNode(id, &pos); }
inline void node_end()                        { VanGui::EndNode(); }
inline void node_title(const char* title)     { VanGui::NodeTitle(title); }
inline bool node_pin(int pin_id, bool is_output, const char* label, Vec4 color = {1,1,1,1})
{ return VanGui::NodePin(pin_id, is_output, label, color); }
inline void node_link(int from_node, int from_pin, int to_node, int to_pin, Vec4 color = {1,1,1,1})
{ VanGui::NodeLink(from_node, from_pin, to_node, to_pin, color); }
inline bool is_link_created(int* fn, int* fp, int* tn, int* tp) { return VanGui::IsLinkCreated(fn, fp, tn, tp); }
inline bool is_link_deleted(int* fn, int* fp, int* tn, int* tp) { return VanGui::IsLinkDeleted(fn, fp, tn, tp); }
inline bool is_node_selected(int node_id)     { return VanGui::IsNodeSelected(node_id); }
#endif

// ===========================================================================
// Theme editor panel / window (vangui_theme_editor).
// ===========================================================================
#ifdef VANGUI_ENABLE_THEME_EDITOR
inline void show_theme_editor(bool* p_open = nullptr)        { VanGui::ShowThemeEditor(p_open); }
inline void show_theme_editor_window(bool* p_open = nullptr) { VanGui::ShowThemeEditorWindow(p_open); }
#endif

// ===========================================================================
// Threading (vangui_thread).
//
// Call van::init_threads() once before your render loop and
// van::shutdown_threads() once after. All other calls are thread-safe.
//
//   // Fire-and-forget
//   van::async([]{ crunch_numbers(); });
//
//   // Result delivered to main thread next frame
//   van::async<std::string>(
//       []{ return load_file("data.bin"); },
//       [](std::string s){ g_data = std::move(s); }
//   );
//
//   // Pollable future
//   auto f = van::async_future<Image>([]{ return decode_png("bg.png"); });
//   if (f.ready()) g_bg = f.get();
//
// Without VANGUI_ENABLE_THREAD everything runs synchronously (single-thread
// fallback — the signatures are identical so no call sites need to change).
// ===========================================================================
inline void init_threads(unsigned int n = 0)  { VanGui::InitThreadPool(n); }
inline void shutdown_threads()                { VanGui::ShutdownThreadPool(); }
inline void post(std::function<void()> fn)    { VanGui::PostToMainThread(std::move(fn)); }
inline void async(std::function<void()> work) { VanGui::Async(std::move(work)); }

template <class T>
inline void async(std::function<T()> work, std::function<void(T)> done)
{ VanGui::Async<T>(std::move(work), std::move(done)); }

inline void async(std::function<void()> work, std::function<void()> done)
{ VanGui::Async(std::move(work), std::move(done)); }

#ifdef VANGUI_ENABLE_THREAD
template <class T>
[[nodiscard]] inline VanGui::VanFuture<T> async_future(std::function<T()> work)
{ return VanGui::AsyncFuture<T>(std::move(work)); }
#endif

// ===========================================================================
// Tier 1-3 tooling & application modules. Each block lights up when its module
// is linked (enable macro defined); header-only ones appear when present.
// ===========================================================================

// --- Log / console (vangui_console) ----------------------------------------
#ifdef VANGUI_ENABLE_CONSOLE
using Console = VanGui::VanConsole;
using LogLevel = VanGui::VanLogLevel;
#endif

// --- Property grid (vangui_property_grid) ----------------------------------
#ifdef VANGUI_ENABLE_PROPERTY_GRID
inline Scope property_grid(const char* id, float label_width = 0.0f)
{ detail::lay_item(); const bool o = VanGui::BeginPropertyGrid(id, label_width); return Scope(o, &VanGui::EndPropertyGrid, /*always_end=*/false); }
inline Scope property_category(const char* label, bool default_open = true)
{ const bool o = VanGui::BeginPropertyCategory(label, default_open); return Scope(o, &VanGui::EndPropertyCategory, /*always_end=*/false); }
inline Response property(const char* label, float& v, float mn = 0.0f, float mx = 0.0f, const char* fmt = "%.3f")
{ return Response(VanGui::PropertyFloat(label, &v, mn, mx, fmt)); }
inline Response property(const char* label, int& v, int mn = 0, int mx = 0)
{ return Response(VanGui::PropertyInt(label, &v, mn, mx)); }
inline Response property(const char* label, bool& v)
{ return Response(VanGui::PropertyBool(label, &v)); }
inline Response property(const char* label, Vec4& v)
{ return Response(VanGui::PropertyColor(label, &v.x)); }
inline Response property(const char* label, char* buf, size_t cap)
{ return Response(VanGui::PropertyText(label, buf, cap)); }
inline Response property_combo(const char* label, int& current, const char* const items[], int count)
{ return Response(VanGui::PropertyCombo(label, &current, items, count)); }
#endif

// --- Curve / gradient editor (vangui_curve_editor) -------------------------
#ifdef VANGUI_ENABLE_CURVE_EDITOR
using CurvePoint   = VanGui::VanCurvePoint;
using GradientStop = VanGui::VanGradientStop;
inline Response curve_editor(const char* id, Vec2 size, CurvePoint* pts, int& count, int max_pts,
                             float x0 = 0.0f, float x1 = 1.0f, float y0 = 0.0f, float y1 = 1.0f, VanU32 col = 0)
{ detail::lay_item(); return Response(VanGui::CurveEditor(id, size, pts, &count, max_pts, x0, x1, y0, y1, col)); }
inline float curve_value(const CurvePoint* pts, int count, float x) { return VanGui::CurveValue(pts, count, x); }
inline Response gradient_editor(const char* id, Vec2 size, GradientStop* stops, int& count, int max_stops)
{ detail::lay_item(); return Response(VanGui::GradientEditor(id, size, stops, &count, max_stops)); }
inline void gradient_bar(const char* id, Vec2 size, const GradientStop* stops, int count)
{ detail::lay_item(); VanGui::GradientBar(id, size, stops, count); }
inline Vec4 gradient_sample(const GradientStop* stops, int count, float t) { return VanGui::GradientSample(stops, count, t); }
#endif

// --- Date / time picker (vangui_datetime) ----------------------------------
#ifdef VANGUI_ENABLE_DATETIME
using Date = VanGui::VanDate;
using Time = VanGui::VanTime;
inline Response calendar(const char* id, Date& d)     { detail::lay_item(); return Response(VanGui::CalendarWidget(id, &d)); }
inline Response date_picker(const char* label, Date& d) { detail::lay_item(); return Response(VanGui::DatePicker(label, &d)); }
inline Response time_picker(const char* label, Time& t, bool with_seconds = true)
{ detail::lay_item(); return Response(VanGui::TimePicker(label, &t, with_seconds)); }
#endif

// --- Markdown (vangui_markdown) --------------------------------------------
#ifdef VANGUI_ENABLE_MARKDOWN
inline void markdown(const char* text, VanGui::VanMarkdownLinkFn on_link = nullptr, void* ud = nullptr)
{ detail::lay_item(); VanGui::Markdown(text, on_link, ud); }
#endif

// --- Hex editor (vangui_hex_editor) ----------------------------------------
#ifdef VANGUI_ENABLE_HEX_EDITOR
using HexEditor = VanGui::VanHexEditor;
#endif

// --- Real-time plot (vangui_plot) ------------------------------------------
#ifdef VANGUI_ENABLE_PLOT
inline Scope plot(const char* id, Vec2 size, float x0, float x1, float y0, float y1)
{ detail::lay_item(); const bool o = VanGui::BeginPlot(id, size, x0, x1, y0, y1); return Scope(o, &VanGui::EndPlot, /*always_end=*/false); }
inline void plot_values(const char* label, const float* v, int n, VanU32 col = 0) { VanGui::PlotValues(label, v, n, col); }
inline void plot_xy(const char* label, const float* xs, const float* ys, int n, VanU32 col = 0) { VanGui::PlotXY(label, xs, ys, n, col); }
inline void plot_hline(float y, VanU32 col = 0) { VanGui::PlotHLine(y, col); }
#endif

// --- Timeline / sequencer (vangui_timeline) --------------------------------
#ifdef VANGUI_ENABLE_TIMELINE
inline Scope timeline(const char* id, Vec2 size, float t0, float t1, float& current)
{ detail::lay_item(); VanGui::BeginTimeline(id, size, t0, t1, &current); return Scope(true, &VanGui::EndTimeline, /*always_end=*/true); }
inline Response timeline_track(const char* name, float* keys, int& count, int max_keys)
{ return Response(VanGui::TimelineTrack(name, keys, &count, max_keys)); }
#endif

// --- Data grid (vangui_data_grid) ------------------------------------------
#ifdef VANGUI_ENABLE_DATA_GRID
using DataGridColumn = VanGui::VanDataGridColumn;
using DataGridModel  = VanGui::VanDataGridModel;
inline int data_grid(const char* id, const DataGridColumn* cols, int col_count,
                     const DataGridModel& model, Vec2 size = {0, 0}, int* selected = nullptr)
{ detail::lay_item(); return VanGui::DataGrid(id, cols, col_count, model, size, selected); }
#endif

// --- Banners + snackbars (vangui_banners) ----------------------------------
#ifdef VANGUI_ENABLE_BANNERS
using BannerType = VanGui::VanBannerType;
inline bool banner(const char* id, BannerType type, const char* text,
                   const char* action = nullptr, bool* p_open = nullptr)
{ detail::lay_item(); return VanGui::Banner(id, type, text, action, p_open); }
inline void snackbar(const char* text, const char* action = nullptr, float duration = 4.0f)
{ VanGui::Snackbar(text, action, duration); }
inline int  render_snackbars() { return VanGui::RenderSnackbars(); }
#endif

// --- Command console / REPL (vangui_repl) ----------------------------------
#ifdef VANGUI_ENABLE_REPL
using Repl = VanGui::VanRepl;
#endif

// --- Rich text (vangui_richtext) -------------------------------------------
#ifdef VANGUI_ENABLE_RICHTEXT
inline void rich_text(const char* tagged) { detail::lay_item(); VanGui::RichText(tagged); }
inline Vec2 calc_rich_text_size(const char* tagged) { return VanGui::CalcRichTextSize(tagged); }
#endif

// --- Perf HUD (vangui_perf_hud) --------------------------------------------
#ifdef VANGUI_ENABLE_PERF_HUD
inline void perf_hud(bool* p_open = nullptr) { VanGui::PerfHUD(p_open); }
inline void perf_hud_contents() { VanGui::PerfHUDContents(); }
#endif

// --- Drag-to-reorder (vangui_reorder.h, header-only) -----------------------
#if __has_include("misc/vangui_reorder.h")
inline bool reorder_item(const char* payload_type, int index, int* out_from, int* out_to)
{ return VanGui::ReorderItem(payload_type, index, out_from, out_to); }
template <class T> inline void reorder_apply(T* arr, int count, int from, int to)
{ VanGui::ReorderApply(arr, count, from, to); }
#endif

// --- Localization-lite (vangui_i18n.h, header-only) ------------------------
#if __has_include("misc/vangui_i18n.h")
using StringCatalog = VanGui::VanStringCatalog;
inline VanGui::VanStringCatalog& global_catalog() { return VanGui::GlobalCatalog(); }
inline const char* tr(const char* key) { return VanGui::Tr(key); }
#endif

} // namespace van
