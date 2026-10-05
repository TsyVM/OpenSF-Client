// The game types' shared rules: which maps each offers (from the map's world script), the
// missions' numbers, the roles a soldier can be given (a captain, a zombie, an undead class), the
// undead's skills, and what the server tells every client of a game's state (proto::ModeState).
// The server plays them (Server/Source/Modes.cpp); the client draws them (World/Objectives.cpp).
// Docs/Modes.md has each game type's rules and where each number comes from.
#pragma once

#include "Game/Navigation.hpp"
#include "Game/Rules.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sf {
struct Level;
}

namespace lsf {

// ── Maps ───────────────────────────────────────────────────────────────────────

// What a map holds for the game types, read from its world script (sf::Level's gameplay layer).
struct MapRules {
    std::string id;
    Mission mission = Mission::Elimination;   // Team Battle's: the objective and what is there to play it
    int sites = 0;                            // bomb sites (Bomb mission objects)
    bool item = false;                        // a thing to take (Take Back, Dual; the Silo's sample)
    bool red_zone = false, blue_zone = false; // EvacuationData for each side
    bool sniper = false;                      // a sniper map (ObjectiveType Sniper; Art Center)
    bool horror = false;                      // Horror Mode's own (nervegashorror, plasmahorror, shanghaihorror)
    bool horror2 = false;                     // Horror Mode 2's (undead and human spawns of their own)
    bool occupy = false;                      // the Silo's consoles
    bool pirate = false;                      // the Pirate Ship's strongholds
    bool deathmatch = false;                  // ObjectiveType Deathmatch (Factory, S.Center)
};
MapRules map_rules(const sf::Level& level);
// Where a soldier stands to reach something at `at`: the nearest cell within `radius` of it that
// `walked` (NavGraph::reachable_from a spawn) says he can walk to -- the nearest of all is often the
// top of the crate or rack it sits on. -1 when there is none.
int stand_cell(const NavGraph& nav, const eng::Vec3& at, const std::vector<char>& walked, float radius);
// How far from his chest a soldier must reach for an item resting at `item`, from that floor:
// kTakeReach at least -- an item on a table is that; the Silo's sample, on its rack 2.45 m above the
// floor, more. -1 when no floor near it is walked to.
float item_reach(const NavGraph& nav, const eng::Vec3& item, const std::vector<char>& walked);
// Whether a game type is played on a map: the original's lists (Docs/Modes.md, "Maps").
bool mode_offers(Mode mode, const MapRules& map);

// The lobby's two map choices that are not maps (MapName.txt 0 and 1): any map the game type
// offers, or one of the "hot" five. The server draws the map when the game starts.
inline constexpr std::string_view kAllRandom = "allrandom";
inline constexpr std::string_view kHotRandom = "hotrandom";
inline bool is_random_map(std::string_view id) { return id == kAllRandom || id == kHotRandom; }
// MapName.txt's Hot Random: "Shanghai, Missile, Desert camp, Satellite, Canyon".
inline constexpr const char* kHotMaps[] = {"shanghai", "missile", "desertcamp", "satellite", "canyon"};

// ── Missions (Team Battle) ─────────────────────────────────────────────────────

// Docs/Research.md §3: plant 5 s, defuse 7 s, 35 s fuse.
inline constexpr float kPlantSeconds = 5.0f;
inline constexpr float kDefuseSeconds = 7.0f;
inline constexpr float kFuseSeconds = 35.0f;
inline constexpr float kSiteReach = 250.0f;      // cm from a site's mark a bomb can be set
inline constexpr float kDefuseReach = 200.0f;    // cm from the bomb a defuser must be
inline constexpr float kBlastRadius = 1500.0f;   // cm the bomb's blast reaches
inline constexpr float kTakeReach = 150.0f;      // cm from a soldier's chest to an item he takes
inline constexpr float kChestHeight = 110.0f;    // cm up from the feet a reach is measured from
inline constexpr float kReturnSeconds = 3.0f;    // a defender's touch on a dropped item: home after this

// ── Roles ──────────────────────────────────────────────────────────────────────

// What the server made of a soldier for a round (proto::RoleNow).
enum class MatchRole : u8 {
    None = 0,
    Captain,     // CTC, Captain Mode: big head, more HP, marked
    Host,        // Horror: a host zombie (1500 HP)
    Zombie,      // Horror: infected; Horror Mode 2: an undead of a class
    Escaped,     // Team Battle's Flee: out, and counted
    Count
};

// The undead's bodies (force archive sf_c_z*): what an infected soldier and Horror Mode 2's
// classes look like. The original's select window also lists Cruel and Wraith; this client ships
// no model for either, so they are not offered.
enum class Undead : u8 { None = 0, Man, Woman, Boss, Driller, Heavy, Hunter, Count };
struct UndeadDef {
    const char* name;      // "Boss"
    const char* model;     // the force archive folder: "zboss"
    const char* motion;    // its clips' prefix and folder: "zboss" (motion/zboss/zboss_l_us)
    // The class card's three bars (ui_info_zombie_<class>.tga, measured 0..1): what the numbers below come from.
    float health_bar, power_bar, speed_bar;
    int health;            // Horror Mode 2's hit points (600 + 2,400 x the bar)
    int claw;              // a swing's damage (20 + 60 x the power bar)
    float speed;           // run speed against a soldier's (0.7 + 0.6 x the bar)
    // Up to five skills, on the keys of weapons 2, 3 and 4, reload and zoom (an undead has claws
    // and nothing else to hold). kSkillNone: none.
    u8 skills[5];
    u8 unlock_rank;        // the evolution rank it needs (zrank_1..3); 1: any
};

// The undead's skills (ui_text_skillname_*): what each does is in Docs/Modes.md.
enum class Skill : u8 {
    None = 0,
    Speed,      // Horror: a burst of speed (Horror 2 Driller's Dash)
    Jump,       // a leap twice as high
    Smoke,      // black fog at the feet
    Search,     // every human on the radar for a while
    SelfBomb,   // Horror: goes off, hurting the humans round about
    Crush,      // Boss: a leap that lands with a blow all round
    Drilling,   // Driller: a charge that strikes the first one met
    Throw,      // Heavy: a rock thrown that bursts
    Snatch,     // Hunter: pulls the human in front of him close
    Count
};
inline constexpr u8 kSkillNone = 0;
struct SkillDef {
    const char* name;      // "Super Jump"
    float cooldown;        // seconds
    float lasts;           // seconds it acts (0: at once)
};
const SkillDef& skill_def(Skill s);
const UndeadDef& undead_def(Undead u);
// Horror's infected look like the soldier they were (Mulan's a woman).
Undead infected_body(u8 force);

// Horror (Docs/Research.md §3): host zombies 1500 HP, infected 200, humans 250; one in four starts a host.
inline constexpr int kHostHealth = 1500;
inline constexpr int kInfectedHealth = 200;
inline constexpr int kHumanHealth = 250;
inline constexpr float kInfectionDelay = 20.0f;    // seconds before the hosts turn ("a rotten smell ...")
inline constexpr int kClawDamage = 50;             // Horror's zombie swing
inline constexpr float kClawReach = 170.0f;        // cm
// Horror Mode 2: the humans' and the undead's own; the undead come back, the humans do not.
inline constexpr int kHorror2Human = 100;
inline constexpr float kUndeadRespawn = 6.0f;
inline constexpr float kPickSeconds = 10.0f;       // the select window's bar before it picks for you
inline constexpr int kRank2Infections = 2, kRank3Infections = 5;   // zrank_2, zrank_3

// ── Captains ───────────────────────────────────────────────────────────────────

inline constexpr int kCtcCaptainHealth = 1000;   // CTC's briefing: "1,000 HP"
inline constexpr int kCaptainPoolPerSoldier = 500;   // Captain Mode: 4,000 HP a full side shares out
// Captain Mode's captains a side: one for every three soldiers or part of three.
inline int captains_for(int side) { return side <= 0 ? 0 : (side + 2) / 3; }

// ── Occupy, Pirate Mode ────────────────────────────────────────────────────────

inline constexpr float kConsoleSeconds = 10.0f;   // the Silo: a console taken
inline constexpr float kStrongholdSeconds = 8.0f; // the Pirate Ship: a neutral stronghold taken
inline constexpr float kStrongholdTick = 3.0f;    // a held stronghold's point, this often
inline constexpr float kTreasureEvery = 60.0f;    // a treasure box appears
inline constexpr float kTreasureSeconds = 3.0f;   // opened with Use held
inline constexpr int kTreasurePoints = 10;
// The end grade by points (inf/mode/piratemode/*_grade.tga).
const char* pirate_grade(int points);

// ── Team Slayer ────────────────────────────────────────────────────────────────

inline constexpr float kProtectSeconds = 3.0f;    // "unbeatable" after a spawn, until the first shot
inline constexpr int kRageDeaths = 3;             // deaths without a kill fill the gauge
inline constexpr float kRageSeconds = 20.0f;
inline constexpr float kRageDamage = 1.5f;
inline constexpr float kMagazineSeconds = 20.0f;  // a dropped magazine lies this long

// ── Guns on the floor (proto::DropWeapon, PickUpWeapon) ────────────────────────

inline constexpr float kWeaponLies = 30.0f;       // a gun put down lies this long
inline constexpr float kPickUpReach = 140.0f;     // cm from the feet to a gun taken up
inline constexpr int kWeaponsLyingMax = 24;       // the oldest goes past this

// ── The Pirate Ship's cannons (map_cannon_state.kst, cannon_state.kst) ─────────────────────────
//
// Six on the map the original numbered 41: two on the ship's deck, two on each shore, each pointing
// at the other side. The tables are in half-centimetres (a soldier is 357 of them tall): halved here,
// every one of the six stands on the map's own floor (`sfcheck <data> cannons`). A soldier beside
// one mans it with Use, fires it with the trigger, and leaves it with Use again or by walking off.
// The ball flies at the table's BaseSpeed and falls; where it lands it bursts as the table says:
// 75 within 37.5 cm, down to 20 at 3 m, nothing on the gunner's own side or on himself.
struct CannonSpot {
    eng::Vec3 at;        // its foot
    eng::Vec3 forward;   // the way it points
};
std::span<const CannonSpot> pirate_cannons(std::string_view map);   // none on any other map
inline constexpr float kCannonReach = 170.0f;     // cm from it to man it
inline constexpr float kCannonSpeed = 1550.0f;    // cm/s (BaseSpeed 3100)
inline constexpr float kCannonGravity = 400.0f;   // cm/s2: a shore's cannon reaches the deck
inline constexpr float kCannonReload = 3.0f;
inline constexpr float kCannonLife = 10.0f;       // BulletLifeTime 10000 ms
inline constexpr float kCannonBlast = 300.0f;     // DamageCoverageDistance 600
inline constexpr float kCannonFull = 37.5f;       // MaxDamageCoverageDistance 75
inline constexpr int kCannonDamageMax = 75, kCannonDamageMin = 20;
inline constexpr float kCannonUp = 80.0f, kCannonDown = 25.0f, kCannonSide = 90.0f;   // degrees it turns
inline constexpr float kCannonMuzzle = 80.0f;     // cm above its foot
// The aim a cannon allows nearest the one asked for, a ball's burst at a distance (0: out of it),
// and a ball `t` seconds after it left `origin` at `velocity` (until it meets something).
eng::Vec3 cannon_aim(const CannonSpot& c, const eng::Vec3& aim);
int cannon_damage(float distance);
eng::Vec3 cannon_ball_at(const eng::Vec3& origin, const eng::Vec3& velocity, float t);

// ── State on the wire ──────────────────────────────────────────────────────────

// An objective's kind (proto::ObjectiveNow::kind) and what its state means.
enum class Objective : u8 {
    Site = 0,      // a bomb site: state 0
    Bomb,          // the planted bomb: state 1 planting, 2 ticking, 3 defusing, 4 gone off, 5 defused
    Item,          // a thing to take: state 0 at home, 1 carried (who), 2 dropped, 3 brought home
    Zone,          // a side's ground (team)
    Console,       // Occupy: team the owner (Team::None neutral), progress, who takes it
    Stronghold,    // Pirate: as a console
    Supply,        // Horror Mode 2: state 0 waiting, 1 there
    Girl,          // Horror Mode 2: state 0 waiting, 1 lost, 2 rescued
    Treasure,      // Pirate: state 0 coming, 1 there, 2 opening (who)
    Magazine,      // Team Slayer: a magazine on the floor
    Weapon,        // a gun on the floor (proto::DropWeapon): `who` the weapon's id, `timer` how long it lies
    Cannon,        // Pirate: a cannon to man: state 0 free, 1 manned (who); `timer` its reload
    Count
};

// Something that happened (proto::ModeEvent::kind): announced, heard, drawn.
enum class ModeEventKind : u8 {
    Planting = 0, Planted, Defusing, Defused, Exploded, Taken, Dropped, Returned, Delivered, Escaped,
    NewCaptain, CaptainDown, Infected, HostsTurned, ConsoleTaken, StrongholdTaken, TreasureOpened, SupplyTaken,
    GirlRescued, SkillUsed, RageOn, MagazineTaken, ClassPicked, RankUp,
    SidesChanged,   // a Team Battle's half: every soldier is on the other side now
    Count
};

}  // namespace lsf
