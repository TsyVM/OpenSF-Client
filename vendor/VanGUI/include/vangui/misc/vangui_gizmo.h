// vangui_gizmo.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — transform gizmos: move, rotate and scale handles
// drawn over a 3D view, a 2D version for canvases, a view cube, and a toolbar.
//
//     if (VanGui::Gizmo("move", view, proj, object, VanGizmoOp_Translate))
//         ApplyToSelection(object);
//
// Everything is drawn into a VanGUI draw list, so it runs on every renderer with
// no graphics code of its own. You hand over the camera's view and projection
// matrices and the object's matrix; the gizmo edits the object's matrix.
//
// THE HANDLES
//   Translate   an arrow per axis, a square per plane, a centre dot that moves
//               in the screen plane
//   Rotate      a ring per axis, a view-facing ring, and a free-rotate sphere
//               (grab inside the rings) — a pie and the angle show while dragging
//   Scale       a box per axis, and a uniform handle at the centre
//   Bounds      the object's bounding box with corner and edge handles; the
//               opposite corner or edge stays where it is
//   Combine any of them (VanGizmoOp_Universal is move + rotate + scale).
//
//   World or local space. Snapping lands the RESULT on the grid (the position,
//   the axis length), not the distance dragged; hold Ctrl to flip snapping for
//   one drag. Escape during a drag puts the matrix back. Axes seen end-on fade
//   out, the back of each ring is drawn dimmed, and an arrow pointing away from
//   the camera flips to point at it.
//
// MATRICES
//   16 floats, translation in elements 12, 13, 14: the layout glm::mat4
//   (value_ptr) and DirectXMath's XMFLOAT4X4 both use, so either passes straight
//   in. Right- or left-handed, OpenGL (-1..1) or Direct3D (0..1) depth: the gizmo
//   reads which from the projection. (A reversed-Z orthographic projection is the
//   one it reads the wrong way round: the dimmed half of each ring swaps.)
//
// INPUT
//   A handle under the pointer takes the mouse from whatever the window drew
//   there first (the 3D view's image or button), so dragging a handle never also
//   orbits the camera. Your click-to-select should still skip the gizmo:
//       if (IsItemClicked() && !IsGizmoOver()) PickObject();
//   Touch input gets bigger handles and hit areas.
//
// UNDO
//   IsGizmoDragStarted() / IsGizmoDragEnded() bracket each drag, and
//   GizmoPushUndo() records one step per drag on a VanCommandStack
//   (vangui_undo.h) or anything with the same perform(do, undo).
//
// LIMITS
//   Drawn as an overlay: scene geometry never hides a handle (the back halves of
//   the rings are dimmed instead, which needs no depth buffer).
//
// A pure consumer of the public VanGui API (plus the core's OverlayHitArea).
// Opt-in / zero-cost via VANGUI_ENABLE_GIZMO; needs the vector module.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

struct VanCanvasView;   // vangui_touch.h: the pan/zoom canvas Gizmo2D can draw on

// Which handles to show. Combine freely.
typedef int VanGizmoOp;
enum VanGizmoOp_
{
    VanGizmoOp_None            = 0,
    VanGizmoOp_TranslateX      = 1 << 0,
    VanGizmoOp_TranslateY      = 1 << 1,
    VanGizmoOp_TranslateZ      = 1 << 2,
    VanGizmoOp_TranslateYZ     = 1 << 3,    // the plane squares, named by the two axes they move along
    VanGizmoOp_TranslateZX     = 1 << 4,
    VanGizmoOp_TranslateXY     = 1 << 5,
    VanGizmoOp_TranslateScreen = 1 << 6,    // centre dot: moves in the plane facing the camera
    VanGizmoOp_RotateX         = 1 << 7,
    VanGizmoOp_RotateY         = 1 << 8,
    VanGizmoOp_RotateZ         = 1 << 9,
    VanGizmoOp_RotateScreen    = 1 << 10,   // outer ring: turns about the view direction
    VanGizmoOp_RotateFree      = 1 << 11,   // inside the rings: a trackball
    VanGizmoOp_ScaleX          = 1 << 12,
    VanGizmoOp_ScaleY          = 1 << 13,
    VanGizmoOp_ScaleZ          = 1 << 14,
    VanGizmoOp_ScaleUniform    = 1 << 15,
    VanGizmoOp_Bounds          = 1 << 16,   // box handles; set VanGizmoOptions::Bounds

    VanGizmoOp_TranslateAxes   = VanGizmoOp_TranslateX | VanGizmoOp_TranslateY | VanGizmoOp_TranslateZ,
    VanGizmoOp_TranslatePlanes = VanGizmoOp_TranslateYZ | VanGizmoOp_TranslateZX | VanGizmoOp_TranslateXY,
    VanGizmoOp_Translate       = VanGizmoOp_TranslateAxes | VanGizmoOp_TranslatePlanes | VanGizmoOp_TranslateScreen,
    VanGizmoOp_RotateAxes      = VanGizmoOp_RotateX | VanGizmoOp_RotateY | VanGizmoOp_RotateZ,
    VanGizmoOp_Rotate          = VanGizmoOp_RotateAxes | VanGizmoOp_RotateScreen | VanGizmoOp_RotateFree,
    VanGizmoOp_ScaleAxes       = VanGizmoOp_ScaleX | VanGizmoOp_ScaleY | VanGizmoOp_ScaleZ,
    VanGizmoOp_Scale           = VanGizmoOp_ScaleAxes | VanGizmoOp_ScaleUniform,
    VanGizmoOp_Universal       = VanGizmoOp_Translate | VanGizmoOp_Rotate | VanGizmoOp_Scale,
};

// One handle: what IsGizmoOver() found under the pointer, or what is being dragged.
// Each translate/rotate/scale part is the op bit of the same name.
enum VanGizmoPart
{
    VanGizmoPart_None = 0,
    VanGizmoPart_TranslateX, VanGizmoPart_TranslateY, VanGizmoPart_TranslateZ,
    VanGizmoPart_TranslateYZ, VanGizmoPart_TranslateZX, VanGizmoPart_TranslateXY,
    VanGizmoPart_TranslateScreen,
    VanGizmoPart_RotateX, VanGizmoPart_RotateY, VanGizmoPart_RotateZ,
    VanGizmoPart_RotateScreen, VanGizmoPart_RotateFree,
    VanGizmoPart_ScaleX, VanGizmoPart_ScaleY, VanGizmoPart_ScaleZ, VanGizmoPart_ScaleUniform,
    VanGizmoPart_BoundsCorner,   // a corner of the box: scales along both of the face's axes
    VanGizmoPart_BoundsEdge,     // the middle of an edge: scales along one axis
    VanGizmoPart_COUNT
};

enum VanGizmoSpace
{
    VanGizmoSpace_World = 0,     // handles line up with the world axes
    VanGizmoSpace_Local,         // handles line up with the object's own axes (scaling always does)
};

typedef int VanGizmoFlags;
enum VanGizmoFlags_
{
    VanGizmoFlags_None         = 0,
    VanGizmoFlags_NoAxisFlip   = 1 << 0,   // arrows keep pointing along +axis even when it faces away
    VanGizmoFlags_NoReadout    = 1 << 1,   // no value label beside the pointer while dragging
    VanGizmoFlags_SnapRelative = 1 << 2,   // snap the change (distance, factor) instead of the result
    VanGizmoFlags_NoSnapToggle = 1 << 3,   // holding Ctrl doesn't flip snapping
    VanGizmoFlags_HideBack     = 1 << 4,   // hide the back halves of the rings rather than dim them
    VanGizmoFlags_Background   = 1 << 5,   // the scene is drawn behind every window: draw into the background
                                           // list and take the mouse only where no window is
};

// Per-call settings. Defaults: world space, no snapping, the current window's
// rectangle as the view.
struct VanGizmoOptions
{
    VanGizmoSpace Space = VanGizmoSpace_World;
    VanGizmoFlags Flags = VanGizmoFlags_None;
    bool          Snap = false;                          // Ctrl held flips this for the drag
    float         SnapTranslate[3] = { 1.0f, 1.0f, 1.0f }; // grid step per axis
    float         SnapRotate = 15.0f;                    // degrees
    float         SnapScale = 0.1f;                      // axis length (the matrix row's length) steps
    float         SnapBounds[3] = { 1.0f, 1.0f, 1.0f };  // box size steps, in world units
    float         Bounds[6] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };   // local box for VanGizmoOp_Bounds: min xyz, max xyz
    VanVec2       ViewportMin = VanVec2(0.0f, 0.0f);     // where the scene is on screen; min == max = the
    VanVec2       ViewportMax = VanVec2(0.0f, 0.0f);     //   current window (or the display with _Background)
    float         Size = 0.0f;                           // handle length in pixels (0 = style)
    VanDrawList*  DrawList = nullptr;                    // nullptr = the window's (background list with _Background)

    void SetViewport(const VanVec2& min, const VanVec2& max) { ViewportMin = min; ViewportMax = max; }
    void SetBounds(const float min[3], const float max[3]) { for (int i = 0; i < 3; ++i) { Bounds[i] = min[i]; Bounds[3 + i] = max[i]; } }
};

// Colours and sizes, shared by every gizmo. GetGizmoStyle() to change them.
struct VanGizmoStyle
{
    VanU32 AxisColor[3]   = { VAN_COL32(232, 64, 84, 255), VAN_COL32(128, 204, 48, 255), VAN_COL32(52, 132, 244, 255) };
    VanU32 ScreenColor    = 0;                               // centre dot, view ring, trackball (0 = the text colour)
    VanU32 ActiveColor    = VAN_COL32(255, 204, 48, 255);    // the handle being dragged (hover eases toward it)
    VanU32 BoundsColor    = 0;                               // the bounds box (0 = the text colour)
    VanU32 ReadoutText    = VAN_COL32(255, 255, 255, 255);
    VanU32 ReadoutBg      = VAN_COL32(16, 18, 24, 216);
    float  Size           = 0.0f;    // handle length in pixels; 0 = 6 font heights
    float  Thickness      = 2.5f;    // lines and rings
    float  HoverThickness = 1.5f;    // added to the hovered handle
    float  ArrowSize      = 11.0f;   // arrow head length in pixels (width 0.8 of it)
    float  BoxSize        = 9.0f;    // scale handle box
    float  DotRadius      = 6.5f;    // centre dot
    float  HitRadius      = 7.0f;    // how far from a line or ring still grabs it
    float  TouchScale     = 1.3f;    // handles grow by this for touch input ...
    float  TouchHitScale  = 2.2f;    // ... and their hit areas by this
    float  PlaneAlpha     = 0.35f;   // plane squares' fill
    float  HiddenAlpha    = 0.28f;   // back halves of rings
    int    RingSegments   = 72;
};

// A matrix as a value, for capturing in lambdas and storing in undo steps.
struct VanGizmoMatrix
{
    float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
};

// A 2D transform for sprites, UV islands and shapes on a canvas.
struct VanGizmoTransform2D
{
    VanVec2 Position = VanVec2(0.0f, 0.0f);
    float   Rotation = 0.0f;                  // radians, clockwise from +X (y down), like the draw list's arcs
    VanVec2 Scale    = VanVec2(1.0f, 1.0f);
};

typedef int VanViewCubeFlags;
enum VanViewCubeFlags_
{
    VanViewCubeFlags_None   = 0,
    VanViewCubeFlags_Axes   = 1 << 0,   // an axis compass (a ball per axis end) instead of a cube
    VanViewCubeFlags_ZUp    = 1 << 1,   // Z is up (Y is up otherwise): which faces read TOP and FRONT
    VanViewCubeFlags_NoDrag = 1 << 2,   // dragging the cube doesn't orbit
};

#ifdef VANGUI_ENABLE_GIZMO

// ── The gizmo ────────────────────────────────────────────────────────────────

// Draws the handles for `ops` at `matrix` (the object's) and edits it while one
// is dragged. Returns true on every frame the matrix changed, including the
// frame Escape put it back.
VANGUI_API bool Gizmo(const char* str_id, const float* view, const float* projection, float* matrix,
                      VanGizmoOp ops = VanGizmoOp_Translate, const VanGizmoOptions& options = VanGizmoOptions());

// The same handles for a flat transform on a canvas (the touch module's
// VanCanvasView; nullptr = canvas units are screen pixels). Uses the X, Y and
// XY translate handles, the Z (or screen) ring, the X/Y/uniform scale handles
// and the bounds box (z ignored).
VANGUI_API bool Gizmo2D(const char* str_id, VanGizmoTransform2D* transform, VanGizmoOp ops = VanGizmoOp_Translate,
                        const VanCanvasView* canvas = nullptr, const VanGizmoOptions& options = VanGizmoOptions());

// State. "Over" and "using" cover every gizmo: check them before your view acts
// on a click or a key. The rest describe the last Gizmo() / Gizmo2D() call.
VANGUI_API bool         IsGizmoOver();            // the pointer is on a handle (this frame or the last)
VANGUI_API bool         IsGizmoUsing();           // a handle is being dragged
VANGUI_API VanGizmoPart GetGizmoPart();           // hovered or dragged part of the last call
VANGUI_API bool         IsGizmoDragStarted();     // this frame a drag began (take an undo snapshot here)
VANGUI_API bool         IsGizmoDragEnded();       // this frame a drag was let go: the change is final
VANGUI_API bool         IsGizmoCancelled();       // this frame Escape ended a drag and the matrix went back
VANGUI_API void         GetGizmoDragMatrices(float before[16], float after[16]);   // start and end of the current or last drag
VANGUI_API void         GetGizmoDelta(float delta[16]);   // this frame's change: new = old * delta (apply it to a selection)

// The shared colours and sizes.
VANGUI_API VanGizmoStyle& GetGizmoStyle();

// ── View cube ────────────────────────────────────────────────────────────────

// A cube (or axis compass) turned like the camera. Click a face, edge or corner
// to swing the camera round to look from that side; drag it to orbit. The camera
// turns about the point `pivot_distance` in front of it. `pos` is the top-left
// corner, `size` the box (0 = 7 font heights). Returns true on every frame it
// changed `view`.
VANGUI_API bool ViewCube(const char* str_id, float* view, const float* projection, float pivot_distance,
                         VanVec2 pos, float size = 0.0f, VanViewCubeFlags flags = VanViewCubeFlags_None);

// ── Tools ────────────────────────────────────────────────────────────────────

// Mode buttons (move / rotate / scale / universal / bounds), world/local and
// snap toggles, as icons when the icon module is built. Pass nullptr to leave a
// group out. Returns true when anything changed.
VANGUI_API bool GizmoToolbar(const char* str_id, VanGizmoOp* ops, VanGizmoSpace* space = nullptr, bool* snap = nullptr,
                             bool vertical = false);
// W move, E rotate, R scale, T bounds, X world/local — ignored while typing or
// with Ctrl/Alt held. Returns true when anything changed.
VANGUI_API bool GizmoShortcuts(VanGizmoOp* ops, VanGizmoSpace* space = nullptr);

// ── Matrix helpers ───────────────────────────────────────────────────────────
// Same layout as above. Angles in degrees; Euler order X, then Y, then Z.

VANGUI_API void GizmoIdentity(float out[16]);
VANGUI_API void GizmoMultiply(const float a[16], const float b[16], float out[16]);   // a first, then b
VANGUI_API bool GizmoInverse(const float m[16], float out[16]);
VANGUI_API void GizmoCompose(const float translation[3], const float rotation_deg[3], const float scale[3], float out[16]);
VANGUI_API void GizmoDecompose(const float m[16], float translation[3], float rotation_deg[3], float scale[3]);
VANGUI_API void GizmoLookAt(const float eye[3], const float target[3], const float up[3], float out[16],
                            bool right_handed = true);
VANGUI_API void GizmoPerspective(float fov_y_deg, float aspect, float z_near, float z_far, float out[16],
                                 bool right_handed = true, bool zero_to_one_depth = false);
VANGUI_API void GizmoOrthographic(float left, float right, float bottom, float top, float z_near, float z_far,
                                  float out[16], bool right_handed = true, bool zero_to_one_depth = false);
// Where a world point lands in a view rectangle; false when it is behind the camera.
VANGUI_API bool GizmoWorldToScreen(const float view[16], const float projection[16], const float world[3],
                                   const VanVec2& viewport_min, const VanVec2& viewport_max, VanVec2* out);

#else // ----------------------------- shims -----------------------------------

inline bool Gizmo(const char*, const float*, const float*, float*, VanGizmoOp = VanGizmoOp_Translate, const VanGizmoOptions& = VanGizmoOptions()) { return false; }
inline bool Gizmo2D(const char*, VanGizmoTransform2D*, VanGizmoOp = VanGizmoOp_Translate, const VanCanvasView* = nullptr, const VanGizmoOptions& = VanGizmoOptions()) { return false; }
inline bool         IsGizmoOver() { return false; }
inline bool         IsGizmoUsing() { return false; }
inline VanGizmoPart GetGizmoPart() { return VanGizmoPart_None; }
inline bool         IsGizmoDragStarted() { return false; }
inline bool         IsGizmoDragEnded() { return false; }
inline bool         IsGizmoCancelled() { return false; }
inline void         GetGizmoDragMatrices(float*, float*) {}
inline void         GetGizmoDelta(float d[16]) { for (int i = 0; i < 16; ++i) d[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
inline VanGizmoStyle& GetGizmoStyle() { static VanGizmoStyle s; return s; }
inline bool ViewCube(const char*, float*, const float*, float, VanVec2, float = 0.0f, VanViewCubeFlags = VanViewCubeFlags_None) { return false; }
inline bool GizmoToolbar(const char*, VanGizmoOp*, VanGizmoSpace* = nullptr, bool* = nullptr, bool = false) { return false; }
inline bool GizmoShortcuts(VanGizmoOp*, VanGizmoSpace* = nullptr) { return false; }
inline void GizmoIdentity(float o[16]) { for (int i = 0; i < 16; ++i) o[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
inline void GizmoMultiply(const float*, const float*, float o[16]) { GizmoIdentity(o); }
inline bool GizmoInverse(const float*, float o[16]) { GizmoIdentity(o); return false; }
inline void GizmoCompose(const float*, const float*, const float*, float o[16]) { GizmoIdentity(o); }
inline void GizmoDecompose(const float*, float t[3], float r[3], float s[3]) { for (int i = 0; i < 3; ++i) { t[i] = r[i] = 0.0f; s[i] = 1.0f; } }
inline void GizmoLookAt(const float*, const float*, const float*, float o[16], bool = true) { GizmoIdentity(o); }
inline void GizmoPerspective(float, float, float, float, float o[16], bool = true, bool = false) { GizmoIdentity(o); }
inline void GizmoOrthographic(float, float, float, float, float, float, float o[16], bool = true, bool = false) { GizmoIdentity(o); }
inline bool GizmoWorldToScreen(const float*, const float*, const float*, const VanVec2&, const VanVec2&, VanVec2*) { return false; }

#endif // VANGUI_ENABLE_GIZMO

// One undo step per drag: when a drag ends, calls stack.perform(redo, undo) with
// closures that hand `apply` the matrix after and before the drag. Works with
// VanCommandStack from vangui_undo.h (perform runs `redo` at once, which only
// re-applies the matrix the object already has).
//     GizmoPushUndo(history, [id](const float* m) { SetObjectMatrix(id, m); });
template <class Stack, class Apply>
inline bool GizmoPushUndo(Stack& stack, Apply apply)
{
    if (!IsGizmoDragEnded())
        return false;
    VanGizmoMatrix before, after;
    GetGizmoDragMatrices(before.m, after.m);
    stack.perform([apply, after]() { apply(after.m); }, [apply, before]() { apply(before.m); });
    return true;
}

} // namespace VanGui
