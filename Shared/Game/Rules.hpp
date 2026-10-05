// Soldier Front's rules as the rewrite plays them: modes, room settings, channels, ranks,
// forces and the weapon roster. Both sides compile the same tables, so a room the client
// offers is one the server accepts. Sources are in Docs/Research.md (§2 flow, §3 modes).
#pragma once

#include "Engine/Core/Types.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lsf {

using eng::i32;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::u8;

// 2: the connection's ping probes (message flag bit 2), which a version 1 build would not skip.
// 3: a room's time of day (RoomSettings, RoomSummary) and the staff flag on the profile.
// 4: the radio's voice (RadioFx) and clans.
// 5: Soldier Front's items (Buy.offer, UseItem, the inventory's items), name colours and worn parts,
//    the Special Force and revenge kill flags.
// 6: Soldier Front's own movement and handling (Movement.hpp's speeds, weapon.kst / recoil.kst), so
//    every soldier in a room runs and spreads the same; the firing flag in Input and snapshots.
// 10: every game type (Team Slayer, Occupy, Horror Mode 2, Pirate Mode; the missions), ModeState and
//     ModeEvent, a mode's own two score columns, skills and an undead's class.
// The protocol both sides speak (PT-2): it must match exactly, in the transport's handshake and in
// Hello. Content is not part of it any more: what a server adds comes in its manifest (§11).
inline constexpr u32 kProtocolVersion = 25;   // 25: a run 8 % slower (708), every gun's cone and kick 5 % wider, the PSG-1's magazine of five; 24: a server's game types and maps switched off by its Game Masters (GamesState, GamesEdit); 23: a kit of six (three throwables), the original's crouch (141 cm, the eye at 127; tucked in the air 118), a run of 770, the sniper rifles' weapon.kst DAMAGE; 22: a faster run (840) and a 12 % higher jump (688), one zoom on every sniper rifle but the AWP, the cone closing through a burst and a steadier kick; 21: no weapon upgrades (attachments, enchanting, elements and what they carried); 20: a faster run (700) and a higher jump (650): client and server predict movement alike; 19: Team Vanilla accounts (Join with a ticket, ServerInfo, Ready), a server's manifest and its download, channels a server edits, the server list's query; 18: the faster run, walk and crouch, a ladder caught in the air (Movement.hpp), a shot's hits no more than its pellets; 17: a Team Battle's sides change at the half; 16: guns on the floor, falls, forfeits and team kills on the record, wear, mending, selling back (Docs/Gaps.md); 15: Fire, Cold and Wind's own stats (burning, chilled, the hit's element); 14: the shop's catalog (rentals), attachments, enchanting, capsules, sprays, your own matches' recordings; 13: room invitations, wall shots and the kill marks' flags; 12: friends, messages, global chat, clan ranks, WarRock retired; 11: ladders, the 16-bit soldier flags; 10: game types; 9: rewards and events; 8: a clan's own emblem; 7: staff ranks, reports, votes, match recordings
// The build: what a release is numbered (DS-5). Client and server of one release share it; TVAS
// tells servers the lowest it still lists. Packs have a format of their own (Game/Registry.hpp).
inline constexpr u32 kBuildNumber = 1;
// SFServer v2's game port (the original tree's GameServer had none fixed).
inline constexpr u16 kDefaultPort = 27240;
inline constexpr int kMaxRoomPlayers = 16;
inline constexpr int kTeamSlots = 8;
inline constexpr u32 kStartingSp = 60000;
inline constexpr u32 kSpRecharge = 10000;   // the SP recharge bar's pay-out
inline constexpr int kMaxHealth = 100;

// ── Modes ──────────────────────────────────────────────────────────────────────

// Every game type the original's waiting room offered (its score tabs inf/tab/base/*_tab, its
// loading titles and briefings load2/*): Docs/Modes.md has each one's rules and where they come
// from. The numbers are kept (rooms and recordings store them); new ones only go on the end.
enum class Mode : u8 {
    TeamBattle,         // the map's mission: blast, capture, escape or dual; rounds, no respawn
    TeamDeathmatch,     // first team to the point goal; respawn
    SingleBattle,       // everyone for themselves to a kill count; respawn anywhere
    Sniper,             // sniper rifles only; the first side to the kill goal; respawn
    CaptureTheCaptain,  // one big-headed 1000 HP captain a side: bring theirs down first; rounds, respawn
    Captain,            // several captains a side, seen by all, the side's HP shared out; rounds
    Horror,             // the host zombies spread the virus; the humans hold out; rounds
    Training,           // targets, no wear, no stats
    TeamSlayer,         // points with a streak's bonus, rage, the killer marked, magazines; respawn
    Occupy,             // the Silo: take both missile consoles, or the sample to one; rounds
    Horror2,            // Horror Mode 2: undead classes and their skills, supply boxes, the girl
    Pirate,             // the Pirate Ship: hold the three strongholds for points; treasure; respawn
    Count
};

// What a game's goal counts.
enum class GoalKind : u8 { None, RoundsToWin, RoundsPlayed, TeamPoints, TeamKills, PlayerKills };

struct ModeInfo {
    const char* name;         // "Team Battle"
    const char* short_name;   // "TB"
    bool teams;
    bool rounds;              // rounds to a win count, rather than one long game
    bool respawn;             // soldiers come back during the game (or the round)
    u8 goal_default, goal_min, goal_max, goal_step;
    const char* goal_label;   // "Rounds to win", "Kills", "Points"
    u8 minutes_default;       // per round (rounds) or per game
    GoalKind goal;
    // The score tab's own columns after Kill and Death (the original's tabs: Occupy's "Capture",
    // "Sample"; the Pirate's "Point", "Save"; Horror Mode 2's "Undead Kill", "Human Kill", "Save"):
    // ScoreRow::extra[0..2]. Null: the column is not shown.
    const char* columns[3];
};
const ModeInfo& mode_info(Mode mode);
inline const char* mode_name(Mode mode) { return mode_info(mode).name; }

// Team Battle's mission is the map's: the world script's ObjectiveType.
enum class Mission : u8 { Elimination, Blast, Capture, Escape, Dual, Count };
Mission mission_from_objective(std::string_view objective);
const char* mission_name(Mission m);
// What each side is told to do (the lobby's MapName.txt carries map-specific wording).
const char* mission_attack_text(Mission m);
const char* mission_defence_text(Mission m);
// A Team Battle with an attacker and a defender (a bomb to plant, a thing to take, a way out to
// reach) changes sides once, so each side attacks and defends: when this many rounds have been
// played (one short of the rounds a side needs: first to five changes after the fourth), every
// soldier goes over to the other side and the rounds each side has won go with its soldiers.
// 0: this game does not change sides (nothing to attack, or a game of one round).
int side_change_round(Mode mode, Mission mission, int goal);

enum class Team : u8 { Red = 0, Blue = 1, None = 2, Observer = 3 };
const char* team_name(Team t);

// The hour a room plays its map at. Soldier Front's maps are baked in one light each, so the
// other hour is a grade of that bake (Game/Render/WorldRenderer): Day on a day map and Night on
// a night one are drawn exactly as baked.
enum class TimeOfDay : u8 { Day = 0, Night = 1, Count };
const char* time_of_day_name(TimeOfDay t);   // "Day", "Night"
// The maps whose lightmaps were made for night (street lamps, a dark sky box): a new room on one
// opens at Night, and its Day is the bake with daylight added rather than one taken away.
bool baked_at_night(std::string_view level_id);
inline TimeOfDay baked_time_of_day(std::string_view level_id) { return baked_at_night(level_id) ? TimeOfDay::Night : TimeOfDay::Day; }

// What a room is: its host's choices on the Make Room card and the waiting room's five
// selectors (game type, win condition, time, 3rd person, join in progress), plus the hour.
struct RoomSettings {
    std::string title = "Let's play!";
    std::string password;
    Mode mode = Mode::TeamBattle;
    std::string map = "missile";   // level id (Shared/SF/Level.hpp)
    u8 max_players = 16;
    u8 goal = 5;                   // rounds to win / kills / points
    u8 minutes = 3;
    bool third_person = false;     // POV: Q switches to the 3rd-person camera
    bool observers = true;         // spectators may take the observer seats
    bool free_join = true;         // join a game in progress
    bool team_balance = true;
    TimeOfDay time_of_day = TimeOfDay::Day;
    // Server bots (Server/Bots.cpp) filling seats the soldiers leave empty: how many, and how well
    // they play (BotSkill). Training's are targets that never fire back.
    u8 bots = 0;
    u8 bot_skill = 1;
    // The original's "(no sniper)" game types (gametext 87-89: SB, TB, CB): nobody carries a sniper
    // rifle in; one in a kit stays at home and the issued rifle takes its slot.
    bool no_snipers = false;
    // A Clan Battle (gametext 86; a room made in a Clan War channel): the red side is one clan, the
    // blue another, and the game is a clan's to win. The server's to say, never the host's.
    bool clan_battle = false;
    std::string red_clan, blue_clan;

    template <typename S> void serialize(S& s) {
        s.str(title, 32);
        s.str(password, 16);
        u8 m = u8(mode);
        s.u8(m);
        mode = Mode(m < u8(Mode::Count) ? m : 0);
        s.str(map, 32);
        s.u8(max_players);
        s.u8(goal);
        s.u8(minutes);
        s.boolean(third_person);
        s.boolean(observers);
        s.boolean(free_join);
        s.boolean(team_balance);
        u8 t = u8(time_of_day);
        s.u8(t);
        time_of_day = TimeOfDay(t < u8(TimeOfDay::Count) ? t : 0);
        s.u8(bots), s.u8(bot_skill);
        s.boolean(no_snipers);
        s.boolean(clan_battle), s.str(red_clan, 24), s.str(blue_clan, 24);
    }
};
// Brings a room's settings within what the mode allows. Returns false when nothing could.
bool sanitize(RoomSettings& r);
// The game types that have a "(no sniper)" variant, the room's game type as its lists say it
// ("Team Deathmatch (no sniper)"), and the next one along when the host turns the selector: each
// game type, then its no-sniper variant where it has one.
bool takes_no_snipers(Mode mode);
std::string game_type_name(Mode mode, bool no_snipers, bool clan_battle = false);
inline std::string game_type_name(const RoomSettings& r) { return game_type_name(r.mode, r.no_snipers, r.clan_battle); }
// Clan Battle (gametext 86, 98, 756): the game types clans meet in (Team Battle: "Clan Battle";
// Team Deathmatch: "Clan Deathmatch"), how many of each clan a game needs ("3 members per each
// teams are required"), and what a soldier who plays one to its end earns his clan: for a win, a
// draw, and (a win only) what his parts add (Game/Items.hpp clan_point).
bool clan_battle_mode(Mode mode);
inline constexpr int kClanBattleMin = 3;
inline constexpr int kClanPointWin = 3, kClanPointDraw = 1;
void step_game_type(RoomSettings& r, int step, u16 allowed = 0xFFFF);

enum class BotSkill : u8 { Easy = 0, Normal = 1, Hard = 2, Target = 3 };
const char* bot_skill_name(BotSkill s);
// A bot's session id: bit 31 set, so nothing sent "to" one reaches anybody.
inline constexpr u32 kBotIdBase = 0x80000000u;
inline bool is_bot_id(u32 id) { return (id & kBotIdBase) != 0; }

// ── Clans ──────────────────────────────────────────────────────────────────────

// The shapes a clan's own emblem is made of (the emblem maker). The numbers are kept: an emblem
// is stored and sent as them, so new ones only ever go on the end.
enum class EmblemShape : u8 {
    Circle, Ring, HalfCircle, Quarter, Square, RoundedSquare, Frame, Triangle, RightTriangle, Trapezoid,
    Parallelogram, Diamond, Pentagon, Hexagon, Octagon, Star, Star4, Burst, Cross, Line,
    Chevron, Arrow, Crescent, Shield, Snowflake, Bolt, Heart, Crown, Gear, Drop,
    // Pictures (the client draws them with VanGUI's icons), placed and coloured as any shape.
    Skull, Flame, Bomb, Knife, Rifle, Sniper, Pistol, Shotgun, MachineGun, Grenade,
    C4, Crosshair, Scope, Explosion, Helmet, Vest, Medal, Trophy, Flag, Rocket,
    Globe, Sun, Moon, Cloud, Tree, Gem, Key, Eye, Mask, Compass,
    Count
};
inline constexpr u8 kEmblemFirstPicture = u8(EmblemShape::Skull);
const char* emblem_shape_name(u8 shape);

// One shape of an emblem. The emblem is a square 200 units across, its middle at (100, 100);
// a shape has its own middle, width and height in those units, is turned about its middle, and
// has one colour. Nothing is uploaded: an emblem is only ever these numbers.
struct EmblemLayer {
    u8 shape = 0;            // EmblemShape
    u8 x = 100, y = 100;     // its middle: 0..200 across and down
    u8 w = 100, h = 100;     // its width and height: 2..250
    u16 turn = 0;            // degrees clockwise, 0..359
    u8 r = 255, g = 255, b = 255;
    u8 a = 255;              // its opacity: 16 (faint) .. 255 (solid)
    u8 style = 0;            // bit 0: its outline only; bits 1..3: the outline's weight; bit 4: mirrored
    bool outline() const { return (style & 1) != 0; }
    int weight() const { return (style >> 1) & 7; }
    bool mirrored() const { return (style & 16) != 0; }
    template <typename S> void serialize(S& s) { s.u8(shape), s.u8(x), s.u8(y), s.u8(w), s.u8(h), s.u16(turn), s.u8(r), s.u8(g), s.u8(b), s.u8(a), s.u8(style); }
    bool operator==(const EmblemLayer&) const = default;
};
inline constexpr size_t kEmblemLayers = 24;

// A clan's mark. Soldier Front's own: one piece from each of the client's mark builder layers
// (SF/ClanMarks.hpp), numbered from 1; 0 leaves the frame or the symbol out. Or the clan's own
// emblem, made in the emblem maker: its shapes, drawn first to last; when it has any, they are
// the mark.
struct ClanMark {
    u8 background = 1, frame = 0, symbol = 1;
    std::vector<EmblemLayer> layers;
    bool own() const { return !layers.empty(); }
    template <typename S> void serialize(S& s) {
        s.u8(background), s.u8(frame), s.u8(symbol);
        s.list(layers, kEmblemLayers);
    }
    bool operator==(const ClanMark&) const = default;
};
// A mark as it may be kept: every number in its range, no more shapes than an emblem holds.
void sanitize_mark(ClanMark& mark);
// An emblem's shapes as a line of text (the server's accounts file), and back.
std::string emblem_text(const ClanMark& mark);
void emblem_from_text(std::string_view text, ClanMark& mark);
inline constexpr size_t kClanNameMin = 2, kClanNameMax = 12;

// A clan's ranks, lowest first. The owner founded it (or was handed it); a co-owner runs it beside
// them; lieutenants bring soldiers in and keep order; members and recruits (everyone starts as one)
// fight in it. Each may act only on those below them (clan_may).
enum class ClanRank : u8 { Recruit = 0, Member, Lieutenant, CoOwner, Owner };
const char* clan_rank_name(ClanRank r);   // "Co-Owner"
// What a member of rank `me` may do: to a member of rank `them` (kick, promote, demote) or at all
// (invite, answer applications, set the notice or the emblem, open or close the clan, hand it
// over, disband it).
enum class ClanPower : u8 { Invite, Answer, Kick, Promote, Demote, Notice, Emblem, Joining, Transfer, Disband };
bool clan_may(ClanRank me, ClanPower what, ClanRank them = ClanRank::Recruit);
// The lowest rank that may (Lieutenant, Co-Owner, Owner), for a refusal's words and a tooltip's.
ClanRank clan_needs(ClanPower what);
inline constexpr size_t kClanNoticeMax = 80;
inline constexpr int kClanMaxMembers = 60;
// Letters, digits, spaces (not at either end, not two together), _ and -.
bool valid_clan_name(std::string_view name, std::string* why = nullptr);

// ── Channels (SF_GameServer.ini) ───────────────────────────────────────────────

// Event and Staff are a server owner's own (§10.1): an event's channel, and one for staff alone.
enum class ChannelKind : u8 { Training, Officers, Sharpshooter, ClanWar, Scrim, Free, Event, Staff, Count };
const char* channel_kind_name(ChannelKind k);

// A channel as a server's channels.cfg keeps it (§10.1). The ranks allowed are inclusive (CH-1):
// SF_GameServer.ini's "under n" and "over n" are read once, by channel_from_legacy, and never again.
inline constexpr int kMaxChannels = 64;   // the list's limit (§3.3, CH-2)
struct ChannelDef {
    u8 id = 0;                 // 1..64
    std::string name;
    ChannelKind kind = ChannelKind::Free;
    int min_rank = 0;          // the lowest rank allowed in, inclusive
    int max_rank = 74;         // the highest, inclusive
    float min_kd = 0;          // /KD UP x
    u16 capacity = 300;
    u16 modes = 0;             // a bit per Mode rooms may be made with; 0: every one
    std::vector<std::string> maps;   // the maps rooms may play (base ids or a pack's x-...); empty: all its modes offer
    bool hidden = false;       // not listed to those who cannot enter
    u8 order = 0;              // its place in the list
    std::string limit_text() const;   // "Below SSG1", "Above SGT", "K/D over 1.2", "Free"
    bool takes_mode(Mode m) const { return modes == 0 || (modes & (1u << unsigned(m))) != 0; }
};
// ── What a server plays (its Game Masters' Games tab) ──────────────────────────
//
// Game types and maps switched off on the whole server: no room is made, changed or started with
// one, and a random map is never drawn from them. A channel's own games and maps stay on top of
// these. The server's games.cfg, beside channels.cfg.
inline constexpr size_t kMaxMapsOff = 256;
struct ServerGames {
    u16 modes_off = 0;                  // a bit per Mode
    std::vector<std::string> maps_off;  // level ids, lower case: the base maps' and a pack's x-...
    bool takes_mode(Mode m) const { return (modes_off & (1u << unsigned(m))) == 0; }
    // A random choice is always taken: it is drawn from the maps that are on.
    bool takes_map(std::string_view id) const;
    template <typename S> void serialize(S& s) {
        s.u16(modes_off);
        u32 n = u32(maps_off.size());
        s.varu(n);
        if constexpr (S::reading) maps_off.resize(std::min<u32>(n, u32(kMaxMapsOff)));
        for (std::string& m : maps_off) s.str(m, 32);
    }
    bool operator==(const ServerGames&) const = default;
};
// Lower case, each once, no more than kMaxMapsOff; a game type past the last off.
void sanitize(ServerGames& g);

// SF_GameServer.ini's limits ("/LV LO n": rank under n; "/LV UP n": over n; -1 none) as inclusive ranks.
ChannelDef channel_from_legacy(u8 id, ChannelKind kind, int below, int above, float kd);
std::vector<ChannelDef> default_channels();
// Whether a player may enter; `why` says why not.
bool channel_allows(const ChannelDef& c, int rank, float kd, std::string* why = nullptr);
// Within its limits (CH-1, CH-2): ranks 0..74 with min <= max, K/D 0..10, capacity 1..`slots`, a
// name 1..32 characters. False and `why` when it cannot be made right.
bool sanitize(ChannelDef& c, u32 slots, std::string* why = nullptr);

// ── Ranks (SF_ClassPoint.txt, 75 steps) ───────────────────────────────────────

int rank_count();
const char* rank_name(int rank);          // "Private", "Staff Sergeant", ...
const char* rank_short(int rank);         // "PVT", "SSG1", ...
u32 rank_xp(int rank);                    // experience at which this rank begins
int rank_for_xp(u32 xp);
// 0..1 through the current rank.
float rank_progress(u32 xp);

// ── Forces ─────────────────────────────────────────────────────────────────────

struct ForceDef {
    u8 id;
    const char* model;   // the force archive's folder id: "delta", "sas", ...
    const char* name;    // "Delta Force"
    const char* nation;
    u32 price;           // SP; 0 = everyone has it
    float speed;         // run speed multiplier
    float upper_defense, lower_defense, avoid_headshot;   // 0..1 damage taken off
    // A server's own character (Game/Registry.hpp): the base force whose HUD art and voice stand in
    // for it. Null for a base force: its own.
    const char* art = nullptr;
};
// The base twelve. force() also finds this session's pack characters (ids from kFirstPackForce).
std::span<const ForceDef> forces();
const ForceDef* force(u8 id);
// The base force model a force's HUD art, radio voice and menu pictures are looked up by.
inline const char* art_model(const ForceDef& f) { return f.art && *f.art ? f.art : f.model; }
const ForceDef* force_by_model(std::string_view model);

// ── Weapons ────────────────────────────────────────────────────────────────────

enum class Slot : u8 { Primary = 0, Secondary = 1, Melee = 2, Throw = 3, Count = 4 };

// A kit: the primary, the sidearm, the blade and three throwables. The original's kit page
// (PageWeapon) has a cell for each, the bottom row's three; config.cfg has a key for each (WEAPON_1
// to WEAPON_7). A cell holds a weapon of its Slot: cells 3 to 5 all hold throwables. In a match the
// fourth weapon key takes the first throwable out and, pressed again, the next one carried.
inline constexpr size_t kLoadoutSlots = 6;
inline constexpr size_t kFirstThrowCell = size_t(Slot::Throw);
using Loadout = std::array<u16, kLoadoutSlots>;
inline constexpr bool throw_cell(size_t cell) { return cell >= kFirstThrowCell && cell < kLoadoutSlots; }
// The Slot a cell holds.
inline constexpr Slot cell_slot(size_t cell) { return cell >= kFirstThrowCell ? Slot::Throw : Slot(cell); }
// Whether a weapon of `slot` belongs in `cell`.
inline constexpr bool fits_cell(Slot slot, size_t cell) { return cell < kLoadoutSlots && cell_slot(cell) == slot; }
// With a blade or a grenade in hand a soldier runs this much faster than his bare pace, ahead of
// any pistol (1.0 to 1.05) or primary (0.7 to 1.0): weapon.kst gives them 1.1, a pistol's step
// ahead, which nobody could feel.
inline constexpr float kLightCarry = 1.15f;
enum class WeaponClass : u8 { Rifle, Smg, Sniper, MachineGun, Shotgun, Pistol, Knife, Grenade, Count };
enum class GrenadeKind : u8 { None, Frag, Flash, Smoke, Gas };
const char* weapon_class_name(WeaponClass c);
const char* slot_name(Slot s);

inline constexpr u16 kNoWeapon = 0xFFFF;
inline constexpr Loadout kNoLoadout{kNoWeapon, kNoWeapon, kNoWeapon, kNoWeapon, kNoWeapon, kNoWeapon};
// A server's own content (its packs, Docs/UniversalServerDeploy.md §11) is numbered from these up,
// per session, by the server's manifest; base weapons, forces and items keep their fixed numbers
// below them (NM-4, NM-5, NM-6).
inline constexpr u16 kFirstPackWeapon = 0x8000;
inline constexpr u8 kFirstPackForce = 128;
inline constexpr u8 kNoForce = 255;
inline constexpr u16 kFirstPackItem = 0xC000;

struct WeaponDef {
    u16 id = 0;
    std::string code;          // the item list's code: "A013"
    std::string model;         // the weapon archive's folder id: "ak74" (bhw/sf_a_ak74)
    std::string name;          // "AK74"
    WeaponClass klass = WeaponClass::Rifle;
    Slot slot = Slot::Primary;
    u32 price = 0;             // SP; 0 = issued
    bool shop = true;          // on sale
    bool admin_only = false;   // the staff variants: only admin accounts carry them
    std::string skin;          // a texture laid over the model's own ("sf_a_gold_ak47s"), empty = none
    // Handling.
    float damage = 32, head_multiplier = 4, leg_multiplier = 0.75f;
    float range = 9000, falloff_start = 3000, falloff_min = 0.7f;   // centimetres
    float rpm = 700;
    bool automatic = true;
    u16 magazine = 30, reserve = 90;
    float reload_time = 2.4f, draw_time = 0.55f;
    // The cone (degrees), as Soldier Front's recoil.kst has it: spread_stand at rest (spread_crouch
    // crouched), spread_per_shot more a shot up to spread_max, spread_recover back a second all the
    // while (a burst too: held down, the cone opens only as far as the shots outrun it); moving adds
    // up to spread_move, spread_move_rate a second; then the whole cone is multiplied running
    // (run_spread), in the air (air_spread: 2 for nearly every gun) and walking (walk_spread). A
    // bullet's distance from the cone's middle is even over its width, so they crowd the middle.
    float spread_stand = 0.32f, spread_crouch = 0.2f, spread_move = 2.4f, spread_move_rate = 2.5f;
    float spread_per_shot = 0.18f, spread_max = 3.3f, spread_recover = 7;
    float run_spread = 1.5f, air_spread = 2.0f, walk_spread = 1.0f;
    // The view's kick each shot (degrees up, and a step sideways, one way through a burst and now and
    // then the other, kept within recoil_side_max), held while the trigger is and returned at
    // recoil_recover a second once it is let go; the view eases onto it in a few hundredths of a second.
    float recoil_up = 0.38f, recoil_up_max = 5.5f, recoil_side = 0.22f, recoil_recover = 14;
    float recoil_side_max = 2.5f;
    float move_speed = 0.95f;   // weapon.kst WEAPON_SPEED: the run speed it leaves you (a blade's and a grenade's at least kLightCarry)
    // How a soldier carries it (weapon.kst): its grip (girp_type), which picks the arms' clips (1 a
    // pistol, 2 a rifle, sniper rifle or full-size SMG, 3 a machine gun, 4 a compact rifle, 5 the P90
    // and MP7, 6 the Uzi and MAC-10, 7 a pump or bolt gun, 9 a knife, 10 a grenade), and the model seen
    // in the hand, a stem of the force archive's weapon/<stem>.lma hung on the right hand by
    // point/<stem>_point.lma (empty: none).
    u8 grip = 2;
    std::string carried;
    // Right click looks through it: weapon.kst's ZOOM (magnification), its art in the effect
    // archive's scope/ (SCOPE_IMAGE: a mask, a frame with the lens cut out, or a dot_ reticle) and how
    // fast the aim moves through it (SCOPE_MOVEFACTOR).
    bool scoped = false;
    float zoom = 0;
    // The scope goes in two steps, half its power and then full: the AWP alone (players, 2026-10-04:
    // every other sniper rifle has one). Any other scope goes straight in at its full power.
    bool double_zoom = false;
    std::string scope_image;
    float scope_move = 1.5f;
    u8 pellets = 1;
    GrenadeKind grenade = GrenadeKind::None;
    float fuse = 0, blast_radius = 0;
    float melee_range = 0;
    // A server's own weapon (Game/Registry.hpp): the base gun (its model id) whose sounds, HUD
    // picture and kill mark stand in for it. Empty for a base weapon: its own.
    std::string like;
};
// The base weapon model a weapon's sounds and pictures are looked up by.
inline const std::string& art_model(const WeaponDef& w) { return w.like.empty() ? w.model : w.like; }
// The base roster (every id under kFirstPackWeapon). weapon() also finds this session's pack weapons.
std::span<const WeaponDef> weapons();
const WeaponDef* weapon(u16 id);
// This session's pack weapon by id (Game/Registry.hpp), null when none is mounted.
const WeaponDef* pack_weapon(u16 id);
const WeaponDef* weapon_by_model(std::string_view model);
const WeaponDef* weapon_by_code(std::string_view code);
// A weapon as an account keeps it: its item code ("A013"); a number is a game version 5 table
// position (Game/WeaponIdsV5.inl). saved_weapon_code gives the code either way; weapon_from_saved
// the weapon, a retired code with a Soldier Front namesake reading as that gun. kNoWeapon when
// nothing answers.
std::string_view saved_weapon_code(std::string_view token);
u16 weapon_from_saved(std::string_view token);
// A weapon no longer in the game (2026-10-02: every WarRock gun), as a saved account may still keep
// it: the Soldier Front gun it reads as (`model`), or else the SP its owner is paid back (`refund`).
// Null when the code is not one (Game/RetiredWeapons.inl).
struct RetiredWeapon {
    const char* code;
    u32 refund;
    const char* model;
};
const RetiredWeapon* retired_weapon(std::string_view code);
// The kit a new soldier is issued before the first shop visit (the free knife included; one
// throwable, the other two cells empty).
Loadout starter_loadout();
// The cell a weapon goes in when it is put in a kit: its Slot's own; a throwable, the cell already
// holding it, else the first empty throwable cell, else the first throwable cell (in place of what
// was there).
size_t equip_cell(const Loadout& kit, const WeaponDef& w);
// A kit as it may be carried: each cell empty or holding a weapon of its own Slot, no throwable twice.
void sanitize_loadout(Loadout& kit);
// Whether a kit carries this weapon, in whichever cell.
inline bool in_kit(const Loadout& kit, u16 weapon_id) {
    for (u16 id : kit)
        if (id == weapon_id && id != kNoWeapon) return true;
    return false;
}
// Whether a weapon is one of those: issued to every soldier, whatever the shop lists it at. An
// issued gun never wears and cannot be sold back (Game/Wear.hpp).
bool issued(u16 weapon_id);
// The Soldier Front weapon folder whose sounds a class borrows when a gun has none of its own (an
// admin variant whose folder the sound table does not list): "m4a1" for rifles, "mp5" for SMGs, ...
const char* stock_sound_model(WeaponClass c);

// ── Scores and pay (Team Deathmatch's scoring, rank pay-outs) ─────────────────

struct KillScore {
    int points = 2;           // TDM: 2 a kill
    int headshot = 1, grenade = 1, knife = 1, double_kill = 1, multi_kill = 2, special = 3;
};
// ── Horror Mode's items (the menu's ui_icon_zombieitem_*; its Rebirth notice) ──────────────────
//
// Seven things bought beforehand (the Item Shop's Horror tab; the original sold them for cash) and
// spent in Horror Mode and Horror Mode 2. A human's: Rebirth (brought down by the undead, he comes
// back a human where he fell: it goes by itself), a Rescue Kit (health back), a Silver Bullet
// (harder hits for a while), a Blind Cleanse (black fog and smoke do not blind him for a while).
// An undead's: Blood Sucking (his claws give him the health they take), a Shout of Anger (harder
// blows), Undead Speed Up (Super Speed's run). The numbers are zombie2.kst's where it has them
// (the supply box's +100 health and its 10 s attack-up of 30%, the dash's 7 s, black fog's 6 s).
enum class HorrorItem : u8 { Rebirth, RescueKit, SilverBullet, BlindCleanse, BloodSucking, ShoutOfAnger, UndeadSpeedUp, Count };
inline constexpr int kHorrorItems = int(HorrorItem::Count);
struct HorrorItemInfo {
    const char* name;
    const char* icon;     // the menu archive's
    const char* about;
    bool undead;          // an undead's to use (else a human's)
    u32 price;            // SP for one
    float seconds;        // how long it acts (0: at once)
};
const HorrorItemInfo& horror_item(HorrorItem i);
inline constexpr int kHorrorItemMax = 99;       // of each, owned
inline constexpr int kRescueKitHealth = 100;
inline constexpr float kHorrorItemPower = 1.3f; // a Silver Bullet's and a Shout of Anger's hits
inline constexpr double kRebirthSeconds = 1.0;  // down, then back

// ── Special points (gametext 178-197): a round's challenges ───────────────────
//
// Each done once a round pays its SP at the match's end, on top of the match's own and before the
// parts' Special point bonus is rolled over all of it; it also counts as a mission accomplished on
// the ID card. A game type that has no rounds is one round long. Training pays none.
enum class Special : u8 {
    FirstKill,      // "Be the first to kill an enemy"
    ThreeKills,     // "Kill more than 3 enemies per round": the third kill of the round
    SidearmKills,   // "Make 2 kills in a round by using secondary weapon"
    KnifeKill,      // "Achieve a 1 knife kill during single round"
    AllEnemies,     // "Kill all enemy": every soldier of the other side, three or more, by one hand
    QuickRound,     // "Win round within 1 minute": everyone on the side that won it
    LoveShot,       // "Love shot": two soldiers bring each other down within moments
    TwentyKills,    // "Achieve 20 kills": in the game, once
    SevenAll,       // "Make round score 7:7": everyone in the game
    Count
};
struct SpecialInfo {
    const char* name;   // the original's own words
    u16 sp;
};
const SpecialInfo& special_info(Special s);
inline constexpr float kQuickRoundSeconds = 60.0f;
inline constexpr float kLoveShotSeconds = 3.0f;

// A match's rank points. Deaths cost 5 points each from 2nd Lieutenant up (gametext 535: no penalty
// for deaths below LV20); `death_penalty` false (lower ranks, Holy Bless) and they cost nothing.
u32 match_xp(int kills, int deaths, bool won, int minutes, bool death_penalty = true);
// The first rank whose deaths cost points: 2nd Lieutenant (2LT1, LV20).
inline constexpr int kDeathPenaltyRank = 20;
// The first rank that may send a gift: Master Sergeant (MSG1, LV15; gametext 1561).
inline constexpr int kGiftRank = 15;

// ── Radio (Z command, X general, C reply; SFSound.xml's RadioMessage voices) ──

// Group 3 is never picked from a menu: it is what a soldier says by himself ("Fire in the hole!").
inline constexpr u8 kRadioAuto = 3;
int radio_count(u8 group);
const char* radio_line(u8 group, u8 line);
// SFSound.xml's name for the voice ("RadioMessage_MoveOut"); its files are keyed Man and Woman.
const char* radio_sound(u8 group, u8 line);
const char* radio_group_name(u8 group);
u32 match_sp(int kills, bool won, int minutes);

}  // namespace lsf
