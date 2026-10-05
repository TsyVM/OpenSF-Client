// vangui_anim.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — Pillar 1: shared animation & transition substrate.
//
// This is the single, authoritative place where VanGUI turns "a target value"
// into "a value that moves toward the target over time". Every higher-level
// enhancement (loading effects, dialog open/close, theme cross-fades, toast
// slides, widget motion) is built on top of this — nothing reimplements
// easing or time integration. (Engineering Constitution, Ch.1: DRY.)
//
// DESIGN CONTRACT
//   * Opt-in / zero-cost-when-off:
//       - Define VANGUI_ENABLE_ANIM to compile the real implementation.
//       - When it is NOT defined, every public function below is an inline
//         no-op shim that snaps to the target instantly and allocates nothing,
//         so calling code compiles and runs identically either way.
//   * Immediate-mode preserved:
//       - No persistent user objects. State is keyed by VanGuiID (the existing
//         ID stack) and stored context-side, exactly like tables/windows.
//   * No per-frame heap allocations: the state pools grow once and recycle
//     slots; eviction marks slots free.
//   * Fallible nothing: these calls cannot fail. They are noexcept-friendly
//     and return by value. (No exceptions; cf. Constitution Ch. Error Handling.)
//
// USAGE
//   1. Build with VANGUI_ENABLE_ANIM (the VANGUI_MISC_ANIM / VANGUI_BUILD_SUITE
//      CMake options define it on the core as well). VanGui::NewFrame() then
//      advances the substrate itself — do NOT call Anim::NewFrameUpdate() from
//      application code, or every animation runs at twice its speed.
//   2. In your UI code, pull the animated value each frame:
//          float a = VanGui::Anim::AnimBool("panel_open", show_panel,
//                                           { .Duration = 0.18f });
//          // use `a` (0..1) to drive alpha / scale / slide
//
// THE ANIMATION CLOCK
//   Everything that moves by itself (spinners, shimmer, pulsing, keyframe loops)
//   reads Anim::GetAnimTime()/Anim::Phase() rather than VanGui::GetTime(). The
//   clock is a double, so looping effects stay smooth after days of uptime; it
//   honours VanMotionConfig::TimeScale (slow motion for tuning); and reading a
//   phase marks the frame as animating, so idle-sleeping backends keep drawing
//   while anything that loops is on screen.
//
// THREADING / CONTEXT
//   State is associated with the active VanGui context. Switching the current
//   context (CreateContext/DestroyContext) transparently resets the pool. Using
//   two VanGui contexts *simultaneously* from interleaved code is not supported.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>   // VanGuiID, VanVec4, VanU32, VANGUI_API, VanGui::GetID
#include <math.h>            // fmod (clock fallbacks when the substrate is off)

namespace VanGui {
namespace Anim {

// ---------------------------------------------------------------------------
// Easing curves
// ---------------------------------------------------------------------------
// `t` is a normalized progress in [0,1]; the return value is the shaped
// progress. Some curves intentionally overshoot (Back*, Elastic*, and any
// CubicBezier whose control points leave [0,1]) — AnimColor()/AnimBool() clamp
// afterwards; AnimFloat()/AnimVec2()/AnimVec4() preserve the overshoot.
//
// The first twelve values are frozen (they are stored in .vss files and
// settings); new curves are appended.

enum VanEasing : int
{
    VanEasing_Linear = 0,
    VanEasing_QuadIn,    VanEasing_QuadOut,    VanEasing_QuadInOut,
    VanEasing_CubicIn,   VanEasing_CubicOut,   VanEasing_CubicInOut,
    VanEasing_ExpoOut,   VanEasing_CircOut,
    VanEasing_BackOut,   VanEasing_ElasticOut, VanEasing_BounceOut,
    // --- appended ---
    VanEasing_SineIn,    VanEasing_SineOut,    VanEasing_SineInOut,
    VanEasing_QuartIn,   VanEasing_QuartOut,   VanEasing_QuartInOut,
    VanEasing_QuintIn,   VanEasing_QuintOut,   VanEasing_QuintInOut,
    VanEasing_ExpoIn,    VanEasing_ExpoInOut,
    VanEasing_CircIn,    VanEasing_CircInOut,
    VanEasing_BackIn,    VanEasing_BackInOut,
    VanEasing_ElasticIn, VanEasing_ElasticInOut,
    VanEasing_BounceIn,  VanEasing_BounceInOut,
    VanEasing_CubicBezier,   // CSS cubic-bezier(x1, y1, x2, y2): VanAnimParams::Bezier
    VanEasing_Steps,         // CSS steps(n, jump-end): VanAnimParams::Steps
    VanEasing_COUNT
};

// How a tween that is interrupted mid-flight by a new target continues.
enum VanAnimRetarget : int
{
    VanAnimRetarget_Proportional = 0,  // new segment runs Duration scaled by how far it has to go (a half-finished fade reverses in half the time)
    VanAnimRetarget_Restart,           // new segment always runs the full Duration
};

// The space colours are blended in. sRGB is the historic per-channel mix;
// Linear avoids the dark band between complementary colours; OKLab keeps
// perceived lightness and hue steady, which is what theme cross-fades want.
enum VanColorSpace : int
{
    VanColorSpace_sRGB = 0,
    VanColorSpace_Linear,
    VanColorSpace_OKLab,
};

// How keyframe tracks and loop helpers behave past their end.
enum VanLoop : int
{
    VanLoop_None = 0,   // hold the last value
    VanLoop_Repeat,     // wrap to the start
    VanLoop_PingPong,   // play forward, then backward
};

// Polynomial curves are constexpr (compile-time validated via static_assert in
// the .cpp). Transcendental curves are evaluated at run time. `Ease` dispatches.
[[nodiscard]] constexpr float EaseLinear  (float t) noexcept { return t; }
[[nodiscard]] constexpr float EaseQuadIn  (float t) noexcept { return t * t; }
[[nodiscard]] constexpr float EaseQuadOut (float t) noexcept { return t * (2.0f - t); }
[[nodiscard]] constexpr float EaseCubicIn (float t) noexcept { return t * t * t; }
[[nodiscard]] constexpr float EaseCubicOut(float t) noexcept { const float u = t - 1.0f; return u * u * u + 1.0f; }
[[nodiscard]] constexpr float EaseQuartIn (float t) noexcept { return t * t * t * t; }
[[nodiscard]] constexpr float EaseQuartOut(float t) noexcept { const float u = 1.0f - t; return 1.0f - u * u * u * u; }
[[nodiscard]] constexpr float EaseQuintIn (float t) noexcept { return t * t * t * t * t; }
[[nodiscard]] constexpr float EaseQuintOut(float t) noexcept { const float u = 1.0f - t; return 1.0f - u * u * u * u * u; }

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

struct VanAnimParams
{
    float           Duration      = 0.20f;                        // seconds for a tween segment
    VanEasing       Easing        = VanEasing_CubicOut;           // shaping curve
    float           Delay         = 0.0f;                         // seconds to wait before moving (also on first appearance when AnimateAppear)
    bool            Unscaled      = false;                        // ignore VanMotionConfig Scale / TimeScale / ReduceMotion
    bool            AnimateAppear = false;                        // first call animates in from AppearFrom instead of starting at the target
    VanVec4         AppearFrom    = VanVec4(0.0f, 0.0f, 0.0f, 0.0f); // start value when AnimateAppear (.x for floats, .xy for Vec2). AnimColor with all zeros = the target colour at alpha 0
    VanAnimRetarget Retarget      = VanAnimRetarget_Proportional; // behaviour when the target changes mid-flight
    VanColorSpace   ColorSpace    = VanColorSpace_sRGB;           // AnimColor blending space
    float           Bezier[4]     = { 0.25f, 0.10f, 0.25f, 1.0f }; // VanEasing_CubicBezier control points (x1, y1, x2, y2); default = CSS "ease"
    int             Steps         = 4;                            // VanEasing_Steps count
};

struct VanSpringParams
{
    float   Stiffness     = 170.0f;   // higher = snappier
    float   Damping       = 26.0f;    // higher = less overshoot
    float   Mass          = 1.0f;     // heavier = slower, more momentum
    float   Eps           = 0.001f;   // settle threshold (value & velocity)
    bool    Unscaled      = false;    // ignore VanMotionConfig Scale / TimeScale / ReduceMotion
    bool    AnimateAppear = false;    // first call springs in from AppearFrom
    VanVec4 AppearFrom    = VanVec4(0.0f, 0.0f, 0.0f, 0.0f);
};

// Built-in widget motion and the global knobs every animation honours. The core
// widgets read this when VANGUI_ENABLE_ANIM is defined.
struct VanMotionConfig
{
    // --- global ---
    float Scale          = 1.0f;    // multiplies every duration, built-in and user (unless VanAnimParams::Unscaled). 0 = everything snaps
    float TimeScale      = 1.0f;    // speed of the animation clock: 0.25 = quarter-speed slow motion for tuning, 0 = frozen
    bool  ReduceMotion   = false;   // accessibility: movement snaps, decorative loops hold still, fades stay (shortened)

    // --- built-in durations (seconds) ---
    float HoverIn        = 0.08f;   // rest -> hovered
    float HoverOut       = 0.16f;   // back to rest
    float Press          = 0.05f;   // -> active/held
    float WindowAppear   = 0.12f;   // window, popup, menu and tooltip fade-in
    float WindowDisappear= 0.10f;   // window, popup, menu and tooltip fade-out after they stop being submitted (0 = vanish)
    float PopupSlide     = 4.0f;    // popups/menus/tooltips drift this many pixels into place (0 = fade only)
    float ScrollDuration = 0.12f;   // seconds for a wheel scroll to cover ~95% of its distance (0 = instant)
    float TreeReveal     = 0.14f;   // tree node / collapsing header content grows open and shrinks shut (0 = snap)
    float TabSlide       = 0.16f;   // selected-tab overline slides between tabs
    float CheckMark      = 0.12f;   // checkbox tick draws in, radio dot grows
    float DimFade        = 0.15f;   // modal dim fades out after the modal closes
    float NavGlide       = 0.10f;   // keyboard/gamepad focus outline glides to the newly focused item (0 = jump)
    float TabContent     = 0.16f;   // a newly selected tab's contents fade in ...
    float TabContentSlide= 10.0f;   // ... and slide this many pixels in from the side the tab sits on (0 = fade only)
    float PressScale     = 0.96f;   // buttons shrink to this while held (1 = off); snaps back over HoverOut
    bool  SmoothScrollTo = false;   // SetScrollX/Y, SetScrollHere*, SetScrollFromPos* ease like the wheel (the *Smooth calls always do)

    // --- touch scrolling (io.MouseSource == VanGuiMouseSource_TouchScreen) ---
    float TouchFriction  = 3.5f;    // how fast a flick's momentum dies away, per second (higher = stops sooner)
    float TouchBounce    = 0.30f;   // seconds for content pulled past an end to spring back
    float TouchOverscroll= 0.55f;   // rubber-band give past the ends (0 = hard stop, 1 = loose)

    // --- switches ---
    bool  Widgets        = true;
    bool  Windows        = true;
    bool  Scrolling      = true;
    bool  Trees          = true;
    bool  Tabs           = true;
    bool  Checks         = true;
    bool  Focus          = true;    // NavGlide
    bool  TabContents    = true;    // TabContent / TabContentSlide
    bool  TouchScroll    = true;    // drag-to-scroll with momentum and overscroll bounce for touch input
    bool  DragScrollMouse= false;   // the same for mouse drags that start on empty space (kiosks, testing)
    bool  FollowSystem   = false;   // keep ReduceMotion in step with the OS "show animations" setting (Windows)
};

// How much the effects module may spend: Reduced halves shadow rings and blur
// radius, Low draws blur as a tint, shadows as a single soft edge and holds shimmer.
enum VanQuality : int
{
    VanQuality_Full = 0,
    VanQuality_Reduced,
    VanQuality_Low,
};

enum VanPowerMode : int
{
    VanPowerMode_Auto = 0,   // low power on battery, and while the app is in the background (BackgroundIsLowPower)
    VanPowerMode_Normal,
    VanPowerMode_Low,
};

// Frame pacing, power and quality (GetPerfConfig()).
struct VanPerfConfig
{
    // Idle sleeping: WaitForEvents() / IdleWaitTimeout()
    float        MaxIdleWait          = 1.0f;    // longest sleep with nothing moving (hover, typing and held buttons wake it sooner)
    // Low power: whatever moves is drawn at LowPowerFps instead of the display rate
    VanPowerMode Power                = VanPowerMode_Auto;
    float        LowPowerFps          = 20.0f;
    bool         BackgroundIsLowPower = true;    // Auto mode: the app is in the background (VanGui::IsAppFocused() is false)
    // Automatic quality: frames slower than FrameBudget for about a second step
    // the quality down one level; comfortably faster for a few seconds, back up
    bool         AutoQuality          = true;
    VanQuality   Quality              = VanQuality_Full;   // the level when AutoQuality is off, the ceiling when it is on
    float        FrameBudget          = 1.0f / 50.0f;      // seconds
};

// Scoped transitions (BeginAppear / BeginTransition): what is drawn in between fades
// (through style Alpha, so child windows fade too) and moves (the current window's
// vertices are offset after the fact, so layout and hit-testing never shift).
struct VanTransitionParams
{
    float     Duration = 0.20f;
    VanEasing Easing   = VanEasing_CubicOut;
    VanVec2   Offset   = VanVec2(0.0f, 0.0f);   // 0,0 = the default for the call: Appear rises 8 px, Transition slides 16 px sideways
    bool      Fade     = true;
};

// A key on a keyframe track. `Easing` shapes the segment that STARTS at this key
// (as in CSS). Keys must be sorted by Time.
struct VanKeyframe
{
    float     Time   = 0.0f;
    float     Value  = 0.0f;
    VanEasing Easing = VanEasing_Linear;
};

// Result of AnimPresence(): `T` is the 0..1 entrance progress; `Visible` stays
// true while the element is entering, shown, or still fading out, so the caller
// keeps drawing it until the exit has finished.
struct VanPresence
{
    float T       = 0.0f;
    bool  Visible = false;
    explicit operator bool() const { return Visible; }
};

// ===========================================================================
//  REAL IMPLEMENTATION  (VANGUI_ENABLE_ANIM defined)
// ===========================================================================
#ifdef VANGUI_ENABLE_ANIM

// --- Easing ------------------------------------------------------------------
[[nodiscard]] VANGUI_API float Ease(VanEasing fn, float t) noexcept;
[[nodiscard]] VANGUI_API float Ease(const VanAnimParams& p, float t) noexcept;          // honours Bezier / Steps
[[nodiscard]] VANGUI_API float EaseBezier(float x1, float y1, float x2, float y2, float t) noexcept;
[[nodiscard]] VANGUI_API float EaseSteps(int steps, float t) noexcept;
[[nodiscard]] VANGUI_API const char* GetEasingName(VanEasing fn) noexcept;

// --- Animated values, keyed by ID ------------------------------------------
// Each returns the *current* value for this frame and advances internal state
// toward `target` using the animation clock's delta.

[[nodiscard]] VANGUI_API float   AnimFloat(VanGuiID id, float target,   const VanAnimParams& p = {});
[[nodiscard]] VANGUI_API VanVec2 AnimVec2 (VanGuiID id, VanVec2 target, const VanAnimParams& p = {});   // positions, sizes, offsets (unclamped)
[[nodiscard]] VANGUI_API VanVec4 AnimVec4 (VanGuiID id, VanVec4 target, const VanAnimParams& p = {});   // rects, anything 4-wide (unclamped)
[[nodiscard]] VANGUI_API VanVec4 AnimColor(VanGuiID id, VanVec4 target, const VanAnimParams& p = {});   // clamped 0..1, blended in p.ColorSpace

// Entrance/exit driver: returns 0..1 progress as `open` flips.
[[nodiscard]] VANGUI_API float   AnimBool (VanGuiID id, bool open,      const VanAnimParams& p = {});

// Presence: like AnimBool, but animates in on first appearance by default and
// reports whether the element still needs drawing (it does until its exit fade
// has finished). Pattern:
//     if (auto pr = Anim::AnimPresence(id, show)) { draw(pr.T); }
[[nodiscard]] VANGUI_API VanPresence AnimPresence(VanGuiID id, bool present, const VanAnimParams& p = {});

// Springs (framerate-independent: fixed 1/240 s sub-steps, stable for any
// realistic stiffness at any frame rate). No fixed duration; settles by physics.
[[nodiscard]] VANGUI_API float   SpringFloat(VanGuiID id, float target,   const VanSpringParams& p = {});
[[nodiscard]] VANGUI_API VanVec2 SpringVec2 (VanGuiID id, VanVec2 target, const VanSpringParams& p = {});
// Kick a spring: add `velocity` (units per second) to the spring on `id`. The
// next SpringFloat/SpringVec2 call on that id carries it. Good for flicks,
// bumps and "nudge" feedback. No-op if the id has no spring yet.
VANGUI_API void  SpringImpulse(VanGuiID id, float velocity);
VANGUI_API void  SpringImpulse(VanGuiID id, VanVec2 velocity);
[[nodiscard]] VANGUI_API float SpringVelocity(VanGuiID id);   // current scalar spring velocity (0 if none)

// --- Clocks, loops and keyframes -------------------------------------------
// Seconds on the animation clock (a double; honours TimeScale).
[[nodiscard]] VANGUI_API double GetAnimTime();
// Seconds the clock advanced this frame (0 when TimeScale is 0).
[[nodiscard]] VANGUI_API float  GetAnimDeltaTime();
// 0..1 position within a repeating cycle of `period` seconds (offset in cycles).
// Reading it marks the frame as animating. Under ReduceMotion, `decorative`
// loops hold at `offset`; non-decorative ones (spinners that signal "busy") run.
[[nodiscard]] VANGUI_API float  Phase(float period, float offset = 0.0f, bool decorative = false);
// Stateless loop helpers built on Phase(): a 0..1 wave shaped by `e`.
[[nodiscard]] VANGUI_API float  Loop(float period, VanLoop mode = VanLoop_PingPong, VanEasing e = VanEasing_SineInOut, bool decorative = true);

// Per-id playback clock: seconds accumulated while `playing` (pauses otherwise).
// `restart` zeroes it this frame.
[[nodiscard]] VANGUI_API float  AnimClock(VanGuiID id, bool playing = true, float rate = 1.0f, bool restart = false);
// Evaluate a keyframe track at `time` (stateless).
[[nodiscard]] VANGUI_API float  EvalKeyframes(const VanKeyframe* keys, int count, float time, VanLoop loop = VanLoop_None);
// Play a keyframe track on `id` (AnimClock + EvalKeyframes).
[[nodiscard]] VANGUI_API float  AnimKeyframes(VanGuiID id, const VanKeyframe* keys, int count, bool playing = true, VanLoop loop = VanLoop_None);
// True while `id`'s clock is still inside the track (for one-shot sequences).
[[nodiscard]] VANGUI_API bool   KeyframesPlaying(VanGuiID id, const VanKeyframe* keys, int count);

// --- Colour ------------------------------------------------------------------
[[nodiscard]] VANGUI_API VanVec4 MixColor(const VanVec4& a, const VanVec4& b, float t, VanColorSpace space = VanColorSpace_OKLab);
[[nodiscard]] VANGUI_API VanU32  MixColorU32(VanU32 a, VanU32 b, float t, VanColorSpace space = VanColorSpace_OKLab);
[[nodiscard]] VANGUI_API VanVec4 ColorToSpace(const VanVec4& srgb, VanColorSpace space);   // alpha passes through
[[nodiscard]] VANGUI_API VanVec4 ColorFromSpace(const VanVec4& c, VanColorSpace space);

// --- Widget motion -----------------------------------------------------------

// The live motion settings the core widgets read. Edit the returned reference.
[[nodiscard]] VANGUI_API VanMotionConfig& GetMotionConfig();
// Effective duration of a built-in or user segment after Scale / ReduceMotion.
// ScaledDuration is for movement (0 under ReduceMotion); ScaledFadeDuration is
// for opacity/colour changes, which ReduceMotion keeps at half length.
[[nodiscard]] VANGUI_API float ScaledDuration(float seconds, bool unscaled = false);
[[nodiscard]] VANGUI_API float ScaledFadeDuration(float seconds, bool unscaled = false);
// True when ReduceMotion is on (or Scale is 0): skip movement, keep fades.
[[nodiscard]] VANGUI_API bool  IsMotionReduced();

// Rest-aware colour easing for elements drawn every frame. Returns the colour
// to draw for `key` this frame, easing toward `target` over `duration` seconds.
// A pool slot is held only while the element is away from `rest` or settling
// back to it, so idle widgets cost a lookup and nothing else.
[[nodiscard]] VANGUI_API VanVec4 StateColor(VanGuiID key, const VanVec4& target, const VanVec4& rest, float duration);

// Rest-aware scalar easing (same slot discipline as StateColor): returns the
// value to use for `key`, easing toward `target`; frees the slot once it is
// back at `rest`. Used by the core for check marks, tree reveals and tabs.
[[nodiscard]] VANGUI_API float StateFloat(VanGuiID key, float target, float rest, float duration, VanEasing easing = VanEasing_CubicOut);

// Mark this frame as animating without a pooled tween (window fades, smooth
// scrolling, custom effects). IsAnimating() reports it so idle-sleeping
// backends keep drawing.
VANGUI_API void KeepAnimating();

// Reduced motion from the operating system. SystemPrefersReducedMotion() asks the
// OS now (Windows: the "Show animations" setting; false elsewhere). With
// VanMotionConfig::FollowSystem on, NewFrameUpdate() re-reads it about once a
// second and copies it into ReduceMotion. Platforms with their own setting
// (Android's animator scale, macOS) push it with SetSystemReducedMotion().
[[nodiscard]] VANGUI_API bool SystemPrefersReducedMotion();
VANGUI_API void SetSystemReducedMotion(bool reduced);

// --- Transitions -------------------------------------------------------------
// Appear: the content between Begin/End fades and rises into place the first time
// `id` is drawn (and again once it has gone unseen for about half a second).
VANGUI_API void BeginAppear(VanGuiID id, const VanTransitionParams& p = {});
VANGUI_API void EndAppear();
// Transition: when `key` changes (a page index, a selected tab, a view mode), the
// new content fades in and slides from the side it comes from: from the right
// when `key` grew, from the left when it shrank. The first key shows at once.
VANGUI_API void BeginTransition(VanGuiID id, int key, const VanTransitionParams& p = {});
VANGUI_API void EndTransition();
// Glide (layout animation): when the content between Begin/End lands somewhere else
// in the window's layout -- rows reordered, something inserted above it, a reflow --
// it glides from where it was instead of jumping. Scrolling is not a move. Rows keep
// their place in the layout, so clicks land where the row will be.
VANGUI_API void BeginGlide(VanGuiID id, float duration = 0.22f);
VANGUI_API void EndGlide();

// --- Frame pacing, power and quality ------------------------------------------
[[nodiscard]] VANGUI_API VanPerfConfig& GetPerfConfig();
// Seconds the app may sleep before it must draw again. `wants_redraw` is whether
// anything is moving (EnhanceWantsRedraw()); even then, low power returns the time
// left until the next LowPowerFps frame. Nothing moving: MaxIdleWait, shortened
// while an item is hovered (tooltip delays), text is being edited (caret blink)
// or a mouse button is held.
[[nodiscard]] VANGUI_API float IdleWaitTimeout(bool wants_redraw);
// Windows: sleep in the thread's message queue for up to IdleWaitTimeout(), waking
// at once on input. Returns true when input arrived. Elsewhere it returns false at
// once: pass IdleWaitTimeout() to your platform's wait (SDL_WaitEventTimeout,
// glfwWaitEventsTimeout, ALooper_pollOnce). The frame after a sleep is not counted
// by the quality governor.
VANGUI_API bool  WaitForEvents(bool wants_redraw);
[[nodiscard]] VANGUI_API bool IsLowPower();
[[nodiscard]] VANGUI_API bool SystemOnBattery();          // Windows; false elsewhere unless pushed
VANGUI_API void  SetSystemOnBattery(bool on_battery);      // for platforms that know (Android, macOS)
[[nodiscard]] VANGUI_API VanQuality GetQuality();
// Optional: the time a frame really took to produce (CPU and GPU), when io.DeltaTime
// isn't it (a capped or sleeping loop). Used for the next quality decision.
VANGUI_API void  ReportFrameTime(float seconds);

// --- Lifecycle / queries ----------------------------------------------------

// Advance the frame, evict stale state. VanGui::NewFrame() calls this; only
// call it yourself when driving the substrate without a VanGui frame (tests).
VANGUI_API void NewFrameUpdate();

// True while any animation touched this frame is still moving. Backends can use
// this to keep rendering continuously, and idle/sleep when it returns false.
[[nodiscard]] VANGUI_API bool IsAnimating();

// Forget the state for one id (e.g. to hard-cut instead of animate).
VANGUI_API void Reset(VanGuiID id);

// Free all animation state for the active context (also called on context swap).
VANGUI_API void Shutdown();

// Frames an entry may go untouched before it is evicted (default 60).
VANGUI_API void SetEvictionFrames(int frames);

// Testing aid: force a fixed DeltaTime for deterministic stepping. Pass a
// negative value to resume using VanGui::GetIO().DeltaTime (the default).
VANGUI_API void SetDeltaTimeOverrideForTesting(float dt);

// Introspection for metrics/debug overlays.
VANGUI_API int  PoolActiveCount();   // tweens touched/alive
VANGUI_API int  PoolCapacity();      // slots allocated in the pool

// ===========================================================================
//  ZERO-COST SHIMS  (VANGUI_ENABLE_ANIM not defined)
// ===========================================================================
#else

[[nodiscard]] inline float Ease(VanEasing, float t) noexcept { return t; }
[[nodiscard]] inline float Ease(const VanAnimParams&, float t) noexcept { return t; }
[[nodiscard]] inline float EaseBezier(float, float, float, float, float t) noexcept { return t; }
[[nodiscard]] inline float EaseSteps(int, float t) noexcept { return t; }
[[nodiscard]] inline const char* GetEasingName(VanEasing) noexcept { return "Linear"; }

[[nodiscard]] inline float   AnimFloat(VanGuiID, float target,   const VanAnimParams& = {}) { return target; }
[[nodiscard]] inline VanVec2 AnimVec2 (VanGuiID, VanVec2 target, const VanAnimParams& = {}) { return target; }
[[nodiscard]] inline VanVec4 AnimVec4 (VanGuiID, VanVec4 target, const VanAnimParams& = {}) { return target; }
[[nodiscard]] inline VanVec4 AnimColor(VanGuiID, VanVec4 target, const VanAnimParams& = {}) { return target; }
[[nodiscard]] inline float   AnimBool (VanGuiID, bool open,      const VanAnimParams& = {}) { return open ? 1.0f : 0.0f; }
[[nodiscard]] inline VanPresence AnimPresence(VanGuiID, bool present, const VanAnimParams& = {}) { VanPresence r; r.T = present ? 1.0f : 0.0f; r.Visible = present; return r; }
[[nodiscard]] inline float   SpringFloat(VanGuiID, float target,   const VanSpringParams& = {}) { return target; }
[[nodiscard]] inline VanVec2 SpringVec2 (VanGuiID, VanVec2 target, const VanSpringParams& = {}) { return target; }
inline void  SpringImpulse(VanGuiID, float) {}
inline void  SpringImpulse(VanGuiID, VanVec2) {}
[[nodiscard]] inline float SpringVelocity(VanGuiID) { return 0.0f; }

[[nodiscard]] inline double GetAnimTime() { return VanGui::GetTime(); }
[[nodiscard]] inline float  GetAnimDeltaTime() { return VanGui::GetIO().DeltaTime; }
[[nodiscard]] inline float  Phase(float period, float offset = 0.0f, bool = false)
{
    if (period <= 0.0f) return 0.0f;
    const double c = VanGui::GetTime() / (double)period + (double)offset;
    return (float)(c - floor(c));
}
[[nodiscard]] inline float  Loop(float period, VanLoop mode = VanLoop_PingPong, VanEasing = VanEasing_SineInOut, bool = true)
{
    const float p = Phase(period);
    return mode == VanLoop_PingPong ? (p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f) : p;
}
[[nodiscard]] inline float  AnimClock(VanGuiID, bool = true, float = 1.0f, bool = false) { return 0.0f; }
[[nodiscard]] inline float  EvalKeyframes(const VanKeyframe* keys, int count, float, VanLoop = VanLoop_None) { return count > 0 ? keys[count - 1].Value : 0.0f; }
[[nodiscard]] inline float  AnimKeyframes(VanGuiID, const VanKeyframe* keys, int count, bool = true, VanLoop = VanLoop_None) { return count > 0 ? keys[count - 1].Value : 0.0f; }
[[nodiscard]] inline bool   KeyframesPlaying(VanGuiID, const VanKeyframe*, int) { return false; }

[[nodiscard]] inline VanVec4 MixColor(const VanVec4& a, const VanVec4& b, float t, VanColorSpace = VanColorSpace_OKLab)
{
    return VanVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
[[nodiscard]] inline VanU32  MixColorU32(VanU32 a, VanU32 b, float t, VanColorSpace = VanColorSpace_OKLab)
{
    return VanGui::ColorConvertFloat4ToU32(MixColor(VanGui::ColorConvertU32ToFloat4(a), VanGui::ColorConvertU32ToFloat4(b), t));
}
[[nodiscard]] inline VanVec4 ColorToSpace(const VanVec4& c, VanColorSpace) { return c; }
[[nodiscard]] inline VanVec4 ColorFromSpace(const VanVec4& c, VanColorSpace) { return c; }

[[nodiscard]] inline VanMotionConfig& GetMotionConfig() { static VanMotionConfig cfg; return cfg; }
[[nodiscard]] inline float ScaledDuration(float, bool = false) { return 0.0f; }
[[nodiscard]] inline float ScaledFadeDuration(float, bool = false) { return 0.0f; }
[[nodiscard]] inline bool  IsMotionReduced() { return true; }
[[nodiscard]] inline VanVec4 StateColor(VanGuiID, const VanVec4& target, const VanVec4&, float) { return target; }
[[nodiscard]] inline float StateFloat(VanGuiID, float target, float, float, VanEasing = VanEasing_CubicOut) { return target; }
inline void KeepAnimating() {}
[[nodiscard]] inline bool SystemPrefersReducedMotion() { return false; }
inline void SetSystemReducedMotion(bool) {}
inline void BeginAppear(VanGuiID, const VanTransitionParams& = {}) {}
inline void EndAppear() {}
inline void BeginTransition(VanGuiID, int, const VanTransitionParams& = {}) {}
inline void EndTransition() {}
inline void BeginGlide(VanGuiID, float = 0.22f) {}
inline void EndGlide() {}
[[nodiscard]] inline VanPerfConfig& GetPerfConfig() { static VanPerfConfig cfg; return cfg; }
[[nodiscard]] inline float IdleWaitTimeout(bool wants_redraw) { return wants_redraw ? 0.0f : GetPerfConfig().MaxIdleWait; }
inline bool  WaitForEvents(bool) { return false; }
[[nodiscard]] inline bool IsLowPower() { return GetPerfConfig().Power == VanPowerMode_Low; }
[[nodiscard]] inline bool SystemOnBattery() { return false; }
inline void  SetSystemOnBattery(bool) {}
[[nodiscard]] inline VanQuality GetQuality() { return GetPerfConfig().AutoQuality ? VanQuality_Full : GetPerfConfig().Quality; }
inline void  ReportFrameTime(float) {}

inline void NewFrameUpdate() {}
[[nodiscard]] inline bool IsAnimating() { return false; }
inline void Reset(VanGuiID) {}
inline void Shutdown() {}
inline void SetEvictionFrames(int) {}
inline void SetDeltaTimeOverrideForTesting(float) {}
inline int  PoolActiveCount() { return 0; }
inline int  PoolCapacity()    { return 0; }

#endif // VANGUI_ENABLE_ANIM

// Delay for the `index`-th item of a staggered entrance: index*step, capped so a
// long list never waits longer than `max_total` for its last row.
[[nodiscard]] inline float StaggerDelay(int index, float step = 0.03f, float max_total = 0.35f)
{
    const float d = (float)index * step;
    return d < max_total ? d : max_total;
}

// --- Convenience string-id overloads (hash via the active ID stack) ---------
[[nodiscard]] inline float   AnimFloat(const char* id, float target,   const VanAnimParams& p = {}) { return AnimFloat(VanGui::GetID(id), target, p); }
[[nodiscard]] inline VanVec2 AnimVec2 (const char* id, VanVec2 target, const VanAnimParams& p = {}) { return AnimVec2 (VanGui::GetID(id), target, p); }
[[nodiscard]] inline VanVec4 AnimVec4 (const char* id, VanVec4 target, const VanAnimParams& p = {}) { return AnimVec4 (VanGui::GetID(id), target, p); }
[[nodiscard]] inline VanVec4 AnimColor(const char* id, VanVec4 target, const VanAnimParams& p = {}) { return AnimColor(VanGui::GetID(id), target, p); }
[[nodiscard]] inline float   AnimBool (const char* id, bool open,      const VanAnimParams& p = {}) { return AnimBool (VanGui::GetID(id), open,  p); }
[[nodiscard]] inline VanPresence AnimPresence(const char* id, bool present, const VanAnimParams& p = {}) { return AnimPresence(VanGui::GetID(id), present, p); }
[[nodiscard]] inline float   SpringFloat(const char* id, float target,   const VanSpringParams& p = {}) { return SpringFloat(VanGui::GetID(id), target, p); }
[[nodiscard]] inline VanVec2 SpringVec2 (const char* id, VanVec2 target, const VanSpringParams& p = {}) { return SpringVec2 (VanGui::GetID(id), target, p); }
[[nodiscard]] inline float   AnimClock(const char* id, bool playing = true, float rate = 1.0f, bool restart = false) { return AnimClock(VanGui::GetID(id), playing, rate, restart); }
[[nodiscard]] inline float   AnimKeyframes(const char* id, const VanKeyframe* keys, int count, bool playing = true, VanLoop loop = VanLoop_None) { return AnimKeyframes(VanGui::GetID(id), keys, count, playing, loop); }
inline void BeginAppear(const char* id, const VanTransitionParams& p = {})              { BeginAppear(VanGui::GetID(id), p); }
inline void BeginTransition(const char* id, int key, const VanTransitionParams& p = {}) { BeginTransition(VanGui::GetID(id), key, p); }
inline void BeginGlide(const char* id, float duration = 0.22f)                          { BeginGlide(VanGui::GetID(id), duration); }

// ---------------------------------------------------------------------------
// VanExitTracker — keep drawing items after the caller stops submitting them.
// ---------------------------------------------------------------------------
// Immediate mode forgets an element the frame it stops being drawn, so a list
// row that is deleted cannot fade out on its own. The tracker remembers the
// last value of every keyed item it saw; items that go missing are handed back
// through ForEachExiting() with a 1..0 fade until their exit has played.
//
//     static Anim::VanExitTracker<Row, 32> exits;
//     exits.Begin();
//     for (Row& r : rows) { exits.Present(r.id, r); DrawRow(r, 1.0f); }
//     exits.ForEachExiting([](const Row& r, float t) { DrawRow(r, t); });
//
// Fixed capacity, no heap. When full, the oldest exiting item is dropped.
template <typename T, int Capacity = 32>
class VanExitTracker
{
public:
    float Duration = 0.18f;

    void Begin() { m_frame++; }

    void Present(VanGuiID key, const T& value)
    {
        Slot* s = Find(key);
        if (!s)
            s = Alloc(key);
        s->Value = value;
        s->Seen = m_frame;
        s->Exiting = false;
        s->Fade = 1.0f;
    }

    template <typename Fn>
    void ForEachExiting(Fn&& fn)
    {
        const float dt = GetAnimDeltaTime();
        const float dur = ScaledFadeDuration(Duration);
        for (int i = 0; i < m_count; )
        {
            Slot& s = m_slots[i];
            if (s.Seen != m_frame)
            {
                s.Exiting = true;
                s.Fade = (dur > 0.0f) ? s.Fade - dt / dur : 0.0f;
                if (s.Fade <= 0.0f) { m_slots[i] = m_slots[--m_count]; continue; }
                KeepAnimating();
                fn(static_cast<const T&>(s.Value), s.Fade);
            }
            ++i;
        }
    }

    int ExitingCount() const { int n = 0; for (int i = 0; i < m_count; ++i) n += m_slots[i].Seen != m_frame ? 1 : 0; return n; }
    void Clear() { m_count = 0; }

private:
    struct Slot { VanGuiID Key = 0; T Value{}; unsigned Seen = 0; float Fade = 1.0f; bool Exiting = false; };
    Slot     m_slots[Capacity];
    int      m_count = 0;
    unsigned m_frame = 0;

    Slot* Find(VanGuiID key) { for (int i = 0; i < m_count; ++i) if (m_slots[i].Key == key) return &m_slots[i]; return nullptr; }
    Slot* Alloc(VanGuiID key)
    {
        if (m_count == Capacity)
        {
            // Drop the exiting item closest to done; if none exit, the oldest slot.
            int victim = 0; float best = 2.0f;
            for (int i = 0; i < m_count; ++i) if (m_slots[i].Exiting && m_slots[i].Fade < best) { best = m_slots[i].Fade; victim = i; }
            m_slots[victim] = m_slots[--m_count];
        }
        Slot& s = m_slots[m_count++];
        s = Slot();
        s.Key = key;
        return &s;
    }
};

} // namespace Anim
} // namespace VanGui
