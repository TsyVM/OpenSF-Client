// vangui_extras.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — everyday widgets and the lightweight helpers.
//
//   Widgets     CopyButton, Keycap, Avatar, EmptyState, PasswordInput,
//               TextEllipsis, KeyValue, DividerText, BadgeDot, RollingNumber
//   Idle cost   BeginCached/EndCached replays a panel's last drawing while
//               nothing about it changed; VirtualList draws only visible rows
//   Tools       CaptureWindow / CaptureScreen write the last frame to PNG with
//               VanGUI's own rasterizer (needs vangui_raster)
//
// Icons come from vangui_icons when it is built (the copy/check morph, the eye);
// without it the same widgets use short text. Motion comes from vangui_anim and
// snaps without it. Formatting helpers (FormatBytes, FormatDuration,
// FormatRelativeTime) are header-only in vangui_format.h.
//
// Opt-in / zero-cost via VANGUI_ENABLE_EXTRAS.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_icons.h"   // VanIconID and the icon enum (declared with or without the icons module)
#include <string.h>         // VanTags

namespace VanGui {

// A list of short strings for TagInput(): owns copies, keeps insertion order,
// ignores empty and duplicate (case-insensitive) entries. Usable without the module.
struct VanTags
{
    VanVector<char*> Items;

    VanTags() {}
    VanTags(const VanTags&) = delete;
    VanTags& operator=(const VanTags&) = delete;
    ~VanTags() { Clear(); }

    int         Size() const { return Items.Size; }
    const char* operator[](int i) const { return Items[i]; }
    bool Contains(const char* s) const
    {
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
        for (const char* t : Items)
        {
            const char* a = t; const char* b = s;
            while (*a && *b && lower(*a) == lower(*b)) { ++a; ++b; }
            if (*a == 0 && *b == 0) return true;
        }
        return false;
    }
    // Trims spaces; returns false for an empty or duplicate tag.
    bool Add(const char* s)
    {
        while (*s == ' ') ++s;
        size_t n = strlen(s);
        while (n > 0 && s[n - 1] == ' ') --n;
        if (n == 0) return false;
        char* copy = (char*)VanGui::MemAlloc(n + 1);
        memcpy(copy, s, n);
        copy[n] = 0;
        if (Contains(copy)) { VanGui::MemFree(copy); return false; }
        Items.push_back(copy);
        return true;
    }
    void Remove(int i) { if (i < 0 || i >= Items.Size) return; VanGui::MemFree(Items[i]); Items.erase(Items.Data + i); }
    void Clear() { for (char* t : Items) VanGui::MemFree(t); Items.clear(); }
};

// A picture loaded from a file by GetImage(). Tex is valid once Ready.
struct VanImageFile
{
    VanTextureRef Tex;
    VanVec2       Size   = VanVec2(0, 0);
    bool          Ready  = false;
    bool          Failed = false;   // missing, unreadable, or not a PNG
};

#ifdef VANGUI_ENABLE_EXTRAS

// Copies `text` to the clipboard when clicked; the copy icon then turns into a
// check for about a second. Returns true on the click.
VANGUI_API bool CopyButton(const char* str_id, const char* text, float size = 0.0f);

// Key caps: Keycap("Ctrl+Shift+P") draws each key in its own cap. "+" alone, or
// "++" at the end, is the plus key.
VANGUI_API void Keycap(const char* keys);

// A round avatar: `image` clipped to a circle, or the initials of `name` on a
// colour picked from the name (the same name always gets the same colour).
// size 0 = two text lines. `ring` != 0 draws a status ring in that colour.
VANGUI_API void Avatar(const char* name, float size = 0.0f, VanTextureID image = VanTextureID_Invalid, VanU32 ring = 0);

// The "nothing here yet" block: icon, title and hint centred in the space left,
// with an optional action button. Returns true when the action is clicked.
VANGUI_API bool EmptyState(VanIconID icon, const char* title, const char* hint = nullptr, const char* action = nullptr);

// A password field with an eye button that shows and hides the text.
VANGUI_API bool PasswordInput(const char* label, char* buf, size_t buf_size, VanGuiInputTextFlags flags = 0);

// One line of text cut to `width` (0 = the space left) with an ellipsis; the whole
// text shows in a tooltip when it was cut.
VANGUI_API void TextEllipsis(const char* text, float width = 0.0f);

// A dimmed key and its value, lined up in two columns (key_width 0 = 40% of the row).
VANGUI_API void KeyValue(const char* key, const char* value, float key_width = 0.0f);
VANGUI_API void KeyValueF(const char* key, const char* fmt, ...) VAN_FMTARGS(2);

// A separator with a centred label: ------ OR ------
VANGUI_API void DividerText(const char* text);

// A notification badge on the corner of the last item: a dot when count <= 0,
// otherwise the count in a pill ("99+" past 99). col 0 = a red.
VANGUI_API void BadgeDot(int count = 0, VanU32 col = 0);

// A number whose digits roll like an odometer to each new value. The value eases
// over `duration` seconds; digits are drawn in equal-width columns so nothing
// shifts while they roll. Precise to about seven significant digits.
VANGUI_API void RollingNumber(const char* str_id, double value, int decimals = 0, const char* prefix = nullptr,
                              const char* suffix = nullptr, float duration = 0.6f);

// Cached region. While the region is not hovered, not marked dirty and nothing
// around it changed (its position, clip, the style, the font atlas), BeginCached
// replays last frame's drawing and returns false: skip the contents. Otherwise it
// returns true; draw them and call EndCached, which records them.
//     if (VanGui::BeginCached("stats", stats_changed)) { DrawStats(); VanGui::EndCached(); }
// The contents must draw into the current window (no child windows inside), and
// pass dirty = true while anything inside animates.
VANGUI_API bool BeginCached(const char* str_id, bool dirty = false);
VANGUI_API void EndCached();
VANGUI_API void ClearCachedRegions();
// Regions held, and how many were replayed instead of redrawn this frame.
VANGUI_API void GetCachedRegionStats(int* out_regions, int* out_replayed_this_frame);

// After VanGui::Render(): write the named window (with its child windows) or the
// whole frame as a PNG, using the CPU rasterizer. `background` fills what the
// window leaves transparent (0 = transparent). Needs vangui_raster; false without.
VANGUI_API bool CaptureWindow(const char* window_name, const char* png_path, VanU32 background = 0);
VANGUI_API bool CaptureScreen(const char* png_path, VanU32 background = VAN_COL32(0, 0, 0, 255));

// Lazy section: while it is off-screen (with half a window of margin, so keyboard
// navigation and small scrolls find it ready), the contents are not built at all;
// the space they took last time (or `estimated_height` before they were ever
// drawn) is reserved instead. Returns true when the contents must be drawn.
//     if (VanGui::BeginLazy("advanced", 400.0f)) { DrawAdvanced(); VanGui::EndLazy(); }
VANGUI_API bool BeginLazy(const char* str_id, float estimated_height = 0.0f);
VANGUI_API void EndLazy();

// Auto-height panel: the contents are laid out in a region whose height eases to
// theirs as they grow and shrink; what doesn't fit yet is clipped (not clickable).
VANGUI_API void BeginAutoHeight(const char* str_id, float duration = 0.18f);
VANGUI_API void EndAutoHeight();

// Loading swap: while `ready` is false, a shimmering skeleton of `skeleton_lines`
// text lines is drawn and this returns false (skip the contents). When it turns
// true the contents fade in over the fading skeleton. Call EndLoadingSwap() only
// when it returned true.
VANGUI_API bool BeginLoadingSwap(const char* str_id, bool ready, int skeleton_lines = 3);
VANGUI_API void EndLoadingSwap();

// Rasterize the glyphs of `text` (or a code point range) now, so a panel opened
// later doesn't stall while the atlas grows. Font/size default to the current
// ones. Call inside a frame. Returns how many glyphs were newly baked.
VANGUI_API int  PrebakeGlyphs(const char* text, VanFont* font = nullptr, float size = 0.0f);
VANGUI_API int  PrebakeGlyphRange(unsigned int first, unsigned int last, VanFont* font = nullptr, float size = 0.0f);

// Two-handle slider: *v_lo <= *v_hi within [v_min, v_max]. Drag either handle
// (the nearer one is picked). Returns true when either changed.
VANGUI_API bool SliderRange(const char* label, float* v_lo, float* v_hi, float v_min, float v_max, const char* format = "%.2f");
VANGUI_API bool SliderRangeInt(const char* label, int* v_lo, int* v_hi, int v_min, int v_max);

// Tags as chips followed by an input: Enter (or a comma) adds, the chip's x or
// Backspace in the empty input removes. Returns true when the list changed.
VANGUI_API bool TagInput(const char* str_id, VanTags* tags, const char* hint = "Add a tag");

// A statistic: label, large value and, unless delta is FLT_MAX, a change with an
// arrow -- green when it goes the good way (`up_is_good`), red when not.
VANGUI_API void StatCard(const char* label, const char* value, float delta = FLT_MAX, const char* delta_suffix = "%",
                         bool up_is_good = true, float width = 0.0f);

// Text with every (ASCII case-insensitive) match of `query` marked and in bold.
// Returns true when there was a match.
VANGUI_API bool TextHighlight(const char* text, const char* query, VanU32 highlight = 0);

// Images from files (PNG). The first call starts loading -- on a worker thread
// when vangui_thread is built -- and the texture is uploaded by the renderer
// backend like the font atlas. Images not asked for in ~10 s are freed.
VANGUI_API VanImageFile GetImage(const char* path);
// Draws it at `size` (0 = its own size; one axis 0 = keep the aspect), with a
// skeleton while it loads. Returns true once it is showing.
VANGUI_API bool ImageFile(const char* path, VanVec2 size = VanVec2(0, 0), float rounding = 0.0f);
VANGUI_API void ClearImageCache();
VANGUI_API int  GetImageCacheSize();

#else // ----------------------------- shims -----------------------------------

inline bool CopyButton(const char* str_id, const char* text, float = 0.0f) { const bool c = VanGui::SmallButton(str_id); if (c) VanGui::SetClipboardText(text); return c; }
inline void Keycap(const char* keys) { VanGui::TextUnformatted(keys); }
inline void Avatar(const char*, float = 0.0f, VanTextureID = VanTextureID_Invalid, VanU32 = 0) {}
inline bool EmptyState(VanIconID, const char* title, const char* = nullptr, const char* = nullptr) { VanGui::TextUnformatted(title); return false; }
inline bool PasswordInput(const char* label, char* buf, size_t buf_size, VanGuiInputTextFlags flags = 0) { return VanGui::InputText(label, buf, buf_size, flags | VanGuiInputTextFlags_Password); }
inline void TextEllipsis(const char* text, float = 0.0f) { VanGui::TextUnformatted(text); }
inline void KeyValue(const char* key, const char* value, float = 0.0f) { VanGui::Text("%s: %s", key, value); }
inline void KeyValueF(const char*, const char*, ...) {}
inline void DividerText(const char* text) { VanGui::SeparatorText(text); }
inline void BadgeDot(int = 0, VanU32 = 0) {}
inline void RollingNumber(const char*, double value, int decimals = 0, const char* = nullptr, const char* = nullptr, float = 0.6f) { VanGui::Text("%.*f", decimals, value); }
inline bool BeginCached(const char*, bool = false) { return true; }
inline void EndCached() {}
inline void ClearCachedRegions() {}
inline void GetCachedRegionStats(int* a, int* b) { if (a) *a = 0; if (b) *b = 0; }
inline bool CaptureWindow(const char*, const char*, VanU32 = 0) { return false; }
inline bool CaptureScreen(const char*, VanU32 = VAN_COL32(0, 0, 0, 255)) { return false; }
inline bool BeginLazy(const char*, float = 0.0f) { return true; }
inline void EndLazy() {}
inline void BeginAutoHeight(const char*, float = 0.18f) {}
inline void EndAutoHeight() {}
inline bool BeginLoadingSwap(const char*, bool ready, int = 3) { return ready; }
inline void EndLoadingSwap() {}
inline int  PrebakeGlyphs(const char*, VanFont* = nullptr, float = 0.0f) { return 0; }
inline int  PrebakeGlyphRange(unsigned int, unsigned int, VanFont* = nullptr, float = 0.0f) { return 0; }
inline bool SliderRange(const char* label, float* lo, float* hi, float mn, float mx, const char* = "%.2f") { const bool a = VanGui::SliderFloat(label, lo, mn, *hi); return a; }
inline bool SliderRangeInt(const char* label, int* lo, int* hi, int mn, int) { return VanGui::SliderInt(label, lo, mn, *hi); }
inline bool TagInput(const char*, VanTags*, const char* = "Add a tag") { return false; }
inline void StatCard(const char* label, const char* value, float = FLT_MAX, const char* = "%", bool = true, float = 0.0f) { VanGui::Text("%s: %s", label, value); }
inline bool TextHighlight(const char* text, const char*, VanU32 = 0) { VanGui::TextUnformatted(text); return false; }
inline VanImageFile GetImage(const char*) { VanImageFile f; f.Failed = true; return f; }
inline bool ImageFile(const char*, VanVec2 = VanVec2(0, 0), float = 0.0f) { return false; }
inline void ClearImageCache() {}
inline int  GetImageCacheSize() { return 0; }

#endif // VANGUI_ENABLE_EXTRAS

// Long lists: draws only the rows in view. `draw_row(i)` is called for each
// visible index; row_height <= 0 measures it from the first row.
//     VanGui::VirtualList(100000, 0.0f, [&](int i) { VanGui::Text("Row %d", i); });
template <class Fn>
inline void VirtualList(int count, float row_height, Fn&& draw_row)
{
    VanGuiListClipper clipper;
    clipper.Begin(count, row_height);
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            draw_row(i);
    clipper.End();
}

} // namespace VanGui
