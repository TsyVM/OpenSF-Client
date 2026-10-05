// vangui_enhance.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — umbrella include + convenience drivers.
//
// Include this one header to pull in whichever enhancement modules you compiled,
// and call two functions per frame instead of hand-calling every Render*:
//
//     VanGui::NewFrame();
//     VanGui::NewFrameExtras();     // advance per-frame extras (after NewFrame)
//     ... your UI ...
//     VanGui::RenderExtras();       // draw overlays (toasts, palette, ...)
//     VanGui::Render();
//
// Each call is fully guarded: with no extras enabled, both are empty and cost
// nothing. A module participates when its enable macro is defined — the CMake
// targets define these (e.g. VANGUI_ENABLE_NOTIFY) so simply linking a module
// opts it into the drivers.
//
// IDLE / POWER INTEGRATION
//   EnhanceWantsRedraw() returns true while any animation is still moving. An
//   idle-aware backend can sleep when it returns false and resume continuous
//   rendering when it returns true — so an animated UI only burns frames while
//   something is actually moving (Roadmap §5.5 idle integration).
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#ifdef VANGUI_ENABLE_ANIM
#include "vangui_anim.h"
#endif
#ifdef VANGUI_ENABLE_NOTIFY
#include "vangui_notify.h"
#endif
#ifdef VANGUI_ENABLE_THEME_ENGINE
#include "vangui_theme_engine.h"
#endif
#ifdef VANGUI_ENABLE_STYLESHEET
#include "vangui_style_sheet.h"
#endif
#ifdef VANGUI_ENABLE_COMMAND_PALETTE
#include "vangui_command_palette.h"
#endif
#ifdef VANGUI_ENABLE_LOADING
#include "vangui_loading.h"
#endif
#ifdef VANGUI_ENABLE_DIALOGS
#include "vangui_dialogs.h"
#endif
#ifdef VANGUI_ENABLE_LAYOUT
#include "vangui_layout.h"
#endif
#ifdef VANGUI_ENABLE_VIEWS
#include "vangui_views.h"
#endif
#ifdef VANGUI_ENABLE_SIGNALS
#include "vangui_signals.h"
#endif
#ifdef VANGUI_ENABLE_THREAD
#include "vangui_thread.h"
#endif
#ifdef VANGUI_ENABLE_VECTOR
#include "vangui_vector.h"
#endif
#ifdef VANGUI_ENABLE_EFFECTS
#include "vangui_effects.h"
#endif
#ifdef VANGUI_ENABLE_ICONS
#include "vangui_icons.h"
#endif
#ifdef VANGUI_ENABLE_GALLERY
#include "vangui_gallery.h"
#endif
#ifdef VANGUI_ENABLE_EXTRAS
#include "vangui_extras.h"
#endif
#ifdef VANGUI_ENABLE_TOUCH
#include "vangui_touch.h"
#endif
#include "vangui_format.h"   // header-only

namespace VanGui {

// Per-frame UPDATE for enabled extras. Call once, right after NewFrame(). These
// are the steps that must run before user widgets (they mutate style).
inline void NewFrameExtras()
{
#ifdef VANGUI_ENABLE_THREAD
    DrainMainThreadQueue();         // dispatch worker results to main thread first
#endif
#ifdef VANGUI_ENABLE_THEME_ENGINE
    RenderThemeTransition();        // apply this frame's interpolated theme
#endif
#ifdef VANGUI_ENABLE_STYLESHEET
    PollStyleSheetChanges();        // hot-reload .vss if it changed
#endif
}

// Per-frame DRAW for enabled overlays. Call once, after your windows, before
// Render().
inline void RenderExtras()
{
#ifdef VANGUI_ENABLE_NOTIFY
    RenderNotifications();
#endif
#ifdef VANGUI_ENABLE_COMMAND_PALETTE
    RenderCommandPalette();
#endif
}

// Tiny metrics window: animation pool occupancy + idle state. For debugging /
// tuning the enhancement layer. No-op body when ANIM is off.
inline void ShowEnhanceMetrics(bool* p_open = nullptr)
{
#if defined(VANGUI_ENABLE_ANIM) || defined(VANGUI_ENABLE_THREAD) || defined(VANGUI_ENABLE_ICONS)
    if (Begin("VanGUI Enhance Metrics", p_open))
    {
#ifdef VANGUI_ENABLE_ANIM
        Text("Active tweens : %d", Anim::PoolActiveCount());
        Text("Pool capacity : %d", Anim::PoolCapacity());
        Text("Animating     : %s", Anim::IsAnimating() ? "yes" : "no");
        Separator();
#endif
#ifdef VANGUI_ENABLE_ICONS
        Text("Icon meshes   : %d cached", GetIconCacheSize());
        Separator();
#endif
#ifdef VANGUI_ENABLE_THREAD
        Text("Thread pool   : %s (%d threads)",
             IsThreadPoolRunning() ? "running" : "stopped", ThreadPoolSize());
        Text("Worker tasks  : %d pending", PendingWorkerTasks());
        Text("Main callbacks: %d pending", PendingMainThreadCallbacks());
        Separator();
#endif
        TextDisabled("Backend may sleep when no animation or work is pending.");
    }
    End();
#else
    (void)p_open;
#endif
}

// True while the UI needs another frame: animation in progress OR main-thread
// callbacks are pending (worker results waiting to be delivered).
[[nodiscard]] inline bool EnhanceWantsRedraw()
{
#ifdef VANGUI_ENABLE_ANIM
    if (Anim::IsAnimating()) return true;
#endif
#ifdef VANGUI_ENABLE_THREAD
    if (PendingMainThreadCallbacks() > 0) return true;
#endif
    return false;
}

} // namespace VanGui
