// vangui_shorthand.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — one-liner sugar over the core immediate-mode API.
//
// Header-only. Zero new subsystems, zero macro gate, zero allocations. Every
// helper below is a thin composition over public VanGui functions, chosen from
// the "I keep writing the same 4 lines" patterns that show up in every real
// app. Nothing here creates retained UI state.
//
// Namespace `vgu::` (same short alias as vangui_scoped.h) so callsites stay
// tight, e.g.:
//
//     if (vgu::Shortcut("Ctrl+S")) SaveDocument();
//     vgu::HelpMarker("Displayed to the user in the notification tray.");
//     vgu::SearchBox(query_buf, sizeof(query_buf), "Search commands...");
//     if (auto s = vgu::Section("Advanced")) { ... }
//     vgu::Kbd("Ctrl", "Shift", "P");
//     vgu::EmptyState("No items", "Add one to get started",
//                     "Add item", []{ AddItem(); });
//
// Design rules:
//   * Pure sugar. Every function decomposes to a short sequence of existing
//     VanGui calls; nothing here allocates or retains state.
//   * `noexcept`-friendly. No exceptions, no std::function.
//   * Accepts trivially-invocable callables via templates (lambdas without
//     captures decay to fn pointers; captured lambdas are inlined by the
//     compiler at zero cost when instantiated per call site).
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_notify.h"   // Toast::* forwards; notify has no compile gate
#ifdef VANGUI_ENABLE_WIDGETS_EXT
#include "vangui_widgets_ext.h"   // SearchBox forwards to the suite's one
#endif
#ifdef VANGUI_ENABLE_LOADING
#include "vangui_loading.h"       // Async::DrawOverlay
#endif

#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdarg>

namespace vgu {

// ============================================================================
// Shortcuts — one-line keyboard-chord dispatch
// ============================================================================
// Returns true once the frame the chord fires. Parsing is done inline; strings
// are tiny (usually < 16 chars) and the check is cheap. If you want registered
// shortcuts with a palette / menu label attached, see misc/vangui_actions.h.

namespace detail {

inline bool IEqualsN(const char* a, const char* b) noexcept
{
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

inline int ParseKey(const char* t) noexcept
{
    if (!t || !*t) return 0;
    if (t[1] == 0) {
        const char c = (char)std::toupper((unsigned char)t[0]);
        if (c >= 'A' && c <= 'Z') return VanGuiKey_A + (c - 'A');
        if (c >= '0' && c <= '9') return VanGuiKey_0 + (c - '0');
    }
    if ((t[0] == 'F' || t[0] == 'f') && t[1]) {
        int n = 0; const char* p = t + 1;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
        if (*p == 0 && n >= 1 && n <= 24) return VanGuiKey_F1 + (n - 1);
    }
    struct N { const char* n; int k; };
    static const N kNamed[] = {
        {"Space",VanGuiKey_Space},{"Enter",VanGuiKey_Enter},{"Return",VanGuiKey_Enter},
        {"Escape",VanGuiKey_Escape},{"Esc",VanGuiKey_Escape},{"Tab",VanGuiKey_Tab},
        {"Backspace",VanGuiKey_Backspace},{"Delete",VanGuiKey_Delete},{"Del",VanGuiKey_Delete},
        {"Insert",VanGuiKey_Insert},{"Ins",VanGuiKey_Insert},
        {"Home",VanGuiKey_Home},{"End",VanGuiKey_End},
        {"PageUp",VanGuiKey_PageUp},{"PgUp",VanGuiKey_PageUp},
        {"PageDown",VanGuiKey_PageDown},{"PgDn",VanGuiKey_PageDown},
        {"Left",VanGuiKey_LeftArrow},{"Right",VanGuiKey_RightArrow},
        {"Up",VanGuiKey_UpArrow},{"Down",VanGuiKey_DownArrow},
    };
    for (const auto& e : kNamed) if (IEqualsN(e.n, t)) return e.k;
    return 0;
}

inline VanGuiKeyChord ParseChord(const char* s) noexcept
{
    if (!s) return 0;
    int chord = 0, key = 0;
    char tok[24];
    while (*s) {
        while (*s == ' ' || *s == '\t') ++s;
        int n = 0;
        while (*s && *s != '+' && n < (int)sizeof(tok) - 1) tok[n++] = *s++;
        while (n > 0 && (tok[n-1] == ' ' || tok[n-1] == '\t')) --n;
        tok[n] = 0;
        if (*s == '+') ++s;
        if (!n) continue;
        if      (IEqualsN(tok,"Ctrl")||IEqualsN(tok,"Control")||
                 IEqualsN(tok,"Cmd") ||IEqualsN(tok,"Command"))   chord |= VanGuiMod_Ctrl;
        else if (IEqualsN(tok,"Shift"))                             chord |= VanGuiMod_Shift;
        else if (IEqualsN(tok,"Alt")||IEqualsN(tok,"Option"))       chord |= VanGuiMod_Alt;
        else if (IEqualsN(tok,"Super")||IEqualsN(tok,"Win")||
                 IEqualsN(tok,"Meta"))                              chord |= VanGuiMod_Super;
        else { const int k = ParseKey(tok); if (!k) return 0; key = k; }
    }
    return key ? (chord | key) : 0;
}

} // namespace detail

// One-liner shortcut check. Usage: `if (vgu::Shortcut("Ctrl+S")) Save();`
[[nodiscard]] inline bool Shortcut(const char* chord_str) noexcept
{
    const VanGuiKeyChord c = detail::ParseChord(chord_str);
    return c != 0 && VanGui::IsKeyChordPressed(c);
}

// Callback variant: fires `fn` when chord hits; returns whether it fired.
template <class Fn>
inline bool Shortcut(const char* chord_str, Fn&& fn)
{
    if (Shortcut(chord_str)) { fn(); return true; }
    return false;
}

// ============================================================================
// Section — collapsible group with automatic indent, one line
// ============================================================================
// `if (auto s = vgu::Section("Advanced")) { ... }`. Remembers open/closed state
// via the underlying CollapsingHeader (ID-keyed).

class Section
{
public:
    explicit Section(const char* label, VanGuiTreeNodeFlags flags = VanGuiTreeNodeFlags_DefaultOpen)
        : m_open(VanGui::CollapsingHeader(label, flags))
    { if (m_open) VanGui::Indent(); }

    ~Section() noexcept { if (m_open) VanGui::Unindent(); }

    Section(const Section&)            = delete;
    Section& operator=(const Section&) = delete;

    explicit operator bool() const noexcept { return m_open; }
private:
    bool m_open;
};

// ============================================================================
// Search box — labelled InputText + clear button + placeholder in one call
// ============================================================================
// `vgu::SearchBox(buf, sizeof(buf), "Search commands...")`. Returns true when
// the buffer content changed this frame.

inline bool SearchBox(char* buf, size_t buf_size, const char* placeholder = "Search...",
                      bool auto_focus = false)
{
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    // One search box: the suite's VanGui::SearchBox (clear "x" drawn inside the field).
    VanGui::PushID(buf);
    if (auto_focus) VanGui::SetKeyboardFocusHere();
    const bool changed_ext = VanGui::SearchBox("##vgu_search", buf, buf_size, placeholder);
    VanGui::PopID();
    return changed_ext;
#else
    VanGui::PushID(buf);   // stable id per buffer instance
    const float w = VanGui::GetContentRegionAvail().x;
    VanGui::SetNextItemWidth(w > 60.0f ? w - 24.0f : w);
    if (auto_focus) VanGui::SetKeyboardFocusHere();
    bool changed = VanGui::InputTextWithHint("##vgu_search", placeholder,
                                             buf, buf_size);
    VanGui::SameLine(0.0f, 4.0f);
    if (VanGui::SmallButton("x")) { buf[0] = 0; changed = true; }
    VanGui::PopID();
    return changed;
#endif
}

// ============================================================================
// Kbd — inline styled key-cap chip. Variadic: vgu::Kbd("Ctrl", "Shift", "P")
// ============================================================================
// Renders as bordered small text on the same line. No allocation.

namespace detail {
inline void KbdOne(const char* s)
{
    if (!s || !*s) return;
    const VanVec2 pad(6.0f, 1.0f);
    VanGui::PushStyleVar(VanGuiStyleVar_FramePadding, pad);
    VanGui::PushStyleVar(VanGuiStyleVar_FrameRounding, 3.0f);
    VanGui::PushStyleColor(VanGuiCol_Button, VanGui::GetColorU32(VanGuiCol_FrameBg));
    VanGui::PushStyleColor(VanGuiCol_ButtonHovered, VanGui::GetColorU32(VanGuiCol_FrameBg));
    VanGui::PushStyleColor(VanGuiCol_ButtonActive, VanGui::GetColorU32(VanGuiCol_FrameBg));
    (void)VanGui::SmallButton(s);
    VanGui::PopStyleColor(3);
    VanGui::PopStyleVar(2);
}
} // namespace detail

inline void Kbd(const char* a) { detail::KbdOne(a); }
inline void Kbd(const char* a, const char* b)
    { detail::KbdOne(a); VanGui::SameLine(0.0f, 2.0f); VanGui::TextUnformatted("+"); VanGui::SameLine(0.0f, 2.0f); detail::KbdOne(b); }
inline void Kbd(const char* a, const char* b, const char* c)
    { Kbd(a, b); VanGui::SameLine(0.0f, 2.0f); VanGui::TextUnformatted("+"); VanGui::SameLine(0.0f, 2.0f); detail::KbdOne(c); }
inline void Kbd(const char* a, const char* b, const char* c, const char* d)
    { Kbd(a, b, c); VanGui::SameLine(0.0f, 2.0f); VanGui::TextUnformatted("+"); VanGui::SameLine(0.0f, 2.0f); detail::KbdOne(d); }

// ============================================================================
// HelpMarker — the classic (?) icon with a hover tooltip
// ============================================================================
inline void HelpMarker(const char* desc)
{
    VanGui::TextDisabled("(?)");
    if (VanGui::IsItemHovered() && VanGui::BeginTooltip()) {
        VanGui::PushTextWrapPos(VanGui::GetFontSize() * 35.0f);
        VanGui::TextUnformatted(desc);
        VanGui::PopTextWrapPos();
        VanGui::EndTooltip();
    }
}

// ============================================================================
// Text helpers
// ============================================================================
inline void CenteredText(const char* text)
{
    const float avail = VanGui::GetContentRegionAvail().x;
    const float tw    = VanGui::CalcTextSize(text).x;
    if (avail > tw) VanGui::SetCursorPosX(VanGui::GetCursorPosX() + (avail - tw) * 0.5f);
    VanGui::TextUnformatted(text);
}

inline void Spacer(float height = 4.0f) { VanGui::Dummy(VanVec2(1.0f, height)); }
inline void HSpacer(float width  = 4.0f) { VanGui::Dummy(VanVec2(width, 1.0f)); VanGui::SameLine(0.0f, 0.0f); }

// Small colored pill/badge for status labels ("New", "Beta", counts, ...).
inline void TextBadge(const char* label, VanU32 bg = 0xFF3F7FCC)
{
    const VanVec2 pad(6.0f, 1.0f);
    VanGui::PushStyleVar(VanGuiStyleVar_FramePadding, pad);
    VanGui::PushStyleVar(VanGuiStyleVar_FrameRounding, 8.0f);
    VanGui::PushStyleColor(VanGuiCol_Button, bg);
    VanGui::PushStyleColor(VanGuiCol_ButtonHovered, bg);
    VanGui::PushStyleColor(VanGuiCol_ButtonActive, bg);
    (void)VanGui::SmallButton(label);
    VanGui::PopStyleColor(3);
    VanGui::PopStyleVar(2);
}

// ============================================================================
// ToggleButton — button that toggles a bool, visually pressed when true
// ============================================================================
inline bool ToggleButton(const char* label, bool* v, const VanVec2& size = VanVec2(0,0))
{
    const VanU32 base   = VanGui::GetColorU32(VanGuiCol_Button);
    const VanU32 active = VanGui::GetColorU32(VanGuiCol_ButtonActive);
    VanGui::PushStyleColor(VanGuiCol_Button, *v ? active : base);
    const bool clicked = VanGui::Button(label, size);
    VanGui::PopStyleColor();
    if (clicked) *v = !*v;
    return clicked;
}

// ============================================================================
// EmptyState — centered icon/title/hint/action, one call
// ============================================================================
// `vgu::EmptyState("No items", "Add one to get started", "Add item", []{...});`
// Any parameter after `title` may be null to omit.

template <class Fn>
inline void EmptyState(const char* title, const char* hint = nullptr,
                       const char* action_label = nullptr, Fn&& on_action = []{})
{
    const VanVec2 avail = VanGui::GetContentRegionAvail();
    const float   pad_y = (avail.y > 120.0f) ? (avail.y * 0.3f) : 12.0f;
    Spacer(pad_y);
    if (title) { VanGui::PushFont(nullptr, VanGui::GetFontSize() * 1.25f); CenteredText(title); VanGui::PopFont(); }
    if (hint)  { Spacer(4.0f); VanGui::PushStyleColor(VanGuiCol_Text, VanGui::GetColorU32(VanGuiCol_TextDisabled));
                 CenteredText(hint); VanGui::PopStyleColor(); }
    if (action_label) {
        Spacer(8.0f);
        const float bw = VanGui::CalcTextSize(action_label).x + 24.0f;
        const float ax = VanGui::GetCursorPosX() + (avail.x - bw) * 0.5f;
        if (avail.x > bw) VanGui::SetCursorPosX(ax);
        if (VanGui::Button(action_label, VanVec2(bw, 0.0f))) on_action();
    }
}

// EmptyState without an action button — most compact form.
inline void EmptyState(const char* title, const char* hint)
{
    EmptyState<void(*)()>(title, hint, nullptr, +[]{});
}

// ============================================================================
// Labeled row — "Label:   [widget]" one-line composition
// ============================================================================
// vgu::Labeled("Name", []{ VanGui::InputText("##n", buf, sz); });
template <class Fn>
inline void Labeled(const char* label, Fn&& draw_widget, float label_w = 120.0f)
{
    VanGui::AlignTextToFramePadding();
    VanGui::TextUnformatted(label);
    VanGui::SameLine(label_w);
    VanGui::SetNextItemWidth(VanGui::GetContentRegionAvail().x);
    draw_widget();
}

// ============================================================================
// Menu item wired to an id-string — for use with vangui_actions if present
// ============================================================================
// Pure UI helper — does not depend on the Actions module. Renders a MenuItem
// with `label` and `shortcut`; caller decides what to Invoke.
inline bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true)
{
    return VanGui::MenuItem(label, shortcut, selected, enabled);
}

// ============================================================================
// Property — one-line "label + auto-picked widget" bound to a value
// ============================================================================
// Overloaded on the value type; picks Checkbox for bool, DragInt/DragFloat for
// numerics, ColorEdit4 for VanVec4, InputText for buffers. Optional min/max
// switches Drag* -> Slider* automatically. Returns true if the value changed
// this frame.
//
// `Bind` is a synonym kept for symmetry with the model/view story.

inline bool Property(const char* label, bool*  v) { return VanGui::Checkbox(label, v); }
inline bool Property(const char* label, int*   v) { return VanGui::DragInt  (label, v); }
inline bool Property(const char* label, float* v) { return VanGui::DragFloat(label, v); }
inline bool Property(const char* label, int*   v, int   mn, int   mx) { return VanGui::SliderInt  (label, v, mn, mx); }
inline bool Property(const char* label, float* v, float mn, float mx) { return VanGui::SliderFloat(label, v, mn, mx); }
inline bool Property(const char* label, VanVec4* v) { return VanGui::ColorEdit4(label, &v->x); }
inline bool Property(const char* label, char*  buf, size_t n) { return VanGui::InputText(label, buf, n); }

template <class... A> inline bool Bind(const char* label, A&&... a) { return Property(label, (A&&)a...); }

// ============================================================================
// Toast — named shortcuts over vangui_notify
// ============================================================================
// Available whenever vangui_notify is built (notify has no compile gate today
// — it always registers into VanGui::Notify* symbols). Also provides a
// `Progress` helper that returns the toast id so callers can update the bar,
// and a `Dismiss` alias.

namespace Toast {

inline int Info    (const char* fmt, ...) VAN_FMTARGS(1);
inline int Ok      (const char* fmt, ...) VAN_FMTARGS(1);
inline int Warn    (const char* fmt, ...) VAN_FMTARGS(1);
inline int Error   (const char* fmt, ...) VAN_FMTARGS(1);

inline int Info (const char* fmt, ...) { va_list a; va_start(a,fmt); int id = (int)VanGui::NotifyInfoV   (fmt, a); va_end(a); return id; }
inline int Ok   (const char* fmt, ...) { va_list a; va_start(a,fmt); int id = (int)VanGui::NotifySuccessV(fmt, a); va_end(a); return id; }
inline int Warn (const char* fmt, ...) { va_list a; va_start(a,fmt); int id = (int)VanGui::NotifyWarningV(fmt, a); va_end(a); return id; }
inline int Error(const char* fmt, ...) { va_list a; va_start(a,fmt); int id = (int)VanGui::NotifyErrorV  (fmt, a); va_end(a); return id; }

// Progress: creates (first call) or updates (subsequent) a persistent toast
// pinned by `id_string`. Caller keeps the returned int as the notify id.
// `frac < 0` hides the bar without dismissing the toast.
inline int Progress(int notify_id, float frac, const char* fmt, ...) VAN_FMTARGS(3);
inline int Progress(int notify_id, float frac, const char* fmt, ...)
{
    if (notify_id == VanGui::VanNotifyID_Invalid) {
        va_list a; va_start(a, fmt);
        notify_id = (int)VanGui::InsertNotificationV(VanGui::VanNotifyType_Info, 0.0f, fmt, a);
        va_end(a);
    }
    VanGui::SetNotificationProgress(notify_id, frac);
    return notify_id;
}

inline void Dismiss(int notify_id) { VanGui::DismissNotification(notify_id); }
inline void Render () { VanGui::RenderNotifications(); }
inline void Clear  () { VanGui::ClearNotifications(); }

} // namespace Toast

// ============================================================================
// Confirm / Prompt / AskYesNo — one-liner message boxes
// ============================================================================
// Uses vangui_dialogs. Two entry points per helper: the "Ask*" variant opens
// the modal (call from a button/event handler once); the plain form draws and
// returns true on the frame the user picks the affirmative answer.
//
// Pattern:
//     if (VanGui::Button("Delete")) vgu::AskConfirm("delete_evt");
//     if (vgu::Confirm("delete_evt", "Delete this item?"))
//         DoDelete();

#ifdef VANGUI_ENABLE_DIALOGS

inline void AskConfirm (const char* id) { VanGui::OpenMessageBox(id); }
inline void AskYesNo   (const char* id) { VanGui::OpenMessageBox(id); }
inline void AskPrompt  (const char* id) { VanGui::OpenMessageBox(id); }

// Confirm / AskYesNo: Yes+No buttons. Returns true the frame the user clicks Yes.
inline bool Confirm (const char* id, const char* message)
    { return VanGui::MessageBox(id, message, VanGui::VanDialogButtons_YesNo) == VanGui::VanDialog_Yes; }
inline bool AskYesNo(const char* id, const char* message) { return Confirm(id, message); }

// Prompt: Ok+Cancel buttons. Returns true the frame the user clicks Ok.
inline bool Prompt (const char* id, const char* message)
    { return VanGui::MessageBox(id, message, VanGui::VanDialogButtons_OkCancel) == VanGui::VanDialog_Ok; }

// Notify-only message box (Ok button).
inline bool Alert  (const char* id, const char* message)
    { return VanGui::MessageBox(id, message, VanGui::VanDialogButtons_Ok) == VanGui::VanDialog_Ok; }

#endif // VANGUI_ENABLE_DIALOGS

// ============================================================================
// Async — thread-pool work with a modal loading overlay, RAII object
// ============================================================================
// Ships as a small object rather than a bare function so its state (running /
// done) lives with the caller — no hidden globals, no allocations per frame.
// One-liner shape at the call site:
//
//     vgu::Async task;                            // once, member of your view
//     if (VanGui::Button("Load")) task.Run([]{ ExpensiveWork(); });
//     task.DrawOverlay("Loading model...");        // draws overlay while busy
//     if (task.JustFinished()) OnLoaded();

#if defined(VANGUI_ENABLE_THREAD) && defined(VANGUI_ENABLE_LOADING)

#include <atomic>
#include <functional>

class Async
{
public:
    Async() = default;
    Async(const Async&)            = delete;
    Async& operator=(const Async&) = delete;

    // Kicks off `work` on VanGui::Async (thread pool). No-op if already busy.
    template <class Fn>
    void Run(Fn&& work)
    {
        if (m_running.load(std::memory_order_acquire)) return;
        m_running.store(true,  std::memory_order_release);
        m_just_finished = false;
        VanGui::Async(std::function<void()>([this, w = std::function<void()>((Fn&&)work)]() mutable {
            w();
            m_running.store(false, std::memory_order_release);
        }));
    }

    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

    // Returns true on the ONE frame following completion, then latches false.
    [[nodiscard]] bool JustFinished()
    {
        const bool running_now = m_running.load(std::memory_order_acquire);
        if (!running_now && m_was_running) { m_was_running = false; m_just_finished = true;  return true; }
        if ( running_now)                  { m_was_running = true;  m_just_finished = false;                  }
        else                               { m_just_finished = false; }
        return false;
    }

    // Covers the current window with vangui_loading's overlay (one centred
    // spinner and the label) while busy, and keeps clicks off the content
    // underneath. Safe to call every frame, anywhere inside the window.
    void DrawOverlay(const char* label = "Working...")
    {
        (void)VanGui::LoadingOverlay("##vgu_async_overlay", IsRunning(), label);
    }

private:
    std::atomic<bool> m_running{false};
    bool              m_was_running   = false;
    bool              m_just_finished = false;
};

#endif // VANGUI_ENABLE_THREAD && VANGUI_ENABLE_LOADING

} // namespace vgu
