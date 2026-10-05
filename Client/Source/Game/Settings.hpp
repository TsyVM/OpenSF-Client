// The player's preferences, kept in settings.cfg beside legacysf.exe (the app's files folder on
// a phone):
//
//   renderer = auto          # auto (DirectX 12 where the card has it), dx12, dx11 or opengl; read at start
//   display = windowed       # windowed | borderless (the whole screen)
//   width = 1600, height = 900             # the window
//   full_width = 0, full_height = 0        # the picture on the whole screen (0: the screen's own size)
//   aspect = auto            # which sizes the options list: auto, 4:3, 5:4, 16:10, 16:9, 21:9
//   scaling = stretch        # a picture of another shape than the screen: stretch | bars
//   vsync = false, max_fps = 144
//   brightness = 1.0         # the world's exposure
//   fov = 80                 # horizontal degrees at 16:9
//   antialiasing = 1         # samples a pixel: 1 (off), 2, 4, 8
//   texture_filter = 8       # 1 (trilinear), 2, 4, 8, 16 (anisotropic)
//   bullet_marks, spent_cases, blood, impact_dust, step_effects = true   # the match's small effects
//   shadows = sun            # soldiers' shadows: off | feet (the original's shade under them) | sun (cast by the map's sun too)
//   sharp_ui = true          # the lobby's plates redrawn sharp (false: the client's own bitmaps)
//   hide_weapon = false      # the original's "invisible weapon": no gun drawn in your hands
//   always_run = true        # false: walk, and the Walk key runs
//   auto_switch = true       # a gun taken up off the floor comes to hand (false: it is slung, and you keep what you hold)
//   scope_hold = false       # true: scoped while the Scope key is held (false: a press in, a press out)
//   scope_speed = 1.0        # the aim's speed through a scope, against the weapon's own
//   show_netgraph = false    # the ping and the frame rate over the last seconds, in a match
//   damage_numbers = false   # each hit's damage over whoever you hit, your total for him under it
//   fidelity = false         # see Docs/Options.md; its parts are the fidelity_* lines (FidelitySettings)
//   sensitivity = 1.0, invert_mouse = false
//   master_volume = 1.0, effects_volume = 1.0, music_volume = 0.7, ui_volume = 1.0
//   lobby_music = true       # the lobby's music (its sound switch)
//   global_chat = true       # global chat heard (and spoken on) everywhere, matches too
//   room_invites = true      # other soldiers may invite you to their rooms
//   radio_voice = english    # the radio's language (config.cfg *setradiolanguage): english, german, korean, spanish
//   show_fps = true
//   crosshair = 1 1 9 2 3 0 1.8 0 4 1 1 120 255 90 230
//                            # the player's own crosshair (Crosshair below, in its order): ticks, the
//                            # upper tick, length, thickness, gap, dot, its size, ring, its size,
//                            # dark edge, opens with the cone, then red green blue and opacity
//   bind_knee = LSHIFT       # the match's keys (Action below), as Engine/Platform/Input names them
//   pad = true               # a controller (Xbox, DualShock 4, DualSense) plays the match
//   pad_knee = B             # its buttons, as Engine/Platform/Gamepad names them (NONE: unbound)
//   pad_look = 1.0, pad_look_vertical = 0.75, pad_invert = false, pad_curve = smooth | linear
//   pad_dead_move = 0.0, pad_dead_look = 0.0, pad_swap_sticks = false, pad_vibration = true
//   touch_controls = true, touch_size = 1.0, touch_look = 1.0, touch_opacity = 0.55, touch_left_fire = true
//   touch_layout = fire:0.9081:0.7222:1.20, stick:0.11:0.77:1.00   # the controls moved or sized (part:x:y:scale)
//   macro_1 = Cover me!      # .. macro_0: the chat macros
//   remember = true, account = name   # the sign-in name (never the password)
//   servers = Legacy@play.example.com:27240, LAN@192.168.1.10   # the server list, name@host[:port]
//   last_server = Legacy
//   data = C:\...\Soldier Front\data  # the Soldier Front client data folder, when not found by itself
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace lsf {

struct ServerEntry {
    std::string name;
    std::string address;   // host[:port]
};

// The match's keys, by Soldier Front's own actions (data/config.cfg's *setcontrol lines), each a
// binding code (Engine/Platform/Input.hpp: a virtual key, or 0x100 + a mouse button). The defaults
// are the original's: left Shift crouches (KNEE), left Ctrl walks, F draws the last weapon, Q is
// the camera, the right button zooms.
enum class Action : int {
    Go, Back, StrafeLeft, StrafeRight, Jump, Knee, Walk,
    Shoot, Zoom, Reload, LastWeapon, Weapon1, Weapon2, Weapon3, Weapon4,
    CameraMode, ScoreView, RadioCommand, RadioGeneral, RadioReply,
    NextWeapon, PrevWeapon,   // the wheel's (config.cfg NEXT_WEAPON, PREV_WEAPON): a key or a pad button may do it too
    Use,                      // config.cfg USE: held to set or make safe a bomb, open the treasure, rescue the girl
    Spray,                    // config.cfg SPRAY: the spray you carry, on the wall in front of you (Game/Shop.hpp)
    // config.cfg WEAPON_DROP: the gun in hand on the floor. Its default is F, as the original's (which
    // shared F with LAST_USED_WEAPON): on the same key as Last weapon, a tap is that and a hold drops.
    DropWeapon,
    PickUpWeapon,             // config.cfg ACQUIRE_WEAPON: the gun at your feet into its slot (yours goes down for it)
    Objective,                // config.cfg OBJECTIVE: the mission's briefing while held
    HideHud,                  // the original's H: the HUD off and on again
    Blood,                    // the original's B: blood on and off
    CenterView,               // config.cfg CENTER_VIEW: the aim back level
    TurnLeft, TurnRight,      // config.cfg ROTATE_LEFT, ROTATE_RIGHT: turning by key
    LookUp, LookDown,         // config.cfg LOOK_UP, LOOK_DOWN
    Item1, Item2, Item3,      // Horror Mode's items (Game/Rules.hpp HorrorItem): your side's three
    Count
};
struct ActionInfo {
    const char* key;        // settings.cfg: bind_<key>, pad_<key>
    const char* sf_name;    // config.cfg's name for it
    const char* label;      // the options' row
    unsigned default_code;
    unsigned default_pad;   // an eng::PadButton, 0 none (the sticks move and look: they are not buttons)
};
const ActionInfo& action_info(Action a);
using Binds = std::array<unsigned, size_t(Action::Count)>;
Binds default_binds();
Binds default_pad_binds();

// A controller in the match. The left stick moves and the right one looks (the other way round
// for a left-handed hold); everything else is a button, bound like a key. There is no aim assist:
// a pad plays the same match as a mouse, on the same terms.
struct PadSettings {
    bool enabled = true;
    Binds binds = default_pad_binds();
    float look = 1.0f;            // the looking stick's turn rate, times kPadTurnRate
    float look_vertical = 0.75f;  // up and down against across
    bool invert_y = false;
    bool smooth = true;           // the stick's travel squared: fine near the centre, full at the edge
    float dead_move = 0.0f;       // more dead zone than the pad's own, 0..0.4 of the travel
    float dead_look = 0.0f;
    bool swap_sticks = false;
    bool vibration = true;
};
inline constexpr float kPadTurnRate = 220.0f;   // degrees a second with the stick hard over, at look 1

// The touch screen in a match (a phone: World/TouchControls.hpp). A stick under the left thumb,
// buttons under the right, the rest of the screen to look. Shown once the screen is touched in a
// match; a controller or the keyboard used puts them away again.
// Each of the controls, which the player may move and size one by one (Options, Controls, Touch,
// Arrange). The stick's place is where it rests; it still comes to the thumb on that side.
enum class TouchPart : int { Fire, LeftFire, Aim, Jump, Crouch, Reload, Swap, Use, Menu, Chat, Score, Stick, Count };
inline constexpr size_t kTouchParts = size_t(TouchPart::Count);
const char* touch_part_key(TouchPart p);     // settings.cfg's name: "fire", "left_fire", ...
const char* touch_part_label(TouchPart p);   // the arranging screen's: "Fire", "Second Fire", ...
struct TouchPlace {
    float x = -1, y = -1;     // the centre, as fractions of the screen; -1: where it is out of the box
    float scale = 1.0f;       // its own size, times the size of them all, 0.5..2
    bool moved() const { return x >= 0 && y >= 0; }
};
struct TouchSettings {
    bool enabled = true;
    float size = 1.0f;        // the buttons and the stick, 0.7..1.4
    float look = 1.0f;        // how far a swipe turns, 0.3..3
    float opacity = 0.55f;    // 0.2..1
    bool left_fire = true;    // a second Fire over the stick, for the left thumb
    std::array<TouchPlace, kTouchParts> places{};
};

// The shapes the options sort screen sizes by.
struct AspectInfo {
    const char* key;     // settings.cfg
    const char* label;
    int w, h;            // 0, 0: every shape
};
inline constexpr int kAspectCount = 6;
const AspectInfo& aspect_info(int index);
// Which of them a size is (0: none of the named ones).
int aspect_of(int width, int height);

// The crosshair, the player's own to shape (the options' Crosshair page; free, as the rest of
// the options are). Lengths are in screen units: a pixel on a screen 900 high. The default is the
// original's: four green ticks that open with the cone a shot leaves in.
struct Crosshair {
    bool lines = true;           // the four ticks
    bool top = true;             // the upper one (off: a T)
    float length = 9.0f;         // of a tick
    float thickness = 2.0f;
    float gap = 3.0f;            // from the centre to a tick, the cone closed
    bool dot = false;
    float dot_size = 1.8f;       // its radius
    bool ring = false;
    float ring_size = 4.0f;      // its radius, out from the gap
    bool outline = true;         // a dark edge, so it shows against a bright wall
    bool moves = true;           // the ticks and the ring open with the cone; off: they stand still
    int r = 120, g = 255, b = 90, a = 230;
    bool operator==(const Crosshair&) const = default;
};
// The shapes the options offer to start from (the first three are the original client's own, off
// its source_crosshair sheet). A preset sets the shape and leaves the colour.
inline constexpr int kCrosshairPresets = 7;
const char* crosshair_preset_name(int index);
Crosshair crosshair_preset(int index, const Crosshair& colour_of);
// Which preset a crosshair is, by shape (-1: the player's own).
int crosshair_preset_of(const Crosshair& c);
// The colours the options offer by name; any other is mixed by hand.
struct CrosshairInk {
    const char* name;
    int r, g, b;
};
inline constexpr int kCrosshairInks = 8;
const CrosshairInk& crosshair_ink(int index);

// Fidelity's finish, part by part (the options' Fidelity page, there once it has been found).
// Every part works on the picture the map made: none of them changes its light, hour or sky.
struct FidelitySettings {
    int shade = 2;                  // contact shadow: 0 off, 1 low, 2 medium, 3 high (samples a pixel)
    float shade_strength = 0.55f;   // 0..1.5
    float shade_radius = 48.0f;     // centimetres, 16..160
    bool glow = true;               // off the brightest light
    float glow_strength = 0.20f;    // 0..1
    float glow_threshold = 0.80f;   // how bright light must be to glow, 0.4..1.2
    bool shafts = false;            // light shafts from the map's own sun, looking toward it
    float shafts_strength = 0.5f;   // 0..1.5
    // Lights on the map's surfaces: a muzzle flash, a blast and the map's lamps light what they
    // are next to and glint off what shines (tile, metal, glass), as does the map's own sun.
    bool lights = true;
    float lights_strength = 1.0f;   // 0..2
    float shine = 1.0f;             // how strong a glint is, 0..2
    // Written but not yet proven on every renderer (October 2026): off until they are.
    bool coronas = false;           // a corona at each of the map's lamps (a headlight, a spot)
    bool reflections = false;       // what shines mirrors the picture: tiled floors, steel, water
    bool water = false;             // water drawn as water: rippling, mirroring the sky
    bool clouds = false;            // cloud drifting over the map's own sky
    bool sun = true;                // the sun's disc (the moon at night) where the map's light comes from
    float corona = 0.6f;            // its corona, 0..1.5
    int curve = 1;                  // highlights: 0 as drawn, 1 a soft shoulder, 2 filmic
    float exposure = 1.0f;          // 0.5..2
    float saturation = 1.04f;       // 0 (black and white) .. 2
    float contrast = 1.03f;         // 0.5..1.5
    float sharpen = 0.25f;          // 0..1
    float vignette = 0.12f;         // 0..1
    bool dither = true;             // gradients (a sky, a dark wall) without bands
    bool smooth_edges = true;       // edge smoothing after the fact (FXAA)
    float render_scale = 1.0f;      // the match's pixels against the picture's: 0.5..2
    bool operator==(const FidelitySettings&) const = default;
};
// Whole looks to start from: Subtle (the default), Rich, Cinematic, Clean (edges and detail only:
// no lights, no sun, nothing added to the picture).
inline constexpr int kFidelityPresets = 4;
const char* fidelity_preset_name(int index);
FidelitySettings fidelity_preset(int index);
int fidelity_preset_of(const FidelitySettings& f);   // -1: the player's own

// A first-person weapon stands where its own .sfc puts it from the camera: the original client's
// framing, and one the guns agree on (the eye some 40 cm ahead of the elbow: the gun's body in the
// corner, the left hand and forearm in view, the right hand under the picture). A weapon's
// Camera01 node is NOT that: it is the artists' camera, and they disagree from gun to gun (the
// M4A1's stands at the elbow, 18 cm left of it, both forearms in the picture; the Black AK47s' 30
// left and 40 up, the gun small in the corner). Players cannot move it (the F8 panel that did was
// taken out on 2026-10-05). Raised when the default framing changes: a `viewmodel_fov` saved with
// an older one takes the new default.
inline constexpr int kViewFramingVersion = 2;

struct Settings {
    enum class Renderer { Auto, D3D12, D3D11, OpenGL };
    Renderer renderer = Renderer::Auto;
    static const char* renderer_key(Renderer r);
    static const char* renderer_name(Renderer r);

    bool borderless = false;
    int width = 1600, height = 900;            // the window
    int full_width = 0, full_height = 0;       // the picture when it has the whole screen (0: the screen's own)
    int aspect = 0;                            // aspect_info: which sizes the options offer
    // A picture of another shape than the screen: pulled over all of it, as the original looked on
    // a wide screen, or kept in shape between black bars.
    enum class Scaling { Stretch, Bars };
    Scaling scaling = Scaling::Stretch;
#ifdef __ANDROID__
    bool vsync = true;
#else
    bool vsync = false;
#endif
    int max_fps = 144;
    float brightness = 1.0f;
    float fov = 80.0f;
    // Graphics. Everything here leaves the map's own look alone: its bake, its hour, its sky.
    int antialiasing = 1;                      // samples a pixel: 1 (off), 2, 4, 8
    int texture_filter = 8;                    // 1 trilinear; 2, 4, 8, 16 anisotropic
    bool bullet_marks = true, spent_cases = true, blood = true, impact_dust = true;
    bool step_effects = true;                  // dust, grass, snow and splashes where soldiers run
    // Soldiers' shadows (config.cfg *setshadow): none; the original's soft shade under each
    // soldier's feet (effect/shadow/shadow.bmp); or cast by the map's own sun as well (TacticalFPS's:
    // a shadow map of what moves, laid only on what the bake left in the sun), the shade under the
    // feet lighter beside it. A map with no sun to speak of (a night) keeps the shade alone.
    enum class Shadows { Off, Feet, Sun };
#ifdef __ANDROID__
    Shadows shadows = Shadows::Feet;
#else
    Shadows shadows = Shadows::Sun;
#endif
    static const char* shadows_key(Shadows s);
    // The lobby kit's plates, tabs and small controls redrawn at the screen's own size (Ui/Kit.hpp);
    // off: the client's own bitmaps, as they are.
    bool sharp_ui = true;
    bool hide_weapon = false;                  // config.cfg *setinvisibleweapon
    bool auto_switch = true;                   // config.cfg *setautoswitch: a gun picked up comes to hand
    bool always_run = true;                    // config.cfg *setalwaysrun
    bool scope_hold = false;                   // scoped while Scope is held (the original: a press in, a press out)
    float scope_speed = 1.0f;                  // the aim through a scope, times the weapon's own (weapon.kst SCOPE_MOVEFACTOR)
    bool show_netgraph = false;                // the match's ping and frame rate graph (the original had none)
    bool damage_numbers = false;               // each hit of yours as a number over the soldier, your total for him under them (the original had none)
    // Fidelity: a finer finish over the same picture (Docs/Options.md). Not listed in the options
    // until it has been found.
    bool fidelity = false, fidelity_found = false;
    FidelitySettings finish;
    float sensitivity = 1.0f;
    bool invert_mouse = false;
    float master_volume = 1.0f, effects_volume = 1.0f, music_volume = 0.7f, ui_volume = 1.0f;
    bool lobby_music = true;   // the lobby's sound switch: its music on or off
    bool global_chat = true;   // global chat: heard in the lobbies, rooms and matches
    bool room_invites = true;  // invitations to rooms are taken (the lobby's Accept Invites plate)
    // The radio's language (config.cfg *setradiolanguage): the archives' radio_eng, radio_ger,
    // radio_kor and radio_spa, each in a man's voice and a woman's (the speaker's soldier says which).
    int radio_voice = 0;
    static constexpr int kRadioVoices = 4;
    static const char* radio_voice_key(int v);    // settings.cfg: english, german, korean, spanish
    static const char* radio_voice_name(int v);   // the options' row
    static const char* radio_voice_folder(int v); // the sound archives' radio_<it>
    bool show_fps = true;
    Crosshair crosshair;
    Binds binds = default_binds();
    unsigned bind(Action a) const { return binds[size_t(a)]; }
    PadSettings pad;
    TouchSettings touch;
    float viewmodel_fov = 56;   // the weapon in hand's own lens (vertical degrees); the world keeps `fov`
    bool remember = true;
    std::string account;
    std::array<std::string, 10> macros;   // chat macros: Alt+1 .. Alt+0 in a match
    std::vector<ServerEntry> servers;
    std::string last_server;
    // Team Vanilla's account service (TV-15): a setting; empty is the address built in.
    std::string tvas;
    // A server's packs (Docs/UniversalServerDeploy.md 11.7, 11.8): the size asked about before it
    // comes down, and how much of them is kept.
    int download_ask_mb = 5;
    int cache_mb = 512;
    // The server list's Favourites and Recent tabs: server ids.
    std::vector<unsigned long long> favourites, recent;
    std::string data_dir;
    bool first_run = true;

    // settings.cfg beside the game, or the file a run was told to use (--settings: a test keeps
    // its own, so the player's is never touched).
    static std::filesystem::path file();
    static void set_file(std::filesystem::path path);
    void load();
    bool save() const;
};

}  // namespace lsf
