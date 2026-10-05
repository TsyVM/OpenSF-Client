// vangui_loading.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 2: loading & busy-state effects.
//
//   Spinners        Spinner (growing/shrinking arc), SpinnerComet (gradient
//                   tail), SpinnerDots, SpinnerBars, SpinnerPulse (ripple),
//                   TypingDots, SpinnerOrbit, SpinnerDualRing, SpinnerEx (one
//                   entry point that fades in/out with a `busy` flag).
//   Progress        ProgressRing (label, indeterminate mode, gradient),
//                   IndeterminateBar, ProgressBarSmooth, ProgressBarBuffered,
//                   ProgressSegments, CountdownRing.
//   State           SpinnerStatus (busy -> check / cross morph), BusyButton.
//   Placeholders    Skeleton, SkeletonText, SkeletonCircle — one shimmer sweep
//                   shared by every skeleton on screen.
//   Cover           BeginLoadingOverlay/EndLoadingOverlay (dims and disables a
//                   region), LoadingOverlay (covers a whole window),
//                   SplashScreen (full-screen boot/loading screen).
//
// Everything that moves reads the animation clock (Anim::Phase), so it keeps an
// idle-sleeping app drawing while it is on screen and stays smooth however long
// the app has been running. Sizes of 0 follow the current font size (DPI).
// Strokes use the vector module: round caps, clean translucency.
//
// DESIGN CONTRACT
//   * Opt-in / zero-cost-when-off: define VANGUI_ENABLE_LOADING. When undefined,
//     every entry point is an inline no-op.
//   * VANGUI_ENABLE_LOADING requires VANGUI_ENABLE_ANIM, VANGUI_ENABLE_VECTOR and
//     VANGUI_ENABLE_EFFECTS (enforced below with #error).
//   * No per-frame heap allocations: strings are caller-owned.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#if defined(VANGUI_ENABLE_LOADING) && (!defined(VANGUI_ENABLE_ANIM) || !defined(VANGUI_ENABLE_VECTOR) || !defined(VANGUI_ENABLE_EFFECTS))
#  error "VANGUI_ENABLE_LOADING requires VANGUI_ENABLE_ANIM, VANGUI_ENABLE_VECTOR and VANGUI_ENABLE_EFFECTS."
#endif

namespace VanGui {

// ---------------------------------------------------------------------------
// Shared low-level progress-bar primitive (DRY).
// ---------------------------------------------------------------------------
// Header-only so both vangui_loading and vangui_notify draw progress through the
// exact same code path without a link dependency between the two modules.
namespace Detail {
inline void ProgressBarPrim(VanDrawList* dl, const VanVec2& p_min, const VanVec2& p_max,
                            float fraction, VanU32 track_col, VanU32 fill_col,
                            float rounding = 2.0f)
{
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    dl->AddRectFilled(p_min, p_max, track_col, rounding);
    if (fraction > 0.0f)
    {
        const VanVec2 fill_max(p_min.x + (p_max.x - p_min.x) * fraction, p_max.y);
        dl->AddRectFilled(p_min, fill_max, fill_col, rounding);
    }
}
} // namespace Detail

enum VanSpinnerStyle
{
    VanSpinnerStyle_Arc = 0,     // growing/shrinking arc (the default spinner)
    VanSpinnerStyle_Classic,     // fixed three-quarter arc turning at a steady rate
    VanSpinnerStyle_Comet,       // arc with a fading tail
    VanSpinnerStyle_Dots,        // ring of dots, brightness chasing round
    VanSpinnerStyle_Bars,        // row of pulsing bars
    VanSpinnerStyle_Pulse,       // rings rippling outward
    VanSpinnerStyle_Typing,      // three bouncing dots
    VanSpinnerStyle_Orbit,       // dots orbiting at different speeds
    VanSpinnerStyle_DualRing,    // two arcs turning opposite ways
    VanSpinnerStyle_Ring,        // indeterminate progress ring (with track)
    VanSpinnerStyle_COUNT
};

enum VanBusyState
{
    VanBusyState_Idle = 0,
    VanBusyState_Busy,
    VanBusyState_Success,
    VanBusyState_Error,
};

struct VanSpinnerParams
{
    VanSpinnerStyle Style     = VanSpinnerStyle_Arc;
    float           Radius    = 0.0f;   // 0 = half the font size
    float           Thickness = 0.0f;   // 0 = radius / 5
    VanU32          Color     = 0;      // 0 = text colour
    VanU32          Color2    = 0;      // second colour (dual ring, orbit); 0 = accent
    float           Speed     = 1.0f;
    bool            KeepSpace = true;   // reserve layout space while hidden (SpinnerEx)
};

struct VanProgressRingParams
{
    float       Radius     = 0.0f;      // 0 = font size
    float       Thickness  = 0.0f;      // 0 = radius / 6
    VanU32      Color      = 0;         // 0 = accent (PlotHistogram)
    VanU32      ColorEnd   = 0;         // non-zero: gradient along the arc
    VanU32      TrackColor = 0;         // 0 = FrameBg
    const char* Format     = nullptr;   // centre label, e.g. "%.0f%%" (gets fraction*100); nullptr = none
    bool        Indeterminate = false;  // also implied by fraction < 0
};

struct VanSplashParams
{
    const char* Title     = nullptr;
    const char* Subtitle  = nullptr;
    const char* Status    = nullptr;
    float       Progress  = -1.0f;      // < 0 = indeterminate
    VanU32      Background = 0;         // 0 = WindowBg, opaque
    VanU32      Accent     = 0;         // 0 = PlotHistogram
    float       FadeOut    = 0.35f;     // seconds
    // Optional logo painter, called with the square it may fill and the 0..1 fade.
    void      (*DrawLogo)(VanDrawList* dl, const VanVec2& min, const VanVec2& max, float alpha, void* user) = nullptr;
    void*       UserData   = nullptr;
};

// Colours the state widgets use. Edit the returned reference.
struct VanLoadingStyle
{
    VanU32 Accent  = 0;                          // 0 = PlotHistogram
    VanU32 Success = VAN_COL32(63, 185, 80, 255);
    VanU32 Error   = VAN_COL32(248, 81, 73, 255);
    VanU32 Warning = VAN_COL32(210, 153, 34, 255);
    float  OverlayDim = 0.6f;                   // overlay background alpha
};

#ifdef VANGUI_ENABLE_LOADING

VANGUI_API VanLoadingStyle& GetLoadingStyle();

// --- Spinners (radius 0 = font-relative, color 0 = text colour) --------------
VANGUI_API void Spinner(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0, float speed = 1.0f);
VANGUI_API void SpinnerClassic(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0, float speed = 1.0f);
VANGUI_API void SpinnerComet(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0, float speed = 1.0f);
VANGUI_API void SpinnerDots(const char* id, float radius = 0.0f, int count = 8, VanU32 color = 0);
VANGUI_API void SpinnerBars(const char* id, VanVec2 size = VanVec2(0, 0), VanU32 color = 0);
VANGUI_API void SpinnerPulse(const char* id, float radius = 0.0f, VanU32 color = 0);
VANGUI_API void TypingDots(const char* id, VanVec2 size = VanVec2(0, 0), VanU32 color = 0);
VANGUI_API void SpinnerOrbit(const char* id, float radius = 0.0f, VanU32 color = 0, VanU32 color2 = 0);
VANGUI_API void SpinnerDualRing(const char* id, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0, VanU32 color2 = 0);
// Any style, fading in when `busy` turns on and out when it turns off.
// Returns true while any of it is visible.
VANGUI_API bool SpinnerEx(const char* id, bool busy, const VanSpinnerParams& p = VanSpinnerParams());
// Draw a spinner of `style` at an explicit place (no layout), with `alpha` fade.
VANGUI_API void DrawSpinner(VanDrawList* dl, const VanVec2& centre, const VanSpinnerParams& p, float alpha = 1.0f);

// --- Progress ------------------------------------------------------------------
// Sliding segment inside a rounded track. Width 0 = available width.
VANGUI_API void IndeterminateBar(const char* id, VanVec2 size = VanVec2(0, 0), VanU32 color = 0);
// Circular progress, smoothed. fraction < 0 = indeterminate.
VANGUI_API void ProgressRing(const char* id, float fraction, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0);
VANGUI_API void ProgressRing(const char* id, float fraction, const VanProgressRingParams& p);
// A bar whose fill eases to the value and carries a moving sheen until complete.
// `overlay` (optional) is drawn centred, e.g. "42%".
VANGUI_API void ProgressBarSmooth(const char* id, float fraction, VanVec2 size = VanVec2(0, 0), const char* overlay = nullptr, VanU32 color = 0);
// Value plus a lighter "buffered" extent ahead of it (streaming, downloads).
VANGUI_API void ProgressBarBuffered(const char* id, float value, float buffer, VanVec2 size = VanVec2(0, 0), VanU32 color = 0);
// `segments` separate blocks; `value` in 0..segments fills them (fractional fills the current one).
VANGUI_API void ProgressSegments(const char* id, float value, int segments, VanVec2 size = VanVec2(0, 0), VanU32 color = 0);
// Ring that empties as time runs out, with the seconds left in the middle.
VANGUI_API void CountdownRing(const char* id, float remaining, float total, float radius = 0.0f, float thickness = 0.0f, VanU32 color = 0);

// --- State -----------------------------------------------------------------------
// A spinner while busy that closes into a circle and draws a check (success) or
// a cross (error); fades away when idle.
VANGUI_API void SpinnerStatus(const char* id, VanBusyState state, float radius = 0.0f, VanU32 color = 0);
// A button that swaps its label for a spinner while busy and for a check/cross
// afterwards, keeping its width. Clicks register only when Idle.
VANGUI_API bool BusyButton(const char* label, VanBusyState state, VanVec2 size = VanVec2(0, 0));

// --- Skeletons (one shimmer sweep across the screen) -----------------------------
VANGUI_API void Skeleton(VanVec2 size = VanVec2(0, 0), float rounding = 4.0f);
VANGUI_API void SkeletonText(int lines, float line_height = 0.0f);
VANGUI_API void SkeletonCircle(float radius = 0.0f);

// --- Covers ----------------------------------------------------------------------
// Wraps content: while `busy`, the content is disabled and dimmed with a centred
// spinner (and optional label) drawn over it. Always pair with EndLoadingOverlay.
// Returns true while the overlay is at least partly visible.
VANGUI_API bool BeginLoadingOverlay(const char* id, bool busy);
VANGUI_API void EndLoadingOverlay(const char* label = nullptr);
// Covers the whole current window (call anywhere inside it). It sits in its own
// window above this one, so it takes the mouse and nothing underneath reacts.
VANGUI_API bool LoadingOverlay(const char* id, bool busy, const char* label = nullptr);
// Full-screen boot/loading screen over everything. Fades out when `show` turns
// false. Returns true while visible.
VANGUI_API bool SplashScreen(const char* id, bool show, const VanSplashParams& p = VanSplashParams());

// RAII guard for BeginLoadingOverlay/EndLoadingOverlay.
struct VanLoadingOverlayScope
{
    bool        Visible;
    const char* Label;
    explicit VanLoadingOverlayScope(const char* id, bool busy, const char* label = nullptr) : Label(label) { Visible = BeginLoadingOverlay(id, busy); }
    ~VanLoadingOverlayScope() { EndLoadingOverlay(Label); }
    explicit operator bool() const { return Visible; }
    VanLoadingOverlayScope(const VanLoadingOverlayScope&) = delete;
    VanLoadingOverlayScope& operator=(const VanLoadingOverlayScope&) = delete;
};

#else // ----------------------------- shims -----------------------------------

inline VanLoadingStyle& GetLoadingStyle() { static VanLoadingStyle s; return s; }
inline void Spinner(const char*, float = 0.0f, float = 0.0f, VanU32 = 0, float = 1.0f) {}
inline void SpinnerClassic(const char*, float = 0.0f, float = 0.0f, VanU32 = 0, float = 1.0f) {}
inline void SpinnerComet(const char*, float = 0.0f, float = 0.0f, VanU32 = 0, float = 1.0f) {}
inline void SpinnerDots(const char*, float = 0.0f, int = 8, VanU32 = 0) {}
inline void SpinnerBars(const char*, VanVec2 = VanVec2(0, 0), VanU32 = 0) {}
inline void SpinnerPulse(const char*, float = 0.0f, VanU32 = 0) {}
inline void TypingDots(const char*, VanVec2 = VanVec2(0, 0), VanU32 = 0) {}
inline void SpinnerOrbit(const char*, float = 0.0f, VanU32 = 0, VanU32 = 0) {}
inline void SpinnerDualRing(const char*, float = 0.0f, float = 0.0f, VanU32 = 0, VanU32 = 0) {}
inline bool SpinnerEx(const char*, bool, const VanSpinnerParams& = VanSpinnerParams()) { return false; }
inline void DrawSpinner(VanDrawList*, const VanVec2&, const VanSpinnerParams&, float = 1.0f) {}
inline void IndeterminateBar(const char*, VanVec2 = VanVec2(0, 0), VanU32 = 0) {}
inline void ProgressRing(const char*, float, float = 0.0f, float = 0.0f, VanU32 = 0) {}
inline void ProgressRing(const char*, float, const VanProgressRingParams&) {}
inline void ProgressBarSmooth(const char*, float, VanVec2 = VanVec2(0, 0), const char* = nullptr, VanU32 = 0) {}
inline void ProgressBarBuffered(const char*, float, float, VanVec2 = VanVec2(0, 0), VanU32 = 0) {}
inline void ProgressSegments(const char*, float, int, VanVec2 = VanVec2(0, 0), VanU32 = 0) {}
inline void CountdownRing(const char*, float, float, float = 0.0f, float = 0.0f, VanU32 = 0) {}
inline void SpinnerStatus(const char*, VanBusyState, float = 0.0f, VanU32 = 0) {}
inline bool BusyButton(const char* label, VanBusyState state, VanVec2 size = VanVec2(0, 0)) { return VanGui::Button(label, size) && state == VanBusyState_Idle; }
inline void Skeleton(VanVec2 = VanVec2(0, 0), float = 4.0f) {}
inline void SkeletonText(int, float = 0.0f) {}
inline void SkeletonCircle(float = 0.0f) {}
inline bool BeginLoadingOverlay(const char*, bool) { return false; }
inline void EndLoadingOverlay(const char* = nullptr) {}
inline bool LoadingOverlay(const char*, bool, const char* = nullptr) { return false; }
inline bool SplashScreen(const char*, bool, const VanSplashParams& = VanSplashParams()) { return false; }

struct VanLoadingOverlayScope
{
    bool Visible = false;
    explicit VanLoadingOverlayScope(const char*, bool, const char* = nullptr) {}
    ~VanLoadingOverlayScope() {}
    explicit operator bool() const { return false; }
};

#endif // VANGUI_ENABLE_LOADING

} // namespace VanGui
