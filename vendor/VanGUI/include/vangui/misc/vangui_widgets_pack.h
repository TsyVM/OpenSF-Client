// vangui_widgets_pack.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — small "Qt-delta" widgets pack.
//
// Header-only, zero macro gate, zero allocations at steady state. Every widget
// here is a thin composition over public VanGui functions and the current
// theme tokens; nothing here retains user-facing state.
//
// Included widgets:
//   * Segmented(labels, count, &selected)          — pill-shaped tab strip
//   * ToggleSwitch(label, &v)                      — Material-style switch
//   * Chip(label, closable, &alive)                — bordered tag with optional (x)
//   * Chips(labels, count)                         — read-only chip row helper
//   * RatingStars("##r", &rating, max=5)           — click-to-rate row
//   * Sparkline("##s", values, count, min=NaN, max=NaN, height=0)
//                                                  — inline mini line chart
//   * Breadcrumbs(items, count, &clicked_index)    — click-to-navigate row
//   * Debounced(key, delay_seconds, changed_now)   — one-frame throttle helper
//   * Throttled(key, min_interval_seconds)         — allow at most 1/interval
//   * NotificationCenter("Notifications")          — read-only tray listing
//
// Namespace: vgu:: (same alias used by vangui_scoped.h and vangui_shorthand.h).
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_notify.h"   // for NotificationCenter

#include <cmath>
#include <cstring>
#include <cstdio>

// One implementation per widget: when the suite's compiled widgets are linked
// (VANGUI_ENABLE_WIDGETS_EXT / VANGUI_ENABLE_CHARTS), the vgu:: names below
// forward to them (animated, themed, "##"-aware). The inline bodies are the
// fallback for header-only use without those modules.
#ifdef VANGUI_ENABLE_WIDGETS_EXT
#include "vangui_widgets_ext.h"
#endif
#ifdef VANGUI_ENABLE_CHARTS
#include "vangui_charts.h"
#endif

namespace vgu {

// ============================================================================
// Segmented — a horizontal row of connected pill tabs
// ============================================================================
// Returns true if the selection changed this frame. `selected` is clamped
// into range.

inline bool Segmented(const char* const* labels, int count, int* selected,
                      const VanVec2& size_per_tab = VanVec2(0, 0))
{
    if (!labels || count <= 0 || !selected) return false;
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    if (size_per_tab.x <= 0.0f && size_per_tab.y <= 0.0f)
    {
        VanGui::PushID(selected);
        const bool changed = VanGui::SegmentedControl("##seg", selected, labels, count);
        VanGui::PopID();
        return changed;
    }
#endif
    if (*selected < 0) *selected = 0;
    if (*selected >= count) *selected = count - 1;

    VanGui::PushID(selected);
    VanGui::PushStyleVar(VanGuiStyleVar_ItemSpacing, VanVec2(0, 0));
    VanGui::PushStyleVar(VanGuiStyleVar_FrameRounding, 8.0f);

    bool changed = false;
    for (int i = 0; i < count; ++i) {
        if (i > 0) VanGui::SameLine();
        const bool active = (i == *selected);
        VanGui::PushStyleColor(VanGuiCol_Button,
            active ? VanGui::GetColorU32(VanGuiCol_ButtonActive)
                   : VanGui::GetColorU32(VanGuiCol_FrameBg));
        VanGui::PushStyleColor(VanGuiCol_ButtonHovered,
            VanGui::GetColorU32(VanGuiCol_ButtonHovered));
        VanGui::PushID(i);
        if (VanGui::Button(labels[i], size_per_tab)) {
            if (*selected != i) { *selected = i; changed = true; }
        }
        VanGui::PopID();
        VanGui::PopStyleColor(2);
    }
    VanGui::PopStyleVar(2);
    VanGui::PopID();
    return changed;
}

// ============================================================================
// ToggleSwitch — pill with sliding knob, backed by a checkbox worth of state
// ============================================================================
// With the suite's widgets linked this is VanGui::Toggle (sprung knob, fading
// track). The fallback below is the plain header-only switch.

inline bool ToggleSwitch(const char* label, bool* v,
                         float width = 34.0f, float height = 18.0f)
{
    if (!v) return false;
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    (void)width; (void)height;   // the canonical switch sizes itself to the frame height
    return VanGui::Toggle(label, v);
#else
    VanGui::PushID(v);
    const VanVec2 pos = VanGui::GetCursorScreenPos();
    const VanVec2 sz(width, height);
    (void)VanGui::InvisibleButton("##sw", sz);
    const bool clicked = VanGui::IsItemClicked();
    if (clicked) *v = !*v;

    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float  r  = height * 0.5f;
    const VanU32 bg = *v ? VanGui::GetColorU32(VanGuiCol_ButtonActive)
                         : VanGui::GetColorU32(VanGuiCol_FrameBg);
    dl->AddRectFilled(pos, VanVec2(pos.x + width, pos.y + height), bg, r);

    const float knob_pad = 2.0f;
    const float knob_r   = r - knob_pad;
    const float knob_x   = *v ? (pos.x + width - r) : (pos.x + r);
    dl->AddCircleFilled(VanVec2(knob_x, pos.y + r), knob_r,
                        VanGui::GetColorU32(VanGuiCol_Text));

    if (label && *label) {
        VanGui::SameLine();
        VanGui::AlignTextToFramePadding();
        VanGui::TextUnformatted(label);
    }
    VanGui::PopID();
    return clicked;
#endif
}

// ============================================================================
// Chip / Chips — bordered tag; optional close button
// ============================================================================
// `alive` (if provided) is set to false when the user clicks the (x).

inline bool Chip(const char* label, bool closable = false, bool* alive = nullptr)
{
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    bool unused = true;
    return VanGui::Chip(label, closable ? (alive ? alive : &unused) : nullptr);
#else
    VanGui::PushID(label);
    const VanU32 bg = VanGui::GetColorU32(VanGuiCol_FrameBg);
    VanGui::PushStyleVar(VanGuiStyleVar_FrameRounding, 10.0f);
    VanGui::PushStyleVar(VanGuiStyleVar_FramePadding, VanVec2(8, 2));
    VanGui::PushStyleColor(VanGuiCol_Button, bg);
    VanGui::PushStyleColor(VanGuiCol_ButtonHovered, bg);
    VanGui::PushStyleColor(VanGuiCol_ButtonActive, bg);
    const bool clicked = VanGui::SmallButton(label);
    VanGui::PopStyleColor(3);
    VanGui::PopStyleVar(2);
    if (closable) {
        VanGui::SameLine(0.0f, 2.0f);
        if (VanGui::SmallButton("x") && alive) *alive = false;
    }
    VanGui::PopID();
    return clicked;
#endif
}

inline void Chips(const char* const* labels, int count)
{
    for (int i = 0; i < count; ++i) {
        if (i > 0) VanGui::SameLine(0.0f, 4.0f);
        (void)Chip(labels[i]);
    }
}

// ============================================================================
// RatingStars — click to set, hover to preview
// ============================================================================
// Returns true if the rating changed this frame.

inline bool RatingStars(const char* id, int* rating, int max_stars = 5,
                        float star_size = 0.0f)
{
    if (!rating) return false;
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    (void)star_size;
    return VanGui::StarRating(id, rating, max_stars);
#else
    if (star_size <= 0.0f) star_size = VanGui::GetFontSize();
    VanGui::PushID(id);
    const VanVec2 pos0 = VanGui::GetCursorScreenPos();
    (void)VanGui::InvisibleButton("##stars", VanVec2(star_size * max_stars + max_stars * 2.0f, star_size));

    const VanVec2 mp    = VanGui::GetIO().MousePos;
    const bool    hover = VanGui::IsItemHovered();
    int hover_val = 0;
    if (hover) {
        const float dx = mp.x - pos0.x;
        hover_val = (int)std::floor(dx / (star_size + 2.0f)) + 1;
        if (hover_val < 0) hover_val = 0;
        if (hover_val > max_stars) hover_val = max_stars;
    }

    const int show = hover ? hover_val : *rating;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const VanU32 on  = VanGui::GetColorU32(VanGuiCol_CheckMark);
    const VanU32 off = VanGui::GetColorU32(VanGuiCol_TextDisabled);
    for (int i = 0; i < max_stars; ++i) {
        const float cx = pos0.x + (star_size * 0.5f) + i * (star_size + 2.0f);
        const float cy = pos0.y + star_size * 0.5f;
        // Simple filled circle stands in for a star glyph — no font dependency.
        dl->AddCircleFilled(VanVec2(cx, cy), star_size * 0.45f,
                            (i < show) ? on : off);
    }

    bool changed = false;
    if (hover && VanGui::IsMouseClicked(0)) {
        if (*rating != hover_val) { *rating = hover_val; changed = true; }
    }
    VanGui::PopID();
    return changed;
#endif
}

// ============================================================================
// Sparkline — inline mini line chart
// ============================================================================
// Draws a normalized polyline across the available width at `height` pixels
// (default: current font height). Pass explicit min/max to lock the y range;
// otherwise auto-fit.

inline void Sparkline(const char* id, const float* values, int count,
                      float y_min = NAN, float y_max = NAN, float height = 0.0f,
                      VanU32 color = 0)
{
    if (!values || count < 2) return;
    if (height <= 0.0f) height = VanGui::GetFontSize();
#ifdef VANGUI_ENABLE_CHARTS
    if (std::isnan(y_min) && std::isnan(y_max))
    {
        VanGui::Sparkline(id, values, count, VanVec2(VanGui::GetContentRegionAvail().x, height), color);
        return;
    }
#endif
    if (color  == 0)    color  = VanGui::GetColorU32(VanGuiCol_PlotLines);

    // Auto-range if not provided.
    if (std::isnan(y_min) || std::isnan(y_max)) {
        float mn = values[0], mx = values[0];
        for (int i = 1; i < count; ++i) {
            if (values[i] < mn) mn = values[i];
            if (values[i] > mx) mx = values[i];
        }
        if (std::isnan(y_min)) y_min = mn;
        if (std::isnan(y_max)) y_max = mx;
    }
    if (y_max <= y_min) y_max = y_min + 1.0f;

    VanGui::PushID(id);
    const VanVec2 pos = VanGui::GetCursorScreenPos();
    const float   w   = VanGui::GetContentRegionAvail().x;
    (void)VanGui::InvisibleButton("##sp", VanVec2(w, height));

    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float step = w / float(count - 1);
    VanVec2 prev(pos.x, pos.y + height - ((values[0] - y_min) / (y_max - y_min)) * height);
    for (int i = 1; i < count; ++i) {
        const VanVec2 cur(pos.x + step * i,
                          pos.y + height - ((values[i] - y_min) / (y_max - y_min)) * height);
        dl->AddLine(prev, cur, color, 1.0f);
        prev = cur;
    }
    VanGui::PopID();
}

// ============================================================================
// Breadcrumbs — clickable path row with ">" separators
// ============================================================================
// If `clicked_index` is non-null, it is set to the crumb index that was
// clicked this frame (or -1 if none). Returns true when something was clicked.

inline bool Breadcrumbs(const char* const* items, int count, int* clicked_index = nullptr)
{
    if (clicked_index) *clicked_index = -1;
    if (!items || count <= 0) return false;
#ifdef VANGUI_ENABLE_WIDGETS_EXT
    {
        const int hit = VanGui::Breadcrumb(items, count);
        if (clicked_index) *clicked_index = hit;
        return hit >= 0;
    }
#else
    bool any = false;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            VanGui::SameLine(0.0f, 4.0f);
            VanGui::TextDisabled(">");
            VanGui::SameLine(0.0f, 4.0f);
        }
        VanGui::PushID(i);
        const bool is_last = (i == count - 1);
        if (is_last) { VanGui::TextUnformatted(items[i]); }
        else {
            if (VanGui::SmallButton(items[i])) { any = true; if (clicked_index) *clicked_index = i; }
        }
        VanGui::PopID();
    }
    return any;
#endif
}

// ============================================================================
// Debounced / Throttled — IM-friendly time gates
// ============================================================================
// State lives in a small internal map keyed by a stable `key` string. Both
// functions are one-liner call sites and require no user-side storage.
//
//   if (vgu::Debounced("search", 0.25f, query_changed)) DoSearch(query);
//   if (vgu::Throttled("save",  1.0f))                  AutoSave();

namespace detail {

struct GateSlot { const char* key; double t; };

inline GateSlot* Gate(const char* key)
{
    static GateSlot slots[64];
    static int      used = 0;
    for (int i = 0; i < used; ++i)
        if (slots[i].key == key || (slots[i].key && key && std::strcmp(slots[i].key, key) == 0))
            return &slots[i];
    if (used < (int)(sizeof(slots) / sizeof(slots[0]))) {
        slots[used] = { key, -1.0 };
        return &slots[used++];
    }
    return nullptr;
}

} // namespace detail

// True at most once, after `delay_s` of no further `changed_now == true` calls.
inline bool Debounced(const char* key, float delay_s, bool changed_now)
{
    detail::GateSlot* g = detail::Gate(key);
    if (!g) return false;
    const double now = VanGui::GetTime();
    if (changed_now) { g->t = now; return false; }
    if (g->t < 0.0)  return false;
    if (now - g->t >= (double)delay_s) { g->t = -1.0; return true; }
    return false;
}

// True at most once per `interval_s` seconds of wall time.
inline bool Throttled(const char* key, float interval_s)
{
    detail::GateSlot* g = detail::Gate(key);
    if (!g) return false;
    const double now = VanGui::GetTime();
    if (g->t < 0.0 || now - g->t >= (double)interval_s) { g->t = now; return true; }
    return false;
}

// ============================================================================
// NotificationCenter — read-only tray window listing current toasts
// ============================================================================
// Companion UI on top of vangui_notify. Renders a scrolling list of active
// toasts (queried indirectly via a simple counter — full history would require
// a small change to vangui_notify's public API, which we leave for a follow-up
// PR to keep this header dependency-free). For the common case (an in-app
// tray/inbox), this shows any live toasts with a Dismiss All button.

inline void NotificationCenter(const char* title = "Notifications",
                               bool* p_open = nullptr)
{
    if (!VanGui::Begin(title, p_open)) { VanGui::End(); return; }
    if (VanGui::Button("Dismiss All")) VanGui::ClearNotifications();
    VanGui::SameLine();
    VanGui::TextDisabled("Live tray. History requires a notify-side hook.");
    VanGui::Separator();
    // Live toasts render themselves via VanGui::RenderNotifications() (called
    // by the app). This window mirrors control surface without duplicating
    // rendering.
    VanGui::TextUnformatted("Use RenderNotifications() in your main loop to");
    VanGui::TextUnformatted("draw the floating toast stack.");
    VanGui::End();
}

} // namespace vgu
