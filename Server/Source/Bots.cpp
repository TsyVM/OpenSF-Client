// Server bots (TacticalFPS's MatchBots, on this server's terms): a soldier in the match with no
// peer, whose moves the server makes for it.
//
// A bot plays by a player's rules and no others. It sees what the sight table says (update_vision,
// the same table that decides what a player is sent), hears a shot a player would hear, walks the
// navigation graph (Game/Navigation.hpp) with the same movement code a client moves with, and its
// shots go through the same damage as a player's -- so to everyone else it is a soldier like any.
// What makes one bot better than another is its skill: how long it takes to react, how fast it
// turns, how far off its first aim is and how quickly that closes, how much recoil it takes out.
//
// It never fires at anything it has no clear line to: being told where an enemy is is not having a
// shot at them (TacticalFPS's first test bot emptied its magazines into a tower).
//
// Training's bots are targets: they never fire, half of them stand and half walk about, and they
// are back a second after they fall.
//
// A game type with an objective gives a bot somewhere to be most of the time (Modes.cpp's
// bot_mode_goal): a bomb site to hold Use at, the bomb to defuse, the item and the way home, a console
// or a stronghold, the girl. An undead has claws: it runs at the nearest human it knows of and swings.
#include "Match.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace lsfs {

using namespace lsf::proto;
using eng::Vec3;

namespace {

struct SkillProfile {
    float reaction;   // seconds from first sight to first shot
    float turn;       // degrees a second the view can swing
    float error;      // degrees the first aim is off by
    float settle;     // seconds for that error to close to a quarter
    float recoil;     // share of the recoil it takes back out
    float head;       // chance an engagement aims for the head
    float burst;      // seconds of held trigger
    float rest;       // seconds between bursts
    float hearing;    // how readily it turns toward a shot
};

constexpr SkillProfile kSkills[] = {
    {0.60f, 240.0f, 7.0f, 1.4f, 0.00f, 0.05f, 0.25f, 0.45f, 0.4f},   // Easy
    {0.34f, 420.0f, 4.0f, 1.0f, 0.45f, 0.15f, 0.35f, 0.30f, 0.8f},   // Normal
    {0.20f, 720.0f, 2.2f, 0.7f, 0.80f, 0.35f, 0.50f, 0.18f, 1.0f},   // Hard
    {1e9f, 120.0f, 0.0f, 1.0f, 0.00f, 0.00f, 0.00f, 1.00f, 0.0f},    // Target: never fires back
};

constexpr const char* kNames[] = {"Viper", "Hawk", "Raven", "Cobra", "Wolf", "Falcon", "Mamba", "Ghost", "Talon", "Saber",
                                  "Jackal", "Lynx", "Fox", "Bison", "Orca", "Kestrel", "Shrike", "Puma", "Badger", "Condor"};
constexpr double kBotStep = 1.0 / 30.0;

float approach(float from, float to, float step) {
    const float d = eng::wrap_degrees(to - from);
    return eng::wrap_degrees(from + std::clamp(d, -step, step));
}

float pitch_to(const Vec3& d) { return std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) / eng::kDegToRad; }

}  // namespace

void Match::add_bots() {
    const int want = std::min<int>(settings_.bots, std::max(0, int(settings_.max_players) - int(players_.size())));
    if (want <= 0) return;
    const BotSkill skill = BotSkill(std::min<int>(settings_.bot_skill, int(BotSkill::Target)));
    for (int k = 0; k < want; ++k) {
        Player p;
        p.id = kBotIdBase | ++next_bot_;
        p.name = std::string(kNames[(seed_ + u32(k)) % std::size(kNames)]) + (next_bot_ > std::size(kNames) ? std::to_string(next_bot_) : "");
        p.name = "[BOT] " + p.name;
        // Seated on the side with fewer soldiers; Training's targets all face the soldiers who came to shoot.
        if (mode_.teams) {
            int red = 0, blue = 0;
            for (const auto& [id, o] : players_) red += o.team == Team::Red, blue += o.team == Team::Blue;
            p.team = red <= blue ? Team::Red : Team::Blue;
        } else {
            p.team = Team::None;
        }
        u8 slot = 0;
        for (bool taken = true; taken; ++slot) {
            taken = false;
            for (const auto& [id, o] : players_) taken |= o.team == p.team && o.slot == slot;
            if (!taken) break;
        }
        p.slot = slot;
        p.force = u8(rng_() % std::max<size_t>(1, forces().size()));
        p.loadout = p.own_loadout = starter_loadout();
        p.shown_xp = rank_xp(int(rng_() % 12));
        p.loaded = true;
        p.bot = std::make_shared<BotBrain>();
        p.bot->skill = skill;
        p.bot->rng = eng::Rng(seed_ ^ (u64(p.id) * 0x9E3779B97F4A7C15ull));
        p.bot->still = skill == BotSkill::Target && (k % 2) == 0;
        players_[p.id] = std::move(p);
    }
    LOG_INFO("Match %u: %d %s bot(s)", id_, want, bot_skill_name(skill));
}

// Where a soldier spawns: the client's own rule (GameWorld::spawn_point) -- the side's spawn
// points, taken by the index the Spawn message carries -- set down on the floor. A Training target
// is put at the point furthest from any soldier, so it is something to walk up to.
Vec3 Match::spawn_position(const Player& p, u8 index) const {
    if (!map_ || map_->spawns.empty()) return p.position;
    std::vector<const sf::LevelSpawn*> list;
    // Horror Mode 2's maps have spawns of their own for the undead and the humans.
    if (settings_.mode == Mode::Horror2)
        for (const sf::LevelSpawn& s : p.team == Team::Red ? map_->zombie_spawns : map_->human_spawns) list.push_back(&s);
    if (list.empty())
        for (const sf::LevelSpawn& s : map_->spawns)
            if (!mode_.teams ? s.team == sf::Team::Any : (p.team == Team::Red ? s.team == sf::Team::Red : s.team == sf::Team::Blue)) list.push_back(&s);
    if (list.empty())
        for (const sf::LevelSpawn& s : map_->spawns) list.push_back(&s);
    const sf::LevelSpawn* pick = list[size_t(index) % list.size()];
    if (p.bot && p.bot->skill == BotSkill::Target) {
        float best = -1;
        for (const sf::LevelSpawn* s : list) {
            float nearest = 1e30f;
            for (const auto& [id, o] : players_)
                if (!o.bot && !o.left && o.alive) nearest = std::min(nearest, eng::length(o.position - s->position));
            nearest += float(p.bot->rng.next() % 400);   // not every target on the same spot
            if (nearest > best) best = nearest, pick = s;
        }
    }
    return settle_spawn(move_def_, map_->collision, pick->position);
}

bool Match::trace_soldiers(const Player& shooter, const Vec3& from, const Vec3& dir, float max_dist, u32& victim, HitZone& zone, float& dist) const {
    float best = max_dist;
    bool hit = false;
    for (const auto& [id, p] : players_) {
        if (id == shooter.id || p.left || !p.alive || p.team == Team::Observer || !enemies(shooter, p)) continue;
        const float h = hull_height_by_flags(move_def_, p.flags);
        const Vec3 o = p.position;
        const float r = 20.0f;
        struct Shape {
            eng::Aabb box;
            HitZone zone;
        };
        const Shape shapes[3] = {
            {{o + Vec3{-10, h - 26, -10}, o + Vec3{10, h - 2, 10}}, HitZone::Head},
            {{o + Vec3{-r, h * 0.48f, -r}, o + Vec3{r, h - 26, r}}, HitZone::Chest},
            {{o + Vec3{-r * 0.9f, 0, -r * 0.9f}, o + Vec3{r * 0.9f, h * 0.48f, r * 0.9f}}, HitZone::Legs},
        };
        const Vec3 inv{dir.x != 0 ? 1.0f / dir.x : 1e30f, dir.y != 0 ? 1.0f / dir.y : 1e30f, dir.z != 0 ? 1.0f / dir.z : 1e30f};
        for (const Shape& s : shapes) {
            const float t = eng::ray_aabb(from, inv, s.box, best);
            if (t >= 0 && t < best) best = t, victim = id, zone = s.zone, hit = true;
        }
    }
    dist = best;
    return hit;
}

void Match::bots_tick(double now) {
    // Without the map a bot has nowhere to stand: it waits (a second or so, the first time a map is used).
    if (!map_ || now - bot_acc_ < kBotStep) return;
    const float dt = float(std::clamp(now - bot_acc_, 0.0, 0.1));
    bot_acc_ = now;   // the last bot step
    // Every ten seconds, each bot in a line of the log: where, going where, fighting whom (tests read it).
    static constexpr double kReport = 10.0;
    const bool report = now - bot_report_ >= kReport;
    if (report) bot_report_ = now;
    for (auto& [id, p] : players_) {
        if (!p.bot || p.left) continue;
        if (report) {
            int seen = 0;
            for (const auto& [t, until] : p.sees) seen += until > now;
            Vec3 objective;
            bool use = false;
            const bool has = bot_mode_goal(p, objective, use, now);
            LOG_INFO("bot %s: %s at (%.0f %.0f %.0f), %s path, target %s, sees %d, %u shots %u hits, %u kills %u deaths%s", p.name.c_str(),
                     p.alive ? "alive" : "down", double(p.position.x), double(p.position.y), double(p.position.z),
                     p.bot->follow.active() ? "on a" : "no", p.bot->target ? "yes" : "none", seen, p.shots, p.hits, unsigned(p.kills), unsigned(p.deaths),
                     has ? eng::str::format(", objective (%.0f %.0f %.0f)%s", double(objective.x), double(objective.y), double(objective.z), use ? " using" : "").c_str()
                         : "");
        }
        // Back after a fall: a target in a second, any other bot as a soldier would be.
        if (!p.alive && p.respawn_at > 0 && now >= p.respawn_at && phase_ == Phase::Live) spawn(p, now);
        if (p.alive && phase_ == Phase::Live) bot_think(p, dt, now);
    }
}

int Match::bot_goal(Player& p, double now) {
    BotBrain& b = *p.bot;
    const NavGraph& g = map_->nav;
    const int here = g.locate(p.move.origin);
    if (here < 0) return -1;
    if (now < b.wander_until || b.skill == BotSkill::Target) return g.random_node(b.rng, here);
    // The game type's objective, three times in four (the fourth goes hunting).
    Vec3 objective;
    bool use = false;
    // Where the bot can stand to reach it: the nearest cell it has a path to (the nearest of all may be
    // the top of the crate a mark sits on), found again only when the objective has moved.
    // At it already (a console being taken, a plant under way): it stays.
    const bool has_objective = bot_mode_goal(p, objective, use, now);
    const bool there = has_objective && eng::length(Vec3{objective.x - p.position.x, 0, objective.z - p.position.z}) < 200.0f;
    if (has_objective && (there || b.rng.next() % 4 != 0)) {
        if (eng::length(objective - b.objective_at) > 60.0f || b.objective_cell < 0) {
            b.objective_at = objective;
            b.objective_cell = -1;
            std::vector<int> path;
            for (int c : g.nearest_cells(objective, g.node(here).component, 400.0f, 250.0f, 16))
                if (c == here || g.find_path(here, c, path)) {
                    b.objective_cell = c;
                    break;
                }
        }
        if (b.objective_cell >= 0) return b.objective_cell;
    }
    // An undead smells the nearest human out.
    if (undead(p)) {
        const Player* prey = nullptr;
        float best = 1e30f;
        for (const auto& [id, o] : players_)
            if (o.alive && !o.left && enemies(p, o))
                if (const float d = eng::length(o.position - p.position); d < best) best = d, prey = &o;
        if (prey)
            if (const int cell = g.locate(prey->position + Vec3{0, 20, 0}); cell >= 0) return cell;
    }
    // A hunt: where somebody was last seen or heard, or the far side's spawns, where an enemy not yet
    // found usually is.
    if (now < b.lead_until)
        if (const int cell = g.locate(b.lead); cell >= 0) return cell;
    std::vector<const sf::LevelSpawn*> far;
    for (const sf::LevelSpawn& s : map_->spawns)
        if (!mode_.teams || (p.team == Team::Red ? s.team == sf::Team::Blue : s.team == sf::Team::Red)) far.push_back(&s);
    if (far.empty()) return g.random_node(b.rng, here);
    const int cell = g.locate(far[b.rng.next() % far.size()]->position + Vec3{0, 20, 0});
    return cell >= 0 && g.node(cell).component == g.node(here).component ? cell : g.random_node(b.rng, here);
}

void Match::bot_fire(Player& p, double now) {
    BotBrain& b = *p.bot;
    const WeaponDef* w = weapon(p.loadout[0]);
    if (!w || w->magazine <= 0) return;
    if (now < b.reload_until || now < b.next_shot) return;
    if (b.clip <= 0) {
        b.reload_until = now + w->reload_time;
        b.clip = w->magazine;
        return;
    }
    --b.clip;
    b.next_shot = now + 60.0 / std::max(1.0f, w->rpm);
    p.fired_at = now;
    p.last_shot[0] = now;
    ++p.shots;
    // The cone and the kick as a player's (Game/Rules.hpp's numbers), on the bot's own view.
    const Vec3 eye = eye_position(move_def_, p.move);
    const float cone = w->spread_stand + b.cone;
    b.cone = std::min(w->spread_max, b.cone + w->spread_per_shot);
    const float a = float(b.rng.next() % 6283) / 1000.0f, r = std::sqrt(float(b.rng.next() % 1000) / 1000.0f) * cone * eng::kDegToRad;
    const Vec3 fwd = eng::angles_to_forward(b.yaw, b.pitch + b.recoil);
    const Vec3 right = eng::yaw_to_right(b.yaw), up = eng::cross(fwd, right);
    const Vec3 dir = eng::normalize(fwd + right * (std::cos(a) * std::tan(r)) + up * (std::sin(a) * std::tan(r)));
    b.recoil = std::min(w->recoil_up_max, b.recoil + w->recoil_up * (1.0f - kSkills[size_t(b.skill)].recoil));
    float wall = w->range;
    Vec3 end = eye + dir * w->range;
    if (const eng::TraceResult tr = map_->collision.trace_ray(eye, end); tr.hit()) wall = w->range * tr.fraction, end = tr.end;
    u32 victim = 0;
    HitZone zone = HitZone::None;
    float dist = 0;
    ShotFx fx;
    fx.shooter = p.id;
    fx.weapon = w->id;
    fx.origin = eye;
    if (trace_soldiers(p, eye, dir, wall, victim, zone, dist)) end = eye + dir * dist;
    fx.end = end;
    send_shot(p, fx);
    if (victim)
        if (Player* v = player(victim)) {
            ++p.hits;
            zone = taken_zone(*v, zone);
            apply_damage(p, *v, int(std::lround(hit_damage(*w, dist, zone))), zone, w->id, false, now);
        }
}

void Match::bot_think(Player& p, float dt, double now) {
    BotBrain& b = *p.bot;
    const SkillProfile& k = kSkills[std::min<size_t>(size_t(b.skill), std::size(kSkills) - 1)];
    const WeaponDef* w = weapon(p.loadout[0]);
    MoveCmd cmd;
    cmd.yaw = b.yaw;
    cmd.pitch = b.pitch;
    const Vec3 eye = eye_position(move_def_, p.move);
    b.recoil = std::max(0.0f, b.recoil - (w ? w->recoil_recover : 10.0f) * dt);
    if (w) b.cone = std::max(0.0f, b.cone - w->spread_recover * dt);

    // ── What it can see and hear ──
    Player* enemy = nullptr;
    float enemy_d = 1e30f;
    for (auto& [id, o] : players_) {
        if (id == p.id || o.left || !o.alive || o.team == Team::Observer || !enemies(p, o)) continue;
        const float d = eng::length(o.position - p.position);
        if (auto it = p.sees.find(id); it != p.sees.end() && it->second > now) {
            if (d < enemy_d) enemy = &o, enemy_d = d;
        } else if (k.hearing > 0 && now - o.fired_at < 0.2 && d < 3000.0f && float(b.rng.next() % 1000) < k.hearing * 60.0f) {
            b.lead = o.position;
            b.lead_until = now + 4.0;
        }
    }
    if (enemy && b.target != enemy->id) {
        b.target = enemy->id;
        b.seen_since = now;
        b.shoot_from = now + k.reaction * (0.75f + 0.5f * float(b.rng.next() % 1000) / 1000.0f);
        b.head = float(b.rng.next() % 1000) / 1000.0f < k.head;
        const float a = float(b.rng.next() % 6283) / 1000.0f;
        b.err_yaw = std::cos(a) * k.error;
        b.err_pitch = std::sin(a) * k.error * 0.6f;
    } else if (!enemy && b.target) {
        if (Player* gone = player(b.target); gone && gone->alive) b.lead = gone->position, b.lead_until = now + 6.0;
        b.target = 0;
    }
    const bool engaging = enemy && b.skill != BotSkill::Target;

    // ── An undead's claws, and its skills now and then ──
    if (undead(p)) {
        if (enemy && enemy_d < kClawReach && now >= b.next_shot) {
            b.next_shot = now + 0.9;
            p.fired_at = now;
            apply_damage(p, *enemy, 1, HitZone::Chest, p.loadout[2], false, now);   // the claws' damage is the class's (mode_damage)
        }
        const UndeadDef& d = undead_def(p.undead);
        if (enemy && enemy_d < 2500.0f && b.rng.next() % 90 == 0)
            for (size_t s = 0; s < 5; ++s)
                if (d.skills[s] != kSkillNone && Skill(d.skills[s]) != Skill::SelfBomb && now >= p.skill_ready[s]) {
                    UseSkill m;
                    m.skill = d.skills[s];
                    m.aim = eng::normalize(enemy->position - p.position);
                    on_skill(p, m, now);
                    break;
                }
    }

    // ── Aim and trigger ──
    bool clear = false;
    if (enemy) {
        const float h = b.head ? eye_height_by_flags(move_def_, enemy->flags) : hull_height_by_flags(move_def_, enemy->flags) * 0.6f;
        const Vec3 point = enemy->position + Vec3{0, h, 0};
        const Vec3 to = point - eye;
        clear = engaging && map_->collision.trace_ray(eye, point).fraction >= 0.999f;
        const float closing = std::max(0.25f, 1.0f - 0.75f * float(now - b.seen_since) / k.settle);
        const float true_yaw = eng::forward_to_yaw(to), true_pitch = pitch_to(to);
        b.yaw = approach(b.yaw, true_yaw + b.err_yaw * closing, k.turn * dt);
        b.pitch = std::clamp(b.pitch + std::clamp(true_pitch + b.err_pitch * closing - b.recoil * k.recoil - b.pitch, -k.turn * dt, k.turn * dt), -89.0f, 89.0f);
        if (engaging && now >= b.shoot_from) {
            const float off_yaw = std::fabs(eng::wrap_degrees(b.yaw - true_yaw));
            const float off_pitch = std::fabs(b.pitch + b.recoil - true_pitch);
            const float tolerance = std::max(1.0f, std::atan2(30.0f, enemy_d) / eng::kDegToRad * 1.5f);
            const bool on = clear && off_yaw < tolerance && off_pitch < tolerance;
            bool fire = false;
            if (now < b.burst_until) {
                fire = on;
            } else if (b.burst_until != 0) {
                b.rest_until = now + k.rest * (0.6f + 0.8f * float(b.rng.next() % 1000) / 1000.0f);
                b.burst_until = 0;
            } else if (now >= b.rest_until && on) {
                b.burst_until = now + k.burst * (0.6f + 0.8f * float(b.rng.next() % 1000) / 1000.0f);
                fire = true;
            }
            if (fire) bot_fire(p, now);
        }
    }

    // ── Where to go ──
    const NavGraph& g = map_->nav;
    bool walking = false;
    if (!g.empty() && !b.still) {
        if (!b.follow.active()) b.repath_at = std::min(b.repath_at, now + 0.4);
        if (now >= b.repath_at) {
            const int goal = bot_goal(p, now);
            const int here = g.locate(p.move.origin);
            std::vector<int> path;
            if (goal >= 0 && here >= 0 && goal != here && g.find_path(here, goal, path)) b.follow.start(std::move(path));
            else b.follow.clear();
            b.goal = goal;
            b.repath_at = now + (b.follow.active() ? 6.0 : 2.5) + float(b.rng.next() % 2000) / 1000.0f;
        }
        cmd.yaw = b.yaw;
        cmd.pitch = b.pitch;
        walking = b.follow.steer(g, map_->collision, move_def_, p.move, cmd, false);
        // A target walks, it does not run.
        if (b.skill == BotSkill::Target) cmd.buttons |= kButtonWalk;
    }
    // At the objective with Use to hold (a plant, a defuse, the treasure, the girl): it stands and holds it.
    {
        Vec3 objective;
        bool use = false;
        if (!engaging && bot_mode_goal(p, objective, use, now) && use) {
            cmd.forward = cmd.side = 0;
            walking = false;
            if (!(p.buttons & kButtonUse)) p.use_from = now;
            p.buttons |= kButtonUse;
        } else {
            p.buttons &= u16(~kButtonUse);
            p.use_from = 0;
        }
    }
    if (undead(p) && enemy && enemy_d < 2000.0f) {
        // Claws reach nothing from across a room: an undead runs straight at what it sees.
        b.yaw = approach(b.yaw, eng::forward_to_yaw(enemy->position - p.position), k.turn * dt);
        cmd.forward = 1;
        cmd.side = 0;
        walking = true;
    } else if (clear && enemy_d < 2500.0f) {
        // A fight it has a shot in is side to side, not along the path.
        if (now >= b.strafe_until) {
            b.strafe = b.rng.next() % 2 ? -1 : 1;
            b.strafe_until = now + 0.4 + float(b.rng.next() % 800) / 1000.0f;
        }
        cmd.forward = 0;
        cmd.side = float(b.strafe);
        cmd.buttons &= u16(~kButtonCrouch);
    } else if (!engaging) {
        const float look = now < b.lead_until && !walking ? eng::forward_to_yaw(b.lead - p.position) : walking ? b.follow.heading() : b.yaw + 40.0f * dt;
        b.yaw = approach(b.yaw, look, k.turn * 0.5f * dt);
        b.pitch = approach(b.pitch, 0.0f, k.turn * 0.5f * dt);
    }
    cmd.yaw = b.yaw;
    cmd.pitch = b.pitch;

    // ── Stuck ──
    if (walking && !engaging) {
        if (eng::length(p.move.origin - b.last_pos) > 60.0f) {
            b.last_pos = p.move.origin;
            b.last_progress = now;
            b.stuck = 0;
        } else if (now - b.last_progress > 1.5) {
            b.last_progress = now;
            if (++b.stuck > 4) {
                b.stuck = 0;
                b.wander_until = now + 12.0;
            } else {
                b.follow.unstick((b.stuck & 1) != 0);
            }
            b.repath_at = 0;
        }
    } else {
        b.last_pos = p.move.origin;
        b.last_progress = now;
    }

    // Tests (Match::test_bots_pace): side to side where it stands, crouched or running.
    if (b.test_pace) {
        cmd.forward = 0;
        cmd.side = std::fmod(now, 2.0) < 1.0 ? 1.0f : -1.0f;
        cmd.buttons = b.test_pace == 1 ? u16(kButtonCrouch) : u16(0);
    }

    // ── Move, with the code a client moves with ──
    float speed = (w ? w->move_speed : 0.95f) * (force(p.force) ? force(p.force)->speed : 1.0f);
    if (undead(p)) speed = undead_def(p.undead).speed * (now < p.speed_until ? 1.5f : 1.0f);
    simulate_move(p.move, cmd, dt, move_def_, map_->collision, speed);
    p.position = p.move.origin;
    p.velocity = p.move.velocity;
    p.yaw = b.yaw;
    p.pitch = b.pitch;
    p.hand = 0;
    p.flags = u16(kFlagAlive | (p.move.on_ground ? kFlagOnGround : 0) | (p.move.ducked ? kFlagCrouched : 0) | (p.move.on_ladder ? kFlagOnLadder : 0) |
                 ((cmd.buttons & kButtonWalk) ? kFlagWalking : 0) | (now - p.fired_at < 0.15 ? kFlagFiring : 0) |
                 (now < b.reload_until ? kFlagReloading : 0));
    p.last_input = now;
    track_fall(p, p.position, p.move.on_ground, p.move.on_ladder, now);
}

void Match::test_bots_pace(u8 pace) {
    for (auto& [id, p] : players_)
        if (p.bot) p.bot->test_pace = pace;
}

void Match::test_bots_before(u32 peer_id, float distance) {
    Player* me = player(peer_id);
    if (!me || !map_) return;
    const Vec3 ahead = eng::angles_to_forward(me->yaw, 0);
    const Vec3 eye = me->position + Vec3{0, move_def_.stand_eye, 0};
    // Short of a wall that close, and a little apart from each other.
    const eng::TraceResult tr = map_->collision.trace_ray(eye, eye + ahead * distance);
    const float reach = tr.hit() ? std::max(120.0f, distance * tr.fraction - 60.0f) : distance;
    float side = 0;
    for (auto& [id, p] : players_) {
        if (!p.bot || !p.alive) continue;
        p.bot->skill = BotSkill::Target;
        p.bot->still = true;
        const Vec3 at = settle_spawn(move_def_, map_->collision, me->position + ahead * reach + eng::yaw_to_right(me->yaw) * side);
        p.move = MoveState{};
        p.move.origin = at;
        p.position = at;
        p.velocity = {};
        p.yaw = eng::wrap_degrees(me->yaw + 180.0f);
        side += 90.0f;
    }
}

}  // namespace lsfs
