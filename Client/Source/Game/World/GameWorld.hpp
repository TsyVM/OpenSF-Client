// The match on this machine: the map, your soldier (moved and aimed here, checked by the server),
// everyone else (from the server's snapshots), weapons with their own first-person animation,
// shots, grenades, and what the HUD shows.
//
// Loading is split the way the loading screen needs it: the map, its collision and every model
// the match will draw are read on a thread; the textures go onto the card a slice a frame.
//
// Soldiers move and are drawn as the original's own data has them (Docs/Research.md §10): the
// speeds its clips were authored at (Game/Movement.hpp), its keys (Settings' Action), its cone
// (weapon.kst, recoil.kst: Game/Rules.hpp), its clips played at the pace the soldier actually
// moves with the legs turned toward where he runs, the gun each one is seen holding (force
// weapon/ + point/), and flashes and tracers from that gun's muzzle in its flare/ art.
#pragma once

#include "Game/Modes.hpp"
#include "Game/Movement.hpp"
#include "Game/Protocol.hpp"
#include "Game/Render/WorldRenderer.hpp"
#include "Game/Rules.hpp"
#include "Game/Ui/Atlas.hpp"
#include "Game/World/KillEffects.hpp"
#include "Game/World/MatchAudio.hpp"
#include "Game/World/TouchControls.hpp"
#include "SF/Level.hpp"
#include "SF/Model.hpp"
#include "Engine/Core/Strings.hpp"

#include <atomic>
#include <cmath>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lsf {

class App;
enum class Action : int;

// One soldier's animated body (a force's model and its clips) or a first-person weapon.
struct Animated {
    std::shared_ptr<sf::Model> model;
    std::unique_ptr<ModelGpu> gpu;
    std::vector<eng::Mat4> local, model_space, skin;
    // A view model's arms and gun as built (bind pose): what its framing is measured off.
    eng::Aabb hands_box, gun_box;
};

// What a soldier is seen holding (weapon.kst OBJECT), on the card.
struct Carried {
    std::shared_ptr<sf::CarriedModel> model;
    std::unique_ptr<ModelGpu> gpu;
    int flame = -1;                  // the muzzle's bone, -1: a knife or a grenade
    eng::Mat4 flame_at;              // the muzzle in the gun's own space
    int cartridge = -1;              // the ejection port's bone, -1: none
    eng::Mat4 cartridge_at;
    float scale = 1;                 // thrown (a grenade): the model's units to a grenade's 11 cm
    eng::Vec3 centre;                // the middle of its bounds (a thrown grenade turns about it)
    float hand_scale = 0;            // the size it has in a hand (a gun on the floor is drawn so), 0 until measured
};

struct KillFeedLine {
    std::string killer, victim;
    eng::u32 killer_id = 0, victim_id = 0;
    u8 killer_team = 0, victim_team = 0;
    u8 killer_colour = 0, victim_colour = 0;   // their Colored Codenames
    std::string weapon;
    u16 weapon_id = 0xFFFF;
    u16 flags = 0;   // proto::KillFlags
    double time = 0;
};

class GameWorld {
public:
    explicit GameWorld(App& app);
    ~GameWorld();

    // A server's match, or `offline` practice on a map with nobody else.
    void start_loading(const proto::MatchLoad& load, bool offline);
    // A match recording (Game/Replay.hpp), watched: the same world fed recorded messages instead
    // of the session's, from a seat that is nobody's (`load.you` 0), with a camera of its own --
    // a soldier's eyes, a chase camera behind them, or free.
    void start_watching(const proto::MatchLoad& load);
    bool watching() const { return watching_; }
    // One recorded message. `fast` while seeking: state only, no shots, sounds or effects.
    void feed(std::span<const u8> msg, bool fast = false);
    // Back to the start of the recording (a seek backwards replays it from the beginning).
    void watch_reset();
    enum class WatchView { Eyes = 0, Chase = 1, Free = 2 };
    WatchView watch_view() const { return watch_view_; }
    void set_watch_view(WatchView v) { watch_view_ = v; }
    u32 followed() const { return follow_; }
    void follow(u32 id) { follow_ = id; }
    // The next (or previous) soldier, the living first.
    void follow_next(int dir);
    float progress() const;
    std::string status() const;
    bool ready() const { return ready_; }
    bool alive() const { return alive_; }
    const std::string& map_id() const { return settings_.map; }
    const sf::Level* level() const { return level_.get(); }
    const RoomSettings& room() const { return settings_; }
    void release();

    void update(float dt);
    void render();
    void draw_hud();
    bool menu_open() const { return menu_open_; }
    // You have the soldier (no menu, chat or dialog in the way): the mouse is the aim, and a
    // controller plays instead of pointing.
    bool in_control() const { return control_; }

    // What the HUD and the results read.
    struct PlayerView {
        u32 id = 0;
        std::string name;
        Team team = Team::Red;
        u8 force = 0;
        Loadout loadout = kNoLoadout;
        bool alive = false;
        int health = 0;
        eng::Vec3 position, velocity;
        float yaw = 0, pitch = 0;
        u16 flags = 0;
        u16 weapon = kNoWeapon;
        u16 kills = 0, deaths = 0, assists = 0, score = 0, ping = 0, headshots = 0;
        u32 xp = 0;
        u8 name_colour = 0;          // Colored Codename (Game/Items.hpp)
        u16 marks = 0;               // his row's marks on the Tab board (Game/Items.hpp RowMark)
        std::vector<u16> parts;      // the character parts worn
        std::string look;            // the model he is drawn with: his force in those parts (look_of)
        // What the server last said of him, oldest first: he is drawn a little behind the newest,
        // between two of them, so a late or early snapshot does not make him stutter.
        struct Sample {
            double at = 0;
            eng::Vec3 position, velocity;
            float yaw = 0, pitch = 0;
            u16 flags = 0;
        };
        std::deque<Sample> samples;
        double snap_time = 0;        // when the last one came
        // Body.
        Animated body;
        std::string lower_clip, upper_clip;
        float lower_time = 0, upper_time = 0;
        float leg_yaw = 0;           // the legs turned toward where he runs, degrees off his aim
        bool was_grounded = true, was_crouched = false;
        double jumped_at = -10, crouch_changed_at = -10;
        u16 shown_weapon = kNoWeapon;
        double drew_at = -10;        // a weapon taken out (the take clip)
        double fired_at = -10, burst_from = -10;
        u32 swings = 0;              // shots seen from him: a blade's swings take turns
        std::string death_clip;
        double died_at = -10;
        // The gun in his hand as last drawn, and its muzzle (where his tracers and flash come from).
        eng::Vec3 muzzle, muzzle_dir;
        double muzzle_at = -10;
        eng::Vec3 eject;             // his gun's ejection port as last drawn (spent cases leave from here)
        bool has_eject = false;
        // An enemy the server will not say where (its sight cull, kFlagHidden): not drawn, not hit,
        // not on the radar, no footsteps.
        bool hidden = false;
        // The game type's (World/Objectives.cpp): a captain's big head, out of the round by escaping,
        // the score tab's own columns.
        bool big_head = false;
        bool escaped = false;
        std::array<u16, 3> extra{};
        // His own guns and the spray he carries, as the match's load said.
        Loadout own = kNoLoadout;
        // ... as he brought them (own is a gun's taken off the floor until he respawns).
        Loadout brought = kNoLoadout;
        u16 spray = kNoSpray;
    };
    const std::map<u32, PlayerView>& players() const { return players_; }
    u32 me() const { return me_; }
    // Tests: turn the view (degrees; pitch up is positive).
    void look(float yaw, float pitch) { yaw_ = yaw, pitch_ = pitch; }
    void open_radio(int group) { radio_menu_ = group; }
    void test_radio(u8 group, u8 line) { say_radio(group, line); }
    // Tests: what the HUD shows after a kill, without anyone to kill (0 a head shot, 1 a
    // four-kill streak), with a hit taken from behind and a few lines in the feed.
    void test_hud(int variant);
    const char* test_kill_mark(int index);
    // Tests: run forward, hold the trigger; jump (and crouch in the air); turn every frame.
    void autopilot(bool run, bool fire) { auto_run_ = run, auto_fire_ = fire; }
    void autopilot_jump(bool jump, bool crouch = false) { auto_jump_ = jump, auto_crouch_ = crouch; }
    void autopilot_side(float side) { auto_side_ = side; }
    void autopilot_turn(float degrees_per_second) { auto_turn_ = degrees_per_second; }
    void autopilot_walk(bool walk) { auto_walk_ = walk; }
    float view_yaw() const { return yaw_; }
    bool on_ground() const { return move_.on_ground; }
    // Tests: stood on the floor under `at`, looking this way (pitch up positive); on a ladder now;
    // the feet, and the eye as drawn this frame (it trails the feet up a stair).
    void test_stand(const eng::Vec3& at, float yaw, float pitch);
    bool on_ladder() const { return move_.on_ladder; }
    eng::Vec3 feet() const { return move_.origin; }
    eng::Vec3 eye() const { return camera_.eye; }
    // Tests: shots sent since the start, and how far this frame's barrel and the newest local
    // tracer's start were apart on screen (pixels; 0 when they leave from the drawn muzzle).
    int shots_fired() const { return shots_fired_; }
    // Tests: your footsteps heard so far (a stride's sound each; a landing or a rung is not one).
    int steps_heard() const { return steps_heard_; }
    int others_steps_heard() const { return others_steps_; }   // ... and everyone else's
    // Tests: how far each bullet went (degrees) from where you aimed (the kick and the cone together)
    // and from the crosshair (the cone alone), in the order fired; and that list started afresh.
    const std::vector<std::pair<float, float>>& shot_offsets() const { return shot_offsets_; }
    void clear_shot_offsets() { shot_offsets_.clear(); }
    const Loadout& loadout() const { return loadout_; }
    double last_shot_at() const { return muzzle_flash_at_; }
    float pitch() const { return pitch_; }
    std::string body_clips() const {
        auto it = players_.find(me_);
        if (it == players_.end()) return {};
        const eng::Vec3& v = it->second.velocity;
        return it->second.lower_clip + " + " + it->second.upper_clip + " at " + std::to_string(int(std::sqrt(v.x * v.x + v.z * v.z))) +
               " cm/s, legs " + std::to_string(int(it->second.leg_yaw)) + " degrees off the aim";
    }
    float tracer_gap_px() const { return tracer_gap_px_; }
    void toggle_third_person() { third_person_ = !third_person_; }
    // Tests: turn to the longest clear line from here (a run that is not into a wall), and swing
    // the third-person camera round the soldier by this many degrees (0: behind him).
    void face_open_space();
    void test_orbit(float degrees, float pitch = -12.0f) { orbit_ = degrees, orbit_pitch_ = pitch; }
    // Tests: turn to the nearest other soldier standing (false: nobody), what his body plays, and
    // where the others' tracers started (their drawn muzzle, or their eye when no gun was drawn).
    bool look_at_other();
    // Tests: stand `distance` away beside the nearest other soldier (`side` degrees round from his front) and face him.
    bool follow_other(float distance, float side);
    // Tests: stand a few metres from the map's `index`-th glow (sf::LevelMaterial::glow) and face it.
    bool view_glow(int index);
    std::string other_clips() const;
    int tracers_from_muzzle() const { return tracers_muzzle_; }
    int tracers_from_eye() const { return tracers_eye_; }
    // Tests: the gun in hand loaded and up (no reload or draw in the way of the next test).
    void test_full_clip() {
        const WeaponDef* w = my_weapon(slot_);
        if (w) clip_[size_t(slot_)] = w->magazine;
        reload_left_ = draw_left_ = fire_cooldown_ = 0;
    }
    // Tests: the grenade in the loadout out, its pin pulled and thrown (false: none carried).
    bool test_throw();
    // Tests: a hit's blood a metre short of the wall ahead (false: no wall in reach).
    bool test_blood();
    // Tests: a death staged (a killer who is not there, with real-looking facts), and a round's end.
    void test_death();
    void test_round_end();
    // Tests: what is in the air and on the walls now.
    std::string effects_summary() const;
    size_t blast_count() const { return blasts_.size(); }
    std::string lamp_report() const;   // Fidelity: the map's lamps and what last frame drew of them (tests)
    size_t grenades_in_air() const { return grenades_.size(); }
    void test_menu(bool open) { menu_open_ = open; }
    // Tests: stood on your side's spawn of this number, looking along `yaw` (level); with `wall`
    // above 0, walked up to that far (cm) from whatever is ahead. The same place every run.
    void test_place(int spawn, float yaw, float wall, bool toward_sun = false);
    // Stood at the spawn with the clearest view of the map's largest water, facing it (false: no water).
    bool view_water();
    // Tests: the first-person weapon's clip now, and how far into it.
    std::string view_clip() const { return vm_clip_ + " at " + std::to_string(int(vm_time_ * 100.0f)) + " cs"; }
    void set_slot_for_test(int slot) {
        if (slot != slot_) set_slot(slot);
        draw_left_ = 0;
    }
    // Tests: the fourth weapon key pressed (the next throwable carried).
    void test_next_throwable() {
        next_throwable();
        draw_left_ = 0;
    }
    // Tests: the weapon in this slot out and its scope at this step (0 out, 1 first, 2 full).
    void test_scope(int slot, int step) {
        if (slot != slot_) set_slot(slot);
        draw_left_ = 0;
        scoped_ = step > 0, zoom_step_ = step;
    }
    int scoped_slot() const {
        for (int s = 0; s < int(kLoadoutSlots); ++s)
            if (const WeaponDef* w = my_weapon(s); w && w->scoped) return s;
        return -1;
    }
    float view_fov() const { return camera_.fov_x; }
    // Tests: a grenade of this kind thrown from the eye as though from the hand (not counted as yours).
    bool test_throw_kind(GrenadeKind kind);
    bool flashed() const;
    // Tests: the view's kick and the cone now (degrees).
    std::string recoil_summary() const {
        return eng::str::format("view kicked %.2f up, %.2f sideways; cone %.2f degrees", double(kick_pitch_), double(kick_yaw_), double(current_cone()));
    }
    size_t smoke_count() const { return smokes_.size(); }
    // Tests: where you stand, the weapon slot in hand, the radio list open (-1: none) and its picked line.
    eng::Vec3 position() const { return move_.origin; }
    int slot() const { return slot_; }
    int radio_open() const { return radio_menu_; }
    int radio_picked() const { return radio_sel_; }
    bool shadows_cast() const { return shadows_cast_; }   // the sun cast soldiers' shadows last frame
    // Tests: back to the map's sun, seen from behind and above (third person), looking down at
    // the ground ahead, where your shadow falls.
    void test_shadow_view();
    // The game type as the server last told it (World/Objectives.cpp), and what it made of a soldier.
    const proto::ModeState& mode_state() const { return mode_state_; }
    const proto::RoleNow* role_of(u32 id) const;
    bool is_undead(u32 id) const;
    // Tests: stood a few metres from the game type's first objective of this kind, facing it (false: none).
    bool view_objective(Objective kind);
    // Tests: Horror Mode 2's class picked as a player would (the window's keys).
    void test_pick_class(Undead u);
    // Tests: the gun in hand put down, the one at your feet taken up (as F and G do); guns on the floor now.
    void test_drop() { drop_weapon(); }
    bool manning_cannon_for_test() const { return manning_cannon(); }
    size_t balls_for_test() const { return balls_.size(); }
    // Tests: the first cannon ball in the air: how long since it was fired, where it is and where that is on the screen.
    std::string ball_report() const;
    void test_pick_up() { pick_up_weapon(); }
    int weapons_on_floor() const;
    // Tests: the score tab up (as Tab holds it); the HUD hidden (H); the briefing held up (O).
    void test_scores(bool on) { test_scores_ = on; }
    void test_hide_hud(bool hidden) { hud_hidden_ = hidden; }
    void test_briefing(bool on) { briefing_held_ = on; }
    bool hud_hidden() const { return hud_hidden_; }
    // Tests: stood a few metres in front of the nearest undead standing, facing him (false: none).
    bool face_undead();
    // Tests: you drawn as an undead of this class whatever the game made of you: the skill bar, no
    // gun, and Horror Mode 2's select window while picking (the pictures of them; nothing is sent).
    void test_undead_hud(Undead u, bool picking = true) { test_undead_ = u, test_undead_picking_ = picking; }
    // Tests: your spray on the wall ahead, as the Spray key puts it (false: none carried, none
    // left this life, or no wall in reach); the sprays on the walls now.
    bool test_spray() { return spray(); }
    size_t sprays_shown() const { return spray_marks_.size(); }
    const std::string& last_shot() const { return last_shot_; }
    // Tests: the damage numbers over a soldier now ("32 32 = 64"; empty when none).
    std::string damage_numbers_of(u32 id) const;
    int dealt_to(u32 id) const {
        auto it = dealt_to_.find(id);
        return it == dealt_to_.end() ? 0 : it->second.first;
    }

    // The spent cases, marks, puffs and blasts (World/Effects.cpp).
    struct EffectArt {
        const TexInfo* bullet_mark[3] = {};
        const TexInfo* blood_mark[6] = {};
        const TexInfo* blood_puff = nullptr;
        const TexInfo* blood_cake = nullptr;
        const TexInfo* impact_smoke = nullptr;
        const TexInfo* dust = nullptr;
        const TexInfo* scorch = nullptr;
        const TexInfo* fireball = nullptr;
        const TexInfo* flash = nullptr;
        const TexInfo* plume[13] = {};
        const TexInfo* smoke = nullptr;
        const TexInfo* shadow = nullptr;   // effect shadow/shadow.bmp, as a mask: the shade under a soldier
        // Footsteps: dust/dust1..3 off soil, sand and mud, particle_grass, particle_snow, and a splash
        // (bulletcrashsmoke/water_splash) with droplets (particle_water) in water.
        const TexInfo* step_dust[3] = {};
        const TexInfo* grass = nullptr;
        const TexInfo* snow = nullptr;
        const TexInfo* splash = nullptr;
        const TexInfo* drop = nullptr;
    };

private:
    void loader(proto::MatchLoad load);
    bool finish_upload();
    void handle_messages();
    void on_message(std::span<const u8> data);
    void update_local(float dt);
    void update_weapon(float dt);
    void update_remote(float dt);
    void fire();
    void throw_grenade();
    void say_radio(u8 group, u8 line);
    void spawn_local(const proto::Spawn& s);
    eng::Vec3 spawn_point(Team team, u8 index) const;
    void set_slot(int slot);
    // The fourth weapon key: the first throwable carried out; with one in hand, the next carried
    // after it (round to the first).
    void next_throwable();
    bool trace_players(const eng::Vec3& from, const eng::Vec3& dir, float max_dist, u32& victim, proto::HitZone& zone, float& dist) const;
    Animated* view_model_for(u16 weapon);
    // A force in character parts (Game/Items.hpp): its model, and the key the match keeps it by.
    std::shared_ptr<sf::Model> load_force_model(u8 force, const std::vector<u16>& parts);
    static std::string look_of(u8 force, const std::vector<u16>& parts);
    const sf::ModelAnimation* motion(const std::string& clip, const std::string& look) const;
    void pose_body(PlayerView& p, float dt);
    Carried* carried_for(u16 weapon);
    // Your gun in a kit's cell.
    const WeaponDef* my_weapon(int slot) const;
    // The Spray key: your spray on the wall in front of you, once a life (told to the server, which
    // tells everyone); and the sprays on the walls, drawn with the marks.
    bool spray();
    void draw_sprays();
    // Damage numbers (Settings::damage_numbers): each hit of yours over whoever took it, newest at the
    // foot of the hits, your total for him under them all; the lot gone 3 s after your last hit on him.
    struct DamageHit {
        int amount = 0;
        u8 kind = 0;          // 0 a hit, 1 a head shot
        double at = 0;
    };
    struct DamageStack {
        std::deque<DamageHit> hits;
        int total = 0;
        double last = -10;
        eng::Vec3 above;      // over his head, where he was last seen
    };
    std::map<u32, DamageStack> damage_numbers_;
    void note_damage(u32 victim, int amount, u8 kind, double now);
    void draw_damage_numbers(double now);
    // The original's H, B and O (World/Hud.cpp): the HUD off, a line saying what such a key just
    // did ("[Blood effect off]"), and the mission's briefing while the key is held.
    bool hud_hidden_ = false;
    std::string hud_tip_;
    double hud_tip_at_ = -10;
    bool briefing_held_ = false;
    void hud_tip(std::string text);
    void draw_hud_tip(double now);
    void draw_briefing();
    // Turning and looking by key (the arrows), and the view levelled (End).
    void key_look(float dt);
    // `casting`: into the sun's shadow map (WorldRenderer::begin_shadows): the body and the gun in
    // hand, nothing taken from the drawing (his muzzle, his ejection port).
    void draw_player(PlayerView& p, u32 id, double now, bool casting = false);
    // The cone a shot leaves in now (degrees): the gun's rest and what firing and moving added,
    // times running, the air or walking (recoil.kst's ADD_VALUE_*). `shots_share` of what firing
    // added: the crosshair opens to the cone with kCrosshairShots of it.
    float current_cone(float shots_share = 1.0f) const;
    // A burst opened the crosshair as wide as the cone and looked like it was flying apart (players,
    // 2026-10-04: "toned down just a bit"): it shows six tenths of what the shots add. The rest, the
    // moving, the air and a run show whole.
    static constexpr float kCrosshairShots = 0.6f;
    bool binding_down(Action a) const;
    bool binding_pressed(Action a) const;
    // The first-person weapon posed for this frame, its world matrix, and its muzzle in the world's
    // own lens (the view model is drawn with a narrower lens of its own).
    bool pose_view_model();
    void draw_scoreboard();
public:
    // Where the scoreboard's result strip is on the stage (the match over: the Result screen puts its
    // XP, SP and Back to the room in it). False while the board is not up.
    bool result_strip(float& x0, float& y0, float& x1, float& y1) const;
private:
    void draw_menu();
    // The game types (World/Objectives.cpp): the server's word, what you press, what you see.
    void on_mode_state(const proto::ModeState& m);
    void on_mode_event(const proto::ModeEvent& m);
    void update_mode_input(double now);
    void draw_objectives(double now);     // under the clock: the goal, its bar, its timer
    void draw_markers(double now);        // over the world: sites, the bomb, items, zones, captains
    bool me_undead() const;               // you are undead (or test_undead_hud draws you as one)
    void draw_skills(double now);         // an undead's skills and their cooldowns
    void draw_horror_items(double now);   // Horror Mode's items you carry: icons, keys, how many
    // Pirate Mode's cannons (Game/Modes.hpp): whether one is yours, what to press beside one, and
    // the balls in the air.
    bool manning_cannon() const;
    void draw_cannons(double now);
    void draw_class_select(double now);   // Horror Mode 2's select window
    void draw_radar_objectives(VanDrawList* dl, const VanVec2& c, float r, const std::function<VanVec2(const eng::Vec3&)>& to_dial);
    bool to_screen(const eng::Vec3& at, VanVec2& out) const;   // stage coordinates; false behind you
    std::string mode_line() const;
    void draw_chat();
    void draw_radio_menu();
    // The HUD's pieces (World/Hud.cpp), in the original's own art (data/menu, inf/).
    void draw_crosshair(double now);
    void draw_clock();
    void draw_kill_feed(double now);
    void draw_kill_effect(double now);
    void draw_hit_zone(double now);
    void draw_damage_arcs(double now);
    void draw_health();
    void draw_weapon_box();
    // Centred along the top for a moment after a weapon is taken out: a panel a key, the other
    // throwables under the fourth, the one in hand lit (the original's switch bar: inf/weapon/1..4.bmp).
    void draw_weapon_bar(double now);
    void draw_banner(double now);
    // A weapon's picture: 'w' the kill feed's icon, 'h' the change banner, 'b' the ammo box's.
    ui::Picture weapon_art(const WeaponDef& w, char kind);
    // The radar (World/Radar.cpp): the floorplan baked from the collision on the loader thread,
    // uploaded with the rest, drawn at the top left.
    void bake_radar(const sf::Level& level);
    void draw_radar(double now);
    // A dialog over the match (staff, report, vote, settings): the mouse and the keys go to it.
    bool dialog_up() const;
    // The camera on a soldier: their eyes, or behind them (watching a recording, and spectating).
    bool camera_on(u32 id, WatchView view);
    // The soldier whose body is not drawn because the camera is in their head (0: none).
    u32 hidden_body() const;
    // Down (World/Death.cpp): who did it and how, a teammate's view until the next round, the
    // round's summary, and the netgraph.
    void spectate_update();
    void spectate_next(int dir);
    void draw_death(double now);
    void draw_round_summary(double now);
    void draw_netgraph(double now);
    // Watching a recording: the camera, and the HUD (whose soldier, their health, the feed, the score).
    void update_watch(float dt);
    void draw_watch_hud(double now);
    // Effects (World/Effects.cpp): the art found once the textures are up; a spent case thrown
    // from a port (right/up/forward the shooter's), a bullet's mark and dust where it struck the map,
    // blood where a soldier was hit (sprayed on to the wall behind him), a grenade going off.
    void load_effect_art();
    void eject_case(const eng::Vec3& port, const eng::Vec3& right, const eng::Vec3& up, const eng::Vec3& forward, const eng::Vec3& carried_by,
                    u16 weapon);
    void bullet_mark(const eng::Vec3& at, const eng::Vec3& normal);
    void blood(const eng::Vec3& at, const eng::Vec3& dir, bool spray);
    // What a step or a landing kicks up from what is underfoot (a sound material id): dust off soil,
    // sand and mud, grass, snow, a splash in water; nothing off a hard floor.
    void footstep_fx(const eng::Vec3& feet, u8 material, bool landing);
    void detonate(const eng::Vec3& at, u16 weapon);
    void update_effects(float dt, double now);
    void draw_effects(double now);
    void draw_grenades(double now);
    // The shade under each soldier's feet (Settings::Shadows), lighter where the sun casts as well.
    void draw_feet_shadows(double now, bool cast);

    App& app_;
    RoomSettings settings_;
    bool offline_ = false;
    u32 me_ = 0;
    u32 seed_ = 0;

    // Loading.
    std::thread thread_;
    std::atomic<float> progress_{0};
    std::atomic<bool> cpu_done_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex mutex_;
    std::string status_ = "Loading";
    bool upload_started_ = false;
    bool ready_ = false;
    bool announced_ = false;

    // The sound of it all (guns, feet, hits, grenades, the announcer).
    std::unique_ptr<MatchAudio> audio_;

    // The map.
    std::unique_ptr<sf::Level> level_;
    std::unique_ptr<eng::CollisionMesh> collision_;
    WorldRenderer renderer_;
    bool renderer_ok_ = false;
    LevelGpu level_gpu_;
    FrameLight light_;
    // Fidelity: the frame's lights (flashes, blasts, the map's lamps) and the lamps' coronas.
    void collect_lights(double now);
    TexInfo corona_art_;
    int lamps_near_ = 0, lamps_seen_ = 0, coronas_drawn_ = 0;   // last frame's (tests)
    float baked_level_ = -1;   // sf::measure_baked_level, when the hour grades the bake
    Camera camera_;

    // Models (read on the loader thread, uploaded on the main one).
    std::map<u16, std::shared_ptr<sf::Model>> weapon_models_;   // by weapon id
    std::map<u16, Animated> view_models_;
    std::map<std::string, std::shared_ptr<sf::Model>> force_models_;   // by look_of
    std::map<std::string, std::shared_ptr<sf::ModelAnimation>> motions_;   // "clip|look"
    std::map<std::string, std::unique_ptr<ModelGpu>> force_gpu_;
    std::map<std::string, std::shared_ptr<sf::CarriedModel>> carried_models_;   // by weapon.kst OBJECT stem
    std::map<std::string, Carried> carried_;
    // The flare/ art: front flashes (4, 5, 6), the side flame sheet, the third-person flash, the tracer.
    struct FlareArt {
        const TexInfo* star[3] = {};
        const TexInfo* side = nullptr;
        const TexInfo* point = nullptr;
        const TexInfo* tail = nullptr;
    } flare_;

    // Everyone.
    std::map<u32, PlayerView> players_;

    // You.
    MovementDef move_def_;
    MoveState move_;
    MoveState move_prev_;          // a tick ago: the camera is drawn between the two
    float move_acc_ = 0;           // time not yet stepped
    // Stairs: the feet go up (or down) a whole step in one tick; the eye follows over a moment,
    // this far behind them, closing (Quake's and Source's step smoothing). Kept a tick at a time
    // and drawn between the last two, as the feet are: taken off all at once while the drawn feet
    // rise through the tick, the eye would dip a step and come back.
    float step_offset_ = 0, step_offset_prev_ = 0;
    float yaw_ = 0, pitch_ = 0;
    bool alive_ = false;
    int health_ = 0;
    Team team_ = Team::Red;
    Loadout loadout_ = kNoLoadout;
    std::array<int, kLoadoutSlots> clip_{}, reserve_{};
    Loadout own_loadout_ = kNoLoadout;   // the guns you brought (MatchLoad)
    Loadout brought_ = kNoLoadout;   // own_loadout_ as the match began: a new life is in it again
    // Guns on the floor (proto::DropWeapon, PickUpWeapon): F held puts the gun in hand down (on
    // Last weapon's key a tap is that), G takes up the one at your feet.
    double drop_held_at_ = -1;
    void drop_weapon();
    void pick_up_weapon();
    const proto::ObjectiveNow* weapon_at_feet() const;
    void draw_floor_weapons();
    void draw_pick_up_prompt();
    bool sprayed_ = false;                          // your spray is on a wall this life
    std::string last_shot_;   // tests: what the last shot hit, as traced here
    int steps_heard_ = 0;     // tests: your footsteps heard (steps_heard)
    int others_steps_ = 0;    // ... and the others' (others_steps_heard)
    std::vector<std::pair<float, float>> shot_offsets_;   // tests: each bullet's degrees off the aim and the crosshair (shot_offsets)
    struct SprayMark {
        eng::Vec3 at, normal;
        u16 spray = kNoSpray;
        u32 player = 0;
        double time = 0;
    };
    std::vector<SprayMark> spray_marks_;
    std::map<u16, const TexInfo*> spray_art_;
    int slot_ = 0, last_slot_ = 0;   // the kit's cells (0..kLoadoutSlots-1): in hand, and the one before
    double weapon_bar_at_ = -10;      // when a weapon was last taken out: the bar across the top shows a while
    float draw_left_ = 0, reload_left_ = 0, fire_cooldown_ = 0;
    float spread_ = 0, moving_spread_ = 0, recoil_pitch_ = 0, recoil_yaw_ = 0;
    float kick_pitch_ = 0, kick_yaw_ = 0;   // the kick as the view shows it, easing toward recoil_* in a few hundredths of a second
    int kick_side_ = 1;                      // the way this burst's sideways kick drifts (-1 left, 1 right)
    bool trigger_released_ = true;
    bool firing_ = false;          // the trigger held and shots leaving: told to the others
    bool walking_ = false;
    bool auto_run_ = false, auto_fire_ = false, auto_jump_ = false, auto_crouch_ = false, auto_walk_ = false;
    float auto_turn_ = 0, auto_side_ = 0, orbit_ = 0, orbit_pitch_ = -12.0f;
    bool scoped_ = false;
    int zoom_step_ = 0;            // 1: the AWP's first step (half its power), 2: full
    // effect scope/<image>, made ready to draw: a mask's black (the bitmaps with no alpha) turned
    // into alpha; `sides`: its edges are solid, so a screen wider than 4:3 is filled out in black.
    struct ScopeArt {
        ui::Picture pic;
        bool sides = false, reticle = false;
        float lens_w = 1, lens_h = 1;   // the see-through middle's share of the picture, across and down
    };
    std::map<std::string, ScopeArt> scope_art_;
    float zoom_now() const;        // the magnification looked through now (1: none)
    void draw_scope();
    bool third_person_ = false;
    // The view model's clip, and how fast it plays (a reload's and a draw's follow their timers).
    std::string vm_clip_ = "draw";
    u32 swings_ = 0;   // a blade's swings so far: its attack clips take turns
    float vm_time_ = 0, vm_rate_ = 1;
    bool vm_loop_ = false;
    bool vm_moving_ = false;
    eng::Mat4 vm_world_;           // where it is drawn this frame
    bool vm_posed_ = false;
    eng::Vec3 vm_flame_, vm_flame_dir_;   // the muzzle as drawn (the view model's lens)
    eng::Vec3 vm_muzzle_;          // the same muzzle in the world's lens: tracers leave from here
    double muzzle_flash_at_ = -10;
    int muzzle_star_ = 0;
    double last_input_send_ = 0;
    u32 tick_ = 0;
    double died_at_ = -10;
    int shots_fired_ = 0, tracers_muzzle_ = 0, tracers_eye_ = 0;
    u32 watched_ = 0;
    float tracer_gap_px_ = -1;

    // The round and the score.
    float round_time_ = 0;
    u8 round_ = 0;
    u16 red_score_ = 0, blue_score_ = 0;
    std::string banner_;
    double banner_at_ = -10;
    std::deque<KillFeedLine> feed_;
    // The round's challenges you did (proto::SpecialPoint): each a line under the banner for a while.
    struct SpecialLine {
        std::string head, text;
        double time = 0;
    };
    std::deque<SpecialLine> specials_;
    double reborn_at_ = -10;   // your Rebirth went: its art, for a moment
    // Cannon balls on their way (proto::CannonFx): flown as the server flies them, until its burst comes.
    struct BallFx {
        eng::Vec3 origin, velocity;
        double born = 0;
    };
    std::vector<BallFx> balls_;
    // A bullet's streak (flare/tail.bmp), running from the muzzle to where it stopped. A local one
    // takes this frame's drawn barrel as its start, a remote one the shooter's drawn muzzle.
    struct Tracer {
        eng::Vec3 a, b;
        double time;
        bool from_barrel = false;   // yours: anchored to the view model's muzzle when first drawn
        u32 shooter = 0;            // someone else's: anchored to his gun's muzzle when drawn
    };
    std::vector<Tracer> tracers_;
    double hit_marker_ = -10;
    double damaged_at_ = -10;
    eng::Vec3 damage_from_;
    bool scoreboard_ = false;
    bool menu_open_ = false;
    bool control_ = false;
    int radio_menu_ = -1;          // the radio list open (0 Z command, 1 X general, 2 C reply), -1 none
    int radio_sel_ = 0;            // the line a controller's d-pad has picked in it
    u32 pad_eaten_ = 0;            // pad buttons the radio's list took: not their bound actions until let go
    TouchControls touch_;          // a phone's touch screen as the controls (World/TouchControls.hpp)
    bool digits_taken_ = false;    // this frame's number keys picked a radio line or a chat macro
    bool chat_open_ = false;
    std::string chat_text_;
    // Whom the match's chat line goes to: 0 everyone (Enter, F3), 1 the team (Y, F4), 2 the clan
    // (F5), 3 global chat (F6) -- the original's keys; a /command in the line overrides it.
    int chat_scope_ = 0;
    struct Grenade {
        eng::Vec3 pos, vel;
        float fuse = 0;
        u16 weapon = kNoWeapon;
        bool mine = false;
        double bounced_at = -10;
        float tumble = 0;            // radians it has turned in the air
    };
    std::vector<Grenade> grenades_;
    struct Smoke {
        eng::Vec3 pos;
        double until;
        double from = 0;
    };
    std::vector<Smoke> smokes_;
    // The grenade in hand: the pin pulled while the trigger is held (the hold clip), let go when
    // it is released (the throw clip), leaving the hand partway through it.
    bool grenade_held_ = false;
    double throw_at_ = -1;

    EffectArt fx_;
    bool shadows_cast_ = false;   // the sun cast soldiers' shadows this frame (tests read it)
    std::shared_ptr<sf::Model> case_models_[2];   // cartridge, cartridge_sg
    // Pirate Mode: the cannon and its ball (the force archives' own), read with the match.
    std::shared_ptr<sf::Model> cannon_model_, ball_model_;
    std::unique_ptr<ModelGpu> cannon_gpu_, ball_gpu_;
    std::vector<eng::Mat4> cannon_skin_, ball_skin_;   // each at rest (a skinned mesh is drawn with its own pose, or it takes the last one bound)
    void draw_cannon_models(double now);
    std::unique_ptr<ModelGpu> case_gpu_[2];
    struct Case {
        eng::Vec3 pos, vel;
        float pitch = 0, yaw = 0, pitch_rate = 0, yaw_rate = 0;
        double born = 0;
        u8 kind = 0;
        bool resting = false;
        double clinked_at = -10;
    };
    std::vector<Case> cases_;
    struct Mark {
        eng::Vec3 pos, normal;
        const TexInfo* tex = nullptr;
        float size = 0, spin = 0;
        u32 colour = 0;
    };
    std::deque<Mark> marks_;
    struct Puff {
        eng::Vec3 pos, vel;
        const TexInfo* tex = nullptr;
        double at = 0;
        float life = 1, size0 = 10, size1 = 20, spin = 0, spin_rate = 0;
        u32 colour = 0xFFFFFFFFu;
        bool alpha = true;
    };
    std::vector<Puff> puffs_;
    std::array<int, 13> step_fx_{};   // footsteps seen this match, by sound material (tests: effects_summary)
    struct Blast {
        eng::Vec3 pos;
        double at = 0;
        bool frag = true;            // a fragmentation grenade's fire; else the flash-bang's light
    };
    std::vector<Blast> blasts_;
    // The death screen: who killed you, with what, from how far, how much they had left, and what
    // each of you did to the other this life.
    struct DeathInfo {
        u32 killer = 0;
        std::string name;
        u8 colour = 0;
        Team team = Team::None;
        u16 weapon = kNoWeapon;
        u16 flags = 0;
        int killer_hp = -1;
        float distance = 0;
        int taken = 0, hits_taken = 0;   // what they did to you
        int dealt = 0, hits_dealt = 0;   // what you did to them
    };
    std::optional<DeathInfo> death_;
    std::map<u32, std::pair<int, int>> taken_from_, dealt_to_;   // this life: damage, hits
    u32 spectating_ = 0;
    u8 round_reason_ = 0;
    bool test_summary_ = false;   // the round summary in a mode without rounds (tests)
    std::deque<float> net_ping_, net_fps_;
    double net_sampled_ = -1;

    // Watching a recording.
    bool watching_ = false;
    std::deque<std::vector<u8>> watch_inbox_;
    WatchView watch_view_ = WatchView::Chase;
    WatchView spec_view_ = WatchView::Eyes;
    u32 follow_ = 0;
    eng::Vec3 free_eye_;
    float free_yaw_ = 0, free_pitch_ = 0;
    bool free_placed_ = false;
    eng::Vec3 vm_eject_;           // the view model's ejection port, in the world's lens
    bool vm_has_eject_ = false;
    float shake_ = 0;              // a blast close by shakes the view
    double flashed_until_ = -10;
    bool match_over_ = false;

    // The kill effect at the top of the screen: one of the original's own by its table's name
    // (World/KillEffects.hpp: HEADSHOT, MULTIKILL, SPECIALPOINT ...) or WALLSHOT, ours; since when;
    // and whether the kill came through a wall (a line under whatever else it was).
    struct KillFx {
        double at = -10;
        std::string effect;
        bool wall = false;
    } kill_fx_;
    KillEffects kill_effects_;
    void show_kill(u16 kill_flags, u32 victim);   // a kill of yours: its effect and its call
    void show_effect(const char* name, bool wall = false);
    // The body part you last hit on an enemy: the "ENEMY" diagram lights it.
    double zone_hit_at_ = -10;
    u8 zone_hit_ = 0;
    bool zone_hit_kill_ = false;
    // Round results in the original's art (win / lose / draw), when the round was yours to win.
    int result_art_ = -1;   // 0 win, 1 lose, 2 draw
    double result_at_ = -10;

    // The game type (World/Objectives.cpp).
    proto::ModeState mode_state_;
    double mode_state_at_ = -10;
    struct ModeNote {
        std::string text;
        VanU32 colour = 0xFFFFFFFF;
        double at = 0;
    };
    std::deque<ModeNote> mode_notes_;
    void mode_note(std::string text, VanU32 colour);
    int pick_index_ = 0;                 // Horror Mode 2's select window: the class shown
    bool pick_sent_ = false;
    double hosts_turned_at_ = -10;
    double sides_changed_at_ = -10;   // a Team Battle's half: the next round's banner says so
    u16 buttons_ = 0;                    // this tick's buttons (Input carries them)
    bool test_scores_ = false;
    Undead test_undead_ = Undead::None;
    bool test_undead_picking_ = true;

    eng::Image radar_image_;
    ui::Picture radar_pic_;
    eng::Vec3 radar_origin_;
    float radar_w_ = 0, radar_h_ = 0;   // centimetres the floorplan covers
};

}  // namespace lsf
