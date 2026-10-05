// Rewards and events: the duffle bags a promotion brings and the gift boxes events drop (the
// inventory's Event tab, as the original's), the hours of play and the days of signing in that
// pay, the day's quests, and the calendar of events a Game Master keeps (a season's, a weekend's,
// one of their own). The server decides and keeps all of it (Server/Source/Rewards.cpp, saved in
// rewards.cfg); the client shows it (Screens/Rewards.cpp) and a Game Master edits it in the staff
// panel. Docs/Rewards.md has the numbers and why they are what they are.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace lsf {

using eng::i64;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::u8;

// ── Boxes: what the Event tab holds, opened for a prize ───────────────────────

enum class BoxKind : u8 {
    DuffleA, DuffleB, DuffleC, DuffleD,          // a promotion's, by the rank reached
    GiftBox, HorrorBox, RabbitBox, PirateBox, DevilBox, TigerBox,   // the events'
    Count
};
inline constexpr int kBoxKinds = int(BoxKind::Count);
inline constexpr u8 kNoBox = 0xFF;
struct BoxInfo {
    const char* key;       // rewards.cfg's name for it
    const char* name;      // "Duffle Bag (B)"
    const char* picture;   // the lobby archive's own art
    const char* about;     // where one comes from
};
const BoxInfo& box_info(u8 kind);
int box_by_key(std::string_view key);   // -1: none
// The duffle bag a promotion to `rank` (Rules.hpp rank index) brings: A up to Sergeant, B the
// sergeants above, C the lieutenants and captains, D Major and up.
BoxKind duffle_for_rank(int rank);

// One prize of a box's table, picked by its weight against the others'.
enum class PrizeKind : u8 { Sp, Xp, Boost, Part, Weapon, Box, Count };
const char* prize_kind_name(u8 kind);
struct Prize {
    u8 kind = 0;
    u16 weight = 1;
    u32 lo = 0, hi = 0;   // SP, XP: the amount (lo..hi); Boost, Part: days (lo..hi); Weapon: the most
                          // its price may be (0: any), and `hi` SP instead when every one is owned;
                          // Box: how many (lo..hi)
    std::string match;    // Boost, Part: words, one of which its name must have ("Santa|X-MAS"; empty:
                          // the usual ones); Box: the box's key
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(weight), s.u32(lo), s.u32(hi), s.str(match, 80); }
    bool operator==(const Prize&) const = default;
};
struct LootTable {
    u8 box = 0;
    std::vector<Prize> prizes;
    template <typename S> void serialize(S& s) { s.u8(box), s.list(prizes, 16); }
    bool operator==(const LootTable&) const = default;
};
// "500 - 1,500 SP", "a part for 7 days (Santa, X-MAS)", ...
std::string prize_text(const Prize& p);

// ── The day's quests ──────────────────────────────────────────────────────────

enum class QuestGoal : u8 {
    Kills, Headshots, Wins, Matches, Minutes, EarnSp, MultiKills,
    KnifeKills, GrenadeKills, PistolKills, RifleKills, SmgKills, SniperKills, ShotgunKills, MachineGunKills,
    Count
};
const char* quest_goal_name(u8 goal);
struct QuestDef {
    u16 id = 0;
    u8 goal = 0;
    u16 target = 1;
    u32 sp = 0, xp = 0;
    u16 weight = 10;      // how often it is drawn against the others
    std::string text;     // its own words; empty: the goal's ("Get 20 kills")
    template <typename S> void serialize(S& s) { s.u16(id), s.u8(goal), s.u16(target), s.u32(sp), s.u32(xp), s.u16(weight), s.str(text, 80); }
    bool operator==(const QuestDef&) const = default;
};
std::string quest_text(const QuestDef& q);

// What a match counted toward the quests (the server, at a match's end).
struct QuestCounts {
    u32 kills = 0, headshots = 0, wins = 0, matches = 0, minutes = 0, sp = 0, multi_kills = 0;
    u32 knife = 0, grenade = 0, pistol = 0, rifle = 0, smg = 0, sniper = 0, shotgun = 0, machine_gun = 0;
};
u32 quest_count(const QuestDef& q, const QuestCounts& c);

// ── Play time and signing in ──────────────────────────────────────────────────

// Minutes played in a day (matches played to their end) that pay, each step once a day.
struct PlayStep {
    u16 minutes = 60;
    u32 sp = 0;
    u8 box = kNoBox;
    template <typename S> void serialize(S& s) { s.u16(minutes), s.u32(sp), s.u8(box); }
    bool operator==(const PlayStep&) const = default;
};
// One day of the week of signing in (the seventh pays the most, then the week begins again).
struct SignInDay {
    u32 sp = 0;
    u8 box = kNoBox;
    template <typename S> void serialize(S& s) { s.u32(sp), s.u8(box); }
    bool operator==(const SignInDay&) const = default;
};

// ── Events ────────────────────────────────────────────────────────────────────

enum class EventRepeat : u8 { Once, Weekly, Yearly, Count };
const char* event_repeat_name(u8 r);
enum class EventTheme : u8 { None, Christmas, Halloween, Easter, Valentine, Summer, NewYear, Anniversary, Weekend, Count };
const char* event_theme_name(u8 t);

struct EventDef {
    u32 id = 0;
    std::string name;            // "Christmas"
    std::string banner;          // the lobby's line while it runs
    u8 theme = 0;                // EventTheme: its colours and its art, nothing more
    u8 repeat = 0;               // EventRepeat
    u64 start = 0, end = 0;      // unix seconds (UTC). Weekly: the first week's; Yearly: one year's
    u16 sp_pct = 100, xp_pct = 100;   // a match's SP and XP, per cent (the highest running counts)
    u8 drop_box = kNoBox;        // a box dropped at a match's end...
    u16 drop_permille = 0;       // ...this many times in a thousand
    u32 sign_in_sp = 0;          // more for the day's sign-in while it runs
    std::vector<QuestDef> quests;   // more quests for the day while it runs
    bool enabled = true;
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(name, 40), s.str(banner, 160), s.u8(theme), s.u8(repeat), s.u64(start), s.u64(end);
        s.u16(sp_pct), s.u16(xp_pct), s.u8(drop_box), s.u16(drop_permille), s.u32(sign_in_sp), s.list(quests, 6), s.boolean(enabled);
    }
    bool operator==(const EventDef&) const = default;
};
// Whether it runs at `now`; `from`/`to` this run's.
bool event_window(const EventDef& e, u64 now, u64* from = nullptr, u64* to = nullptr);
// When it next begins after `now` (0: never again).
u64 event_next(const EventDef& e, u64 now);
// One of each theme to start from, dated for the year `now` falls in.
EventDef event_template(EventTheme theme, u64 now);

// Calendar helpers (UTC, the proleptic Gregorian calendar).
u64 utc_time(int year, int month, int day, int hour = 0, int minute = 0);
void utc_date(u64 t, int& year, int& month, int& day, int& hour, int& minute);
std::string utc_text(u64 t);              // "2026-12-20 00:00"
bool parse_utc(std::string_view text, u64& out);

// ── All of it ─────────────────────────────────────────────────────────────────

struct RewardsConfig {
    std::vector<LootTable> boxes;     // one a BoxKind
    std::vector<PlayStep> play;       // up to six a day
    std::vector<SignInDay> week;      // seven
    std::vector<QuestDef> quests;     // the pool the day's are drawn from
    u8 quests_a_day = 3;
    u32 all_quests_sp = 800;          // all of the day's done
    u8 all_quests_box = u8(BoxKind::GiftBox);
    u8 day_starts = 0;                // the hour (UTC) a day begins
    std::vector<EventDef> events;
    template <typename S> void serialize(S& s) {
        s.list(boxes, kBoxKinds), s.list(play, 6), s.list(week, 7), s.list(quests, 48), s.u8(quests_a_day), s.u32(all_quests_sp),
            s.u8(all_quests_box), s.u8(day_starts), s.list(events, 64);
    }
    bool operator==(const RewardsConfig&) const = default;
    const LootTable* table(u8 box) const;
};
RewardsConfig default_rewards();
// Everything within its range: weights, amounts, days, counts; a table for every box.
void sanitize(RewardsConfig& c);
// The server's rewards.cfg, and back.
std::string rewards_text(const RewardsConfig& c);
bool rewards_from_text(std::string_view text, RewardsConfig& out, std::string* why = nullptr);
// The day (counted from 1970) `t` falls in, a day beginning at `day_starts` UTC.
u32 reward_day(u64 t, u8 day_starts);
u64 reward_day_start(u32 day, u8 day_starts);

}  // namespace lsf
