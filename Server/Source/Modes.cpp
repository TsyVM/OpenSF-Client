// Each game type's own rules, as Match's (Server/Source/Match.cpp keeps the game every type shares:
// spawns, shots, damage, kills, rounds, pay). Docs/Modes.md has every rule here and where it comes
// from: the original's briefings (menu archive load2/*), its score tabs (inf/tab/base/*), its maps'
// world scripts (mission objects, EvacuationData zones, strongholds, Horror Mode 2's spawns), and
// Docs/Research.md §3 for the numbers players reported.
//
//   Team Battle   the map's mission: Blast (plant 5 s at a site, 35 s fuse, defuse 7 s), Take Back
//                 (bring the item to your zone), Flee (half the attackers reach the zone), Dual (either
//                 side brings the one item home); elimination; time up to the defenders (Dual: a draw)
//   CTC           a captain a side, in turn: 1000 HP, a big head; the first captain down loses the round
//   Captain Mode  a captain for every three a side, the side's 500 HP a soldier shared out at random
//                 between them; captains are seen through walls; all a side's captains down loses it
//   Sniper Mode   sniper rifles, pistols and knives; the first side to the kill goal
//   Horror        twenty seconds, then one in four turns host zombie (1500 HP); whoever a zombie kills
//                 rises one (200 HP); humans (250 HP) win with every zombie down or the clock run out
//   Horror Mode 2 a quarter start undead and pick a class (Boss, Driller, Heavy, Hunter); the undead
//                 come back, humans do not; supply boxes, the girl to rescue, evolution
//   Team Slayer   Team Deathmatch's points, three seconds unbeatable, rage after three deaths without a
//                 kill, magazines from the fallen, the one who killed you last marked
//   Occupy        the Silo: the attackers take both missile consoles, or bring the sample to one
//   Pirate Mode   three strongholds, a point each every three seconds held; treasure; kills
#include "Match.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cmath>

namespace lsfs {

using namespace lsf::proto;
using eng::Vec3;

namespace {

constexpr double kStateEvery = 1.0;        // the state again even when nothing changed
constexpr double kRiseSeconds = 2.0;       // Horror: the infected rise where they fell
constexpr double kCaptainRespawn = 5.0;    // Captain Mode's soldiers who are not captains
constexpr double kSupplyEvery = 30.0;      // Horror Mode 2: a supply box comes back
constexpr double kGirlEvery = 45.0;        // Horror Mode 2: the girl, somewhere else
constexpr float kGirlSeconds = 2.0f;       // Use held by her
constexpr double kDroppedHome = 40.0;      // a dropped item no one touches goes home
constexpr float kTouchHeight = 200.0f;     // cm up or down an item or zone still counts
constexpr float kSelfBombRadius = 600.0f;
constexpr int kSelfBombDamage = 150;
constexpr float kCrushRadius = 400.0f;
constexpr int kCrushDamage = 60;
constexpr float kDrillReach = 600.0f;
constexpr int kDrillDamage = 80;
constexpr float kThrowReach = 2500.0f, kThrowRadius = 300.0f;
constexpr int kThrowDamage = 70;
constexpr float kSnatchReach = 1500.0f;
constexpr int kBombDamage = 200;
constexpr size_t kMagazinesMax = 12;

float flat_distance(const Vec3& a, const Vec3& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
// An item within a soldier's reach: from his chest, as far as the item's own reach (Modes.hpp item_reach).
bool reaches(const Vec3& feet, const Vec3& item, float reach) { return eng::length(item - (feet + Vec3{0, kChestHeight, 0})) <= reach; }
bool within(const Vec3& feet, const Vec3& at, float radius) { return flat_distance(feet, at) <= radius && std::fabs(feet.y - at.y) <= kTouchHeight; }

// A zombie's claws: the issued knife (what the client swings), its damage the game type's.
u16 claws_weapon() {
    static u16 id = [] {
        if (const WeaponDef* m9 = weapon_by_model("m9"); m9 && m9->klass == WeaponClass::Knife) return m9->id;
        for (const WeaponDef& w : weapons())
            if (w.klass == WeaponClass::Knife && w.price == 0) return w.id;
        for (const WeaponDef& w : weapons())
            if (w.klass == WeaponClass::Knife) return w.id;
        return kNoWeapon;
    }();
    return id;
}

// A cannon ball's burst is told and counted as a fragmentation grenade's.
u16 frag_weapon() {
    static u16 id = [] {
        if (const WeaponDef* m67 = weapon_by_model("m67")) return m67->id;
        for (const WeaponDef& w : weapons())
            if (w.grenade == GrenadeKind::Frag) return w.id;
        return kNoWeapon;
    }();
    return id;
}

u16 smoke_weapon() {
    static u16 id = [] {
        for (const WeaponDef& w : weapons())
            if (w.grenade == GrenadeKind::Smoke) return w.id;
        return kNoWeapon;
    }();
    return id;
}

Team other_side(Team t) { return t == Team::Red ? Team::Blue : t == Team::Blue ? Team::Red : Team::None; }

}  // namespace

// ── Helpers ───────────────────────────────────────────────────────────────────

Match::Obj* Match::find_obj(Objective kind, int index) {
    for (Obj& o : objs_)
        if (o.kind == kind && (index < 0 || o.index == index)) return &o;
    return nullptr;
}

int Match::count_side(Team t, bool alive_only) const {
    int n = 0;
    for (const auto& [id, p] : players_)
        if (!p.left && p.loaded && p.team == t && (!alive_only || p.alive)) ++n;
    return n;
}

Vec3 Match::zone_of(Team t, bool& found) const {
    found = false;
    for (const Obj& o : objs_)
        if (o.kind == Objective::Zone && o.team == t) {
            found = true;
            return o.at;
        }
    return {};
}

void Match::mode_event(ModeEventKind kind, u32 who, Team team, u8 value, const Vec3& at, u32 other) {
    ModeEvent e;
    e.kind = u8(kind);
    e.who = who;
    e.other = other;
    e.team = u8(team);
    e.value = value;
    e.at = at;
    to_all(encode(e));
    mode_dirty_ = true;
    // The ID card's "Accomplished Missions": an objective done by this soldier's own hand.
    switch (kind) {
        case ModeEventKind::Planted: case ModeEventKind::Defused: case ModeEventKind::Delivered: case ModeEventKind::Escaped:
        case ModeEventKind::ConsoleTaken: case ModeEventKind::StrongholdTaken: case ModeEventKind::TreasureOpened: case ModeEventKind::GirlRescued:
            if (Player* p = player(who)) ++p->missions;
            break;
        default: break;
    }
}

void Match::make_undead(Player& p, MatchRole role, Undead body, double now) {
    (void)now;
    p.role = role;
    p.undead = body;
    p.team = Team::Red;
    p.skill_ready = {};
    p.self_bombed = false;
    p.speed_until = p.jump_until = p.search_until = 0;
    mode_dirty_ = true;
}

void Match::drop_item(Obj& item, const Vec3& at, double now) {
    item.state = 2;
    item.at = at;
    item.radius = kTakeReach;   // on the floor now: an ordinary reach (its home's is kept in `value`)
    item.who = 0;
    item.since = now;
    item.until = now + kDroppedHome;
    item.taker = Team::None;
    item.progress = 0;
    mode_event(ModeEventKind::Dropped, 0, Team::None, item.index, at);
}

void Match::blast(Player& by, const Vec3& at, float radius, int damage, u16 weapon_id, double now) {
    std::vector<u32> hit;
    for (auto& [id, o] : players_)
        if (id != by.id && !o.left && o.alive && enemies(by, o)) hit.push_back(id);
    for (u32 id : hit) {
        Player* o = player(id);
        if (!o || !o->alive) continue;
        const Vec3 body = o->position + Vec3{0, 90, 0};
        const float d = eng::length(body - at);
        if (d > radius) continue;
        float amount = float(damage) * std::max(0.2f, 1.0f - d / radius);
        // Through a wall it carries half as far.
        if (map_ && map_->collision.trace_ray(at + Vec3{0, 50, 0}, body).hit()) amount *= 0.5f;
        // The bomb's own blast (no weapon) is marked as the bomb's; anything else as a grenade's.
        apply_damage(by, *o, int(std::lround(amount)), HitZone::Chest, weapon_id, true, now, weapon_id == kNoWeapon ? u16(kKillBomb) : u16(0));
    }
}

// ── The game and its rounds ───────────────────────────────────────────────────

void Match::mode_begin(double now) {
    // Horror: everyone starts human, on the humans' side; the undead are made in the game.
    if (horror())
        for (auto& [id, p] : players_)
            if (p.team != Team::Observer) p.team = Team::Blue;
    next_treasure_ = now + kTreasureEvery;
    next_tick_ = now;
}

void Match::mode_round_start(double now) {
    objs_.clear();
    escaped_ = escape_needed_ = 0;
    mode_phase_ = 0;
    mode_phase_ends_ = 0;
    mode_dirty_ = true;
    mode_ready_ = false;
    for (auto& [id, p] : players_) {
        p.role = MatchRole::None;
        p.undead = Undead::None;
        p.picking = false;
        p.skill_ready = {};
        p.self_bombed = false;
        p.speed_until = p.jump_until = p.search_until = p.power_until = 0;
        p.leech_until = p.cleanse_until = 0;
        p.power = 1.0f;
        p.use_from = 0;
        p.health_max = kMaxHealth;
        p.rise_here = false;
        p.respawn_at = -1;
        if (horror() && p.team != Team::Observer) p.team = Team::Blue;
    }
    std::vector<Player*> side[2];
    for (auto& [id, p] : players_)
        if (!p.left && (p.team == Team::Red || p.team == Team::Blue)) side[p.team == Team::Red ? 0 : 1].push_back(&p);
    for (auto& s : side) std::sort(s.begin(), s.end(), [](const Player* a, const Player* b) { return a->slot < b->slot; });

    switch (settings_.mode) {
        case Mode::CaptureTheCaptain:
            // One captain a side, the side's soldiers in turn round by round.
            for (int t = 0; t < 2; ++t) {
                if (side[t].empty()) continue;
                Player& c = *side[t][captain_turn_[size_t(t)]++ % side[t].size()];
                c.role = MatchRole::Captain;
                c.health_max = kCtcCaptainHealth;
            }
            break;
        case Mode::Captain:
            // A captain for every three; the side's pool (500 a soldier: 4,000 for a full side) shared
            // out at random between them, none below 300.
            for (int t = 0; t < 2; ++t) {
                std::vector<Player*> list = side[t];
                if (list.empty()) continue;
                std::shuffle(list.begin(), list.end(), rng_);
                const int n = captains_for(int(list.size()));
                const int pool = kCaptainPoolPerSoldier * int(list.size());
                std::vector<float> share(static_cast<size_t>(n), 0.0f);
                float sum = 0;
                for (float& w : share) sum += (w = 1.0f + float(rng_() % 1000) / 1000.0f);
                for (int k = 0; k < n; ++k) {
                    list[size_t(k)]->role = MatchRole::Captain;
                    list[size_t(k)]->health_max = std::max(300, int(float(pool) * share[size_t(k)] / sum));
                }
            }
            break;
        case Mode::Horror:
            // A rotten smell first: the hosts turn when this runs out.
            mode_phase_ = 0;
            mode_phase_ends_ = now + kInfectionDelay;
            break;
        case Mode::Horror2: {
            // A quarter start undead (one at least), and pick a class before they come in.
            std::vector<Player*> all = side[1];
            std::shuffle(all.begin(), all.end(), rng_);
            const size_t n = std::max<size_t>(1, all.size() / 4);
            for (size_t k = 0; k < n && k < all.size() && all.size() > 1; ++k) {
                Player& u = *all[k];
                make_undead(u, MatchRole::Zombie, Undead::Boss, now);
                // Not in with the others: in once the class is picked (Match::start_round leaves out
                // whoever has a time to come back).
                u.picking = true;
                u.pick_until = u.respawn_at = now + kPickSeconds;
                if (u.next_class == Undead::None) u.next_class = Undead::Boss;
                if (u.bot) {
                    static const Undead kChoices[] = {Undead::Boss, Undead::Heavy, Undead::Hunter};
                    u.next_class = kChoices[rng_() % std::size(kChoices)];
                    u.picking = false;
                    u.respawn_at = now + 1.0;
                }
            }
            break;
        }
        default:
            break;
    }
    if (map_) {
        // The map's pieces for this round.
        const ServerMap& m = *map_;
        mode_ready_ = true;
        auto add_zone = [&](const sf::LevelZone& z) {
            Obj o;
            o.kind = Objective::Zone;
            o.team = z.team == sf::Team::Blue ? Team::Blue : Team::Red;
            o.at = o.home = z.position;
            o.radius = z.radius;
            objs_.push_back(o);
        };
        // Every cell an attacker walks to from his spawn: what a piece's stand-point and an item's
        // reach are found from (once a map).
        if (walked_.empty()) {
            int red_cell = -1;
            for (const sf::LevelSpawn& s : m.spawns)
                if (s.team != sf::Team::Blue && red_cell < 0) red_cell = m.nav.locate(settle_spawn(move_def_, m.collision, s.position));
            walked_ = m.nav.reachable_from(red_cell);
        }
        auto add_item = [&](u8 index) {
            for (const sf::LevelObjective& lo : m.objectives) {
                const std::string type = eng::str::lower(lo.type), file = eng::str::lower(lo.file);
                if (type == "bomb" || file.find("silolcd") != std::string::npos) continue;
                Obj o;
                o.kind = Objective::Item;
                o.index = index;
                o.at = o.home = lo.position;
                const float reach = item_reach(m.nav, lo.position, walked_);
                o.radius = reach > 0 ? reach : kTakeReach;
                o.value = u16(o.radius);
                objs_.push_back(o);
                return;
            }
        };
        if (settings_.mode == Mode::TeamBattle) {
            switch (m.rules.mission) {
                case Mission::Blast: {
                    u8 k = 0;
                    for (const sf::LevelObjective& lo : m.objectives)
                        if (eng::str::lower(lo.type) == "bomb") {
                            Obj o;
                            o.kind = Objective::Site;
                            o.index = k++;
                            o.at = o.home = lo.position;
                            o.radius = kSiteReach;
                            objs_.push_back(o);
                        }
                    break;
                }
                case Mission::Capture:
                    add_item(0);
                    for (const sf::LevelZone& z : m.zones)
                        if (z.team != sf::Team::Blue) add_zone(z);
                    break;
                case Mission::Escape:
                    for (const sf::LevelZone& z : m.zones)
                        if (z.team != sf::Team::Blue) add_zone(z);
                    // Half the attackers (rounded up) must get out.
                    escape_needed_ = u16(std::max(1, (int(side[0].size()) + 1) / 2));
                    break;
                case Mission::Dual:
                    add_item(0);
                    for (const sf::LevelZone& z : m.zones) add_zone(z);
                    break;
                default:
                    break;
            }
        } else if (settings_.mode == Mode::Occupy) {
            u8 k = 0;
            for (const sf::LevelZone& z : m.zones) {
                Obj o;
                o.kind = Objective::Console;
                o.index = k++;
                o.at = o.home = z.position;
                o.radius = z.radius;
                objs_.push_back(o);
            }
            add_item(0);   // the sample
        } else if (settings_.mode == Mode::Pirate) {
            // The strongholds, red's end first: red_location, center_location, blue_location.
            std::vector<sf::LevelPlace> places = m.places;
            auto order = [](const std::string& n) { return n.find("red") != std::string::npos ? 0 : n.find("blue") != std::string::npos ? 2 : 1; };
            std::sort(places.begin(), places.end(), [&](const auto& a, const auto& b) { return order(a.name) < order(b.name); });
            u8 k = 0;
            for (const sf::LevelPlace& pl : places) {
                Obj o;
                o.kind = Objective::Stronghold;
                o.index = k++;
                o.at = o.home = pl.position;
                o.radius = std::max(150.0f, pl.radius);
                objs_.push_back(o);
            }
            // The ship's cannons (Game/Modes.hpp), each free and loaded.
            balls_.clear();
            u8 cannon = 0;
            for (const CannonSpot& c : pirate_cannons(settings_.map)) {
                Obj o;
                o.kind = Objective::Cannon;
                o.index = cannon++;
                o.at = c.at;
                o.home = c.forward;
                o.radius = kCannonReach;
                objs_.push_back(o);
            }
            Obj t;
            t.kind = Objective::Treasure;
            t.until = next_treasure_;
            t.radius = 150;
            objs_.push_back(t);
        } else if (settings_.mode == Mode::Horror2) {
            u8 k = 0;
            for (const Vec3& at : m.ammo_spots) {
                Obj o;
                o.kind = Objective::Supply;
                o.index = k++;
                o.state = 1;
                o.at = o.home = at;
                o.radius = 120;
                o.value = u16(rng_() % 6);
                objs_.push_back(o);
                if (k >= 8) break;   // a handful about the map, not every spot
            }
            if (!m.npc_spots.empty()) {
                Obj g;
                g.kind = Objective::Girl;
                g.at = g.home = m.npc_spots[rng_() % m.npc_spots.size()];
                g.radius = 150;
                objs_.push_back(g);
            }
        }
        // Where a soldier stands to use each piece (the bots head there).
        for (Obj& o : objs_) {
            const int c = stand_cell(m.nav, o.at, walked_, std::max(o.radius, 300.0f));
            o.stand = c >= 0 ? m.nav.node(c).pos : o.at;
        }
    }
}

void Match::mode_spawn(Player& p, Spawn& s, double now) {
    switch (settings_.mode) {
        case Mode::Sniper: {
            // Sniper rifles only (the PSG-1 for a soldier without one), pistols and knives; no grenades.
            const WeaponDef* w = weapon(s.loadout[0]);
            if (!w || w->klass != WeaponClass::Sniper) {
                const WeaponDef* psg = weapon_by_model("psg1");
                s.loadout[0] = psg ? psg->id : kNoWeapon;
            }
            for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c) s.loadout[c] = kNoWeapon;
            break;
        }
        case Mode::CaptureTheCaptain:
        case Mode::Captain:
            if (p.role == MatchRole::Captain) s.health = u16(std::clamp(p.health_max, 1, 65535));
            break;
        case Mode::Horror:
            if (undead(p)) {
                s.health = u16(p.role == MatchRole::Host ? kHostHealth : kInfectedHealth);
                s.loadout = kNoLoadout, s.loadout[size_t(Slot::Melee)] = claws_weapon();
            } else {
                s.health = u16(kHumanHealth);
            }
            break;
        case Mode::Horror2:
            if (undead(p)) {
                Undead u = p.next_class;
                if (u < Undead::Boss || u >= Undead::Count || undead_def(u).unlock_rank > p.rank) u = Undead::Boss;
                p.undead = u;
                const float grown = 1.0f + 0.25f * float(p.rank - 1);
                s.health = u16(std::clamp(int(float(undead_def(u).health) * grown), 1, 65535));
                s.loadout = kNoLoadout, s.loadout[size_t(Slot::Melee)] = claws_weapon();
                p.picking = false;
                mode_event(ModeEventKind::ClassPicked, p.id, Team::Red, u8(u));
            } else {
                s.health = u16(kHorror2Human);
            }
            break;
        case Mode::TeamSlayer:
            p.protected_until = now + kProtectSeconds;
            if (p.rage >= kRageDeaths) {
                p.rage = 0;
                p.rage_until = now + kRageSeconds;
                mode_event(ModeEventKind::RageOn, p.id, p.team);
            }
            break;
        default:
            break;
    }
    // Horror: an infected soldier (or a host turning) rises where he stood.
    if (p.rise_here) {
        s.here = true;
        s.at = p.died_at;
        s.yaw = p.died_yaw;
        p.rise_here = false;
    }
    mode_dirty_ = true;
}

void Match::mode_kill(Player& attacker, Player& victim, u16 weapon_id, u16 flags, double now) {
    (void)weapon_id;
    // A team kill scores nothing, as a soldier's own death does not.
    const bool suicide = attacker.id == victim.id || (flags & kKillTeam);
    // Whatever the victim carried or was doing goes down with him.
    for (Obj& o : objs_) {
        if (o.kind == Objective::Item && o.state == 1 && o.who == victim.id) drop_item(o, victim.died_at, now);
        if (o.kind == Objective::Bomb && o.state == 1 && o.who == victim.id) o.state = 0xFF;   // a plant cut short
        if (o.kind == Objective::Bomb && o.state == 3 && o.who == victim.id) o.state = 2, o.who = 0, o.progress = 0;
    }
    std::erase_if(objs_, [](const Obj& o) { return o.kind == Objective::Bomb && o.state == 0xFF; });

    // A kill's points, as Match::kill counted them for the soldier.
    const KillScore ks;
    int points = ks.points;
    if (flags & kKillHeadshot) points += ks.headshot;
    if (flags & kKillGrenade) points += ks.grenade;
    if (flags & kKillKnife) points += ks.knife;
    if (flags & kKillDouble) points += ks.double_kill;
    if (flags & (kKillMulti | kKillSpecialForce)) points += ks.multi_kill;
    auto side_points = [&](Team t, int n) {
        if (t == Team::Red) red_ = u16(std::min(65535, red_ + n));
        else if (t == Team::Blue) blue_ = u16(std::min(65535, blue_ + n));
    };

    switch (settings_.mode) {
        case Mode::TeamDeathmatch:
            if (!suicide) side_points(attacker.team, points);
            attacker.extra[0] = attacker.score;
            break;
        case Mode::TeamSlayer:
            if (!suicide) {
                // Team Slayer's own: a revenge kill one more, a kill in rage two more.
                int bonus = 0;
                if (flags & kKillRevenge) bonus += 1;
                if (now < attacker.rage_until) bonus += 2;
                attacker.score = u16(std::min(65535, attacker.score + bonus));
                side_points(attacker.team, points + bonus);
                attacker.rage = 0;
                ++attacker.vs[victim.id][0];
                ++victim.vs[attacker.id][1];
                // His magazine on the floor, for whoever walks over it.
                Obj mag;
                mag.kind = Objective::Magazine;
                mag.at = victim.died_at;
                mag.until = now + kMagazineSeconds;
                mag.radius = 100;
                objs_.push_back(mag);
                size_t mags = 0;
                for (const Obj& o : objs_) mags += o.kind == Objective::Magazine;
                if (mags > kMagazinesMax)
                    if (auto it = std::find_if(objs_.begin(), objs_.end(), [](const Obj& o) { return o.kind == Objective::Magazine; }); it != objs_.end())
                        objs_.erase(it);
            }
            ++victim.rage;
            attacker.extra[0] = attacker.score;
            victim.extra[0] = victim.score;
            break;
        case Mode::Sniper:
            if (!suicide) side_points(attacker.team, 1);
            break;
        case Mode::CaptureTheCaptain:
            if (victim.role == MatchRole::Captain) {
                victim.respawn_at = -1;
                if (!suicide) ++attacker.extra[0];
                mode_event(ModeEventKind::CaptainDown, victim.id, victim.team, 0, victim.died_at, attacker.id);
                mode_end_round(other_side(victim.team), RoundReason::Objective, now);
            }
            break;
        case Mode::Captain:
            if (victim.role == MatchRole::Captain) {
                victim.respawn_at = -1;
                if (!suicide) ++attacker.extra[0];
                mode_event(ModeEventKind::CaptainDown, victim.id, victim.team, 0, victim.died_at, attacker.id);
            } else {
                victim.respawn_at = now + kCaptainRespawn;
            }
            break;
        case Mode::Horror:
            victim.respawn_at = -1;
            if (!suicide && undead(attacker) && !undead(victim) && spend_horror_item(victim, HorrorItem::Rebirth)) {
                // Rebirth: not infected; back a human where he fell.
                victim.respawn_at = now + kRebirthSeconds;
                victim.rise_here = true;
            } else if (!suicide && undead(attacker) && !undead(victim)) {
                // Infected: he rises one of them where he fell.
                ++attacker.extra[1];
                ++attacker.infections;
                make_undead(victim, MatchRole::Zombie, infected_body(victim.force), now);
                victim.respawn_at = now + kRiseSeconds;
                victim.rise_here = true;
                mode_event(ModeEventKind::Infected, victim.id, Team::Red, 0, victim.died_at, attacker.id);
            } else if (!suicide && !undead(attacker) && undead(victim)) {
                ++attacker.extra[0];
            }
            break;
        case Mode::Horror2:
            victim.respawn_at = -1;
            if (!suicide && undead(attacker) && !undead(victim) && spend_horror_item(victim, HorrorItem::Rebirth)) {
                victim.respawn_at = now + kRebirthSeconds;
                victim.rise_here = true;
            } else if (!suicide && undead(attacker) && !undead(victim)) {
                ++attacker.extra[1];
                ++attacker.infections;
                const u8 rank = u8(attacker.infections >= kRank3Infections ? 3 : attacker.infections >= kRank2Infections ? 2 : 1);
                if (rank > attacker.rank) {
                    attacker.rank = rank;
                    attacker.health = std::min(attacker.health + attacker.health_max / 4, attacker.health_max + attacker.health_max / 2);
                    mode_event(ModeEventKind::RankUp, attacker.id, Team::Red, rank);
                }
                make_undead(victim, MatchRole::Zombie, Undead::Boss, now);
                if (victim.bot) {
                    // A bot picks as a player might: any class its rank has.
                    static const Undead kChoices[] = {Undead::Boss, Undead::Heavy, Undead::Hunter, Undead::Driller};
                    victim.next_class = kChoices[rng_() % (victim.rank >= 2 ? 4 : 3)];
                }
                victim.picking = !victim.bot;
                victim.pick_until = now + kUndeadRespawn;
                victim.respawn_at = now + kUndeadRespawn;
                mode_event(ModeEventKind::Infected, victim.id, Team::Red, 0, victim.died_at, attacker.id);
            } else if (undead(victim)) {
                if (!suicide && !undead(attacker)) ++attacker.extra[0];
                // The undead come back, and may pick again while they wait.
                victim.picking = !victim.bot;
                victim.pick_until = now + kUndeadRespawn;
                victim.respawn_at = now + kUndeadRespawn;
            }
            break;
        case Mode::Pirate:
            if (!suicide) {
                side_points(attacker.team, 1);
                attacker.points = u16(attacker.points + 1);
                // A Save: the enemy brought down in a stronghold your side holds or is taking.
                for (const Obj& o : objs_)
                    if (o.kind == Objective::Stronghold && (o.team == attacker.team || o.taker == victim.team) && within(victim.died_at, o.at, o.radius)) {
                        ++attacker.extra[1];
                        attacker.points = u16(attacker.points + 2);
                        break;
                    }
                attacker.extra[0] = attacker.points;
            }
            break;
        default:
            break;
    }
    mode_dirty_ = true;
}

int Match::mode_damage(Player& attacker, Player& victim, int amount, u16 weapon_id, bool grenade, double now) {
    if (&attacker != &victim && now < victim.protected_until) return 0;   // Team Slayer: unbeatable
    float a = float(amount);
    if (undead(attacker) && !grenade)
        if (const WeaponDef* w = weapon(weapon_id); w && w->klass == WeaponClass::Knife) {
            // Claws: the class's (Horror Mode 2, a little more for each rank), else Horror's.
            a = settings_.mode == Mode::Horror2 ? float(undead_def(attacker.undead).claw) * (1.0f + 0.15f * float(attacker.rank - 1)) : float(kClawDamage);
        }
    if (now < attacker.rage_until) a *= kRageDamage;
    if (now < attacker.power_until) a *= attacker.power;
    // Blood Sucking: an undead's blow gives him the health it takes.
    if (now < attacker.leech_until && undead(attacker) && &attacker != &victim && !undead(victim))
        attacker.health = std::min(std::max(attacker.health_max, kMaxHealth), attacker.health + int(std::lround(a)));
    return int(std::lround(a));
}

void Match::mode_end_round(Team winner, RoundReason reason, double now) {
    if (phase_ != Phase::Live) return;
    if (horror())
        for (auto& [id, p] : players_)
            if (!p.left && p.team == winner) ++p.round_wins;
    end_round(winner, reason, now);
    mode_dirty_ = true;
    send_mode_state(now);
}

void Match::mode_time_up(double now) {
    switch (settings_.mode) {
        case Mode::TeamBattle: {
            // A bomb set keeps the round going until it goes off or is made safe.
            if (const Obj* b = find_obj(Objective::Bomb); b && (b->state == 2 || b->state == 3)) {
                round_ends_ = b->until + 1.0;
                return;
            }
            const bool dual = map_ && map_->rules.mission == Mission::Dual;
            mode_end_round(dual ? Team::None : Team::Blue, RoundReason::TimeUp, now);
            return;
        }
        case Mode::CaptureTheCaptain:
            mode_end_round(Team::None, RoundReason::TimeUp, now);
            return;
        case Mode::Captain: {
            // The side whose captains have more of their health left.
            float left[2] = {0, 0}, full[2] = {0, 0};
            for (const auto& [id, p] : players_) {
                if (p.left || p.role != MatchRole::Captain || (p.team != Team::Red && p.team != Team::Blue)) continue;
                const int t = p.team == Team::Red ? 0 : 1;
                full[t] += float(p.health_max);
                left[t] += p.alive ? float(std::max(0, p.health)) : 0.0f;
            }
            const float r = full[0] > 0 ? left[0] / full[0] : 0, b = full[1] > 0 ? left[1] / full[1] : 0;
            mode_end_round(r > b + 1e-3f ? Team::Red : b > r + 1e-3f ? Team::Blue : Team::None, RoundReason::TimeUp, now);
            return;
        }
        case Mode::Horror:
        case Mode::Horror2:
            // The humans held out.
            mode_end_round(count_side(Team::Blue, true) > 0 ? Team::Blue : Team::Red, RoundReason::TimeUp, now);
            return;
        default:
            // Time runs out on the attackers: the defending side (blue) holds.
            mode_end_round(Team::Blue, RoundReason::TimeUp, now);
            return;
    }
}

void Match::mode_check(double now) {
    const int red_n = count_side(Team::Red, false), blue_n = count_side(Team::Blue, false);
    int red_alive = count_side(Team::Red, true), blue_alive = count_side(Team::Blue, true);
    switch (settings_.mode) {
        case Mode::Horror: {
            if (mode_phase_ == 0) return;   // nobody has turned yet
            // The infected about to rise count as standing.
            for (const auto& [id, p] : players_)
                if (!p.left && p.loaded && undead(p) && !p.alive && p.respawn_at > 0) ++red_alive;
            // So does a human about to be reborn (a Rebirth spent: no other human comes back).
            for (const auto& [id, p] : players_)
                if (!p.left && p.loaded && !undead(p) && p.team == Team::Blue && !p.alive && p.respawn_at > 0) ++blue_alive;
            if (blue_alive == 0) mode_end_round(Team::Red, RoundReason::Elimination, now);
            else if (red_alive == 0) mode_end_round(Team::Blue, RoundReason::Elimination, now);
            return;
        }
        case Mode::Horror2:
            // Every human down or turned: the undead have it (one about to be reborn still stands).
            for (const auto& [id, p] : players_)
                if (!p.left && p.loaded && !undead(p) && p.team == Team::Blue && !p.alive && p.respawn_at > 0) ++blue_alive;
            if (red_n + blue_n > 1 && blue_alive == 0) mode_end_round(Team::Red, RoundReason::Elimination, now);
            return;
        case Mode::CaptureTheCaptain:
            return;   // soldiers come back; a captain down ends it (mode_kill)
        case Mode::Captain: {
            int captains[2] = {0, 0}, standing[2] = {0, 0};
            for (const auto& [id, p] : players_)
                if (!p.left && p.role == MatchRole::Captain && (p.team == Team::Red || p.team == Team::Blue)) {
                    ++captains[p.team == Team::Red ? 0 : 1];
                    standing[p.team == Team::Red ? 0 : 1] += p.alive;
                }
            if (captains[0] > 0 && standing[0] == 0) mode_end_round(Team::Blue, RoundReason::Objective, now);
            else if (captains[1] > 0 && standing[1] == 0) mode_end_round(Team::Red, RoundReason::Objective, now);
            return;
        }
        default:
            break;
    }
    if (red_n == 0 || blue_n == 0) return;   // a practice round with one side empty never ends early
    const Obj* bomb = find_obj(Objective::Bomb);
    const bool ticking = bomb && (bomb->state == 2 || bomb->state == 3);
    if (blue_alive == 0) {
        mode_end_round(Team::Red, RoundReason::Elimination, now);
    } else if (red_alive == 0 && !ticking) {
        // Flee: those already out may be enough (mode_tick ends it the moment they are).
        mode_end_round(Team::Blue, RoundReason::Elimination, now);
    } else if (settings_.mode == Mode::TeamBattle && escape_needed_ > 0 && red_alive + escaped_ < escape_needed_) {
        mode_end_round(Team::Blue, RoundReason::Elimination, now);   // too few left to get out
    }
}

bool Match::mode_tells(const Player& viewer, const Player& target, double now) const {
    // Captain Mode: "the positions of the captains on the opposing team are visible to all".
    if (settings_.mode == Mode::Captain && target.role == MatchRole::Captain) return true;
    // An undead's Search: every human, wherever.
    if (now < viewer.search_until && undead(viewer) && !undead(target)) return true;
    return false;
}

// ── Every tick ────────────────────────────────────────────────────────────────

void Match::mode_tick(double now) {
    const float dt = float(std::clamp(now - next_tick_, 0.0, 0.25));
    next_tick_ = now;
    if (!mode_ready_ && map_ && phase_ == Phase::Live) {
        // The map was not read when the round began: its pieces now.
        const auto keep_roles = players_;
        mode_round_start(now);
        for (auto& [id, p] : players_)
            if (auto it = keep_roles.find(id); it != keep_roles.end()) p.role = it->second.role, p.undead = it->second.undead, p.team = it->second.team,
                                                                     p.health_max = it->second.health_max, p.picking = it->second.picking;
    }
    auto holding_use = [&](const Player& p, float seconds) { return p.alive && p.use_from > 0 && now - p.use_from >= seconds; };

    // ── Team Battle's missions, and Occupy's sample ──
    if (settings_.mode == Mode::TeamBattle || settings_.mode == Mode::Occupy) {
        const Mission mission = map_ ? map_->rules.mission : Mission::Elimination;
        if (settings_.mode == Mode::TeamBattle && mission == Mission::Blast) {
            Obj* bomb = find_obj(Objective::Bomb);
            if (!bomb || bomb->state == 1) {
                // A plant: an attacker at a site with Use held, five seconds.
                for (auto& [id, p] : players_) {
                    if (!p.alive || p.team != Team::Red || p.use_from <= 0) continue;
                    if (bomb && bomb->who != id) continue;
                    for (const Obj& site : objs_) {
                        if (site.kind != Objective::Site || !within(p.position, site.at, kSiteReach)) continue;
                        if (!bomb) {
                            Obj b;
                            b.kind = Objective::Bomb;
                            b.state = 1;
                            b.who = id;
                            b.index = site.index;
                            b.at = p.position;
                            b.radius = kDefuseReach;
                            objs_.push_back(b);
                            bomb = &objs_.back();
                            mode_event(ModeEventKind::Planting, id, Team::Red, site.index, p.position);
                        }
                        bomb->progress = float(std::min(1.0, (now - p.use_from) / kPlantSeconds));
                        mode_dirty_ = true;
                        if (bomb->progress >= 1.0f) {
                            bomb->state = 2;
                            bomb->until = now + kFuseSeconds;
                            bomb->progress = 0;
                            bomb->who = id;
                            p.use_from = 0;
                            mode_event(ModeEventKind::Planted, id, Team::Red, bomb->index, bomb->at);
                            LOG_INFO("Match %u: %s planted the bomb at site %c", id_, p.name.c_str(), 'A' + bomb->index);
                        }
                        break;
                    }
                    break;
                }
                // The planter let go, or walked off it: the plant is lost.
                if (bomb && bomb->state == 1) {
                    const Player* planter = player(bomb->who);
                    if (!planter || !planter->alive || planter->use_from <= 0 || !within(planter->position, bomb->at, kSiteReach)) {
                        objs_.erase(std::find_if(objs_.begin(), objs_.end(), [](const Obj& o) { return o.kind == Objective::Bomb; }));
                        bomb = nullptr;
                        mode_dirty_ = true;
                    }
                }
            }
            if (bomb && (bomb->state == 2 || bomb->state == 3)) {
                // A defuse: a defender at the bomb with Use held, seven seconds.
                Player* defuser = bomb->state == 3 ? player(bomb->who) : nullptr;
                if (defuser && (!defuser->alive || defuser->use_from <= 0 || !within(defuser->position, bomb->at, kDefuseReach))) {
                    bomb->state = 2, bomb->who = 0, bomb->progress = 0, defuser = nullptr;
                    mode_dirty_ = true;
                }
                if (!defuser)
                    for (auto& [id, p] : players_)
                        if (p.alive && p.team == Team::Blue && p.use_from > 0 && within(p.position, bomb->at, kDefuseReach)) {
                            bomb->state = 3, bomb->who = id, defuser = &p;
                            mode_event(ModeEventKind::Defusing, id, Team::Blue, bomb->index, bomb->at);
                            break;
                        }
                if (defuser) {
                    bomb->progress = float(std::min(1.0, (now - defuser->use_from) / kDefuseSeconds));
                    mode_dirty_ = true;
                    if (bomb->progress >= 1.0f) {
                        bomb->state = 5;
                        mode_event(ModeEventKind::Defused, defuser->id, Team::Blue, bomb->index, bomb->at);
                        LOG_INFO("Match %u: %s defused the bomb", id_, defuser->name.c_str());
                        mode_end_round(Team::Blue, RoundReason::Objective, now);
                        return;
                    }
                }
                if (now >= bomb->until) {
                    bomb->state = 4;
                    const Vec3 at = bomb->at;
                    const u32 planter = bomb->who;
                    mode_event(ModeEventKind::Exploded, planter, Team::Red, bomb->index, at);
                    LOG_INFO("Match %u: the bomb went off", id_);
                    mode_end_round(Team::Red, RoundReason::Objective, now);
                    // Whoever set it is credited with what it does (when he is still here to be).
                    for (auto& [id, p] : players_)
                        if (!p.left && p.team == Team::Red) {
                            blast(p, at, kBlastRadius, kBombDamage, kNoWeapon, now);
                            break;
                        }
                    return;
                }
            }
        }
        // A thing to take: Take Back, Dual, the Silo's sample.
        if (Obj* item = find_obj(Objective::Item)) {
            const bool dual = settings_.mode == Mode::TeamBattle && mission == Mission::Dual;
            if (item->state == 1) {
                Player* carrier = player(item->who);
                if (!carrier || !carrier->alive) {
                    drop_item(*item, carrier ? carrier->position : item->at, now);
                } else {
                    item->at = carrier->position;
                    // Home: his side's zone (the Silo: either console).
                    for (const Obj& z : objs_) {
                        const bool goal = settings_.mode == Mode::Occupy ? z.kind == Objective::Console
                                                                        : z.kind == Objective::Zone && z.team == carrier->team;
                        if (goal && within(carrier->position, z.at, z.radius)) {
                            item->state = 3;
                            if (settings_.mode == Mode::Occupy) ++carrier->extra[1];
                            mode_event(ModeEventKind::Delivered, carrier->id, carrier->team, item->index, carrier->position);
                            LOG_INFO("Match %u: %s brought the item home", id_, carrier->name.c_str());
                            mode_end_round(carrier->team, RoundReason::Objective, now);
                            return;
                        }
                    }
                }
            } else if (item->state == 0 || item->state == 2) {
                bool touched_by_defender = false;
                for (auto& [id, p] : players_) {
                    if (!p.alive || !reaches(p.position, item->at, item->radius) || (p.team != Team::Red && p.team != Team::Blue)) continue;
                    if (dual || p.team == Team::Red) {
                        item->state = 1, item->who = id, item->taker = Team::None, item->progress = 0;
                        mode_event(ModeEventKind::Taken, id, p.team, item->index, p.position);
                        break;
                    }
                    // A defender standing on it a while sends it home.
                    if (item->state == 2) {
                        touched_by_defender = true;
                        if (item->taker != Team::Blue) item->taker = Team::Blue, item->since = now;
                        item->progress = float(std::min(1.0, (now - item->since) / kReturnSeconds));
                        mode_dirty_ = true;
                        if (item->progress >= 1.0f) {
                            item->state = 0, item->at = item->home, item->taker = Team::None, item->progress = 0, item->radius = float(item->value);
                            mode_event(ModeEventKind::Returned, id, Team::Blue, item->index, item->home);
                        }
                    }
                }
                if (item->state == 2 && !touched_by_defender && item->taker == Team::Blue) item->taker = Team::None, item->progress = 0, mode_dirty_ = true;
                if (item->state == 2 && now >= item->until) {
                    item->state = 0, item->at = item->home, item->radius = float(item->value);
                    mode_event(ModeEventKind::Returned, 0, Team::None, item->index, item->home);
                }
            }
        }
        // Flee: an attacker in the zone is out, and counted.
        if (settings_.mode == Mode::TeamBattle && mission == Mission::Escape) {
            for (auto& [id, p] : players_) {
                if (!p.alive || p.team != Team::Red) continue;
                for (const Obj& z : objs_)
                    if (z.kind == Objective::Zone && z.team == Team::Red && within(p.position, z.at, z.radius)) {
                        p.alive = false;
                        p.role = MatchRole::Escaped;
                        p.respawn_at = -1;
                        ++escaped_;
                        mode_event(ModeEventKind::Escaped, id, Team::Red, u8(std::min<int>(escaped_, 255)), p.position);
                        LOG_INFO("Match %u: %s escaped (%u of %u)", id_, p.name.c_str(), unsigned(escaped_), unsigned(escape_needed_));
                        break;
                    }
            }
            if (escape_needed_ > 0 && escaped_ >= escape_needed_) {
                mode_end_round(Team::Red, RoundReason::Objective, now);
                return;
            }
        }
        // Occupy: a console taken by attackers standing at it alone; both taken wins.
        if (settings_.mode == Mode::Occupy) {
            int taken = 0, consoles = 0;
            for (Obj& c : objs_) {
                if (c.kind != Objective::Console) continue;
                ++consoles;
                if (c.team == Team::Red) {
                    ++taken;
                    continue;
                }
                int reds = 0, blues = 0;
                u32 first = 0;
                for (const auto& [id, p] : players_) {
                    if (!p.alive || !within(p.position, c.at, c.radius)) continue;
                    if (p.team == Team::Red) ++reds, first = first ? first : id;
                    if (p.team == Team::Blue) ++blues;
                }
                const float was = c.progress;
                if (reds > 0 && blues == 0) c.progress = std::min(1.0f, c.progress + dt / kConsoleSeconds), c.taker = Team::Red, c.who = first;
                else if (reds == 0 && c.progress > 0) c.progress = std::max(0.0f, c.progress - dt / kConsoleSeconds);
                if (c.progress != was) mode_dirty_ = mode_dirty_ || int(c.progress * 10) != int(was * 10);
                if (c.progress >= 1.0f) {
                    c.team = Team::Red;
                    ++taken;
                    for (auto& [id, p] : players_)
                        if (p.alive && p.team == Team::Red && within(p.position, c.at, c.radius)) ++p.extra[0];
                    mode_event(ModeEventKind::ConsoleTaken, c.who, Team::Red, c.index, c.at);
                }
            }
            if (consoles > 0 && taken == consoles) {
                mode_end_round(Team::Red, RoundReason::Objective, now);
                return;
            }
        }
    }

    // ── Horror: the hosts turn ──
    if (settings_.mode == Mode::Horror && mode_phase_ == 0 && mode_phase_ends_ > 0 && now >= mode_phase_ends_) {
        std::vector<Player*> humans;
        for (auto& [id, p] : players_)
            if (!p.left && p.loaded && p.alive && p.team == Team::Blue) humans.push_back(&p);
        if (humans.size() >= 2) {
            std::shuffle(humans.begin(), humans.end(), rng_);
            const size_t hosts = std::max<size_t>(1, humans.size() / 4);
            for (size_t k = 0; k < hosts; ++k) {
                Player& h = *humans[k];
                make_undead(h, MatchRole::Host, infected_body(h.force), now);
                h.died_at = h.position;
                h.died_yaw = h.yaw;
                h.rise_here = true;
                spawn(h, now);   // turned where he stands
                mode_event(ModeEventKind::HostsTurned, h.id, Team::Red, 0, h.position);
            }
            mode_phase_ = 1;
            LOG_INFO("Match %u: %zu host zombie(s) of %zu", id_, hosts, humans.size());
        } else {
            mode_phase_ends_ = now + 5.0;   // not enough people to start: wait a little more
        }
        mode_dirty_ = true;
    }

    // ── Horror Mode 2: the select window's time, supply boxes, the girl ──
    if (settings_.mode == Mode::Horror2) {
        for (auto& [id, p] : players_)
            if (p.picking && now >= p.pick_until) p.picking = false, mode_dirty_ = true;
        for (Obj& o : objs_) {
            if (o.kind == Objective::Supply) {
                if (o.state == 0 && now >= o.until) o.state = 1, o.value = u16(rng_() % 6), mode_dirty_ = true;
                if (o.state != 1) continue;
                for (auto& [id, p] : players_) {
                    if (!p.alive || undead(p) || !within(p.position, o.at, o.radius)) continue;
                    // ui_icon_itembuff_*: heal, team heal, power 2 up, power 3 up, supply (ammunition), weapon.
                    switch (o.value) {
                        case 0: p.health = std::min(p.health_max, p.health + 50); break;
                        case 1:
                            for (auto& [mid, mate] : players_)
                                if (mate.alive && !undead(mate)) mate.health = std::min(mate.health_max, mate.health + 30);
                            break;
                        case 2: p.power = 2.0f, p.power_until = now + 15.0; break;
                        case 3: p.power = 3.0f, p.power_until = now + 8.0; break;
                        default: break;   // 4, 5: ammunition, the client's to fill (the event says so)
                    }
                    o.state = 0;
                    o.until = now + kSupplyEvery;
                    mode_event(ModeEventKind::SupplyTaken, id, p.team, u8(o.value), o.at);
                    break;
                }
            } else if (o.kind == Objective::Girl) {
                if (o.state == 2 && now >= o.until && map_ && !map_->npc_spots.empty()) {
                    o.state = 0;
                    o.at = map_->npc_spots[rng_() % map_->npc_spots.size()];
                    mode_dirty_ = true;
                }
                if (o.state != 0) continue;
                for (auto& [id, p] : players_) {
                    if (p.alive && !undead(p) && within(p.position, o.at, o.radius) && p.use_from > 0) {
                        o.who = id;
                        o.progress = float(std::min(1.0, (now - p.use_from) / kGirlSeconds));
                        mode_dirty_ = true;
                        if (holding_use(p, kGirlSeconds)) {
                            // Rescued: every human standing is made whole, and she goes.
                            for (auto& [mid, mate] : players_)
                                if (mate.alive && !undead(mate)) mate.health = mate.health_max;
                            ++p.extra[2];
                            o.state = 2, o.progress = 0, o.until = now + kGirlEvery;
                            p.use_from = 0;
                            mode_event(ModeEventKind::GirlRescued, id, p.team, 0, o.at);
                        }
                        break;
                    }
                }
            }
        }
    }

    // ── Pirate Mode: the strongholds, their points, the treasure ──
    if (settings_.mode == Mode::Pirate) {
        tick_cannons(now, dt);
        int held[2] = {0, 0};
        for (Obj& s : objs_) {
            if (s.kind != Objective::Stronghold) continue;
            int n[2] = {0, 0};
            for (const auto& [id, p] : players_)
                if (p.alive && within(p.position, s.at, s.radius) && (p.team == Team::Red || p.team == Team::Blue)) ++n[p.team == Team::Red ? 0 : 1];
            const Team in = n[0] > 0 && n[1] == 0 ? Team::Red : n[1] > 0 && n[0] == 0 ? Team::Blue : Team::None;
            const float was = s.progress;
            if (in != Team::None && in != s.team) {
                s.taker = in;
                if (s.team != Team::None) {
                    // Theirs: brought back to nobody's first.
                    s.progress = std::max(0.0f, s.progress - dt / kStrongholdSeconds);
                    if (s.progress <= 0) s.team = Team::None, mode_dirty_ = true;
                } else {
                    s.progress = std::min(1.0f, s.progress + dt / kStrongholdSeconds);
                    if (s.progress >= 1.0f) {
                        s.team = in;
                        for (auto& [id, p] : players_)
                            if (p.alive && p.team == in && within(p.position, s.at, s.radius)) {
                                p.points = u16(p.points + 3);
                                p.extra[0] = p.points;
                            }
                        mode_event(ModeEventKind::StrongholdTaken, 0, in, s.index, s.at);
                    }
                }
            } else if (in == Team::None && s.team == Team::None && s.progress > 0 && n[0] + n[1] == 0) {
                s.progress = std::max(0.0f, s.progress - dt / (kStrongholdSeconds * 3));
            } else if (in == s.team && s.team != Team::None) {
                s.progress = 1.0f, s.taker = Team::None;
            }
            if (int(s.progress * 10) != int(was * 10)) mode_dirty_ = true;
            if (s.team == Team::Red) ++held[0];
            if (s.team == Team::Blue) ++held[1];
        }
        if (now - mode_phase_ends_ >= kStrongholdTick) {
            mode_phase_ends_ = now;
            red_ = u16(std::min(65535, red_ + held[0]));
            blue_ = u16(std::min(65535, blue_ + held[1]));
            if (held[0] + held[1] > 0) mode_dirty_ = true, send_score();
        }
        if (Obj* t = find_obj(Objective::Treasure)) {
            if (t->state == 0 && now >= t->until && map_ && !map_->npc_spots.empty()) {
                t->state = 1;
                t->at = map_->npc_spots[rng_() % map_->npc_spots.size()];
                mode_event(ModeEventKind::TreasureOpened, 0, Team::None, 0, t->at);   // value 0: it is there
            } else if (t->state == 1 || t->state == 2) {
                Player* opener = t->state == 2 ? player(t->who) : nullptr;
                if (opener && (!opener->alive || opener->use_from <= 0 || !within(opener->position, t->at, t->radius))) {
                    t->state = 1, t->who = 0, t->progress = 0, opener = nullptr;
                    mode_dirty_ = true;
                }
                if (!opener)
                    for (auto& [id, p] : players_)
                        if (p.alive && p.use_from > 0 && within(p.position, t->at, t->radius)) {
                            t->state = 2, t->who = id, opener = &p;
                            break;
                        }
                if (opener) {
                    t->progress = float(std::min(1.0, (now - opener->use_from) / kTreasureSeconds));
                    mode_dirty_ = true;
                    if (t->progress >= 1.0f) {
                        if (opener->team == Team::Red) red_ = u16(red_ + kTreasurePoints);
                        else blue_ = u16(blue_ + kTreasurePoints);
                        send_score();
                        opener->points = u16(opener->points + 5);
                        opener->extra[0] = opener->points;
                        opener->use_from = 0;
                        mode_event(ModeEventKind::TreasureOpened, opener->id, opener->team, 1, t->at);
                        t->state = 0, t->who = 0, t->progress = 0, t->until = now + kTreasureEvery;
                    }
                }
            }
        }
    }

    // ── Team Slayer: magazines on the floor ──
    if (settings_.mode == Mode::TeamSlayer && !objs_.empty()) {
        for (Obj& m : objs_) {
            if (m.kind != Objective::Magazine || m.state != 0) continue;
            if (now >= m.until) {
                m.state = 1;
                continue;
            }
            for (auto& [id, p] : players_)
                if (p.alive && within(p.position, m.at, m.radius)) {
                    m.state = 1;
                    mode_event(ModeEventKind::MagazineTaken, id, p.team, 0, m.at);
                    break;
                }
        }
        const size_t before = objs_.size();
        std::erase_if(objs_, [](const Obj& o) { return o.kind == Objective::Magazine && o.state != 0; });
        if (objs_.size() != before) mode_dirty_ = true;
    }

    // Skills wearing off.
    for (auto& [id, p] : players_)
        if ((p.speed_until > 0 && now >= p.speed_until) || (p.jump_until > 0 && now >= p.jump_until) || (p.search_until > 0 && now >= p.search_until) ||
            (p.rage_until > 0 && now >= p.rage_until) || (p.protected_until > 0 && now >= p.protected_until) || (p.cleanse_until > 0 && now >= p.cleanse_until)) {
            if (p.cleanse_until > 0 && now >= p.cleanse_until) p.cleanse_until = 0;
            if (p.speed_until > 0 && now >= p.speed_until) p.speed_until = 0;
            if (p.jump_until > 0 && now >= p.jump_until) p.jump_until = 0;
            if (p.search_until > 0 && now >= p.search_until) p.search_until = 0;
            if (p.rage_until > 0 && now >= p.rage_until) p.rage_until = 0;
            if (p.protected_until > 0 && now >= p.protected_until) p.protected_until = 0;
            mode_dirty_ = true;
        }

    if (mode_dirty_ ? now - mode_sent_ >= 0.1 : now - mode_sent_ >= kStateEvery) send_mode_state(now);
}

// ── Skills and classes ────────────────────────────────────────────────────────

void Match::on_skill(Player& p, const UseSkill& m, double now) {
    if (!undead(p) || !p.alive) return;
    const UndeadDef& d = undead_def(p.undead);
    int k = -1;
    for (int i = 0; i < 5; ++i)
        if (d.skills[size_t(i)] == m.skill && m.skill != kSkillNone) k = i;
    if (k < 0 || now < p.skill_ready[size_t(k)]) return;
    const Skill skill = Skill(m.skill);
    const SkillDef& sd = skill_def(skill);
    Vec3 aim = m.aim;
    if (eng::length_sq(aim) < 1e-6f) aim = eng::angles_to_forward(p.yaw, p.pitch);
    aim = eng::normalize(aim);
    const Vec3 eye = p.position + Vec3{0, move_def_.stand_eye, 0};
    // The first enemy along the aim within `reach`, inside `cone` degrees of it, in sight.
    auto first_in_aim = [&](float reach, float cone) -> Player* {
        Player* best = nullptr;
        float best_d = reach;
        for (auto& [id, o] : players_) {
            if (id == p.id || !o.alive || o.left || !enemies(p, o)) continue;
            const Vec3 to = o.position + Vec3{0, 90, 0} - eye;
            const float d = eng::length(to);
            if (d > best_d || d < 1) continue;
            if (eng::dot(to / d, aim) < std::cos(cone * eng::kDegToRad)) continue;
            if (map_ && map_->sight.trace_ray(eye, o.position + Vec3{0, 90, 0}).hit()) continue;
            best = &o, best_d = d;
        }
        return best;
    };
    Vec3 at = p.position;
    u32 other = 0;
    switch (skill) {
        case Skill::Speed: p.speed_until = now + sd.lasts; break;
        case Skill::Jump: p.jump_until = now + sd.lasts; break;
        case Skill::Search: p.search_until = now + sd.lasts; break;
        case Skill::Smoke: {
            GrenadeFx fx;
            fx.thrower = p.id;
            fx.weapon = smoke_weapon();
            fx.origin = p.position + Vec3{0, 30, 0};
            fx.exploded = true;
            if (fx.weapon != kNoWeapon) to_all(encode(fx));
            break;
        }
        case Skill::SelfBomb: {
            if (p.self_bombed) return;
            p.self_bombed = true;
            mode_event(ModeEventKind::SkillUsed, p.id, p.team, m.skill, p.position);
            blast(p, p.position + Vec3{0, 80, 0}, kSelfBombRadius, kSelfBombDamage, claws_weapon(), now);
            if (p.alive) apply_damage(p, p, 1 << 20, HitZone::Chest, kNoWeapon, true, now);
            p.skill_ready[size_t(k)] = now + 1e9;
            return;
        }
        case Skill::Crush: blast(p, p.position + Vec3{0, 40, 0}, kCrushRadius, kCrushDamage, claws_weapon(), now); break;
        case Skill::Drilling:
            if (Player* o = first_in_aim(kDrillReach, 25.0f)) {
                apply_damage(p, *o, kDrillDamage, HitZone::Chest, claws_weapon(), true, now);
                at = o->position, other = o->id;
            }
            break;
        case Skill::Throw: {
            // A rock along the aim: it bursts on the first one it meets, or where it lands.
            Vec3 end = eye + aim * kThrowReach;
            if (map_)
                if (const eng::TraceResult tr = map_->collision.trace_ray(eye, end); tr.hit()) end = tr.end;
            u32 victim = 0;
            HitZone zone = HitZone::None;
            float dist = 0;
            if (trace_soldiers(p, eye, aim, eng::length(end - eye), victim, zone, dist)) end = eye + aim * dist;
            blast(p, end, kThrowRadius, kThrowDamage, claws_weapon(), now);
            at = end;
            break;
        }
        case Skill::Snatch:
            if (Player* o = first_in_aim(kSnatchReach, 20.0f)) {
                // Pulled to just in front of him (his client is told where; a bot is put there).
                Vec3 flat = Vec3{aim.x, 0, aim.z};
                if (eng::length_sq(flat) < 1e-6f) flat = {0, 0, 1};
                at = p.position + eng::normalize(flat) * 150.0f;
                if (map_) at = settle_spawn(move_def_, map_->collision, at + Vec3{0, 30, 0});
                o->position = at;
                if (o->bot) o->move.origin = at, o->move.velocity = {};
                other = o->id;
            } else {
                return;   // nobody to pull: the skill is not spent
            }
            break;
        default: return;
    }
    p.skill_ready[size_t(k)] = now + std::max(0.5f, sd.cooldown);
    mode_event(ModeEventKind::SkillUsed, p.id, p.team, m.skill, at, other);
}

// ── Pirate Mode's cannons ─────────────────────────────────────────────────────

void Match::on_cannon(Player& p, const CannonUse& m, double now) {
    if (settings_.mode != Mode::Pirate) return;
    Obj* c = nullptr;
    for (Obj& o : objs_)
        if (o.kind == Objective::Cannon && o.index == m.index) c = &o;
    if (!c) return;
    switch (CannonOp(m.op)) {
        case CannonOp::Man:
            if (c->who != 0 || eng::length(p.position - c->at) > kCannonReach + 40.0f) return;
            for (Obj& o : objs_)
                if (o.kind == Objective::Cannon && o.who == p.id) o.who = 0, o.state = 0;   // one cannon a gunner
            c->who = p.id, c->state = 1;
            mode_dirty_ = true;
            break;
        case CannonOp::Leave:
            if (c->who != p.id) return;
            c->who = 0, c->state = 0;
            mode_dirty_ = true;
            break;
        case CannonOp::Fire: {
            if (c->who != p.id || now < c->until) return;
            const Vec3 aim = cannon_aim(CannonSpot{c->at, c->home}, m.aim);
            Ball b;
            b.pos = c->at + Vec3{0, kCannonMuzzle, 0} + aim * 90.0f;
            b.vel = aim * kCannonSpeed;
            b.by = p.id;
            b.team = p.team;
            b.born = now;
            CannonFx fx;
            fx.index = c->index;
            fx.by = p.id;
            fx.origin = b.pos;
            fx.velocity = b.vel;
            to_all(encode(fx));
            balls_.push_back(b);
            c->until = now + kCannonReload;
            mode_dirty_ = true;
            break;
        }
        default: break;
    }
}

void Match::cannon_burst(const Ball& b, double now) {
    GrenadeFx fx;
    fx.thrower = b.by;
    fx.weapon = frag_weapon();
    fx.origin = b.pos;
    fx.exploded = true;
    to_all(encode(fx));
    Player* by = player(b.by);
    if (!by) return;   // the gunner has gone: a bang, and no more
    // The other side only (cannon_state: DamageForMySide 0, DamageForMySelf 0).
    std::vector<u32> hit;
    for (auto& [id, o] : players_)
        if (id != by->id && !o.left && o.alive && enemies(*by, o)) hit.push_back(id);
    for (u32 id : hit) {
        Player* o = player(id);
        if (!o || !o->alive) continue;
        const Vec3 body = o->position + Vec3{0, 90, 0};
        int amount = cannon_damage(eng::length(body - b.pos));
        if (amount <= 0) continue;
        if (map_ && map_->collision.trace_ray(b.pos + Vec3{0, 30, 0}, body).hit()) amount /= 2;   // behind something
        apply_damage(*by, *o, amount, HitZone::Chest, frag_weapon(), true, now);
    }
}

void Match::tick_cannons(double now, float dt) {
    // A gunner who fell, left or walked off no longer has his cannon.
    for (Obj& o : objs_) {
        if (o.kind != Objective::Cannon || o.who == 0) continue;
        const Player* p = player(o.who);
        if (!p || !p->alive || eng::length(p->position - o.at) > kCannonReach + 80.0f) o.who = 0, o.state = 0, mode_dirty_ = true;
    }
    for (size_t i = 0; i < balls_.size();) {
        Ball& b = balls_[i];
        bool done = now - b.born > double(kCannonLife);
        // In short steps, so a ball neither skips through a wall nor past a soldier.
        for (float left = dt; !done && left > 0;) {
            const float step = std::min(left, 1.0f / 120.0f);
            left -= step;
            b.vel.y -= kCannonGravity * step;
            const Vec3 next = b.pos + b.vel * step;
            if (map_) {
                if (const eng::TraceResult tr = map_->collision.trace_ray(b.pos, next); tr.hit() && !tr.start_solid) {
                    b.pos = tr.end;
                    cannon_burst(b, now);
                    done = true;
                    break;
                }
            }
            b.pos = next;
            for (const auto& [id, o] : players_)
                if (!o.left && o.alive && o.team != b.team && o.team != Team::Observer && eng::length(o.position + Vec3{0, 90, 0} - b.pos) < 70.0f) {
                    cannon_burst(b, now);
                    done = true;
                    break;
                }
        }
        if (done) balls_.erase(balls_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
}

bool Match::spend_horror_item(Player& p, HorrorItem item) {
    Peer* peer = p.bot ? nullptr : server_.peer(p.id);
    if (!peer || !peer->account) return false;
    u16& have = peer->account->horror_items[size_t(item)];
    if (have == 0) return false;
    --have;
    ++p.horror_used[size_t(item)];   // taken from the universal record by TVAS, with the report
    HorrorItemUsed used;
    used.player = p.id;
    used.item = u8(item);
    used.left = have;
    to_all(encode(used));
    mode_dirty_ = true;
    return true;
}

void Match::on_horror_item(Player& p, u8 item, double now) {
    if ((settings_.mode != Mode::Horror && settings_.mode != Mode::Horror2) || item >= kHorrorItems) return;
    const HorrorItem what = HorrorItem(item);
    const HorrorItemInfo& info = horror_item(what);
    // Each side has its own; Rebirth goes by itself (mode_kill).
    if (what == HorrorItem::Rebirth || info.undead != undead(p)) return;
    // Nothing spent for nothing: health already whole, the same thing still acting.
    const int whole = std::max(p.health_max, kMaxHealth);
    if (what == HorrorItem::RescueKit && p.health >= whole) return;
    if ((what == HorrorItem::SilverBullet || what == HorrorItem::ShoutOfAnger) && now < p.power_until) return;
    if (what == HorrorItem::BlindCleanse && now < p.cleanse_until) return;
    if (what == HorrorItem::BloodSucking && now < p.leech_until) return;
    if (what == HorrorItem::UndeadSpeedUp && now < p.speed_until) return;
    if (!spend_horror_item(p, what)) return;
    switch (what) {
        case HorrorItem::RescueKit: p.health = std::min(whole, p.health + kRescueKitHealth); break;
        case HorrorItem::SilverBullet:
        case HorrorItem::ShoutOfAnger: p.power = kHorrorItemPower, p.power_until = now + double(info.seconds); break;
        case HorrorItem::BlindCleanse: p.cleanse_until = now + double(info.seconds); break;
        case HorrorItem::BloodSucking: p.leech_until = now + double(info.seconds); break;
        case HorrorItem::UndeadSpeedUp: p.speed_until = now + double(info.seconds); break;
        default: break;
    }
}

void Match::on_pick(Player& p, const PickClass& m, double now) {
    if (settings_.mode != Mode::Horror2 || !undead(p)) return;
    const Undead u = Undead(m.undead);
    if (u < Undead::Boss || u >= Undead::Count || undead_def(u).unlock_rank > p.rank) return;
    p.next_class = u;
    p.picking = false;
    // At the round's start they wait for their pick (or the bar); then in at once.
    if (!p.alive && p.respawn_at > now + 0.5 && now - round_started_ < kPickSeconds + 1.0) p.respawn_at = now + 0.5;
    mode_dirty_ = true;
}

// ── What every client is told ─────────────────────────────────────────────────

void Match::send_mode_state(double now) {
    mode_sent_ = now;
    mode_dirty_ = false;
    ModeState base;
    base.phase = mode_phase_;
    base.phase_left = settings_.mode == Mode::Horror && mode_phase_ == 0 ? float(std::max(0.0, mode_phase_ends_ - now)) : 0.0f;
    int humans = 0, dead_undead = 0, undead_n = 0;
    for (const auto& [id, p] : players_) {
        if (p.left || !p.loaded) continue;
        if (horror()) {
            if (undead(p)) ++undead_n, dead_undead += !p.alive;
            else if (p.alive && p.team == Team::Blue) ++humans;
        }
    }
    base.counts = {escaped_, escape_needed_, u16(humans), u16(undead_n - dead_undead)};
    if (mode_.goal == GoalKind::TeamPoints || mode_.goal == GoalKind::TeamKills) base.points = {red_, blue_};
    for (const Obj& o : objs_) {
        // ObjectiveNow's fields as Game/Modes.hpp has them for each kind: `who` is the side taking
        // a console or stronghold, a supply box's buff; `team` an item's defender sending it home.
        ObjectiveNow n;
        n.kind = u8(o.kind);
        n.state = o.state;
        n.team = u8(o.kind == Objective::Item ? o.taker : o.team);
        n.index = o.index;
        n.who = o.kind == Objective::Supply ? u32(o.value) : (o.kind == Objective::Stronghold || o.kind == Objective::Console) ? u32(o.taker) : o.who;
        n.at = o.at;
        n.radius = o.radius;
        n.progress = o.progress;
        n.timer = o.until > now ? float(o.until - now) : 0.0f;
        base.objectives.push_back(n);
    }
    auto role_of = [&](const Player& p, const Player& viewer) {
        RoleNow r;
        r.player = p.id;
        r.role = u8(p.role);
        r.undead = u8(p.undead);
        r.rank = p.rank;
        r.health_max = u16(std::clamp(p.health_max, 0, 65535));
        if (now < p.speed_until) r.flags |= kRoleSpeed, r.boost = float(p.speed_until - now);
        if (now < p.jump_until) r.flags |= kRoleJump, r.boost = std::max(r.boost, float(p.jump_until - now));
        if (now < p.rage_until) r.flags |= kRoleRage, r.boost = std::max(r.boost, float(p.rage_until - now));
        if (now < p.protected_until) r.flags |= kRoleProtected;
        if (now < p.cleanse_until) r.flags |= kRoleCleansed;
        if (p.picking) r.flags |= kRolePicking;
        if (mode_tells(viewer, p, now) && enemies(viewer, p)) r.flags |= kRoleSeen;
        if (settings_.mode == Mode::TeamSlayer && viewer.last_killer == p.id) r.flags |= kRoleKiller;
        return r;
    };
    // Everyone has the same, but for what is their own (cooldowns, rage, their record) and whom
    // they are shown (a Search, their killer).
    for (const auto& [vid, v] : players_) {
        if (v.left || v.bot) continue;
        ModeState s = base;
        for (const auto& [id, p] : players_)
            if (!p.left && (p.role != MatchRole::None || p.picking || p.health_max != kMaxHealth || now < p.protected_until || now < p.rage_until || now < p.cleanse_until ||
                            (settings_.mode == Mode::TeamSlayer && v.last_killer == id) || mode_tells(v, p, now)))
                s.roles.push_back(role_of(p, v));
        if (undead(v)) {
            const UndeadDef& d = undead_def(v.undead);
            for (size_t k = 0; k < 5; ++k)
                s.cooldowns[k] = d.skills[k] != kSkillNone && v.skill_ready[k] > now ? float(std::min(9999.0, v.skill_ready[k] - now)) : 0.0f;
        }
        s.rage = u8(std::min(v.rage, 255));
        if (v.last_killer)
            if (auto it = v.vs.find(v.last_killer); it != v.vs.end()) s.vs_mine = u8(std::min<int>(it->second[0], 255)), s.vs_theirs = u8(std::min<int>(it->second[1], 255));
        server_.send(vid, encode(s));
    }
    // The recording keeps everyone's view as the first watcher would have it.
    ModeState rec = base;
    for (const auto& [id, p] : players_)
        if (!p.left && (p.role != MatchRole::None || p.health_max != kMaxHealth)) {
            RoleNow r;
            r.player = id, r.role = u8(p.role), r.undead = u8(p.undead), r.rank = p.rank, r.health_max = u16(std::clamp(p.health_max, 0, 65535));
            rec.roles.push_back(r);
        }
    record(encode(rec));
}

// ── Bots ──────────────────────────────────────────────────────────────────────

bool Match::bot_mode_goal(Player& p, Vec3& goal, bool& use, double now) {
    (void)now;
    use = false;
    auto near_obj = [&](Objective kind, auto&& accept) -> const Obj* {
        const Obj* best = nullptr;
        float best_d = 1e30f;
        for (const Obj& o : objs_)
            if (o.kind == kind && accept(o)) {
                const float d = eng::length(o.at - p.position);
                if (d < best_d) best = &o, best_d = d;
            }
        return best;
    };
    auto go = [&](const Vec3& at, bool hold_use, float reach) {
        goal = at;
        use = hold_use && within(p.position, at, reach * 0.8f);
        return true;
    };
    // A piece at its place: its stand-point, the floor a soldier reaches it from.
    auto go_to = [&](const Obj& o, bool hold_use, float reach) {
        goal = o.stand;
        use = hold_use && within(p.position, o.at, reach * 0.8f);
        return true;
    };
    switch (settings_.mode) {
        case Mode::TeamBattle: {
            const Mission mission = map_ ? map_->rules.mission : Mission::Elimination;
            if (mission == Mission::Blast) {
                const Obj* bomb = find_obj(Objective::Bomb);
                const bool set = bomb && (bomb->state == 2 || bomb->state == 3);
                if (p.team == Team::Red) {
                    if (set) return go(bomb->at, false, kDefuseReach);
                    // A plant under way: its planter keeps at it, the rest cover him. Else a site each,
                    // by the bot's number, so both are tried.
                    if (bomb) return go(bomb->at, bomb->who == p.id, kSiteReach);
                    const Obj* site = near_obj(Objective::Site, [&](const Obj& o) { return o.index == (p.id & 1); });
                    if (!site) site = near_obj(Objective::Site, [](const Obj&) { return true; });
                    if (site) return go_to(*site, true, kSiteReach);
                } else {
                    if (set) return go(bomb->at, true, kDefuseReach);
                    if (const Obj* site = near_obj(Objective::Site, [&](const Obj& o) { return o.index == ((p.id >> 1) & 1); })) return go_to(*site, false, 0);
                }
                return false;
            }
            if (mission == Mission::Capture || mission == Mission::Dual) {
                const Obj* item = find_obj(Objective::Item);
                if (!item) return false;
                bool found = false;
                if (item->state == 1 && item->who == p.id) {
                    const Vec3 home = zone_of(p.team, found);
                    return found && go(home, false, 0);
                }
                if (item->state == 1) {
                    const Player* c = player(item->who);
                    if (c && c->team != p.team) return go(c->position, false, 0);   // after the carrier
                    if (c) return go(c->position + Vec3{150, 0, 0}, false, 0);       // with him
                }
                if (mission == Mission::Capture && p.team == Team::Blue && item->state == 0) return go_to(*item, false, 0);   // guard it
                return item->state == 0 ? go_to(*item, false, 0) : go(item->at, false, 0);
            }
            if (mission == Mission::Escape) {
                bool found = false;
                const Vec3 z = zone_of(Team::Red, found);
                return found && go(z, false, 0);
            }
            return false;
        }
        case Mode::Occupy: {
            const Obj* item = find_obj(Objective::Item);
            if (item && item->state == 1 && item->who == p.id)
                if (const Obj* c = near_obj(Objective::Console, [](const Obj&) { return true; })) return go_to(*c, false, 0);
            // Every other attacker by seat goes for the sample (the first seat for a console).
            if (p.team == Team::Red && item && item->state != 1 && item->state != 3 && (p.slot & 1))
                return item->state == 0 ? go_to(*item, false, 0) : go(item->at, false, 0);
            if (p.team == Team::Blue && (p.id & 1)) return false;   // half the defenders hunt rather than stand guard
            if (const Obj* c = near_obj(Objective::Console, [](const Obj& o) { return o.team != Team::Red; })) return go_to(*c, false, 0);
            return false;
        }
        case Mode::Pirate: {
            if (const Obj* t = find_obj(Objective::Treasure); t && (t->state == 1 || t->state == 2) && eng::length(t->at - p.position) < 2500.0f)
                return go(t->at, true, t->radius);
            if (const Obj* s = near_obj(Objective::Stronghold, [&](const Obj& o) { return o.team != p.team; })) return go_to(*s, false, 0);
            return false;
        }
        case Mode::Horror2:
            if (!undead(p))
                if (const Obj* g = find_obj(Objective::Girl); g && g->state == 0) return go_to(*g, true, g->radius);
            return false;
        default:
            return false;
    }
}

}  // namespace lsfs
