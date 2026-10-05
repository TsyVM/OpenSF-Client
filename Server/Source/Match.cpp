#include "Match.hpp"

#include "Engine/Core/Crypto.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Json.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Ballistics.hpp"
#include "Game/Wear.hpp"

#include <algorithm>
#include <ctime>
#include <cmath>

namespace lsfs {

using namespace lsf::proto;
using eng::Vec3;

namespace {

constexpr double kSnapshotInterval = 1.0 / 20.0;
constexpr double kLoadTimeout = 90.0;
constexpr double kRoundOverSeconds = 5.0;
constexpr double kRespawnSeconds = 3.0;
constexpr double kSpawnProtection = 2.0;
constexpr float kMaxSpeed = 1600.0f;   // cm/s: faster than anything a soldier does, falling included

}  // namespace

namespace {

float rand01() {
    thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

// Which zone's parts protect a hit: 0 the head, 1 the upper body (chest, stomach, arms), 2 the legs.
int protected_zone(HitZone z) { return z == HitZone::Head ? 0 : z == HitZone::Legs ? 2 : 1; }

}  // namespace

void Match::take_account(Player& p, const Account& a) const {
    const u64 now = server_.reward_now();
    // A broken gun stays at home: the starter's takes its slot (Game/Wear.hpp).
    const auto starter = starter_loadout();
    for (size_t s = 0; s < 2; ++s)
        if (a.broken(p.own_loadout[s])) p.own_loadout[s] = p.loadout[s] = starter[s];
    // A "(no sniper)" room: a sniper rifle stays at home too (gametext 87-89).
    if (settings_.no_snipers)
        if (const WeaponDef* w = weapon(p.own_loadout[0]); w && w->klass == WeaponClass::Sniper) {
            p.own_loadout[0] = p.loadout[0] = starter[0];
            const WeaponDef* issue = weapon(starter[0]);
            server_.notice(p.id, NoticeKind::Info, "This room takes no sniper rifles: your " + w->name + " stays behind, and you carry the " + (issue ? issue->name : std::string("issued rifle")) + ".");
        }
    p.parts = a.worn(a.force, now);
    p.name_colour = a.name_colour;
    p.shown_xp = a.shown_xp(now);
    p.holy_bless = a.has_boost(Boost::HolyBless, now);
    // A set worn whole, and the row's marks: his boosts, special point parts, the set, an event.
    p.set = full_set(p.parts, a.force);
    p.marks = 0;
    if (a.has_boost(Boost::PointsX2, now)) p.marks |= kMarkPointsX2;
    if (a.has_boost(Boost::DoubleUp, now)) p.marks |= kMarkDoubleUp;
    if (p.holy_bless) p.marks |= kMarkHolyBless;
    if (part_totals(p.parts, a.force).point > 0 || set_info(p.set).xp_pct > 0) p.marks |= kMarkSpecial;
    if (p.set == ItemSet::BlackDragon) p.marks |= kMarkBlackDragon;
    if (p.set == ItemSet::Santa) p.marks |= kMarkSanta;
    // TV's events (the heartbeat's word): their marks only; the pay is TVAS's.
    if (const u16 xp = server_.events_xp_pct(); xp >= 500) p.marks |= kMarkExpX5;
    else if (xp >= 300) p.marks |= kMarkExpX3;
    else if (xp > 100 || server_.events_sp_pct() > 100) p.marks |= kMarkEvent;
    p.prevent_team_kill = a.has_boost(Boost::PreventTeamKill, now);
    p.spray = a.owns_spray(a.spray, now) ? a.spray : kNoSpray;
}

Match::Match(Server& server, Room& room, const std::string& map)
    : server_(server), room_(room), settings_(room.settings), mode_(mode_info(room.settings.mode)) {
    settings_.map = map;
    std::random_device rd;
    seed_ = rd();
    rng_.seed(seed_);
    for (const Seat& s : room.seats) {
        Peer* peer = server.peer(s.peer);
        if (!peer || !peer->account) continue;
        Player p;
        p.id = s.peer;
        p.account = peer->account->id;
        p.name = peer->account->code_name;
        p.team = s.team;
        p.slot = s.slot;
        p.force = peer->account->force;
        p.loadout = p.own_loadout = peer->account->loadout;
        take_account(p, *peer->account);
        players_[p.id] = std::move(p);
    }
    id_ = server.new_match_id();
    add_bots();
    for (auto& [id, p] : players_)
        if (Peer* peer = server.peer(id)) peer->last_match = id_;
}

Match::~Match() {
    if (recording_.is_open()) {
        recording_.close();
        recording_info(false);
    }
}

void Match::to_all(const std::vector<u8>& msg, bool reliable, u32 except) {
    for (auto& [id, p] : players_)
        if (!p.left && !p.bot && id != except) server_.send(id, msg, reliable);
    record(msg);
}

void Match::record(const std::vector<u8>& msg) {
    if (!recording_.is_open()) return;
    const double now = server_.now();
    recording_.add(u32(std::max(0.0, now - recording_from_) * 1000.0), msg);
    // Every half minute: the block out to the file, and the list's entry brought up to date.
    if (now - recording_saved_ > 30.0) {
        recording_saved_ = now;
        recording_.flush();
        recording_info(true);
    }
}

void Match::recording_info(bool live) {
    Server::RecordingInfo i;
    i.match = id_;
    i.started = started_unix_;
    i.map = settings_.map;
    i.mode = u8(settings_.mode);
    i.seconds = u32(std::max(0.0, server_.now() - recording_from_));
    i.bytes = u32(std::min<size_t>(recording_.bytes_written(), 0xFFFFFFFFu));
    i.live = live;
    for (const auto& [id, p] : players_) {
        i.names.push_back(p.name);
        if (p.account) i.accounts.push_back(p.account), i.teams.push_back(u8(p.team));
    }
    for (const auto& [account, d] : gone_) {
        if (std::find(i.accounts.begin(), i.accounts.end(), account) != i.accounts.end()) continue;
        i.accounts.push_back(account), i.teams.push_back(d.team);
    }
    i.finished = finished_;
    i.winner = u8(winner_);
    i.winner_account = winner_account_;
    // DS-8: kept past its 7 days while its report waits for TVAS.
    i.report_waiting = finished_ && !server_.options().no_progress && server_.tvas().enabled() && report_waiting_;
    server_.recording_update(i);
}

Match::Player* Match::player(u32 id) {
    auto it = players_.find(id);
    return it == players_.end() || it->second.left ? nullptr : &it->second;
}

proto::MatchLoad Match::load_message(u32 you) const {
    MatchLoad m;
    m.settings = settings_;
    m.settings.password.clear();
    m.seed = seed_;
    m.you = you;
    for (const auto& [id, p] : players_) {
        if (p.left) continue;
        MatchPlayer mp;
        mp.id = id;
        mp.name = p.name;
        mp.team = u8(p.team);
        mp.force = p.force;
        mp.loadout = p.loadout;
        mp.xp = p.shown_xp;
        mp.name_colour = p.name_colour;
        mp.parts = p.parts;
        mp.spray = p.spray;
        mp.marks = p.marks;
        m.players.push_back(std::move(mp));
    }
    return m;
}

void Match::begin_loading(double now) {
    phase_ = Phase::Loading;
    phase_at_ = now;
    map_ = server_.maps().get(settings_.map);   // and its reading started, when it is not read yet
    for (auto& [id, p] : players_) server_.send(id, encode(load_message(id)));
    // The recording: its header carries the load as a seat that is nobody's (you = 0).
    const std::filesystem::path path = server_.recording_path(id_);
    if (!path.empty()) {
        replay::Header h;
        h.game_version = kProtocolVersion;
        h.match = id_;
        h.started = started_unix_ = u64(std::time(nullptr));
        h.server = server_.options().name;
        h.load = encode(load_message(0));
        // MT-5: what it was played with, so nobody watches it half drawn.
        h.manifest = server_.content().manifest_hash();
        h.packs = server_.content().heartbeat_list();
        recording_from_ = recording_saved_ = now;
        if (recording_.open(path, h)) {
            recording_info(true);
            LOG_INFO("Match %u in room %u: recording to %s", id_, unsigned(room_.id), eng::str::narrow(path.wstring()).c_str());
        } else {
            LOG_WARN("Match %u: cannot write %s", id_, eng::str::narrow(path.wstring()).c_str());
        }
    }
}

void Match::join(u32 peer_id) {
    Peer* peer = server_.peer(peer_id);
    Seat* seat = room_.seat_of(peer_id);
    if (!peer || !peer->account || !seat) return;
    Player p;
    p.id = peer_id;
    p.account = peer->account->id;
    p.name = peer->account->code_name;
    p.team = seat->team;
    p.slot = seat->slot;
    p.force = peer->account->force;
    p.loadout = p.own_loadout = peer->account->loadout;
    take_account(p, *peer->account);
    // Horror: a newcomer joins the humans; the undead are made, not chosen.
    if (horror() && p.team != Team::Observer) p.team = Team::Blue;
    // Past a Team Battle's half the room's sides are the other way round in the match.
    if (sides_changed_) p.team = p.team == Team::Red ? Team::Blue : p.team == Team::Blue ? Team::Red : p.team;
    players_[peer_id] = std::move(p);
    peer->last_match = id_;
    seat->state = SlotState::Loading;
    // Everyone learns of the newcomer through the load list the newcomer gets and the next
    // snapshot; the others' clients add any id they do not know.
    server_.send(peer_id, encode(load_message(peer_id)));
}

void Match::leave(u32 peer_id, bool deserted) {
    auto it = players_.find(peer_id);
    if (it == players_.end() || it->second.left) return;
    Player& p = it->second;
    review_outliers(p);
    if (p.account) {
        Departed& d = gone_[p.account];
        d.team = u8(p.team);
        d.force = p.force;
        d.horror_used = p.horror_used;
        // Gone before the end of his own accord: a forfeit (the ID card's "Forced Quits"), and his
        // guns worn for it (the original's "gun-endurance penalty", gametext 1083) -- both told to
        // TVAS with the report.
        if (deserted && phase_ != Phase::Over && phase_ != Phase::Done && settings_.mode != Mode::Training && p.team != Team::Observer) {
            d.forfeit = true;
            d.wear = wear_points(p, kWearLeftEarly);
        }
    }
    p.left = true;
    p.alive = false;
    // The last soldier gone: nobody is left to play it out, so it ends now and its report goes
    // (the forfeits and the guns' wear in it) instead of running on empty to its clock.
    bool anyone = false, ever = false;
    for (auto& [id, o] : players_) {
        if (o.bot) continue;
        ever = true;
        anyone = anyone || !o.left;
    }
    if (ever && !anyone) abandoned_ = true;
}

std::map<std::string, int> Match::wear_points(Player& p, int extra) const {
    std::map<std::string, int> out;
    if (settings_.mode == Mode::Training) return out;
    for (size_t s = 0; s < 2; ++s) {
        const WeaponDef* w = weapon(p.own_loadout[s]);
        if (!w || !wears(*w) || w->id >= kFirstPackWeapon) continue;   // a server's own guns never wear universally
        out[w->code] += kWearMatch + int(p.fired[s] / kWearRounds) + extra;
    }
    p.fired = {};
    return out;
}

HitZone Match::taken_zone(const Player& victim, HitZone zone) const {
    // The force's Avoid Headshot (Rules.hpp ForceDef): a head shot taken as one to the chest, that
    // often. An undead has no force.
    if (zone == HitZone::Head && !undead(victim))
        if (const ForceDef* f = force(victim.force); f && f->avoid_headshot > 0 && rand01() < f->avoid_headshot) return HitZone::Chest;
    return zone;
}

void Match::track_fall(Player& p, const Vec3& feet, bool on_ground, bool on_ladder, double now) {
    // On a ladder, dead, between rounds, or one of the undead (Super Jump lands where it likes): no
    // fall. Nor in a new life's first second: the client may still be saying where the last one ended.
    if (!p.alive || on_ladder || undead(p) || phase_ != Phase::Live || now - p.spawned_at < 1.0) {
        p.airborne = false;
        return;
    }
    if (!on_ground) {
        p.air_top = p.airborne ? std::max(p.air_top, feet.y) : feet.y;
        p.airborne = true;
        return;
    }
    if (!p.airborne) return;
    p.airborne = false;
    if (const int hurt = fall_damage(p.air_top - feet.y, part_totals(p.parts, p.force).fall_damage); hurt > 0)
        apply_damage(p, p, hurt, HitZone::Legs, kNoWeapon, false, now, kKillFall);
}

bool Match::enemies(const Player& a, const Player& b) const {
    if (a.id == b.id) return false;
    if (!mode_.teams) return true;
    return a.team != b.team;
}

void Match::spawn(Player& p, double now) {
    if (p.team == Team::Observer) return;
    p.alive = true;
    p.airborne = false;
    p.sprayed = false;
    p.respawn_at = -1;
    p.spawned_at = now;
    p.damaged_by.clear();
    p.velocity = {};
    p.sees.clear();
    Spawn s;
    s.player = p.id;
    // Each side's spawn points taken in turn, starting from the seat, so nobody lands on anybody.
    s.index = u8((p.slot + p.spawns * 3 + (mode_.respawn ? rng_() % 8 : 0)) & 0xFF);
    ++p.spawns;
    s.force = p.force;
    s.loadout = p.own_loadout;
    s.health = kMaxHealth;
    // The game type's: the health, what is carried, a team changed, where (Server/Modes.cpp).
    mode_spawn(p, s, now);
    s.team = u8(p.team);
    p.health = s.health;
    p.health_max = std::max<int>(s.health, kMaxHealth);
    p.loadout = s.loadout;
    if (p.bot) {
        // Its soldier is the server's to place: where the Spawn's index puts it on the client.
        p.move = MoveState{};
        p.move.origin = s.here && map_ ? settle_spawn(move_def_, map_->collision, s.at) : spawn_position(p, s.index);
        p.position = p.move.origin;
        p.bot->follow.clear();
        p.bot->repath_at = 0;
        p.bot->target = 0;
        if (const WeaponDef* w = weapon(p.loadout[0])) p.bot->clip = w->magazine, p.bot->reload_until = 0;
        p.bot->yaw = s.here ? s.yaw : float(p.bot->rng.next() % 360);
    }
    if (s.here) p.position = s.at;
    to_all(encode(s));
}

void Match::start_round(double now) {
    ++round_;
    phase_ = Phase::Live;
    phase_at_ = round_started_ = now;
    round_ends_ = now + double(std::max<int>(1, settings_.minutes)) * 60.0;
    round_first_kill_ = false;
    for (auto& [id, p] : players_) p.round_kills = p.round_sidearm = 0, p.round_knife = false, p.round_killer = 0;
    if (round_ == 1) mode_begin(now);
    mode_round_start(now);
    RoundStart rs;
    rs.round = round_;
    rs.seconds = float(round_ends_ - now);
    to_all(encode(rs));
    // Everyone in at once, but whoever the game type has coming in later (Horror Mode 2's undead
    // pick a class first).
    for (auto& [id, p] : players_)
        if (!p.left && p.loaded && !(p.respawn_at > now)) spawn(p, now);
    send_score();
    send_mode_state(now);
}

void Match::tick(double now) {
    ++tick_;
    if (!map_) map_ = server_.maps().get(settings_.map);
    if (abandoned_ && (phase_ == Phase::Live || phase_ == Phase::RoundOver)) {
        finish(now);
        return;
    }
    if (abandoned_ && phase_ == Phase::Loading && now - phase_at_ > 1.0) {
        phase_ = Phase::Done;
        return;
    }
    switch (phase_) {
        case Phase::Loading: {
            bool all = true;
            for (auto& [id, p] : players_)
                if (!p.left && !p.loaded) all = false;
            // The server's own read of the map first (the bots, the objectives); a player's client
            // takes longer to load anyway, so this only ever holds up a room of bots.
            const bool map_coming = !map_ && server_.maps().available();
            if ((all && !map_coming) || now - phase_at_ > kLoadTimeout) {
                room_.phase = RoomPhase::Playing;
                started_at_ = now;
                for (Seat& s : room_.seats) s.state = SlotState::Playing;
                MatchBegin mb;
                mb.tick = tick_;
                to_all(encode(mb));
                start_round(now);
            }
            return;
        }
        case Phase::Live: {
            bots_tick(now);
            // Back in: whoever the game type said comes back, when it said (Modes.cpp mode_kill).
            for (auto& [id, p] : players_)
                if (!p.left && p.loaded && !p.alive && p.respawn_at > 0 && now >= p.respawn_at) spawn(p, now);
            mode_tick(now);
            if (phase_ != Phase::Live) return;   // the game type ended the round (or the game)
            // Guns left on the floor go after a while.
            if (std::erase_if(objs_, [&](const Obj& o) { return o.kind == Objective::Weapon && now >= o.until; })) mode_dirty_ = true;
            if (now >= round_ends_) {
                if (mode_.rounds) mode_time_up(now);
                else finish(now);
                return;
            }
            check_round(now);
            break;
        }
        case Phase::RoundOver:
            if (now - phase_at_ >= kRoundOverSeconds) {
                // Rounds a side may draw (Dual's, CTC's clock running out) cannot keep a game going for
                // ever: after twice the goal and one more, the side with more rounds has it.
                const bool over = mode_.goal == GoalKind::RoundsPlayed ? round_ >= settings_.goal
                                                                       : (red_ >= settings_.goal || blue_ >= settings_.goal || round_ >= 2 * settings_.goal + 1);
                if (over) {
                    finish(now);
                } else {
                    // The half: attackers and defenders change over before the next round.
                    const int half = side_change_round(settings_.mode, map_ ? map_->rules.mission : Mission::Elimination, settings_.goal);
                    if (half > 0 && !sides_changed_ && int(round_) == half) change_sides();
                    start_round(now);
                }
            }
            break;
        case Phase::Over:
            if (now - phase_at_ >= 1.0) phase_ = Phase::Done;
            return;
        case Phase::Done:
            return;
    }
    if (now - last_snapshot_ >= kSnapshotInterval) {
        last_snapshot_ = now;
        send_snapshot(now);
    }
    if (now - last_score_ >= 2.0) {
        last_score_ = now;
        send_score();
    }
}

void Match::check_round(double now) {
    if (!mode_.rounds) {
        // Point and kill goals.
        if (settings_.goal > 0) {
            if (mode_.goal == GoalKind::TeamPoints || mode_.goal == GoalKind::TeamKills) {
                if (red_ >= settings_.goal || blue_ >= settings_.goal) finish(now);
            } else if (mode_.goal == GoalKind::PlayerKills) {
                for (auto& [id, p] : players_)
                    if (!p.left && p.kills >= settings_.goal) {
                        finish(now);
                        break;
                    }
            }
        }
        return;
    }
    // Rounds: the game type's objectives and eliminations (Modes.cpp).
    mode_check(now);
}

void Match::award_special(Player& p, Special s) {
    if (p.left || p.team == Team::Observer || settings_.mode == Mode::Training) return;
    const SpecialInfo& info = special_info(s);
    p.special_sp = u16(std::min<int>(int(p.special_sp) + info.sp, 60000));
    ++p.missions;
    if (p.bot) return;
    SpecialPoint m;
    m.player = p.id;
    m.kind = u8(s);
    m.sp = info.sp;
    server_.send(p.id, encode(m));
}

void Match::end_round(Team winner, RoundReason reason, double now) {
    if (winner == Team::Red) ++red_;
    if (winner == Team::Blue) ++blue_;
    // The round's own challenges: won inside a minute (everyone on that side), and seven rounds all.
    if (mode_.rounds && mode_.teams) {
        const bool quick = (winner == Team::Red || winner == Team::Blue) && now - round_started_ <= double(kQuickRoundSeconds);
        const bool seven = (winner == Team::Red || winner == Team::Blue) && red_ == 7 && blue_ == 7;
        for (auto& [id, p] : players_) {
            if (p.left || !p.loaded || p.team == Team::Observer) continue;
            if (quick && p.team == winner) award_special(p, Special::QuickRound);
            if (seven) award_special(p, Special::SevenAll);
        }
    }
    // The ID card's survival rate: rounds played, and those still standing (or escaped) at the end.
    for (auto& [id, p] : players_)
        if (!p.left && p.loaded && p.team != Team::Observer) {
            ++p.rounds;
            if (p.alive || p.role == MatchRole::Escaped) ++p.survived;
        }
    phase_ = Phase::RoundOver;
    phase_at_ = now;
    RoundEnd re;
    re.winner = u8(winner);
    re.reason = u8(reason);
    re.red_wins = u8(std::min<u16>(red_, 255));
    re.blue_wins = u8(std::min<u16>(blue_, 255));
    to_all(encode(re));
    send_score();
    recording_.flush();   // a stopped server costs at most the round it was in
}

// A Team Battle's half (Rules.hpp side_change_round): every soldier goes over to the other side,
// and what belongs to a side goes with its soldiers -- the rounds it has won, and in a Clan
// Battle the clan it is. The room's own seats stay as they were made: this is the match's.
void Match::change_sides() {
    auto other = [](Team t) { return t == Team::Red ? Team::Blue : t == Team::Blue ? Team::Red : t; };
    for (auto& [id, p] : players_) p.team = other(p.team);
    for (auto& [account, d] : gone_) d.team = u8(other(Team(d.team)));
    std::swap(red_, blue_);
    std::swap(settings_.red_clan, settings_.blue_clan);
    std::swap(captain_turn_[0], captain_turn_[1]);
    sides_changed_ = true;
    mode_event(ModeEventKind::SidesChanged, 0, Team::None, 0, {});
    send_score();
}

void Match::send_score() {
    Score sc;
    sc.red = red_;
    sc.blue = blue_;
    for (auto& [id, p] : players_) {
        if (p.left) continue;
        ScoreRow r;
        r.id = id;
        r.kills = p.kills, r.deaths = p.deaths, r.assists = p.assists, r.score = p.score;
        r.extra = p.extra;
        r.headshots = p.headshots;
        sc.rows.push_back(r);
    }
    to_all(encode(sc));
}

void Match::finish(double now) {
    if (phase_ == Phase::Over || phase_ == Phase::Done) return;
    for (auto& [id, p] : players_)
        if (!p.left) review_outliers(p);
    phase_ = Phase::Over;
    phase_at_ = now;
    MatchOver over;
    Team winner = Team::None;
    if (mode_.teams && mode_.goal != GoalKind::RoundsPlayed) winner = red_ > blue_ ? Team::Red : blue_ > red_ ? Team::Blue : Team::None;
    over.winner = u8(winner);
    u32 best = 0;
    int best_kills = -1;
    for (auto& [id, p] : players_) {
        if (p.left) continue;
        if (int(p.kills) > best_kills) best_kills = p.kills, best = id;
    }
    over.winner_player = mode_.teams ? 0 : best;
    over.match = recording_.is_open() ? id_ : 0;
    finished_ = true;
    winner_ = winner;
    if (!mode_.teams)
        if (const Player* b = player(best)) winner_account_ = b->account;
    const int minutes = std::max(1, int((now - started_at_) / 60.0 + 0.5));
    over.no_progress = server_.options().no_progress;
    for (auto& [id, p] : players_) {
        if (p.left) continue;
        ScoreRow r;
        r.id = id;
        r.kills = p.kills, r.deaths = p.deaths, r.assists = p.assists, r.score = p.score;
        r.extra = p.extra;
        r.headshots = p.headshots;
        over.rows.push_back(r);
        if (p.bot || !p.account || settings_.mode == Mode::Training || p.team == Team::Observer || over.no_progress) continue;
        // TVAS works the pay out (PR-3): shown as pending until its answer comes (MatchRewards).
        Reward rw;
        rw.id = id;
        rw.pending = true;
        rw.won = mode_.goal == GoalKind::RoundsPlayed ? p.round_wins * 2 >= std::max<int>(1, round_) : mode_.teams ? p.team == winner : id == best;
        over.rewards.push_back(rw);
    }
    to_all(encode(over));
    recording_.close();
    report_waiting_ = true;
    recording_info(false);
    if (!over.no_progress && settings_.mode != Mode::Training) send_report(now, minutes);
    LOG_INFO("Match in room %u over: red %u, blue %u", unsigned(room_.id), unsigned(red_), unsigned(blue_));
}

// PR-3: one report a match, signed (the server's TVAS link), naming every soldier's result -- kills
// and deaths on soldiers apart from those on bots (PR-11), head shots, rounds, objectives, the
// round's challenges, shots and hits, the guns' wear, Horror items spent -- and the recording's hash.
// TVAS pays by its own rules; nothing here is a figure TVAS takes on trust.
void Match::send_report(double now, int minutes) {
    using eng::json::Value;
    Value rep;
    // Unique to this server for good (PR-7: TVAS ignores a repeat), whatever its recordings keep.
    rep["match_id"] = (started_unix_ << 20) | (id_ & 0xFFFFF);
    rep["ended"] = u64(std::time(nullptr));
    rep["mode"] = unsigned(settings_.mode);
    rep["map"] = settings_.map;
    rep["minutes"] = minutes;
    rep["seconds"] = std::max(1, int(now - started_at_));
    rep["rounds"] = std::max<int>(1, round_);
    rep["teams"] = mode_.teams;
    rep["winner"] = unsigned(winner_);
    int bots = 0;
    for (auto& [id, p] : players_) bots += p.bot ? 1 : 0;
    rep["bots"] = bots;
    {
        // The recording's SHA-256, so staff can match a report to what was played.
        if (auto bytes = eng::fs::read_file(server_.recording_path(id_)))
            rep["recording"] = eng::crypto::to_hex(eng::crypto::sha256(std::span<const eng::u8>(bytes->data(), bytes->size())));
    }
    if (settings_.clan_battle) {
        Value cb;
        u64 red = 0, blue = 0;
        for (auto& [id, p] : players_) {
            if (p.left || !p.account) continue;
            const Account* a = server_.accounts().find(p.account);
            if (!a || !a->clan_id) continue;
            if (p.team == Team::Red && eng::str::iequals(a->clan, settings_.red_clan)) red = a->clan_id;
            if (p.team == Team::Blue && eng::str::iequals(a->clan, settings_.blue_clan)) blue = a->clan_id;
        }
        cb["red"] = red;
        cb["blue"] = blue;
        cb["red_score"] = unsigned(red_);
        cb["blue_score"] = unsigned(blue_);
        cb["bots"] = bots;
        rep["clan_battle"] = std::move(cb);
    }
    Value players = Value::array();
    for (auto& [id, p] : players_) {
        if (p.left || p.bot || !p.account || p.team == Team::Observer) continue;
        Value e;
        e["account"] = p.account;
        e["team"] = unsigned(p.team);
        e["won"] = mode_.goal == GoalKind::RoundsPlayed ? p.round_wins * 2 >= std::max<int>(1, round_) : mode_.teams ? p.team == winner_ : winner_account_ == p.account;
        e["kills"] = unsigned(p.kills - std::min<u16>(p.kills, p.bot_kills));
        e["bot_kills"] = unsigned(std::min<u16>(p.kills, p.bot_kills));
        e["deaths"] = unsigned(p.deaths - std::min<u16>(p.deaths, p.bot_deaths));
        e["bot_deaths"] = unsigned(std::min<u16>(p.deaths, p.bot_deaths));
        e["headshots"] = unsigned(std::min<u16>(p.headshots, u16(p.kills - std::min<u16>(p.kills, p.bot_kills))));
        e["score"] = unsigned(p.score);
        e["rounds"] = unsigned(p.rounds);
        e["survived"] = unsigned(p.survived);
        e["missions"] = unsigned(p.missions);
        e["team_kills"] = unsigned(p.team_kills);
        e["shots"] = p.shots;
        e["hits"] = std::min(p.hits, p.shots);
        e["special_sp"] = unsigned(p.special_sp);
        e["multi_kills"] = unsigned(p.multi_kills);
        Value ck = Value::array();
        for (u16 k : p.class_kills) ck.push(unsigned(k));
        e["class_kills"] = std::move(ck);
        e["holy_bless"] = p.holy_bless;
        e["force"] = unsigned(p.force);
        Value wear;
        for (const auto& [code, pts] : wear_points(p, 0)) wear[code] = pts;
        e["wear"] = std::move(wear);
        Value hu = Value::array();
        for (u16 n : p.horror_used) hu.push(unsigned(n));
        e["horror_used"] = std::move(hu);
        players.push(std::move(e));
    }
    for (const auto& [account, d] : gone_) {
        if (!d.forfeit) continue;
        Value e;
        e["account"] = account;
        e["team"] = unsigned(d.team);
        e["forfeit"] = true;
        e["force"] = unsigned(d.force);
        Value wear;
        for (const auto& [code, pts] : d.wear) wear[code] = pts;
        e["wear"] = std::move(wear);
        Value hu = Value::array();
        for (u16 n : d.horror_used) hu.push(unsigned(n));
        e["horror_used"] = std::move(hu);
        players.push(std::move(e));
    }
    rep["players"] = std::move(players);
    // The answer may come after the room is gone: it finds its soldiers by account, not by room.
    // Those who left early are told too, if they are still on the server (their forfeit, their guns).
    std::map<u64, u32> sessions;
    for (auto& [id, p] : players_)
        if (p.account && !p.bot) sessions[p.account] = id;
    Server& server = server_;
    const u32 match = id_;
    server_.tvas().report(rep["match_id"].as_uint(), rep, [&server, match, sessions](const TvasReply& r) { server.match_reported(match, sessions, r); });
}

void Match::kill(Player& attacker, Player& victim, u16 weapon_id, u16 flags, double now) {
    // What the game type adds to how it was made: a captain brought down, Team Slayer's rage.
    if (victim.role == MatchRole::Captain && attacker.id != victim.id) flags |= kKillCaptain;
    if (settings_.mode == Mode::TeamSlayer && now < attacker.rage_until && attacker.id != victim.id) flags |= kKillRage;
    if ((flags & kKillWall) && attacker.id != victim.id) ++attacker.wall_kills;
    victim.alive = false;
    victim.health = 0;
    victim.died_at = victim.position;
    victim.died_yaw = victim.yaw;
    ++victim.deaths;
    // His primary falls where he did (his sidearm when he had none), a magazine and one spare in it.
    if (!undead(victim) && settings_.mode != Mode::Training)
        for (int slot : {0, 1})
            if (const WeaponDef* w = weapon(victim.loadout[size_t(slot)])) {
                const u16 rounds = u16(std::min<int>(w->magazine, 999));
                drop_weapon(victim, slot, rounds, rounds, victim.died_at, now);
                break;
            }
    // Back in a while where the game type has soldiers come back; mode_kill may say otherwise.
    victim.respawn_at = mode_.respawn ? now + kRespawnSeconds : -1;
    if (victim.bot && victim.bot->skill == BotSkill::Target) victim.respawn_at = now + 1.0;
    const bool suicide = attacker.id == victim.id;
    if (suicide) flags |= kKillSuicide;
    // A teammate brought down (a grenade of his: Prevent Team Kill keeps a soldier from it): no
    // kill, no points, a team kill on the thrower's record.
    const bool team_kill = !suicide && !enemies(attacker, victim);
    if (team_kill) {
        flags |= kKillTeam;
        ++attacker.team_kills;
    }
    if (!suicide && !team_kill) {
        ++attacker.kills;
        if (victim.bot) ++attacker.bot_kills;
        if (attacker.bot) ++victim.bot_deaths;
        if (flags & kKillHeadshot) ++attacker.headshots;
        // Double and multi kills: within three seconds of the last.
        attacker.streak = now - attacker.last_kill_at < 3.0 ? attacker.streak + 1 : 1;
        attacker.last_kill_at = now;
        if (attacker.streak == 2) flags |= kKillDouble;
        if (attacker.streak == 3) flags |= kKillMulti;
        if (attacker.streak >= 4) flags |= kKillSpecialForce;
        if (attacker.streak >= 2) ++attacker.multi_kills;
        if (const WeaponDef* w = weapon(weapon_id); w && w->klass < WeaponClass::Count) ++attacker.class_kills[size_t(w->klass)];
        if (attacker.last_killer == victim.id) flags |= kKillRevenge, attacker.last_killer = 0;
        victim.last_killer = attacker.id;
        // The round's challenges (special points, gametext 178-197). An undead earns none.
        if (settings_.mode != Mode::Training && !undead(attacker)) {
            victim.round_killer = attacker.id;
            victim.killed_at = now;
            ++attacker.round_kills;
            if (!round_first_kill_) round_first_kill_ = true, award_special(attacker, Special::FirstKill);
            if (attacker.round_kills == 3) award_special(attacker, Special::ThreeKills);
            if (const WeaponDef* w = weapon(weapon_id); w && w->slot == Slot::Secondary && ++attacker.round_sidearm == 2) award_special(attacker, Special::SidearmKills);
            if ((flags & kKillKnife) && !attacker.round_knife) attacker.round_knife = true, award_special(attacker, Special::KnifeKill);
            if (attacker.kills == 20) award_special(attacker, Special::TwentyKills);
            // A love shot: he was already down by this soldier's hand, moments ago (his grenade, his last bullet).
            if (!attacker.alive && attacker.round_killer == victim.id && now - attacker.killed_at <= double(kLoveShotSeconds))
                award_special(attacker, Special::LoveShot), award_special(victim, Special::LoveShot);
            // Every soldier of the other side, three or more, by the one hand (no coming back in this game type).
            if (mode_.teams && !mode_.respawn) {
                int foes = 0;
                bool all = true;
                for (auto& [id, o] : players_)
                    if (!o.left && o.team != Team::Observer && enemies(attacker, o)) ++foes, all = all && !o.alive && o.round_killer == attacker.id;
                if (foes >= 3 && all) award_special(attacker, Special::AllEnemies);
            }
        }
        // A kill's points (Team Deathmatch's scoring, Docs/Research.md §3): every soldier's own score;
        // the game types that count them for the side do so in mode_kill.
        const KillScore ks;
        int points = ks.points;
        if (flags & kKillHeadshot) points += ks.headshot;
        if (flags & kKillGrenade) points += ks.grenade;
        if (flags & kKillKnife) points += ks.knife;
        if (flags & kKillDouble) points += ks.double_kill;
        if (flags & (kKillMulti | kKillSpecialForce)) points += ks.multi_kill;
        attacker.score = u16(attacker.score + points);
        // An assist for everyone else who did real damage.
        for (auto& [who, dmg] : victim.damaged_by)
            if (who != attacker.id && dmg >= 40)
                if (Player* helper = player(who)) ++helper->assists;
    }
    Kill k;
    k.killer = attacker.id;
    k.victim = victim.id;
    k.weapon = weapon_id;
    k.flags = flags;
    k.killer_health = u16(std::clamp(attacker.health, 0, 65535));
    to_all(encode(k));
    mode_kill(attacker, victim, weapon_id, flags, now);
    send_score();
}

void Match::apply_damage(Player& attacker, Player& victim, int amount, HitZone zone, u16 weapon_id, bool grenade, double now, u16 how) {
    if (!victim.alive || amount <= 0) return;
    if (now - victim.spawned_at < kSpawnProtection && &attacker != &victim) return;
    // A fall's is the ground's: no armour stops it (the parts' own share came off in track_fall).
    if (!(how & kKillFall)) {
        // The game type's say (protection, rage, an undead's claws, a supply's power).
        amount = mode_damage(attacker, victim, amount, weapon_id, grenade, now);
        if (amount <= 0) return;
        // The force's armour (an undead has none).
        if (const ForceDef* f = undead(victim) ? nullptr : force(victim.force)) {
            const bool upper = zone == HitZone::Head || zone == HitZone::Chest || zone == HitZone::Arms;
            const float def = upper ? f->upper_defense : f->lower_defense;
            amount = std::max(1, int(std::lround(float(amount) * (1.0f - def))));
        }
        // The parts worn: each takes its roll off the hit on its zone, the zone's cap over them all.
        if (!victim.parts.empty() && !undead(victim)) {
            const float off = roll_protection(victim.parts, victim.force, protected_zone(zone), &rand01);
            amount = std::max(1, int(std::lround(float(amount) * (1.0f - off))));
        }
    }
    // Prevent Team Kill: no friendly blast hurts (friendly fire being off, the only one is your own).
    if (grenade && !enemies(attacker, victim) && victim.prevent_team_kill) return;
    victim.health -= amount;
    victim.damaged_by[attacker.id] += amount;
    Damage d;
    d.attacker = attacker.id;
    d.victim = victim.id;
    d.amount = u16(std::min(amount, 65535));
    d.health = u16(std::clamp(victim.health, 0, 65535));
    d.zone = u8(zone);
    d.from = attacker.position;
    to_all(encode(d));
    if (victim.health <= 0) {
        u16 flags = how;
        const WeaponDef* w = weapon(weapon_id);
        // A blast has no head shots; the bomb's own is the bomb's, any other a grenade's.
        if (zone == HitZone::Head && !grenade) flags |= kKillHeadshot;
        if (grenade && !(how & kKillBomb) && w && w->klass == WeaponClass::Grenade) flags |= kKillGrenade;
        if (w && w->klass == WeaponClass::Knife) flags |= kKillKnife;
        kill(attacker, victim, weapon_id, flags, now);
    }
}

// A thrown grenade's blast, decided here: the thrower's game only knows of the soldiers it was told
// of (Match::tells), and a grenade is thrown at the ones round the corner. Everyone within its
// reach is hurt by how near they stood, the thrower too; a wall between the blast and all of a
// soldier (head, chest and feet) stops it.
void Match::grenade_blast(Player& by, const WeaponDef& w, const Vec3& at, double now) {
    if (w.damage <= 0 || w.blast_radius <= 0) return;
    // Teammates too: a frag's blast is nobody's friend (Prevent Team Kill keeps a soldier from his
    // side's, as the original's item says: "protects you from friendly kill by RGD5, M67 blast").
    std::vector<u32> near;
    for (auto& [id, o] : players_)
        if (!o.left && o.alive && o.team != Team::Observer && eng::length(o.position - at) <= w.blast_radius + 60.0f) near.push_back(id);
    for (u32 id : near) {
        Player* o = player(id);
        if (!o || !o->alive) continue;
        const float h = hull_height_by_flags(move_def_, o->flags);
        const Vec3 parts[3] = {o->position + Vec3{0, h * 0.55f, 0}, o->position + Vec3{0, h - 12, 0}, o->position + Vec3{0, 15, 0}};
        const float d = eng::length(o->position - at);
        if (d > w.blast_radius) continue;
        bool reached = !map_;
        for (int k = 0; k < 3 && !reached; ++k) reached = !map_->collision.trace_ray(at + Vec3{0, 10, 0}, parts[k]).hit();
        if (!reached) continue;
        const float amount = w.damage * std::clamp(1.0f - d / std::max(1.0f, w.blast_radius), 0.1f, 1.0f);
        apply_damage(by, *o, int(std::lround(amount)), HitZone::Chest, w.id, true, now);
    }
}

// A bullet the shooter's game reported as hitting nobody, followed on through what it can pass
// (Game/Ballistics.hpp): the soldiers the shooter was not told of are tried, the nearest along the
// line taken, and the wall takes its share of the damage. Without this a wall could only be shot
// through at somebody already seen.
void Match::blind_wall_shot(Player& by, const WeaponDef& w, const Vec3& origin, const Vec3& end, double now) {
    const float limit = penetration_cm(w);
    if (!map_ || limit <= 0) return;
    const Vec3 way = end - origin;
    if (eng::length(way) < 1.0f) return;
    const Vec3 dir = eng::normalize(way);
    Player* best = nullptr;
    float best_t = w.range;
    HitZone best_zone = HitZone::None;
    for (auto& [id, o] : players_) {
        if (id == by.id || o.left || !o.alive || !enemies(by, o) || tells(by, o, now)) continue;
        float t = 0;
        HitZone zone = HitZone::None;
        if (ray_soldier(origin, dir, o.position, o.flags, move_def_, best_t, t, zone)) best = &o, best_t = t, best_zone = zone;
    }
    if (!best) return;
    const float thick = wall_thickness(map_->collision, origin, origin + dir * best_t, limit);
    if (thick < 0) return;
    ++by.hits;
    const float amount = hit_damage(w, best_t, best_zone) * wall_damage_scale(thick, limit);
    apply_damage(by, *best, std::max(1, int(std::lround(amount))), best_zone, w.id, false, now, thick > 0 ? u16(kKillWall) : u16(0));
}

void Match::on_message(u32 peer_id, std::span<const u8> data, double now) {
    Player* me = player(peer_id);
    if (!me) return;
    switch (peek_id(data)) {
        case Msg::LoadDone: {
            if (me->loaded) break;
            me->loaded = true;
            if (Seat* s = room_.seat_of(peer_id)) s->state = phase_ == Phase::Loading ? SlotState::Loading : SlotState::Playing;
            if (phase_ == Phase::Live || phase_ == Phase::RoundOver) {
                // A late arrival: the round so far, and a place in it.
                MatchBegin mb;
                mb.tick = tick_;
                server_.send(peer_id, encode(mb));
                RoundStart rs;
                rs.round = round_;
                rs.seconds = float(std::max(0.0, round_ends_ - now));
                server_.send(peer_id, encode(rs));
                // Everyone already standing, so the newcomer sees them.
                for (auto& [id, other] : players_) {
                    if (other.left || !other.alive || id == peer_id) continue;
                    Spawn s;
                    s.player = id;
                    s.team = u8(other.team);
                    s.index = 0;
                    s.here = true;
                    s.at = other.position;
                    s.yaw = other.yaw;
                    s.health = u16(std::clamp(other.health, 0, 65535));
                    s.force = other.force;
                    s.loadout = other.loadout;
                    server_.send(peer_id, encode(s));
                }
                if (mode_.respawn || phase_ == Phase::RoundOver) me->respawn_at = now + 1.0;
                else me->respawn_at = -1;   // rounds: wait for the next one
                send_score();
                mode_dirty_ = true;
            }
            break;
        }
        case Msg::Input: {
            Input in;
            if (!decode(data, in)) break;
            me->last_input = now;
            // A soldier cannot outrun kMaxSpeed; a report that does is ignored (and the next
            // honest one carries on from wherever it says).
            if (eng::length(Vec3{in.velocity.x, 0, in.velocity.z}) > kMaxSpeed) break;
            // Further than anybody could have gone since the last one (with room for a late packet
            // and a fall): counted, and believed -- a teleport is a number for review, not a fight.
            if (me->alive && me->last_input_at > 0) {
                const double gap = std::min(1.0, now - me->last_input_at);
                if (eng::length(in.position - me->position) > float(kMaxSpeed * gap) * 1.6f + 250.0f) ++me->teleports;
            }
            me->last_input_at = now;
            me->aims.push_back({now, in.yaw, in.pitch});
            while (!me->aims.empty() && now - me->aims.front().t > 1.0) me->aims.pop_front();
            me->position = in.position;
            me->velocity = in.velocity;
            me->yaw = in.yaw;
            me->pitch = in.pitch;
            me->flags = in.flags;
            me->hand = u8(std::min<size_t>(in.weapon_slot, kLoadoutSlots - 1));
            me->buttons = in.buttons;
            track_fall(*me, in.position, (in.flags & kFlagOnGround) != 0, (in.flags & kFlagOnLadder) != 0, now);
            if (!(in.buttons & kButtonUse)) me->use_from = 0;
            else if (me->use_from == 0) me->use_from = now;
            break;
        }
        case Msg::UseSkill: {
            UseSkill m;
            if (decode(data, m) && me->alive && phase_ == Phase::Live) on_skill(*me, m, now);
            break;
        }
        case Msg::PickClass: {
            PickClass m;
            if (decode(data, m)) on_pick(*me, m, now);
            break;
        }
        case Msg::CannonUse: {
            CannonUse m;
            if (decode(data, m) && me->alive && phase_ == Phase::Live) on_cannon(*me, m, now);
            break;
        }
        case Msg::UseHorrorItem: {
            UseHorrorItem m;
            if (decode(data, m) && me->alive && phase_ == Phase::Live) on_horror_item(*me, m.item, now);
            break;
        }
        case Msg::DropWeapon: {
            DropWeapon m;
            if (decode(data, m) && me->alive && phase_ == Phase::Live && m.slot <= 1 && !undead(*me)) drop_weapon(*me, m.slot, m.clip, m.reserve, me->position, now);
            break;
        }
        case Msg::PickUpWeapon: {
            PickUpWeapon m;
            if (decode(data, m) && me->alive && phase_ == Phase::Live && !undead(*me)) pick_up(*me, m, now);
            break;
        }
        case Msg::Shoot: {
            Shoot s;
            if (!decode(data, s) || phase_ != Phase::Live) break;
            const WeaponDef* w = weapon(s.weapon);
            if (!w) break;
            const bool grenade = w->klass == WeaponClass::Grenade;
            if (grenade) {
                // A grenade going off: believed only for one this soldier threw (Msg::Throw), within
                // a throw's reach of where it left the hand -- and it still counts when the thrower
                // has died since. Who it hurts is decided here (grenade_blast).
                auto it = std::find_if(me->thrown.begin(), me->thrown.end(), [&](const Player::Thrown& t) { return t.weapon == s.weapon; });
                if (it == me->thrown.end()) break;
                const Player::Thrown thrown = *it;
                me->thrown.erase(it);
                if (now - thrown.at < 0.3 || now - thrown.at > 10.0 || eng::length(s.end - thrown.from) > 4500.0f) break;
                me->protected_until = 0;
                grenade_blast(*me, *w, s.end, now);
                break;
            }
            if (!me->alive) break;
            bool carried = false;
            for (u16 id : me->loadout) carried |= id == s.weapon;
            if (!carried) break;
            const int slot = int(w->slot);
            const double min_gap = 60.0 / std::max(1.0f, w->rpm) * 0.7;
            if (now - me->last_shot[slot] < min_gap) break;
            me->last_shot[slot] = now;
            me->protected_until = 0;   // Team Slayer: the first shot ends being unbeatable
            // Everyone else who could see or hear it sees the shot (tracer, flash, sound).
            me->fired_at = now;
            ++me->shots;
            for (size_t c = 0; c < kLoadoutSlots; ++c)
                if (me->own_loadout[c] == s.weapon) {
                    ++me->fired[c];
                    break;
                }
            ShotFx fx;
            fx.shooter = peer_id;
            fx.weapon = s.weapon;
            fx.origin = s.origin;
            fx.end = s.end;
            send_shot(*me, fx);
            // Damage, pellet by pellet, each hit checked.
            const float limit = penetration_cm(*w);
            const Vec3 eye = me->position + Vec3{0, eye_height_by_flags(move_def_, me->flags), 0};
            struct Hurt {
                int amount = 0;
                HitZone zone = HitZone::None;
                u16 how = 0;
            };
            std::map<u32, Hurt> total;
            // A hit a pellet, as the shooter's game sends them: a bullet's one, a shotgun's one each.
            // More is a report made up (the same soldier listed sixteen times for one rifle shot).
            const size_t most = std::max<size_t>(1, w->pellets);
            if (s.hits.size() > most) ++me->blocked;
            for (const ShotHit& h : std::span(s.hits).first(std::min(s.hits.size(), most))) {
                Player* victim = player(h.victim);
                if (!victim || !victim->alive || !enemies(*me, *victim)) continue;
                const float dist = eng::length(victim->position - me->position);
                const float reach = w->klass == WeaponClass::Knife ? w->melee_range * 1.6f + 120.0f : w->range * 1.25f + 300.0f;
                if (dist > reach) continue;
                // In sight (or seen a moment ago: the lag's benefit), a plain hit. Through a wall the
                // bullet can pass (Game/Ballistics.hpp: measured here, to where the shooter's game put
                // the hit when that is on the victim), a wall shot: the wall takes its share. Through
                // anything more, not believed -- the shooter's game was never told where they were.
                float scale = 1.0f;
                u16 how = 0;
                if (map_) {
                    const float hh = hull_height_by_flags(move_def_, victim->flags);
                    Vec3 point = h.point;
                    if (eng::length(point - (victim->position + Vec3{0, hh * 0.5f, 0})) > 160.0f) point = victim->position + Vec3{0, hh * 0.6f, 0};
                    const bool told = me->sees.contains(victim->id) && me->sees[victim->id] > now;
                    const bool open = !h.wall && (line_of_sight(*me, *victim) || told);
                    if (!open) {
                        const float thick = limit > 0 ? wall_thickness(map_->collision, eye, point, limit) : -1.0f;
                        if (thick < 0 && !(h.wall && told && line_of_sight(*me, *victim))) {
                            ++me->blocked;
                            continue;
                        }
                        if (thick > 0) scale = wall_damage_scale(thick, limit), how = kKillWall;
                    }
                } else if (h.wall) {
                    // No map here to measure with: the shooter's own measure, within the weapon's.
                    if (limit <= 0 || float(h.wall) > limit) continue;
                    scale = wall_damage_scale(float(h.wall), limit), how = kKillWall;
                }
                ++me->hits;
                // A head shot a moment after a swing of thirty degrees or more: a flick.
                if (HitZone(h.zone) == HitZone::Head && !me->aims.empty()) {
                    const Player::Aim& then = me->aims.front();
                    const float swing = std::fabs(eng::wrap_degrees(me->yaw - then.yaw)) + std::fabs(me->pitch - then.pitch);
                    if (now - then.t <= 0.25 + 1e-3 && swing >= 30.0f) ++me->flicks;
                }
                const HitZone zone = taken_zone(*victim, HitZone(h.zone));
                Hurt& t = total[h.victim];
                t.amount += std::max(1, int(std::lround(hit_damage(*w, dist, zone) * scale)));
                if (t.zone == HitZone::None || zone == HitZone::Head) t.zone = zone;
                t.how |= how;
            }
            for (auto& [vid, hurt] : total)
                if (Player* victim = player(vid)) apply_damage(*me, *victim, hurt.amount, hurt.zone, s.weapon, false, now, hurt.how);
            // Nobody reported hit: the bullet is followed through the walls it can pass, for a
            // soldier the shooter was never told of.
            if (s.hits.empty() && w->pellets <= 1 && eng::length(s.end - s.origin) > 1.0f) blind_wall_shot(*me, *w, eye, eye + (s.end - s.origin), now);
            break;
        }
        case Msg::Throw: {
            Throw t;
            if (!decode(data, t) || !me->alive) break;
            // Remembered until it goes off (Msg::Shoot with a grenade): a blast is believed for these.
            if (const WeaponDef* tw = weapon(t.weapon); tw && tw->klass == WeaponClass::Grenade) {
                bool carried = false;
                for (u16 id : me->loadout) carried |= id == t.weapon;
                if (!carried) break;
                std::erase_if(me->thrown, [&](const Player::Thrown& o) { return now - o.at > 10.0; });
                if (me->thrown.size() >= 4) me->thrown.erase(me->thrown.begin());
                me->thrown.push_back({t.weapon, now, t.origin});
            }
            GrenadeFx fx;
            fx.thrower = peer_id;
            fx.weapon = t.weapon;
            fx.origin = t.origin;
            fx.velocity = t.velocity;
            to_all(encode(fx), true, peer_id);
            break;
        }
        case Msg::Spray: {
            // Your insignia on the wall in front of you: once a life, close enough to reach it.
            Spray m;
            if (!decode(data, m) || !me->alive || phase_ != Phase::Live || !me->spray || me->sprayed) break;
            const Vec3 eye = me->position + Vec3{0, eye_height_by_flags(move_def_, me->flags), 0};
            const float n = eng::length(m.normal);
            if (eng::length(m.at - eye) > kSprayReach + 90.0f || n < 0.5f || n > 1.5f) break;
            me->sprayed = true;
            SprayFx fx;
            fx.player = peer_id;
            fx.spray = me->spray;
            fx.at = m.at;
            fx.normal = m.normal * (1.0f / n);
            to_all(encode(fx));
            break;
        }
        case Msg::Radio: {
            Radio r;
            if (!decode(data, r) || r.line >= radio_count(r.group)) break;
            // The words to the team's chat, and the voice to the team's ears (the speaker's too).
            ChatLine line;
            line.scope = u8(ChatScope::Team);
            line.from = me->name;
            line.team = u8(me->team);
            line.text = std::string("(Radio) ") + radio_line(r.group, r.line);
            RadioFx fx;
            fx.speaker = peer_id;
            fx.group = r.group;
            fx.line = r.line;
            const auto msg = encode(line), voice = encode(fx);
            for (auto& [id, p] : players_)
                if (!p.left && (!mode_.teams || p.team == me->team)) server_.send(id, msg), server_.send(id, voice);
            record(voice);
            break;
        }
        case Msg::LeaveMatch: {
            // Back to the waiting room while the others play on.
            leave(peer_id);
            if (Seat* s = room_.seat_of(peer_id)) s->state = SlotState::Wait;
            break;
        }
        default:
            break;
    }
}

// ── Anti-cheat ────────────────────────────────────────────────────────────────

float Match::hit_damage(const WeaponDef& w, float dist, HitZone zone) const {
    float amount = w.damage;
    if (zone == HitZone::Head) amount *= w.head_multiplier;
    else if (zone == HitZone::Legs) amount *= w.leg_multiplier;
    else if (zone == HitZone::Arms) amount *= 0.9f;
    if (dist > w.falloff_start && w.range > w.falloff_start)
        amount *= eng::lerpf(1.0f, w.falloff_min, std::clamp((dist - w.falloff_start) / (w.range - w.falloff_start), 0.0f, 1.0f));
    return amount;
}

// Whether an eye could see any part of a soldier: their head, chest, feet and both shoulders, from
// the eye and from a step to either side of it (the peek), through the map less its glass.
bool Match::line_of_sight(const Player& from, const Player& to) const {
    if (!map_) return true;
    const Vec3 eye = from.position + Vec3{0, eye_height_by_flags(move_def_, from.flags), 0};
    const float h = hull_height_by_flags(move_def_, to.flags);
    const Vec3 d = to.position - from.position;
    Vec3 side = eng::normalize(Vec3{-d.z, 0, d.x});
    if (eng::length_sq(side) < 1e-6f) side = {1, 0, 0};
    const Vec3 points[5] = {to.position + Vec3{0, h - 12, 0}, to.position + Vec3{0, h * 0.65f, 0}, to.position + Vec3{0, 20, 0},
                            to.position + Vec3{0, h * 0.7f, 0} + side * 24.0f, to.position + Vec3{0, h * 0.7f, 0} - side * 24.0f};
    const Vec3 eyes[3] = {eye, eye + side * 30.0f, eye - side * 30.0f};
    for (const Vec3& e : eyes) {
        // An eye stepped into a wall sees nothing.
        if (&e != &eyes[0] && map_->sight.trace_ray(eye, e).hit()) continue;
        for (const Vec3& pt : points) {
            const eng::TraceResult tr = map_->sight.trace_ray(e, pt);
            if (!tr.hit() || eng::length(tr.end - pt) < 15.0f) return true;
        }
    }
    return false;
}

// Who sees whom, twenty times a second: close by, heard firing, or in sight -- and then for a
// second more, so somebody stepping out from a corner is not late on everyone's screen.
void Match::update_vision(double now) {
    if (!map_) return;
    for (auto& [vid, v] : players_) {
        if (v.left || !v.loaded || !v.alive || v.team == Team::Observer) continue;
        for (auto& [tid, t] : players_) {
            if (tid == vid || t.left || !t.alive || !enemies(v, t)) continue;
            const float d = eng::length(t.position - v.position);
            const bool seen = d < 600.0f || (now - t.fired_at < 1.5 && d < 3000.0f) || line_of_sight(v, t);
            if (seen) v.sees[tid] = now + 1.0;
        }
    }
}

bool Match::tells(const Player& viewer, const Player& target, double now) const {
    if (!map_ || viewer.id == target.id || viewer.team == Team::Observer || !target.alive || !enemies(viewer, target)) return true;
    if (mode_tells(viewer, target, now)) return true;   // a captain everyone is shown; a Search
    auto sees = [&](const Player& who) {
        auto it = who.sees.find(target.id);
        return it != who.sees.end() && it->second > now;
    };
    if (viewer.alive) return sees(viewer);
    // Down: what the side still standing sees (the spectator's view); in a game without sides, all.
    if (!mode_.teams) return true;
    for (const auto& [id, mate] : players_)
        if (!mate.left && mate.alive && mate.team == viewer.team && sees(mate)) return true;
    return false;
}

// Everyone, as the recording keeps it; and each soldier's own copy, the enemies they could not see
// cut down to the fact that they are alive, and no enemy's health.
void Match::send_snapshot(double now) {
    update_vision(now);
    Snapshot snap;
    snap.tick = tick_;
    snap.round_time = float(std::max(0.0, round_ends_ - now));
    for (auto& [id, p] : players_) {
        if (p.left || !p.loaded) continue;
        PlayerSnap ps;
        ps.id = id;
        ps.position = p.position;
        ps.velocity = p.velocity;
        ps.yaw = p.yaw;
        ps.pitch = p.pitch;
        ps.flags = u16((p.flags & ~(kFlagAlive | kFlagHidden)) | (p.alive ? kFlagAlive : 0));
        ps.health = u16(std::clamp(p.health, 0, 65535));
        ps.weapon = p.loadout[std::min<size_t>(p.hand, kLoadoutSlots - 1)];
        snap.players.push_back(ps);
    }
    record(encode(snap));
    for (auto& [vid, v] : players_) {
        if (v.left || v.bot) continue;
        if (!map_) {
            server_.send(vid, encode(snap), false);
            continue;
        }
        Snapshot mine = snap;
        for (PlayerSnap& ps : mine.players) {
            const Player& t = players_.at(ps.id);
            if (!enemies(v, t) || v.team == Team::Observer) continue;
            ps.health = u16(t.alive ? 100 : 0);
            if (tells(v, t, now)) continue;
            ps.flags = u16(kFlagHidden | (t.alive ? kFlagAlive : 0));
            ps.position = {};
            ps.velocity = {};
            ps.yaw = ps.pitch = 0;
            ps.weapon = kNoWeapon;
        }
        server_.send(vid, encode(mine), false);
    }
}

// A shot is heard further than it is seen: told to whoever could see the shooter, or was within
// earshot of them (30 m).
void Match::send_shot(const Player& shooter, const ShotFx& fx) {
    const auto msg = encode(fx);
    record(msg);
    const double now = server_.now();
    for (auto& [id, v] : players_) {
        if (v.left || v.bot || id == shooter.id) continue;
        const float d = eng::length(v.position - shooter.position);
        if (!tells(v, shooter, now) && d > kShotEarshot) continue;
        server_.send(id, msg, false);
    }
}

// On the way out (leaving early does not dodge it), a soldier's match is read for numbers no player
// makes; anything over the server's thresholds is filed as a report from "System", pointing at the
// match's recording, and staff online hear of it.
void Match::review_outliers(Player& p) {
    if (p.bot || p.reviewed) return;
    p.reviewed = true;
    Peer* peer = server_.peer(p.id);
    if (!peer || !peer->account || settings_.mode == Mode::Training) return;
    const ServerOptions& o = server_.options();
    std::string why;
    auto add = [&](const std::string& s) { why += (why.empty() ? "" : "; ") + s; };
    if (p.kills >= o.outlier_min_kills && float(p.headshots) >= o.outlier_headshot_share * float(p.kills))
        add(eng::str::format("%u of %u kills were head shots", unsigned(p.headshots), unsigned(p.kills)));
    if (p.shots >= 60 && float(p.hits) >= o.outlier_accuracy * float(p.shots)) add(eng::str::format("hit %u of %u shots", p.hits, p.shots));
    if (p.flicks >= o.outlier_flicks) add(eng::str::format("%u head shots a moment after a 30-degree swing", p.flicks));
    if (p.teleports >= o.outlier_teleports) add(eng::str::format("moved further than possible %u times", p.teleports));
    if (p.blocked >= o.outlier_blocked) add(eng::str::format("%u hits on soldiers out of sight", p.blocked));
    if (why.empty()) return;
    Account& a = *peer->account;
    server_.accounts().add_report(nullptr, a, ReportReason::Cheating, "Automatic: " + why, id_);
    LOG_WARN("anticheat: %s (#%llu) in match %u: %s", a.code_name.c_str(), (unsigned long long)a.id, id_, why.c_str());
    server_.staff_notice("Automatic report: " + a.code_name + " (" + why + "). F9 to review.");
}

// ── Guns on the floor ─────────────────────────────────────────────────────────

void Match::drop_weapon(Player& p, int slot, u16 clip, u16 reserve, const Vec3& at, double now) {
    const u16 id = p.loadout[size_t(slot)];
    const WeaponDef* w = weapon(id);
    if (!w || (w->slot != Slot::Primary && w->slot != Slot::Secondary)) return;
    Obj o;
    o.kind = Objective::Weapon;
    o.who = id;
    o.at = at;
    // Let go in the air (a jump, a fall): it lands on the floor below.
    if (map_)
        if (const eng::TraceResult tr = map_->collision.trace_ray(at + Vec3{0, 30, 0}, at - Vec3{0, 3000, 0}); tr.hit() && !tr.start_solid) o.at = tr.end;
    o.radius = kPickUpReach;
    o.since = now;
    o.until = now + kWeaponLies;
    // The rounds said to be in it, no more than it holds.
    o.clip = std::min<u16>(clip, w->magazine);
    o.reserve = std::min<u16>(reserve, u16(std::min<int>(w->reserve + w->magazine * 2, 999)));
    objs_.push_back(o);
    // Too many: the oldest goes.
    int lying = 0;
    for (const Obj& x : objs_) lying += x.kind == Objective::Weapon;
    if (lying > kWeaponsLyingMax)
        if (auto it = std::find_if(objs_.begin(), objs_.end(), [](const Obj& x) { return x.kind == Objective::Weapon; }); it != objs_.end()) objs_.erase(it);
    p.loadout[size_t(slot)] = kNoWeapon;
    Rearm r;
    r.player = p.id;
    r.slot = u8(slot);
    to_all(encode(r));
    mode_dirty_ = true;
}

void Match::pick_up(Player& p, const PickUpWeapon& m, double now) {
    // The nearest gun within reach of his feet.
    auto best = objs_.end();
    float best_d = kPickUpReach;
    for (auto it = objs_.begin(); it != objs_.end(); ++it)
        if (it->kind == Objective::Weapon)
            if (const float d = eng::length(it->at - p.position); d <= best_d) best = it, best_d = d;
    if (best == objs_.end()) return;
    const Obj taken = *best;
    const WeaponDef* w = weapon(u16(taken.who));
    objs_.erase(best);
    if (!w) return;
    const int slot = w->slot == Slot::Primary ? 0 : 1;
    // His own in that slot goes down where the other lay.
    if (p.loadout[size_t(slot)] != kNoWeapon) drop_weapon(p, slot, m.clip[size_t(slot)], m.reserve[size_t(slot)], taken.at, now);
    p.loadout[size_t(slot)] = u16(taken.who);
    Rearm r;
    r.player = p.id;
    r.slot = u8(slot);
    r.weapon = u16(taken.who);
    r.clip = taken.clip, r.reserve = taken.reserve;
    to_all(encode(r));
    mode_dirty_ = true;
}

}  // namespace lsfs
