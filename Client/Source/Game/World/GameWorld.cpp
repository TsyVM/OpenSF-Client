#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Ballistics.hpp"
#include "Game/Items.hpp"
#include "Game/Screens/Screens.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace lsf {

using eng::Mat4;
using eng::Vec3;
using namespace proto;

namespace {

// The arms' clips (u_<nn>_...) by weapon.kst's grip. Read off the force archive's point files: each
// was exported standing in the wait clip of the set its gun is held with (139 of them match one to
// within 2 degrees), so a gun's grip says which set puts both hands on it: 1 pistols u_02, 2 rifles
// and sniper rifles u_05, 3 machine guns u_06, 4 compact rifles u_09, 5 P90 and MP7 u_10, 6 Uzi
// and MAC-10 u_12, 7 pump and bolt guns u_04, 8 two pistols u_13, 9 knives u_01, 10 grenades u_08,
// 11 the Gatling gun u_11.
// A blade's own arm clips (the force archive's motion/u_01_*): how it is held, taken out and swung.
// The M9's are the plain ones (u_01_wait, u_01_take; u_01_shoot00 and u_01_shoot01 by turns); the
// others' carry a name (u_01_wait_ax_hatchet, u_01_shoot_claw ...). No table in the client says
// which blade takes which, so these go by the names, and by the blades that share a model or a
// class in weapon.kst: the shovel with the hatchet (class 12), the 7th Anniversary knives with the
// Shark M10 (its model, reskinned), a machete swung as the jungle machete is and held as the Shark
// M10 (class 11; the hatchet's wait and the Shark's are one file).
struct BladeArms {
    const char* model;
    const char* hold;
    const char* swing;
};
const BladeArms kBladeArms[] = {
    {"ax_hatchet", "ax_hatchet", "ax_hatchet"},       {"shovel", "ax_hatchet", "ax_hatchet"},
    {"flintlock_sword_m", "ax_hatchet", "ax_hatchet"}, {"flintlock_sword_p", "ax_hatchet", "ax_hatchet"},
    {"dragon_claw", "claw", "claw"},                  {"large_scissors", "scissors", "scissors"},
    {"shark_m10", "shark_m10", "shark_m10"},          {"7th_knife_2004", "shark_m10", "shark_m10"},
    {"7th_knife_2006", "shark_m10", "shark_m10"},     {"jungle_machete", "shark_m10", "jungle_machete"},
    {"machete", "shark_m10", "jungle_machete"},
};
const BladeArms* blade_arms(const WeaponDef* w) {
    if (!w || w->klass != WeaponClass::Knife) return nullptr;
    for (const BladeArms& b : kBladeArms)
        if (art_model(*w) == b.model) return &b;
    return nullptr;
}
// How long the M9's swings take in the hand (its view model's shoot and shoot_01): the soldier's
// own clips of them (u_01_shoot00, u_01_shoot01) run two and nearly three times as long, and are
// played at the hand's pace.
constexpr float kPlainSwing[2] = {1.0f, 0.59f};

const char* upper_type(u8 grip) {
    switch (grip) {
        case 1: return "02";
        case 3: return "06";
        case 4: return "09";
        case 5: return "10";
        case 6: return "12";
        case 7: return "04";
        case 8: return "13";
        case 9: return "01";
        case 10: return "08";
        case 11: return "11";
        default: return "05";
    }
}

// The pace each locomotion clip was authored at, in cm/s (the planted foot's speed in it):
// played at the soldier's real speed over this, his feet keep their grip on the floor.
constexpr float kRunPace = 385, kBackPace = 285, kWalkPace = 115, kCrouchPace = 80;
// The way a soldier faces in his own frame: Soldier Front's -Z (his head leans that way, his gun
// points that way, a walk's planted foot slides the other way), which the (-z, y, x) turn into the
// engine's frame makes +X. Drawn at his yaw less this, he faces where he aims; drawn at his yaw
// as though he faced +Z (as the match did before), every soldier ran a quarter turn sideways.
constexpr float kBodyFacing = 90.0f;
// Others are drawn this far behind the newest snapshot (they come twenty times a second).
constexpr double kInterpDelay = 0.1;
// A bullet's streak: how fast it runs and how long it is (cm).
constexpr float kTracerSpeed = 16000, kTracerLength = 260, kTracerWidth = 4.0f;

float frand(std::mt19937& rng, float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

std::mt19937 g_rng{12345};

float flat_speed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

Vec3 origin_of(const Mat4& m) { return {m.m[3][0], m.m[3][1], m.m[3][2]}; }

// Turns `bone` about the model's up axis (yaw, degrees) and then about the axis across a body that
// faces +X (pitch, degrees, up positive: +X lifts toward +Y), about the bone's own origin, by
// changing its local transform. The bones' model-space matrices are brought up to date first.
void turn_bone(const sf::Model& m, std::vector<Mat4>& local, std::vector<Mat4>& ms, int bone, float yaw_deg, float pitch_deg) {
    if (bone < 0 || size_t(bone) >= local.size()) return;
    sf::pose_to_model(m, local, ms);
    const Mat4& cur = ms[size_t(bone)];
    const Vec3 o = origin_of(cur);
    const Mat4 rot = Mat4::yaw(yaw_deg) * Mat4::rotation_z(pitch_deg * eng::kDegToRad);
    const Mat4 want = cur * Mat4::translation(-o) * rot * Mat4::translation(o);
    const int parent = m.bones[size_t(bone)].parent;
    local[size_t(bone)] = parent >= 0 ? want * ms[size_t(parent)].inverse() : want;
}

// A stick as the game reads it: `dead` more of its travel ignored (the pad's own dead zone is
// already out), the rest run from nothing to full, squared when `smooth` (fine near the centre).
void shape_stick(float& x, float& y, float dead, bool smooth) {
    const float len = std::sqrt(x * x + y * y);
    if (len < 1e-4f) {
        x = y = 0;
        return;
    }
    float t = std::clamp((std::min(len, 1.0f) - dead) / std::max(0.05f, 1.0f - dead), 0.0f, 1.0f);
    if (smooth) t *= t;
    x = x / len * t;
    y = y / len * t;
}

// Moves `v` toward `target` by at most `step`.
float approach(float v, float target, float step) { return v < target ? std::min(target, v + step) : std::max(target, v - step); }

}  // namespace

GameWorld::GameWorld(App& app) : app_(app), audio_(std::make_unique<MatchAudio>(app.sounds())) {}

GameWorld::~GameWorld() { release(); }

void GameWorld::release() {
    if (thread_.joinable()) thread_.join();
    app_.window().set_mouse_captured(false);
#ifndef _WIN32
    app_.window().touch_is_mouse = true;
#endif
    app_.sounds().stop_world();
}

void GameWorld::start_loading(const MatchLoad& load, bool offline) {
    settings_ = load.settings;
    offline_ = offline;
    me_ = load.you;
    seed_ = load.seed;
    g_rng.seed(load.seed ^ 0x5F17);
    third_person_ = false;
    for (const MatchPlayer& mp : load.players) {
        PlayerView p;
        p.id = mp.id;
        p.name = mp.name;
        p.team = Team(mp.team);
        p.force = mp.force;
        p.loadout = mp.loadout;
        p.xp = mp.xp;
        p.name_colour = mp.name_colour;
        p.marks = mp.marks;
        p.parts = mp.parts;
        p.look = look_of(mp.force, mp.parts);
        p.own = p.brought = mp.loadout;
        p.spray = mp.spray;
        players_[p.id] = std::move(p);
        if (mp.id == me_) {
            team_ = Team(mp.team);
            loadout_ = mp.loadout;
            own_loadout_ = brought_ = mp.loadout;
        }
    }
    thread_ = std::thread([this, load] { loader(load); });
}

float GameWorld::progress() const {
    const float cpu = progress_;
    if (!cpu_done_) return cpu * 0.7f;
    return 0.7f + 0.3f * (ready_ ? 1.0f : renderer_.level_progress());
}

std::string GameWorld::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string GameWorld::look_of(u8 force, const std::vector<u16>& parts) {
    std::vector<u16> mine;
    for (u16 p : parts)
        if (const ItemDef* d = item(p); d && d->kind == ItemKind::Part && d->force == force) mine.push_back(p);
    std::sort(mine.begin(), mine.end());
    std::string key = std::to_string(force);
    for (u16 p : mine) key += "," + std::to_string(p);
    return key;
}

std::shared_ptr<sf::Model> GameWorld::load_force_model(u8 id, const std::vector<u16>& parts) {
    const ForceDef* def = force(id);
    std::vector<std::string> worn;
    for (u16 p : parts)
        if (const ItemDef* d = item(p); d && d->kind == ItemKind::Part && d->force == id) worn.push_back(sf::lower(d->model));
    if (!def) def = &forces()[0];
    // The forces as they are now: a server's own come and go with it (MT-4).
    static std::mutex m;
    static std::vector<sf::ForceSet> sets;
    static u32 epoch = ~0u;
    {
        std::lock_guard lock(m);
        if (sets.empty() || epoch != app_.content_epoch()) sets = sf::discover_forces(app_.data()), epoch = app_.content_epoch();
    }
    for (const sf::ForceSet& s : sets)
        if (s.id == def->model) {
            std::string err;
            if (auto model = sf::load_force(app_.data(), s, "", worn, &err)) return std::make_shared<sf::Model>(std::move(*model));
            LOG_WARN("Force %s: %s", def->name, err.c_str());
        }
    return nullptr;
}

void GameWorld::loader(MatchLoad load) {
    auto say = [&](float p, std::string s) {
        {
            std::lock_guard lock(mutex_);
            status_ = std::move(s);
        }
        progress_ = p;
    };
    say(0.02f, "Reading " + app_.map_title(settings_.map));
    std::string err;
    auto level = sf::load_level(app_.data(), settings_.map, sf::kLevelAll, &err);
    if (!level) {
        LOG_ERROR("Map %s: %s", settings_.map.c_str(), err.c_str());
        say(1.0f, "The map could not be read: " + err);
        failed_ = true;
        cpu_done_ = true;
        return;
    }
    say(0.35f, "Building the collision");
    auto coll = std::make_unique<eng::CollisionMesh>();
    std::vector<u8> surfaces(level->collision_surfaces.size());
    for (size_t i = 0; i < surfaces.size(); ++i) surfaces[i] = u8(level->collision_surfaces[i]);
    coll->build(level->collision_vertices, level->collision_indices, surfaces);
    bake_radar(*level);
    light_.sun_direction = level->sun_direction;
    light_.sun_colour = level->sun_colour;
    light_.ambient = level->ambient;
    light_.exposure = app_.settings().brightness;
    // The other hour than the bake's is a grade, normalised by how bright the bake is.
    if ((settings_.time_of_day == TimeOfDay::Night) != baked_at_night(settings_.map)) {
        say(0.4f, "Setting the hour");
        baked_level_ = sf::measure_baked_level(app_.data(), *level);
    }

    say(0.5f, "Dressing the soldiers");
    // One model a look: a force, in whatever parts its soldier wears.
    std::map<std::string, std::pair<u8, std::vector<u16>>> looks_needed;
    for (const auto& [id, p] : players_) looks_needed.emplace(p.look.empty() ? look_of(p.force, {}) : p.look, std::make_pair(p.force, p.parts));
    std::map<std::string, std::shared_ptr<sf::Model>> force_models;
    for (const auto& [key, look] : looks_needed)
        if (auto m = load_force_model(look.first, look.second)) force_models[key] = m;

    say(0.65f, "Checking the weapons");
    const auto sets = sf::discover_weapons(app_.data());
    auto load_sf = [&](const std::string& id) -> std::shared_ptr<sf::Model> {
        for (const sf::WeaponSet& s : sets)
            if (s.id == id) {
                std::string e;
                if (auto m = sf::load_weapon(app_.data(), s, 0, &e)) return std::make_shared<sf::Model>(std::move(*m));
                LOG_WARN("Weapon %s: %s", id.c_str(), e.c_str());
            }
        return nullptr;
    };
    std::map<u16, std::shared_ptr<sf::Model>> weapon_models;
    // Yours, and every primary and sidearm in the match: any of them may be put down and taken up.
    std::vector<u16> first_person(loadout_.begin(), loadout_.end());
    for (const MatchPlayer& mp : load.players)
        for (size_t k = 0; k < 2; ++k)
            if (std::find(first_person.begin(), first_person.end(), mp.loadout[k]) == first_person.end()) first_person.push_back(mp.loadout[k]);
    int n = 0;
    for (u16 wid : first_person) {
        const WeaponDef* w = weapon(wid);
        say(0.65f + 0.2f * float(n++) / float(std::max<size_t>(1, first_person.size())), w ? "Checking the " + w->name : "Checking the weapons");
        if (!w) continue;
        if (auto m = load_sf(w->model)) {
            if (!w->skin.empty()) {
                // A skin laid over the gun's own texture (the admin variants).
                for (sf::ModelMesh& mesh : m->meshes)
                    if (mesh.name.starts_with("gun:") && mesh.material < m->materials.size()) {
                        sf::ModelMaterial& mat = m->materials[mesh.material];
                        if (auto where = app_.data().resolve_any_extension(sf::Pack::Weapon, "bhw/sf_a_texture/" + w->skin + ".jpg")) {
                            sf::ModelMaterial skin = mat;
                            skin.where = *where;
                            skin.texture = app_.data().key(sf::Pack::Weapon, *where);
                            mesh.material = u32(m->materials.size());
                            m->materials.push_back(skin);
                        }
                    }
            }
            weapon_models[wid] = m;
        }
    }

    // What everyone is seen holding: each loadout's weapons, by weapon.kst's OBJECT.
    say(0.86f, "Arming the soldiers");
    std::map<std::string, std::shared_ptr<sf::CarriedModel>> carried;
    for (const MatchPlayer& mp : load.players)
        for (u16 wid : mp.loadout)
            if (const WeaponDef* w = weapon(wid); w && !w->carried.empty() && !carried.contains(w->carried)) {
                std::string e;
                auto c = sf::load_carried(app_.data(), w->carried, &e);
                if (!c) LOG_WARN("Carried %s: %s", w->carried.c_str(), e.c_str());
                carried[w->carried] = c ? std::make_shared<sf::CarriedModel>(std::move(*c)) : nullptr;
            }

    // The spent cases every gun throws out (World/Effects.cpp).
    std::shared_ptr<sf::Model> cases[2];
    for (int k = 0; k < 2; ++k) {
        std::string e;
        if (auto m = sf::load_cartridge(app_.data(), k ? "cartridge_sg" : "cartridge", &e)) cases[k] = std::make_shared<sf::Model>(std::move(*m));
        else LOG_WARN("Cartridge: %s", e.c_str());
    }

    // The Pirate Ship's cannons and their balls (World/Objectives.cpp).
    std::shared_ptr<sf::Model> cannon, ball;
    if (settings_.mode == Mode::Pirate && !pirate_cannons(settings_.map).empty()) {
        std::string e;
        if (auto m = sf::load_force_prop(app_.data(), "weapon/sf_c_cannon_body", "weapon/sf_c_cannon_bone", &e)) cannon = std::make_shared<sf::Model>(std::move(*m));
        else LOG_WARN("Cannon: %s", e.c_str());
        if (auto m = sf::load_force_prop(app_.data(), "weapon/sf_a_cannon_bullet", {}, &e)) ball = std::make_shared<sf::Model>(std::move(*m));
        else LOG_WARN("Cannon ball: %s", e.c_str());
    }

    say(0.9f, "Teaching the soldiers to move");
    std::map<std::string, std::shared_ptr<sf::ModelAnimation>> motions;
    // The original's clips (Docs/Research.md §10): l_ the legs (us standing, ud the stand-to-crouch
    // change, ds crouched, uwf walking, urf running, urb running back, dwf crouch-walking, uj the
    // jump), a_ the whole body (ud standing deaths, dd crouched deaths), u_<nn>_ the arms by weapon.
    std::vector<std::string> clips = {"l_us", "l_ud", "l_ds", "l_uwf_01", "l_urf_01", "l_urb_01", "l_dwf_01", "l_uj_00", "l_uj_01"};
    for (int i = 0; i <= 6; ++i) clips.push_back(eng::str::format("a_ud_%02d", i));
    for (int i = 0; i <= 2; ++i) clips.push_back(eng::str::format("a_dd_%02d", i));
    for (const char* t : {"01", "02", "04", "05", "06", "08", "09", "10", "11", "12", "13"})
        for (const char* a : {"wait", "take", "shoot00", "shoot00_stop", "shoot", "shoot_stop", "shoot01", "shoot01_stop", "shoot_r",
                              "reload00", "reload"})
            clips.push_back(std::string("u_") + t + "_" + a);
    for (const BladeArms& b : kBladeArms)
        for (const std::string& c : {std::string("wait_") + b.hold, std::string("take_") + b.hold, std::string("shoot_") + b.swing})
            if (std::find(clips.begin(), clips.end(), "u_01_" + c) == clips.end()) clips.push_back("u_01_" + c);
    for (const auto& [key, model] : force_models)
        for (const std::string& c : clips)
            if (auto anim = sf::load_character_motion(app_.data(), *model, c))
                motions[c + "|" + key] = std::make_shared<sf::ModelAnimation>(std::move(*anim));

    // Horror's and Horror Mode 2's undead (Game/Modes.hpp Undead): each body and its own clips, kept
    // under the names a soldier's are (pose_body asks for "l_urf_01", "u_09_wait", "a_ud_03" ...):
    // their legs as theirs, their arms' every grip their claws' (u_01), their deaths theirs.
    if (settings_.mode == Mode::Horror || settings_.mode == Mode::Horror2) {
        say(0.92f, "Raising the dead");
        const std::vector<Undead> bodies = settings_.mode == Mode::Horror ? std::vector<Undead>{Undead::Man, Undead::Woman}
                                                                         : std::vector<Undead>{Undead::Boss, Undead::Driller, Undead::Heavy, Undead::Hunter};
        auto undead_clip = [](const std::string& c) -> std::string {
            if (c.starts_with("l_")) {
                std::string u = c;
                for (const char* s : {"uwf", "urf", "urb", "dwf"})
                    if (u == std::string("l_") + s + "_01") return std::string("l_") + s + "01";
                return u;
            }
            if (c.starts_with("a_")) return c;
            if (c.starts_with("u_") && c.size() > 5) {
                const std::string act = c.substr(5);
                if (act == "wait" || act == "take") return "u_01_wait";
                if (act == "shoot00" || act == "shoot" || act == "shoot_r") return "u_01_shoot";
                if (act == "shoot01") return "u_01_shoot01";
            }
            return {};
        };
        for (Undead u : bodies) {
            const UndeadDef& d = undead_def(u);
            std::string e;
            auto model = sf::load_undead(app_.data(), d.model, &e);
            if (!model) {
                LOG_WARN("Undead %s: %s", d.model, e.c_str());
                continue;
            }
            const std::string key = std::string("undead:") + d.model;
            auto shared = std::make_shared<sf::Model>(std::move(*model));
            for (const std::string& c : clips)
                if (const std::string uc = undead_clip(c); !uc.empty())
                    if (auto anim = sf::load_undead_motion(app_.data(), *shared, d.motion, uc))
                        motions[c + "|" + key] = std::make_shared<sf::ModelAnimation>(std::move(*anim));
            force_models[key] = shared;
        }
    }

    say(0.95f, "Tuning the sounds");
    std::vector<u16> held(loadout_.begin(), loadout_.end());
    for (const MatchPlayer& mp : load.players) held.insert(held.end(), mp.loadout.begin(), mp.loadout.end());
    std::sort(held.begin(), held.end());
    held.erase(std::unique(held.begin(), held.end()), held.end());
    audio_->prepare(*level, held);

    {
        std::lock_guard lock(mutex_);
        level_ = std::make_unique<sf::Level>(std::move(*level));
        collision_ = std::move(coll);
        force_models_ = std::move(force_models);
        weapon_models_ = std::move(weapon_models);
        carried_models_ = std::move(carried);
        motions_ = std::move(motions);
        case_models_[0] = cases[0], case_models_[1] = cases[1];
        cannon_model_ = cannon, ball_model_ = ball;
        status_ = "Putting the textures on the card";
    }
    progress_ = 1.0f;
    cpu_done_ = true;
}

bool GameWorld::finish_upload() {
    if (!cpu_done_ || failed_) return false;
    if (!upload_started_) {
        upload_started_ = true;
        if (thread_.joinable()) thread_.join();
        renderer_ok_ = renderer_.init(app_.device());
        renderer_.textures().init(&app_.device(), &app_.data());
        if (!renderer_ok_) {
            failed_ = true;
            return false;
        }
        renderer_.begin_level(*level_, level_gpu_);
        renderer_.set_time_of_day(settings_.time_of_day, baked_at_night(settings_.map), baked_level_);
        if (ui::Atlas* a = ui::atlas(); a && radar_image_.width) radar_pic_ = a->image_picture("radar", radar_image_);
    }
    if (!renderer_.step_level()) return false;
    // Models: every force in the match, your weapons, and what everyone carries.
    for (auto& [key, model] : force_models_)
        if (!force_gpu_.contains(key)) force_gpu_[key] = renderer_.upload_model(*model, sf::Pack::Force);
    for (auto& [wid, model] : weapon_models_) {
        if (view_models_.contains(wid)) continue;
        Animated a;
        a.model = model;
        a.gpu = renderer_.upload_model(*model, sf::Pack::Weapon);
        for (const sf::ModelMesh& mesh : model->meshes) {
            if (mesh.name.starts_with("hands:")) a.hands_box.add(mesh.bounds);
            if (mesh.name.starts_with("gun:") || mesh.name.starts_with("wr:")) a.gun_box.add(mesh.bounds);
        }
        view_models_[wid] = std::move(a);
    }
    for (const WeaponDef& w : weapons())
        if (!w.carried.empty() && carried_models_.contains(w.carried)) (void)carried_for(w.id);
    // The flare/ art the flashes and the tracers are drawn with.
    TextureCache& tc = renderer_.textures();
    const char* stars[] = {"flare/4.tga", "flare/5.tga", "flare/6.tga"};
    for (int i = 0; i < 3; ++i) flare_.star[i] = &tc.get(sf::Pack::Weapon, std::nullopt, stars[i]);
    flare_.side = &tc.get(sf::Pack::Weapon, std::nullopt, "flare/sf_muzzle.tga");
    flare_.point = &tc.get(sf::Pack::Weapon, std::nullopt, "flare/sf_point_fire01.tga");
    flare_.tail = &tc.get(sf::Pack::Weapon, std::nullopt, "flare/tail.bmp");
    load_effect_art();
    for (auto& [id, p] : players_) {
        if (p.look.empty()) p.look = look_of(p.force, p.parts);
        auto it = force_models_.find(p.look);
        if (it == force_models_.end()) continue;
        p.body.model = it->second;
    }
    ready_ = true;
    LOG_INFO("Match world ready: %s, %zu textures, %zu carried models", settings_.map.c_str(), renderer_.textures().size(), carried_.size());
    if (watching_) {
        // A recording: nobody to spawn and nothing to tell; its messages bring the match.
    } else if (!offline_) {
        app_.session().load_done();
    } else {
        Spawn s;
        s.player = me_;
        s.team = u8(team_);
        s.index = 0;
        s.loadout = loadout_;
        spawn_local(s);
        round_time_ = float(settings_.minutes) * 60.0f;
    }
    return true;
}

Carried* GameWorld::carried_for(u16 wid) {
    const WeaponDef* w = weapon(wid);
    if (!w || w->carried.empty()) return nullptr;
    if (auto it = carried_.find(w->carried); it != carried_.end()) return it->second.gpu ? &it->second : nullptr;
    // Not read with the match (someone joined later): read now, once.
    auto m = carried_models_.find(w->carried);
    if (m == carried_models_.end()) {
        std::string e;
        auto c = sf::load_carried(app_.data(), w->carried, &e);
        if (!c) LOG_WARN("Carried %s: %s", w->carried.c_str(), e.c_str());
        m = carried_models_.emplace(w->carried, c ? std::make_shared<sf::CarriedModel>(std::move(*c)) : nullptr).first;
    }
    Carried& c = carried_[w->carried];
    if (!m->second || !renderer_ok_) return nullptr;
    c.model = m->second;
    c.gpu = renderer_.upload_model(c.model->model, sf::Pack::Force);
    std::vector<Mat4> local(c.model->model.bones.size()), ms;
    for (size_t i = 0; i < local.size(); ++i) local[i] = c.model->model.bones[i].bind_local;
    sf::pose_to_model(c.model->model, local, ms);
    if (const sf::ModelSocket* flame = c.model->model.socket("flame"); flame && size_t(flame->bone) < ms.size()) {
        c.flame = flame->bone;
        c.flame_at = ms[size_t(flame->bone)];
    }
    if (const sf::ModelSocket* port = c.model->model.socket("cartridge"); port && size_t(port->bone) < ms.size()) {
        c.cartridge = port->bone;
        c.cartridge_at = ms[size_t(port->bone)];
    }
    // Thrown, it is drawn on its own at a grenade's size: an M67 stands about 11 cm.
    const eng::Aabb box = c.model->model.bounds();
    c.centre = box.valid() ? (box.min + box.max) * 0.5f : Vec3{};
    const Vec3 ext = box.valid() ? box.max - box.min : Vec3{1, 1, 1};
    c.scale = 11.0f / std::max({ext.x, ext.y, ext.z, 1e-3f});
    return c.gpu ? &c : nullptr;
}

eng::Vec3 GameWorld::spawn_point(Team team, u8 index) const {
    std::vector<const sf::LevelSpawn*> list;
    const bool ffa = !mode_info(settings_.mode).teams;
    if (settings_.mode == Mode::Horror2)
        for (const sf::LevelSpawn& s : team == Team::Red ? level_->zombie_spawns : level_->human_spawns) list.push_back(&s);
    if (list.empty())
        for (const sf::LevelSpawn& s : level_->spawns) {
            if (ffa ? s.team == sf::Team::Any : (team == Team::Red ? s.team == sf::Team::Red : s.team == sf::Team::Blue)) list.push_back(&s);
        }
    if (list.empty())
        for (const sf::LevelSpawn& s : level_->spawns) list.push_back(&s);
    if (list.empty()) return level_->bounds.valid() ? (level_->bounds.min + level_->bounds.max) * 0.5f : Vec3{};
    return list[index % list.size()]->position;
}

void GameWorld::spawn_local(const Spawn& s) {
    std::vector<const sf::LevelSpawn*> list;
    const bool ffa = !mode_info(settings_.mode).teams;
    // Horror Mode 2's maps have spawns of their own for the undead and the humans.
    if (settings_.mode == Mode::Horror2)
        for (const sf::LevelSpawn& sp : Team(s.team) == Team::Red ? level_->zombie_spawns : level_->human_spawns) list.push_back(&sp);
    if (list.empty())
        for (const sf::LevelSpawn& sp : level_->spawns)
            if (ffa ? sp.team == sf::Team::Any : (Team(s.team) == Team::Red ? sp.team == sf::Team::Red : sp.team == sf::Team::Blue)) list.push_back(&sp);
    if (list.empty())
        for (const sf::LevelSpawn& sp : level_->spawns) list.push_back(&sp);
    Vec3 at{};
    float yaw = 0;
    if (s.here) {
        // Horror: risen where he fell, or turned where he stood.
        at = s.at;
        yaw = s.yaw;
    } else if (!list.empty()) {
        const sf::LevelSpawn& sp = *list[s.index % list.size()];
        at = sp.position;
        yaw = sp.yaw;
    }
    move_ = MoveState{};
    move_.origin = collision_ ? settle_spawn(move_def_, *collision_, at) : at;
    move_prev_ = move_;
    move_acc_ = 0;
    yaw_ = yaw;
    pitch_ = 0;
    alive_ = true;
    sprayed_ = false;   // a spray a life
    health_ = s.health;
    taken_from_.clear();
    dealt_to_.clear();
    death_.reset();
    spectating_ = 0;
    team_ = Team(s.team);
    loadout_ = s.loadout;
    own_loadout_ = brought_;
    for (int i = 0; i < int(kLoadoutSlots); ++i) {
        const WeaponDef* w = my_weapon(i);
        clip_[size_t(i)] = w ? w->magazine : 0;
        reserve_[size_t(i)] = w ? w->reserve : 0;
    }
    slot_ = 0;
    while (slot_ < int(kLoadoutSlots) - 1 && loadout_[size_t(slot_)] == kNoWeapon) ++slot_;
    set_slot(slot_);
    spread_ = moving_spread_ = 0;
    recoil_pitch_ = recoil_yaw_ = kick_pitch_ = kick_yaw_ = 0;
}

void GameWorld::set_slot(int slot) {
    if (slot < 0 || slot >= int(kLoadoutSlots) || loadout_[size_t(slot)] == kNoWeapon) return;
    if (slot != slot_) weapon_bar_at_ = app_.now();
    // A pin pulled and the weapon changed: the grenade is kept (a throw already let go still flies).
    grenade_held_ = false;
    if (throw_at_ >= 0) {
        throw_at_ = -1;
        throw_grenade();
    }
    if (slot != slot_) last_slot_ = slot_;
    slot_ = slot;
    const WeaponDef* w = my_weapon(slot);
    draw_left_ = w ? w->draw_time : 0.4f;
    // Quick Weapon Switch (the Item Shop's): drawn in half the time.
    if (app_.session().has_boost(Boost::QuickSwitch, app_.now())) draw_left_ *= 0.5f;
    reload_left_ = 0;
    audio_->reload(me_, kNoWeapon, 0, nullptr);   // a reload cut short stops its sounds
    if (w) audio_->draw(me_, w->id, nullptr);
    scoped_ = false;
    vm_clip_ = "draw";
    vm_time_ = 0;
    vm_loop_ = false;
    vm_moving_ = false;
    // The draw clip lasts as long as the weapon takes to come up.
    vm_rate_ = 1;
    if (Animated* vm = view_model_for(loadout_[size_t(slot)]); vm && vm->model && draw_left_ > 0.05f)
        if (const sf::ModelAnimation* a = vm->model->animation("draw")) vm_rate_ = std::max(0.1f, a->duration / draw_left_);
    if (players_.contains(me_)) players_[me_].drew_at = app_.now();
}

void GameWorld::next_throwable() {
    // A throwable with nothing left in it is passed over; none with any: nothing happens.
    auto has_some = [&](size_t c) { return loadout_[c] != kNoWeapon && (clip_[c] > 0 || reserve_[c] > 0); };
    const size_t from = throw_cell(size_t(slot_)) ? size_t(slot_) + 1 : kFirstThrowCell;
    for (size_t i = 0; i < kLoadoutSlots - kFirstThrowCell; ++i) {
        const size_t c = kFirstThrowCell + (from - kFirstThrowCell + i) % (kLoadoutSlots - kFirstThrowCell);
        if (has_some(c)) {
            if (int(c) != slot_) set_slot(int(c));
            else weapon_bar_at_ = app_.now();   // the only one: the bar says so
            return;
        }
    }
}

// An action is on when its key is, or its button on the controller in hand (a button the radio's
// list took stays its until let go), or its button on the touch screen.
bool GameWorld::binding_down(Action a) const {
    if (touch_.down(a)) return true;
    if (const unsigned key = app_.settings().bind(a); key && app_.window().input().binding_down(key)) return true;
    const eng::Gamepad* pad = app_.pad_in_hand();
    const unsigned button = app_.settings().pad.binds[size_t(a)];
    return pad && button && !(pad_eaten_ & button) && pad->down(button);
}
bool GameWorld::binding_pressed(Action a) const {
    if (touch_.pressed(a)) return true;
    if (const unsigned key = app_.settings().bind(a); key && app_.window().input().binding_pressed(key)) return true;
    const eng::Gamepad* pad = app_.pad_in_hand();
    const unsigned button = app_.settings().pad.binds[size_t(a)];
    return pad && button && !(pad_eaten_ & button) && pad->pressed(button);
}

// ── Messages ───────────────────────────────────────────────────────────────────

void GameWorld::handle_messages() {
    auto& q = watching_ ? watch_inbox_ : app_.session().match_events;
    while (!q.empty()) {
        std::vector<u8> msg = std::move(q.front());
        q.pop_front();
        on_message(msg);
    }
}

void GameWorld::on_message(std::span<const u8> data) {
    const double now = app_.now();
    switch (peek_id(data)) {
        case Msg::MatchBegin: {
            if (!announced_) {
                announced_ = true;
                banner_ = mode_name(settings_.mode);
                banner_at_ = now;
            }
            break;
        }
        case Msg::RoundStart: {
            RoundStart m;
            if (!decode(data, m)) break;
            round_ = m.round;
            round_time_ = m.seconds;
            banner_ = mode_info(settings_.mode).rounds ? eng::str::format("ROUND %u", unsigned(m.round)) : std::string(mode_name(settings_.mode));
            if (now - sides_changed_at_ < 3.0) banner_ = "SIDES CHANGED  -  " + banner_;
            banner_at_ = now;
            break;
        }
        case Msg::Spawn: {
            Spawn m;
            if (!decode(data, m)) break;
            auto& p = players_[m.player];
            p.id = m.player;
            p.alive = true;
            p.health = m.health;
            p.team = Team(m.team);
            p.force = m.force;
            p.loadout = m.loadout;
            // His body: his force's, or the undead one the game type made him (World/Objectives.cpp).
            {
                const RoleNow* r = role_of(m.player);
                const std::string look = r && Undead(r->undead) != Undead::None ? std::string("undead:") + undead_def(Undead(r->undead)).model : look_of(p.force, p.parts);
                if (look != p.look || !p.body.model) {
                    p.look = look;
                    if (auto it = force_models_.find(p.look); it != force_models_.end()) p.body.model = it->second;
                    p.lower_clip.clear(), p.upper_clip.clear();
                }
            }
            p.escaped = false;
            p.own = p.brought;
            const Vec3 at = m.here ? m.at : spawn_point(Team(m.team), m.index);
            // A new life starts where it starts: nothing to draw him sliding from.
            p.position = at;
            p.samples.clear();
            p.death_clip.clear();
            if (m.player == me_ && ready_) spawn_local(m);
            break;
        }
        case Msg::Snapshot: {
            Snapshot m;
            if (!decode(data, m)) break;
            round_time_ = m.round_time;
            for (const PlayerSnap& s : m.players) {
                if (s.id == me_) {
                    // The server's word on health and life; the position is ours.
                    health_ = s.health;
                    if (!(s.flags & kFlagAlive) && alive_) alive_ = false;
                    continue;
                }
                auto& p = players_[s.id];
                p.id = s.id;
                if (s.flags & kFlagHidden) {
                    // Out of sight: alive or not is all that is said; the trail starts afresh on return.
                    p.hidden = true;
                    p.alive = s.flags & kFlagAlive;
                    p.samples.clear();
                    p.snap_time = 0;
                    continue;
                }
                p.hidden = false;
                // A reload begun, a weapon drawn: heard where they stand.
                if (p.snap_time > 0 && p.alive && (s.flags & kFlagAlive)) {
                    if ((s.flags & kFlagReloading) && !(p.flags & kFlagReloading)) {
                        if (const WeaponDef* w = weapon(s.weapon)) audio_->reload(s.id, s.weapon, w->reload_time, &s.position);
                    } else if (s.weapon != p.weapon && p.weapon != kNoWeapon && s.weapon != kNoWeapon) {
                        audio_->draw(s.id, s.weapon, &s.position);
                    }
                }
                // Back from the dead somewhere else: start his trail afresh.
                if ((s.flags & kFlagAlive) && !p.alive) p.samples.clear();
                p.samples.push_back({now, s.position, s.velocity, s.yaw, s.pitch, s.flags});
                while (p.samples.size() > 32) p.samples.pop_front();
                if (p.snap_time <= 0) p.position = s.position, p.yaw = s.yaw;
                p.alive = s.flags & kFlagAlive;
                p.health = s.health;
                p.weapon = s.weapon;
                p.snap_time = now;
            }
            break;
        }
        case Msg::Damage: {
            Damage m;
            if (!decode(data, m)) break;
            if (m.victim == me_) {
                health_ = m.health;
                damaged_at_ = now;
                if (!watching_) app_.rumble(0.75f, 0.18f);
                damage_from_ = m.from;
                if (m.attacker != me_) taken_from_[m.attacker].first += m.amount, ++taken_from_[m.attacker].second;
            }
            if (m.attacker == me_ && m.victim != me_) dealt_to_[m.victim].first += m.amount, ++dealt_to_[m.victim].second;
            if (m.attacker == me_ && m.victim != me_ && m.amount > 0) note_damage(m.victim, m.amount, HitZone(m.zone) == HitZone::Head ? 1 : 0, now);
            if (m.attacker == me_ && m.victim != me_) {
                hit_marker_ = now;
                zone_hit_at_ = now, zone_hit_ = m.zone, zone_hit_kill_ = false;
            }
            if (auto it = players_.find(m.victim); it != players_.end()) it->second.health = m.health;
            if (m.amount > 0) {
                const bool you = m.victim == me_;
                Vec3 at = move_.origin;
                bool woman = false;
                if (auto it = players_.find(m.victim); it != players_.end()) {
                    if (!you) at = it->second.position;
                    const ForceDef* f = force(it->second.force);
                    woman = f && std::string_view(art_model(*f)) == "mulan";
                }
                audio_->body_hit(at + Vec3{0, 110, 0}, HitZone(m.zone), you, woman, false);
                // Blood where he was hit (yours were drawn when you fired), splashed on what is
                // behind him; seen from your own eyes, only the splash.
                if (m.attacker != me_ || m.victim == me_) {
                    const auto it = players_.find(m.victim);
                    const float h = it != players_.end() ? hull_height_by_flags(move_def_, it->second.flags) : move_def_.stand_height;
                    const float up = HitZone(m.zone) == HitZone::Head ? h - 14 : HitZone(m.zone) == HitZone::Legs ? h * 0.3f : h * 0.68f;
                    const Vec3 hit = at + Vec3{0, up, 0};
                    blood(hit, hit - m.from, !you || third_person_);
                }
            }
            break;
        }
        case Msg::Kill: {
            Kill m;
            if (!decode(data, m)) break;
            KillFeedLine line;
            auto name = [&](u32 id) {
                auto it = players_.find(id);
                return it == players_.end() ? std::string("?") : it->second.name;
            };
            line.killer = m.killer == m.victim ? std::string() : name(m.killer);
            line.victim = name(m.victim);
            line.killer_id = m.killer, line.victim_id = m.victim;
            line.weapon_id = m.weapon;
            if (auto it = players_.find(m.killer); it != players_.end()) line.killer_team = u8(it->second.team), line.killer_colour = it->second.name_colour;
            if (auto it = players_.find(m.victim); it != players_.end()) {
                line.victim_team = u8(it->second.team);
                line.victim_colour = it->second.name_colour;
                it->second.alive = false;
            }
            const WeaponDef* w = weapon(m.weapon);
            line.weapon = (m.flags & kKillFall) ? "fell" : w ? w->name : "";
            line.flags = m.flags;
            line.time = now;
            feed_.push_back(line);
            while (feed_.size() > 6) feed_.pop_front();
            if (m.killer == me_ && m.victim != me_) {
                show_kill(m.flags, m.victim);
                zone_hit_at_ = now, zone_hit_kill_ = true;
                if (m.flags & kKillHeadshot) zone_hit_ = u8(HitZone::Head);
            }
            if (m.victim == me_) {
                alive_ = false;
                died_at_ = now;
                // The death screen's facts, taken now: they change once the killer moves on.
                DeathInfo d;
                d.killer = m.killer == m.victim ? 0 : m.killer;
                d.name = line.killer;
                d.colour = line.killer_colour;
                d.team = Team(line.killer_team);
                d.weapon = m.weapon;
                d.flags = m.flags;
                d.killer_hp = d.killer ? int(m.killer_health) : -1;
                if (auto it = players_.find(m.killer); it != players_.end() && d.killer && !it->second.hidden)
                    d.distance = eng::length(it->second.position - move_.origin) / 100.0f;
                if (auto it = taken_from_.find(m.killer); it != taken_from_.end()) d.taken = it->second.first, d.hits_taken = it->second.second;
                if (auto it = dealt_to_.find(m.killer); it != dealt_to_.end()) d.dealt = it->second.first, d.hits_dealt = it->second.second;
                death_ = d;
                spectating_ = 0;
            }
            break;
        }
        case Msg::SpecialPointMsg: {
            SpecialPoint m;
            if (!decode(data, m) || m.player != me_ || m.kind >= u8(Special::Count)) break;
            specials_.push_back({eng::str::format("SPECIAL POINT  +%u SP", unsigned(m.sp)), special_info(Special(m.kind)).name, now});
            while (specials_.size() > 4) specials_.pop_front();
            break;
        }
        case Msg::CannonFx: {
            CannonFx m;
            if (!decode(data, m)) break;
            balls_.push_back({m.origin, m.velocity, now});
            if (const WeaponDef* boom = weapon_by_model("m67")) audio_->explosion(boom->id, m.origin);   // the cannon's report
            if (m.by == me_) shake_ = std::max(shake_, 0.5f);
            break;
        }
        case Msg::HorrorItemUsed: {
            // Horror Mode's items: yours spent (what is left of it), or somebody else's told in the notes.
            HorrorItemUsed m;
            if (!decode(data, m) || m.item >= kHorrorItems) break;
            const HorrorItemInfo& info = horror_item(HorrorItem(m.item));
            if (m.player == me_) {
                app_.session().horror_items[m.item] = m.left;
                if (HorrorItem(m.item) == HorrorItem::Rebirth) reborn_at_ = now;
                specials_.push_back({eng::str::upper(info.name), info.about, now});
                while (specials_.size() > 4) specials_.pop_front();
            } else if (auto it = players_.find(m.player); it != players_.end()) {
                mode_note(it->second.name + (HorrorItem(m.item) == HorrorItem::Rebirth ? " is reborn" : std::string(" used ") + info.name), ui::col(ui::pal().gold));
            }
            break;
        }
        case Msg::Rearm: {
            // A gun put down or taken up: the slot now holds this, as it was made, with these rounds.
            Rearm m;
            if (!decode(data, m) || m.slot >= kLoadoutSlots) break;
            const size_t k = m.slot;
            if (auto it = players_.find(m.player); it != players_.end()) {
                it->second.loadout[k] = m.weapon;
                if (m.weapon != kNoWeapon) it->second.own[k] = m.weapon;
            }
            if (m.player != me_) break;
            loadout_[k] = m.weapon;
            if (m.weapon == kNoWeapon) {
                // Put down: in hand next whatever else is carried.
                if (slot_ == int(k))
                    for (int s = 0; s < int(kLoadoutSlots); ++s)
                        if (loadout_[size_t(s)] != kNoWeapon) {
                            set_slot(s);
                            break;
                        }
                break;
            }
            own_loadout_[k] = m.weapon;
            clip_[k] = m.clip, reserve_[k] = m.reserve;
            // Taken up: to hand at once (the original's auto switch), or slung where the player has
            // that off -- unless it took the place of what was in hand, or the hands were empty.
            if (app_.settings().auto_switch || slot_ == int(k) || loadout_[size_t(slot_) % kLoadoutSlots] == kNoWeapon) {
                set_slot(int(k));
                audio_->draw(me_, m.weapon, nullptr);
            }
            break;
        }
        case Msg::SprayFx: {
            // A soldier's spray on a wall: his newest takes the place of the one before.
            SprayFx m;
            if (!decode(data, m) || !lsf::spray(m.spray)) break;
            std::erase_if(spray_marks_, [&](const SprayMark& s) { return s.player == m.player; });
            spray_marks_.push_back({m.at, m.normal, m.spray, m.player, now});
            if (spray_marks_.size() > 64) spray_marks_.erase(spray_marks_.begin());
            if (!spray_art_.contains(m.spray)) spray_art_[m.spray] = &renderer_.textures().get_keyed(sf::Pack::Effect, lsf::spray(m.spray)->texture);
            (void)audio_->sounds().play_named("Spray", "Default", &m.at);
            break;
        }
        case Msg::ShotFx: {
            ShotFx m;
            if (!decode(data, m)) break;
            // From the shooter's drawn muzzle when it is drawn this frame; his eye otherwise.
            Tracer t{m.origin, m.end, now};
            t.shooter = m.shooter;
            tracers_.push_back(t);
            if (auto it = players_.find(m.shooter); it != players_.end()) {
                PlayerView& p = it->second;
                if (now - p.fired_at > 0.35) p.burst_from = now;
                p.fired_at = now;
                if (m.shooter != me_) ++p.swings;   // one's own are counted where they are made
                // His spent case, from his gun's port as last drawn.
                if (p.has_eject && now - p.muzzle_at < 0.25) {
                    const Vec3 fwd = eng::angles_to_forward(p.yaw, p.pitch), right = eng::yaw_to_right(p.yaw);
                    eject_case(p.eject, right, Vec3{0, 1, 0}, fwd, p.velocity, m.weapon);
                }
            }
            audio_->fire(m.shooter, m.weapon, &m.origin);
            // Where it stopped, when the map stopped it: the sound, and the mark and the dust.
            if (const Vec3 d = m.end - m.origin; collision_ && eng::length(d) > 1.0f) {
                const Vec3 dir = eng::normalize(d);
                const WeaponDef* w = weapon(m.weapon);
                const bool blade = w && w->klass == WeaponClass::Knife;
                if (const eng::TraceResult tr = collision_->trace_ray(m.end - dir * 20.0f, m.end + dir * 20.0f); tr.hit()) {
                    audio_->impact(m.end, dir, blade);
                    if (!blade && !tr.start_solid) bullet_mark(tr.end, tr.normal);
                }
            }
            break;
        }
        case Msg::ModeStateMsg: {
            ModeState m;
            if (decode(data, m)) on_mode_state(m);
            break;
        }
        case Msg::ModeEventMsg: {
            ModeEvent m;
            if (decode(data, m)) on_mode_event(m);
            break;
        }
        case Msg::GrenadeFx: {
            GrenadeFx m;
            if (!decode(data, m)) break;
            if (m.exploded) {
                // Gone off where it was put (an undead's Black Fog, a cannon ball's burst): no flight.
                if (!balls_.empty()) {
                    auto nearest = balls_.end();
                    float best = 600.0f;
                    for (auto it = balls_.begin(); it != balls_.end(); ++it)
                        if (const float d = eng::length(cannon_ball_at(it->origin, it->velocity, float(now - it->born)) - m.origin); d < best) best = d, nearest = it;
                    if (nearest != balls_.end()) balls_.erase(nearest);
                }
                detonate(m.origin, m.weapon);
            } else {
                const WeaponDef* w = weapon(m.weapon);
                grenades_.push_back({m.origin, m.velocity, w ? w->fuse : 2.0f, m.weapon, false});
                audio_->fire(m.thrower, m.weapon, &m.origin);
                if (auto it = players_.find(m.thrower); it != players_.end()) it->second.fired_at = it->second.burst_from = now;
            }
            break;
        }
        case Msg::RoundEnd: {
            RoundEnd m;
            if (!decode(data, m)) break;
            red_score_ = m.red_wins;
            blue_score_ = m.blue_wins;
            round_reason_ = m.reason;
            banner_ = Team(m.winner) == Team::Red ? "RED TEAM WINS THE ROUND" : Team(m.winner) == Team::Blue ? "BLUE TEAM WINS THE ROUND" : "DRAW";
            banner_at_ = now;
            // Yours to win or lose: the original's art says which.
            if (team_ == Team::Red || team_ == Team::Blue) {
                result_art_ = Team(m.winner) == Team::None ? 2 : Team(m.winner) == team_ ? 0 : 1;
                result_at_ = now;
            }
            break;
        }
        case Msg::Score: {
            Score m;
            if (!decode(data, m)) break;
            red_score_ = m.red;
            blue_score_ = m.blue;
            for (const ScoreRow& r : m.rows)
                if (auto it = players_.find(r.id); it != players_.end()) {
                    it->second.kills = r.kills, it->second.deaths = r.deaths, it->second.assists = r.assists;
                    it->second.score = r.score, it->second.ping = r.ping, it->second.extra = r.extra, it->second.headshots = r.headshots;
                }
            break;
        }
        case Msg::MatchOver: {
            match_over_ = true;
            MatchOver m;
            if (decode(data, m)) {
                for (const ScoreRow& r : m.rows)
                    if (auto it = players_.find(r.id); it != players_.end()) {
                        it->second.kills = r.kills, it->second.deaths = r.deaths, it->second.assists = r.assists, it->second.score = r.score;
                        it->second.extra = r.extra, it->second.headshots = r.headshots;
                    }
                const Team me = team_;
                banner_ = !mode_info(settings_.mode).teams ? (m.winner_player == me_ ? "YOU WIN" : "YOU LOSE")
                          : Team(m.winner) == Team::None ? "DRAW"
                          : Team(m.winner) == me          ? "YOU WIN"
                                                          : "YOU LOSE";
                // Horror's sides change every round: whether the game was yours is the server's to say.
                if (mode_info(settings_.mode).goal == GoalKind::RoundsPlayed)
                    for (const Reward& rw : m.rewards)
                        if (rw.id == me_) banner_ = rw.won ? "YOU WIN" : "YOU LOSE";
                banner_at_ = now;
            }
            if (watching_) break;   // a recording ends where it ends; the viewer stays
            app_.window().set_mouse_captured(false);
            app_.go(Screen::Result);
            break;
        }
        case Msg::ChatLine:
            break;   // the session keeps the room's chat; the HUD reads it
        case Msg::RadioFx: {
            RadioFx m;
            if (!decode(data, m)) break;
            bool woman = false;
            if (auto it = players_.find(m.speaker); it != players_.end())
                if (const ForceDef* f = force(it->second.force)) woman = std::string_view(art_model(*f)) == "mulan";
            audio_->set_radio_language(Settings::radio_voice_folder(app_.settings().radio_voice));
            audio_->radio(m.group, m.line, woman);
            break;
        }
        default:
            break;
    }
}

// ── Simulation ─────────────────────────────────────────────────────────────────

void GameWorld::update(float dt) {
    if (!ready_) {
        finish_upload();
        return;
    }
    handle_messages();
    const double now = app_.now();
    audio_->update(now);
    if (round_time_ > 0) round_time_ = std::max(0.0f, round_time_ - dt);
    // A streak is gone once its tail has reached where the bullet stopped.
    std::erase_if(tracers_, [&](const Tracer& t) { return float(now - t.time) * kTracerSpeed > eng::length(t.b - t.a) + kTracerLength || now - t.time > 1.5; });
    std::erase_if(smokes_, [&](const Smoke& s) { return now > s.until; });
    for (auto& g : grenades_) {
        // Thrown grenades fly with gravity and bounce off the map.
        g.fuse -= dt;
        Vec3 next = g.pos + g.vel * dt;
        g.vel.y -= 980.0f * dt;
        if (collision_) {
            const eng::TraceResult tr = collision_->trace_ray(g.pos, next);
            if (tr.hit()) {
                if (eng::length(g.vel) > 150.0f && now - g.bounced_at > 0.12) {
                    audio_->grenade_bounce(tr.end, eng::normalize(g.vel));
                    g.bounced_at = now;
                }
                next = tr.end + tr.normal * 2.0f;
                g.vel = (g.vel - tr.normal * (2.0f * eng::dot(g.vel, tr.normal))) * 0.4f;
            }
        }
        g.pos = next;
    }
    for (auto& g : grenades_) {
        if (g.fuse > 0) continue;
        // Anyone's grenade going off: what it looks like, and a flash-bang blinds you whoever threw it.
        const WeaponDef* w = weapon(g.weapon);
        if (!w) continue;
        detonate(g.pos, g.weapon);
        if (w->grenade == GrenadeKind::Flash && alive_) {
            const Vec3 to = g.pos - camera_.eye;
            const float d = eng::length(to);
            if (d < w->blast_radius && eng::dot(eng::normalize(to), camera_.forward()) > 0.2f &&
                !(collision_ && collision_->trace_ray(camera_.eye, g.pos + Vec3{0, 10, 0}).hit()))
                flashed_until_ = std::max(flashed_until_, now + 3.0 * (1.0 - d / w->blast_radius));
        }
        if (!g.mine) continue;
        // Your own grenade went off: report who it caught.
        Shoot s;
        s.tick = tick_;
        s.weapon = g.weapon;
        s.origin = g.pos;
        s.direction = {0, 1, 0};
        s.end = g.pos;
        if (w->damage > 0) {
            for (const auto& [id, p] : players_) {
                if (!p.alive && id != me_) continue;
                const Vec3 at = id == me_ ? move_.origin : p.position;
                if (eng::length(at - g.pos) > w->blast_radius) continue;
                if (collision_ && collision_->trace_ray(g.pos + Vec3{0, 10, 0}, at + Vec3{0, 90, 0}).hit()) continue;
                s.hits.push_back({id, u8(HitZone::Chest), at});
            }
        }
        if (!offline_) app_.session().send(s);
    }
    for (const Grenade& g : grenades_)
        if (g.fuse <= 0) audio_->explosion(g.weapon, g.pos);
    std::erase_if(grenades_, [](const Grenade& g) { return g.fuse <= 0; });
    update_effects(dt, now);

    if (watching_) {
#ifndef _WIN32
        app_.window().touch_is_mouse = true;   // a spectator's fingers work the watch's own plates
#endif
        update_remote(dt);
        update_watch(dt);
        return;
    }
    update_local(dt);
    update_mode_input(app_.now());
    update_weapon(dt);
    update_remote(dt);
}

void GameWorld::update_local(float dt) {
    eng::Input& in = app_.window().input();
    const bool typing = chat_open_ || VanGui::GetIO().WantTextInput;
    // The touch screen (a phone): its fingers read before anything asks for an action. Its Menu is
    // Esc's, its Chat is Enter's; a controller or a key used puts it away (Back, Esc, does not).
    {
        TouchControls::Context tc;
        tc.control = !typing && !menu_open_ && !match_over_ && !dialog_up();
        tc.alive = alive_;
        const WeaponDef* tw = my_weapon(slot_);
        tc.scope = tw && tw->scoped;
        const eng::Gamepad* tp = app_.pad_in_hand();
        const u32 first = in.first_pressed();
        tc.other_input = (tp && tp->active()) || (first != 0 && first < 0x100 && first != VK_ESCAPE);
        touch_.update(app_.window(), app_.settings().touch, tc);
        if (touch_.menu_pressed()) menu_open_ = !menu_open_;
        if (touch_.chat_pressed()) chat_open_ = true, chat_scope_ = 0;
    }
    // Esc: the menu (and the mouse back).
    if (in.key_pressed(VK_ESCAPE) && !chat_open_) menu_open_ = !menu_open_;
    if (!typing && !menu_open_) {
        const int scope = in.key_pressed(VK_RETURN) || in.key_pressed(VK_F3) ? 0 : in.key_pressed('Y') || in.key_pressed(VK_F4) ? 1
                          : in.key_pressed(VK_F5)                                    ? 2
                          : in.key_pressed(VK_F6)                                    ? 3
                                                                                     : -1;
        if (scope >= 0) chat_open_ = true, chat_scope_ = scope;
    }
    scoreboard_ = !typing && binding_down(Action::ScoreView);
    const bool control = control_ = !typing && !menu_open_ && !match_over_ && !dialog_up();
    app_.window().set_mouse_captured(control && app_.window().focused());
#ifndef _WIN32
    // The fingers are the controls while they show; the menus' pointer otherwise.
    app_.window().touch_is_mouse = !(control && touch_.shown());
#endif

    if (control) {
        // The mouse turns you on the ground and in the air alike.
        // Through a scope the aim slows by its magnification, times the table's SCOPE_MOVEFACTOR
        // (and the player's own scope speed, the options').
        float scope_slow = 1.0f;
        if (const WeaponDef* sw = my_weapon(slot_); scoped_ && sw)
            scope_slow = std::clamp(sw->scope_move / std::max(1.0f, zoom_now()), 0.05f, 1.0f) * app_.settings().scope_speed;
        const float sens = 0.022f * 3.0f * app_.settings().sensitivity * scope_slow;
        yaw_ = eng::wrap_degrees(yaw_ + in.mouse_dx() * sens);
        pitch_ = std::clamp(pitch_ - in.mouse_dy() * sens * (app_.settings().invert_mouse ? -1.0f : 1.0f), -88.0f, 88.0f);
        // A controller in hand: its looking stick turns you at a rate (a mouse turns by distance),
        // slowed through a scope the same. No aim assist: the stick is all there is.
        const eng::Gamepad* pad = app_.pad_in_hand();
        const PadSettings& ps = app_.settings().pad;
        if (pad) {
            float lx = ps.swap_sticks ? pad->lx() : pad->rx(), ly = ps.swap_sticks ? pad->ly() : pad->ry();
            shape_stick(lx, ly, ps.dead_look, ps.smooth);
            const float rate = kPadTurnRate * ps.look * scope_slow * dt;
            yaw_ = eng::wrap_degrees(yaw_ + lx * rate);
            pitch_ = std::clamp(pitch_ + ly * rate * ps.look_vertical * (ps.invert_y ? -1.0f : 1.0f), -88.0f, 88.0f);
        }
        // A finger swiped: turned as far as it went (as a mouse turns by distance), slowed through
        // a scope the same, up and down as the mouse's.
        if (touch_.look_x() != 0 || touch_.look_y() != 0) {
            yaw_ = eng::wrap_degrees(yaw_ + touch_.look_x() * scope_slow);
            pitch_ = std::clamp(pitch_ - touch_.look_y() * scope_slow * (app_.settings().invert_mouse ? -1.0f : 1.0f), -88.0f, 88.0f);
        }
        // The radio's list on a pad: the d-pad moves the line picked, A (Cross) says it, B (Circle)
        // puts the list away; while it is up those four are the list's, whatever they are bound to.
        for (u32 b = 1; b <= eng::kPadRT; b <<= 1)
            if ((pad_eaten_ & b) && !(pad && pad->down(b))) pad_eaten_ &= ~b;
        if (pad && radio_menu_ >= 0) {
            const int lines = std::max(1, std::min(10, radio_count(u8(radio_menu_))));
            radio_sel_ = std::clamp(radio_sel_, 0, lines - 1);
            if (pad->pressed(eng::kPadDown)) radio_sel_ = (radio_sel_ + 1) % lines;
            if (pad->pressed(eng::kPadUp)) radio_sel_ = (radio_sel_ + lines - 1) % lines;
            for (const u32 b : {u32(eng::kPadUp), u32(eng::kPadDown), u32(eng::kPadA), u32(eng::kPadB)})
                if (pad->down(b)) pad_eaten_ |= b;
            if (pad->pressed(eng::kPadA)) {
                say_radio(u8(radio_menu_), u8(radio_sel_));
                radio_menu_ = -1;
            } else if (pad->pressed(eng::kPadB)) {
                radio_menu_ = -1;
            }
        }
        if (binding_pressed(Action::CameraMode) && settings_.third_person) third_person_ = !third_person_;
        // H: the HUD off (and the line saying how to have it back); B: blood on or off, kept.
        if (binding_pressed(Action::HideHud)) {
            hud_hidden_ = !hud_hidden_;
            const std::string key = eng::Input::binding_name(app_.settings().bind(Action::HideHud));
            hud_tip(hud_hidden_ ? "[HUD off]   Press " + key + " to bring it back." : std::string("[HUD on]"));
        }
        if (binding_pressed(Action::Blood)) {
            Settings& s = app_.settings();
            s.blood = !s.blood;
            (void)s.save();
            hud_tip(s.blood ? "[Blood effect on]" : "[Blood effect off]");
        }
        briefing_held_ = binding_down(Action::Objective);
        if (binding_pressed(Action::CenterView)) pitch_ = 0;
        key_look(dt);
        // Radio: Z command, X general, C reply open their lists; a number key says a line
        // (config.cfg's RADIOMESSAGE_* and MESSAGE_1..0). The same key again closes it.
        const Action radios[3] = {Action::RadioCommand, Action::RadioGeneral, Action::RadioReply};
        for (int g = 0; g < 3; ++g)
            if (binding_pressed(radios[g])) radio_menu_ = radio_menu_ == g ? -1 : g, radio_sel_ = 0;
        digits_taken_ = radio_menu_ >= 0 || in.key_down(VK_MENU);
        auto digit = [&](int i) { return in.key_pressed(u32(i < 9 ? '1' + i : '0')); };
        if (radio_menu_ >= 0) {
            for (int i = 0; i < radio_count(u8(radio_menu_)) && i < 10; ++i)
                if (digit(i)) {
                    say_radio(u8(radio_menu_), u8(i));
                    radio_menu_ = -1;
                    break;
                }
        } else if (in.key_down(VK_MENU)) {
            // Chat macros: Alt and a number says the line set for it in the options.
            for (int i = 0; i < 10; ++i)
                if (digit(i) && !app_.settings().macros[size_t(i)].empty() && !offline_)
                    app_.session().chat(ChatScope::All, app_.settings().macros[size_t(i)]);
        }
    } else {
        radio_menu_ = -1;
        digits_taken_ = false;
    }
    if (auto_turn_ != 0) yaw_ = eng::wrap_degrees(yaw_ + auto_turn_ * dt);
    MoveCmd cmd;
    cmd.yaw = yaw_;
    cmd.pitch = pitch_;
    if (control && alive_) {
        // The original's keys (data/config.cfg): Shift crouches, Ctrl walks, unless rebound.
        cmd.forward = (binding_down(Action::Go) ? 1.0f : 0.0f) - (binding_down(Action::Back) ? 1.0f : 0.0f);
        cmd.side = (binding_down(Action::StrafeRight) ? 1.0f : 0.0f) - (binding_down(Action::StrafeLeft) ? 1.0f : 0.0f);
        // The moving stick, as far as it is pushed.
        if (const eng::Gamepad* pad = app_.pad_in_hand()) {
            const PadSettings& ps = app_.settings().pad;
            float mx = ps.swap_sticks ? pad->rx() : pad->lx(), my = ps.swap_sticks ? pad->ry() : pad->ly();
            shape_stick(mx, my, ps.dead_move, false);
            cmd.forward = std::clamp(cmd.forward + my, -1.0f, 1.0f);
            cmd.side = std::clamp(cmd.side + mx, -1.0f, 1.0f);
        }
        // The touch stick: its direction at full length (how far it is pushed says walk or run, below).
        if (touch_.moving()) {
            cmd.forward = std::clamp(cmd.forward + touch_.forward(), -1.0f, 1.0f);
            cmd.side = std::clamp(cmd.side + touch_.side(), -1.0f, 1.0f);
        }
        if (binding_down(Action::Jump)) cmd.buttons |= kButtonJump;
        if (binding_down(Action::Knee)) cmd.buttons |= kButtonCrouch;
        if (binding_down(Action::Use)) cmd.buttons |= kButtonUse;
        if (binding_pressed(Action::Spray)) (void)spray();
        // The original's "always run": on, the Walk key walks; off, you walk and the key runs.
        if (binding_down(Action::Walk) == app_.settings().always_run) cmd.buttons |= kButtonWalk;
        // The touch stick pushed part way walks (quietly, as the Walk key); all the way, it runs.
        if (touch_.moving()) {
            if (touch_.walking()) cmd.buttons |= kButtonWalk;
            else cmd.buttons &= u16(~u16(kButtonWalk));
        }
    }
    if (auto_run_) cmd.forward = 1.0f;
    if (auto_side_ != 0) cmd.side = auto_side_;
    if (auto_jump_) cmd.buttons |= kButtonJump;
    if (auto_crouch_) cmd.buttons |= kButtonCrouch;
    if (auto_walk_) cmd.buttons |= kButtonWalk;
    walking_ = cmd.buttons & kButtonWalk;
    buttons_ = cmd.buttons;
    constexpr float kStep = 1.0f / 64.0f;
    if (alive_ && collision_) {
        const WeaponDef* w = my_weapon(slot_);
        const ForceDef* f = force(players_.contains(me_) ? players_[me_].force : 0);
        // The force's speed and the parts' (shown as +0.1 a tenth of a point: +1 %).
        float parts_speed = 0;
        if (players_.contains(me_)) parts_speed = part_totals(players_[me_].parts, players_[me_].force).speed;
        float speed = (w ? w->move_speed : 1.0f) * (f ? f->speed : 1.0f) * std::clamp(1.0f + parts_speed * 0.1f, 0.8f, 1.2f);
        // An undead runs at its class's pace; Super Speed half again, Super Jump half as high again.
        MovementDef def = move_def_;
        if (const RoleNow* r = role_of(me_)) {
            if (Undead(r->undead) != Undead::None) speed = undead_def(Undead(r->undead)).speed;
            if (r->flags & kRoleSpeed) speed *= 1.5f;
            if (r->flags & kRoleJump) def.jump_speed *= 1.45f;
        }
        // Fixed steps so a slow frame moves the same as a fast one; the camera is drawn between
        // the last two, so at any frame rate it glides instead of stepping 64 times a second.
        move_acc_ += dt;
        int n = 0;
        while (move_acc_ >= kStep && n < 8) {
            move_prev_ = move_;
            step_offset_prev_ = step_offset_;
            simulate_move(move_, cmd, kStep, def, *collision_, speed);
            // A stair stepped up or down (Movement.hpp stair_rise), taken by the eye over a moment
            // instead: it catches the feet up in about a tenth of a second, never more than
            // kStairEyeTrail steps behind on a long flight run up fast.
            float offset = step_offset_;
            if (move_prev_.on_ground && move_.on_ground) offset -= stair_rise(def, move_prev_.origin, move_.origin);
            const float trail = def.step_height * kStairEyeTrail;
            step_offset_ = std::clamp(offset, -trail, trail) * std::exp(-kStairEyeCatchUp * kStep);
            if (std::fabs(step_offset_) < 0.05f) step_offset_ = 0;
            move_acc_ -= kStep;
            ++tick_;
            ++n;
        }
        if (n == 8) move_acc_ = 0;   // a long stall: the rest is dropped, not caught up in a burst
        const MatchAudio::Step step =
            audio_->track(me_, move_.origin, move_.velocity, move_.on_ground, move_.on_ladder, (cmd.buttons & kButtonWalk) || move_.ducked, true, app_.now());
        if (step.step) ++steps_heard_;
        if (step.step || step.landing) footstep_fx(move_.origin, step.material, step.landing);
    } else {
        move_prev_ = move_;
        move_acc_ = 0;
        step_offset_ = step_offset_prev_ = 0;
    }
    const float alpha = std::clamp(move_acc_ / kStep, 0.0f, 1.0f);
    MoveState shown = move_;
    shown.origin = eng::lerp(move_prev_.origin, move_.origin, alpha);
    shown.duck_amount = eng::lerpf(move_prev_.duck_amount, move_.duck_amount, alpha);
    shown.tuck_amount = eng::lerpf(move_prev_.tuck_amount, move_.tuck_amount, alpha);
    // Where the camera is.
    camera_.yaw = yaw_ + kick_yaw_;
    camera_.pitch = pitch_ + kick_pitch_;
    if (shake_ > 0) {
        // A blast close by.
        const float t = float(app_.now());
        camera_.yaw += std::sin(t * 61.0f) * shake_ * 1.6f;
        camera_.pitch += std::sin(t * 47.0f + 1.3f) * shake_ * 1.2f;
    }
    camera_.fov_x = app_.settings().fov;
    if (scoped_)
        camera_.fov_x = 2.0f * std::atan(std::tan(camera_.fov_x * 0.5f * eng::kDegToRad) / std::max(1.0f, zoom_now())) / eng::kDegToRad;
    Vec3 eye = eye_position(move_def_, shown);
    eye.y += eng::lerpf(step_offset_prev_, step_offset_, alpha);
    if (!alive_ && app_.now() - died_at_ < 6.0) {
        // The death: the view sinks to the floor.
        eye.y -= std::min(1.0f, float(app_.now() - died_at_) * 1.5f) * (move_def_.stand_eye - 30.0f);
    }
    // A scope is looked through from the eye, whatever the camera mode.
    if (third_person_ && alive_ && !scoped_) {
        // Tests may swing the camera round to see the soldier from the side.
        if (orbit_ != 0) camera_.yaw = yaw_ + orbit_, camera_.pitch = orbit_pitch_;
        const Vec3 back = camera_.forward() * (orbit_ != 0 ? -230.0f : -180.0f) + Vec3{0, 30, 0};
        Vec3 want = eye + back;
        if (collision_) {
            const eng::TraceResult tr = collision_->trace_ray(eye, want);
            if (tr.hit()) want = eye + (want - eye) * std::max(0.1f, tr.fraction - 0.05f);
        }
        eye = want;
    }
    camera_.eye = eye;
    camera_.update(app_.picture_aspect());
    app_.sounds().set_listener(camera_.eye, camera_.yaw);
    // Down in a round with no respawn: a teammate's view, after a moment on your own (Death.cpp).
    if (!alive_ && !mode_info(settings_.mode).respawn && !match_over_ && app_.now() - died_at_ > 2.5) spectate_update();

    const u16 flags = u16((alive_ ? kFlagAlive : 0) | (move_.ducked ? kFlagCrouched : 0) | (move_.on_ground ? kFlagOnGround : 0) |
                          (move_.on_ladder ? kFlagOnLadder : 0) |
                        (reload_left_ > 0 ? kFlagReloading : 0) | (scoped_ ? kFlagScoped : 0) | (walking_ ? kFlagWalking : 0) |
                        (firing_ ? kFlagFiring : 0));
    // Tell the server where we are, 30 times a second.
    if (!offline_ && app_.now() - last_input_send_ >= 1.0 / 30.0) {
        last_input_send_ = app_.now();
        Input msg;
        msg.tick = tick_;
        msg.position = move_.origin;
        msg.velocity = move_.velocity;
        msg.yaw = yaw_;
        msg.pitch = pitch_;
        msg.flags = flags;
        msg.weapon_slot = u8(slot_);
        msg.buttons = buttons_;
        app_.session().send(msg, false);
    }
    if (players_.contains(me_)) {
        auto& p = players_[me_];
        p.position = shown.origin;
        p.velocity = move_.velocity;
        p.yaw = yaw_;
        p.pitch = pitch_;
        p.flags = flags;
        p.weapon = loadout_[size_t(slot_)];
        p.alive = alive_;
        p.health = health_;
        // Your own body (seen from behind, and its shadow) moves as the others' do. In first person
        // it is posed all the same when it casts a shadow from the sun: a shadow that glides along
        // without walking is the first thing anyone notices.
        if (third_person_ || app_.settings().shadows == Settings::Shadows::Sun) pose_body(p, dt);
    }
}

void GameWorld::hud_tip(std::string text) {
    hud_tip_ = std::move(text);
    hud_tip_at_ = app_.now();
}

void GameWorld::key_look(float dt) {
    // Not while Horror Mode 2's select window has the arrows.
    const RoleNow* role = role_of(me_);
    if (role && (role->flags & kRolePicking) && !pick_sent_) return;
    constexpr float kTurnRate = 140.0f, kLookRate = 90.0f;   // degrees a second
    const float turn = (binding_down(Action::TurnRight) ? 1.0f : 0.0f) - (binding_down(Action::TurnLeft) ? 1.0f : 0.0f);
    const float look = (binding_down(Action::LookUp) ? 1.0f : 0.0f) - (binding_down(Action::LookDown) ? 1.0f : 0.0f);
    if (turn != 0) yaw_ = eng::wrap_degrees(yaw_ + turn * kTurnRate * dt);
    if (look != 0) pitch_ = std::clamp(pitch_ + look * kLookRate * dt, -88.0f, 88.0f);
}

void GameWorld::test_stand(const Vec3& at, float yaw, float pitch) {
    if (!collision_) return;
    move_.origin = settle_spawn(move_def_, *collision_, at);
    move_.velocity = {};
    move_.on_ground = true;
    move_.on_ladder = false;
    move_prev_ = move_;
    step_offset_ = step_offset_prev_ = 0;
    yaw_ = yaw, pitch_ = pitch;
}

void GameWorld::test_place(int spawn, float yaw, float wall, bool toward_sun) {
    if (!collision_) return;
    move_.origin = settle_spawn(move_def_, *collision_, spawn_point(team_, u8(std::max(0, spawn))));
    yaw_ = yaw;
    pitch_ = 0;
    if (toward_sun) {
        // The way that faces the map's sun most squarely, the head tipped no further than a soldier's is.
        const Vec3 to_sun = eng::normalize(light_.sun_direction * -1.0f);
        float best = -2;
        for (int y = 0; y < 360; y += 2)
            for (int p = -30; p <= 30; p += 2)
                if (const float d = eng::dot(eng::angles_to_forward(float(y), float(p)), to_sun); d > best) best = d, yaw_ = float(y), pitch_ = float(p);
        static bool said = false;
        if (!said)
            LOG_INFO("autotest: facing the sun (yaw %.0f, pitch %.0f, %.0f degrees off it)", double(yaw_), double(pitch_),
                     double(std::acos(std::clamp(best, -1.0f, 1.0f)) / eng::kDegToRad));
        said = true;
        yaw = yaw_;
    }
    if (wall > 0) {
        const Vec3 eye = eye_position(move_def_, move_), dir = eng::angles_to_forward(yaw, 0);
        const eng::TraceResult tr = collision_->trace_ray(eye, eye + dir * 6000.0f);
        if (tr.hit()) move_.origin = settle_spawn(move_def_, *collision_, move_.origin + dir * std::max(0.0f, tr.fraction * 6000.0f - wall));
    }
    move_.velocity = {};
    move_prev_ = move_;
    recoil_pitch_ = recoil_yaw_ = kick_pitch_ = kick_yaw_ = 0;
}

void GameWorld::test_shadow_view() {
    const Vec3 away{light_.sun_direction.x, 0.0f, light_.sun_direction.z};
    if (eng::length_sq(away) > 1e-4f) yaw_ = std::atan2(away.x, away.z) / eng::kDegToRad;
    pitch_ = -62.0f;
    third_person_ = true;
    static bool said = false;
    if (!said)
        LOG_INFO("autotest: the map's sun travels (%.2f, %.2f, %.2f); facing yaw %.0f, away from it", double(light_.sun_direction.x),
                 double(light_.sun_direction.y), double(light_.sun_direction.z), double(yaw_));
    said = true;
}

void GameWorld::face_open_space() {
    if (!collision_) return;
    const Vec3 eye = eye_position(move_def_, move_);
    float best = -1, best_yaw = yaw_;
    for (int k = 0; k < 24; ++k) {
        const float y = float(k) * 15.0f;
        const eng::TraceResult tr = collision_->trace_ray(eye, eye + eng::angles_to_forward(y, 0) * 4000.0f);
        const float d = tr.hit() ? tr.fraction * 4000.0f : 4000.0f;
        if (d > best) best = d, best_yaw = y;
    }
    yaw_ = best_yaw;
    pitch_ = 0;
}

bool GameWorld::look_at_other() {
    const Vec3 eye = eye_position(move_def_, move_);
    float best = 1e30f;
    watched_ = 0;
    for (const auto& [id, p] : players_)
        if (id != me_ && p.alive && eng::length(p.position - eye) < best) best = eng::length(p.position - eye), watched_ = id;
    if (!watched_) return false;
    const Vec3 to = players_[watched_].position + Vec3{0, 110, 0} - eye;
    yaw_ = eng::forward_to_yaw(to);
    pitch_ = eng::forward_to_pitch(to);
    return true;
}

bool GameWorld::follow_other(float distance, float side) {
    if (!look_at_other() || !collision_ || !alive_) return false;
    const PlayerView& p = players_[watched_];
    const Vec3 dir = eng::angles_to_forward(p.yaw + side, 0);
    const Vec3 up{0, 120, 0};
    Vec3 want = p.position + dir * distance;
    // Never through a wall: as far as the line from him stays clear.
    if (const eng::TraceResult tr = collision_->trace_ray(p.position + up, want + up); tr.hit())
        want = p.position + dir * (distance * std::max(0.25f, tr.fraction - 0.1f));
    move_.origin = settle_spawn(move_def_, *collision_, want);
    move_.velocity = {};
    move_prev_ = move_;
    return look_at_other();
}

bool GameWorld::view_glow(int index) {
    const std::vector<Vec3>& at = level_->glows;
    if (index >= int(at.size()) || !collision_) return false;
    const Vec3 c = at[size_t(index)];
    // From whichever side has the longest clear view of it, up to four metres off.
    float best = -1;
    Vec3 stand = c;
    for (int k = 0; k < 16; ++k) {
        const Vec3 dir = eng::angles_to_forward(float(k) * 22.5f, 0);
        const eng::TraceResult tr = collision_->trace_ray(c, c + dir * 400.0f);
        const float d = tr.hit() ? tr.fraction * 400.0f : 400.0f;
        if (d > best) best = d, stand = c + dir * std::max(60.0f, d - 40.0f);
    }
    move_.origin = settle_spawn(move_def_, *collision_, stand - Vec3{0, move_def_.stand_eye, 0});
    move_.velocity = {};
    move_prev_ = move_;
    const Vec3 to = c - eye_position(move_def_, move_);
    yaw_ = eng::forward_to_yaw(to);
    pitch_ = eng::forward_to_pitch(to);
    return true;
}

std::string GameWorld::other_clips() const {
    auto it = players_.find(watched_);
    if (it == players_.end()) return "nobody";
    const PlayerView& p = it->second;
    const WeaponDef* w = weapon(p.weapon);
    return eng::str::format("%s plays %s + %s at %d cm/s, legs %d degrees off his aim, %s in hand%s, %zu snapshots buffered, %.0f cm away",
                            p.name.c_str(), p.lower_clip.c_str(), p.upper_clip.c_str(), int(flat_speed(p.velocity)), int(p.leg_yaw),
                            w ? w->name.c_str() : "nothing", app_.now() - p.muzzle_at < 0.25 ? " (drawn)" : " (NOT drawn)", p.samples.size(),
                            double(eng::length(p.position - move_.origin)));
}

float GameWorld::current_cone(float shots_share) const {
    const WeaponDef* w = my_weapon(slot_);
    if (!w) return 1.0f;
    float cone = (move_.ducked ? w->spread_crouch : w->spread_stand) + spread_ * shots_share + moving_spread_;
    // recoil.kst's ADD_VALUE_*: in the air the cone doubles for nearly every gun; running it grows
    // by half; walking leaves it be.
    const float h = flat_speed(move_.velocity);
    if (!move_.on_ground) cone *= w->air_spread;
    else if (h > 30.0f) cone *= walking_ ? w->walk_spread : w->run_spread;
    if (w->scoped && !scoped_) cone += 4.0f;
    return cone;
}

void GameWorld::update_weapon(float dt) {
    eng::Input& in = app_.window().input();
    const bool control = !(chat_open_ || VanGui::GetIO().WantTextInput) && !menu_open_ && !match_over_ && alive_ && !dialog_up();
    const WeaponDef* w = my_weapon(slot_);
    vm_time_ += dt * vm_rate_;
    draw_left_ = std::max(0.0f, draw_left_ - dt);
    fire_cooldown_ = std::max(0.0f, fire_cooldown_ - dt);
    if (w) {
        // The cone closes all the while, at recoil.kst's IMPACT_DOME_DEC a tenth of a second, a shot
        // or not: held down, a gun's cone opens only as far as its shots outrun that (an M4A1's
        // reaches about 3 degrees after a whole magazine, not its 4.5 cap after eleven shots), and
        // fired a shot or a few at a time it stays at rest. The kick stays while the burst goes on
        // (a shot's gap and a little after), or it would never climb, and comes back once the
        // trigger rests. The view follows the kick in a few hundredths of a second, not at once.
        spread_ = std::max(0.0f, spread_ - w->spread_recover * dt);
        const bool settling = app_.now() - muzzle_flash_at_ > std::max(0.15, 1.5 * 60.0 / std::max(1.0f, w->rpm));
        if (settling) {
            recoil_pitch_ = std::max(0.0f, recoil_pitch_ - w->recoil_recover * dt);
            recoil_yaw_ -= recoil_yaw_ * std::min(1.0f, w->recoil_recover * 0.3f * dt);
        }
        const float ease = 1.0f - std::exp(-dt / 0.035f);
        kick_pitch_ += (recoil_pitch_ - kick_pitch_) * ease;
        kick_yaw_ += (recoil_yaw_ - kick_yaw_) * ease;
        // Moving opens the cone up to the gun's MOVING_MAX_ANGLE, as fast as its MOVING_VALUE_INC;
        // standing still it closes twice as fast.
        const bool moving = flat_speed(move_.velocity) > 30.0f;
        moving_spread_ = moving ? std::min(w->spread_move, moving_spread_ + w->spread_move_rate * dt)
                                : std::max(0.0f, moving_spread_ - w->spread_move_rate * 2.0f * dt);
    }
    if (reload_left_ > 0) {
        reload_left_ -= dt;
        if (reload_left_ <= 0 && w) {
            const int need = w->magazine - clip_[size_t(slot_)];
            const int take = std::min(need, reserve_[size_t(slot_)]);
            clip_[size_t(slot_)] += take;
            reserve_[size_t(slot_)] -= take;
        }
    }
    if (!control) {
        trigger_released_ = true;
        firing_ = false;
        // Dead: a pin pulled goes nowhere. Otherwise a grenade already let go still leaves the hand.
        if (!alive_) grenade_held_ = false, throw_at_ = -1;
        else if (throw_at_ >= 0 && app_.now() >= throw_at_) throw_at_ = -1, throw_grenade();
        return;
    }
    // Slots: 1-3 (as bound) the primary, sidearm and blade; 4 the throwables, each press the next
    // carried; the wheel; the last weapon (F).
    const Action slots[4] = {Action::Weapon1, Action::Weapon2, Action::Weapon3, Action::Weapon4};
    for (int k = 0; k < 4; ++k)
        if (binding_pressed(slots[k]) && !(digits_taken_ && app_.settings().bind(slots[k]) >= '0' && app_.settings().bind(slots[k]) <= '9')) {
            if (k == int(Slot::Throw)) next_throwable();
            else set_slot(k);
        }
    // The next weapon carried, or the one before: the wheel, or what is bound to them. A throwable
    // with nothing left in it is passed over.
    auto cycle = [&](int dir) {
        const int n = int(kLoadoutSlots);
        int s = slot_;
        for (int tries = 0; tries < n; ++tries) {
            s = (s + (dir > 0 ? 1 : n - 1)) % n;
            const size_t c = size_t(s);
            if (loadout_[c] != kNoWeapon && (!throw_cell(c) || clip_[c] > 0 || reserve_[c] > 0)) break;
        }
        set_slot(s);
    };
    if (in.wheel() != 0) cycle(in.wheel() < 0 ? 1 : -1);
    if (binding_pressed(Action::NextWeapon)) cycle(1);
    if (binding_pressed(Action::PrevWeapon)) cycle(-1);
    // Last weapon and Drop (the original gave both F): on one key a tap is the last weapon and a
    // hold of kDropHold puts the gun in hand down; on keys of their own, each is itself.
    constexpr double kDropHold = 0.4;
    const unsigned drop_key = app_.settings().bind(Action::DropWeapon);
    if (drop_key && drop_key == app_.settings().bind(Action::LastWeapon)) {
        const double now = app_.now();
        if (binding_pressed(Action::LastWeapon)) drop_held_at_ = now;
        if (drop_held_at_ >= 0) {
            if (!binding_down(Action::LastWeapon)) set_slot(last_slot_), drop_held_at_ = -1;
            else if (now - drop_held_at_ >= kDropHold) drop_weapon(), drop_held_at_ = -1;
        }
    } else {
        if (binding_pressed(Action::LastWeapon)) set_slot(last_slot_);
        if (binding_pressed(Action::DropWeapon)) drop_weapon();
    }
    if (binding_pressed(Action::PickUpWeapon)) pick_up_weapon();
    w = my_weapon(slot_);
    if (!w) return;
    auto start_reload = [&] {
        reload_left_ = w->reload_time;
        audio_->reload(me_, w->id, w->reload_time, nullptr);
        scoped_ = false;
        vm_clip_ = "reload";
        vm_time_ = 0;
        vm_loop_ = false;
        // The reload clip is played over the weapon's own reload time, so the magazine is in when
        // the hands say it is.
        vm_rate_ = 1;
        if (Animated* vm = view_model_for(w->id); vm && vm->model && w->reload_time > 0.05f)
            if (const sf::ModelAnimation* a = vm->model->animation("reload")) vm_rate_ = std::max(0.1f, a->duration / w->reload_time);
    };
    // Reload.
    const bool gun = w->magazine > 0 && w->klass != WeaponClass::Grenade;
    if (gun && binding_pressed(Action::Reload) && reload_left_ <= 0 && clip_[size_t(slot_)] < w->magazine && reserve_[size_t(slot_)] > 0) start_reload();
    // Scope.
    // Right click: in, at the scope's full power; once more, out. The AWP's goes in two steps, at
    // half its power first (WeaponDef::double_zoom).
    // Held instead (the options' choice): in at the scope's full power while the key is down, out
    // when it comes up.
    if (!scoped_) zoom_step_ = 0;
    if (app_.settings().scope_hold) {
        const bool want = w->scoped && binding_down(Action::Zoom) && reload_left_ <= 0;
        if (want && !scoped_) scoped_ = true, zoom_step_ = 2;
        else if (!want && scoped_) scoped_ = false, zoom_step_ = 0;
    } else if (w->scoped && binding_pressed(Action::Zoom) && reload_left_ <= 0) {
        if (!scoped_) scoped_ = true, zoom_step_ = w->double_zoom ? 1 : 2;
        else if (zoom_step_ == 1) zoom_step_ = 2;
        else scoped_ = false, zoom_step_ = 0;
    }
    // Fire: on the ground or in the air alike (the air only widens the cone).
    // (At a cannon the trigger is the cannon's: World/Objectives.cpp.)
    const bool held = (binding_down(Action::Shoot) || auto_fire_) && !manning_cannon();
    if (!held) trigger_released_ = true;
    bool shot = false;
    if (held && draw_left_ <= 0 && reload_left_ <= 0 && fire_cooldown_ <= 0 && (w->automatic || trigger_released_)) {
        if (w->klass == WeaponClass::Grenade) {
            // The pin pulled: held back until the trigger is let go.
            if ((clip_[size_t(slot_)] > 0 || reserve_[size_t(slot_)] > 0) && !grenade_held_ && throw_at_ < 0) {
                grenade_held_ = true;
                vm_clip_ = "hold";
                vm_time_ = 0;
                vm_rate_ = 1;
                vm_loop_ = false;
            }
        } else if (gun && clip_[size_t(slot_)] <= 0) {
            // Dry: reload by itself when there is ammunition left, else the empty click.
            if (reserve_[size_t(slot_)] > 0) {
                start_reload();
            } else {
                audio_->empty_click();
                fire_cooldown_ = 0.3f;
            }
        } else {
            fire();
            shot = true;
        }
        trigger_released_ = false;
    }
    // Let go: the throw clip, the grenade leaving the hand a third of the way through it.
    if (grenade_held_ && !held && w->klass == WeaponClass::Grenade) {
        grenade_held_ = false;
        vm_clip_ = "throw";
        vm_time_ = 0;
        vm_rate_ = 1;
        vm_loop_ = false;
        float release = 0.2f, length = 0.8f;
        if (Animated* vm = view_model_for(w->id); vm && vm->model)
            if (const sf::ModelAnimation* a = vm->model->animation("throw")) release = a->duration * 0.35f, length = a->duration;
        throw_at_ = app_.now() + release;
        fire_cooldown_ = std::max(fire_cooldown_, length);
    }
    if (throw_at_ >= 0 && app_.now() >= throw_at_) {
        throw_at_ = -1;
        throw_grenade();
    }
    // Others see the burst while the trigger is held and shots keep leaving.
    if (shot) firing_ = true;
    else if (!held || reload_left_ > 0 || (gun && clip_[size_t(slot_)] <= 0)) firing_ = false;
}

bool GameWorld::trace_players(const Vec3& from, const Vec3& dir, float max_dist, u32& victim, HitZone& zone, float& dist) const {
    // Each soldier is three shapes: a head box, a torso box and a legs box (Game/Ballistics.hpp: the
    // server traces the same ones).
    float best = max_dist;
    bool hit = false;
    for (const auto& [id, p] : players_) {
        if (id == me_ || !p.alive || p.hidden) continue;
        if (mode_info(settings_.mode).teams && p.team == team_) continue;
        float t = 0;
        HitZone z = HitZone::None;
        if (ray_soldier(from, dir, p.position, p.flags, move_def_, best, t, z)) {
            best = t;
            victim = id;
            zone = z;
            hit = true;
        }
    }
    dist = best;
    return hit;
}

void GameWorld::fire() {
    const WeaponDef* w = my_weapon(slot_);
    if (!w) return;
    const double now = app_.now();
    const bool melee = w->klass == WeaponClass::Knife;
    fire_cooldown_ = 60.0f / std::max(1.0f, w->rpm);
    if (!melee) --clip_[size_t(slot_)];
    audio_->fire(me_, w->id, nullptr);
    app_.rumble(melee ? 0.2f : 0.3f, 0.07f);
    vm_clip_ = "shoot";
    // A blade's attack clips take turns: the M9's slash and its stab (shoot, shoot_01), a sword's
    // three cuts (shoot_bayonet and its _01, _02: its "shoot" is the flintlock in its hilt going off).
    if (melee)
        if (const Animated* vm = view_model_for(w->id); vm && vm->model) {
            std::vector<const char*> turns;
            for (const char* c : {"shoot_bayonet", "shoot_bayonet_01", "shoot_bayonet_02"})
                if (vm->model->animation(c)) turns.push_back(c);
            if (turns.empty())
                for (const char* c : {"shoot", "shoot_01"})
                    if (vm->model->animation(c)) turns.push_back(c);
            if (!turns.empty()) vm_clip_ = turns[swings_ % turns.size()];
            ++swings_;
        }
    vm_time_ = 0;
    vm_rate_ = 1;
    vm_loop_ = false;
    vm_moving_ = false;
    ++shots_fired_;
    if (players_.contains(me_)) {
        auto& p = players_[me_];
        if (now - p.fired_at > 0.35) p.burst_from = now;
        p.fired_at = now;
        ++p.swings;
    }
    // The cone the shot leaves in (the crosshair shows the same).
    const float cone = current_cone();
    Shoot s;
    s.tick = tick_;
    s.weapon = w->id;
    s.origin = camera_.eye;
    const Vec3 fwd = eng::angles_to_forward(yaw_ + kick_yaw_, pitch_ + kick_pitch_);
    s.direction = fwd;
    const float range = melee ? w->melee_range : w->range;
    const int pellets = std::max<int>(1, w->pellets);
    const float wall_limit = penetration_cm(*w);   // the wall this bullet goes through (0: none)
    for (int k = 0; k < pellets; ++k) {
        // Anywhere in the cone, the nearer its middle the likelier (a bullet's distance from it is
        // even over the cone's width, so they crowd the middle and thin out toward the edge).
        const float a = frand(g_rng, 0, 6.2831853f), r = frand(g_rng, 0, 1) * cone * eng::kDegToRad;
        const Vec3 right = eng::yaw_to_right(yaw_);
        const Vec3 up = eng::cross(fwd, right);
        const Vec3 dir = eng::normalize(fwd + right * (std::cos(a) * std::tan(r)) + up * (std::sin(a) * std::tan(r)));
        if (shot_offsets_.size() < 4096)
            shot_offsets_.push_back({std::acos(std::clamp(eng::dot(dir, eng::angles_to_forward(yaw_, pitch_)), -1.0f, 1.0f)) * eng::kRadToDeg,
                                     std::acos(std::clamp(eng::dot(dir, fwd), -1.0f, 1.0f)) * eng::kRadToDeg});
        float wall = range;
        Vec3 end = camera_.eye + dir * range;
        bool struck_map = false;
        Vec3 normal{0, 1, 0};
        if (collision_) {
            const eng::TraceResult tr = collision_->trace_ray(camera_.eye, end);
            if (tr.hit()) {
                wall = range * tr.fraction;
                end = tr.end;
                normal = tr.normal;
                struck_map = !tr.start_solid;
            }
        }
        // The nearest soldier along the line, whatever stands between; then what does: nothing, a
        // wall this bullet goes through (a wall shot: the wall keeps its mark, the streak ends at
        // it, and the server takes the wall's share off the damage), or one it does not.
        u32 victim = 0;
        HitZone zone = HitZone::None;
        float dist = 0;
        const bool traced = trace_players(camera_.eye, dir, range, victim, zone, dist);
        if (k == 0) last_shot_ = traced ? eng::str::format("on %u (zone %d) %.0f cm off, the map %.0f cm off", victim, int(zone), double(dist), double(wall))
                                        : eng::str::format("nobody; the map %.0f cm off", double(wall));
        if (traced) {
            const Vec3 at = camera_.eye + dir * dist;
            float thick = 0;
            if (dist > wall) thick = collision_ && wall_limit > 0 ? wall_thickness(*collision_, camera_.eye, at, wall_limit) : -1.0f;
            if (thick >= 0) {
                s.hits.push_back({victim, u8(zone), at, thick > 0 ? u8(std::clamp(thick, 1.0f, 255.0f)) : u8(0)});
                // Blood where it went in (the server's word on the damage follows).
                if (k < 3) blood(at, dir, true);
                if (thick <= 0) {
                    end = at;
                    struck_map = false;
                }
            }
        }
        // The wall answers (a shotgun's first few pellets are enough), and keeps the mark.
        if (struck_map && k < 3) audio_->impact(end, dir, melee);
        if (struck_map && !melee) bullet_mark(end, normal);
        if (k == 0) s.end = end;
        if (!melee && k < 4) {
            // A streak from the barrel: its start is this frame's drawn muzzle, set when drawn.
            Tracer t{vm_posed_ ? vm_muzzle_ : camera_.eye + fwd * 30.0f - Vec3{0, 6, 0}, end, now};
            t.from_barrel = true;
            tracers_.push_back(t);
        }
    }
    if (!melee) {
        // The spent case: from the drawn gun's port (seen from behind: your carried gun's).
        const Vec3 right = eng::yaw_to_right(yaw_), up = eng::cross(fwd, right);
        if (third_person_ && players_.contains(me_) && players_[me_].has_eject && now - players_[me_].muzzle_at < 0.25)
            eject_case(players_[me_].eject, right, Vec3{0, 1, 0}, fwd, move_.velocity, w->id);
        else if (!third_person_ && vm_has_eject_)
            eject_case(vm_eject_, right, up, fwd, move_.velocity, w->id);
        // The kick: up INCLINE a shot, and sideways one way through a burst (now and then the
        // other), as far as IMPACT_LIMIT_WIDTH lets it, rather than a jerk either way each shot.
        const bool fresh_burst = now - muzzle_flash_at_ > std::max(0.15, 1.5 * 60.0 / std::max(1.0f, w->rpm));
        if (fresh_burst) kick_side_ = g_rng() % 2 ? 1 : -1;
        else if (frand(g_rng, 0, 1) < 0.2f) kick_side_ = -kick_side_;
        muzzle_flash_at_ = now;
        muzzle_star_ = int(g_rng() % 3);
        spread_ = std::min(w->spread_max, spread_ + w->spread_per_shot);
        recoil_pitch_ = std::min(w->recoil_up_max, recoil_pitch_ + w->recoil_up);
        recoil_yaw_ = std::clamp(recoil_yaw_ + float(kick_side_) * w->recoil_side * frand(g_rng, 0.5f, 1.0f), -w->recoil_side_max, w->recoil_side_max);
    }
    if (!offline_) app_.session().send(s);
}

void GameWorld::say_radio(u8 group, u8 line) {
    if (!offline_) {
        Radio r;
        r.group = group;
        r.line = line;
        app_.session().send(r);
        return;
    }
    const ForceDef* f = force(players_.contains(me_) ? players_[me_].force : 0);
    audio_->set_radio_language(Settings::radio_voice_folder(app_.settings().radio_voice));
    audio_->radio(group, line, f && std::string_view(art_model(*f)) == "mulan");
}

void GameWorld::throw_grenade() {
    const WeaponDef* w = my_weapon(slot_);
    if (!w) return;
    if (clip_[size_t(slot_)] > 0) --clip_[size_t(slot_)];
    else --reserve_[size_t(slot_)];
    fire_cooldown_ = 1.0f;
    audio_->fire(me_, w->id, nullptr);
    if (w->grenade == GrenadeKind::Frag) say_radio(kRadioAuto, 0);   // "Fire in the hole!"
    if (players_.contains(me_)) players_[me_].fired_at = players_[me_].burst_from = app_.now();
    const Vec3 dir = camera_.forward();
    Grenade g;
    // Leaving the hand a little ahead of the eye (clear of the arm), short of any wall that close.
    g.pos = camera_.eye + dir * 55.0f;
    if (collision_)
        if (const eng::TraceResult tr = collision_->trace_ray(camera_.eye, g.pos); tr.hit()) g.pos = camera_.eye + dir * std::max(5.0f, 55.0f * tr.fraction - 8.0f);
    // 1450 cm/s: at 1300 a throw fell short (players, 2026-10-04: "nades need to fly a little
    // further"); about a quarter further on the flat, well inside the server's 45 m.
    g.vel = dir * 1450.0f + Vec3{0, 200, 0} + move_.velocity * 0.5f;
    g.fuse = w->fuse;
    g.weapon = w->id;
    g.mine = true;
    grenades_.push_back(g);
    if (!offline_) {
        Throw t;
        t.weapon = w->id;
        t.origin = g.pos;
        t.velocity = g.vel;
        app_.session().send(t);
    }
    // Nothing left: the next throwable carried, else back to the primary.
    if (clip_[size_t(slot_)] <= 0 && reserve_[size_t(slot_)] <= 0) {
        const int was = slot_;
        next_throwable();
        if (slot_ == was) set_slot(0);
    }
}

const sf::ModelAnimation* GameWorld::motion(const std::string& clip, const std::string& look) const {
    auto it = motions_.find(clip + "|" + look);
    return it == motions_.end() ? nullptr : it->second.get();
}

void GameWorld::update_remote(float dt) {
    const double now = app_.now();
    for (auto& [id, p] : players_) {
        if (id == me_) continue;
        // Drawn a tenth of a second behind the newest snapshot, between the two around that moment;
        // past the newest, carried on along its velocity a little.
        auto& S = p.samples;
        if (!S.empty()) {
            const double t = now - kInterpDelay;
            while (S.size() > 2 && S[1].at <= t) S.pop_front();
            if (S.size() >= 2 && t <= S[1].at) {
                const PlayerView::Sample& a = S[0];
                const PlayerView::Sample& b = S[1];
                const float f = std::clamp(float((t - a.at) / std::max(1e-3, b.at - a.at)), 0.0f, 1.0f);
                p.position = eng::lerp(a.position, b.position, f);
                p.velocity = eng::lerp(a.velocity, b.velocity, f);
                p.yaw = eng::lerp_degrees(a.yaw, b.yaw, f);
                p.pitch = eng::lerpf(a.pitch, b.pitch, f);
                p.flags = f < 0.5f ? a.flags : b.flags;
            } else {
                const PlayerView::Sample& s = S.back();
                const float ahead = std::clamp(float(t - s.at), 0.0f, 0.15f);
                p.position = s.position + s.velocity * ahead;
                p.velocity = s.velocity;
                p.yaw = s.yaw;
                p.pitch = s.pitch;
                p.flags = s.flags;
            }
            // Life is the server's word now, not a tenth of a second ago.
            p.flags = u16((p.flags & ~kFlagAlive) | (p.alive ? kFlagAlive : 0));
        }
        pose_body(p, dt);
        if (p.alive && !p.hidden) {
            const MatchAudio::Step step = audio_->track(id, p.position, p.velocity, p.flags & kFlagOnGround, p.flags & kFlagOnLadder,
                                                        p.flags & (kFlagWalking | kFlagCrouched), false, now);
            if (step.step) ++others_steps_;
            if (step.step || step.landing) footstep_fx(p.position, step.material, step.landing);
        }
        else audio_->forget(id);
    }
}

// A soldier's body, posed for this frame the way the original moves its soldiers: the legs play
// the clip for how he moves at the pace he moves (so the feet hold the floor), turned toward where
// he runs with the spine turning back to his aim; the arms play his weapon's clips (draw, fire,
// reload, at rest); the spine leans to where he looks up or down; a death plays once and is held.
void GameWorld::pose_body(PlayerView& p, float dt) {
    if (!p.body.model) return;
    const sf::Model& m = *p.body.model;
    const std::string& look = p.look;
    const double now = app_.now();
    const float speed = flat_speed(p.velocity);
    const bool crouch = p.flags & kFlagCrouched;
    const bool ladder = p.flags & kFlagOnLadder;
    const bool grounded = (p.flags & kFlagOnGround) || ladder;
    // What changed since the last frame.
    if (crouch != p.was_crouched) p.was_crouched = crouch, p.crouch_changed_at = now;
    if (!grounded && p.was_grounded && p.velocity.y > 100.0f) p.jumped_at = now;
    p.was_grounded = grounded;
    if (p.weapon != p.shown_weapon) {
        if (p.shown_weapon != kNoWeapon && p.weapon != kNoWeapon) p.drew_at = now;
        p.shown_weapon = p.weapon;
    }

    std::vector<Mat4>& local = p.body.local;
    local.resize(m.bones.size());
    for (size_t i = 0; i < m.bones.size(); ++i) local[i] = m.bones[i].bind_local;

    if (!p.alive) {
        // A death, chosen once: one of the standing ones, or the crouched ones if he fell crouched.
        if (p.death_clip.empty()) {
            const u32 pick = (p.id * 2654435761u) ^ u32(p.deaths * 40503u) ^ u32(now * 7.0);
            p.death_clip = crouch ? eng::str::format("a_dd_%02u", pick % 3) : eng::str::format("a_ud_%02u", pick % 7);
            if (!motion(p.death_clip, look)) p.death_clip = "a_ud_00";
            p.died_at = now;
        }
        if (const sf::ModelAnimation* a = motion(p.death_clip, look)) sf::sample_animation(m, *a, float(now - p.died_at), false, local);
        sf::pose_to_model(m, local, p.body.model_space);
        sf::skin_matrices(m, p.body.model_space, p.body.skin);
        return;
    }
    p.death_clip.clear();

    // Which way the legs go: toward where he runs (backing away they face the aim and run back),
    // never more than 70 degrees off it; the spine takes the turn back out.
    bool backward = false;
    float leg_target = 0;
    if (speed > 20.0f && grounded && !ladder) {
        float rel = eng::wrap_degrees(eng::forward_to_yaw(p.velocity) - p.yaw);
        if (std::fabs(rel) > 100.0f) {
            backward = true;
            rel = eng::wrap_degrees(rel - 180.0f);
        }
        leg_target = std::clamp(rel, -70.0f, 70.0f);
    }
    p.leg_yaw = approach(p.leg_yaw, leg_target, 540.0f * dt);

    // ── The legs ──
    std::string lower = "l_us";
    float rate = 1;
    bool loop = true, from_clock = false;
    float clock = 0;
    const sf::ModelAnimation* ud = motion("l_ud", look);
    const float since_crouch = float(now - p.crouch_changed_at);
    if (ladder) {
        // On a ladder the feet walk the rungs (forwards going up, backwards coming down) and the gun
        // stays in hand: Soldier Front has no climbing clip, and neither do we.
        if (std::fabs(p.velocity.y) > 20.0f) lower = "l_uwf_01", rate = std::fabs(p.velocity.y) / kWalkPace * (p.velocity.y < 0 ? -1.0f : 1.0f);
    } else if (!grounded) {
        // The take-off, then the legs held tucked until he lands.
        const sf::ModelAnimation* off = motion("l_uj_00", look);
        const float since_jump = float(now - p.jumped_at);
        from_clock = true, loop = false;
        if (off && since_jump < off->duration) lower = "l_uj_00", clock = since_jump;
        else lower = "l_uj_01", clock = since_jump - (off ? off->duration : 0.0f);
    } else if (speed > 20.0f) {
        const bool walk = (p.flags & kFlagWalking) || speed < 0.5f * (kWalkPace + kRunPace);
        if (crouch) lower = "l_dwf_01", rate = speed / kCrouchPace * (backward ? -1.0f : 1.0f);
        else if (backward && !walk) lower = "l_urb_01", rate = speed / kBackPace;
        else if (walk) lower = "l_uwf_01", rate = speed / kWalkPace * (backward ? -1.0f : 1.0f);
        else lower = "l_urf_01", rate = speed / kRunPace;
    } else if (ud && since_crouch < ud->duration) {
        // Going down or getting up: the change itself, forwards or backwards.
        lower = "l_ud", from_clock = true, loop = false;
        clock = crouch ? since_crouch : ud->duration - since_crouch;
    } else if (crouch) {
        lower = "l_ds";
    }
    if (lower != p.lower_clip) {
        p.lower_clip = lower;
        const sf::ModelAnimation* a = motion(lower, look);
        p.lower_time = rate < 0 && a ? a->duration : 0.0f;
    }
    p.lower_time += dt * rate;
    if (from_clock) p.lower_time = std::max(0.0f, clock);

    // ── The arms ──
    const WeaponDef* w = weapon(p.weapon);
    // A blade is held in a blade's arms whatever the table says (weapon.kst gives the pirates'
    // swords the rifle's grip, and a sword held as a rifle is a soldier aiming a cutlass).
    const std::string t = std::string("u_") + upper_type(w && w->klass == WeaponClass::Knife ? u8(9) : w ? w->grip : u8(2)) + "_";
    auto pick = [&](std::initializer_list<const char*> names) -> std::string {
        for (const char* n : names)
            if (motion(t + n, look)) return t + n;
        return {};
    };
    std::string upper;
    float urate = 1;
    bool uloop = true;
    const float draw_time = w ? std::max(0.2f, w->draw_time) : 0.5f;
    const double since_fire = now - p.fired_at;
    const sf::ModelAnimation* stop = nullptr;
    // A blade: its own way of being held, and a swing played out to its end (a gun's shot is over
    // in a fifth of a second; a cut is not).
    const bool melee = w && w->klass == WeaponClass::Knife;
    const BladeArms* blade = blade_arms(w);
    auto named = [&](const std::string& act) { return motion(t + act, look) ? t + act : std::string(); };
    std::string swing;
    float swing_rate = 1;
    if (melee) {
        if (blade) swing = named(std::string("shoot_") + blade->swing);
        if (swing.empty()) {
            const int turn = int((p.swings + 1) & 1);   // his first swing is the first clip
            swing = pick({turn ? "shoot01" : "shoot00"});
            if (swing.empty()) swing = pick({"shoot00", "shoot", "shoot01"});
            // The soldier's own: an undead's claws run at their own pace.
            if (const sf::ModelAnimation* a = motion(swing, look); a && !look.starts_with("undead:") && (swing.ends_with("shoot00") || swing.ends_with("shoot01")))
                swing_rate = std::max(1.0f, a->duration / kPlainSwing[swing.ends_with("shoot01") ? 1 : 0]);
        }
    }
    const sf::ModelAnimation* swung = swing.empty() ? nullptr : motion(swing, look);
    if (p.flags & kFlagReloading) {
        upper = pick({"reload00", "reload"});
        uloop = false;
        if (const sf::ModelAnimation* a = motion(upper, look); a && w && w->reload_time > 0.1f) urate = a->duration / w->reload_time;
    } else if (now - p.drew_at < draw_time) {
        if (blade) upper = named(std::string("take_") + blade->hold);
        if (upper.empty()) upper = pick({"take"});
        uloop = false;
        if (const sf::ModelAnimation* a = motion(upper, look)) urate = a->duration / draw_time;
    } else if (swung && since_fire < double(swung->duration / swing_rate)) {
        upper = swing, uloop = false, urate = swing_rate;
    } else if (melee) {
        // At rest again: nothing of a gun's "stop" to play.
    } else if ((p.flags & kFlagFiring) || since_fire < 0.2) {
        upper = pick({"shoot00", "shoot", "shoot01", "shoot_r"});
        uloop = w && w->automatic;
    } else {
        const std::string stop_name = pick({"shoot00_stop", "shoot_stop", "shoot01_stop"});
        stop = stop_name.empty() ? nullptr : motion(stop_name, look);
        if (stop && since_fire < 0.2 + stop->duration) upper = stop_name, uloop = false;
    }
    if (upper.empty() && blade) upper = named(std::string("wait_") + blade->hold);
    if (upper.empty()) upper = pick({"wait"});
    if (upper.empty()) upper = "u_04_wait";
    // A new shot from a gun that does not fire on its own starts the clip again.
    if (upper != p.upper_clip || (!uloop && upper.find("shoot") != std::string::npos && upper.find("stop") == std::string::npos &&
                                  p.burst_from >= now)) {
        p.upper_clip = upper;
        p.upper_time = 0;
    }
    p.upper_time += dt * urate;

    static thread_local std::vector<bool> upper_mask, lower_mask;
    upper_mask = sf::upper_body_mask(m);
    lower_mask.assign(upper_mask.size(), false);
    for (size_t i = 0; i < upper_mask.size(); ++i) lower_mask[i] = !upper_mask[i];
    if (const sf::ModelAnimation* a = motion(p.lower_clip, look)) sf::sample_animation(m, *a, p.lower_time, loop, local, &lower_mask);
    if (const sf::ModelAnimation* a = motion(p.upper_clip, look)) sf::sample_animation(m, *a, p.upper_time, uloop, local, &upper_mask);

    // The legs turned to where he runs, the spine turned back to his aim and leaning to his pitch.
    std::vector<Mat4>& ms = p.body.model_space;
    const float lean = std::clamp(p.pitch, -60.0f, 60.0f);
    if (std::fabs(p.leg_yaw) > 0.01f) turn_bone(m, local, ms, m.find_bone("Bip01"), p.leg_yaw, 0);
    const struct {
        const char* bone;
        float yaw, pitch;
    } spine[] = {{"Bip01 Spine", 0.4f, 0.3f}, {"Bip01 Spine1", 0.3f, 0.3f}, {"Bip01 Spine2", 0.3f, 0.4f}};
    for (const auto& s : spine)
        if (std::fabs(p.leg_yaw) > 0.01f || std::fabs(lean) > 0.01f) turn_bone(m, local, ms, m.find_bone(s.bone), -p.leg_yaw * s.yaw, lean * s.pitch);
    // A captain (CTC, Captain Mode): the briefings' "oversized head".
    if (p.big_head)
        if (const int head = m.find_bone("Bip01 Head"); head >= 0 && size_t(head) < local.size())
            local[size_t(head)] = Mat4::scale(Vec3{1.8f, 1.8f, 1.8f}) * local[size_t(head)];
    sf::pose_to_model(m, local, ms);
    sf::skin_matrices(m, ms, p.body.skin);
}

float GameWorld::zoom_now() const {
    const WeaponDef* w = my_weapon(slot_);
    if (!scoped_ || !w || w->zoom <= 0) return 1.0f;
    return zoom_step_ == 1 ? std::max(2.0f, w->zoom * 0.5f) : w->zoom;
}

Animated* GameWorld::view_model_for(u16 wid) {
    auto it = view_models_.find(wid);
    return it == view_models_.end() ? nullptr : &it->second;
}

// ── Drawing ────────────────────────────────────────────────────────────────────

// The first-person weapon for this frame: which clip (the draw, a shot, the reload, or at rest
// and on the move with move_start / move_end between, as the original's .sfm lists them), posed,
// placed from the eye as its .sfc says, and its muzzle found in both lenses.
bool GameWorld::pose_view_model() {
    vm_posed_ = false;
    if (!alive_ || third_person_ || me_undead()) return false;   // an undead's claws are not drawn in first person
    Animated* vm = view_model_for(loadout_[size_t(slot_)]);
    if (!vm || !vm->model || !vm->gpu) return false;
    const sf::Model& m = *vm->model;
    auto set_clip = [&](const char* name, bool loop) {
        vm_clip_ = name;
        vm_time_ = 0;
        vm_rate_ = 1;
        vm_loop_ = loop;
    };
    const sf::ModelAnimation* anim = m.animation(vm_clip_);
    const bool done = !anim || (!vm_loop_ && vm_time_ > anim->duration);
    const bool one_shot = vm_clip_ == "draw" || vm_clip_ == "reload" || vm_clip_.starts_with("shoot") || vm_clip_ == "throw" || vm_clip_ == "hold";
    if (vm_clip_ == "hold" && grenade_held_) {
        // The pin out, the arm back: held on the clip's last frame until it is let go.
    } else if (vm_clip_ == "throw" && done && m.animation("draw")) {
        set_clip("draw", false);   // the next grenade comes up
        anim = m.animation(vm_clip_);
    } else if (!one_shot || done) {
        // Running lowers the gun (move_start, then move); stopping brings it back (move_end). Walking
        // and crouching keep it up, however fast they go.
        const bool moving = move_.on_ground && !walking_ && !move_.ducked && flat_speed(move_.velocity) > 0.5f * (kWalkPace + kRunPace) && !scoped_;
        const bool have_start = m.animation("move_start"), have_end = m.animation("move_end"), have_move = m.animation("move");
        if (vm_clip_ == "move_start") {
            if (done) moving && have_move ? set_clip("move", true) : (have_end ? set_clip("move_end", false) : set_clip("idle", true));
        } else if (vm_clip_ == "move") {
            if (!moving) have_end ? set_clip("move_end", false) : set_clip("idle", true);
        } else if (vm_clip_ == "move_end") {
            if (moving && have_move) have_start ? set_clip("move_start", false) : set_clip("move", true);
            else if (done) set_clip("idle", true);
        } else if (moving && have_move) {
            have_start ? set_clip("move_start", false) : set_clip("move", true);
        } else if (vm_clip_ != "idle") {
            set_clip("idle", true);
        }
        anim = m.animation(vm_clip_);
    }
    vm->local.resize(m.bones.size());
    for (size_t i = 0; i < m.bones.size(); ++i) vm->local[i] = m.bones[i].bind_local;
    if (anim) sf::sample_animation(m, *anim, vm_time_, vm_loop_, vm->local);
    sf::pose_to_model(m, vm->local, vm->model_space);
    sf::skin_matrices(m, vm->model_space, vm->skin);
    // The eye is where the weapon's own .sfc puts it (sf::Model::view_eye): the original's framing,
    // and the same for every gun (some 40 cm ahead of the elbow, so the gun's body fills the
    // corner, the left hand and forearm show and the right hand is under the picture). A gun
    // with no .sfc falls back on its Camera01 at rest -- the artists' camera, which the oldest
    // guns (the M4A1, the G36C, the MP5 ...) left standing at the elbow, both arms in the picture.
    static std::map<const sf::Model*, Vec3> rest_cameras;
    Vec3 cam{0, 0, 0};
    if (m.has_view_eye) {
        cam = m.view_eye;
    } else if (auto it = rest_cameras.find(&m); it != rest_cameras.end()) {
        cam = it->second;
    } else {
        if (const sf::ModelSocket* s = m.socket("Camera01")) {
            std::vector<Mat4> rest_local(m.bones.size()), rest;
            for (size_t i = 0; i < m.bones.size(); ++i) rest_local[i] = m.bones[i].bind_local;
            if (const sf::ModelAnimation* idle = m.animation("idle")) sf::sample_animation(m, *idle, 0, false, rest_local);
            sf::pose_to_model(m, rest_local, rest);
            cam = origin_of(rest[size_t(s->bone)]);
        }
        rest_cameras[&m] = cam;
    }
    // The gun sits still in the view as you walk: no bob, no sway (the user's rule). What moves it
    // is the weapon's own carry clip alone. Its framing is the weapon's own, for everyone: the F8
    // panel that let a player move it was taken out on 2026-10-05.
    const Mat4 cam_world = camera_.view.inverse();
    vm_world_ = Mat4::translation(-cam) * cam_world;
    // The muzzle as drawn, and the same point in the world's lens: the view model is drawn with a
    // narrower lens, so on screen its muzzle sits where the world's lens puts this other point.
    const Vec3 fwd = camera_.forward();
    if (const sf::ModelSocket* flame = m.socket("flame")) vm_flame_ = vm_world_.transform_point(origin_of(vm->model_space[size_t(flame->bone)]));
    else vm_flame_ = camera_.eye + fwd * 60.0f - Vec3{0, 8, 0};
    vm_flame_dir_ = fwd;
    const float world_fov_y = 2.0f * std::atan(std::tan(camera_.fov_x * 0.5f * eng::kDegToRad) / (16.0f / 9.0f));
    const float k = std::tan(world_fov_y * 0.5f) / std::tan(app_.settings().viewmodel_fov * 0.5f * eng::kDegToRad);
    Vec3 cs = camera_.view.transform_point(vm_flame_);
    cs.x *= k, cs.y *= k;
    vm_muzzle_ = cam_world.transform_point(cs);
    // The ejection port, the same way, and brought toward the eye along its own line of sight to
    // the scale the case is drawn at (the view model is in its units, 0.233 cm): the case leaves
    // from where the port is seen, as big beside it as it should be.
    vm_has_eject_ = false;
    if (const sf::ModelSocket* port = m.socket("cartridge"); port && size_t(port->bone) < vm->model_space.size()) {
        Vec3 ps = camera_.view.transform_point(vm_world_.transform_point(origin_of(vm->model_space[size_t(port->bone)])));
        ps.x *= k, ps.y *= k;
        ps = ps * 0.2f;
        if (ps.z > 0.01f && ps.z < 18.0f) ps = ps * (18.0f / ps.z);   // clear of the near plane
        vm_eject_ = cam_world.transform_point(ps);
        vm_has_eject_ = true;
    }
    vm_posed_ = true;
    return true;
}

void GameWorld::draw_player(PlayerView& p, u32 id, double now, bool casting) {
    auto gpu = force_gpu_.find(p.look);
    if (gpu == force_gpu_.end() || !gpu->second || p.body.skin.empty() || p.hidden || p.escaped) return;
    if (!p.alive && now - p.snap_time > 5.0 && id != me_) return;
    // The models face +X in their own frame (kBodyFacing): turned onto his aim.
    const Mat4 world = Mat4::yaw(p.yaw - kBodyFacing) * Mat4::translation(p.position);
    const eng::Vec4 tint = mode_info(settings_.mode).teams && p.team == Team::Blue ? eng::Vec4{0.92f, 0.95f, 1.05f, 1} : eng::Vec4{1, 1, 1, 1};
    renderer_.draw_model(*gpu->second, world, &p.body.skin, tint);
    // The gun in his right hand (weapon.kst's OBJECT, placed by its point file), and its muzzle.
    // An undead has claws, nothing in them.
    if (!p.alive || p.look.starts_with("undead:")) return;
    Carried* c = carried_for(p.weapon);
    const int hand = p.body.model ? p.body.model->find_bone("Bip01 R Hand") : -1;
    if (!c || hand < 0 || size_t(hand) >= p.body.model_space.size()) return;
    const Mat4 gun = c->model->attach * p.body.model_space[size_t(hand)] * world;
    renderer_.draw_model(*c->gpu, gun, nullptr, tint);
    if (casting) return;
    if (c->flame >= 0) {
        const Mat4 flame = c->flame_at * gun;
        p.muzzle = origin_of(flame);
        p.muzzle_dir = eng::normalize(p.muzzle - origin_of(gun));
        p.muzzle_at = now;
    }
    p.has_eject = c->cartridge >= 0;
    if (p.has_eject) p.eject = origin_of(c->cartridge_at * gun);
}

// Fidelity's lights for this frame. Each is drawn only while its source is in the eye's own line of
// sight: Fidelity shows nothing the plain game would not (no glow round a corner from a shot the
// player could not have seen).
void GameWorld::collect_lights(double now) {
    FramePipe& pipe = app_.frame_pipe();
    if (!pipe.fidelity()) return;
    const FidelitySettings& f = pipe.finish();
    const bool lights = pipe.surfaces() && f.lights;
    if (!corona_art_.tex) corona_art_.tex = pipe.corona();
    const bool coronas = f.coronas && corona_art_.tex;
    if (!lights && !coronas) return;
    const Vec3 eye = camera_.eye;
    auto in_sight = [&](const Vec3& at) {
        if (!collision_) return true;
        const Vec3 d = at - eye;
        const float len = eng::length(d);
        if (len < 40.0f) return true;
        return !collision_->trace_ray(eye, at - d * (20.0f / len)).hit();
    };
    // Muzzle flashes: a warm burst for the frames the flash itself is drawn.
    for (auto& [id, p] : players_) {
        if (!p.alive || !lights) continue;
        const WeaponDef* w = weapon(p.weapon);
        if (!w || w->klass == WeaponClass::Knife || w->klass == WeaponClass::Grenade) continue;
        Vec3 at;
        if (id == me_ && !third_person_) {
            if (now - muzzle_flash_at_ > 0.06) continue;
            // Your own, a little ahead of the eye (the gun in hand is drawn through a lens of its own).
            at = eye + camera_.forward() * 70.0f - Vec3{0, 8, 0};
        } else {
            if (now - p.fired_at > 0.06 || now - p.muzzle_at > 0.25) continue;
            at = p.muzzle + p.muzzle_dir * 8.0f;
            if (!in_sight(at)) continue;
        }
        pipe.add_light({at, 520.0f, Vec3{1.0f, 0.74f, 0.42f} * 2.6f, 1.0f});
    }
    // Blasts: a grenade's fire for half a second, a flash-bang's white for a quarter.
    for (const Blast& b : blasts_) {
        const float t = float(now - b.at);
        const float life = b.frag ? 0.6f : 0.3f;
        if (t >= life || !lights) continue;
        const float k = 1.0f - t / life;
        const Vec3 at = b.pos + Vec3{0, 45, 0};
        if (!in_sight(at)) continue;
        if (b.frag) pipe.add_light({at, 950.0f, Vec3{1.0f, 0.60f, 0.26f} * (3.6f * k * std::sqrt(k)), 1.0f});
        else pipe.add_light({at, 1500.0f, Vec3{1.0f, 1.0f, 1.0f} * (4.0f * k), 1.0f});
    }
    // The map's own lamps (its glow props): the bake has their light already, so they pool only
    // a little and mostly glint off what shines beneath them.
    // A beam (a headlight, a spot) lights what is ahead of it and has a corona at its bulb,
    // strongest looked into; a tube, a sign or a fire glows all round from its middle.
    lamps_near_ = lamps_seen_ = coronas_drawn_ = 0;
    if (level_) {
        const Vec3 fwd = camera_.forward();
        for (const sf::Level::Lamp& lamp : level_->lamps) {
            const Vec3 d = lamp.source - eye;
            const float d2 = eng::dot(d, d);
            if (d2 > 3000.0f * 3000.0f || eng::dot(d, fwd) < -600.0f) continue;
            ++lamps_near_;
            // The bulb itself is inside its housing: sight is taken to a point a little out along the beam.
            const Vec3 bulb = lamp.beam ? lamp.source + lamp.direction * 14.0f : lamp.source;
            if (!in_sight(bulb)) continue;
            ++lamps_seen_;
            // Its own colour (most are white: a warm white then, as a filament's or a headlamp's is).
            Vec3 tint{float((lamp.colour >> 16) & 0xFF), float((lamp.colour >> 8) & 0xFF), float(lamp.colour & 0xFF)};
            const float top = std::max(tint.x, std::max(tint.y, tint.z));
            tint = top > 1 ? tint * (1.0f / top) : Vec3{1, 1, 1};
            if (std::min(tint.x, std::min(tint.y, tint.z)) > 0.85f) tint = {1.0f, 0.93f, 0.78f};
            if (lights) {
                if (lamp.beam) pipe.add_light({lamp.source + lamp.direction * (lamp.length * 0.45f), std::clamp(lamp.length * 0.7f, 260.0f, 700.0f), tint * 1.2f, 0.35f});
                else pipe.add_light({lamp.source, std::clamp(lamp.length + lamp.width * 1.5f, 260.0f, 600.0f), tint * 1.0f, 0.3f});
            }
            if (coronas && (lamp.beam || lamp.length < 90.0f)) {
                const float dist = std::sqrt(d2);
                const Vec3 to_eye = d * (-1.0f / std::max(dist, 1.0f));
                float a = lamp.beam ? std::clamp(eng::dot(lamp.direction, to_eye) * 1.3f + 0.2f, 0.0f, 1.0f) : 0.7f;
                a *= std::clamp(1.0f - dist / 3000.0f, 0.0f, 1.0f);
                if (a <= 0.02f) continue;
                // A little toward the eye, clear of the lamp's own glass; larger with distance, as a glare is.
                const float size = (lamp.beam ? 85.0f : 55.0f) * (1.0f + dist / 1500.0f);
                const u32 ink = u32(a * 235.0f) << 24 | u32(tint.z * 255.0f) << 16 | u32(tint.y * 255.0f) << 8 | u32(tint.x * 255.0f);
                renderer_.sprite(corona_art_, bulb + to_eye * 12.0f, size, ink);
                ++coronas_drawn_;
            }
        }
    }
}

std::string GameWorld::lamp_report() const {
    if (!level_) return "no level";
    int beams = 0;
    for (const sf::Level::Lamp& l : level_->lamps) beams += l.beam ? 1 : 0;
    return eng::str::format("%zu lamps (%d beams); last frame %d near, %d in sight, %d coronas, corona art %s", level_->lamps.size(), beams, lamps_near_,
                            lamps_seen_, coronas_drawn_, corona_art_.tex ? "loaded" : "MISSING");
}

bool GameWorld::view_water() {
    if (!level_ || !collision_) return false;
    // The widest stretch of water, by the map's own batches.
    const LevelGpu::Batch* best = nullptr;
    float area = 0;
    for (const LevelGpu::Batch& b : level_gpu_.batches) {
        if (!b.water) continue;
        const Vec3 e = b.bounds.max - b.bounds.min;
        if (e.x * e.z > area) area = e.x * e.z, best = &b;
    }
    if (!best) return false;
    const Vec3 water = (best->bounds.min + best->bounds.max) * 0.5f + Vec3{0, 20, 0};
    // The spawn nearest it that can see it.
    float nearest = 1e12f;
    Vec3 stand = move_.origin;
    for (const sf::LevelSpawn& s : level_->spawns) {
        const Vec3 at = settle_spawn(move_def_, *collision_, s.position);
        const Vec3 eye = at + Vec3{0, move_def_.stand_eye, 0};
        float d = eng::length_sq(water - eye);
        if (collision_->trace_ray(eye, water).hit()) d += 4.0e7f;   // out of sight: only if nothing better
        if (d < nearest) nearest = d, stand = at;
    }
    move_.origin = stand;
    move_.velocity = {};
    move_prev_ = move_;
    const Vec3 to = eng::normalize(water - eye_position(move_def_, move_));
    float facing = -2;
    for (int y = 0; y < 360; y += 2)
        for (int p = -40; p <= 20; p += 2)
            if (const float k = eng::dot(eng::angles_to_forward(float(y), float(p)), to); k > facing) facing = k, yaw_ = float(y), pitch_ = float(p);
    recoil_pitch_ = recoil_yaw_ = kick_pitch_ = kick_yaw_ = 0;
    return true;
}

void GameWorld::render() {
    if (!ready_ || !renderer_ok_) return;
    const double now = app_.now();
    light_.exposure = app_.settings().brightness;
    renderer_.set_texture_filter(app_.settings().texture_filter);
    FramePipe& pipe = app_.frame_pipe();
    renderer_.set_sky_moon(!pipe.sky_body());
    renderer_.set_fidelity_sky(pipe.fidelity() && pipe.finish().clouds, pipe.surfaces() && pipe.finish().water);
    renderer_.begin_frame(camera_, light_, now);
    // Soldiers' shadows from the map's sun, before anything they fall on is drawn. Everybody,
    // you too: in first person your body is not on screen, but its shadow is. The map is centred
    // a little way ahead of the eye: the ground you look at is where a missing shadow is noticed.
    bool cast = false;
    if (app_.settings().shadows == Settings::Shadows::Sun && renderer_.begin_shadows(camera_.eye + camera_.forward() * 800.0f, 2600.0f)) {
        for (auto& [id, p] : players_)
            if (!p.hidden) draw_player(p, id, now, true);
        renderer_.end_shadows();
        cast = true;
    }
    shadows_cast_ = cast;
    // Fidelity's lights: the solid world writes its surfaces beside the picture until they are laid on.
    renderer_.set_surfaces(pipe.surfaces());
    renderer_.draw_sky(level_gpu_, camera_);
    renderer_.draw_level(level_gpu_, camera_);
    draw_feet_shadows(now, cast);
    // Everyone else (and you, seen from behind), each with the gun in his hand.
    for (auto& [id, p] : players_) {
        if (id == me_ && (!third_person_ || scoped_)) continue;
        if (id != 0 && id == hidden_body()) continue;   // the camera is inside him
        draw_player(p, id, now);
    }
    draw_floor_weapons();
    draw_cannon_models(now);
    // Your weapon for this frame first: the streaks you fire leave from its muzzle.
    const bool view_model = pose_view_model() && !scoped_;
    for (Tracer& t : tracers_) {
        if (t.from_barrel) {
            // Yours: this frame's barrel, as drawn (seen from behind: your gun's muzzle).
            Vec3 from = t.a;
            if (third_person_ && players_.contains(me_) && now - players_[me_].muzzle_at < 0.25) from = players_[me_].muzzle;
            else if (vm_posed_) from = vm_muzzle_;
            // How far apart this frame's barrel and the streak's start are on screen (tests).
            if (vm_posed_ && !third_person_) {
                const eng::Vec4 a = camera_.view_proj.transform({vm_muzzle_.x, vm_muzzle_.y, vm_muzzle_.z, 1});
                const eng::Vec4 b = camera_.view_proj.transform({from.x, from.y, from.z, 1});
                if (a.w > 1e-3f && b.w > 1e-3f) {
                    const float dx = (a.x / a.w - b.x / b.w) * 0.5f * float(app_.picture_width());
                    const float dy = (a.y / a.w - b.y / b.w) * 0.5f * float(app_.picture_height());
                    tracer_gap_px_ = std::sqrt(dx * dx + dy * dy);
                }
            }
            t.a = from;
            t.from_barrel = false;
        } else if (t.shooter) {
            // Someone else's: his drawn muzzle, when his gun was drawn just now.
            if (auto it = players_.find(t.shooter); it != players_.end() && now - it->second.muzzle_at < 0.25) t.a = it->second.muzzle, ++tracers_muzzle_;
            else ++tracers_eye_;
            t.shooter = 0;
        }
        const Vec3 d = t.b - t.a;
        const float len = eng::length(d);
        if (len < 1.0f || !flare_.tail) continue;
        const Vec3 dir = d / len;
        const float head = std::min(len, float(now - t.time) * kTracerSpeed + kTracerLength * 0.35f);
        const float tail = std::max(0.0f, head - kTracerLength);
        if (head - tail > 1.0f) renderer_.beam(*flare_.tail, t.a + dir * tail, t.a + dir * head, kTracerWidth, 0xFFFFFFFFu);
    }
    // Others' muzzle flashes (flare/sf_point_fire01): a frame or two at each shot.
    for (auto& [id, p] : players_) {
        if (id == me_ && (!third_person_ || scoped_)) continue;
        if (!p.alive || now - p.fired_at > 0.06 || now - p.muzzle_at > 0.25 || !flare_.point) continue;
        const WeaponDef* w = weapon(p.weapon);
        if (!w || w->klass == WeaponClass::Knife || w->klass == WeaponClass::Grenade) continue;
        renderer_.sprite(*flare_.point, p.muzzle + p.muzzle_dir * 6.0f, 34.0f, 0xFFFFFFFFu, frand(g_rng, 0, 6.28f));
    }
    // Grenades in the air, spent cases, marks, blood, dust and blasts (World/Effects.cpp).
    draw_grenades(now);
    draw_effects(now);
    // Fidelity's contact shadow goes on the solid world, before what is painted over it (smoke,
    // flashes, tracers, the gun in hand). Nothing with Fidelity off.
    collect_lights(now);
    {
        // Where the map's light comes from: its sun; under the room's night laid over a day's
        // bake, the moon the night sky hangs there.
        PipeSky sky;
        sky.night = settings_.time_of_day == TimeOfDay::Night;
        sky.body_toward = sky.night && renderer_.night_over_day() ? renderer_.moon_toward() : eng::normalize(light_.sun_direction * -1.0f);
        sky.light_travel = sky.body_toward * -1.0f;
        sky.light_colour = renderer_.light_colour();
        pipe.shade(camera_, sky);
    }
    renderer_.set_surfaces(false);
    renderer_.resume();
    renderer_.flush_lines();
    renderer_.flush_sprites(camera_);

    // Your weapon, in its own pass, and its flash: the front star (flare/4, 5 or 6) and the side
    // flame along the barrel (flare/sf_muzzle's first row).
    // The original's "invisible weapon": nothing in your hands on screen (the tracers still leave
    // from where its muzzle would be).
    if (view_model && !app_.settings().hide_weapon) {
        Animated* vm = view_model_for(loadout_[size_t(slot_)]);
        renderer_.begin_view_model(camera_, app_.settings().viewmodel_fov);
        renderer_.draw_model(*vm->gpu, vm_world_, &vm->skin, {1, 1, 1, 1}, 0.12f, 0);
        if (now - muzzle_flash_at_ < 0.05) {
            const Vec3 at = vm_flame_ + vm_flame_dir_ * 3.0f;
            if (const TexInfo* star = flare_.star[size_t(muzzle_star_)]) renderer_.sprite(*star, at, 22.0f, 0xFFFFFFFFu, frand(g_rng, 0, 6.28f));
            if (flare_.side) {
                const float cell[4] = {0.0f, 0.0f, 0.5f, 1.0f / 6.0f};
                renderer_.beam(*flare_.side, at - vm_flame_dir_ * 2.0f, at + vm_flame_dir_ * 30.0f, 12.0f, 0xFFFFFFFFu, cell);
            }
            renderer_.flush_sprites(camera_);
        }
    }
}

// ── Your guns ────────────────────────────────────────────────────────────────────

const WeaponDef* GameWorld::my_weapon(int slot) const {
    if (slot < 0 || slot >= int(kLoadoutSlots)) return nullptr;
    return weapon(loadout_[size_t(slot)]);
}

// ── Damage numbers ───────────────────────────────────────────────────────────────

void GameWorld::note_damage(u32 victim, int amount, u8 kind, double now) {
    DamageStack& s = damage_numbers_[victim];
    // Three seconds without hurting him: a fresh count.
    if (now - s.last > 3.0) s.hits.clear(), s.total = 0;
    s.hits.push_back({amount, kind, now});
    while (s.hits.size() > 6) s.hits.pop_front();
    s.total += amount;
    s.last = now;
}

std::string GameWorld::damage_numbers_of(u32 id) const {
    auto it = damage_numbers_.find(id);
    if (it == damage_numbers_.end() || app_.now() - it->second.last > 3.0) return {};
    std::string out;
    for (const DamageHit& h : it->second.hits) out += std::to_string(h.amount) + " ";
    return out + "= " + std::to_string(it->second.total);
}

}  // namespace lsf
