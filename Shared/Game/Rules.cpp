#include "Game/Rules.hpp"

#include "Game/Registry.hpp"

#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>

namespace lsf {

// ── Modes ──────────────────────────────────────────────────────────────────────

const ModeInfo& mode_info(Mode mode) {
    using G = GoalKind;
    //  name                  short  teams  rounds respawn def min max step label        minutes goal             columns
    static const ModeInfo kModes[] = {
        {"Team Battle", "TB", true, true, false, 5, 1, 15, 1, "Rounds to win", 3, G::RoundsToWin, {}},
        {"Team Deathmatch", "TDM", true, false, true, 100, 50, 250, 25, "Points", 10, G::TeamPoints, {"Point"}},
        {"Single Battle", "SB", false, false, true, 30, 10, 100, 5, "Kills", 10, G::PlayerKills, {}},
        {"Sniper Mode", "SNP", true, false, true, 30, 10, 100, 5, "Kills", 10, G::TeamKills, {}},
        {"Capture the Captain", "CTC", true, true, true, 5, 1, 15, 1, "Rounds to win", 5, G::RoundsToWin, {"Captain"}},
        {"Captain Mode", "CPT", true, true, true, 5, 1, 15, 1, "Rounds to win", 4, G::RoundsToWin, {"Captain"}},
        {"Horror Mode", "HOR", true, true, false, 5, 1, 15, 1, "Rounds", 4, G::RoundsPlayed, {"Undead Kill", "Human Kill"}},
        {"Training", "TRN", false, false, true, 0, 0, 0, 1, "", 30, G::None, {}},
        {"Team Slayer", "TS", true, false, true, 100, 50, 250, 25, "Points", 10, G::TeamPoints, {"Point"}},
        {"Occupy", "OCC", true, true, false, 5, 1, 15, 1, "Rounds to win", 4, G::RoundsToWin, {"Capture", "Sample"}},
        {"Horror Mode 2", "HR2", true, true, false, 5, 1, 15, 1, "Rounds", 5, G::RoundsPlayed, {"Undead Kill", "Human Kill", "Save"}},
        {"Pirate Mode", "PIR", true, false, true, 150, 50, 250, 25, "Points", 10, G::TeamPoints, {"Point", "Save"}},
    };
    static_assert(std::size(kModes) == size_t(Mode::Count));
    return kModes[std::min<size_t>(size_t(mode), size_t(Mode::Count) - 1)];
}

Mission mission_from_objective(std::string_view objective) {
    const std::string o = eng::str::lower(objective);
    if (o == "destroy") return Mission::Blast;
    if (o == "takeback" || o == "take back" || o == "occupy") return Mission::Capture;
    if (o == "escape") return Mission::Escape;
    if (o == "dual") return Mission::Dual;
    return Mission::Elimination;
}

const char* mission_name(Mission m) {
    switch (m) {
        case Mission::Blast: return "Blast Operation";
        case Mission::Capture: return "Capture Operation";
        case Mission::Escape: return "Flee Operation";
        case Mission::Dual: return "Dual Operation";
        default: return "Elimination";
    }
}

const char* mission_attack_text(Mission m) {
    switch (m) {
        case Mission::Blast: return "Plant the bomb at one of the two targets.";
        case Mission::Capture: return "Seize the target and bring it back to base.";
        case Mission::Escape: return "Break through the enemy lines to the extraction point.";
        case Mission::Dual: return "Take the enemy's target home before they take yours.";
        default: return "Eliminate the enemy team.";
    }
}

const char* mission_defence_text(Mission m) {
    switch (m) {
        case Mission::Blast: return "Stop the bomb, or defuse it once it is planted.";
        case Mission::Capture: return "Keep the target out of enemy hands.";
        case Mission::Escape: return "Hold the line: nobody reaches the extraction point.";
        case Mission::Dual: return "Take the enemy's target home before they take yours.";
        default: return "Eliminate the enemy team.";
    }
}

const char* team_name(Team t) {
    switch (t) {
        case Team::Red: return "Red";
        case Team::Blue: return "Blue";
        case Team::Observer: return "Observer";
        default: return "";
    }
}

bool takes_no_snipers(Mode mode) { return mode == Mode::SingleBattle || mode == Mode::TeamBattle || mode == Mode::TeamDeathmatch; }

bool clan_battle_mode(Mode mode) { return mode == Mode::TeamBattle || mode == Mode::TeamDeathmatch; }

std::string game_type_name(Mode mode, bool no_snipers, bool clan_battle) {
    std::string name = mode_name(mode);
    if (clan_battle && clan_battle_mode(mode)) name = mode == Mode::TeamBattle ? "Clan Battle" : "Clan Deathmatch";
    return no_snipers && takes_no_snipers(mode) ? name + " (no sniper)" : name;
}

void step_game_type(RoomSettings& r, int step, u16 allowed) {
    if (step == 0) return;
    const Mode before = r.mode;
    // A clan battle room turns through the game types clans meet in, and no others; every room
    // through the ones its channel and its server play.
    auto next = [&](int way) {
        Mode m = r.mode;
        for (int k = 0; k < int(Mode::Count); ++k) {
            m = Mode((int(m) + int(Mode::Count) + way) % int(Mode::Count));
            if ((allowed & (1u << unsigned(m))) && (!r.clan_battle || clan_battle_mode(m))) {
                r.mode = m;
                return;
            }
        }
    };
    if (step > 0) {
        if (takes_no_snipers(r.mode) && !r.no_snipers) r.no_snipers = true;
        else next(+1), r.no_snipers = false;
    } else {
        if (r.no_snipers) r.no_snipers = false;
        else next(-1), r.no_snipers = takes_no_snipers(r.mode);
    }
    if (r.mode != before) {
        r.goal = mode_info(r.mode).goal_default;
        r.minutes = mode_info(r.mode).minutes_default;
        if (mode_info(r.mode).teams && (r.max_players & 1)) ++r.max_players;
    }
}

bool sanitize(RoomSettings& r) {
    r.title = eng::str::sanitize_line(r.title, 32);
    if (r.title.empty()) r.title = "Let's play!";
    r.password = eng::str::sanitize_line(r.password, 16);
    r.map = eng::str::lower(eng::str::sanitize_line(r.map, 32));
    if (r.map.empty()) return false;
    // A clan battle: a game type clans meet in, soldiers only, nobody in mid-game, the sides as the clans are.
    r.red_clan = eng::str::sanitize_line(r.red_clan, 24), r.blue_clan = eng::str::sanitize_line(r.blue_clan, 24);
    if (r.clan_battle) {
        if (!clan_battle_mode(r.mode)) r.mode = Mode::TeamBattle, r.goal = mode_info(r.mode).goal_default, r.minutes = mode_info(r.mode).minutes_default;
        r.bots = 0;
        r.free_join = false;
        r.team_balance = false;
    } else {
        r.red_clan.clear(), r.blue_clan.clear();
    }
    const ModeInfo& m = mode_info(r.mode);
    r.max_players = u8(std::clamp<int>(r.max_players, 2, kMaxRoomPlayers));
    if (m.teams && (r.max_players & 1)) ++r.max_players;
    if (m.goal_max > 0) r.goal = u8(std::clamp<int>(r.goal, m.goal_min, m.goal_max));
    else r.goal = 0;
    r.minutes = u8(std::clamp<int>(r.minutes, 1, 60));
    if (!takes_no_snipers(r.mode)) r.no_snipers = false;
    if (u8(r.time_of_day) >= u8(TimeOfDay::Count)) r.time_of_day = TimeOfDay::Day;
    // Bots fill at most every seat but one; Training's are always targets, and it has some.
    r.bots = u8(std::min<int>(r.bots, r.max_players - 1));
    r.bot_skill = u8(std::min<int>(r.bot_skill, int(BotSkill::Hard)));
    if (r.mode == Mode::Training) {
        r.bot_skill = u8(BotSkill::Target);
        if (r.bots == 0) r.bots = 6;
    }
    return true;
}

const char* bot_skill_name(BotSkill s) {
    switch (s) {
        case BotSkill::Easy: return "Easy";
        case BotSkill::Hard: return "Hard";
        case BotSkill::Target: return "Target";
        default: return "Normal";
    }
}

const char* emblem_shape_name(u8 shape) {
    static const char* names[] = {"Circle", "Ring", "Half circle", "Quarter", "Square", "Rounded square", "Frame", "Triangle", "Right triangle", "Trapezoid",
                                  "Parallelogram", "Diamond", "Pentagon", "Hexagon", "Octagon", "Star", "Four-point star", "Burst", "Cross", "Line",
                                  "Chevron", "Arrow", "Crescent", "Shield", "Snowflake", "Bolt", "Heart", "Crown", "Gear", "Drop",
                                  "Skull", "Flame", "Bomb", "Knife", "Rifle", "Sniper rifle", "Pistol", "Shotgun", "Machine gun", "Grenade",
                                  "C4", "Crosshair", "Scope", "Explosion", "Helmet", "Vest", "Medal", "Trophy", "Flag", "Rocket",
                                  "Globe", "Sun", "Moon", "Cloud", "Tree", "Gem", "Key", "Eye", "Mask", "Compass"};
    static_assert(std::size(names) == size_t(EmblemShape::Count));
    return shape < u8(EmblemShape::Count) ? names[shape] : "?";
}

void sanitize_mark(ClanMark& mark) {
    mark.background = std::max<u8>(1, mark.background);
    if (mark.layers.size() > kEmblemLayers) mark.layers.resize(kEmblemLayers);
    for (EmblemLayer& l : mark.layers) {
        if (l.shape >= u8(EmblemShape::Count)) l.shape = 0;
        l.x = std::min<u8>(l.x, 200), l.y = std::min<u8>(l.y, 200);
        l.w = std::clamp<u8>(l.w, 2, 250), l.h = std::clamp<u8>(l.h, 2, 250);
        l.turn = u16(l.turn % 360);
        l.a = std::max<u8>(l.a, 16);
        l.style &= 0x1F;
    }
}

// Twelve bytes a shape, as hex, the shapes run together.
std::string emblem_text(const ClanMark& mark) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (const EmblemLayer& l : mark.layers) {
        const u8 bytes[12] = {l.shape, l.x, l.y, l.w, l.h, u8(l.turn >> 8), u8(l.turn & 0xFF), l.r, l.g, l.b, l.a, l.style};
        for (u8 b : bytes) out += hex[b >> 4], out += hex[b & 15];
    }
    return out;
}

void emblem_from_text(std::string_view text, ClanMark& mark) {
    mark.layers.clear();
    auto nibble = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t at = 0; at + 24 <= text.size() && mark.layers.size() < kEmblemLayers; at += 24) {
        u8 bytes[12];
        bool ok = true;
        for (size_t k = 0; k < 12; ++k) {
            const int hi = nibble(text[at + k * 2]), lo = nibble(text[at + k * 2 + 1]);
            if (hi < 0 || lo < 0) ok = false;
            bytes[k] = u8((hi << 4) | lo);
        }
        if (!ok) break;
        EmblemLayer l;
        l.shape = bytes[0], l.x = bytes[1], l.y = bytes[2], l.w = bytes[3], l.h = bytes[4];
        l.turn = u16((bytes[5] << 8) | bytes[6]);
        l.r = bytes[7], l.g = bytes[8], l.b = bytes[9], l.a = bytes[10], l.style = bytes[11];
        mark.layers.push_back(l);
    }
    sanitize_mark(mark);
}

bool valid_clan_name(std::string_view name, std::string* why) {
    auto no = [&](const char* text) {
        if (why) *why = text;
        return false;
    };
    if (name.size() < kClanNameMin || name.size() > kClanNameMax) return no("A clan name is 2 to 12 characters.");
    if (name.front() == ' ' || name.back() == ' ' || name.find("  ") != std::string_view::npos) return no("Spaces go between words in a clan name.");
    for (char c : name)
        if (!std::isalnum((unsigned char)c) && c != ' ' && c != '_' && c != '-') return no("Use letters, digits, spaces, _ and - in a clan name.");
    return true;
}

const char* stock_sound_model(WeaponClass c) {
    switch (c) {
        case WeaponClass::Smg: return "mp5";
        case WeaponClass::Sniper: return "awp";
        case WeaponClass::MachineGun: return "m249";
        case WeaponClass::Shotgun: return "m870";
        case WeaponClass::Pistol: return "beretta";
        case WeaponClass::Knife: return "m9";
        case WeaponClass::Grenade: return "m67";
        default: return "m4a1";
    }
}

const char* time_of_day_name(TimeOfDay t) { return t == TimeOfDay::Night ? "Night" : "Day"; }

bool baked_at_night(std::string_view level_id) {
    // The four horror maps: dark, moonlit or lamp-lit bakes (their lobby pictures, and sfcheck
    // lightlevels puts three of them among the five darkest). Nighthawk is named for the aircraft
    // on its helipad and is lit by day.
    static constexpr std::string_view kNight[] = {"nervegashorror", "plasmahorror", "shanghaihorror", "villagehorror"};
    for (std::string_view n : kNight)
        if (level_id == n) return true;
    // A server's own map says so itself (the list above cannot name it: §11.5).
    if (const PackMap* m = registry::map(level_id)) return m->night;
    return false;
}

// ── Channels ───────────────────────────────────────────────────────────────────

const char* channel_kind_name(ChannelKind k) {
    switch (k) {
        case ChannelKind::Training: return "Training Room";
        case ChannelKind::Officers: return "NCO & Officers Lounge";
        case ChannelKind::Sharpshooter: return "Sharpshooter Club";
        case ChannelKind::ClanWar: return "Clan War";
        case ChannelKind::Scrim: return "Scrim";
        case ChannelKind::Event: return "Event";
        case ChannelKind::Staff: return "Staff";
        default: return "Free Channel";
    }
}

std::string ChannelDef::limit_text() const {
    const int top = rank_count() - 1;
    if (max_rank < top && min_rank <= 0) return eng::str::format("Below %s", rank_short(max_rank + 1));
    if (min_rank > 0 && max_rank >= top) return eng::str::format("Above %s", rank_short(min_rank - 1));
    if (min_rank > 0 && max_rank < top) return eng::str::format("%s to %s", rank_short(min_rank), rank_short(max_rank));
    if (min_kd > 0) return eng::str::format("K/D over %.1f", double(min_kd));
    return "Free";
}

ChannelDef channel_from_legacy(u8 id, ChannelKind kind, int below, int above, float kd) {
    ChannelDef c;
    c.id = id;
    c.kind = kind;
    c.name = eng::str::format("%s (ch %u)", channel_kind_name(kind), unsigned(id));
    // "Under n" lets n - 1 in at most; "over n" lets n + 1 in at least (CH-1).
    c.min_rank = above >= 0 ? above + 1 : 0;
    c.max_rank = below >= 0 ? below - 1 : rank_count() - 1;
    c.min_kd = kd;
    return c;
}

std::vector<ChannelDef> default_channels() {
    // SF_GameServer.ini (2013): the thirteen channels, their limits verbatim.
    std::vector<ChannelDef> out;
    auto add = [&](u8 id, ChannelKind kind, int below, int above, float kd) {
        out.push_back(channel_from_legacy(id, kind, below, above, kd));
        out.back().order = id;
    };
    add(1, ChannelKind::Training, 4, -1, 0);
    add(2, ChannelKind::Training, 4, -1, 0);
    add(3, ChannelKind::Officers, -1, 5, 0);
    add(4, ChannelKind::Officers, -1, 5, 0);
    add(5, ChannelKind::Sharpshooter, -1, -1, 1.2f);
    add(6, ChannelKind::Sharpshooter, -1, -1, 1.2f);
    add(7, ChannelKind::ClanWar, -1, -1, 0);
    add(8, ChannelKind::ClanWar, -1, -1, 0);
    add(9, ChannelKind::Scrim, -1, 20, 0);
    add(10, ChannelKind::Free, -1, -1, 0);
    add(11, ChannelKind::Free, -1, -1, 0);
    add(12, ChannelKind::Free, -1, -1, 0);
    add(13, ChannelKind::Free, -1, -1, 0);
    return out;
}

bool channel_allows(const ChannelDef& c, int rank, float kd, std::string* why) {
    auto no = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (rank > c.max_rank) return no(eng::str::format("%s is for soldiers below %s.", c.name.c_str(), rank_name(c.max_rank + 1)));
    if (rank < c.min_rank) return no(eng::str::format("%s is for soldiers above %s.", c.name.c_str(), rank_name(std::max(0, c.min_rank - 1))));
    if (c.min_kd > 0 && kd < c.min_kd) return no(eng::str::format("%s needs a K/D over %.1f.", c.name.c_str(), double(c.min_kd)));
    return true;
}

bool sanitize(ChannelDef& c, u32 slots, std::string* why) {
    auto no = [&](const char* text) {
        if (why) *why = text;
        return false;
    };
    if (c.id < 1 || c.id > kMaxChannels) return no("A channel's number is 1 to 64.");
    c.name = eng::str::sanitize_line(c.name, 32);
    if (c.name.empty()) return no("A channel needs a name.");
    if (c.kind >= ChannelKind::Count) c.kind = ChannelKind::Free;
    const int top = rank_count() - 1;
    c.min_rank = std::clamp(c.min_rank, 0, top);
    c.max_rank = std::clamp(c.max_rank, 0, top);
    if (c.min_rank > c.max_rank) return no("The lowest rank allowed is above the highest.");
    c.min_kd = std::clamp(std::isfinite(c.min_kd) ? c.min_kd : 0.0f, 0.0f, 10.0f);
    c.capacity = u16(std::clamp<u32>(c.capacity, 1, std::max<u32>(1, slots)));
    c.modes = u16(c.modes & ((1u << unsigned(Mode::Count)) - 1));
    if (c.maps.size() > 64) c.maps.resize(64);
    for (std::string& m : c.maps) m = eng::str::lower(eng::str::sanitize_line(m, 32));
    std::erase_if(c.maps, [](const std::string& m) { return m.empty(); });
    return true;
}

bool ServerGames::takes_map(std::string_view id) const {
    if (id == "allrandom" || id == "hotrandom") return true;   // Game/Modes.hpp kAllRandom, kHotRandom
    for (const std::string& m : maps_off)
        if (eng::str::iequals(m, id)) return false;
    return true;
}

void sanitize(ServerGames& g) {
    g.modes_off = u16(g.modes_off & ((1u << unsigned(Mode::Count)) - 1));
    std::vector<std::string> kept;
    for (const std::string& m : g.maps_off) {
        std::string id = eng::str::lower(eng::str::sanitize_line(m, 32));
        if (!id.empty() && std::find(kept.begin(), kept.end(), id) == kept.end() && kept.size() < kMaxMapsOff) kept.push_back(std::move(id));
    }
    g.maps_off = std::move(kept);
}

// ── Ranks ──────────────────────────────────────────────────────────────────────

namespace {
struct RankRow {
    const char* name;
    const char* short_name;
    u32 max_point;   // SF_ClassPoint.txt MAXPOINT: the last XP of this rank
};
const RankRow kRanks[] = {
    {"Private", "PVT", 3499},          {"Private 2nd Class", "PV2", 4055}, {"Private 1st Class", "PFC", 4666}, {"Corporal", "CPL", 5338},
    {"Sergeant", "SGT", 6077},         {"Staff Sergeant", "SSG1", 6890},   {"Staff Sergeant", "SSG2", 7785},   {"Staff Sergeant", "SSG3", 8769},
    {"Staff Sergeant", "SSG4", 9852},  {"Staff Sergeant", "SSG5", 11042},  {"Sergeant 1st Class", "SFC1", 12352},
    {"Sergeant 1st Class", "SFC2", 13793}, {"Sergeant 1st Class", "SFC3", 15378}, {"Sergeant 1st Class", "SFC4", 17121},
    {"Sergeant 1st Class", "SFC5", 19039}, {"Master Sergeant", "MSG1", 21149}, {"Master Sergeant", "MSG2", 23469},
    {"Master Sergeant", "MSG3", 26022}, {"Master Sergeant", "MSG4", 28829}, {"Master Sergeant", "MSG5", 31918},
    {"2nd Lieutenant", "2LT1", 35315}, {"2nd Lieutenant", "2LT2", 39392}, {"2nd Lieutenant", "2LT3", 44284},
    {"2nd Lieutenant", "2LT4", 50155}, {"2nd Lieutenant", "2LT5", 57200}, {"1st Lieutenant", "1LT1", 65654},
    {"1st Lieutenant", "1LT2", 75798}, {"1st Lieutenant", "1LT3", 87972}, {"1st Lieutenant", "1LT4", 102580},
    {"1st Lieutenant", "1LT5", 120110}, {"Captain", "CPT1", 141145}, {"Captain", "CPT2", 166388}, {"Captain", "CPT3", 196680},
    {"Captain", "CPT4", 233029},       {"Captain", "CPT5", 276649},       {"Major", "MAJ1", 328993},         {"Major", "MAJ2", 391805},
    {"Major", "MAJ3", 467180},         {"Major", "MAJ4", 557630},         {"Major", "MAJ5", 666169},
    {"Lieutenant Colonel", "LTC1", 796417}, {"Lieutenant Colonel", "LTC2", 952714}, {"Lieutenant Colonel", "LTC3", 1140271},
    {"Lieutenant Colonel", "LTC4", 1365339}, {"Lieutenant Colonel", "LTC5", 1635420}, {"Colonel", "COL1", 1959518},
    {"Colonel", "COL2", 2348436},      {"Colonel", "COL3", 2815137},      {"Colonel", "COL4", 3375178},      {"Colonel", "COL5", 4047228},
    {"Brigadier General", "BG1", 4853687}, {"Brigadier General", "BG2", 5821438}, {"Brigadier General", "BG3", 6982739},
    {"Brigadier General", "BG4", 8376301}, {"Brigadier General", "BG5", 10048575}, {"Major General", "MG1", 12055304},
    {"Major General", "MG2", 14463379}, {"Major General", "MG3", 17353068}, {"Major General", "MG4", 20820696},
    {"Major General", "MG5", 24981849}, {"Lieutenant General", "LTG1", 29975232}, {"Lieutenant General", "LTG2", 35967292},
    {"Lieutenant General", "LTG3", 43157765}, {"Lieutenant General", "LTG4", 51786331}, {"Lieutenant General", "LTG5", 62140611},
    {"General", "GEN1", 74565747},     {"General", "GEN2", 88233397},     {"General", "GEN3", 103267812},    {"General", "GEN4", 119805668},
    {"General", "GEN5", 137997309},    {"General of the Army", "GA1", 158008115}, {"General of the Army", "GA2", 180020002},
    {"General of the Army", "GA3", 204233077}, {"General of the Army", "GA4", 230867460}, {"General of the Army", "GA5", 300000000},
};
constexpr int kRankCount = int(sizeof(kRanks) / sizeof(kRanks[0]));
}  // namespace

int rank_count() { return kRankCount; }
const char* rank_name(int r) { return kRanks[std::clamp(r, 0, kRankCount - 1)].name; }
const char* rank_short(int r) { return kRanks[std::clamp(r, 0, kRankCount - 1)].short_name; }
u32 rank_xp(int r) { return r <= 0 ? 0 : kRanks[std::min(r, kRankCount) - 1].max_point + 1; }

int rank_for_xp(u32 xp) {
    for (int r = 0; r < kRankCount; ++r)
        if (xp <= kRanks[r].max_point) return r;
    return kRankCount - 1;
}

float rank_progress(u32 xp) {
    const int r = rank_for_xp(xp);
    const u32 lo = rank_xp(r), hi = kRanks[r].max_point + 1;
    return hi > lo ? std::clamp(float(xp - lo) / float(hi - lo), 0.0f, 1.0f) : 1.0f;
}

// ── Forces ─────────────────────────────────────────────────────────────────────

std::span<const ForceDef> forces() {
    // The twelve forces of the roster. ARTC is the free starting force (ARTC, Force Recon, Mulan
    // and PSU ship as FXA models, the rest as LMA). Prices are the ijji shop's in SP; Mulan was
    // sold for cash and is priced in SP here.
    static const ForceDef kForces[] = {
        {0, "artc", "ARTC", "Korea", 0, 1.00f, 0.00f, 0.00f, 0.00f},
        {1, "delta", "Delta Force", "USA", 1000, 1.00f, 0.02f, 0.00f, 0.00f},
        {2, "rokmc", "ROKMC", "Korea", 1000, 1.00f, 0.02f, 0.00f, 0.00f},
        {3, "ksf", "KSF", "Korea", 1000, 1.01f, 0.00f, 0.02f, 0.00f},
        {4, "sas", "SAS", "England", 1000, 1.00f, 0.03f, 0.00f, 0.00f},
        {5, "gsg9", "GSG-9", "Germany", 1000, 1.00f, 0.00f, 0.03f, 0.00f},
        {6, "gign", "GIGN", "France", 1000, 1.02f, 0.00f, 0.00f, 0.00f},
        {7, "spetsnaz", "Spetsnaz", "Russia", 1000, 1.00f, 0.02f, 0.02f, 0.00f},
        {8, "srg", "SRG", "Hong Kong", 9900, 1.02f, 0.03f, 0.02f, 0.02f},
        {9, "forcerecon", "Force Recon", "USA", 9900, 1.02f, 0.02f, 0.03f, 0.02f},
        {10, "psu", "PSU", "Philippines", 9900, 1.03f, 0.02f, 0.02f, 0.02f},
        {11, "mulan", "Mulan", "China", 30000, 1.04f, 0.02f, 0.02f, 0.03f},
    };
    return kForces;
}

const ForceDef* force(u8 id) {
    for (const ForceDef& f : forces())
        if (f.id == id) return &f;
    if (id >= kFirstPackForce && id != kNoForce)
        if (const PackForce* f = registry::force(id)) return &f->def;
    return nullptr;
}

const ForceDef* force_by_model(std::string_view model) {
    for (const ForceDef& f : forces())
        if (eng::str::iequals(f.model, model)) return &f;
    return nullptr;
}

// ── Weapons ────────────────────────────────────────────────────────────────────

const char* weapon_class_name(WeaponClass c) {
    switch (c) {
        case WeaponClass::Rifle: return "Rifle";
        case WeaponClass::Smg: return "SMG";
        case WeaponClass::Sniper: return "Sniper";
        case WeaponClass::MachineGun: return "Machine Gun";
        case WeaponClass::Shotgun: return "Shotgun";
        case WeaponClass::Pistol: return "Pistol";
        case WeaponClass::Knife: return "Melee";
        case WeaponClass::Grenade: return "Throwing";
        default: return "?";
    }
}

const char* slot_name(Slot s) {
    switch (s) {
        case Slot::Primary: return "Primary";
        case Slot::Secondary: return "Secondary";
        case Slot::Melee: return "Melee";
        case Slot::Throw: return "Throwing";
        default: return "?";
    }
}

namespace {

// weapon.kst's grip (girp_type) for each class of ours, where the table has no row for the weapon.
u8 grip_for(WeaponClass c) {
    switch (c) {
        case WeaponClass::Knife: return 9;
        case WeaponClass::Pistol: return 1;
        case WeaponClass::Shotgun: return 7;
        case WeaponClass::MachineGun: return 3;
        case WeaponClass::Grenade: return 10;
        default: return 2;                           // rifles, sniper rifles, full-size SMGs
    }
}

WeaponDef base_for(WeaponClass c) {
    WeaponDef w;
    w.klass = c;
    w.grip = grip_for(c);
    switch (c) {
        case WeaponClass::Rifle:
            break;
        case WeaponClass::Smg:
            w.damage = 26, w.head_multiplier = 3.6f, w.leg_multiplier = 0.8f, w.range = 6000, w.falloff_start = 1800, w.falloff_min = 0.6f;
            w.rpm = 850, w.reserve = 120, w.reload_time = 2.1f, w.draw_time = 0.45f;
            w.spread_stand = 0.4f, w.spread_crouch = 0.26f, w.spread_move = 1.8f, w.spread_per_shot = 0.15f,
            w.spread_max = 3.6f, w.spread_recover = 8;
            w.recoil_up = 0.28f, w.recoil_up_max = 4.4f, w.recoil_side = 0.2f, w.recoil_recover = 16, w.move_speed = 1.0f;
            break;
        case WeaponClass::Sniper:
            w.damage = 95, w.head_multiplier = 2.5f, w.leg_multiplier = 0.6f, w.range = 14000, w.falloff_start = 8000, w.falloff_min = 0.9f;
            w.rpm = 50, w.automatic = false, w.magazine = 10, w.reserve = 30, w.reload_time = 3.2f, w.draw_time = 0.9f;
            w.spread_stand = 4, w.spread_crouch = 3, w.spread_move = 8, w.spread_per_shot = 1.2f, w.spread_max = 8,
            w.spread_recover = 3;
            w.recoil_up = 1.6f, w.recoil_up_max = 8, w.recoil_side = 0.3f, w.recoil_recover = 8, w.move_speed = 0.85f, w.scoped = true;
            break;
        case WeaponClass::MachineGun:
            w.damage = 34, w.head_multiplier = 3.4f, w.leg_multiplier = 0.8f, w.falloff_start = 3500, w.rpm = 650, w.magazine = 100,
            w.reserve = 200, w.reload_time = 5, w.draw_time = 1;
            w.spread_stand = 0.6f, w.spread_crouch = 0.3f, w.spread_move = 3.5f, w.spread_per_shot = 0.2f,
            w.spread_max = 4.5f, w.spread_recover = 6;
            w.recoil_up = 0.45f, w.recoil_up_max = 7, w.recoil_side = 0.3f, w.recoil_recover = 11, w.move_speed = 0.8f;
            break;
        case WeaponClass::Shotgun:
            w.damage = 22, w.head_multiplier = 2, w.leg_multiplier = 0.85f, w.range = 3000, w.falloff_start = 700, w.falloff_min = 0.25f,
            w.pellets = 8, w.rpm = 90, w.automatic = false, w.magazine = 8, w.reserve = 32, w.reload_time = 3.4f, w.draw_time = 0.7f;
            w.spread_stand = 2.6f, w.spread_crouch = 2, w.spread_move = 4, w.spread_per_shot = 0.6f, w.spread_max = 5,
            w.spread_recover = 5;
            w.recoil_up = 1.4f, w.recoil_up_max = 7, w.recoil_side = 0.3f, w.recoil_recover = 9, w.move_speed = 0.9f;
            break;
        case WeaponClass::Pistol:
            w.damage = 28, w.head_multiplier = 4, w.leg_multiplier = 0.8f, w.range = 4500, w.falloff_start = 1500, w.falloff_min = 0.6f,
            w.rpm = 400, w.automatic = false, w.magazine = 15, w.reserve = 60, w.reload_time = 1.8f, w.draw_time = 0.35f;
            w.spread_stand = 0.5f, w.spread_crouch = 0.32f, w.spread_move = 1.6f, w.spread_per_shot = 0.35f,
            w.spread_max = 3, w.spread_recover = 9;
            w.recoil_up = 0.5f, w.recoil_up_max = 4, w.recoil_side = 0.25f, w.recoil_recover = 15, w.move_speed = 1.05f;
            break;
        case WeaponClass::Knife:
            w.damage = 55, w.head_multiplier = 1.6f, w.leg_multiplier = 1, w.range = 150, w.rpm = 120, w.automatic = false, w.magazine = 0,
            w.reserve = 0, w.draw_time = 0.3f, w.move_speed = 1.1f, w.melee_range = 150;
            break;
        case WeaponClass::Grenade:
            w.damage = 120, w.range = 420, w.rpm = 60, w.automatic = false, w.magazine = 1, w.reserve = 1, w.draw_time = 0.5f,
            w.move_speed = 1.05f, w.grenade = GrenadeKind::Frag, w.fuse = 2.2f, w.blast_radius = 450;
            break;
        default:
            break;
    }
    return w;
}

Slot slot_for(WeaponClass c) {
    switch (c) {
        case WeaponClass::Pistol:
        case WeaponClass::Shotgun: return Slot::Secondary;   // Soldier Front carries shotguns as sidearms
        case WeaponClass::Knife: return Slot::Melee;
        case WeaponClass::Grenade: return Slot::Throw;
        default: return Slot::Primary;
    }
}

struct Row {
    u16 id;              // the weapon's number on the wire, fixed here (NM-4): never a row's place
    const char* code;
    const char* model;   // the weapon archive folder (bhw/sf_a_<model>)
    const char* name;
    WeaponClass klass;
    u32 price;
    bool admin;          // staff variant
    const char* skin;    // texture laid over the gun, or null
};

// The Soldier Front roster: 41 regular weapons and throwables, 13 more the client has its own model
// of (codes are weapon.kst's, prices on the shop's ladder for the class), 17 admin-only variants,
// and every melee the client ships. Codes and prices are SF_ItemList.ini's where it lists them, the
// ijji shop's otherwise. Nothing outside this table is offered, carried or accepted (the WarRock guns
// that once followed it were retired on 2026-10-02: Game/RetiredWeapons.inl). Each row's first number
// is its id: a new row takes the next unused one, a removed row's is never given out again, and every
// id stays under kFirstPackWeapon (a server's own weapons are numbered from there).
const Row kRows[] = {
    // Rifles
    {0, "A013", "ak74", "AK-74", WeaponClass::Rifle, 33000, false, nullptr},
    {1, "A019", "an94", "AN94", WeaponClass::Rifle, 55000, false, nullptr},
    {2, "A027", "famas", "FA-MAS", WeaponClass::Rifle, 58000, false, nullptr},
    {3, "B101", "fn_fal", "FN FAL", WeaponClass::Rifle, 50000, false, nullptr},
    {4, "A005", "g36c", "G36C", WeaponClass::Rifle, 23000, false, nullptr},
    {5, "B102", "g3a3", "G3A3", WeaponClass::Rifle, 55000, false, nullptr},
    {6, "A025", "galill", "GALIL", WeaponClass::Rifle, 53000, false, nullptr},
    {7, "A011", "k2", "K2", WeaponClass::Rifle, 35000, false, nullptr},
    {8, "B103", "l85a2", "L85A2", WeaponClass::Rifle, 50000, false, nullptr},
    {9, "A007", "m16", "M16A2", WeaponClass::Rifle, 30000, false, nullptr},
    {10, "A006", "m4a1", "M4A1", WeaponClass::Rifle, 22500, false, nullptr},
    {11, "B104", "evl_scar_h", "SCAR-H", WeaponClass::Rifle, 50000, false, nullptr},
    {12, "A024", "sig551", "SIG551", WeaponClass::Rifle, 45000, false, nullptr},
    {13, "A008", "aug", "Steyr AUG", WeaponClass::Rifle, 25000, false, nullptr},
    {14, "B105", "type89", "TYPE89", WeaponClass::Rifle, 48000, false, nullptr},
    {15, "A168", "black_ak47s", "AK47s", WeaponClass::Rifle, 52000, false, nullptr},
    {16, "A277", "hk416", "HK416", WeaponClass::Rifle, 54000, false, nullptr},
    {17, "A501", "stg44", "STG44", WeaponClass::Rifle, 48000, false, nullptr},
    // SMGs
    {18, "A018", "k1", "K1", WeaponClass::Smg, 29000, false, nullptr},
    {19, "B201", "k7", "K7", WeaponClass::Smg, 52000, false, nullptr},
    {20, "A004", "mp5", "MP5", WeaponClass::Smg, 20000, false, nullptr},
    {21, "B202", "mp7a1", "MP7A1", WeaponClass::Smg, 32000, false, nullptr},
    {22, "A012", "p90", "P90", WeaponClass::Smg, 25000, false, nullptr},
    {23, "B203", "ump45", "UMP45", WeaponClass::Smg, 42000, false, nullptr},
    {24, "B204", "uzi", "UZI", WeaponClass::Smg, 53000, false, nullptr},
    {25, "A167", "mac10", "MAC-10", WeaponClass::Smg, 34000, false, nullptr},
    {26, "A456", "thompson", "Thompson M1A1", WeaponClass::Smg, 44000, false, nullptr},
    {27, "A543", "kriss", "KRISS Super V", WeaponClass::Smg, 50000, false, nullptr},
    // Sniper rifles
    {28, "B301", "awp", "AWP", WeaponClass::Sniper, 54000, false, nullptr},
    {29, "A030", "dragunov", "Dragunov", WeaponClass::Sniper, 55000, false, nullptr},
    {30, "A021", "frf2", "FR-F2", WeaponClass::Sniper, 33000, false, nullptr},
    {31, "B302", "m110", "M110 SASS", WeaponClass::Sniper, 46000, false, nullptr},
    {32, "A009", "psg1", "PSG-1", WeaponClass::Sniper, 20000, false, nullptr},
    {33, "A153", "wa2000", "WA2000", WeaponClass::Sniper, 52000, false, nullptr},
    {34, "A307", "dsr1", "DSR-1", WeaponClass::Sniper, 56000, false, nullptr},
    {35, "A175", "m200", "CheyTac M200", WeaponClass::Sniper, 58000, false, nullptr},
    // Machine guns
    {36, "A031", "m249", "M249", WeaponClass::MachineGun, 56000, false, nullptr},
    {37, "A014", "mg36", "MG36", WeaponClass::MachineGun, 21000, false, nullptr},
    {38, "A227", "m134", "Gatling Gun", WeaponClass::MachineGun, 60000, false, nullptr},
    // Pistols
    {39, "A003", "beretta", "Beretta M92F", WeaponClass::Pistol, 16000, false, nullptr},
    {40, "A028", "deserteagle", "Desert Eagle", WeaponClass::Pistol, 45000, false, nullptr},
    {41, "A002", "glock23", "Glock23", WeaponClass::Pistol, 15000, false, nullptr},
    {42, "B401", "m945c", "M945C", WeaponClass::Pistol, 28000, false, nullptr},
    {43, "A017", "colt45", "Colt45", WeaponClass::Pistol, 18000, false, nullptr},
    {44, "A269", "luger", "Luger", WeaponClass::Pistol, 26000, false, nullptr},
    // Shotguns
    {45, "A020", "benellim", "Benelli M1", WeaponClass::Shotgun, 38000, false, nullptr},
    {46, "B501", "desperado", "Desperado", WeaponClass::Shotgun, 39000, false, nullptr},
    {47, "A022", "m870", "Remington M870", WeaponClass::Shotgun, 43000, false, nullptr},
    {48, "A211", "spas12", "SPAS-12", WeaponClass::Shotgun, 42000, false, nullptr},
    // Grenades and utility
    {49, "A016", "m18", "AN-M8 Smoke", WeaponClass::Grenade, 700, false, nullptr},
    {50, "A015", "flashbang", "Flashbang", WeaponClass::Grenade, 1500, false, nullptr},
    {51, "A010", "m67", "M67 Grenade", WeaponClass::Grenade, 2000, false, nullptr},
    {52, "A026", "rgd5", "RGD-5", WeaponClass::Grenade, 2500, false, nullptr},
    {53, "A029", "vx", "VX Grenade", WeaponClass::Grenade, 2000, false, nullptr},
    // Melee: every one the client ships
    {54, "K001", "m9", "M9 Knife", WeaponClass::Knife, 0, false, nullptr},
    {55, "K002", "ax_hatchet", "Hatchet", WeaponClass::Knife, 30000, false, nullptr},
    {56, "K003", "machete", "Machete", WeaponClass::Knife, 25000, false, nullptr},
    {57, "K004", "jungle_machete", "Jungle Machete", WeaponClass::Knife, 28000, false, nullptr},
    {58, "K005", "shovel", "Shovel", WeaponClass::Knife, 22000, false, nullptr},
    {59, "K006", "large_scissors", "Large Scissors", WeaponClass::Knife, 26000, false, nullptr},
    {60, "K007", "dragon_claw", "Dragon Claw", WeaponClass::Knife, 35000, false, nullptr},
    {61, "K008", "shark_m10", "Shark M10", WeaponClass::Knife, 32000, false, nullptr},
    {62, "K009", "flintlock_sword_m", "Flintlock Sword", WeaponClass::Knife, 36000, false, nullptr},
    {63, "K010", "7th_knife_2004", "7th Anniversary Knife (2004)", WeaponClass::Knife, 24000, false, nullptr},
    {64, "K011", "7th_knife_2006", "7th Anniversary Knife (2006)", WeaponClass::Knife, 24000, false, nullptr},
    // Admin-only variants
    {65, "S001", "alcad_ak74", "ALCAD AK74", WeaponClass::Rifle, 0, true, nullptr},
    {66, "S002", "alcad_m4a1", "ALCAD M4A1", WeaponClass::Rifle, 0, true, nullptr},
    {67, "S003", "cutie_m4a1", "Cutie M4A1", WeaponClass::Rifle, 0, true, nullptr},
    {68, "S004", "dragon_m4a1", "Dragon M4A1", WeaponClass::Rifle, 0, true, nullptr},
    {69, "S005", "engraving_ak74", "Engraving AK74", WeaponClass::Rifle, 0, true, nullptr},
    {70, "S006", "engraving_psg1", "Engraving PSG-1", WeaponClass::Sniper, 0, true, nullptr},
    {71, "S007", "ak47s", "Gold AK47S", WeaponClass::Rifle, 0, true, "sf_a_gold_ak47s"},
    {72, "S008", "gold_ak74", "Gold AK74", WeaponClass::Rifle, 0, true, nullptr},
    {73, "S009", "gold_an94", "Gold AN94", WeaponClass::Rifle, 0, true, nullptr},
    {74, "S010", "gold_desperado", "Gold Desperado", WeaponClass::Shotgun, 0, true, nullptr},
    {75, "S011", "gold_dragunov", "Gold Dragunov", WeaponClass::Sniper, 0, true, nullptr},
    {76, "S012", "gold_galil", "Gold GALIL", WeaponClass::Rifle, 0, true, nullptr},
    {77, "S013", "gold_m4a1", "Gold M4A1", WeaponClass::Rifle, 0, true, nullptr},
    {78, "S014", "gold_mg36", "Gold MG36", WeaponClass::MachineGun, 0, true, nullptr},
    {79, "S015", "gold_scar_h", "Gold SCAR-H", WeaponClass::Rifle, 0, true, nullptr},
    {80, "S016", "gold_uzi", "Gold UZI", WeaponClass::Smg, 0, true, nullptr},
    {81, "S017", "limited_beretta", "Ltd. Beretta M92F", WeaponClass::Pistol, 0, true, nullptr},
};

// Soldier Front's own numbers for its weapons (weapon.kst, recoil.kst), in Tools/sf_weapons.py's order.
struct SfRow {
    const char* model;
    float weapon_speed;
    int kst_class, grip;
    const char* carried;
    float dome_default, dome_inc, dome_max, dome_dec;   // degrees; _dec each tenth of a second
    float moving_max, moving_inc;                       // degrees; _inc each tenth of a second
    float add_run, add_jump, add_walk;
    float zoom;
    const char* scope_image;
    float scope_move;
    float incline, limit_width;
    int damage_type;                                    // DAMAGE_CAL_TYPE: 2 a sniper rifle's, 1 any other gun's
    float damage;
};

const SfRow kSfRows[] = {
#include "Game/SfWeapons.inl"
};

const SfRow* sf_row(std::string_view model) {
    for (const SfRow& r : kSfRows)
        if (model == r.model) return &r;
    return nullptr;
}

// The tables' handling over a weapon. The cone is taken only from a gun's row (knives and grenades
// have none); the move speed, the clips and the carried model from every row.
void apply_sf_row(WeaponDef& w, const SfRow& r) {
    w.move_speed = r.weapon_speed;
    w.grip = u8(r.grip);
    w.carried = r.carried;
    w.run_spread = r.add_run, w.air_spread = r.add_jump, w.walk_spread = r.add_walk;
    // A scope where the table gives a magnification (sniper rifles without one fall back to the class's).
    if (r.zoom > 0) {
        w.scoped = true, w.zoom = r.zoom, w.scope_image = r.scope_image, w.scope_move = r.scope_move > 0 ? r.scope_move : 1.5f;
    } else if (w.scoped) {
        w.zoom = 8, w.scope_image = "scope2.bmp";
    } else {
        w.scoped = false;
    }
    // A sniper rifle's hit, as the table has it: 200 to 250 (the AWP's and the M200's 250, the
    // Dragunov's 230, the DSR-1's 220) against a soldier's 100, so one to the body or an arm brings him
    // down through the most armour a force and its parts wear (a third off at most). The legs take
    // a third of it: a hit there does not, alone. Our own 72 to 115 took two (players, 2026-10-04:
    // "snipers still isn't shooting enough damage"). The other guns' DAMAGE (the M4A1's 60, the AK-74's
    // 80) is reckoned some other way the table does not say, and is not read.
    if (r.damage_type == 2 && r.damage > 0) w.damage = r.damage, w.leg_multiplier = 0.35f;
    if (r.dome_max <= 0 && r.dome_default <= 0) return;
    // The kick: the view climbs INCLINE a shot and wanders sideways within IMPACT_LIMIT_WIDTH.
    if (r.incline > 0) w.recoil_up = r.incline;
    if (r.limit_width > 0) w.recoil_side = r.limit_width * 0.06f, w.recoil_side_max = r.limit_width * 0.5f;
    w.spread_stand = r.dome_default;
    w.spread_crouch = r.dome_default * 0.7f;
    w.spread_per_shot = r.dome_inc;
    w.spread_max = std::max(r.dome_max, r.dome_default);
    w.spread_recover = r.dome_dec * 10.0f;
    w.spread_move = r.moving_max;
    w.spread_move_rate = r.moving_inc * 10.0f;
}

// Every gun's cone and kick a little wider and harder than the tables have them (players,
// 2026-10-05: "loosen up the fire spread and recoil by 5%"): the cone at rest, crouched, moving,
// each shot's step and its widest, and the view's kick up and sideways with their limits. How fast
// each settles back is left as it is.
constexpr float kLooseHandling = 1.05f;

void loosen(WeaponDef& w, float k) {
    w.spread_stand *= k, w.spread_crouch *= k, w.spread_move *= k, w.spread_per_shot *= k, w.spread_max *= k;
    w.recoil_up *= k, w.recoil_up_max *= k, w.recoil_side *= k, w.recoil_side_max *= k;
}

}  // namespace

std::span<const WeaponDef> weapons() {
    static const std::vector<WeaponDef> table = [] {
        std::vector<WeaponDef> out;
        for (const Row& r : kRows) {
            WeaponDef w = base_for(r.klass);
            w.id = r.id;
            w.code = r.code;
            w.model = r.model;
            w.name = r.name;
            w.slot = slot_for(r.klass);
            w.price = r.price;
            w.admin_only = r.admin;
            w.shop = !r.admin && r.price > 0;
            if (r.skin) w.skin = r.skin;
            // A few that Soldier Front players knew apart.
            const std::string m = r.model;
            if (m == "flashbang") w.grenade = GrenadeKind::Flash, w.damage = 0, w.blast_radius = 2200, w.fuse = 1.6f;
            if (m == "m18") w.grenade = GrenadeKind::Smoke, w.damage = 0, w.blast_radius = 340, w.fuse = 1.8f;
            if (m == "vx") w.grenade = GrenadeKind::Gas, w.damage = 8, w.blast_radius = 380, w.fuse = 1.8f;
            if (m == "rgd5") w.fuse = 1.6f;   // "a shorter fuse ... explodes faster than the M67"
            // Variants handle like the gun they dress up.
            auto is = [&](const char* base) { return m == base || m.ends_with(std::string("_") + base); };
            if (is("ak74") || is("an94") || is("ak47s") || is("g3a3") || is("fn_fal") || is("scar_h")) w.damage = 36, w.recoil_up = 0.46f;
            if (is("m4a1") || is("g36c")) w.rpm = 780;
            if (m == "m16") w.rpm = 650, w.damage = 34;
            if (m == "famas") w.rpm = 900;
            if (m == "deserteagle") w.damage = 48, w.magazine = 7, w.reserve = 35, w.rpm = 240;
            if (m == "awp") w.double_zoom = true;
            if (is("psg1") || is("dragunov") || m == "m110") w.rpm = 180;
            // Five rounds a magazine, thirty spare: weapon.kst's ROUND and BULLET for every PSG-1
            // (players, 2026-10-05: "the PSG-1 should only have 5 bullets, not 10").
            if (is("psg1")) w.magazine = 5, w.reserve = 30;
            if (m == "m945c") w.magazine = 8, w.reserve = 40, w.damage = 34;
            if (is("desperado")) w.magazine = 6, w.reserve = 30;
            if (m == "shovel" || m == "ax_hatchet" || m == "large_scissors") w.damage = 65, w.rpm = 90;
            if (m == "hk416") w.rpm = 750;
            if (m == "stg44") w.rpm = 550, w.damage = 38;
            if (m == "mac10") w.rpm = 1000, w.damage = 24;
            if (m == "thompson") w.rpm = 700, w.damage = 30, w.magazine = 30;
            if (m == "kriss") w.rpm = 1100, w.damage = 25, w.magazine = 25, w.reserve = 125;
            if (m == "wa2000") w.rpm = 180;
            if (m == "m134") w.rpm = 1100, w.damage = 28, w.magazine = 200, w.reserve = 200, w.reload_time = 6, w.draw_time = 1.4f;
            if (m == "colt45") w.magazine = 7, w.reserve = 35, w.damage = 34;
            if (m == "luger") w.magazine = 8, w.reserve = 40, w.damage = 30;
            // The client's own tables: move speed, the cone and how it is carried.
            if (const SfRow* sf = sf_row(m)) apply_sf_row(w, *sf);
            if (w.klass == WeaponClass::Knife || w.klass == WeaponClass::Grenade) w.move_speed = std::max(w.move_speed, kLightCarry);
            else loosen(w, kLooseHandling);
            out.push_back(std::move(w));
        }
        return out;
    }();
    return table;
}

const WeaponDef* weapon(u16 id) {
    // By the table's own ids (NM-4), so a gap a removed row leaves never shifts another gun.
    static const std::vector<const WeaponDef*> by_id = [] {
        std::vector<const WeaponDef*> out;
        for (const WeaponDef& w : weapons()) {
            if (w.id >= kFirstPackWeapon) continue;
            if (w.id >= out.size()) out.resize(size_t(w.id) + 1, nullptr);
            out[w.id] = &w;
        }
        return out;
    }();
    if (id < by_id.size() && by_id[id]) return by_id[id];
    return id >= kFirstPackWeapon && id != kNoWeapon ? pack_weapon(id) : nullptr;
}

const WeaponDef* weapon_by_model(std::string_view model) {
    for (const WeaponDef& w : weapons())
        if (eng::str::iequals(w.model, model)) return &w;
    return nullptr;
}

const WeaponDef* weapon_by_code(std::string_view code) {
    for (const WeaponDef& w : weapons())
        if (eng::str::iequals(w.code, code)) return &w;
    return nullptr;
}

namespace {

const char* const kWeaponIdsV5[] = {
#include "Game/WeaponIdsV5.inl"
};

const RetiredWeapon kRetired[] = {
#include "Game/RetiredWeapons.inl"
};

}  // namespace

const RetiredWeapon* retired_weapon(std::string_view code) {
    for (const RetiredWeapon& r : kRetired)
        if (eng::str::iequals(code, r.code)) return &r;
    return nullptr;
}

std::string_view saved_weapon_code(std::string_view token) {
    token = eng::str::trim(token);
    if (!token.empty() && std::all_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        int index = 0;
        if (!eng::str::parse_int(token, index) || index < 0 || size_t(index) >= std::size(kWeaponIdsV5)) return {};
        return kWeaponIdsV5[index];
    }
    return token;
}

u16 weapon_from_saved(std::string_view token) {
    const std::string_view code = saved_weapon_code(token);
    if (code.empty() || code == "-") return kNoWeapon;
    if (const WeaponDef* w = weapon_by_code(code)) return w->id;
    if (const RetiredWeapon* r = retired_weapon(code); r && r->model)
        if (const WeaponDef* w = weapon_by_model(r->model)) return w->id;
    return kNoWeapon;
}

const char* clan_rank_name(ClanRank r) {
    switch (r) {
        case ClanRank::Owner: return "Owner";
        case ClanRank::CoOwner: return "Co-Owner";
        case ClanRank::Lieutenant: return "Lieutenant";
        case ClanRank::Member: return "Member";
        default: return "Recruit";
    }
}

ClanRank clan_needs(ClanPower what) {
    switch (what) {
        case ClanPower::Notice:
        case ClanPower::Emblem:
        case ClanPower::Joining: return ClanRank::CoOwner;
        case ClanPower::Transfer:
        case ClanPower::Disband: return ClanRank::Owner;
        default: return ClanRank::Lieutenant;
    }
}

bool clan_may(ClanRank me, ClanPower what, ClanRank them) {
    if (me < clan_needs(what)) return false;
    switch (what) {
        case ClanPower::Kick: return me > them;
        // Up to the rank below your own: an owner makes co-owners, a co-owner lieutenants, a
        // lieutenant members of recruits.
        case ClanPower::Promote: return me > them && u8(them) + 1 < u8(me);
        case ClanPower::Demote: return me > them && them > ClanRank::Recruit;
        default: return true;
    }
}

Loadout starter_loadout() {
    auto id = [](const char* m) { const WeaponDef* w = weapon_by_model(m); return w ? w->id : kNoWeapon; };
    // What the first shop visit used to buy with the starting SP: a rifle, a pistol, a grenade.
    return {id("m4a1"), id("glock23"), id("m9"), id("m67"), kNoWeapon, kNoWeapon};
}

bool issued(u16 weapon_id) {
    static const Loadout kit = starter_loadout();
    return weapon_id != kNoWeapon && std::find(kit.begin(), kit.end(), weapon_id) != kit.end();
}

size_t equip_cell(const Loadout& kit, const WeaponDef& w) {
    if (w.slot != Slot::Throw) return size_t(w.slot);
    for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c)
        if (kit[c] == w.id) return c;
    for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c)
        if (kit[c] == kNoWeapon) return c;
    return kFirstThrowCell;
}

void sanitize_loadout(Loadout& kit) {
    for (size_t c = 0; c < kLoadoutSlots; ++c) {
        if (kit[c] == kNoWeapon) continue;
        const WeaponDef* w = weapon(kit[c]);
        bool twice = false;
        for (size_t o = kFirstThrowCell; o < c; ++o) twice |= throw_cell(c) && kit[o] == kit[c];
        if (!w || !fits_cell(w->slot, c) || twice) kit[c] = kNoWeapon;
    }
}

const SpecialInfo& special_info(Special s) {
    static const SpecialInfo kTable[size_t(Special::Count)] = {
        {"Be the first to kill an enemy", 30},
        {"Kill 3 enemies in a round", 50},
        {"Make 2 kills in a round by using secondary weapon", 50},
        {"Achieve a knife kill during a single round", 40},
        {"Kill all enemy", 100},
        {"Win round within 1 minute", 20},
        {"Love shot", 30},
        {"Achieve 20 kills", 100},
        {"Make round score 7:7", 50},
    };
    return kTable[size_t(s) < size_t(Special::Count) ? size_t(s) : 0];
}

int side_change_round(Mode mode, Mission mission, int goal) {
    if (mode != Mode::TeamBattle || goal < 2) return 0;
    if (mission != Mission::Blast && mission != Mission::Capture && mission != Mission::Escape) return 0;
    return goal - 1;
}

u32 match_xp(int kills, int deaths, bool won, int minutes, bool death_penalty) {
    const int base = 60 * std::max(1, minutes) + kills * 30 - (death_penalty ? deaths * 5 : 0) + (won ? 300 : 100);
    return u32(std::max(20, base));
}

namespace {
// SFSound.xml's RadioMessage list, in its MessageID order: 111-117 command (Z), 121-126 general
// (X), 101-106 reply (C), and 91, said by itself when a grenade is thrown. The words are what the
// voices say (radio_eng/x, c, z and auto).
struct RadioEntry {
    const char* text;
    const char* sound;   // SFSound.xml's name for it
};
const RadioEntry kRadio[4][8] = {
    {{"Go go go!", "RadioMessage_MoveOut"},
     {"Fall back!", "RadioMessage_FallBack"},
     {"Move quietly.", "RadioMessage_MoveQuietly"},
     {"Need backup!", "RadioMessage_NeedSupport"},
     {"Circle around the back.", "RadioMessage_AttackingTheBack"},
     {"Stick together.", "RadioMessage_TogetherTeam"},
     {"Follow me.", "RadioMessage_FollowMe"},
     {nullptr, nullptr}},
    {{"Danger!", "RadioMessage_Danger"},
     {"Enemy spotted!", "RadioMessage_EnemyOnSight"},
     {"Enemy down.", "RadioMessage_EnemyDown"},
     {"Man down!", "RadioMessage_ManDown"},
     {"Reached the target.", "RadioMessage_TargetAccquierd"},
     {"Back home.", "RadioMessage_BackHome"},
     {nullptr, nullptr}},
    {{"Affirmative.", "RadioMessage_Affirmative"},
     {"Negative.", "RadioMessage_Negative"},
     {"Ready.", "RadioMessage_IamReady"},
     {"On my way.", "RadioMessage_OnMyWay"},
     {"Thanks.", "RadioMessage_Thanks"},
     {"Sorry.", "RadioMessage_Sorry"},
     {nullptr, nullptr}},
    {{"Fire in the hole!", "RadioMessage_FireIntheHole"}, {nullptr, nullptr}},
};
}  // namespace

int radio_count(u8 group) {
    if (group > kRadioAuto) return 0;
    int n = 0;
    while (n < 8 && kRadio[group][n].text) ++n;
    return n;
}

const char* radio_line(u8 group, u8 line) { return line < radio_count(group) ? kRadio[group][line].text : ""; }

const char* radio_sound(u8 group, u8 line) { return line < radio_count(group) ? kRadio[group][line].sound : ""; }

const char* radio_group_name(u8 group) {
    switch (group) {
        case 0: return "Command";
        case 1: return "General";
        case 2: return "Reply";
        case kRadioAuto: return "Auto";
        default: return "";
    }
}

u32 match_sp(int kills, bool won, int minutes) { return u32(std::max(50, 40 * std::max(1, minutes) + kills * 20 + (won ? 250 : 80))); }

}  // namespace lsf
