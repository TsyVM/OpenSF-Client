// The options, as the client's own dialog laid them out ("USER SETTING"): System, Controls and
// Macro across the top in the kit's own plates, Default / Confirm / Cancel along the bottom.
//
//   System    Display   the window or the whole screen, the picture's shape and size, how a
//                       picture of another shape meets the screen; the renderer, vertical sync,
//                       the frame limit, brightness, field of view
//             Graphics  anti-aliasing, texture filtering; the small effects of a fight
//             Sound     the four volumes, the lobby's music
//             Game      the weapon in hand, always run; what the screen says
//             Crosshair the player's own: its lines, dot and ring, their sizes, its colour (free:
//                       nothing here is the Item Shop's)
//             Fidelity  not listed until it has been found (graphics_page): the finish, part by part
//   Controls  Basic Controls   the keys and the mouse (the original's actions, data/config.cfg)
//             Controller       a pad: its sticks and its buttons
//             Radio Message    the radio's lines
//   Macro     ten lines of chat
//
// What is offered is what the original offered (config.cfg: resolution, filter, gamma, blood,
// invisible weapon, always run, the keys) and what a screen of today needs (its shapes and sizes,
// three renderers, a controller). Nothing here changes a map's light, hour or sky, and nothing
// here thins a smoke grenade or a flash: those are the fight's, the same for everyone.
// Docs/Options.md has the reasons, and what was left out.
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Crosshair.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <utility>

namespace lsf {

using namespace ui;

namespace {

constexpr VanU32 kOptLabel = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kOptDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kKeyInk = VAN_COL32(224, 221, 94, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);
constexpr float kRow = 22.0f;

// A section of the options: the kit's own heading sprite (a bead and a word: GRAPHICS, SOUND,
// ...) and a keyline running on to x1.
void option_heading(const char* sprite, float x0, float y, float x1) {
    ui::Atlas* a = ui::atlas();
    const ui::SpriteRef s = a ? a->sprite(sprite) : ui::SpriteRef{};
    const float w = s.valid() ? s.w : 60, h = s.valid() ? s.h : 15;
    if (s.valid()) ui::sprite_at(sprite, 0, 1, x0, y, x0 + w, y + h);
    ui::fill_at(x0 + w + 6, y + h * 0.5f, x1, y + h * 0.5f + 1, kRule);
}

void label(float x, float y, std::string_view s, bool enabled = true) {
    ui::text_at(x, y, x + 102, y + 18, s, enabled ? kOptLabel : kOptDim, ui::Align::Left, true);
}

// A line of small print under a setting.
void note(float x0, float y, float x1, std::string_view s) { ui::text_at(x0, y, x1, y + 15, s, kOptDim, ui::Align::Left, true, 11.0f); }

// What a row is, said in the line along the bottom of the dialog while the pointer is on it.
void hint(float x0, float y, float x1, const char* text) {
    if (VanGui::IsMouseHoveringRect(ui::pg(x0, y), ui::pg(x1, y + 18))) ui::tip(text);
}

// A key and what it does, the key on a small plate.
void key_row(float x0, float y, float x1, const char* action, const char* key) {
    ui::text_at(x0, y, x1 - 96, y + 18, action, kOptLabel, ui::Align::Left, true);
    ui::sprite_at("bg_gray_1", 0, 1, x1 - 94, y + 1, x1, y + 17);
    ui::text_at(x1 - 94, y + 1, x1, y + 17, key, kKeyInk, ui::Align::Center, true, 11.0f);
}

// A key the player sets: click its plate, then press the key (or a mouse button); Esc leaves it,
// Backspace takes the key off. A key another action already had is taken from it, so no two
// actions share one.
void bind_row(App& app, float x0, float y, float x1, Action a) {
    ScreenState& st = app.state();
    Settings& e = st.edit;
    const bool waiting = st.binding == int(a);
    bool hovered = false;
    if (ui::region(9600 + int(a), x1 - 94, y + 1, x1, y + 17, &hovered) && !waiting) {
        st.binding = int(a);
        st.pad_binding = -1;
        st.binding_since = app.now();
        ui::click_sound();
    }
    ui::text_at(x0, y, x1 - 96, y + 18, action_info(a).label, kOptLabel, ui::Align::Left, true);
    ui::sprite_at("bg_gray_1", 0, 1, x1 - 94, y + 1, x1, y + 17, waiting || hovered ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 215));
    const std::string key = waiting ? "press a key" : e.bind(a) ? eng::Input::binding_name(e.bind(a)) : std::string("-");
    ui::text_at(x1 - 94, y + 1, x1, y + 17, key, waiting ? VAN_COL32(255, 220, 90, 255) : kKeyInk, ui::Align::Center, true, 11.0f);
    if (!waiting || app.now() - st.binding_since < 0.2) return;
    const eng::Input& in = app.window().input();
    if (in.key_pressed(VK_ESCAPE)) {
        st.binding = -1;
        return;
    }
    if (in.key_pressed(VK_BACK)) {
        e.binds[size_t(a)] = 0;
        st.binding = -1;
        return;
    }
    // Shift, Ctrl and Alt arrive as both the generic key and its side; the side is what is kept.
    u32 code = 0;
    for (u32 sided : {u32(VK_LSHIFT), u32(VK_RSHIFT), u32(VK_LCONTROL), u32(VK_RCONTROL), u32(VK_LMENU), u32(VK_RMENU)})
        if (in.key_pressed(sided)) code = sided;
    if (!code) code = in.first_pressed();
    if (code == 0 || code == VK_SHIFT || code == VK_CONTROL || code == VK_MENU) return;
    // One key, one action -- but Last weapon and Drop may share one, as the original's F did (a tap
    // and a hold: GameWorld::update_weapon).
    const auto pair = [](size_t x, size_t y) {
        const size_t l = size_t(Action::LastWeapon), d = size_t(Action::DropWeapon);
        return (x == l && y == d) || (x == d && y == l);
    };
    for (size_t k = 0; k < e.binds.size(); ++k)
        if (e.binds[k] == code && !pair(k, size_t(a))) e.binds[k] = 0;
    e.binds[size_t(a)] = code;
    st.binding = -1;
}

// The same for the controller: click the plate, press the button. Esc (on the keyboard) leaves
// it, Backspace takes the button off; so does waiting six seconds.
void pad_row(App& app, float x0, float y, float x1, Action a, bool enabled) {
    ScreenState& st = app.state();
    Settings& e = st.edit;
    const eng::Gamepad& pad = app.pad();
    const bool waiting = st.pad_binding == int(a);
    bool hovered = false;
    if (ui::region(9700 + int(a), x1 - 94, y + 1, x1, y + 16, &hovered) && !waiting && enabled) {
        if (!pad.connected()) {
            toast(Toast::Info, "Plug a controller in to set its buttons.");
        } else {
            st.pad_binding = int(a);
            st.binding = -1;
            st.binding_since = app.now();
            ui::click_sound();
        }
    }
    ui::text_at(x0, y, x1 - 96, y + 17, action_info(a).label, enabled ? kOptLabel : kOptDim, ui::Align::Left, true);
    ui::sprite_at("bg_gray_1", 0, 1, x1 - 94, y + 1, x1, y + 16, (waiting || hovered) && enabled ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, enabled ? 215 : 110));
    const eng::PadKind kind = pad.connected() ? pad.kind() : eng::PadKind::Xbox;
    const unsigned button = e.pad.binds[size_t(a)];
    const std::string name = waiting ? "press a button" : button ? eng::Gamepad::button_label(button, kind) : std::string("-");
    ui::text_at(x1 - 94, y + 1, x1, y + 16, name, waiting ? VAN_COL32(255, 220, 90, 255) : enabled ? kKeyInk : kOptDim, ui::Align::Center, true, 11.0f);
    if (!waiting) return;
    const eng::Input& in = app.window().input();
    if (in.key_pressed(VK_ESCAPE) || app.now() - st.binding_since > 6.0 || !pad.connected()) {
        st.pad_binding = -1;
        return;
    }
    if (in.key_pressed(VK_BACK)) {
        e.pad.binds[size_t(a)] = 0;
        st.pad_binding = -1;
        return;
    }
    if (app.now() - st.binding_since < 0.2) return;
    const u32 pressed = pad.any_pressed();
    if (!pressed || pressed == eng::kPadStart) return;   // Start is the menu's, always
    for (unsigned& other : e.pad.binds)
        if (other == pressed) other = 0;
    e.pad.binds[size_t(a)] = pressed;
    st.pad_binding = -1;
}

// A 0..1 volume as a 0..100 slider.
void volume_row(int key, float x0, float y, float x1, const char* name, float& v) {
    label(x0, y, name);
    float pct = v * 100.0f;
    if (ui::trackbar(key, x0 + 104, y, x1, y + 18, pct, 0, 100, "%.0f%%")) v = std::clamp(pct / 100.0f, 0.0f, 1.0f);
}

// A row that steps through named values with the arrows. Returns true when it changed.
bool choice_row(int key, float x0, float y, float x1, const char* name, int& index, std::initializer_list<const char*> names, bool enabled = true) {
    label(x0, y, name, enabled);
    const int n = int(names.size());
    index = std::clamp(index, 0, n - 1);
    const int d = ui::arrows(key, x0 + 104, y, x1, y + 18, *(names.begin() + index), enabled);
    if (d == 0) return false;
    index = (index + d + n) % n;
    return true;
}

// A row that steps through numbers (anti-aliasing's samples, the filter's).
bool number_row(int key, float x0, float y, float x1, const char* name, int& value, std::initializer_list<int> values,
                const std::function<std::string(int)>& text) {
    label(x0, y, name);
    int at = 0;
    for (int i = 0; i < int(values.size()); ++i)
        if (*(values.begin() + i) == value) at = i;
    const int d = ui::arrows(key, x0 + 104, y, x1, y + 18, text(*(values.begin() + at)));
    if (d == 0) return false;
    at = (at + d + int(values.size())) % int(values.size());
    value = *(values.begin() + at);
    return true;
}

// ── Display: sizes ─────────────────────────────────────────────────────────────

// The sizes the Resolution row steps through, for the mode and the shape chosen. With the whole
// screen the first is 0 x 0: the screen's own, whatever shape it is.
std::vector<eng::ScreenSize> size_list(App& app, const Settings& e) {
    std::vector<eng::ScreenSize> sizes = app.resolutions(e.borderless, e.aspect);
    if (e.borderless) {
        std::erase(sizes, app.window().screen());
        sizes.insert(sizes.begin(), eng::ScreenSize{0, 0});
    }
    return sizes;
}

eng::ScreenSize& size_of(Settings& e, eng::ScreenSize& scratch) {
    scratch = e.borderless ? eng::ScreenSize{e.full_width, e.full_height} : eng::ScreenSize{e.width, e.height};
    return scratch;
}

void set_size(Settings& e, const eng::ScreenSize& s) {
    if (e.borderless) e.full_width = s.width, e.full_height = s.height;
    else e.width = s.width, e.height = s.height;
}

// After the mode or the shape changed: a size the list has. A window takes the largest of the
// shape no wider than it was; the whole screen goes back to the screen's own.
void settle_size(App& app, Settings& e) {
    const std::vector<eng::ScreenSize> sizes = size_list(app, e);
    eng::ScreenSize scratch;
    const eng::ScreenSize cur = size_of(e, scratch);
    if (sizes.empty() || std::find(sizes.begin(), sizes.end(), cur) != sizes.end()) return;
    if (e.borderless) {
        // A shape was picked: its largest size (the list's last); any shape: the screen's own.
        set_size(e, e.aspect > 0 && sizes.size() > 1 ? sizes.back() : sizes.front());
        return;
    }
    eng::ScreenSize best = sizes.front();
    for (const eng::ScreenSize& s : sizes)
        if (s.width <= cur.width) best = s;
    set_size(e, best);
}

const char* shape_name(int w, int h) {
    const int a = aspect_of(w, h);
    return a > 0 ? aspect_info(a).label : "its own shape";
}

void display_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    ui::heading(L0, y, L1, "DISPLAY");
    float ly = y + 24;
    int mode = e.borderless ? 1 : 0;
    if (choice_row(9200, L0, ly, L1, "Display", mode, {"In a window", "The whole screen"})) {
        e.borderless = mode == 1;
        settle_size(app, e);
    }
    hint(L0, ly, L1, "In a window, or the whole screen (borderless: no mode change, Alt+Tab is instant).");
    ly += kRow;
    if (choice_row(9201, L0, ly, L1, "Shape", e.aspect, {"Any", "4:3", "5:4", "16:10", "16:9", "21:9"})) settle_size(app, e);
    hint(L0, ly, L1, "Which sizes the Resolution row offers. Not a setting of its own.");
    ly += kRow;
    const std::vector<eng::ScreenSize> sizes = size_list(app, e);
    const eng::ScreenSize screen = app.window().screen();
    eng::ScreenSize scratch;
    const eng::ScreenSize cur = size_of(e, scratch);
    int at = -1;
    for (int i = 0; i < int(sizes.size()); ++i)
        if (sizes[size_t(i)] == cur) at = i;
    label(L0, ly, "Resolution");
    const std::string size_text = cur.width > 0 ? eng::str::format("%d x %d", cur.width, cur.height)
                                                : eng::str::format("This screen's (%d x %d)", screen.width, screen.height);
    if (const int d = ui::arrows(9202, L0 + 104, ly, L1, ly + 18, size_text, sizes.size() > 1 || (at < 0 && !sizes.empty())); d != 0 && !sizes.empty()) {
        const int n = int(sizes.size());
        if (at < 0) {
            // A size the list does not have (set by hand): on to its nearest neighbour.
            at = d > 0 ? n - 1 : 0;
            for (int i = 0; i < n; ++i)
                if (d > 0 ? sizes[size_t(i)].width >= cur.width : sizes[size_t(i)].width <= cur.width) {
                    at = i;
                    if (d > 0) break;
                }
        } else {
            at = (at + d + n) % n;
        }
        set_size(e, sizes[size_t(at)]);
    }
    hint(L0, ly, L1, e.borderless ? "The size the match is drawn at. The lobby and the HUD are always drawn at the screen's own size."
                                  : "The window's size.");
    ly += kRow;
    // Only a picture of another shape than the screen has a choice to make.
    const bool other_shape = e.borderless && cur.width > 0 &&
                             std::fabs(float(cur.width) / float(cur.height) - float(screen.width) / float(std::max(1, screen.height))) > 0.01f;
    int scaling = e.scaling == Settings::Scaling::Bars ? 1 : 0;
    if (choice_row(9203, L0, ly, L1, "Picture", scaling, {"Stretched to fill", "Black bars"}, other_shape))
        e.scaling = scaling == 1 ? Settings::Scaling::Bars : Settings::Scaling::Stretch;
    hint(L0, ly, L1, "A picture of another shape than the screen: pulled over all of it, as the original looked on a wide screen, or kept in shape between black bars.");
    ly += kRow + 4;
    note(L0, ly, L1, eng::str::format("This screen is %d x %d (%s).", screen.width, screen.height, shape_name(screen.width, screen.height)));
    ly += 15;
    if (other_shape) {
        note(L0, ly, L1, e.scaling == Settings::Scaling::Bars ? "The picture keeps its shape, black at its sides." : "The picture is pulled over the whole screen,");
        if (e.scaling != Settings::Scaling::Bars) note(L0, ly + 15, L1, "as the original looked on a wide one.");
        ly += 30;
    }
    if (e.borderless && cur.width > 0 && (cur.width < screen.width || cur.height < screen.height))
        note(L0, ly, L1, "The match is drawn at this size; the menus stay sharp.");

    ui::heading(R0, y, R1, "PICTURE");
    float ry = y + 24;
    int renderer = int(e.renderer);
    if (choice_row(9210, R0, ry, R1, "Renderer", renderer, {"Automatic", "DirectX 12", "DirectX 11", "OpenGL"})) e.renderer = Settings::Renderer(renderer);
    hint(R0, ry, R1, "What draws the game. Automatic: DirectX 12 where the card has it. Taken when the game starts.");
    ry += kRow;
    note(R0, ry - 2, R1, eng::str::format("Drawing with %s now.%s", eng::api_name(app.device().api()),
                                           e.renderer != app.settings().renderer ? " Another one needs a restart." : ""));
    ry += 18;
    (void)ui::check(9211, R0, ry, "Vertical sync", e.vsync, true, "The frame rate follows the screen's own: no tearing, a frame or so more delay.");
    ry += kRow;
    label(R0, ry, "Frame limit", !e.vsync);
    float fps = float(e.max_fps);
    if (ui::trackbar(9212, R0 + 104, ry, R1, ry + 18, fps, 30, 360, "%.0f", !e.vsync)) e.max_fps = int(std::lround(fps));
    hint(R0, ry, R1, "The most frames a second the game draws, without vertical sync.");
    ry += kRow;
    label(R0, ry, "Brightness");
    (void)ui::trackbar(9213, R0 + 104, ry, R1, ry + 18, e.brightness, 0.5f, 2.0f, "%.2f");
    hint(R0, ry, R1, "The original's gamma: the whole match lighter or darker. A map's own light is left as it was made.");
    ry += kRow;
    label(R0, ry, "Field of view");
    (void)ui::trackbar(9214, R0 + 104, ry, R1, ry + 18, e.fov, 60.0f, 110.0f, "%.0f");
    hint(R0, ry, R1, "How wide you see, in degrees across a 16:9 screen (a wider screen sees more at the sides).");
    ry += kRow + 4;
    note(R0, ry, R1, app.device().adapter_name());
}

void graphics_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    ScreenState& st = app.state();
    option_heading("graphic_text", L0, y, L1);
    // Seven taps on the word itself: something the options do not list.
    if (ui::region(9229, L0, y - 2, L0 + 96, y + 17) && !e.fidelity_found && ++st.fidelity_taps >= 7) {
        e.fidelity_found = e.fidelity = true;
        app.settings().fidelity_found = true;
        app.save_settings();
        st.system_sub = 5;   // its own page, there from now on
        toast(Toast::Good, "Fidelity. You found it.");
    }
    float ly = y + 24;
    (void)number_row(9220, L0, ly, L1, "Anti-aliasing", e.antialiasing, {1, 2, 4, 8},
                     [](int v) { return v <= 1 ? std::string("Off") : eng::str::format("%dx", v); });
    hint(L0, ly, L1, "Smooths the stairs on the edges of the world (multisampling). Off, as the original was.");
    ly += kRow;
    (void)number_row(9221, L0, ly, L1, "Texture filter", e.texture_filter, {1, 2, 4, 8, 16},
                     [](int v) { return v <= 1 ? std::string("Trilinear") : eng::str::format("Anisotropic %dx", v); });
    hint(L0, ly, L1, "How sharp a floor or a wall stays when seen at a slant. 8x, as the original had it.");
    ly += kRow;
    int shade = int(e.shadows);
    if (choice_row(9222, L0, ly, L1, "Shadows", shade, {"Off", "Under the feet", "From the sun"})) e.shadows = Settings::Shadows(shade);
    hint(L0, ly, L1, "Soldiers' shadows. Under the feet: the original's soft shade. From the sun: each soldier also casts "
                     "his own from the map's sun.");
    ly += kRow + 4;
    note(L0, ly, L1, "A map's light, hour and sky are always its own.");

    option_heading("advanced_text", R0, y, R1);
    float ry = y + 24;
    (void)ui::check(9230, R0, ry, "Bullet marks", e.bullet_marks, true, "Holes where shots strike the map. Yours alone to see.");
    (void)ui::check(9231, R0 + 150, ry, "Spent cases", e.spent_cases, true, "Cases thrown out of every gun that fires, lying where they land a while.");
    ry += kRow;
    (void)ui::check(9232, R0, ry, "Blood", e.blood, true, "The original's blood setting: a hit soldier bleeds, and splashes the wall or floor behind him.");
    (void)ui::check(9233, R0 + 150, ry, "Impact dust", e.impact_dust, true, "The chips and dust a shot knocks off what it strikes.");
    ry += kRow;
    (void)ui::check(9234, R0, ry, "Footsteps", e.step_effects, true,
                    "Dust off soil and sand, grass, snow and splashes in water where soldiers run and land. The steps are always heard.");
    ry += kRow + 4;
    note(R0, ry, R1, "Smoke and flash grenades are always drawn in");
    note(R0, ry + 15, R1, "full: they are the same fight for everyone.");
}

void sound_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    option_heading("sound_text", L0, y, L1);
    float ly = y + 24;
    volume_row(9240, L0, ly, L1, "Master", e.master_volume);
    hint(L0, ly, L1, "Everything the game plays.");
    ly += kRow;
    volume_row(9241, L0, ly, L1, "Effects", e.effects_volume);
    hint(L0, ly, L1, "The match: shots, steps, blasts, the radio.");
    ly += kRow;
    volume_row(9242, L0, ly, L1, "Music", e.music_volume);
    hint(L0, ly, L1, "The lobby's music.");
    ly += kRow;
    volume_row(9243, L0, ly, L1, "Interface", e.ui_volume);
    hint(L0, ly, L1, "The menus' clicks and chimes.");
    ly += kRow + 6;
    label(L0, ly, "Radio voice");
    if (const int d = ui::arrows(9245, L0 + 104, ly, L1, ly + 18, Settings::radio_voice_name(e.radio_voice)); d != 0)
        e.radio_voice = (e.radio_voice + d + Settings::kRadioVoices) % Settings::kRadioVoices;
    hint(L0, ly, L1, "The original's radio language: the Z, X and C lines in English, German, Korean or Spanish. "
                     "A man's voice or a woman's, as the soldier speaking is.");
    ui::heading(R0, y, R1, "LOBBY");
    (void)ui::check(9244, R0, y + 24, "Music in the lobby", e.lobby_music, true, "The lobby's sound switch: its music on or off.");
    // Heard as they are set.
    Settings preview = app.settings();
    preview.master_volume = e.master_volume, preview.effects_volume = e.effects_volume, preview.music_volume = e.music_volume, preview.ui_volume = e.ui_volume;
    app.sounds().apply(preview);
}

// A slider under a name, the name dimmed with it.
void slider_row(int key, float x0, float y, float x1, const char* name, float& v, float lo, float hi, const char* format, bool enabled = true) {
    label(x0, y, name, enabled);
    (void)ui::trackbar(key, x0 + 104, y, x1, y + 18, v, lo, hi, format, enabled);
}

void game_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    ui::heading(L0, y, L1, "MATCH");
    float ly = y + 24;
    (void)ui::check(9252, L0, ly, "Always run", e.always_run, true, "The original's always run. Off: you walk, and the Walk key runs.");
    ly += kRow;
    (void)ui::check(9253, L0, ly, "Hide the weapon in my hands", e.hide_weapon, true, "The original's invisible weapon: nothing in your hands on screen. A clear view, the same aim.");
    ly += kRow;
    (void)ui::check(9257, L0, ly, "Take a picked-up weapon in hand", e.auto_switch, true,
                    "The original's auto switch: a gun taken up off the floor comes straight to hand. Off: it is slung, and you keep what you are holding.");
    ly += kRow + 6;
    int hold = e.scope_hold ? 1 : 0;
    if (choice_row(9254, L0, ly, L1, "Scope", hold, {"Press in, press out", "Hold to look"})) e.scope_hold = hold == 1;
    hint(L0, ly, L1, "Press in, press out: the original's (a sniper rifle's goes in two steps). Hold to look: scoped while the key is held, at full power.");
    ly += kRow;
    slider_row(9255, L0, ly, L1, "Scope speed", e.scope_speed, 0.2f, 3.0f, "%.2f");
    hint(L0, ly, L1, "How fast the aim turns through a scope, against the weapon's own (1.00). The mouse's speed outside a scope is on Controls.");
    ly += kRow;
    slider_row(9256, L0, ly, L1, "Weapon view", e.viewmodel_fov, 40.0f, 80.0f, "%.0f deg");
    hint(L0, ly, L1, "The lens the weapon in your hands is drawn through: larger shows it smaller and further off. The world keeps its own field of view.");

    ui::heading(R0, y, R1, "SCREEN");
    float ry = y + 24;
    (void)ui::check(9260, R0, ry, "Show the frame rate", e.show_fps, true, "Frames a second and the renderer, at the foot of the screen.");
    ry += kRow;
    (void)ui::check(9263, R0, ry, "Show the net graph", e.show_netgraph, true, "In a match: your ping and frame rate over the last few seconds, as a graph.");
    ry += kRow;
    (void)ui::check(9264, R0, ry, "Show damage numbers", e.damage_numbers, true,
                    "In a match: each hit of yours as a number over the soldier you hit, your total for him under them. Gone after 3 s without hurting him.");
    ry += kRow;
    (void)ui::check(9261, R0, ry, "Remember my account name", e.remember, true, "Your account name is kept for the next sign-in. Never the password.");
    ry += kRow;
    (void)ui::check(9262, R0, ry, "Sharp interface", e.sharp_ui, true, "The lobby's buttons and tabs redrawn at your screen's size. Off: the original bitmaps.");
    ry += kRow + 10;
    // D25, D28: your Team Vanilla account's, the same on every server; changed at once.
    ui::heading(R0, ry, R1, "PRIVACY");
    ry = privacy_rows(app, R0, ry + 24, R1, true);
    if (app.session().tv.signed_in) {
        note(R0, ry, R1, "Kept with your account, the same on every server.");
        note(R0, ry + 15, R1, "Staff and Team Vanilla's own mail always reach you.");
    } else {
        note(R0, ry, R1, "Sign in to choose these: they are kept with your account.");
    }
}

// ── Crosshair: the player's own, free ─────────────────────────────────────────

void crosshair_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    Crosshair& c = e.crosshair;
    ui::heading(L0, y, L1, "SHAPE");
    float ly = y + 24;
    // A shape to start from: the original's own three, and four more.
    const int preset = crosshair_preset_of(c);
    label(L0, ly, "Start from");
    if (const int d = ui::arrows(9270, L0 + 104, ly, L1, ly + 18, preset >= 0 ? crosshair_preset_name(preset) : "Your own"); d != 0) {
        const int n = kCrosshairPresets;
        c = crosshair_preset(preset < 0 ? (d > 0 ? 0 : n - 1) : (preset + d + n) % n, c);
    }
    ly += kRow + 2;
    (void)ui::check(9271, L0, ly, "Lines", c.lines);
    (void)ui::check(9272, L0 + 150, ly, "Upper line", c.top, c.lines, "Off: a T, clear above the centre");
    ly += kRow;
    slider_row(9273, L0, ly, L1, "  Length", c.length, 1.0f, 30.0f, "%.0f", c.lines);
    ly += kRow;
    slider_row(9274, L0, ly, L1, "  Thickness", c.thickness, 1.0f, 6.0f, "%.0f", c.lines || c.ring);
    ly += kRow;
    slider_row(9275, L0, ly, L1, "  Gap", c.gap, 0.0f, 20.0f, "%.0f", c.lines || c.ring);
    ly += kRow + 2;
    (void)ui::check(9276, L0, ly, "Dot", c.dot);
    (void)ui::trackbar(9277, L0 + 104, ly, L1, ly + 18, c.dot_size, 0.5f, 6.0f, "%.1f", c.dot);
    ly += kRow;
    (void)ui::check(9278, L0, ly, "Ring", c.ring);
    (void)ui::trackbar(9279, L0 + 104, ly, L1, ly + 18, c.ring_size, 1.0f, 30.0f, "%.0f", c.ring);
    ly += kRow + 2;
    (void)ui::check(9280, L0, ly, "Dark edge", c.outline, true, "A dark outline, so it shows against a bright wall or the sky");
    (void)ui::check(9281, L0 + 150, ly, "Opens with your aim", c.moves, true,
                    "It spreads as your aim loosens: running, in the air, firing. Off: it stands still");
    ly += kRow + 4;
    note(L0, ly, L1, "Yours to shape, and free: nothing here is bought.");
    // Whole steps, and something always drawn.
    c.length = std::round(c.length), c.thickness = std::round(c.thickness), c.gap = std::round(c.gap), c.ring_size = std::round(c.ring_size);
    if (!c.lines && !c.dot && !c.ring) c.dot = true;

    ui::heading(R0, y, R1, "COLOUR");
    float ry = y + 24;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    for (int i = 0; i < kCrosshairInks; ++i) {
        const CrosshairInk& ink = crosshair_ink(i);
        const float sx = R0 + 37.0f * float(i);
        const bool chosen = c.r == ink.r && c.g == ink.g && c.b == ink.b;
        bool hovered = false;
        if (ui::region(9282 + i, sx, ry, sx + 30, ry + 18, &hovered)) c.r = ink.r, c.g = ink.g, c.b = ink.b, ui::click_sound();
        dl->AddRectFilled(ui::pg(sx, ry), ui::pg(sx + 30, ry + 18), VAN_COL32(ink.r, ink.g, ink.b, 255));
        dl->AddRect(ui::pg(sx, ry), ui::pg(sx + 30, ry + 18), chosen ? VAN_COL32(255, 255, 255, 255) : hovered ? VAN_COL32(200, 200, 196, 255) : VAN_COL32(20, 20, 18, 255),
                    0.0f, 0, chosen ? std::max(2.0f, ui::pgy(2)) : 1.0f);
        if (hovered) VanGui::SetTooltip("%s", ink.name);
    }
    ry += kRow + 4;
    auto channel = [&](int key, const char* name, int& v, float lo) {
        float f = float(v);
        label(R0, ry, name);
        if (ui::trackbar(key, R0 + 104, ry, R1, ry + 18, f, lo, 255.0f, "%.0f")) v = int(std::lround(f));
        ry += kRow;
    };
    channel(9292, "Red", c.r, 0.0f);
    channel(9293, "Green", c.g, 0.0f);
    channel(9294, "Blue", c.b, 0.0f);
    channel(9295, "Opacity", c.a, 60.0f);

    // As it is in a match, over a dark wall, a pale one and the sky. It opens now and then, as it
    // would for a burst, when it is one that moves.
    ry += 6;
    ui::heading(R0, ry, R1, "PREVIEW");
    ry += 22;
    const float py0 = ry, py1 = ry + 104;
    ui::well(R0, py0, R1, py1);
    static const VanU32 backs[3] = {VAN_COL32(34, 33, 29, 255), VAN_COL32(150, 141, 124, 255), VAN_COL32(168, 200, 232, 255)};
    const float bw = (R1 - R0 - 2) / 3.0f;
    const float beat = float(std::fmod(app.now(), 3.2));
    const float open = c.moves && beat > 2.0f ? std::sin((beat - 2.0f) / 1.2f * eng::kPi) * 12.0f : 0.0f;
    const float unit = ui::crosshair_unit();
    ui::clip_begin(R0 + 1, py0 + 1, R1 - 1, py1 - 1);
    for (int i = 0; i < 3; ++i) {
        const float bx = R0 + 1 + bw * float(i);
        dl->AddRectFilled(ui::pg(bx, py0 + 1), ui::pg(bx + bw, py1 - 1), backs[i]);
    }
    for (int i = 0; i < 3; ++i) ui::draw_crosshair(dl, ui::pg(R0 + 1 + bw * (float(i) + 0.5f), (py0 + py1) * 0.5f), unit, c, open);
    ui::clip_end();
    note(R0, py1 + 3, R1, "The size it has in a match, on this screen.");
}

// ── Fidelity: there once it has been found ────────────────────────────────────

void fidelity_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    FidelitySettings& f = e.finish;
    ui::heading(L0, y, L1, "FIDELITY");
    float ly = y + 24;
    int on = e.fidelity ? 1 : 0;
    if (choice_row(9340, L0, ly, L1, "Fidelity", on, {"Off", "On"})) e.fidelity = on == 1;
    ly += kRow;
    const bool en = e.fidelity;
    // A whole look to start from; anything moved by hand makes it the player's own.
    const int look = fidelity_preset_of(f);
    label(L0, ly, "Look", en);
    if (const int d = ui::arrows(9341, L0 + 104, ly, L1, ly + 18, look >= 0 ? fidelity_preset_name(look) : "Your own", en); d != 0) {
        const int n = kFidelityPresets;
        FidelitySettings next = fidelity_preset(look < 0 ? (d > 0 ? 0 : n - 1) : (look + d + n) % n);
        next.render_scale = f.render_scale, next.dither = f.dither, next.smooth_edges = f.smooth_edges, next.exposure = f.exposure;
        f = next;
    }
    ly += kRow + 2;
    (void)choice_row(9342, L0, ly, L1, "Contact shadow", f.shade, {"Off", "Low", "Medium", "High"}, en);
    ly += kRow;
    slider_row(9343, L0, ly, L1, "  Strength", f.shade_strength, 0.0f, 1.5f, "%.2f", en && f.shade > 0);
    ly += kRow;
    slider_row(9344, L0, ly, L1, "  Reach", f.shade_radius, 16.0f, 160.0f, "%.0f cm", en && f.shade > 0);
    ly += kRow + 2;
    (void)ui::check(9360, L0, ly, "Lights on surfaces", f.lights, en,
                    "A muzzle flash, a grenade's blast and the map's lamps light what they are next to, and glint off tile, metal and glass");
    ly += kRow;
    slider_row(9361, L0, ly, L1, "  Strength", f.lights_strength, 0.0f, 2.0f, "%.2f", en && f.lights);
    ly += kRow;
    slider_row(9362, L0, ly, L1, "  Shine", f.shine, 0.0f, 2.0f, "%.2f", en && f.lights);
    ly += kRow + 2;
    (void)ui::check(9366, L0, ly, "Lamp coronas", f.coronas, en, "A corona at each of the map's lamps: a headlight, a spot, a bulb");
    (void)ui::check(9367, L0 + 150, ly, "Reflections", f.reflections, en, "What shines mirrors the picture: tiled floors, steel, water");
    ly += kRow;
    (void)ui::check(9368, L0, ly, "Water", f.water, en, "Water drawn as water: rippling, mirroring the sky above it");
    (void)ui::check(9369, L0 + 150, ly, "Clouds", f.clouds, en, "Cloud drifting over the map's own sky, in the map's own light");
    ly += kRow + 2;
    // A switch and its strength on one row.
    auto pair = [&](int key, const char* name, bool& on, float& v, float lo, float hi, const char* tip) {
        (void)ui::check(key, L0, ly, name, on, en, tip);
        (void)ui::trackbar(key + 1, L0 + 124, ly, L1, ly + 18, v, lo, hi, "%.2f", en && on);
        ly += kRow;
    };
    pair(9363, "Sun and moon", f.sun, f.corona, 0.0f, 1.5f,
         "The sun's disc where the map's light comes from (the moon, in tonight's phase, when the room's hour is night); the slider is its corona");
    pair(9348, "Light shafts", f.shafts, f.shafts_strength, 0.0f, 1.5f, "Sunlight past rooftops and through gaps, looking toward the map's own sun");
    ly += 2;
    note(L0, ly, L1, "A map's light and hour are always its own.");

    ui::heading(R0, y, R1, "PICTURE");
    float ry = y + 24;
    (void)choice_row(9350, R0, ry, R1, "Highlights", f.curve, {"As drawn", "Soft", "Filmic"}, en);
    ry += kRow;
    slider_row(9365, R0, ry, R1, "Exposure", f.exposure, 0.5f, 2.0f, "%.2f", en);
    ry += kRow;
    slider_row(9351, R0, ry, R1, "Saturation", f.saturation, 0.0f, 2.0f, "%.2f", en);
    ry += kRow;
    slider_row(9352, R0, ry, R1, "Contrast", f.contrast, 0.5f, 1.5f, "%.2f", en);
    ry += kRow;
    slider_row(9353, R0, ry, R1, "Sharpening", f.sharpen, 0.0f, 1.0f, "%.2f", en);
    ry += kRow;
    slider_row(9354, R0, ry, R1, "Vignette", f.vignette, 0.0f, 1.0f, "%.2f", en);
    ry += kRow + 2;
    (void)ui::check(9345, R0, ry, "Glow", f.glow, en, "A glow off the brightest light: the sky, a lamp, a muzzle flash");
    (void)ui::trackbar(9346, R0 + 124, ry, R1, ry + 18, f.glow_strength, 0.0f, 1.0f, "%.2f", en && f.glow);
    ry += kRow;
    slider_row(9347, R0, ry, R1, "  Threshold", f.glow_threshold, 0.4f, 1.2f, "%.2f", en && f.glow);
    ry += kRow + 2;
    (void)ui::check(9357, R0, ry, "Smooth edges", f.smooth_edges, en, "Edges softened after the picture is drawn (FXAA), on top of anti-aliasing");
    (void)ui::check(9358, R0 + 150, ry, "Smooth gradients", f.dither, en, "A fine dither, so a glow or a vignette does not band");
    ry += kRow + 2;
    static const float scales[] = {0.5f, 0.67f, 0.75f, 0.85f, 1.0f, 1.25f, 1.5f, 2.0f};
    int at = 4;
    for (int i = 0; i < 8; ++i)
        if (std::fabs(scales[i] - f.render_scale) < std::fabs(scales[at] - f.render_scale)) at = i;
    if (choice_row(9359, R0, ry, R1, "Render scale", at, {"50%", "67%", "75%", "85%", "100%", "125%", "150%", "200%"}, en)) f.render_scale = scales[at];
    ry += kRow;
    const int pw = std::clamp(int(std::lround(float(app.picture_width()) * f.render_scale)), 16, 8192);
    const int ph = std::clamp(int(std::lround(float(app.picture_height()) * f.render_scale)), 16, 8192);
    note(R0, ry, R1, eng::str::format("The match is drawn at %d x %d%s.", pw, ph, f.render_scale > 1.01f ? ", then made finer" : f.render_scale < 0.99f ? ", then enlarged" : ""));
    ry += 15;
    if (app.world()) note(R0, ry, R1, "What you change shows in the match behind this."), ry += 15;
    ry += 8;
    note(R0, ry, R1, "A light is drawn only while its source is in your");
    note(R0, ry + 15, R1, "own sight: Fidelity shows nothing the plain game");
    note(R0, ry + 30, R1, "would not.");
}

std::string pad_status(const eng::Gamepad& pad) {
    if (!pad.connected()) return "No controller connected (USB or Bluetooth).";
    switch (pad.kind()) {
        case eng::PadKind::DualSense: return "DualSense (PlayStation 5): connected.";
        case eng::PadKind::DualShock4: return "DualShock 4 (PlayStation 4): connected.";
        default: return "Xbox controller: connected.";
    }
}

// The pad as the game reads it right now: both sticks, both triggers, what is held. So a player
// can see their controller is the one being listened to before a match depends on it.
void pad_tester(const eng::Gamepad& pad, float x0, float y0, float x1) {
    const eng::PadKind kind = pad.connected() ? pad.kind() : eng::PadKind::Xbox;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const VanU32 rim = VAN_COL32(80, 80, 78, 255), lit = VAN_COL32(202, 228, 80, 255), dim = VAN_COL32(128, 128, 122, 255);
    auto stick = [&](float cx, float sx, float sy) {
        const float r = 17;
        ui::well(cx - r, y0, cx + r, y0 + 2 * r);
        const VanVec2 c = ui::pg(cx, y0 + r);
        dl->AddLine(ui::pg(cx - r + 3, y0 + r), ui::pg(cx + r - 3, y0 + r), rim);
        dl->AddLine(ui::pg(cx, y0 + 3), ui::pg(cx, y0 + 2 * r - 3), rim);
        const VanVec2 at = ui::pg(cx + sx * (r - 4), y0 + r - sy * (r - 4));
        dl->AddCircleFilled(at, ui::pgy(3.2f), pad.connected() ? lit : dim);
        (void)c;
    };
    auto trigger = [&](float x, float v, const char* name) {
        ui::well(x, y0, x + 9, y0 + 34);
        const float h = 32.0f * std::clamp(v, 0.0f, 1.0f);
        if (h > 0.5f) ui::fill_at(x + 1, y0 + 33 - h, x + 8, y0 + 33, lit);
        ui::text_at(x - 8, y0 + 35, x + 17, y0 + 48, name, dim, ui::Align::Center, false, 10.0f);
    };
    stick(x0 + 18, pad.lx(), pad.ly());
    stick(x0 + 62, pad.rx(), pad.ry());
    trigger(x0 + 92, pad.lt(), eng::Gamepad::button_label(eng::kPadLT, kind).c_str());
    trigger(x0 + 116, pad.rt(), eng::Gamepad::button_label(eng::kPadRT, kind).c_str());
    // What is held, by name.
    std::string held;
    for (eng::u32 b = 1; b <= eng::kPadRT; b <<= 1)
        if (pad.down(b) && b != eng::kPadLT && b != eng::kPadRT) held += (held.empty() ? "" : "  ") + eng::Gamepad::button_label(b, kind);
    ui::text_at(x0 + 142, y0, x1, y0 + 14, "Held now", dim, ui::Align::Left, false, 10.0f);
    ui::text_at(x0 + 142, y0 + 14, x1, y0 + 30, !pad.connected() ? "-" : held.empty() ? "nothing" : held, pad.connected() && !held.empty() ? lit : dim, ui::Align::Left,
                true, 11.0f);
    ui::text_at(x0, y0 + 35, x0 + 80, y0 + 48, "sticks", dim, ui::Align::Center, false, 10.0f);
}

void controller_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    PadSettings& p = e.pad;
    const eng::Gamepad& pad = app.pad();
    const eng::PadKind kind = pad.connected() ? pad.kind() : eng::PadKind::Xbox;
    ui::heading(L0, y, L1, "CONTROLLER");
    float ly = y + 22;
    ui::text_at(L0, ly, L1, ly + 16, pad_status(pad), pad.connected() ? VAN_COL32(150, 210, 110, 255) : kOptDim, ui::Align::Left, true, 11.0f);
    ly += 18;
    (void)ui::check(9320, L0, ly, "Play with a controller", p.enabled);
    (void)ui::check(9321, L0 + 170, ly, "Vibration", p.vibration, p.enabled);
    ly += kRow;
    label(L0, ly, "Look speed", p.enabled);
    (void)ui::trackbar(9322, L0 + 104, ly, L1, ly + 18, p.look, 0.2f, 3.0f, "%.2f", p.enabled);
    ly += kRow;
    label(L0, ly, "Up and down", p.enabled);
    (void)ui::trackbar(9323, L0 + 104, ly, L1, ly + 18, p.look_vertical, 0.3f, 1.5f, "%.2f", p.enabled);
    ly += kRow;
    label(L0, ly, "Dead zone, move", p.enabled);
    (void)ui::trackbar(9324, L0 + 104, ly, L1, ly + 18, p.dead_move, 0.0f, 0.4f, "%.2f", p.enabled);
    ly += kRow;
    label(L0, ly, "Dead zone, look", p.enabled);
    (void)ui::trackbar(9325, L0 + 104, ly, L1, ly + 18, p.dead_look, 0.0f, 0.4f, "%.2f", p.enabled);
    ly += kRow;
    (void)ui::check(9326, L0, ly, "Invert up and down", p.invert_y, p.enabled);
    (void)ui::check(9327, L0 + 170, ly, "Smooth stick", p.smooth, p.enabled, "Fine near the centre, full at the edge");
    ly += kRow;
    (void)ui::check(9328, L0, ly, "Swap the sticks", p.swap_sticks, p.enabled, "The right stick moves, the left one looks");
    ly += kRow + 2;
    const std::string a = eng::Gamepad::button_label(eng::kPadA, kind), b = eng::Gamepad::button_label(eng::kPadB, kind);
    const std::string x = eng::Gamepad::button_label(eng::kPadX, kind), start = eng::Gamepad::button_label(eng::kPadStart, kind);
    note(L0, ly, L1, p.swap_sticks ? "The right stick moves, the left stick looks." : "The left stick moves, the right stick looks.");
    note(L0, ly + 15, L1, eng::str::format("%s opens the menu. In menus the left stick is the", start.c_str()));
    note(L0, ly + 30, L1, eng::str::format("pointer: %s clicks, %s goes back, %s is the right", a.c_str(), b.c_str(), x.c_str()));
    note(L0, ly + 45, L1, "button, the right stick scrolls. No aim assist.");
    note(L0, ly + 60, L1, "Xbox, DualShock 4 and DualSense pads, by cable or Bluetooth.");
    pad_tester(pad, L0, ly + 82, L1);

    ui::heading(R0, y, R1, "BUTTONS");
    float ry = y + 21;
    for (Action act : {Action::Jump, Action::Knee, Action::Walk, Action::Shoot, Action::Zoom, Action::Reload, Action::Use, Action::LastWeapon,
                       Action::NextWeapon, Action::PrevWeapon, Action::Weapon1, Action::Weapon2, Action::Weapon3, Action::Weapon4, Action::CameraMode,
                       Action::ScoreView, Action::RadioCommand, Action::RadioGeneral, Action::RadioReply, Action::Spray})
        pad_row(app, R0, ry, R1, act, p.enabled), ry += 17;
    note(R0, ry + 2, R1, "Click a plate, press a button. Backspace: none.");
}

// The touch screen in a match (a phone; World/TouchControls.hpp). Shown on Android only.
[[maybe_unused]] void touch_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    TouchSettings& t = e.touch;
    ui::heading(L0, y, L1, "TOUCH");
    float ly = y + 22;
    (void)ui::check(9370, L0, ly, "Touch controls in a match", t.enabled);
    ly += kRow;
    label(L0, ly, "Size", t.enabled);
    (void)ui::trackbar(9371, L0 + 104, ly, L1, ly + 18, t.size, 0.7f, 1.4f, "%.2f", t.enabled);
    ly += kRow;
    label(L0, ly, "Look speed", t.enabled);
    (void)ui::trackbar(9372, L0 + 104, ly, L1, ly + 18, t.look, 0.3f, 3.0f, "%.2f", t.enabled);
    ly += kRow;
    label(L0, ly, "Opacity", t.enabled);
    (void)ui::trackbar(9373, L0 + 104, ly, L1, ly + 18, t.opacity, 0.2f, 1.0f, "%.2f", t.enabled);
    ly += kRow;
    (void)ui::check(9374, L0, ly, "A second Fire for the left thumb", t.left_fire, t.enabled);
    ly += kRow + 2;
    // Each control where you want it, at the size you want it (the screen itself, the options aside).
    if (ui::text_button(9375, L0, ly, L0 + 200, ly + 28, "Arrange on the screen", t.enabled, "Drag each control where you want it, and size it"))
        app.state().touch_arrange = true;
    int moved = 0;
    for (const TouchPlace& p : t.places) moved += p.moved() || p.scale != 1.0f ? 1 : 0;
    ui::text_at(L0 + 210, ly + 6, L1, ly + 22, moved ? eng::str::format("%d moved or sized", moved) : std::string("As they come"), kOptDim, ui::Align::Left,
                true, 11.0f);
    ly += 36;
    note(L0, ly, L1, "The left thumb moves: the stick comes to where it lands.");
    note(L0, ly + 15, L1, "Pushed part way it walks, quietly; all the way it runs.");
    note(L0, ly + 30, L1, "Swipe anywhere else to look. Fire looks too while held.");
    note(L0, ly + 45, L1, "Up and down as the mouse's: Keys, Invert the mouse.");
    note(L0, ly + 60, L1, "A controller or a keyboard puts them away; a touch");
    note(L0, ly + 75, L1, "brings them back. No aim assist.");

    ui::heading(R0, y, R1, "THE BUTTONS");
    float ry = y + 22;
    for (const char* line : {"FIRE: shoot (held), and look while held", "AIM: the scope, on a gun that has one", "JUMP", "CROUCH: tap on, tap off",
                             "RELOAD", "SWAP: the next weapon you carry", "USE: plant, defuse, open (held)", "MENU, CHAT and SCORE along the top",
                             "Back (the phone's): the menu too"})
        note(R0, ry, R1, line), ry += 17;
}

void keys_page(App& app, Settings& e, float L0, float L1, float R0, float R1, float y) {
    // The original's own actions and defaults (data/config.cfg); a plate is clicked to change it.
    option_heading("move_text", L0, y, L1);
    float ly = y + 22;
    for (Action a : {Action::Go, Action::Back, Action::StrafeLeft, Action::StrafeRight, Action::Jump, Action::Knee, Action::Walk})
        bind_row(app, L0, ly, L1, a), ly += 19;
    ly += 8;
    option_heading("weapon_text", L0, ly, L1);
    ly += 22;
    for (Action a : {Action::Shoot, Action::Zoom, Action::Reload, Action::Use, Action::Spray, Action::LastWeapon, Action::NextWeapon, Action::PrevWeapon})
        bind_row(app, L0, ly, L1, a), ly += 19;
    note(L0, ly + 4, L1, "The wheel also changes weapon. Backspace: no key.");

    option_heading("view_text", R0, y, R1);
    float ry = y + 22;
    ui::text_at(R0, ry, R0 + 100, ry + 18, "Mouse speed", kOptLabel, ui::Align::Left, true);
    (void)ui::trackbar(9310, R0 + 104, ry, R1, ry + 18, e.sensitivity, 0.1f, 5.0f, "%.2f");
    hint(R0, ry, R1, "How far the aim turns for the mouse's travel. Raw input: Windows' pointer speed has no say.");
    ry += 22;
    (void)ui::check(9311, R0, ry, "Invert the mouse", e.invert_mouse, true, "The mouse forward looks down.");
    ry += 24;
    for (Action a : {Action::Weapon1, Action::Weapon2, Action::Weapon3, Action::Weapon4, Action::CameraMode, Action::ScoreView, Action::RadioCommand,
                     Action::RadioGeneral, Action::RadioReply})
        bind_row(app, R0, ry, R1, a), ry += 19;
    const std::pair<const char*, const char*> fixed[] = {{"Talk (to the team)", "Enter (Y)"}, {"Chat macro", "Alt + 1 - 0"}, {"Menu", "Esc"}};
    for (const auto& [a, k] : fixed) key_row(R0, ry, R1, a, k), ry += 19;
}

// The match's other keys: the original's floor-gun keys (config.cfg WEAPON_DROP, ACQUIRE_WEAPON), its
// H, B and O, and turning and looking by key (ROTATE_*, LOOK_*, CENTER_VIEW).
void match_keys_page(App& app, float L0, float L1, float R0, float R1, float y) {
    ui::heading(L0, y, L1, "GUNS ON THE FLOOR");
    float ly = y + 22;
    for (Action a : {Action::DropWeapon, Action::PickUpWeapon}) bind_row(app, L0, ly, L1, a), ly += 19;
    note(L0, ly + 4, L1, "Drop on Last weapon's key (the original's F): a tap");
    note(L0, ly + 19, L1, "is the last weapon, a hold puts the gun in hand down.");
    ly += 52;
    ui::heading(L0, ly, L1, "THE SCREEN");
    ly += 22;
    for (Action a : {Action::Objective, Action::HideHud, Action::Blood}) bind_row(app, L0, ly, L1, a), ly += 19;
    ui::heading(R0, y, R1, "LOOKING BY KEY");
    float ry = y + 22;
    for (Action a : {Action::TurnLeft, Action::TurnRight, Action::LookUp, Action::LookDown, Action::CenterView}) bind_row(app, R0, ry, R1, a), ry += 19;
    note(R0, ry + 4, R1, "Turning by key goes with the mouse, not instead of it.");
    ry += 34;
    ui::heading(R0, ry, R1, "HORROR MODE'S ITEMS");
    ry += 22;
    for (Action a : {Action::Item1, Action::Item2, Action::Item3}) bind_row(app, R0, ry, R1, a), ry += 19;
    note(R0, ry + 4, R1, "A human's: Rescue Kit, Silver Bullet, Blind Cleanse.");
    note(R0, ry + 19, R1, "An undead's: Blood Sucking, Shout of Anger, Speed Up.");
}

void radio_page(float L0, float R1, float y) {
    // The radio: Z, X and C open their lists, the number keys pick a line.
    const float col_w = (R1 - L0) / 3.0f;
    for (u8 g = 0; g < 3; ++g) {
        const float cx0 = L0 + col_w * float(g), cx1 = cx0 + col_w - 12;
        ui::text_at(cx0, y, cx1, y + 18, eng::str::format("%c  %s", "ZXC"[g], radio_group_name(g)), kKeyInk, ui::Align::Left, true, 13.0f);
        ui::fill_at(cx0, y + 20, cx1, y + 21, kRule);
        float ly = y + 26;
        for (int i = 0; i < radio_count(g); ++i) {
            ui::text_at(cx0, ly, cx0 + 18, ly + 18, eng::str::format("%d.", i + 1), kOptDim, ui::Align::Left, true);
            ui::text_at(cx0 + 18, ly, cx1, ly + 18, radio_line(g, u8(i)), kOptLabel, ui::Align::Left, true);
            ly += 20;
        }
    }
}

// Default: the tab in front put back as the game first has it.
void restore_defaults(Settings& e, int tab) {
    const Settings d;
    if (tab == 0) {
        e.renderer = d.renderer, e.borderless = d.borderless, e.width = d.width, e.height = d.height;
        e.full_width = d.full_width, e.full_height = d.full_height, e.aspect = d.aspect, e.scaling = d.scaling;
        e.vsync = d.vsync, e.max_fps = d.max_fps, e.brightness = d.brightness, e.fov = d.fov;
        e.antialiasing = d.antialiasing, e.texture_filter = d.texture_filter, e.fidelity = false, e.finish = d.finish;
        e.bullet_marks = d.bullet_marks, e.spent_cases = d.spent_cases, e.blood = d.blood, e.impact_dust = d.impact_dust, e.step_effects = d.step_effects,
        e.shadows = d.shadows;
        e.hide_weapon = d.hide_weapon, e.always_run = d.always_run, e.auto_switch = d.auto_switch, e.crosshair = d.crosshair;
        e.scope_hold = d.scope_hold, e.scope_speed = d.scope_speed, e.viewmodel_fov = d.viewmodel_fov;
        e.master_volume = d.master_volume, e.effects_volume = d.effects_volume, e.music_volume = d.music_volume, e.ui_volume = d.ui_volume;
        e.lobby_music = d.lobby_music, e.show_fps = d.show_fps, e.show_netgraph = d.show_netgraph, e.damage_numbers = d.damage_numbers, e.sharp_ui = d.sharp_ui;
        e.radio_voice = d.radio_voice;
    } else if (tab == 1) {
        e.binds = d.binds, e.sensitivity = d.sensitivity, e.invert_mouse = d.invert_mouse, e.pad = d.pad, e.touch = d.touch;
    } else {
        e.macros = d.macros;
        e.global_chat = d.global_chat;
    }
}

}  // namespace

void open_settings(App& app, int tab, int sub) {
    ScreenState& st = app.state();
    st.edit = app.settings();
    if (tab >= 0) st.settings_tab = tab;
    if (sub >= 0) (st.settings_tab == 0 ? st.system_sub : st.settings_sub) = sub;
    st.binding = st.pad_binding = -1;
    st.fidelity_taps = 0;
    st.settings_open = true;
}

// The touch controls' arranging screen: the whole screen, with the options set aside under it (they
// come back as they were when it closes). What is arranged is the options' own copy: OK keeps it.
void touch_arrange_modal(App& app) {
    ScreenState& st = app.state();
    const VanVec2 ds = VanGui::GetIO().DisplaySize;
    constexpr const char* kId = "##touch_arrange";
    if (!VanGui::IsPopupOpen(kId)) VanGui::OpenPopup(kId);
    VanGui::SetNextWindowPos({0, 0});
    VanGui::SetNextWindowSize(ds);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    const bool shown = VanGui::BeginPopupModal(kId, nullptr,
                                               VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoMove |
                                                   VanGuiWindowFlags_NoSavedSettings | VanGuiWindowFlags_NoScrollWithMouse | VanGuiWindowFlags_NoNav);
    VanGui::PopStyleVar(2);
    if (!shown) return;
    TouchControls::Screen screen;
    screen.w = ds.x, screen.h = ds.y;
#ifndef _WIN32
    for (int i = 0; i < 4; ++i) screen.inset[i] = float(app.window().safe_inset[i]);
#endif
    if (!TouchControls::arrange(st.edit.touch, screen, st.touch_picked)) {
        st.touch_arrange = false;
        VanGui::CloseCurrentPopup();
    }
    VanGui::EndPopup();
}

void settings_modal(App& app) {
    ScreenState& st = app.state();
    constexpr float X0 = 182, Y0 = 126, X1 = 842, Y1 = 646;
    // Esc while a key or a button is being set leaves that, not the dialog.
    if (st.binding >= 0 || st.pad_binding >= 0) ui::dialog_keep_on_escape();
    if (!ui::dialog_begin("Settings", X0, Y0, X1, Y1, "", &st.settings_open)) {
        st.settings_open = false;
        return;
    }
    Settings& e = st.edit;
    ui::sprite_at("title_option", 0, 1, X0 + 18, Y0 + 6, X0 + 18 + 86, Y0 + 19);
    // What the control under the pointer is for: said along the bottom, not in a tooltip.
    static std::string about;
    about.clear();
    ui::set_tip_sink(&about);

    // The three tabs: the kit's two-state plates (the lit word when chosen).
    const char* tab_sprites[] = {"system", "controls", "macro"};
    for (int t = 0; t < 3; ++t) {
        const float tx = X0 + 12 + 86.0f * float(t), ty = Y0 + 32;
        bool hovered = false;
        if (ui::region(9100 + t, tx, ty, tx + 82, ty + 30, &hovered) && st.settings_tab != t) {
            st.settings_tab = t;
            st.binding = st.pad_binding = -1;
            ui::click_sound();
        }
        ui::sprite_at(tab_sprites[t], st.settings_tab == t ? 1 : 0, 2, tx, ty, tx + 82, ty + 30,
                      st.settings_tab == t || hovered ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 190));
    }
    ui::fill_at(X0 + 10, Y0 + 63, X1 - 10, Y0 + 64, kRule);

    const float L0 = X0 + 16, L1 = X0 + 318, R0 = X0 + 342, R1 = X1 - 16;
    float y = Y0 + 74;
    if (st.settings_tab == 0) {
        // ── System: Display, Graphics, Sound, Game, Crosshair; and Fidelity, once it has been found ──
        static const char* subs[] = {"Display", "Graphics", "Sound", "Game", "Crosshair", "Fidelity"};
        const int count = e.fidelity_found ? 6 : 5;
        st.system_sub = std::clamp(st.system_sub, 0, count - 1);
        for (int t = 0; t < count; ++t) {
            const float tx = L0 + 86.0f * float(t);
            if (ui::tab_button(9110 + t, tx, y, tx + 82, y + 26, subs[t], st.system_sub == t) && st.system_sub != t) st.system_sub = t, st.fidelity_taps = 0;
        }
        y += 38;
        switch (st.system_sub) {
            case 0: display_page(app, e, L0, L1, R0, R1, y); break;
            case 1: graphics_page(app, e, L0, L1, R0, R1, y); break;
            case 2: sound_page(app, e, L0, L1, R0, R1, y); break;
            case 3: game_page(app, e, L0, L1, R0, R1, y); break;
            case 4: crosshair_page(app, e, L0, L1, R0, R1, y); break;
            default: fidelity_page(app, e, L0, L1, R0, R1, y); break;
        }
    } else if (st.settings_tab == 1) {
        // ── Controls: the keys, the controller, the radio's lines (and a phone's touch screen) ──
#ifdef __ANDROID__
        constexpr int kSubs = 5;
#else
        constexpr int kSubs = 4;
#endif
        st.settings_sub = std::clamp(st.settings_sub, 0, kSubs - 1);
        auto pick = [&](int t) {
            if (st.settings_sub == t) return;
            st.settings_sub = t;
            st.binding = st.pad_binding = -1;
            ui::click_sound();
        };
        struct Sub {
            const char* sprite;   // the kit's plate, or none: a plate with the word on it
            const char* word;
        };
        static const Sub subs[] = {{"method_key", ""}, {nullptr, "Controller"}, {"method_radio", ""}, {nullptr, "Match"}, {nullptr, "Touch"}};
        for (int t = 0; t < kSubs; ++t) {
            const float tx = L0 + 86.0f * float(t);
            if (!subs[t].sprite) {
                if (ui::tab_button(9300 + t, tx, y, tx + 82, y + 30, subs[t].word, st.settings_sub == t)) pick(t);
                continue;
            }
            bool hovered = false;
            if (ui::region(9300 + t, tx, y, tx + 82, y + 30, &hovered)) pick(t);
            ui::sprite_at(subs[t].sprite, st.settings_sub == t ? 1 : 0, 2, tx, y, tx + 82, y + 30,
                          st.settings_sub == t || hovered ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 190));
        }
        y += 40;
        if (st.settings_sub == 0) keys_page(app, e, L0, L1, R0, R1, y);
        else if (st.settings_sub == 1) controller_page(app, e, L0, L1, R0, R1, y);
        else if (st.settings_sub == 2) radio_page(L0, R1, y);
        else if (st.settings_sub == 3) match_keys_page(app, L0, L1, R0, R1, y);
        else touch_page(app, e, L0, L1, R0, R1, y);
    } else {
        // ── Macro: ten lines of chat ──
        option_heading("macro_text", L0, y, R1);
        ui::text_at(L0, y + 20, R1, y + 38, "In a match, hold Alt and press a number: the line is said to everyone.", kOptDim, ui::Align::Left, true, 11.0f);
        float ly = y + 44;
        for (int i = 0; i < 10; ++i) {
            const int key = (i + 1) % 10;
            const float cx0 = i < 5 ? L0 : R0, cx1 = i < 5 ? L1 : R1;
            const float ry = ly + 26.0f * float(i % 5);
            ui::sprite_at("bg_gray_1", 0, 1, cx0, ry + 1, cx0 + 50, ry + 19);
            ui::text_at(cx0, ry + 1, cx0 + 50, ry + 19, eng::str::format("Alt + %d", key), kKeyInk, ui::Align::Center, true, 11.0f);
            (void)ui::edit_at(9400 + i, cx0 + 56, ry, cx1, ry + 20, e.macros[size_t(i)], 60, "a line of chat");
        }
        // ── Chat: global on or off, and the commands every chat box takes ──
        const float cy = ly + 26.0f * 5 + 18;
        ui::heading(L0, cy, L1, "CHAT");
        (void)ui::check(9420, L0, cy + 24, "Global chat", e.global_chat, true,
                        "One channel for everyone signed in, heard in the lobbies, the rooms and the matches. Off: you neither hear nor speak on it.");
        ui::text_at(L0, cy + 50, L1, cy + 66, "Staff can take a soldier off global chat alone.", kOptDim, ui::Align::Left, true, 11.0f);
        ui::heading(R0, cy, R1, "CHAT COMMANDS");
        const char* commands[] = {"/w name text   whisper (left in their inbox if away)", "/r text   answer the last whisper",
                                  "/c text   your clan, wherever they are", "/g text   global chat", "/t text   your team, in a match"};
        for (int i = 0; i < 5; ++i)
            ui::text_at(R0, cy + 24 + 17.0f * float(i), R1, cy + 40 + 17.0f * float(i), commands[i], kOptDim, ui::Align::Left, true, 11.0f);
    }

    // Default, Credits / Confirm, Cancel.
    constexpr float BY = Y1 - 50;
    if (ui::kit_button(9500, "baseValue_1", X0 + 14, BY, X0 + 87, BY + 41, true, "Put this tab's settings back to their defaults")) {
        restore_defaults(e, st.settings_tab);
        if (st.settings_tab == 0) settle_size(app, e);
        st.binding = st.pad_binding = -1;
    }
    if (ui::text_button(9503, X0 + 96, BY + 8, X0 + 170, BY + 34, "Credits", true, "Who made Soldier Front, and who made this")) {
        st.credits_open = true;
        st.credits_since = app.now();
        st.credits_scroll = 0;
        st.credits_by_hand = false;
    }
    const bool confirm = ui::kit_button(9501, "confirm_1", X1 - 170, BY, X1 - 97, BY + 41);
    const bool cancel = ui::kit_button(9502, "cancel_1", X1 - 87, BY, X1 - 14, BY + 41);
    {
        const float ax0 = X0 + 182, ax1 = X1 - 182;
        ui::fill_at(ax0, BY + 2, ax1, BY + 39, VAN_COL32(0, 0, 0, 70));
        ui::fill_at(ax0, BY + 2, ax0 + 2, BY + 39, about.empty() ? kRule : VAN_COL32(202, 228, 80, 255));
        if (about.empty()) ui::text_at(ax0 + 8, BY + 2, ax1, BY + 39, "Point at a setting to read what it does.", kOptDim, ui::Align::Left, true, 11.0f);
        else ui::text_wrapped(ax0 + 6, BY + 4, ax1 - 2, BY + 39, about, kOptLabel, 11.0f);
    }
    ui::set_tip_sink(nullptr);
    if (confirm) {
        const bool renderer_changed = e.renderer != app.settings().renderer;
        app.settings() = e;
        app.save_settings();
        app.apply_display();
        app.session().set_global_chat(e.global_chat);
        // A renderer is chosen when the game starts. Out of a match the game can start itself again.
        if (renderer_changed && !app.world()) st.restart_ask = true;
        else toast(Toast::Good, "Options saved.%s", renderer_changed ? " The new renderer is used from the next start." : "");
        st.settings_open = false;
        ui::dialog_close();
    }
    if (cancel || !st.settings_open) {
        st.settings_open = false;
        if (cancel) ui::dialog_close();
    }
    // The credits take the dialog's place and hand it back (what was being set is kept).
    if (st.credits_open) {
        st.settings_open = true;
        ui::dialog_close();
    }
    // Whatever happened, the mixer ends on the kept volumes, and no key is left half set.
    if (!st.settings_open) app.sounds().apply(app.settings()), st.binding = st.pad_binding = -1;
    ui::dialog_end();
}

// A new renderer was confirmed: the game starts itself again with it, or keeps going until the
// player next starts it.
void restart_modal(App& app) {
    ScreenState& st = app.state();
    constexpr float X0 = 302, Y0 = 290, X1 = 722, Y1 = 462;
    if (!ui::dialog_begin("Restart", X0, Y0, X1, Y1, "RENDERER", &st.restart_ask)) {
        st.restart_ask = false;
        return;
    }
    const char* name = Settings::renderer_name(app.settings().renderer);
    ui::text_at(X0 + 18, Y0 + 38, X1 - 18, Y0 + 56, eng::str::format("%s is used from the next time the game starts.", name), kOptLabel, ui::Align::Left, true);
    ui::text_at(X0 + 18, Y0 + 58, X1 - 18, Y0 + 76, "Restart now? You will sign in again afterwards.", kOptLabel, ui::Align::Left, true);
    if (app.settings().renderer != Settings::Renderer::Auto)
        ui::text_at(X0 + 18, Y0 + 82, X1 - 18, Y0 + 98, "If this card cannot run it, the game falls back to one it can.", kOptDim, ui::Align::Left, true, 11.0f);
    const float by = Y1 - 46;
    if (ui::text_button(9510, X1 - 250, by, X1 - 138, by + 30, "Restart now")) {
        st.restart_ask = false;
        ui::dialog_close();
        app.restart();
    }
    if (ui::text_button(9511, X1 - 128, by, X1 - 16, by + 30, "Later")) {
        st.restart_ask = false;
        ui::dialog_close();
        toast(Toast::Good, "Options saved. The new renderer is used from the next start.");
    }
    ui::dialog_end();
}

}  // namespace lsf
