#include "Game/Settings.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Gamepad.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Platform/Keys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace lsf {

namespace {

constexpr unsigned kMouse1 = 0x100, kMouse2 = 0x101;

// The original's defaults, from its data/config.cfg. The pad's are ours (the original had none):
// the triggers fire and scope, the face buttons jump, crouch, reload and change weapon, the d-pad
// is the radio, as shooters on a pad have long had them.
const ActionInfo kActions[] = {
    {"go", "GO", "Forward", 'W', 0},
    {"back", "BACK", "Back", 'S', 0},
    {"strafe_left", "STRAFT_LEFT", "Left", 'A', 0},
    {"strafe_right", "STRAFT_RIGHT", "Right", 'D', 0},
    {"jump", "JUMP", "Jump", VK_SPACE, eng::kPadA},
    {"knee", "KNEE", "Crouch", VK_LSHIFT, eng::kPadB},
    {"walk", "WALK", "Walk", VK_LCONTROL, eng::kPadLThumb},
    {"shoot", "SHOOT", "Fire", kMouse1, eng::kPadRT},
    {"zoom", "ZOOM_VIEW", "Scope", kMouse2, eng::kPadLT},
    {"reload", "RELOAD", "Reload", 'R', eng::kPadX},
    {"last_weapon", "LAST_USED_WEAPON", "Last weapon", 'F', eng::kPadRB},
    {"weapon_1", "WEAPON_1", "Primary", '1', 0},
    {"weapon_2", "WEAPON_2", "Sidearm", '2', 0},
    {"weapon_3", "WEAPON_3", "Melee", '3', eng::kPadRThumb},
    {"weapon_4", "WEAPON_4", "Throwing", '4', eng::kPadLB},
    {"camera", "CAMERA_MODE", "3rd person (room allowing)", 'Q', eng::kPadDown},
    {"scores", "SCORE_VIEW", "Scores", VK_TAB, eng::kPadBack},
    {"radio_command", "RADIOMESSAGE_COMMAND", "Radio: command", 'Z', eng::kPadLeft},
    {"radio_general", "RADIOMESSAGE_GENERAL", "Radio: general", 'X', eng::kPadUp},
    {"radio_reply", "RADIOMESSAGE_REPLY", "Radio: reply", 'C', eng::kPadRight},
    {"next_weapon", "NEXT_WEAPON", "Next weapon", 0, eng::kPadY},
    {"prev_weapon", "PREV_WEAPON", "Previous weapon", 0, 0},
    // On a pad it shares X with Reload: tapped it reloads, held at a bomb site it plants.
    {"use", "USE", "Use (plant, defuse, open)", 'E', eng::kPadX},
    {"spray", "SPRAY", "Spray", 'T', 0},
    {"drop_weapon", "WEAPON_DROP", "Drop the weapon (hold)", 'F', 0},
    {"pick_up", "ACQUIRE_WEAPON", "Pick up a weapon", 'G', 0},
    {"objective", "OBJECTIVE", "The mission (hold)", 'O', 0},
    {"hide_hud", "HIDE_HUD", "Hide the HUD", 'H', 0},
    {"blood", "BLOOD", "Blood on / off", 'B', 0},
    {"center_view", "CENTER_VIEW", "Level the view", VK_END, 0},
    {"turn_left", "ROTATE_LEFT", "Turn left", VK_LEFT, 0},
    {"turn_right", "ROTATE_RIGHT", "Turn right", VK_RIGHT, 0},
    {"look_up", "LOOK_UP", "Look up", VK_UP, 0},
    {"look_down", "LOOK_DOWN", "Look down", VK_DOWN, 0},
    {"item_1", "ITEM_1", "Horror item 1", '5', 0},
    {"item_2", "ITEM_2", "Horror item 2", '6', 0},
    {"item_3", "ITEM_3", "Horror item 3", '7', 0},
};
static_assert(std::size(kActions) == size_t(Action::Count));

const AspectInfo kAspects[kAspectCount] = {
    {"auto", "Any", 0, 0}, {"4:3", "4:3", 4, 3}, {"5:4", "5:4", 5, 4}, {"16:10", "16:10", 16, 10}, {"16:9", "16:9", 16, 9}, {"21:9", "21:9", 21, 9},
};

std::filesystem::path g_file;

// The nearest of the values a setting takes.
int snap(int v, std::initializer_list<int> allowed) {
    int best = *allowed.begin();
    for (const int a : allowed)
        if (std::abs(a - v) < std::abs(best - v)) best = a;
    return best;
}

}  // namespace

const ActionInfo& action_info(Action a) { return kActions[std::clamp(int(a), 0, int(Action::Count) - 1)]; }

namespace {
struct TouchPartName {
    const char* key;
    const char* label;
};
constexpr TouchPartName kTouchPartNames[kTouchParts] = {
    {"fire", "Fire"},     {"left_fire", "Second Fire"}, {"aim", "Aim"},     {"jump", "Jump"},   {"crouch", "Crouch"}, {"reload", "Reload"},
    {"swap", "Swap"},     {"use", "Use"},               {"menu", "Menu"},   {"chat", "Chat"},   {"score", "Score"},   {"stick", "The stick"},
};
}  // namespace

const char* touch_part_key(TouchPart p) { return kTouchPartNames[std::clamp(size_t(p), size_t(0), kTouchParts - 1)].key; }
const char* touch_part_label(TouchPart p) { return kTouchPartNames[std::clamp(size_t(p), size_t(0), kTouchParts - 1)].label; }

Binds default_binds() {
    Binds b{};
    for (size_t i = 0; i < b.size(); ++i) b[i] = kActions[i].default_code;
    return b;
}

Binds default_pad_binds() {
    Binds b{};
    for (size_t i = 0; i < b.size(); ++i) b[i] = kActions[i].default_pad;
    return b;
}

const AspectInfo& aspect_info(int index) { return kAspects[std::clamp(index, 0, kAspectCount - 1)]; }

int aspect_of(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    const float shape = float(width) / float(height);
    // 21:9 screens are sold as 2.33 to 2.4; 1366x768 is 16:9 to a pixel or two.
    for (int i = 1; i < kAspectCount; ++i) {
        const float want = float(kAspects[i].w) / float(kAspects[i].h);
        if (std::fabs(shape - want) < (i == 5 ? 0.09f : 0.02f)) return i;
    }
    return 0;
}

namespace {

// name, then: ticks, upper tick, length, thickness, gap, dot, its size, ring, its size.
struct CrosshairShape {
    const char* name;
    bool lines, top;
    float length, thickness, gap;
    bool dot;
    float dot_size;
    bool ring;
    float ring_size;
};
const CrosshairShape kCrosshairShapes[kCrosshairPresets] = {
    {"Classic", true, true, 9, 2, 3, false, 1.8f, false, 4},
    {"Classic with dot", true, true, 9, 2, 3, true, 1.8f, false, 4},
    {"Circle", false, true, 9, 2, 3, true, 1.4f, true, 4},
    {"Cross", true, true, 14, 2, 0, false, 1.8f, false, 4},
    {"Dot", false, true, 9, 2, 3, true, 2.4f, false, 4},
    {"T", true, false, 9, 2, 3, false, 1.8f, false, 4},
    {"Fine", true, true, 6, 1, 2, false, 1.2f, false, 4},
};
const CrosshairInk kCrosshairInkTable[kCrosshairInks] = {
    {"Green", 120, 255, 90}, {"Red", 255, 72, 60},   {"Yellow", 255, 228, 80},  {"Cyan", 90, 228, 255},
    {"White", 245, 245, 245}, {"Pink", 255, 96, 208}, {"Orange", 255, 150, 40}, {"Blue", 96, 140, 255},
};

struct FidelityLook {
    const char* name;
    FidelitySettings f;
};
FidelityLook fidelity_look(int index) {
    FidelitySettings f;
    switch (index) {
        case 1:
            f.shade = 3, f.shade_strength = 0.8f, f.shade_radius = 64;
            f.glow_strength = 0.35f, f.glow_threshold = 0.72f;
            f.shafts = true, f.shafts_strength = 0.6f;
            f.lights_strength = 1.3f, f.shine = 1.4f, f.corona = 0.9f;
            f.saturation = 1.10f, f.contrast = 1.06f, f.sharpen = 0.35f, f.vignette = 0.15f;
            return {"Rich", f};
        case 2:
            f.shade = 3, f.shade_strength = 0.9f, f.shade_radius = 72;
            f.glow_strength = 0.45f, f.glow_threshold = 0.68f;
            f.shafts = true, f.shafts_strength = 0.8f;
            f.lights_strength = 1.5f, f.shine = 1.6f, f.corona = 1.2f;
            f.curve = 2, f.saturation = 0.94f, f.contrast = 1.10f, f.sharpen = 0.2f;
            f.vignette = 0.35f;
            return {"Cinematic", f};
        case 3:
            f.shade = 0, f.glow = false, f.lights = false, f.sun = false, f.curve = 0, f.saturation = 1, f.contrast = 1, f.vignette = 0;
            f.sharpen = 0.3f;
            return {"Clean", f};
        default: return {"Subtle", f};
    }
}

}  // namespace

const char* crosshair_preset_name(int index) { return kCrosshairShapes[std::clamp(index, 0, kCrosshairPresets - 1)].name; }

Crosshair crosshair_preset(int index, const Crosshair& colour_of) {
    const CrosshairShape& s = kCrosshairShapes[std::clamp(index, 0, kCrosshairPresets - 1)];
    Crosshair c = colour_of;
    c.lines = s.lines, c.top = s.top, c.length = s.length, c.thickness = s.thickness, c.gap = s.gap;
    c.dot = s.dot, c.dot_size = s.dot_size, c.ring = s.ring, c.ring_size = s.ring_size;
    return c;
}

int crosshair_preset_of(const Crosshair& c) {
    for (int i = 0; i < kCrosshairPresets; ++i) {
        const Crosshair p = crosshair_preset(i, c);
        // What a shape does not draw is not part of it.
        if (p.lines != c.lines || p.dot != c.dot || p.ring != c.ring) continue;
        if (c.lines && (p.top != c.top || p.length != c.length)) continue;
        if ((c.lines || c.ring) && (p.thickness != c.thickness || p.gap != c.gap)) continue;
        if (c.dot && p.dot_size != c.dot_size) continue;
        if (c.ring && p.ring_size != c.ring_size) continue;
        return i;
    }
    return -1;
}

const CrosshairInk& crosshair_ink(int index) { return kCrosshairInkTable[std::clamp(index, 0, kCrosshairInks - 1)]; }

const char* fidelity_preset_name(int index) { return fidelity_look(std::clamp(index, 0, kFidelityPresets - 1)).name; }
FidelitySettings fidelity_preset(int index) { return fidelity_look(std::clamp(index, 0, kFidelityPresets - 1)).f; }
int fidelity_preset_of(const FidelitySettings& f) {
    for (int i = 0; i < kFidelityPresets; ++i) {
        FidelitySettings p = fidelity_preset(i);
        // The picture's size and the two that only tidy it are set apart from a look.
        p.render_scale = f.render_scale, p.dither = f.dither, p.smooth_edges = f.smooth_edges, p.exposure = f.exposure;
        if (p == f) return i;
    }
    return -1;
}

const char* Settings::renderer_key(Renderer r) {
    switch (r) {
        case Renderer::D3D12: return "dx12";
        case Renderer::D3D11: return "dx11";
        case Renderer::OpenGL: return "opengl";
        default: return "auto";
    }
}

const char* Settings::renderer_name(Renderer r) {
    switch (r) {
        case Renderer::D3D12: return "DirectX 12";
        case Renderer::D3D11: return "DirectX 11";
        case Renderer::OpenGL: return "OpenGL";
        default: return "Automatic";
    }
}

const char* Settings::radio_voice_key(int v) {
    static const char* keys[kRadioVoices] = {"english", "german", "korean", "spanish"};
    return keys[std::clamp(v, 0, kRadioVoices - 1)];
}
const char* Settings::radio_voice_name(int v) {
    static const char* names[kRadioVoices] = {"English", "German", "Korean", "Spanish"};
    return names[std::clamp(v, 0, kRadioVoices - 1)];
}
const char* Settings::radio_voice_folder(int v) {
    static const char* folders[kRadioVoices] = {"eng", "ger", "kor", "spa"};
    return folders[std::clamp(v, 0, kRadioVoices - 1)];
}

const char* Settings::shadows_key(Shadows s) {
    switch (s) {
        case Shadows::Off: return "off";
        case Shadows::Feet: return "feet";
        default: return "sun";
    }
}

std::filesystem::path Settings::file() { return g_file.empty() ? eng::fs::executable_directory() / "settings.cfg" : g_file; }
void Settings::set_file(std::filesystem::path path) { g_file = std::move(path); }

void Settings::load() {
    const auto text = eng::fs::read_text_file(file());
    if (!text) return;
    eng::ConfigFile cfg;
    std::string err;
    if (!eng::ConfigFile::parse(*text, cfg, &err)) {
        LOG_WARN("settings.cfg: %s", err.c_str());
        return;
    }
    const eng::ConfigSection& r = cfg.root();
    first_run = false;
    const std::string rend = eng::str::lower(r.get_string("renderer"));
    for (const Renderer k : {Renderer::Auto, Renderer::D3D12, Renderer::D3D11, Renderer::OpenGL})
        if (rend == renderer_key(k)) renderer = k;
    borderless = eng::str::lower(r.get_string("display")) == "borderless";
    width = std::clamp(r.get_int("width", width), 640, 7680);
    height = std::clamp(r.get_int("height", height), 360, 4320);
    full_width = r.get_int("full_width", 0);
    full_height = r.get_int("full_height", 0);
    if (full_width < 640 || full_height < 360 || full_width > 7680 || full_height > 4320) full_width = full_height = 0;
    const std::string shape = eng::str::lower(r.get_string("aspect"));
    for (int i = 0; i < kAspectCount; ++i)
        if (shape == kAspects[i].key) aspect = i;
    scaling = eng::str::lower(r.get_string("scaling")) == "bars" ? Scaling::Bars : Scaling::Stretch;
    vsync = r.get_bool("vsync", vsync);
    max_fps = std::clamp(r.get_int("max_fps", max_fps), 30, 360);
    brightness = std::clamp(r.get_float("brightness", brightness), 0.5f, 2.0f);
    fov = std::clamp(r.get_float("fov", fov), 60.0f, 110.0f);
    antialiasing = snap(r.get_int("antialiasing", antialiasing), {1, 2, 4, 8});
    texture_filter = snap(r.get_int("texture_filter", texture_filter), {1, 2, 4, 8, 16});
    bullet_marks = r.get_bool("bullet_marks", bullet_marks);
    spent_cases = r.get_bool("spent_cases", spent_cases);
    blood = r.get_bool("blood", blood);
    impact_dust = r.get_bool("impact_dust", impact_dust);
    step_effects = r.get_bool("step_effects", step_effects);
    const std::string shade = eng::str::lower(r.get_string("shadows"));
    for (const Shadows k : {Shadows::Off, Shadows::Feet, Shadows::Sun})
        if (shade == shadows_key(k)) shadows = k;
    sharp_ui = r.get_bool("sharp_ui", sharp_ui);
    hide_weapon = r.get_bool("hide_weapon", hide_weapon);
    auto_switch = r.get_bool("auto_switch", auto_switch);
    always_run = r.get_bool("always_run", always_run);
    scope_hold = r.get_bool("scope_hold", scope_hold);
    scope_speed = std::clamp(r.get_float("scope_speed", scope_speed), 0.2f, 3.0f);
    show_netgraph = r.get_bool("show_netgraph", show_netgraph);
    damage_numbers = r.get_bool("damage_numbers", damage_numbers);
    const std::string voice = eng::str::lower(r.get_string("radio_voice"));
    for (int v = 0; v < kRadioVoices; ++v)
        if (voice == radio_voice_key(v)) radio_voice = v;
    fidelity = r.get_bool("fidelity", fidelity);
    // Asked for by hand in the file: found, as far as the options are concerned.
    fidelity_found = r.get_bool("fidelity_found", fidelity_found) || fidelity;
    {
        FidelitySettings& f = finish;
        auto num = [&](const char* k, float& v, float lo, float hi) { v = std::clamp(r.get_float(k, v), lo, hi); };
        f.shade = std::clamp(r.get_int("fidelity_shade", f.shade), 0, 3);
        num("fidelity_shade_strength", f.shade_strength, 0.0f, 1.5f);
        num("fidelity_shade_radius", f.shade_radius, 16.0f, 160.0f);
        f.glow = r.get_bool("fidelity_glow", f.glow);
        num("fidelity_glow_strength", f.glow_strength, 0.0f, 1.0f);
        num("fidelity_glow_threshold", f.glow_threshold, 0.4f, 1.2f);
        f.shafts = r.get_bool("fidelity_shafts", f.shafts);
        num("fidelity_shafts_strength", f.shafts_strength, 0.0f, 1.5f);
        f.lights = r.get_bool("fidelity_lights", f.lights);
        num("fidelity_lights_strength", f.lights_strength, 0.0f, 2.0f);
        num("fidelity_shine", f.shine, 0.0f, 2.0f);
        f.coronas = r.get_bool("fidelity_coronas", f.coronas);
        f.reflections = r.get_bool("fidelity_reflections", f.reflections);
        f.water = r.get_bool("fidelity_water", f.water);
        f.clouds = r.get_bool("fidelity_clouds", f.clouds);
        f.sun = r.get_bool("fidelity_sun", f.sun);
        num("fidelity_corona", f.corona, 0.0f, 1.5f);
        f.curve = std::clamp(r.get_int("fidelity_curve", f.curve), 0, 2);
        num("fidelity_exposure", f.exposure, 0.5f, 2.0f);
        num("fidelity_saturation", f.saturation, 0.0f, 2.0f);
        num("fidelity_contrast", f.contrast, 0.5f, 1.5f);
        num("fidelity_sharpen", f.sharpen, 0.0f, 1.0f);
        num("fidelity_vignette", f.vignette, 0.0f, 1.0f);
        f.dither = r.get_bool("fidelity_dither", f.dither);
        f.smooth_edges = r.get_bool("fidelity_smooth_edges", f.smooth_edges);
        num("fidelity_render_scale", f.render_scale, 0.5f, 2.0f);
    }
    sensitivity = std::clamp(r.get_float("sensitivity", sensitivity), 0.05f, 10.0f);
    invert_mouse = r.get_bool("invert_mouse", invert_mouse);
    auto vol = [&](const char* k, float& v) { v = std::clamp(r.get_float(k, v), 0.0f, 1.0f); };
    vol("master_volume", master_volume);
    vol("effects_volume", effects_volume);
    vol("music_volume", music_volume);
    vol("ui_volume", ui_volume);
    lobby_music = r.get_bool("lobby_music", lobby_music);
    global_chat = r.get_bool("global_chat", global_chat);
    room_invites = r.get_bool("room_invites", room_invites);
    for (size_t i = 0; i < macros.size(); ++i) macros[i] = r.get_string(eng::str::format("macro_%zu", (i + 1) % 10));
    show_fps = r.get_bool("show_fps", show_fps);
    // A line an older build wrote (one number, a style the CrossHair item unlocked) keeps the default.
    if (const std::string v = r.get_string("crosshair"); !v.empty()) {
        Crosshair c;
        int lines = 1, top = 1, dot = 0, ring = 0, outline = 1, moves = 1;
        if (std::sscanf(v.c_str(), "%d %d %f %f %f %d %f %d %f %d %d %d %d %d %d", &lines, &top, &c.length, &c.thickness, &c.gap, &dot, &c.dot_size, &ring,
                        &c.ring_size, &outline, &moves, &c.r, &c.g, &c.b, &c.a) == 15) {
            c.lines = lines != 0, c.top = top != 0, c.dot = dot != 0, c.ring = ring != 0, c.outline = outline != 0, c.moves = moves != 0;
            c.length = std::clamp(c.length, 1.0f, 40.0f), c.thickness = std::clamp(c.thickness, 1.0f, 8.0f), c.gap = std::clamp(c.gap, 0.0f, 30.0f);
            c.dot_size = std::clamp(c.dot_size, 0.5f, 8.0f), c.ring_size = std::clamp(c.ring_size, 1.0f, 40.0f);
            c.r = std::clamp(c.r, 0, 255), c.g = std::clamp(c.g, 0, 255), c.b = std::clamp(c.b, 0, 255), c.a = std::clamp(c.a, 60, 255);
            // Something must be drawn.
            if (!c.lines && !c.dot && !c.ring) c.lines = true;
            crosshair = c;
        }
    }
    // NONE keeps an action unbound (its key went to another action); a name nobody knows keeps the default.
    for (size_t i = 0; i < binds.size(); ++i) {
        const std::string name = eng::str::upper(r.get_string(std::string("bind_") + kActions[i].key));
        if (name == "NONE") binds[i] = 0;
        else if (const unsigned code = name.empty() ? 0 : eng::Input::binding_from_name(name)) binds[i] = code;
    }
    pad.enabled = r.get_bool("pad", pad.enabled);
    for (size_t i = 0; i < pad.binds.size(); ++i) {
        const std::string name = eng::str::upper(r.get_string(std::string("pad_") + kActions[i].key));
        if (name == "NONE") pad.binds[i] = 0;
        else if (const unsigned button = name.empty() ? 0 : eng::Gamepad::button_from_key(name)) pad.binds[i] = button;
    }
    pad.look = std::clamp(r.get_float("pad_look", pad.look), 0.2f, 3.0f);
    pad.look_vertical = std::clamp(r.get_float("pad_look_vertical", pad.look_vertical), 0.3f, 1.5f);
    pad.invert_y = r.get_bool("pad_invert", pad.invert_y);
    if (const std::string curve = eng::str::lower(r.get_string("pad_curve")); !curve.empty()) pad.smooth = curve != "linear";
    pad.dead_move = std::clamp(r.get_float("pad_dead_move", pad.dead_move), 0.0f, 0.4f);
    pad.dead_look = std::clamp(r.get_float("pad_dead_look", pad.dead_look), 0.0f, 0.4f);
    pad.swap_sticks = r.get_bool("pad_swap_sticks", pad.swap_sticks);
    pad.vibration = r.get_bool("pad_vibration", pad.vibration);
    touch.enabled = r.get_bool("touch_controls", touch.enabled);
    touch.size = std::clamp(r.get_float("touch_size", touch.size), 0.7f, 1.4f);
    touch.look = std::clamp(r.get_float("touch_look", touch.look), 0.3f, 3.0f);
    touch.opacity = std::clamp(r.get_float("touch_opacity", touch.opacity), 0.2f, 1.0f);
    touch.left_fire = r.get_bool("touch_left_fire", touch.left_fire);
    // part:x:y:scale, comma-separated; parts not named stay where they are out of the box.
    touch.places = {};
    for (std::string_view item : eng::str::split(r.get("touch_layout"), ',')) {
        const auto bits = eng::str::split(eng::str::trim(item), ':');
        if (bits.size() != 4) continue;
        for (size_t p = 0; p < kTouchParts; ++p) {
            if (eng::str::trim(bits[0]) != touch_part_key(TouchPart(p))) continue;
            float x = 0, y = 0, s = 1;
            if (!eng::str::parse_float(bits[1], x) || !eng::str::parse_float(bits[2], y) || !eng::str::parse_float(bits[3], s)) break;
            if (x < 0 || y < 0) x = y = -1;   // sized only
            else x = std::clamp(x, 0.0f, 1.0f), y = std::clamp(y, 0.0f, 1.0f);
            touch.places[p] = {x, y, std::clamp(s, 0.5f, 2.0f)};
        }
    }
    // A weapon lens saved by an older build (the gun held too close) is dropped for today's default.
    // The view_rifle, view_smg ... lines the F8 panel once wrote are not read: every weapon is framed
    // by its own place.
    if (r.get_int("view_framing", 0) == kViewFramingVersion) viewmodel_fov = std::clamp(r.get_float("viewmodel_fov", viewmodel_fov), 40.0f, 80.0f);
    remember = r.get_bool("remember", remember);
    account = r.get_string("account");
    last_server = r.get_string("last_server");
    tvas = r.get_string("tvas");
    download_ask_mb = std::clamp(r.get_int("download_ask_mb", download_ask_mb), 0, 50);
    cache_mb = std::clamp(r.get_int("cache_mb", cache_mb), 50, 8192);
    auto ids = [&](const char* key, std::vector<unsigned long long>& out) {
        out.clear();
        for (std::string_view item : eng::str::split(r.get(key), ','))
            if (const unsigned long long id = std::strtoull(std::string(eng::str::trim(item)).c_str(), nullptr, 10); id && out.size() < 64) out.push_back(id);
    };
    ids("favourites", favourites);
    ids("recent", recent);
    data_dir = r.get_string("data");
    servers.clear();
    for (std::string_view item : eng::str::split(r.get("servers"), ',')) {
        item = eng::str::trim(item);
        if (item.empty()) continue;
        ServerEntry e;
        const size_t at = item.find('@');
        if (at == std::string_view::npos) {
            e.name = std::string(item);
            e.address = std::string(item);
        } else {
            e.name = std::string(eng::str::trim(item.substr(0, at)));
            e.address = std::string(eng::str::trim(item.substr(at + 1)));
        }
        if (!e.address.empty()) servers.push_back(std::move(e));
    }
}

bool Settings::save() const {
    eng::ConfigFile cfg;
    eng::ConfigSection& r = cfg.section("");
    auto flag = [&](const char* k, bool v) { r.set(k, v ? "true" : "false"); };
    r.set("renderer", renderer_key(renderer));
    r.set("display", borderless ? "borderless" : "windowed");
    r.set("width", std::to_string(width));
    r.set("height", std::to_string(height));
    r.set("full_width", std::to_string(full_width));
    r.set("full_height", std::to_string(full_height));
    r.set("aspect", aspect_info(aspect).key);
    r.set("scaling", scaling == Scaling::Bars ? "bars" : "stretch");
    flag("vsync", vsync);
    r.set("max_fps", std::to_string(max_fps));
    r.set("brightness", eng::str::format("%.2f", double(brightness)));
    r.set("fov", eng::str::format("%.0f", double(fov)));
    r.set("antialiasing", std::to_string(antialiasing));
    r.set("texture_filter", std::to_string(texture_filter));
    flag("bullet_marks", bullet_marks);
    flag("spent_cases", spent_cases);
    flag("blood", blood);
    flag("impact_dust", impact_dust);
    flag("step_effects", step_effects);
    r.set("shadows", shadows_key(shadows));
    flag("sharp_ui", sharp_ui);
    flag("hide_weapon", hide_weapon);
    flag("auto_switch", auto_switch);
    flag("always_run", always_run);
    flag("scope_hold", scope_hold);
    r.set("scope_speed", eng::str::format("%.2f", double(scope_speed)));
    flag("show_netgraph", show_netgraph);
    flag("damage_numbers", damage_numbers);
    r.set("radio_voice", radio_voice_key(radio_voice));
    // Fidelity stays out of the file until it has been found: nothing there to stumble on.
    if (fidelity_found) {
        flag("fidelity", fidelity);
        flag("fidelity_found", true);
        const FidelitySettings& f = finish;
        auto num = [&](const char* k, float v) { r.set(k, eng::str::format("%.2f", double(v))); };
        r.set("fidelity_shade", std::to_string(f.shade));
        num("fidelity_shade_strength", f.shade_strength);
        num("fidelity_shade_radius", f.shade_radius);
        flag("fidelity_glow", f.glow);
        num("fidelity_glow_strength", f.glow_strength);
        num("fidelity_glow_threshold", f.glow_threshold);
        flag("fidelity_shafts", f.shafts);
        num("fidelity_shafts_strength", f.shafts_strength);
        flag("fidelity_lights", f.lights);
        num("fidelity_lights_strength", f.lights_strength);
        num("fidelity_shine", f.shine);
        flag("fidelity_coronas", f.coronas);
        flag("fidelity_reflections", f.reflections);
        flag("fidelity_water", f.water);
        flag("fidelity_clouds", f.clouds);
        flag("fidelity_sun", f.sun);
        num("fidelity_corona", f.corona);
        r.set("fidelity_curve", std::to_string(f.curve));
        num("fidelity_exposure", f.exposure);
        num("fidelity_saturation", f.saturation);
        num("fidelity_contrast", f.contrast);
        num("fidelity_sharpen", f.sharpen);
        num("fidelity_vignette", f.vignette);
        flag("fidelity_dither", f.dither);
        flag("fidelity_smooth_edges", f.smooth_edges);
        num("fidelity_render_scale", f.render_scale);
    }
    r.set("sensitivity", eng::str::format("%.2f", double(sensitivity)));
    flag("invert_mouse", invert_mouse);
    r.set("master_volume", eng::str::format("%.2f", double(master_volume)));
    r.set("effects_volume", eng::str::format("%.2f", double(effects_volume)));
    r.set("music_volume", eng::str::format("%.2f", double(music_volume)));
    r.set("ui_volume", eng::str::format("%.2f", double(ui_volume)));
    flag("lobby_music", lobby_music);
    flag("global_chat", global_chat);
    flag("room_invites", room_invites);
    for (size_t i = 0; i < macros.size(); ++i)
        if (!macros[i].empty()) r.set(eng::str::format("macro_%zu", (i + 1) % 10), macros[i]);
    flag("show_fps", show_fps);
    {
        const Crosshair& c = crosshair;
        r.set("crosshair", eng::str::format("%d %d %.1f %.1f %.1f %d %.1f %d %.1f %d %d %d %d %d %d", c.lines ? 1 : 0, c.top ? 1 : 0, double(c.length),
                                            double(c.thickness), double(c.gap), c.dot ? 1 : 0, double(c.dot_size), c.ring ? 1 : 0, double(c.ring_size),
                                            c.outline ? 1 : 0, c.moves ? 1 : 0, c.r, c.g, c.b, c.a));
    }
    for (size_t i = 0; i < binds.size(); ++i)
        r.set(std::string("bind_") + kActions[i].key, binds[i] ? eng::Input::binding_name(binds[i]) : std::string("NONE"));
    flag("pad", pad.enabled);
    for (size_t i = 0; i < pad.binds.size(); ++i) {
        const std::string name = eng::Gamepad::button_key(pad.binds[i]);
        r.set(std::string("pad_") + kActions[i].key, name.empty() ? std::string("NONE") : name);
    }
    r.set("pad_look", eng::str::format("%.2f", double(pad.look)));
    r.set("pad_look_vertical", eng::str::format("%.2f", double(pad.look_vertical)));
    flag("pad_invert", pad.invert_y);
    r.set("pad_curve", pad.smooth ? "smooth" : "linear");
    r.set("pad_dead_move", eng::str::format("%.2f", double(pad.dead_move)));
    r.set("pad_dead_look", eng::str::format("%.2f", double(pad.dead_look)));
    flag("pad_swap_sticks", pad.swap_sticks);
    flag("pad_vibration", pad.vibration);
    flag("touch_controls", touch.enabled);
    r.set("touch_size", eng::str::format("%.2f", double(touch.size)));
    r.set("touch_look", eng::str::format("%.2f", double(touch.look)));
    r.set("touch_opacity", eng::str::format("%.2f", double(touch.opacity)));
    flag("touch_left_fire", touch.left_fire);
    {
        std::string layout;
        for (size_t p = 0; p < kTouchParts; ++p) {
            const TouchPlace& tp = touch.places[p];
            if (!tp.moved() && tp.scale == 1.0f) continue;
            // Sized only: kept where it is out of the box (-1).
            layout += (layout.empty() ? "" : ", ") + eng::str::format("%s:%.4f:%.4f:%.2f", touch_part_key(TouchPart(p)), double(tp.moved() ? tp.x : -1.0f),
                                                                      double(tp.moved() ? tp.y : -1.0f), double(tp.scale));
        }
        r.set("touch_layout", layout);
    }
    r.set("viewmodel_fov", eng::str::format("%.1f", double(viewmodel_fov)));
    r.set("view_framing", std::to_string(kViewFramingVersion));
    flag("remember", remember);
    r.set("account", remember ? account : "");
    std::string list;
    for (const ServerEntry& e : servers) list += (list.empty() ? "" : ", ") + e.name + "@" + e.address;
    r.set("servers", list);
    r.set("last_server", last_server);
    if (!tvas.empty()) r.set("tvas", tvas);
    r.set("download_ask_mb", std::to_string(download_ask_mb));
    r.set("cache_mb", std::to_string(cache_mb));
    auto ids = [&](const char* key, const std::vector<unsigned long long>& from) {
        std::string text;
        for (unsigned long long id : from) text += (text.empty() ? "" : ", ") + std::to_string(id);
        r.set(key, text);
    };
    ids("favourites", favourites);
    ids("recent", recent);
    if (!data_dir.empty()) r.set("data", data_dir);
    const std::string text = "# Soldier Front Legacy settings (written by the game)\n" + cfg.serialize();
    return eng::fs::write_text_file(file(), text);
}

}  // namespace lsf
