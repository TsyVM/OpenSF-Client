// The front end's look and its building blocks, on VanGUI and its suite (anim, notify, loading,
// banners, dialogs).
//
// Everything is laid out on a 1600x900 stage fitted uniformly into the window (the original's
// 1024x768 pages, widened to 16:9); stage(x, y) turns stage units into pixels. The look is the
// 2010 Soldier Front kit — olive-grey panels with a black keyline, gold and lime lettering, the
// lobby's own three-state button sprites — plus what the original never had: eased hovers,
// screens that slide in, toasts that resolve in place, spinners and progress bars.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/UI/UiLayer.hpp"
#include "Game/Ui/Atlas.hpp"

#include <vangui/vangui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lsf::ui {

inline constexpr float kStageW = 1600.0f;
inline constexpr float kStageH = 900.0f;

// ── Palette ────────────────────────────────────────────────────────────────────
struct Palette {
    VanVec4 ink{0.031f, 0.035f, 0.027f, 1.0f};
    VanVec4 panel{0.118f, 0.118f, 0.110f, 0.86f};        // bg_gray_1
    VanVec4 panel_brown{0.184f, 0.165f, 0.106f, 0.88f};  // bg_brown_1
    VanVec4 panel_title{0.243f, 0.243f, 0.220f, 0.95f};  // title_box_1
    VanVec4 keyline{0.0f, 0.0f, 0.0f, 1.0f};             // *LINEFRAME 255 0 0 0
    VanVec4 bevel{0.310f, 0.300f, 0.220f, 0.55f};
    VanVec4 select{0.251f, 0.294f, 0.110f, 0.90f};       // list_selected_1
    VanVec4 hover{0.300f, 0.300f, 0.230f, 0.45f};
    VanVec4 steel{0.302f, 0.290f, 0.247f, 1.0f};         // the button plates
    VanVec4 gold{0.839f, 0.675f, 0.337f, 1.0f};          // launcher gold
    VanVec4 gold_bright{0.953f, 0.827f, 0.557f, 1.0f};
    VanVec4 lime{0.878f, 0.867f, 0.369f, 1.0f};          // the kit's highlighted lettering
    VanVec4 text{0.886f, 0.890f, 0.863f, 1.0f};
    VanVec4 text_dim{0.588f, 0.588f, 0.550f, 1.0f};
    VanVec4 text_mute{0.388f, 0.388f, 0.360f, 1.0f};
    VanVec4 red{0.800f, 0.137f, 0.059f, 1.0f};           // teambar_red_1
    VanVec4 blue{0.169f, 0.263f, 0.827f, 1.0f};          // teambar_blue_1
    VanVec4 good{0.439f, 0.753f, 0.478f, 1.0f};
    VanVec4 bad{0.886f, 0.424f, 0.424f, 1.0f};
    VanVec4 warn{0.941f, 0.745f, 0.275f, 1.0f};
    VanVec4 marquee{0.984f, 0.290f, 0.290f, 1.0f};       // "Enjoy SpecialForce!!" 251 74 74
};
const Palette& pal();
VanU32 col(const VanVec4& c, float alpha = 1.0f);
VanVec4 mix(const VanVec4& a, const VanVec4& b, float t);
VanVec4 team_colour(eng::u8 team);
void apply_theme(VanGuiStyle& style);

// ── Stage ──────────────────────────────────────────────────────────────────────
// The picture's place in the window, in pixels: all of it, unless the game has the whole screen
// with a picture of another shape kept between bars. The stage, the HUD and the pages all sit in it.
struct ViewRect {
    float x = 0, y = 0, w = 1600, h = 900;
};
void set_view(float window_w, float window_h, const ViewRect& picture);
ViewRect view();
// The whole window is the picture.
void set_stage(float window_w, float window_h);
// The match's HUD: the stage at the picture's own shape, 900 high and as wide as that makes it
// (1200 at 4:3, 1440 at 16:10, 2100 at 21:9), so what hugs an edge hugs the picture's. The menus'
// stage stays 1600x900 fitted whole inside the picture. HudStage switches for a scope's length.
void stage_fill(bool fill);
bool stage_filled();
struct HudStage {
    HudStage() : was_(stage_filled()) { stage_fill(true); }
    ~HudStage() { stage_fill(was_); }
    HudStage(const HudStage&) = delete;
    HudStage& operator=(const HudStage&) = delete;

private:
    bool was_;
};
float stage_w();   // the stage's extent now: kStageW x kStageH, or the HUD's
float stage_h();
float scale();
VanVec2 stage(float x, float y);      // a point
float px(float design);                // a length
VanVec2 stage_origin();

// ── Fonts and art ──────────────────────────────────────────────────────────────
void set_fonts(const eng::UiFonts* fonts);
void set_atlas(Atlas* atlas);
Atlas* atlas();
VanFont* font_body();
VanFont* font_bold();
VanFont* font_heading();   // condensed (Bahnschrift): titles, HUD numbers
VanFont* font_display();
VanFont* font_mono();
VanFont* font_page();        // Tahoma: the original client's lobby face
VanFont* font_page_bold();
VanFont* font_kit();         // the kit's plate lettering (Ui/Kit.hpp)
VanVec2 text_size(VanFont* font, float size, std::string_view text);
enum class Align { Left, Center, Right };
// Text at stage coordinates, `size` in stage units.
void text(VanDrawList* dl, VanFont* font, float size, float x, float y, VanU32 colour, std::string_view s, Align align = Align::Left,
          bool shadow = true);

// ── Motion (VanGUI's anim substrate) ───────────────────────────────────────────
void tick(double now, float dt);
double now();
// An eased value that follows `target` (keyed by `id` in the current ID scope).
float follow(const char* id, float target, float seconds = 0.18f);
// 0 -> 1 over `seconds`, `delay` after `key` was first asked for this visit; reset_intros(prefix)
// replays a screen's entrance.
float intro(const std::string& key, float seconds = 0.35f, float delay = 0.0f);
void reset_intros(std::string_view prefix);
// How far (stage units) a panel still is from home on its entrance.
float slide(const std::string& key, float distance, float seconds = 0.4f, float delay = 0.0f);
float pulse(float hz = 1.2f, float low = 0.55f);

// ── Chrome ─────────────────────────────────────────────────────────────────────
void backdrop(const Picture& pic, float darken);
void sprite(VanDrawList* dl, const SpriteRef& s, float x, float y, float w, float h, VanU32 tint = 0xFFFFFFFF);
void picture(VanDrawList* dl, const Picture& p, float x, float y, float w, float h, VanU32 tint = 0xFFFFFFFF, bool cover = true);
// The kit's panel: grey fill, black keyline, a bevel, and a title strip when `title` is given.
void panel(VanDrawList* dl, float x, float y, float w, float h, const char* title = nullptr, bool brown = false);
// A borderless window over a stage rect for widgets. Always pair with end_area().
bool begin_area(const char* id, float x, float y, float w, float h, VanGuiWindowFlags extra = 0);
void end_area();

// ── Controls ───────────────────────────────────────────────────────────────────
// One of the lobby's own button sprites (states stacked: normal, hover, pressed, [disabled]),
// at stage coordinates. Eased hover glow; greyed and inert when !enabled.
bool sprite_button(const char* id, const char* sprite_name, float x, float y, float w, float h, bool enabled = true, int states = 3,
                   const char* tooltip = nullptr);
enum class Style { Normal, Primary, Danger, Ghost, Red, Blue };
// A drawn button at the cursor (inside an area), `w`/`h` stage units.
bool button(const char* label, float w, float h, Style style = Style::Normal, bool enabled = true);
// The same at stage coordinates, outside any area.
bool button_at(const char* id, const char* label, float x, float y, float w, float h, Style style = Style::Normal, bool enabled = true);
// A row of tabs; returns true when the selection changed.
bool tabs(const char* id, const std::vector<std::string>& labels, int& selected, float w, float h);
// ◀ value ▶, the pages' HORIZBAR: returns true when changed.
bool stepper(const char* id, const char* value, int& index, int count, float w, float h);
bool checkbox(const char* label, bool& v);
bool input(const char* id, const char* hint, std::string& value, float w, bool password = false, VanGuiInputTextFlags flags = 0);
// A phone's keyboard for the text field just drawn (text: its contents; nothing where a keyboard is
// at hand): up while the field has the focus, its typing put into the field as typing. Every text
// field calls it; phone_keyboard_frame() once a frame, after the screen, puts it away when its
// field is gone.
void phone_keyboard(const char* text, int limit);
void phone_keyboard_frame();
// The player's own machine, as the screens name it: "this PC", "this Mac", "this device".
const char* this_device();
void meter(float fraction, float w, float h, const VanVec4& colour, const char* overlay = nullptr);
// The marquee (*FLOWTEXT) scrolling across a stage rect.
void marquee(VanDrawList* dl, std::string_view text, float x, float y, float w, float size);
// A loading wheel (VanGUI Spinner) centred on a stage point.
void spinner(const char* id, float cx, float cy, float radius, const VanVec4& colour);
// A progress bar with the fraction and a caption.
void progress(VanDrawList* dl, float fraction, float x, float y, float w, float h, const char* caption = nullptr);
// A dimmed full-window veil (behind modals, over a loading screen).
void veil(float alpha);

// ── Sound ──────────────────────────────────────────────────────────────────────
// What a control plays when it is used (the client's Menu/ButtonClick); the app sets it once.
void set_click_sound(std::function<void()> play);
void click_sound();

// ── Modals ─────────────────────────────────────────────────────────────────────
void open_modal(const char* id);
// A centred modal with the kit's chrome that scales in; pair with end_modal() when it returns true.
bool begin_modal(const char* id, const char* title, float w, float h, bool* open = nullptr);
void end_modal();

// ── Toasts (VanGUI notifications) ──────────────────────────────────────────────
enum class Toast { Info, Good, Warning, Bad };
void toast(Toast kind, const char* fmt, ...);
// Where toasts go instead while it is set: the original's pages told you things in their chat
// log, so the page screens with one route them there.
void set_toast_sink(std::function<void(Toast, const std::string&)> sink);
// A toast that stays up with a progress sweep until resolved (connecting, buying, loading).
int pending(const char* text);
void resolve(int id, bool ok, const std::string& text);
void pending_progress(int id, float fraction);

}  // namespace lsf::ui
