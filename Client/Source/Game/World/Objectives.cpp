// The game types on screen (Game/Modes.hpp): what the server's ModeState and ModeEvent say, drawn
// over the match in the original's own art where it has some --
//
//   bomb, escape        inf/bomb.bmp, inf/escape.bmp (the mission's marks)
//   captains            inf/mode/ctc_red.tga, ctc_blue.tga over a captain's head
//   undead skills       inf/mode/zombie/hud_*.tga (dash, jump, smoke, rader, bomb, crashjump, pulling)
//   class select        ui/texture/common/ui_info_zombie_<class>.tga, icon/ui_icon_select_zombie_*.tga
//   strongholds         ui/texture/icon/ui_icon_piratemode_3dpos_capture.tga, _tresurebox.tga
//
// -- and in the HUD's own lettering where it has none: the goal under the clock, a bar for a plant,
// a defuse or a capture, a mark over each site, item, zone and stronghold, the lines of what just
// happened, an undead's skills and their cooldowns, Horror Mode 2's select window.
//
// What you press: Use (E) is held at a site, the bomb, the treasure, the girl (Input's buttons; the
// server times it). An undead's skills are on the keys of weapons 2, 3 and 4, reload and scope.
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

using eng::Mat4;
using eng::Vec3;
using namespace proto;

namespace {

ui::Picture art(std::string_view key) {
    ui::Atlas* a = ui::atlas();
    return a ? a->picture(sf::Pack::Menu, key) : ui::Picture{};
}

// A picture centred on (cx, cy), `h` tall, its own aspect kept; false when the archive lacks it.
bool art_at(VanDrawList* dl, std::string_view key, float cx, float cy, float h, VanU32 tint = 0xFFFFFFFF) {
    const ui::Picture p = art(key);
    if (!p.valid()) return false;
    const float w = h * p.w / std::max(1.0f, p.h);
    ui::picture(dl, p, cx - w * 0.5f, cy - h * 0.5f, w, h, tint, false);
    return true;
}

const char* skill_icon(Skill s) {
    switch (s) {
        case Skill::Speed: return "inf/mode/zombie/hud_dash.tga";
        case Skill::Jump: return "inf/mode/zombie/hud_jump.tga";
        case Skill::Smoke: return "inf/mode/zombie/hud_smoke.tga";
        case Skill::Search: return "inf/mode/zombie/hud_rader.tga";
        case Skill::SelfBomb: return "inf/mode/zombie/hud_bomb.tga";
        case Skill::Crush: return "inf/mode/zombie/hud_crashjump.tga";
        case Skill::Drilling: return "inf/mode/zombie/hud_hand.tga";
        case Skill::Throw: return "inf/mode/zombie/hud_grenade.tga";
        case Skill::Snatch: return "inf/mode/zombie/hud_pulling.tga";
        default: return "";
    }
}

// The keys an undead's skills are on, in its class's order (Game/Modes.hpp UndeadDef::skills).
constexpr Action kSkillKeys[5] = {Action::Weapon2, Action::Weapon3, Action::Weapon4, Action::Reload, Action::Zoom};

// What a map's mission item is (MapName.txt's own words for it).
std::string item_name(std::string_view map) {
    if (map == "hospital") return "DNA sample";
    if (map == "nervegas" || map == "nervegashorror") return "nerve gas sample";
    if (map == "emp") return "EMP controller";
    if (map == "village" || map == "fortress") return "laptop";
    if (map == "canyon") return "satellite modem";
    if (map == "kinabalu") return "black box";
    if (map == "silo") return "sample";
    return "objective";
}

const char* stronghold_name(u8 index) {
    static const char* names[] = {"the pirates' stronghold", "the centre stronghold", "the marines' stronghold"};
    return index < 3 ? names[index] : "a stronghold";
}

Undead class_at(int i) {
    static const Undead kClasses[] = {Undead::Boss, Undead::Driller, Undead::Heavy, Undead::Hunter};
    return kClasses[size_t(std::clamp(i, 0, 3))];
}

}  // namespace

// ── The server's word ─────────────────────────────────────────────────────────

const RoleNow* GameWorld::role_of(u32 id) const {
    for (const RoleNow& r : mode_state_.roles)
        if (r.player == id) return &r;
    return nullptr;
}

bool GameWorld::is_undead(u32 id) const {
    const RoleNow* r = role_of(id);
    return r && (MatchRole(r->role) == MatchRole::Host || MatchRole(r->role) == MatchRole::Zombie);
}

bool GameWorld::me_undead() const {
    return is_undead(me_) || test_undead_ != Undead::None;
}

void GameWorld::mode_note(std::string text, VanU32 colour) {
    mode_notes_.push_back({std::move(text), colour, app_.now()});
    while (mode_notes_.size() > 4) mode_notes_.pop_front();
}

void GameWorld::on_mode_state(const ModeState& m) {
    mode_state_ = m;
    mode_state_at_ = app_.now();
    // What each soldier now is: his body (an undead's, or his force's), his side (Horror's change
    // with infection), a captain's head, out of the round by escaping.
    const bool horror = settings_.mode == Mode::Horror || settings_.mode == Mode::Horror2;
    for (auto& [id, p] : players_) {
        const RoleNow* r = role_of(id);
        const bool dead_body = r && Undead(r->undead) != Undead::None;
        const std::string look = dead_body ? std::string("undead:") + undead_def(Undead(r->undead)).model : look_of(p.force, p.parts);
        if (look != p.look) {
            p.look = look;
            auto it = force_models_.find(look);
            p.body.model = it != force_models_.end() ? it->second : nullptr;
            p.lower_clip.clear(), p.upper_clip.clear();
        }
        if (horror && p.team != Team::Observer) p.team = is_undead(id) ? Team::Red : Team::Blue;
        p.big_head = r && MatchRole(r->role) == MatchRole::Captain;
        p.escaped = r && MatchRole(r->role) == MatchRole::Escaped;
    }
    if (horror)
        if (auto it = players_.find(me_); it != players_.end() && it->second.team != Team::Observer) team_ = it->second.team;
    // Horror Mode 2's window: up while the server says you are picking.
    const RoleNow* mine = role_of(me_);
    if (!mine || !(mine->flags & kRolePicking)) pick_sent_ = false;
}

void GameWorld::on_mode_event(const ModeEvent& e) {
    const ui::Palette& pal = ui::pal();
    const double now = app_.now();
    auto name = [&](u32 id) -> std::string {
        if (id == me_) return "You";
        auto it = players_.find(id);
        return it == players_.end() ? std::string("Someone") : it->second.name;
    };
    const VanU32 side_ink = Team(e.team) == Team::Red ? ui::col(pal.red) : Team(e.team) == Team::Blue ? ui::col(pal.blue) : ui::col(pal.gold);
    const std::string item = item_name(settings_.map);
    // The medal (the kill table's SPECIALPOINT; the undead's game types and the Pirate Ship have
    // their own) for an objective you did yourself: the bomb set or made safe, the thing brought
    // home, an escape, a console or a stronghold taken, the girl rescued. The Pirate Ship's
    // treasure opened is its TREASURE mark.
    if (e.who == me_ && e.who != 0) {
        const ModeEventKind kind = ModeEventKind(e.kind);
        const bool point = kind == ModeEventKind::Planted || kind == ModeEventKind::Defused || kind == ModeEventKind::Delivered || kind == ModeEventKind::Escaped ||
                           kind == ModeEventKind::ConsoleTaken || kind == ModeEventKind::StrongholdTaken || kind == ModeEventKind::GirlRescued;
        const bool treasure = kind == ModeEventKind::TreasureOpened && e.value != 0;
        if (point || treasure) {
            const bool horror = settings_.mode == Mode::Horror || settings_.mode == Mode::Horror2;
            show_effect(settings_.mode == Mode::Pirate ? (treasure ? "P_OCCUPY" : "P_SPECIALPOINT") : horror ? "Z_SPECIALPOINT" : "SPECIALPOINT");
            audio_->special_point();
        }
    }
    switch (ModeEventKind(e.kind)) {
        case ModeEventKind::Planting: break;   // the bar under the clock says so
        case ModeEventKind::Planted:
            mode_note(eng::str::format("The bomb is set at site %c: %.0f seconds!", 'A' + e.value, double(kFuseSeconds)), ui::col(pal.red));
            banner_ = "BOMB PLANTED", banner_at_ = now;
            break;
        case ModeEventKind::Defusing: mode_note(name(e.who) + " is making the bomb safe", ui::col(pal.blue)); break;
        case ModeEventKind::Defused:
            mode_note(name(e.who) + " made the bomb safe", ui::col(pal.blue));
            banner_ = "BOMB DEFUSED", banner_at_ = now;
            break;
        case ModeEventKind::Exploded:
            detonate(e.at + Vec3{0, 60, 0}, kNoWeapon);
            shake_ = std::max(shake_, eng::length(e.at - camera_.eye) < kBlastRadius * 2 ? 1.0f : 0.3f);
            mode_note("The bomb went off!", ui::col(pal.red));
            break;
        case ModeEventKind::Taken: mode_note(name(e.who) + (e.who == me_ ? " have the " : " has the ") + item, side_ink); break;
        case ModeEventKind::Dropped: mode_note("The " + item + " is down", ui::col(pal.warn)); break;
        case ModeEventKind::Returned: mode_note("The " + item + " is back in its place", ui::col(pal.blue)); break;
        case ModeEventKind::Delivered: mode_note(name(e.who) + " brought the " + item + " home!", side_ink); break;
        case ModeEventKind::Escaped:
            mode_note(eng::str::format("%s escaped (%u of %u)", name(e.who).c_str(), unsigned(e.value), unsigned(mode_state_.counts[1])), ui::col(pal.red));
            if (auto it = players_.find(e.who); it != players_.end()) it->second.escaped = true, it->second.alive = false;
            if (e.who == me_) alive_ = false;
            break;
        case ModeEventKind::CaptainDown:
            mode_note(std::string(Team(e.team) == Team::Red ? "Red's" : "Blue's") + " captain is down" + (e.other ? ": " + name(e.other) : std::string()),
                      Team(e.team) == Team::Red ? ui::col(pal.red) : ui::col(pal.blue));
            break;
        case ModeEventKind::Infected: mode_note(name(e.who) + (e.who == me_ ? " were" : " was") + " infected", ui::col(pal.bad)); break;
        case ModeEventKind::HostsTurned:
            if (now - hosts_turned_at_ > 5.0) {
                hosts_turned_at_ = now;
                banner_ = "THE HOSTS HAVE TURNED", banner_at_ = now;
            }
            if (e.who == me_) mode_note("You are a host zombie: spread it to every last human", ui::col(pal.bad));
            break;
        case ModeEventKind::ConsoleTaken: mode_note(eng::str::format("Console %c taken", 'A' + e.value), ui::col(pal.red)); break;
        case ModeEventKind::StrongholdTaken:
            mode_note(std::string(Team(e.team) == Team::Red ? "The pirates" : "The marines") + " took " + stronghold_name(e.value), side_ink);
            break;
        case ModeEventKind::TreasureOpened:
            if (e.value == 0) mode_note("A treasure box has washed up!", ui::col(pal.gold));
            else mode_note(name(e.who) + eng::str::format(" opened the treasure: +%d", kTreasurePoints), side_ink);
            break;
        case ModeEventKind::SupplyTaken:
            if (e.who == me_ && (e.value == 4 || e.value == 5))
                for (int i = 0; i < 4; ++i)
                    if (const WeaponDef* w = my_weapon(i)) reserve_[size_t(i)] = w->reserve;
            if (e.who == me_) {
                static const char* kinds[] = {"First aid", "Team heal", "Power x2", "Power x3", "Ammunition", "Ammunition"};
                mode_note(std::string("Supply: ") + kinds[std::min<int>(e.value, 5)], ui::col(pal.good));
            }
            break;
        case ModeEventKind::GirlRescued: mode_note(name(e.who) + " rescued the girl: every human made whole", ui::col(pal.good)); break;
        case ModeEventKind::SkillUsed:
            switch (Skill(e.value)) {
                case Skill::SelfBomb:
                case Skill::Crush:
                case Skill::Throw:
                    detonate(e.at + Vec3{0, 40, 0}, kNoWeapon);
                    break;
                case Skill::Snatch:
                    // Pulled in front of the Hunter: your own soldier is put there.
                    if (e.other == me_ && alive_) {
                        move_.origin = e.at;
                        move_.velocity = {};
                        move_prev_ = move_;
                        mode_note("A Hunter dragged you in!", ui::col(pal.bad));
                    }
                    break;
                default: break;
            }
            break;
        case ModeEventKind::RageOn:
            if (e.who == me_) banner_ = "RAGE", banner_at_ = now;
            break;
        case ModeEventKind::MagazineTaken:
            if (e.who == me_)
                for (int i = 0; i < 4; ++i)
                    if (const WeaponDef* w = my_weapon(i)) reserve_[size_t(i)] = w->reserve;
            break;
        case ModeEventKind::RankUp: mode_note(name(e.who) + eng::str::format(" evolved: rank %u", unsigned(e.value)), ui::col(pal.bad)); break;
        case ModeEventKind::SidesChanged: {
            // The half: everyone is on the other side (each soldier's next spawn says so too).
            auto other = [](Team t) { return t == Team::Red ? Team::Blue : t == Team::Blue ? Team::Red : t; };
            for (auto& [id, p] : players_) p.team = other(p.team);
            team_ = other(team_);
            std::swap(red_score_, blue_score_);
            sides_changed_at_ = now;
            banner_ = "SIDES CHANGED", banner_at_ = now;
            if (team_ == Team::Red || team_ == Team::Blue)
                mode_note(std::string("Sides changed: you are ") + (team_ == Team::Red ? "Red" : "Blue") + " now, and " + (team_ == Team::Red ? "attack" : "defend"),
                          team_ == Team::Red ? ui::col(pal.red) : ui::col(pal.blue));
            break;
        }
        default: break;
    }
}

// ── What you press ────────────────────────────────────────────────────────────

// ── Pirate Mode's cannons ─────────────────────────────────────────────────────

// How far round the cannon's model is turned from the way its table points it (degrees).
constexpr float kCannonModelTurn = -90.0f;   // its barrel lies along its own +x

bool GameWorld::manning_cannon() const {
    if (settings_.mode != Mode::Pirate) return false;
    for (const ObjectiveNow& o : mode_state_.objectives)
        if (Objective(o.kind) == Objective::Cannon && o.who == me_ && me_ != 0) return true;
    return false;
}

// The cannons where they stand, each pointing its way, and the balls in the air (the client's own
// models: the force archives' sf_c_cannon_body and sf_a_cannon_bullet).
void GameWorld::draw_cannon_models(double now) {
    if (settings_.mode != Mode::Pirate || !renderer_ok_) return;
    // A model at rest: every bone where its file has it.
    auto at_rest = [](const sf::Model& m, std::vector<Mat4>& skin) {
        std::vector<Mat4> local(m.bones.size()), in_model;
        for (size_t i = 0; i < local.size(); ++i) local[i] = m.bones[i].bind_local;
        sf::pose_to_model(m, local, in_model);
        sf::skin_matrices(m, in_model, skin);
    };
    if (cannon_model_ && !cannon_gpu_) {
        cannon_gpu_ = renderer_.upload_model(*cannon_model_, sf::Pack::Force);
        at_rest(*cannon_model_, cannon_skin_);
        const eng::Aabb b = cannon_model_->bounds();
        LOG_INFO("Cannon model: %zu meshes, %zu bones, bounds (%.0f %.0f %.0f) - (%.0f %.0f %.0f)", cannon_model_->meshes.size(), cannon_model_->bones.size(), double(b.min.x),
                 double(b.min.y), double(b.min.z), double(b.max.x), double(b.max.y), double(b.max.z));
    }
    if (ball_model_ && !ball_gpu_) {
        ball_gpu_ = renderer_.upload_model(*ball_model_, sf::Pack::Force);
        at_rest(*ball_model_, ball_skin_);
        const eng::Aabb b = ball_model_->bounds();
        LOG_INFO("Cannon ball model: %zu meshes, bounds (%.1f %.1f %.1f) - (%.1f %.1f %.1f)", ball_model_->meshes.size(), double(b.min.x), double(b.min.y), double(b.min.z), double(b.max.x),
                 double(b.max.y), double(b.max.z));
    }
    if (cannon_gpu_)
        for (const ObjectiveNow& o : mode_state_.objectives) {
            if (Objective(o.kind) != Objective::Cannon) continue;
            const auto spots = pirate_cannons(settings_.map);
            if (o.index >= spots.size()) continue;
            const CannonSpot& c = spots[o.index];
            renderer_.draw_model(*cannon_gpu_, Mat4::yaw(eng::forward_to_yaw(c.forward) + kCannonModelTurn) * Mat4::translation(c.at), &cannon_skin_);
        }
    if (ball_gpu_ && ball_model_) {
        // The original's ball at the size its table gives it (cannon_state.kst BulletSize 0.6:
        // some 45 cm across), tumbling as it flies.
        constexpr float kBallSize = 0.6f, kBallSpin = 6.0f;
        const eng::Aabb b = ball_model_->bounds();
        const Vec3 mid = (b.min + b.max) * 0.5f;
        for (const BallFx& f : balls_) {
            if (now - f.born > double(kCannonLife)) continue;
            const float age = float(now - f.born);
            const Vec3 at = cannon_ball_at(f.origin, f.velocity, age);
            renderer_.draw_model(*ball_gpu_,
                                 Mat4::translation(mid * -1.0f) * Mat4::rotation_x(age * kBallSpin) * Mat4::scale(Vec3{kBallSize, kBallSize, kBallSize}) * Mat4::translation(at),
                                 &ball_skin_);
        }
    }
}

std::string GameWorld::ball_report() const {
    if (balls_.empty()) return "no ball";
    const BallFx& b = balls_.front();
    const float age = float(app_.now() - b.born);
    const Vec3 at = cannon_ball_at(b.origin, b.velocity, age);
    VanVec2 s{0, 0};
    const bool seen = to_screen(at, s);
    return eng::str::format("%.2f s old at (%.0f %.0f %.0f), %.0f cm from the eye, %s (%.0f, %.0f) of the stage", double(age), double(at.x), double(at.y), double(at.z),
                            double(eng::length(at - camera_.eye)), seen ? "on screen at" : "off screen", double(s.x), double(s.y));
}

void GameWorld::draw_cannons(double now) {
    if (settings_.mode != Mode::Pirate) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    // The balls in the air are their own model (draw_cannon_models); a plain dark ball stands in
    // only where the client has none to draw.
    std::erase_if(balls_, [&](const BallFx& b) { return now - b.born > double(kCannonLife); });
    for (const BallFx& b : balls_) {
        if (ball_gpu_) break;
        const Vec3 at = cannon_ball_at(b.origin, b.velocity, float(now - b.born));
        VanVec2 s;
        if (!to_screen(at, s)) continue;
        const float r = std::clamp(9000.0f / std::max(60.0f, eng::length(at - camera_.eye)), 2.0f, 26.0f);
        dl->AddCircleFilled(ui::stage(s.x, s.y), ui::px(r), VAN_COL32(18, 18, 20, 255), 20);
        dl->AddCircleFilled(ui::stage(s.x - r * 0.3f, s.y - r * 0.3f), ui::px(r * 0.3f), VAN_COL32(120, 120, 126, 200), 12);
    }
    if (!alive_ || watching_) return;
    // What to press: beside a free one, and at yours.
    const unsigned use = app_.settings().bind(Action::Use), fire = app_.settings().bind(Action::Shoot);
    const std::string use_key = use ? eng::Input::binding_name(use) : std::string("Use"), fire_key = fire ? eng::Input::binding_name(fire) : std::string("Fire");
    const float W = ui::stage_w(), H = ui::stage_h();
    for (const ObjectiveNow& o : mode_state_.objectives) {
        if (Objective(o.kind) != Objective::Cannon) continue;
        if (o.who == me_) {
            const std::string line = o.timer > 0 ? eng::str::format("Loading...  %.1f s        %s: leave the cannon", double(o.timer), use_key.c_str())
                                                 : fire_key + ": fire        " + use_key + ": leave the cannon";
            ui::text(dl, ui::font_heading(), 20, W * 0.5f, H * 0.68f, ui::col(o.timer > 0 ? pal.text_dim : pal.gold_bright), line, ui::Align::Center);
            return;
        }
    }
    for (const ObjectiveNow& o : mode_state_.objectives)
        if (Objective(o.kind) == Objective::Cannon && o.who == 0 && eng::length(o.at - feet()) <= kCannonReach) {
            ui::text(dl, ui::font_heading(), 20, W * 0.5f, H * 0.68f, ui::col(pal.gold_bright), use_key + ": man the cannon", ui::Align::Center);
            return;
        }
}

// Horror Mode's items by side, on the three item keys (Rebirth goes by itself).
constexpr HorrorItem kHumanItems[3] = {HorrorItem::RescueKit, HorrorItem::SilverBullet, HorrorItem::BlindCleanse};
constexpr HorrorItem kUndeadItems[3] = {HorrorItem::BloodSucking, HorrorItem::ShoutOfAnger, HorrorItem::UndeadSpeedUp};
constexpr Action kItemKeys[3] = {Action::Item1, Action::Item2, Action::Item3};

void GameWorld::draw_horror_items(double now) {
    (void)now;
    if ((settings_.mode != Mode::Horror && settings_.mode != Mode::Horror2) || watching_) return;
    const auto& have = app_.session().horror_items;
    if (std::all_of(have.begin(), have.end(), [](u16 n) { return n == 0; })) return;   // nothing carried: nothing shown
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const bool zombie = me_undead();
    const float x = ui::stage_w() - 66;
    float y = ui::stage_h() * 0.5f - 104;
    auto slot = [&](HorrorItem it, const std::string& key) {
        const u16 n = have[size_t(it)];
        dl->AddRectFilled(ui::stage(x, y), ui::stage(x + 46, y + 46), VAN_COL32(0, 0, 0, 160), ui::px(4));
        (void)art_at(dl, horror_item(it).icon, x + 23, y + 23, 36, n ? 0xFFFFFFFF : VAN_COL32(90, 90, 90, 255));
        ui::text(dl, ui::font_bold(), 11, x + 3, y + 1, ui::col(pal.gold), key, ui::Align::Left);
        ui::text(dl, ui::font_bold(), 12, x + 43, y + 30, ui::col(n ? pal.text : pal.text_dim), eng::str::format("%u", unsigned(n)), ui::Align::Right);
        y += 52;
    };
    if (!zombie) slot(HorrorItem::Rebirth, "auto");
    for (int k = 0; k < 3; ++k) {
        const unsigned code = app_.settings().bind(kItemKeys[k]);
        slot(zombie ? kUndeadItems[k] : kHumanItems[k], code ? eng::Input::binding_name(code) : std::string("-"));
    }
}

void GameWorld::update_mode_input(double now) {
    (void)now;
    if (!control_ || watching_) return;
    const RoleNow* mine = role_of(me_);
    // Pirate Mode's cannons: Use beside a free one mans it; at yours, the trigger fires it and Use leaves it.
    if (alive_ && settings_.mode == Mode::Pirate) {
        const ObjectiveNow* mine_now = nullptr;
        const ObjectiveNow* beside = nullptr;
        for (const ObjectiveNow& o : mode_state_.objectives) {
            if (Objective(o.kind) != Objective::Cannon) continue;
            if (o.who == me_) mine_now = &o;
            else if (o.who == 0 && eng::length(o.at - feet()) <= kCannonReach && !beside) beside = &o;
        }
        CannonUse m;
        if (mine_now) {
            m.index = mine_now->index;
            if (binding_pressed(Action::Use)) m.op = u8(CannonOp::Leave), app_.session().send(m);
            else if (binding_pressed(Action::Shoot) && mine_now->timer <= 0) m.op = u8(CannonOp::Fire), m.aim = camera_.forward(), app_.session().send(m);
        } else if (beside && binding_pressed(Action::Use)) {
            m.op = u8(CannonOp::Man), m.index = beside->index;
            app_.session().send(m);
        }
    }
    // Horror Mode's items: your side's three, each on its key, while you have one.
    if (alive_ && (settings_.mode == Mode::Horror || settings_.mode == Mode::Horror2))
        for (int k = 0; k < 3; ++k) {
            const HorrorItem it = me_undead() ? kUndeadItems[k] : kHumanItems[k];
            if (!binding_pressed(kItemKeys[k]) || app_.session().horror_items[size_t(it)] == 0) continue;
            UseHorrorItem m;
            m.item = u8(it);
            app_.session().send(m);
        }
    // Horror Mode 2's select window: left and right choose, down (or Enter) picks.
    if (mine && (mine->flags & kRolePicking) && !pick_sent_) {
        const auto& in = app_.window().input();
        if (in.key_pressed(VK_LEFT)) pick_index_ = (pick_index_ + 3) % 4;
        if (in.key_pressed(VK_RIGHT)) pick_index_ = (pick_index_ + 1) % 4;
        for (int i = 0; i < 4; ++i)
            if (in.key_pressed(u32('1' + i))) pick_index_ = i;
        if (in.key_pressed(VK_DOWN) || in.key_pressed(VK_RETURN)) {
            const Undead u = class_at(pick_index_);
            if (undead_def(u).unlock_rank <= mine->rank) {
                PickClass m;
                m.undead = u8(u);
                app_.session().send(m);
                pick_sent_ = true;
            }
        }
        return;
    }
    // An undead's skills.
    if (!alive_ || !mine || Undead(mine->undead) == Undead::None) return;
    const UndeadDef& d = undead_def(Undead(mine->undead));
    for (size_t k = 0; k < 5; ++k) {
        if (d.skills[k] == kSkillNone || !binding_pressed(kSkillKeys[k]) || mode_state_.cooldowns[k] > 0) continue;
        UseSkill m;
        m.skill = d.skills[k];
        m.aim = camera_.forward();
        app_.session().send(m);
    }
}

void GameWorld::test_pick_class(Undead u) {
    for (int i = 0; i < 4; ++i)
        if (class_at(i) == u) pick_index_ = i;
    PickClass m;
    m.undead = u8(u);
    app_.session().send(m);
    pick_sent_ = true;
}

// ── Seen ──────────────────────────────────────────────────────────────────────

bool GameWorld::to_screen(const Vec3& at, VanVec2& out) const {
    const eng::Vec4 v = camera_.view_proj.transform({at.x, at.y, at.z, 1});
    if (v.w < 5.0f) return false;
    const float x = v.x / v.w, y = v.y / v.w;
    if (std::fabs(x) > 1.1f || std::fabs(y) > 1.1f) return false;
    out = {(x * 0.5f + 0.5f) * ui::stage_w(), (0.5f - y * 0.5f) * ui::stage_h()};
    return true;
}

std::string GameWorld::mode_line() const {
    const bool red = team_ == Team::Red;
    const std::string item = item_name(settings_.map);
    const Mission mission = level_ ? map_rules(*level_).mission : Mission::Elimination;
    switch (settings_.mode) {
        case Mode::TeamBattle:
            switch (mission) {
                case Mission::Blast: return red ? "Plant the bomb at site A or B" : "Hold sites A and B";
                case Mission::Capture: return red ? "Take the " + item + " back to your base" : "Keep the " + item + " from them";
                case Mission::Escape:
                    return eng::str::format("%s: %u of %u out", red ? "Break through and escape" : "Stop them escaping", unsigned(mode_state_.counts[0]),
                                            unsigned(mode_state_.counts[1]));
                case Mission::Dual: return "Bring the " + item + " to your base first";
                default: return "Eliminate the enemy";
            }
        case Mode::TeamDeathmatch:
        case Mode::TeamSlayer: return eng::str::format("First side to %u points", unsigned(settings_.goal));
        case Mode::SingleBattle: return eng::str::format("First to %u kills", unsigned(settings_.goal));
        case Mode::Sniper: return eng::str::format("First side to %u kills", unsigned(settings_.goal));
        case Mode::CaptureTheCaptain: return role_of(me_) && MatchRole(role_of(me_)->role) == MatchRole::Captain ? "You are the captain: stay alive" : "Bring down their captain";
        case Mode::Captain: {
            int left[2] = {0, 0};
            for (const RoleNow& r : mode_state_.roles)
                if (MatchRole(r.role) == MatchRole::Captain)
                    if (auto it = players_.find(r.player); it != players_.end() && it->second.alive && (it->second.team == Team::Red || it->second.team == Team::Blue))
                        ++left[it->second.team == Team::Red ? 0 : 1];
            return eng::str::format("Captains standing: red %d, blue %d", left[0], left[1]);
        }
        case Mode::Horror:
            if (mode_state_.phase == 0) return eng::str::format("A rotten smell of the undead... %.0f", double(std::ceil(mode_state_.phase_left)));
            return eng::str::format("Humans %u   Undead %u", unsigned(mode_state_.counts[2]), unsigned(mode_state_.counts[3]));
        case Mode::Horror2: return eng::str::format("Humans %u   Undead %u", unsigned(mode_state_.counts[2]), unsigned(mode_state_.counts[3]));
        case Mode::Occupy: return red ? "Take both missile consoles, or bring the sample to one" : "Hold the missile consoles";
        case Mode::Pirate: return eng::str::format("Hold the strongholds: first side to %u", unsigned(settings_.goal));
        default: return {};
    }
}

void GameWorld::draw_objectives(double now) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float cx = ui::stage_w() * 0.5f;
    float y = 66;
    const std::string line = mode_line();
    if (!line.empty()) {
        ui::text(dl, ui::font_bold(), 15, cx + 1, y + 1, VAN_COL32(0, 0, 0, 200), line, ui::Align::Center);
        ui::text(dl, ui::font_bold(), 15, cx, y, ui::col(pal.lime), line, ui::Align::Center);
        y += 22;
    }
    // A bar: a plant, a defuse, a capture, the treasure, the girl -- whoever is at it.
    auto bar = [&](const std::string& label, float progress, VanU32 ink) {
        const float w = 260;
        dl->AddRectFilled(ui::stage(cx - w * 0.5f, y), ui::stage(cx + w * 0.5f, y + 14), VAN_COL32(0, 0, 0, 170));
        dl->AddRectFilled(ui::stage(cx - w * 0.5f + 2, y + 2), ui::stage(cx - w * 0.5f + 2 + (w - 4) * std::clamp(progress, 0.0f, 1.0f), y + 12), ink);
        ui::text(dl, ui::font_bold(), 12, cx, y + 15, ui::col(pal.text), label, ui::Align::Center);
        y += 34;
    };
    auto who = [&](u32 id) {
        auto it = players_.find(id);
        return id == me_ ? std::string("You") : it == players_.end() ? std::string() : it->second.name;
    };
    for (const ObjectiveNow& o : mode_state_.objectives) {
        switch (Objective(o.kind)) {
            case Objective::Bomb:
                if (o.state == 1) bar(who(o.who) + eng::str::format(" setting the bomb at site %c", 'A' + o.index), o.progress, ui::col(pal.red));
                if (o.state == 2 || o.state == 3) {
                    // The fuse, big, until it goes off or is made safe.
                    const int s = int(std::ceil(o.timer));
                    const bool flash = s <= 10 && std::fmod(now, 0.5) < 0.25;
                    art_at(dl, "inf/bomb.bmp", cx - 70, y + 14, 30);
                    ui::text(dl, ui::font_heading(), 28, cx + 10, y, ui::col(flash ? pal.warn : pal.red), eng::str::format("0:%02d", s), ui::Align::Center);
                    y += 36;
                    if (o.state == 3) bar(who(o.who) + " making it safe", o.progress, ui::col(pal.blue));
                }
                break;
            case Objective::Item:
                if (o.state == 2 && Team(o.team) == Team::Blue && o.progress > 0) bar("Sending it home", o.progress, ui::col(pal.blue));
                break;
            case Objective::Console:
                if (Team(o.team) != Team::Red && o.progress > 0.01f) bar(eng::str::format("Console %c", 'A' + o.index), o.progress, ui::col(pal.red));
                break;
            case Objective::Girl:
            case Objective::Treasure:
                if (o.who && o.progress > 0.01f) bar(who(o.who) + (Objective(o.kind) == Objective::Girl ? " rescuing the girl" : " opening the treasure"), o.progress, ui::col(pal.gold));
                break;
            default: break;
        }
    }
    // The Pirate Ship's three strongholds, their colours and who is taking them.
    if (settings_.mode == Mode::Pirate) {
        float sx = cx - 60;
        for (const ObjectiveNow& o : mode_state_.objectives) {
            if (Objective(o.kind) != Objective::Stronghold) continue;
            const VanU32 ink = Team(o.team) == Team::Red ? ui::col(pal.red) : Team(o.team) == Team::Blue ? ui::col(pal.blue) : VAN_COL32(150, 150, 140, 255);
            dl->AddCircleFilled(ui::stage(sx, y + 12), ui::px(12), VAN_COL32(0, 0, 0, 170), 24);
            dl->AddCircleFilled(ui::stage(sx, y + 12), ui::px(10), ink, 24);
            if (Team(o.who) != Team::None && o.progress < 1.0f) {
                const VanU32 taking = Team(o.who) == Team::Red ? ui::col(pal.red) : ui::col(pal.blue);
                dl->PathArcTo(ui::stage(sx, y + 12), ui::px(14), -1.5708f, -1.5708f + 6.2832f * o.progress, 24);
                dl->PathStroke(taking, 0, 3.0f);
            }
            sx += 60;
        }
        y += 30;
    }
    // What just happened, newest at the bottom, each for six seconds.
    for (const ModeNote& n : mode_notes_) {
        const float age = float(now - n.at);
        if (age > 6.0f) continue;
        const float a = std::clamp(6.0f - age, 0.0f, 1.0f);
        ui::text(dl, ui::font_bold(), 14, cx + 1, y + 1, VAN_COL32(0, 0, 0, int(200 * a)), n.text, ui::Align::Center);
        ui::text(dl, ui::font_bold(), 14, cx, y, (n.colour & 0x00FFFFFFu) | (u32(255 * a) << 24), n.text, ui::Align::Center);
        y += 19;
    }
    // Team Slayer: the rage gauge over the health (inf/mode/teamdeathmatch2's rage bar, in its colours).
    if (settings_.mode == Mode::TeamSlayer && alive_) {
        const RoleNow* mine = role_of(me_);
        const bool raging = mine && (mine->flags & kRoleRage);
        const float fill = raging ? 1.0f : float(mode_state_.rage) / float(kRageDeaths);
        const float x0 = 24, y0 = ui::stage_h() - 158;
        ui::text(dl, ui::font_heading(), 15, x0, y0 - 18, raging ? ui::col(pal.red) : ui::col(pal.text_dim), raging ? "RAGE" : "Rage", ui::Align::Left);
        dl->AddRectFilled(ui::stage(x0, y0), ui::stage(x0 + 190, y0 + 10), VAN_COL32(40, 40, 40, 200));
        dl->AddRectFilled(ui::stage(x0, y0), ui::stage(x0 + 190 * std::clamp(fill, 0.0f, 1.0f), y0 + 10), raging ? VAN_COL32(230, 60, 30, 255) : VAN_COL32(150, 40, 25, 230));
    }
}

void GameWorld::draw_markers(double now) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const bool red = team_ == Team::Red;
    const bool human = !me_undead();
    auto label = [&](const Vec3& at, const std::string& text, VanU32 ink, const char* icon = nullptr, float lift = 70) {
        VanVec2 s;
        if (!to_screen(at + Vec3{0, lift, 0}, s)) return;
        const float d = eng::length(at - camera_.eye) / 100.0f;
        if (icon && art_at(dl, icon, s.x, s.y - 10, 26)) {
        } else {
            dl->AddCircleFilled(ui::stage(s.x, s.y - 8), ui::px(11), VAN_COL32(0, 0, 0, 160), 20);
            dl->AddCircle(ui::stage(s.x, s.y - 8), ui::px(11), ink, 20, 2.0f);
            if (text.size() == 1) {
                ui::text(dl, ui::font_bold(), 14, s.x, s.y - 16, ink, text, ui::Align::Center);
                ui::text(dl, ui::font_body(), 11, s.x, s.y + 6, ui::col(pal.text_dim), eng::str::format("%.0fm", double(d)), ui::Align::Center);
                return;
            }
        }
        ui::text(dl, ui::font_bold(), 13, s.x + 1, s.y - 37, VAN_COL32(0, 0, 0, 200), text, ui::Align::Center);
        ui::text(dl, ui::font_bold(), 13, s.x, s.y - 38, ink, text, ui::Align::Center);
        ui::text(dl, ui::font_body(), 11, s.x, s.y + 6, ui::col(pal.text_dim), eng::str::format("%.0fm", double(d)), ui::Align::Center);
    };
    const Mission mission = level_ ? map_rules(*level_).mission : Mission::Elimination;
    for (const ObjectiveNow& o : mode_state_.objectives) {
        switch (Objective(o.kind)) {
            case Objective::Site: {
                // A site taken by a plant is marked by the bomb.
                bool planted = false;
                for (const ObjectiveNow& b : mode_state_.objectives)
                    if (Objective(b.kind) == Objective::Bomb && b.index == o.index && b.state >= 2) planted = true;
                if (!planted) label(o.at, std::string(1, char('A' + o.index)), red ? ui::col(pal.red) : ui::col(pal.blue));
                break;
            }
            case Objective::Bomb:
                if (o.state >= 2 && o.state <= 3) label(o.at, eng::str::format("0:%02d", int(std::ceil(o.timer))), ui::col(pal.red), "inf/bomb.bmp", 40);
                break;
            case Objective::Item: {
                if (o.state == 3) break;
                if (o.state == 1 && o.who == me_) break;   // you have it
                const VanU32 ink = o.state == 1 ? ui::col(pal.warn) : ui::col(pal.gold);
                label(o.at, o.state == 1 ? "CARRIED" : o.state == 2 ? "DROPPED" : item_name(settings_.map), ink, nullptr, o.state == 1 ? 200 : 40);
                break;
            }
            case Objective::Zone:
                // Where your side is sent: a way out, home with the item.
                if ((Team(o.team) == team_ && (mission == Mission::Escape || mission == Mission::Dual)) ||
                    (mission == Mission::Capture && red && Team(o.team) == Team::Red))
                    label(o.at, mission == Mission::Escape ? "ESCAPE" : "BASE", ui::col(pal.good), mission == Mission::Escape ? "inf/escape.bmp" : nullptr, 60);
                break;
            case Objective::Console:
                label(o.at, std::string("Console ") + char('A' + o.index), Team(o.team) == Team::Red ? ui::col(pal.red) : ui::col(pal.text), nullptr, 120);
                break;
            case Objective::Stronghold: {
                const VanU32 ink = Team(o.team) == Team::Red ? ui::col(pal.red) : Team(o.team) == Team::Blue ? ui::col(pal.blue) : ui::col(pal.text);
                label(o.at, o.index == 1 ? "CENTRE" : o.index == 0 ? "PIRATES'" : "MARINES'", ink, "ui/texture/icon/ui_icon_piratemode_3dpos_capture.tga", 120);
                break;
            }
            case Objective::Treasure:
                if (o.state == 1 || o.state == 2) label(o.at, "TREASURE", ui::col(pal.gold), "ui/texture/icon/ui_icon_piratemode_3dpos_tresurebox.tga", 60);
                break;
            case Objective::Cannon:
                // Marked from near by: free, yours, or somebody's.
                if (eng::length(o.at - camera_.eye) < 2500.0f)
                    label(o.at, o.who == me_ ? "YOUR CANNON" : o.who ? "MANNED" : "CANNON", o.who == me_ ? ui::col(pal.good) : o.who ? ui::col(pal.text_dim) : ui::col(pal.gold), nullptr, 150);
                break;
            case Objective::Girl:
                if (o.state == 0 && human) label(o.at, "HELP!", ui::col(pal.good), nullptr, 160);
                break;
            case Objective::Supply:
                if (o.state == 1 && human && eng::length(o.at - camera_.eye) < 1800.0f) label(o.at, "SUPPLY", ui::col(pal.good), nullptr, 50);
                break;
            case Objective::Magazine:
                if (eng::length(o.at - camera_.eye) < 1200.0f) label(o.at, "AMMO", ui::col(pal.text_dim), nullptr, 20);
                break;
            default: break;
        }
    }
    // Captains: your side's always, the other side's where they are seen (Captain Mode: always).
    for (const RoleNow& r : mode_state_.roles) {
        auto it = players_.find(r.player);
        if (it == players_.end() || !it->second.alive || it->second.hidden || r.player == me_) continue;
        const PlayerView& p = it->second;
        if (MatchRole(r.role) == MatchRole::Captain) {
            VanVec2 s;
            if (to_screen(p.position + Vec3{0, 230, 0}, s))
                if (!art_at(dl, p.team == Team::Red ? "inf/mode/ctc_red.tga" : "inf/mode/ctc_blue.tga", s.x, s.y, 30))
                    ui::text(dl, ui::font_heading(), 18, s.x, s.y - 9, ui::col(ui::team_colour(u8(p.team))), "C", ui::Align::Center);
        }
        // Team Slayer: the one who killed you last.
        if (r.flags & kRoleKiller) {
            VanVec2 s;
            if (to_screen(p.position + Vec3{0, 215, 0}, s)) {
                dl->AddRectFilled(ui::stage(s.x - 26, s.y - 10), ui::stage(s.x + 26, s.y + 8), VAN_COL32(150, 20, 20, 220), ui::px(6));
                ui::text(dl, ui::font_bold(), 13, s.x, s.y - 8, VAN_COL32(255, 255, 255, 255), "Killer", ui::Align::Center);
            }
        }
        // Spawn protection: unbeatable for a moment.
        if ((r.flags & kRoleProtected) && p.team == team_) {
            VanVec2 s;
            if (to_screen(p.position + Vec3{0, 200, 0}, s)) dl->AddCircle(ui::stage(s.x, s.y), ui::px(9), VAN_COL32(255, 255, 255, 160), 16, 2.0f);
        }
    }
    (void)now;
}

void GameWorld::draw_radar_objectives(VanDrawList* dl, const VanVec2& c, float r, const std::function<VanVec2(const Vec3&)>& to_dial) {
    const ui::Palette& pal = ui::pal();
    auto mark = [&](const Vec3& at, VanU32 ink, const char* letter) {
        VanVec2 o = to_dial(at);
        const float d = std::sqrt(o.x * o.x + o.y * o.y);
        if (d > r - ui::px(6)) o = VanVec2(o.x * (r - ui::px(6)) / d, o.y * (r - ui::px(6)) / d);
        const VanVec2 at2(c.x + o.x, c.y + o.y);
        dl->AddRectFilled(VanVec2(at2.x - ui::px(5), at2.y - ui::px(5)), VanVec2(at2.x + ui::px(5), at2.y + ui::px(5)), VAN_COL32(0, 0, 0, 200));
        dl->AddRect(VanVec2(at2.x - ui::px(5), at2.y - ui::px(5)), VanVec2(at2.x + ui::px(5), at2.y + ui::px(5)), ink, 0, 0, 1.5f);
        if (letter) {
            VanFont* f = ui::font_bold();
            const VanVec2 ts = f->CalcTextSizeA(ui::px(9), FLT_MAX, 0.0f, letter);
            dl->AddText(f, ui::px(9), VanVec2(at2.x - ts.x * 0.5f, at2.y - ts.y * 0.5f), ink, letter);
        }
    };
    const Mission mission = level_ ? map_rules(*level_).mission : Mission::Elimination;
    for (const ObjectiveNow& o : mode_state_.objectives) {
        switch (Objective(o.kind)) {
            case Objective::Site: mark(o.at, ui::col(pal.red), o.index ? "B" : "A"); break;
            case Objective::Bomb:
                if (o.state >= 2 && o.state <= 3) mark(o.at, VAN_COL32(255, 60, 40, 255), "!");
                break;
            case Objective::Item:
                if (o.state != 3) mark(o.at, ui::col(pal.gold), "*");
                break;
            case Objective::Zone:
                if (Team(o.team) == team_ || mission == Mission::Capture) mark(o.at, ui::col(pal.good), mission == Mission::Escape ? "E" : "H");
                break;
            case Objective::Console: mark(o.at, Team(o.team) == Team::Red ? ui::col(pal.red) : ui::col(pal.text), o.index ? "B" : "A"); break;
            case Objective::Stronghold:
                mark(o.at, Team(o.team) == Team::Red ? ui::col(pal.red) : Team(o.team) == Team::Blue ? ui::col(pal.blue) : ui::col(pal.text), o.index == 1 ? "C" : "S");
                break;
            case Objective::Treasure:
                if (o.state == 1 || o.state == 2) mark(o.at, ui::col(pal.gold), "$");
                break;
            case Objective::Girl:
                if (o.state == 0 && !me_undead()) mark(o.at, ui::col(pal.good), "+");
                break;
            default: break;
        }
    }
    // The enemies the game type shows you wherever they are: Captain Mode's captains, a Search's humans.
    for (const RoleNow& rn : mode_state_.roles) {
        if (!(rn.flags & kRoleSeen)) continue;
        auto it = players_.find(rn.player);
        if (it == players_.end() || !it->second.alive) continue;
        VanVec2 o = to_dial(it->second.position);
        const float d = std::sqrt(o.x * o.x + o.y * o.y);
        if (d > r - ui::px(4)) o = VanVec2(o.x * (r - ui::px(4)) / d, o.y * (r - ui::px(4)) / d);
        dl->AddCircleFilled(VanVec2(c.x + o.x, c.y + o.y), ui::px(4.5f), VAN_COL32(255, 60, 40, 255), 10);
    }
}

void GameWorld::draw_skills(double now) {
    (void)now;
    RoleNow shown_role;
    const RoleNow* mine = role_of(me_);
    if (test_undead_ != Undead::None) {
        shown_role.player = me_, shown_role.role = u8(MatchRole::Zombie), shown_role.undead = u8(test_undead_), shown_role.rank = 2;
        mine = &shown_role;
    }
    if (!alive_ || !mine || Undead(mine->undead) == Undead::None) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const UndeadDef& d = undead_def(Undead(mine->undead));
    const float W = ui::stage_w(), H = ui::stage_h();
    // The class and its rank over the skills; the skills along the bottom right, where a gun's box is.
    float x = W - 30;
    const float y = H - 96;
    std::string title = d.name;
    if (settings_.mode == Mode::Horror2) title += eng::str::format("   rank %u", unsigned(mine->rank));
    if (MatchRole(mine->role) == MatchRole::Host) title = "Host Zombie";
    ui::text(dl, ui::font_heading(), 20, W - 30, y - 34, ui::col(pal.bad), title, ui::Align::Right);
    for (int k = 4; k >= 0; --k) {
        const Skill s = Skill(d.skills[size_t(k)]);
        if (s == Skill::None) continue;
        x -= 64;
        const float cool = mode_state_.cooldowns[size_t(k)];
        dl->AddRectFilled(ui::stage(x, y), ui::stage(x + 56, y + 56), VAN_COL32(0, 0, 0, 170), ui::px(4));
        if (!art_at(dl, skill_icon(s), x + 28, y + 28, 50, cool > 0 ? VAN_COL32(110, 110, 110, 255) : 0xFFFFFFFF))
            ui::text(dl, ui::font_bold(), 11, x + 28, y + 22, ui::col(pal.text), skill_def(s).name, ui::Align::Center);
        if (cool > 0) {
            ui::text(dl, ui::font_heading(), 22, x + 28, y + 15, ui::col(pal.text), eng::str::format("%.0f", double(std::ceil(cool))), ui::Align::Center);
        } else if ((mine->flags & (kRoleSpeed | kRoleJump)) && (s == Skill::Speed || s == Skill::Jump)) {
            dl->AddRect(ui::stage(x, y), ui::stage(x + 56, y + 56), ui::col(pal.good), ui::px(4), 0, 2.0f);
        }
        // Its key, as bound.
        const unsigned code = app_.settings().bind(kSkillKeys[size_t(k)]);
        ui::text(dl, ui::font_bold(), 11, x + 4, y + 2, ui::col(pal.gold), code ? eng::Input::binding_name(code) : std::string("-"), ui::Align::Left);
        ui::text(dl, ui::font_body(), 11, x + 28, y + 58, ui::col(pal.text_dim), skill_def(s).name, ui::Align::Center);
    }
}

void GameWorld::draw_class_select(double now) {
    RoleNow shown_role;
    const RoleNow* mine = role_of(me_);
    if (test_undead_ != Undead::None) {
        shown_role.player = me_, shown_role.role = u8(MatchRole::Zombie), shown_role.flags = test_undead_picking_ ? kRolePicking : 0, shown_role.rank = 1;
        mine = &shown_role;
        for (int i = 0; i < 4; ++i)
            if (class_at(i) == test_undead_) pick_index_ = i;
    }
    if (!mine || !(mine->flags & kRolePicking) || (pick_sent_ && test_undead_ == Undead::None) || watching_) return;
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    const float x0 = W * 0.5f - 340, y0 = H * 0.5f - 230;
    dl->AddRectFilled(ui::stage(x0 - 10, y0 - 10), ui::stage(x0 + 690, y0 + 440), VAN_COL32(10, 4, 4, 225), ui::px(6));
    const Undead shown = class_at(pick_index_);
    // The class's own card (its art carries its name and its three bars). The art is named for the
    // class without the folder's "z": zboss's is ui_info_zombie_boss.
    const std::string card = std::string("ui/texture/common/ui_info_zombie_") + (undead_def(shown).model + 1) + ".tga";
    const ui::Picture pic = art(card);
    if (pic.valid()) {
        // The card is drawn in the top-left 670x312 of its 1024x512 page.
        ui::picture(dl, pic, x0, y0, 670.0f * 1024.0f / 670.0f, 312.0f * 512.0f / 312.0f, 0xFFFFFFFF, false);
    } else {
        ui::text(dl, ui::font_heading(), 30, x0 + 20, y0 + 20, ui::col(pal.bad), undead_def(shown).name);
    }
    // The four to choose from.
    for (int i = 0; i < 4; ++i) {
        const Undead u = class_at(i);
        const bool locked = undead_def(u).unlock_rank > mine->rank;
        const std::string icon = std::string("ui/texture/icon/ui_icon_select_zombie_") + (undead_def(u).model + 1) +
                                 (locked ? "_lock.tga" : i == pick_index_ ? "_on.tga" : "_off.tga");
        const float ix = x0 + 115 + 120.0f * float(i);
        if (!art_at(dl, icon, ix, y0 + 360, 80))
            ui::text(dl, ui::font_bold(), 16, ix, y0 + 350, ui::col(i == pick_index_ ? pal.lime : pal.text_dim), undead_def(u).name, ui::Align::Center);
        if (locked) ui::text(dl, ui::font_body(), 11, ix, y0 + 404, ui::col(pal.text_dim), eng::str::format("rank %u", unsigned(undead_def(u).unlock_rank)), ui::Align::Center);
    }
    if (!art_at(dl, "ui/texture/text/ui_msg_zombieselect.tga", W * 0.5f, y0 + 322, 22))
        ui::text(dl, ui::font_bold(), 14, W * 0.5f, y0 + 312, ui::col(pal.text), "Choose undead with Left, Right and press Down", ui::Align::Center);
    (void)now;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

bool GameWorld::view_objective(Objective kind) {
    for (const ObjectiveNow& o : mode_state_.objectives) {
        if (Objective(o.kind) != kind || !collision_) continue;
        // A few metres back toward wherever there is room (nearer when a corner gives none), then facing it.
        for (int a = 0; a < 24; ++a) {
            const float yaw = float(a % 8) * 45.0f, dist = a < 8 ? 450.0f : a < 16 ? 300.0f : 180.0f;
            const Vec3 back = o.at + eng::angles_to_forward(yaw, 0) * dist + Vec3{0, 60, 0};
            if (collision_->trace_ray(o.at + Vec3{0, 60, 0}, back).hit()) continue;
            move_.origin = settle_spawn(move_def_, *collision_, back);
            move_.velocity = {};
            move_prev_ = move_;
            const Vec3 to = o.at - (move_.origin + Vec3{0, move_def_.stand_eye, 0});
            yaw_ = eng::forward_to_yaw(to);
            pitch_ = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) / eng::kDegToRad;
            return true;
        }
    }
    return false;
}

bool GameWorld::face_undead() {
    const PlayerView* best = nullptr;
    float best_d = 1e30f;
    for (const auto& [id, p] : players_)
        if (id != me_ && p.alive && !p.hidden && is_undead(id))
            if (const float d = eng::length(p.position - move_.origin); d < best_d) best_d = d, best = &p;
    if (!best || !collision_) return false;
    // In front of him, where there is room: his face, and the light on it.
    for (int a = 0; a < 8; ++a) {
        const float yaw = best->yaw + float(a) * 45.0f;
        const Vec3 at = best->position + eng::angles_to_forward(yaw, 0) * 320.0f + Vec3{0, 60, 0};
        if (collision_->trace_ray(best->position + Vec3{0, 60, 0}, at).hit()) continue;
        move_.origin = settle_spawn(move_def_, *collision_, at);
        move_.velocity = {};
        move_prev_ = move_;
        const Vec3 to = best->position + Vec3{0, 110, 0} - (move_.origin + Vec3{0, move_def_.stand_eye, 0});
        yaw_ = eng::forward_to_yaw(to);
        pitch_ = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)) / eng::kDegToRad;
        return true;
    }
    return false;
}

}  // namespace lsf
