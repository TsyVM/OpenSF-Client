// vangui_gallery.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — the gallery.
//
// Every loading wheel, the motion presets, the draw-list effects, the vector
// strokes, the icon set, the feedback widgets and the gizmos on one page, live, next to the
// knobs that drive them (motion scale and slow motion, icon size, weight and
// style, blur radius ...). It is where a change to the suite is looked at, and
// tests/enhance/test_visual photographs every section of it.
//
// Sections draw into the current window, so an app can put any of them in its
// own debug UI; ShowEnhanceGallery() puts them all in one window with tabs.
// Sections whose module is not compiled in are left out.
//
// Toasts and snackbars fired from the Feedback section are drawn by the
// gallery window itself (both renderers run once a frame, so an app that also
// calls them loses nothing).
//
// Opt-in / zero-cost via VANGUI_ENABLE_GALLERY.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

enum VanGallerySection
{
    VanGallerySection_Loading = 0,   // spinners, progress, status, skeletons, overlay (vangui_loading)
    VanGallerySection_Motion,        // presence, springs, stagger, easing curves, keyframes (vangui_anim)
    VanGallerySection_Effects,       // shadows, gradients, opacity, transforms, clips, shimmer, blur (vangui_effects)
    VanGallerySection_Vector,        // caps and joins, fillets, dashes, draw-on, glow (vangui_vector)
    VanGallerySection_Icons,         // the set, styles, morphs, motion, glyphs (vangui_icons)
    VanGallerySection_Feedback,      // toasts, snackbars, banners, toggles, ripples (notify / banners / widgets_ext / feedback)
    VanGallerySection_Everyday,      // small widgets, rolling numbers, transitions, live reorder, cached regions (vangui_extras)
    VanGallerySection_Touch,         // pull to refresh, swipe rows, long press, pan/zoom canvas (vangui_touch)
    VanGallerySection_Gizmo,         // move/rotate/scale handles, view cube, toolbar, 2D on a canvas (vangui_gizmo)
    VanGallerySection_COUNT
};

#ifdef VANGUI_ENABLE_GALLERY

VANGUI_API void        ShowEnhanceGallery(bool* p_open = nullptr);
// One section into the current window. Returns false if its module is not compiled in.
VANGUI_API bool        ShowGallerySection(VanGallerySection section);
VANGUI_API const char* GetGallerySectionName(VanGallerySection section);
VANGUI_API bool        IsGallerySectionAvailable(VanGallerySection section);

#else // ----------------------------- shims -----------------------------------

inline void        ShowEnhanceGallery(bool* = nullptr) {}
inline bool        ShowGallerySection(VanGallerySection) { return false; }
inline const char* GetGallerySectionName(VanGallerySection) { return ""; }
inline bool        IsGallerySectionAvailable(VanGallerySection) { return false; }

#endif // VANGUI_ENABLE_GALLERY

} // namespace VanGui
