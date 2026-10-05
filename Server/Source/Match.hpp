// One room's game, from loading to the results: rounds and respawns by mode, spawns, the
// 20 Hz snapshot, checked shots, damage, kills, scores and pay. Each game type's own rules (the
// missions, captains, the undead, strongholds ...) are in Server/Source/Modes.cpp.
//
// Authority is split the way Soldier Front split it: a client moves its own soldier and traces
// its own shots against the map, and the server checks what it is told (speed, fire rate,
// range, who is alive, which side) and owns health, kills, rounds and rewards.
#pragma once

#include "ServerOnly.hpp"

#include "Server.hpp"

#include "Bots.hpp"
#include "Maps.hpp"

#include "Game/Modes.hpp"
#include "Game/Movement.hpp"
#include "Game/Replay.hpp"

#include <deque>

#include <map>
#include <random>

namespace lsfs {

using proto::HitZone;
using proto::ShotFx;

class Match {
public:
    // `map`: the map the game is played on (a room's random choice drawn, MapCache::pick_map).
    Match(Server& server, Room& room, const std::string& map);
    ~Match();
    u32 id() const { return id_; }

    void begin_loading(double now);
    void tick(double now);
    void on_message(u32 peer, std::span<const u8> data, double now);
    void join(u32 peer);    // mid-game (free join)
    // Tests (Server::test_bots_before): every bot stands still `distance` in front of this soldier, never firing.
    void test_bots_before(u32 peer, float distance);
    // ... and side to side where they stand: 1 crouched, 2 running, 0 still again.
    void test_bots_pace(u8 pace);
    // Out of the match. `deserted`: he went of his own accord (left, dropped) before the end: a
    // forfeit on his record and his guns' wear for it; put out by a vote or the host, not.
    void leave(u32 peer, bool deserted = true);
    bool done() const { return phase_ == Phase::Done; }

private:
    enum class Phase { Loading, Live, RoundOver, Over, Done };

    struct Player {
        u32 id = 0;
        u64 account = 0;                    // the TV account (0: a bot)
        std::string name;
        Team team = Team::Red;
        u8 force = 0;
        Loadout loadout = kNoLoadout;       // carried now
        Loadout own_loadout = kNoLoadout;   // the account's (a zombie's claws are not)
        bool loaded = false;
        bool alive = false;
        bool left = false;
        int health = 0;
        eng::Vec3 position, velocity;
        float yaw = 0, pitch = 0;
        u16 flags = 0;
        u8 slot = 0;   // the seat (a side's spawn order, its turn); never the weapon in hand
        u8 hand = 0;   // the kit's cell in hand (Input::weapon_slot)
        u16 kills = 0, deaths = 0, assists = 0, score = 0, headshots = 0;
        // Of those, the ones on bots and by bots (PR-11: never counted toward anything universal).
        u16 bot_kills = 0, bot_deaths = 0;
        // For the day's quests (Rewards.cpp): kills by the weapon's class, and multi kills.
        std::array<u16, size_t(WeaponClass::Count)> class_kills{};
        u16 multi_kills = 0;
        // For the ID card (Accounts.hpp): teammates killed, objectives done, rounds played and lived
        // through; and for a gun's wear (Game/Wear.hpp), rounds fired from each slot.
        u16 team_kills = 0, missions = 0, rounds = 0, survived = 0;
        std::array<u32, kLoadoutSlots> fired{};
        // A fall (track_fall): in the air since leaving the ground, and the highest the feet were.
        bool airborne = false;
        float air_top = 0;
        double respawn_at = -1;
        double last_input = 0;
        double last_shot[4] = {-10, -10, -10, -10};
        double spawned_at = 0;
        double last_kill_at = -10;
        int streak = 0;
        u32 last_killer = 0;   // who killed this soldier last: killing them back is revenge
        // The round's challenges (Rules.hpp Special): this round's kills, those with the sidearm, a
        // knife kill made, who brought him down this round and when; and what they have paid.
        u16 round_kills = 0, round_sidearm = 0;
        bool round_knife = false;
        u32 round_killer = 0;
        double killed_at = -100;
        u16 special_sp = 0;
        // What the account brings (Game/Items.hpp): the parts worn for this force, the name's colour,
        // and the boosts that change the match (read once, when the match starts or they join).
        std::vector<u16> parts;
        u8 name_colour = 0;
        u32 shown_xp = 0;
        bool holy_bless = false, prevent_team_kill = false;
        // The spray he carries (Game/Shop.hpp) and whether this life has used it.
        u16 spray = kNoSpray;
        // The set he wears whole, and what marks his row on the Tab board (Game/Items.hpp).
        ItemSet set = ItemSet::None;
        u16 marks = 0;
        bool sprayed = false;
        std::map<u32, int> damaged_by;
        u8 spawns = 0;
        // The game type's (Server/Modes.cpp).
        MatchRole role = MatchRole::None;
        Undead undead = Undead::None;
        Undead next_class = Undead::None;   // Horror Mode 2: the class picked for the next life
        bool picking = false;               // Horror Mode 2: the select window is up
        double pick_until = 0;
        u8 rank = 1;                        // an undead's evolution (zrank_1..3)
        u16 infections = 0;                 // humans this undead turned
        int health_max = kMaxHealth;
        std::array<double, 5> skill_ready{};   // when each of the class's skills may be used again
        bool self_bombed = false;
        double speed_until = 0, jump_until = 0, search_until = 0;
        double protected_until = 0;         // Team Slayer: unbeatable until then, or the first shot
        double power_until = 0;             // Horror Mode 2's supply: harder hits until then
        double leech_until = 0;             // Blood Sucking: his claws give him the health they take
        double cleanse_until = 0;           // a Blind Cleanse: fog and smoke do not blind him
        float power = 1.0f;
        int rage = 0;                       // Team Slayer: deaths without a kill
        double rage_until = 0;
        std::map<u32, std::array<u16, 2>> vs;   // Team Slayer: [0] kills of, [1] deaths to, each soldier
        std::array<u16, 3> extra{};         // the score tab's own columns (ModeInfo::columns)
        u16 points = 0;                     // Pirate Mode: one's own points (the grade)
        u16 round_wins = 0;                 // Horror: rounds ended on the side that won
        u16 buttons = 0;                    // the last Input's
        double use_from = 0;                // Use held since (0: not held)
        eng::Vec3 died_at;                       // where he fell (an infected rises there)
        float died_yaw = 0;
        bool rise_here = false;             // the next spawn is at died_at (Horror: infected, a host turning)
        // A bot (Server/Bots.cpp): no peer; its brain, and its soldier moved here on the server.
        std::shared_ptr<BotBrain> bot;
        MoveState move;
        // Sight (update_vision): whom this soldier is told of, until when; and their last shot.
        std::map<u32, double> sees;
        double fired_at = -10;
        // What this match saw of them, for review_outliers.
        struct Aim {
            double t;
            float yaw, pitch;
        };
        std::deque<Aim> aims;
        u32 shots = 0, hits = 0, flicks = 0, teleports = 0, blocked = 0;
        u16 wall_kills = 0;
        // Grenades in the air: thrown, not yet gone off (a blast is believed only for one of these,
        // and still counts when its thrower has died since).
        struct Thrown {
            u16 weapon = kNoWeapon;
            double at = 0;
            eng::Vec3 from;
        };
        std::vector<Thrown> thrown;
        double last_input_at = 0;
        bool reviewed = false;
        // Horror Mode's items spent here: taken from the universal record by TVAS, with the report.
        std::array<u16, kHorrorItems> horror_used{};
    };
    // Someone who left before the end (PR-3: the report names them too): their forfeit and their guns'
    // penalty wear (Docs/Gaps.md), or nothing when they were put out.
    struct Departed {
        u8 team = 0;
        bool forfeit = false;
        std::map<std::string, int> wear;
        std::array<u16, kHorrorItems> horror_used{};
        u8 force = 0;
    };

    Player* player(u32 id);
    void start_round(double now);
    void spawn(Player& p, double now);
    void end_round(Team winner, proto::RoundReason reason, double now);
    void change_sides();
    void finish(double now);
    void send_score();
    // `grenade`: a blast (no head shots, Prevent Team Kill applies). `how`: KillFlags the kill carries
    // if this hit is the one (a wall shot, the bomb's blast).
    void apply_damage(Player& attacker, Player& victim, int amount, proto::HitZone zone, u16 weapon, bool grenade, double now, u16 how = 0);
    void kill(Player& attacker, Player& victim, u16 weapon, u16 flags, double now);
    // One of the round's challenges done: its SP for the match's end, a mission on the record, and
    // the soldier told.
    void award_special(Player& p, Special s);
    // A thrown grenade going off at `at`: everyone it reaches, the thrower too, walls in the way.
    void grenade_blast(Player& by, const WeaponDef& w, const eng::Vec3& at, double now);
    // A bullet nobody was reported hit by, followed through the walls it can pass: a soldier the
    // shooter was never told of may be standing behind one.
    void blind_wall_shot(Player& by, const WeaponDef& w, const eng::Vec3& origin, const eng::Vec3& end, double now);
    void check_round(double now);
    bool enemies(const Player& a, const Player& b) const;
    // Where a hit lands once the victim's force has had its say (Avoid Headshot: a head shot taken
    // to the chest, that often).
    HitZone taken_zone(const Player& victim, HitZone zone) const;
    void take_account(Player& p, const Account& a) const;
    // A soldier's feet as he reports them (or a bot's as moved here): a landing from higher than
    // kFallSafe hurts, less with parts that take falling damage off (Game/Items.hpp fall_damage).
    void track_fall(Player& p, const eng::Vec3& feet, bool on_ground, bool on_ladder, double now);
    // A gun's wear for this match (Game/Wear.hpp): a point, one for every kWearRounds fired, and
    // `extra` for leaving early -- told to TVAS with the report (PR-9), by the gun's code.
    std::map<std::string, int> wear_points(Player& p, int extra) const;
    // The match report (PR-3), sent through the server's TVAS link; MatchRewards follows its answer.
    void send_report(double now, int minutes);

    // ── The game types (Server/Modes.cpp) ──
    // An objective in play (proto::ObjectiveNow on the wire).
    struct Obj {
        Objective kind = Objective::Site;
        u8 state = 0;
        Team team = Team::None;
        u8 index = 0;
        u32 who = 0;
        eng::Vec3 at, home;
        eng::Vec3 stand;            // where a soldier stands to use it: floor walked to (bots head here)
        float radius = 0;
        float progress = 0;
        double until = 0;           // a fuse's end, a return, the next appearance
        double since = 0;
        Team taker = Team::None;    // the side taking a console or a stronghold
        u16 value = 0;              // a magazine's rounds; a supply's kind
        // A gun on the floor (Objective::Weapon, `who` its id): the rounds in it.
        u16 clip = 0, reserve = 0;
    };
    bool undead(const Player& p) const { return p.role == MatchRole::Host || p.role == MatchRole::Zombie; }
    bool horror() const { return settings_.mode == Mode::Horror || settings_.mode == Mode::Horror2; }
    void mode_begin(double now);                  // the match's first round is about to start
    void mode_round_start(double now);            // before anyone spawns into a round
    void mode_spawn(Player& p, proto::Spawn& s, double now);
    void mode_tick(double now);
    // After a kill is counted: points, captains, infection, rage, a dropped item; and who comes back when.
    void mode_kill(Player& attacker, Player& victim, u16 weapon, u16 flags, double now);
    // A hit as the game type has it (protection, rage, claws, power): what it comes to, 0 for none.
    int mode_damage(Player& attacker, Player& victim, int amount, u16 weapon, bool grenade, double now);
    void mode_time_up(double now);                // a round's clock ran out
    void mode_check(double now);                  // a round's objectives and eliminations
    bool mode_tells(const Player& viewer, const Player& target, double now) const;   // seen through walls
    void mode_end_round(Team winner, proto::RoundReason reason, double now);
    void mode_event(ModeEventKind kind, u32 who, Team team = Team::None, u8 value = 0, const eng::Vec3& at = {}, u32 other = 0);
    void send_mode_state(double now);
    void on_skill(Player& p, const proto::UseSkill& m, double now);
    void on_pick(Player& p, const proto::PickClass& m, double now);
    // Horror Mode's items (Game/Modes.hpp HorrorItem): one asked for, and one spent out of the
    // soldier's account (false: he has none), everyone told.
    void on_horror_item(Player& p, u8 item, double now);
    bool spend_horror_item(Player& p, HorrorItem item);
    // Pirate Mode's cannons (Game/Modes.hpp): one manned, left or fired; the balls flown and burst.
    struct Ball {
        eng::Vec3 pos, vel;
        u32 by = 0;
        Team team = Team::None;
        double born = 0;
    };
    void on_cannon(Player& p, const proto::CannonUse& m, double now);
    void tick_cannons(double now, float dt);
    void cannon_burst(const Ball& b, double now);
    void make_undead(Player& p, MatchRole role, Undead body, double now);
    void blast(Player& by, const eng::Vec3& at, float radius, int damage, u16 weapon, double now);   // a skill's or the bomb's
    void drop_item(Obj& item, const eng::Vec3& at, double now);
    Obj* find_obj(Objective kind, int index = -1);   // -1: the first of its kind, whatever its number
    int count_side(Team t, bool alive_only) const;
    eng::Vec3 zone_of(Team t, bool& found) const;
    // Bots and the game type: where a bot should be heading, and whether to hold Use there.
    bool bot_mode_goal(Player& p, eng::Vec3& goal, bool& use, double now);
    // Guns on the floor (proto::DropWeapon, PickUpWeapon): one put down where `at` is, and the one at
    // a soldier's feet taken up (his own in that slot put down in its place).
    void drop_weapon(Player& p, int slot, u16 clip, u16 reserve, const eng::Vec3& at, double now);
    void pick_up(Player& p, const proto::PickUpWeapon& m, double now);

    // Anti-cheat (TacticalFPS's Phase 5): each soldier is sent an enemy only while it could see or
    // hear them; a hit on somebody the shooter could not see is not believed; and on the way out a
    // soldier's match is read for numbers no player makes (a "System" report).
    void update_vision(double now);
    bool tells(const Player& viewer, const Player& target, double now) const;
    bool line_of_sight(const Player& from, const Player& to) const;
    void send_snapshot(double now);
    void send_shot(const Player& shooter, const ShotFx& fx);
    void review_outliers(Player& p);
    float hit_damage(const WeaponDef& w, float distance, HitZone zone) const;

    // Bots (Server/Bots.cpp).
    void add_bots();
    void bots_tick(double now);
    void bot_think(Player& p, float dt, double now);
    int bot_goal(Player& p, double now);
    void bot_fire(Player& p, double now);
    eng::Vec3 spawn_position(const Player& p, u8 index) const;
    // The nearest soldier along a ray (a bot's shot), boxes as the client's: head, chest, legs.
    bool trace_soldiers(const Player& shooter, const eng::Vec3& from, const eng::Vec3& dir, float max_dist, u32& victim, HitZone& zone,
                        float& dist) const;
    proto::MatchLoad load_message(u32 you) const;
    // Every soldier still in the match (but `except`) is sent `msg`, and the recording keeps it.
    void to_all(const std::vector<u8>& msg, bool reliable = true, u32 except = 0);
    void record(const std::vector<u8>& msg);
    void recording_info(bool live);

    Server& server_;
    Room& room_;
    RoomSettings settings_;
    ModeInfo mode_;
    Phase phase_ = Phase::Loading;
    std::map<u32, Player> players_;
    double phase_at_ = 0;
    double started_at_ = 0;
    double round_ends_ = 0;
    double last_snapshot_ = 0;
    double last_score_ = 0;
    u32 tick_ = 0;
    u8 round_ = 0;
    u16 red_ = 0, blue_ = 0;   // rounds won, or team points
    bool sides_changed_ = false;   // a Team Battle past its half: everyone on the other side from the room's
    u32 seed_ = 0;
    std::mt19937 rng_;

    // The game type's state (Server/Modes.cpp).
    std::vector<Obj> objs_;
    std::vector<Ball> balls_;   // Pirate Mode: cannon balls in the air
    u8 mode_phase_ = 0;
    double mode_phase_ends_ = 0;
    u16 escaped_ = 0, escape_needed_ = 0;
    double mode_sent_ = 0;
    bool mode_dirty_ = true;
    bool mode_ready_ = false;            // the map's pieces are laid out (map_ was read)
    std::vector<char> walked_;           // the map's cells walked to from an attacker's spawn (NavGraph::reachable_from)
    double next_treasure_ = 0, next_tick_ = 0;
    std::array<u8, 2> captain_turn_{};   // CTC: whose turn it is to be captain, each side
    double round_started_ = 0;
    bool round_first_kill_ = false;   // this round's first kill is made

    // The map as the server knows it (Maps.hpp): null until read, and for good without client data.
    std::shared_ptr<const ServerMap> map_;
    MovementDef move_def_;
    double bot_acc_ = 0;
    double bot_report_ = 0;
    double last_vision_ = 0;
    u32 next_bot_ = 0;

    // The match recording (Game/Replay.hpp): everything everyone was sent, for staff.
    u32 id_ = 0;
    replay::Writer recording_;
    double recording_from_ = 0;
    double recording_saved_ = 0;
    u64 started_unix_ = 0;
    // How it ended, for each soldier's own list of their matches (Server::RecordingInfo).
    bool finished_ = false;
    Team winner_ = Team::None;
    u64 winner_account_ = 0;
    bool report_waiting_ = false;
    bool abandoned_ = false;   // every soldier has left: it ends at the next tick
    std::map<u64, Departed> gone_;   // accounts that left before the end
};

}  // namespace lsfs
