// vangui_icons.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — drawn icons.
//
// A set of line-art icons drawn straight into a draw list: one stroke weight,
// round caps and joins, filleted corners, on a 24 x 24 grid. They scale to any
// size, take any colour and can be animated per frame. Each stroke covers its
// pixels once, corners and caps included, so a faded icon has no beads at its
// joints; where two separate strokes cross (a plus, a T-junction) the crossing
// is covered twice and reads slightly darker while the icon is translucent.
// Glyphs from AddIconFont() are flattened to one coverage mask and fade exactly.
//
//   Drawing      DrawIcon / DrawIconEx (colour, weight, style, draw-on trim,
//                rotation, scale, offset, alpha).
//   Styles       Outline (the designed look), Duotone (closed shapes washed with
//                a second colour), Filled (closed shapes solid, details knocked out).
//   Widgets      Icon (inline), IconButton, IconLabelButton, IconText.
//   In text      AddIconFont() merges the set into a font as private-use glyphs,
//                so any label can carry an icon: Button(VANICON_SAVE " Save").
//                The glyphs are rasterized from the same vectors at whatever
//                size the font is drawn, so they stay crisp at every scale.
//   Motion       DrawIconMorph (menu<->close, play<->pause, plus<->minus, ...),
//                DrawIconTransition (any icon to any icon), AnimatedIcon
//                (spin, pulse, shake, pop, bounce, draw-on, wiggle, blink).
//   Registry     RegisterIcon (draw with VanIconPen, the same grid and rules
//                as the built-in set), RegisterIconSvg (import SVG / path data).
//   Cache        Static icons are tessellated once per size and copied after,
//                so a locker full of icons costs a memcpy each.
//
// The grid rules every built-in icon is held to (each learned from a broken icon):
//   * round caps on every open end and round joins at every corner;
//   * one stroke weight, 2 grid units, on everything;
//   * strokes, not silhouettes (solid marks only where a thing is a hole or a dot);
//   * nothing closer than 1 unit to the edge;
//   * corners filleted: 2 units on anything 8 or more across, 1 on smaller detail;
//   * at least 2 units of clear space between separate parts;
//   * a round cap adds the stroke's thickness to a dash: keep gaps over ~3 units;
//   * a closed path must not repeat its first point.
//
// Opt-in / zero-cost via VANGUI_ENABLE_ICONS (requires VANGUI_ENABLE_VECTOR;
// the font glyphs also need VANGUI_ENABLE_RASTER, SVG icons VANGUI_ENABLE_SVG).
// Like the FreeType shim, the icon font is a VanFontLoader, which is the one
// part of this module that reaches into vangui_internal.h.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>
#include "vangui_vector.h"
#include <initializer_list>

#if defined(VANGUI_ENABLE_ICONS) && !defined(VANGUI_ENABLE_VECTOR)
#  error "VANGUI_ENABLE_ICONS requires VANGUI_ENABLE_VECTOR."
#endif

namespace VanGui {

typedef int VanIconID;

// The built-in set. Values are stable (they are also the glyph codepoints'
// offsets); new icons are appended.
enum VanIcon_ : int
{
    // ── Shell: headers and the screens they reach ──
    VanIcon_People = 0, VanIcon_Friends, VanIcon_Backpack, VanIcon_Cart, VanIcon_Shield, VanIcon_HousePlus,
    VanIcon_DoorIn, VanIcon_Crosshair, VanIcon_Person, VanIcon_Trophy, VanIcon_Gear, VanIcon_DoorOut,
    VanIcon_Newspaper, VanIcon_Envelope, VanIcon_Power, VanIcon_Question, VanIcon_PersonPlus, VanIcon_Calendar,
    // ── Combat ──
    VanIcon_Skull, VanIcon_Headshot, VanIcon_KnifeKill, VanIcon_GrenadeKill, VanIcon_Explosion, VanIcon_Fire,
    VanIcon_Wallbang, VanIcon_LongShot, VanIcon_NoScope, VanIcon_Backstab, VanIcon_Assist, VanIcon_Revenge,
    VanIcon_FirstBlood, VanIcon_DoubleKill, VanIcon_Streak, VanIcon_Damage, VanIcon_HitMarker, VanIcon_Armour,
    VanIcon_Health, VanIcon_Ammo,
    // ── Weapons ──
    VanIcon_Rifle, VanIcon_Smg, VanIcon_Sniper, VanIcon_Shotgun, VanIcon_MachineGun, VanIcon_Pistol,
    VanIcon_Knife, VanIcon_GrenadeItem, VanIcon_C4, VanIcon_Rocket,
    // ── Kit ──
    VanIcon_Helmet, VanIcon_Vest, VanIcon_Boots, VanIcon_Gloves, VanIcon_Mask, VanIcon_Magazine, VanIcon_Scope,
    VanIcon_Suppressor, VanIcon_Laser, VanIcon_Grip,
    // ── Numbers ──
    VanIcon_Kills, VanIcon_Deaths, VanIcon_Ratio, VanIcon_Accuracy, VanIcon_Score, VanIcon_Medal, VanIcon_Star,
    VanIcon_StarOutline, VanIcon_Crown, VanIcon_Chevron, VanIcon_Level, VanIcon_Graph, VanIcon_TrendUp, VanIcon_TrendDown,
    // ── Money and the shop ──
    VanIcon_Coin, VanIcon_Cash, VanIcon_Gem, VanIcon_PriceTag, VanIcon_Lock, VanIcon_Unlock, VanIcon_Crate,
    VanIcon_Gift, VanIcon_Key, VanIcon_Percent, VanIcon_Restock, VanIcon_Timer, VanIcon_Trash, VanIcon_Bundle,
    // ── People ──
    VanIcon_Chat, VanIcon_TeamChat, VanIcon_Mic, VanIcon_MicMute, VanIcon_Speaker, VanIcon_SpeakerMute,
    VanIcon_AddFriend, VanIcon_RemoveFriend, VanIcon_Block, VanIcon_Report, VanIcon_Online, VanIcon_Offline,
    // ── Matches ──
    VanIcon_Map, VanIcon_ModeTdm, VanIcon_ModeFfa, VanIcon_ModeBomb, VanIcon_Flag, VanIcon_Players, VanIcon_Private,
    VanIcon_Host, VanIcon_Ready, VanIcon_Spectate, VanIcon_Bomb, VanIcon_Defuse, VanIcon_Round, VanIcon_Clock,
    // ── Interface ──
    VanIcon_Check, VanIcon_Cross, VanIcon_Plus, VanIcon_Minus, VanIcon_ChevronLeft, VanIcon_ChevronRight,
    VanIcon_ChevronUp, VanIcon_ChevronDown, VanIcon_ArrowLeft, VanIcon_ArrowRight, VanIcon_Close, VanIcon_Menu,
    VanIcon_More, VanIcon_Info, VanIcon_Warning, VanIcon_Refresh, VanIcon_Search, VanIcon_Filter, VanIcon_Sort, VanIcon_Eye,
    // ── System ──
    VanIcon_EyeOff, VanIcon_Volume, VanIcon_Brightness, VanIcon_Fullscreen, VanIcon_Keyboard, VanIcon_Mouse,
    VanIcon_Monitor, VanIcon_Ping, VanIcon_Save, VanIcon_Folder, VanIcon_Edit, VanIcon_Copy,
    // ── Media ──
    VanIcon_Play, VanIcon_Pause, VanIcon_Stop, VanIcon_SkipBack, VanIcon_SkipForward, VanIcon_Record,
    VanIcon_Repeat, VanIcon_Shuffle,
    // ── Editing and tools ──
    VanIcon_Undo, VanIcon_Redo, VanIcon_Cut, VanIcon_Clipboard, VanIcon_Paste, VanIcon_Link, VanIcon_Unlink,
    VanIcon_ZoomIn, VanIcon_ZoomOut, VanIcon_Move, VanIcon_Rotate, VanIcon_Crop, VanIcon_Scale, VanIcon_Pointer,
    VanIcon_Ruler, VanIcon_Magnet,
    // ── Files and data ──
    VanIcon_File, VanIcon_FileText, VanIcon_FilePlus, VanIcon_FolderOpen, VanIcon_Image, VanIcon_Download,
    VanIcon_Upload, VanIcon_Archive, VanIcon_Code, VanIcon_Terminal, VanIcon_Database, VanIcon_Server, VanIcon_Package,
    VanIcon_Recent, VanIcon_Table, VanIcon_Tag, VanIcon_Hash, VanIcon_Tree, VanIcon_Chart,
    // ── Navigation, layout and view ──
    VanIcon_Home, VanIcon_ArrowUp, VanIcon_ArrowDown, VanIcon_ExternalLink, VanIcon_Sidebar, VanIcon_Panel,
    VanIcon_Columns, VanIcon_Grid, VanIcon_List, VanIcon_Layers, VanIcon_Maximize, VanIcon_Restore, VanIcon_Minimize,
    VanIcon_MoreVertical, VanIcon_DragHandle, VanIcon_History, VanIcon_Frame, VanIcon_FrameAll, VanIcon_Focus,
    // ── Status ──
    VanIcon_Bell, VanIcon_BellOff, VanIcon_CheckCircle, VanIcon_XCircle, VanIcon_AlertCircle, VanIcon_Help,
    VanIcon_Loader, VanIcon_Pin, VanIcon_Bookmark, VanIcon_Heart, VanIcon_Share, VanIcon_Send, VanIcon_Inbox, VanIcon_Sparkle,
    // ── World and map editing ──
    VanIcon_Globe, VanIcon_Cube, VanIcon_Wire, VanIcon_Road, VanIcon_Street, VanIcon_Route, VanIcon_Zone,
    VanIcon_Marker, VanIcon_Boundary, VanIcon_Chunk, VanIcon_Stream, VanIcon_Texture, VanIcon_Traffic, VanIcon_Gauge,
    VanIcon_Sun, VanIcon_Moon, VanIcon_Cloud, VanIcon_MapPin, VanIcon_Compass,
    // ── Things ──
    VanIcon_Palette, VanIcon_Brush, VanIcon_Camera, VanIcon_Bulb, VanIcon_Bug, VanIcon_Wrench, VanIcon_Wifi,
    VanIcon_Battery, VanIcon_Cpu, VanIcon_Plug, VanIcon_Music, VanIcon_Video, VanIcon_Headphones, VanIcon_Gamepad,
    VanIcon_Book, VanIcon_Sliders, VanIcon_Flask,

    VanIcon_COUNT,             // built-in icons
    VanIcon_UserFirst = 1024,  // RegisterIcon() hands out ids from here
    VanIcon_None = -1,
};

enum VanIconStyle
{
    VanIconStyle_Outline = 0,   // strokes only (the designed look)
    VanIconStyle_Duotone,       // closed shapes washed with the secondary colour
    VanIconStyle_Filled,        // closed shapes solid; details inside them knocked out in the secondary colour
};

struct VanIconParams
{
    VanU32       Color     = 0;      // 0 = text colour
    VanU32       Secondary = 0;      // duotone wash / filled knock-out; 0 = automatic (Color at 28% / window background)
    float        Thickness = 0.0f;   // stroke width in pixels; 0 = size / 12 (the designed weight)
    VanIconStyle Style     = VanIconStyle_Outline;
    float        Trim      = 1.0f;   // draw-on: 0..1 of the icon's total stroke length
    float        Rotation  = 0.0f;   // radians, about the centre
    float        Scale     = 1.0f;   // about the centre
    VanVec2      Offset    = VanVec2(0, 0);
    float        Alpha     = 1.0f;
};

// Two-state morphs between related icons (t = 0 the first, t = 1 the second).
enum VanIconMorph
{
    VanIconMorph_MenuClose = 0,      // three bars fold into a cross
    VanIconMorph_PlayPause,          // the triangle splits into two bars
    VanIconMorph_PlusMinus,          // the vertical bar closes
    VanIconMorph_PlusClose,          // the plus turns 45 degrees into a cross
    VanIconMorph_ChevronRightDown,   // disclosure arrow turns
    VanIconMorph_ArrowLeftRight,     // arrow swings round
    VanIconMorph_CheckCross,         // tick redraws as a cross
    VanIconMorph_EyeEyeOff,          // the slash draws across
    VanIconMorph_LockUnlock,         // the shackle lifts and swings
    VanIconMorph_MicMute,            // the slash draws across
    VanIconMorph_COUNT
};

// Motion presets for AnimatedIcon().
enum VanIconMotion
{
    VanIconMotion_None = 0,
    VanIconMotion_Spin,      // continuous rotation (refresh, loader, gear)
    VanIconMotion_Pulse,     // gentle scale breathing
    VanIconMotion_Shake,     // bell ring: a damped wobble every couple of seconds
    VanIconMotion_Pop,       // springs up when `active` turns on
    VanIconMotion_Bounce,    // hops
    VanIconMotion_DrawOn,    // draws itself when it appears (and again when `active` turns on)
    VanIconMotion_Wiggle,    // small continuous rock
    VanIconMotion_Blink,     // fades in and out (recording, live)
};

// The grid pen: the same coordinates, weights and helpers the built-in set is drawn
// with, for writing your own icons (RegisterIcon). Grid units: 24 across, origin top
// left; angles in radians, clockwise from +X (y down). Closed shapes (rect, circle,
// ellipse, closed paths) are what the Duotone and Filled styles fill.
struct VanIconPen
{
    VanDrawList* dl = nullptr;
    VanVec2      origin;          // where grid (0, 0) lands
    float        u = 1.0f;        // one grid unit in pixels
    VanStroke    s;               // colour and weight

    static constexpr float Grid = 24.0f, Stroke = 2.0f, Radius = 2.0f, Fine = 1.0f;

    VanVec2 at(float x, float y) const { return VanVec2(origin.x + x * u, origin.y + y * u); }

    void line(float x0, float y0, float x1, float y1) const;
    // Open or closed run of (x, y) pairs, corners rounded by `fillet` grid units.
    void path(const float* xy, int pairs, bool closed = false, float fillet = Radius) const;
    void path(std::initializer_list<float> xy, bool closed = false, float fillet = Radius) const { path(xy.begin(), int(xy.size() / 2), closed, fillet); }
    void rect(float x0, float y0, float x1, float y1, float r = Radius) const;
    void circle(float cx, float cy, float r) const;
    void ellipse(float cx, float cy, float rx, float ry) const;
    void arc(float cx, float cy, float r, float a0, float a1) const;
    void ellipse_arc(float cx, float cy, float rx, float ry, float a0, float a1) const;
    void curve(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) const;
    void dashed(float x0, float y0, float x1, float y1, float dash, float gap) const;
    void dashed_ring(float cx, float cy, float r, float dash, float gap) const;
    // Solid marks. A dot is sized in stroke widths so it carries the lines' weight;
    // fill_rect covers exactly what rect() with the same arguments would outline.
    void dot(float cx, float cy, float weights = 1.0f) const;
    void disc(float cx, float cy, float r) const;
    void fill(const float* xy, int pairs) const;
    void fill(std::initializer_list<float> xy) const { fill(xy.begin(), int(xy.size() / 2)); }
    void fill_rect(float x0, float y0, float x1, float y1, float r = Radius) const;

    // Composite path: begin(), then to()/arc_to()/curve_to(), then end(). One stroke, so
    // joins stay clean when the icon is drawn translucent.
    void begin() const;
    void to(float x, float y) const;
    void arc_to(float cx, float cy, float r, float a0, float a1) const;
    void curve_to(float c1x, float c1y, float c2x, float c2y, float x, float y) const;
    void end(bool closed = false, float fillet = 0.0f) const;

    // Shapes several icons share. figure() is a head over shoulders at scale k;
    // chevron() points along (dx, dy); arrow_head() puts a head of barb length
    // `head` on (x, y) pointing along `angle`.
    void figure(float cx, float cy, float k) const;
    void figure_half(float cx, float cy, float k, bool keep_right) const;
    void chevron(float cx, float cy, float reach, float span, float dx, float dy) const;
    void arrow(float x0, float y0, float x1, float y1, float head) const;
    void arrow_head(float x, float y, float angle, float head) const;
    void check(float cx, float cy, float k) const;
    void ex(float cx, float cy, float r) const;
    void plus(float cx, float cy, float r) const;
    void star(float cx, float cy, float r, bool filled) const;
    // The same pen on a grid shrunk about the icon's centre; the stroke keeps its weight.
    VanIconPen scaled(float k) const;

    void* ctx = nullptr;   // [internal] per-draw state (trim budget, style pass)
};

typedef void (*VanIconDrawFn)(const VanIconPen& pen, void* user_data);

#ifdef VANGUI_ENABLE_ICONS

// ── Drawing ──────────────────────────────────────────────────────────────────
// Drawn in a square of `size` centred on `centre`. `col` 0 = the text colour; `thickness`
// 0 = the designed weight (size/12).
VANGUI_API void DrawIcon(VanDrawList* dl, VanIconID icon, const VanVec2& centre, float size, VanU32 col = 0, float thickness = 0.0f);
VANGUI_API void DrawIconEx(VanDrawList* dl, VanIconID icon, const VanVec2& centre, float size, const VanIconParams& p);
VANGUI_API void DrawIconMorph(VanDrawList* dl, VanIconMorph morph, float t, const VanVec2& centre, float size, const VanIconParams& p = VanIconParams());
// Any icon to any other: the old one turns and shrinks away as the new one turns in.
VANGUI_API void DrawIconTransition(VanDrawList* dl, VanIconID from, VanIconID to, float t, const VanVec2& centre, float size, const VanIconParams& p = VanIconParams());

// ── Names and the registry ───────────────────────────────────────────────────
VANGUI_API const char* GetIconName(VanIconID icon);
VANGUI_API VanIconID   FindIcon(const char* name);               // VanIcon_None if unknown
VANGUI_API int         GetIconCount();                           // built-in + registered
VANGUI_API VanIconID   GetIconByIndex(int index);                // for pickers and galleries
// Names match case-insensitively and ignore '_' and '-' ("house-plus" finds HousePlus).
// Registering a built-in's name redraws that built-in everywhere (widgets, font glyphs)
// and returns its id; a null `draw` gives it its own drawing back. Any other name gets a
// new id from VanIcon_UserFirst. Drawings are cached per size: call ClearIconCache() if
// a callback's drawing changes.
VANGUI_API VanIconID   RegisterIcon(const char* name, VanIconDrawFn draw, void* user_data = nullptr);
// SVG document or bare path data ("M4 4 L20 20 ..." on a 24 grid, stroked 2 wide with round
// caps and joins like the built-in set). Drawn in the icon's colour, weight and style;
// each paint's own opacity is kept. Needs VANGUI_ENABLE_SVG.
VANGUI_API VanIconID   RegisterIconSvg(const char* name, const char* svg_or_path_data);

// ── Widgets ──────────────────────────────────────────────────────────────────
// size 0 = the current font size. col 0 = text colour.
VANGUI_API void Icon(VanIconID icon, float size = 0.0f, VanU32 col = 0);
VANGUI_API bool IconButton(const char* str_id, VanIconID icon, float size = 0.0f, const char* tooltip = nullptr);
VANGUI_API bool IconLabelButton(VanIconID icon, const char* label, const VanVec2& size = VanVec2(0, 0));
VANGUI_API void IconText(VanIconID icon, const char* fmt, ...) VAN_FMTARGS(2);
// A button whose icon morphs as *state flips (menu/close, play/pause ...). Returns true when clicked.
VANGUI_API bool IconToggleButton(const char* str_id, VanIconMorph morph, bool* state, float size = 0.0f);
// An inline icon with a motion preset. `active` drives Pop / DrawOn / Shake.
VANGUI_API void AnimatedIcon(const char* str_id, VanIconID icon, VanIconMotion motion, float size = 0.0f, VanU32 col = 0, bool active = true);

// ── Icons in text ────────────────────────────────────────────────────────────
// Merge the icon set into the atlas's last font (or add it as its own font) as
// private-use glyphs at U+E000 + icon id. Call while building fonts. The glyphs
// are rasterized on demand at the size text is drawn. Needs VANGUI_ENABLE_RASTER.
VANGUI_API VanFont*    AddIconFont(VanFontAtlas* atlas, float size_pixels = 0.0f, bool merge = true,
                                   VanIconStyle style = VanIconStyle_Outline);
VANGUI_API VanWchar    GetIconCodepoint(VanIconID icon);
// UTF-8 of the icon's glyph, for building labels at runtime (static storage).
VANGUI_API const char* GetIconUtf8(VanIconID icon);

// ── Cache ────────────────────────────────────────────────────────────────────
VANGUI_API void SetIconCacheEnabled(bool enabled);
VANGUI_API void ClearIconCache();
VANGUI_API int  GetIconCacheSize();

#else // ----------------------------- shims -----------------------------------

inline void DrawIcon(VanDrawList*, VanIconID, const VanVec2&, float, VanU32 = 0, float = 0.0f) {}
inline void DrawIconEx(VanDrawList*, VanIconID, const VanVec2&, float, const VanIconParams&) {}
inline void DrawIconMorph(VanDrawList*, VanIconMorph, float, const VanVec2&, float, const VanIconParams& = VanIconParams()) {}
inline void DrawIconTransition(VanDrawList*, VanIconID, VanIconID, float, const VanVec2&, float, const VanIconParams& = VanIconParams()) {}
inline const char* GetIconName(VanIconID) { return ""; }
inline VanIconID   FindIcon(const char*) { return VanIcon_None; }
inline int         GetIconCount() { return 0; }
inline VanIconID   GetIconByIndex(int) { return VanIcon_None; }
inline VanIconID   RegisterIcon(const char*, VanIconDrawFn, void* = nullptr) { return VanIcon_None; }
inline VanIconID   RegisterIconSvg(const char*, const char*) { return VanIcon_None; }
inline void Icon(VanIconID, float size = 0.0f, VanU32 = 0) { const float s = size > 0.0f ? size : VanGui::GetFontSize(); VanGui::Dummy(VanVec2(s, s)); }
inline bool IconButton(const char* str_id, VanIconID, float = 0.0f, const char* = nullptr) { return VanGui::Button(str_id); }
inline bool IconLabelButton(VanIconID, const char* label, const VanVec2& size = VanVec2(0, 0)) { return VanGui::Button(label, size); }
inline void IconText(VanIconID, const char* fmt, ...) { VanGui::TextUnformatted(fmt); }
inline bool IconToggleButton(const char* str_id, VanIconMorph, bool* state, float = 0.0f) { const bool c = VanGui::Button(str_id); if (c && state) *state = !*state; return c; }
inline void AnimatedIcon(const char*, VanIconID, VanIconMotion, float size = 0.0f, VanU32 = 0, bool = true) { const float s = size > 0.0f ? size : VanGui::GetFontSize(); VanGui::Dummy(VanVec2(s, s)); }
inline VanFont*    AddIconFont(VanFontAtlas*, float = 0.0f, bool = true, VanIconStyle = VanIconStyle_Outline) { return nullptr; }
inline VanWchar    GetIconCodepoint(VanIconID) { return 0; }
inline const char* GetIconUtf8(VanIconID) { return ""; }
inline void SetIconCacheEnabled(bool) {}
inline void ClearIconCache() {}
inline int  GetIconCacheSize() { return 0; }

// The pen without the module: it draws nothing.
inline void VanIconPen::line(float, float, float, float) const {}
inline void VanIconPen::path(const float*, int, bool, float) const {}
inline void VanIconPen::rect(float, float, float, float, float) const {}
inline void VanIconPen::circle(float, float, float) const {}
inline void VanIconPen::ellipse(float, float, float, float) const {}
inline void VanIconPen::arc(float, float, float, float, float) const {}
inline void VanIconPen::ellipse_arc(float, float, float, float, float, float) const {}
inline void VanIconPen::curve(float, float, float, float, float, float, float, float) const {}
inline void VanIconPen::dashed(float, float, float, float, float, float) const {}
inline void VanIconPen::dashed_ring(float, float, float, float, float) const {}
inline void VanIconPen::dot(float, float, float) const {}
inline void VanIconPen::disc(float, float, float) const {}
inline void VanIconPen::fill(const float*, int) const {}
inline void VanIconPen::fill_rect(float, float, float, float, float) const {}
inline void VanIconPen::begin() const {}
inline void VanIconPen::to(float, float) const {}
inline void VanIconPen::arc_to(float, float, float, float, float) const {}
inline void VanIconPen::curve_to(float, float, float, float, float, float) const {}
inline void VanIconPen::end(bool, float) const {}
inline void VanIconPen::figure(float, float, float) const {}
inline void VanIconPen::figure_half(float, float, float, bool) const {}
inline void VanIconPen::chevron(float, float, float, float, float, float) const {}
inline void VanIconPen::arrow(float, float, float, float, float) const {}
inline void VanIconPen::arrow_head(float, float, float, float) const {}
inline void VanIconPen::check(float, float, float) const {}
inline void VanIconPen::ex(float, float, float) const {}
inline void VanIconPen::plus(float, float, float) const {}
inline void VanIconPen::star(float, float, float, bool) const {}
inline VanIconPen VanIconPen::scaled(float) const { return *this; }

#endif // VANGUI_ENABLE_ICONS

} // namespace VanGui

// Compile-time glyph strings (VANICON_SAVE etc.) for labels, generated from the enum.
#include "vangui_icons_utf8.h"
