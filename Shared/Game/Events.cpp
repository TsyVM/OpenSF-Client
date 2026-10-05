#include "Game/Events.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace lsf {

// ── Boxes ──────────────────────────────────────────────────────────────────────

namespace {
const BoxInfo kBoxes[kBoxKinds] = {
    {"duffle_a", "Duffle Bag (A)", "dufflebag_a_lb.bmp", "A promotion up to Sergeant."},
    {"duffle_b", "Duffle Bag (B)", "dufflebag_b_lb.bmp", "A promotion from Staff Sergeant to Master Sergeant."},
    {"duffle_c", "Duffle Bag (C)", "dufflebag_c_lb.bmp", "A promotion through the lieutenants and captains."},
    {"duffle_d", "Duffle Bag (D)", "dufflebag_d_lb.bmp", "A promotion from Major up."},
    {"gift", "Gift Box", "gift_box_lb.bmp", "Christmas and the year's other events; the day's quests, all done."},
    {"horror", "Horror Gift Box", "horror gift box_lb.bmp", "Halloween."},
    {"rabbit", "Rabbit Gift Box", "rabbit_giftbox_lb.bmp", "Easter."},
    {"pirate", "Pirate Gift Box", "gift_box_pirate_lb.bmp", "Summer."},
    {"devil", "Black Devil Gift Box", "blackdevil giftbox_lb.bmp", "Valentine's."},
    {"tiger", "White Tiger Lucky Bag", "sf_o_gsg9_pockettiger_LB.tga", "The new year."},
};

const char* kPrizeKinds[] = {"sp", "xp", "boost", "part", "weapon", "box"};
const char* kGoals[] = {"kills", "headshots", "wins", "matches", "minutes", "earn_sp", "multi_kills", "knife_kills", "grenade_kills",
                        "pistol_kills", "rifle_kills", "smg_kills", "sniper_kills", "shotgun_kills", "mg_kills"};
const char* kGoalNames[] = {"Kills", "Head shots", "Wins", "Matches", "Minutes played", "SP earned", "Multi kills", "Knife kills",
                            "Grenade kills", "Pistol kills", "Rifle kills", "SMG kills", "Sniper kills", "Shotgun kills", "Machine gun kills"};
const char* kRepeats[] = {"once", "weekly", "yearly"};
const char* kRepeatNames[] = {"Once", "Every week", "Every year"};
const char* kThemes[] = {"none", "christmas", "halloween", "easter", "valentine", "summer", "new_year", "anniversary", "weekend"};
const char* kThemeNames[] = {"Its own", "Christmas", "Halloween", "Easter", "Valentine's", "Summer", "New Year", "Anniversary", "Weekend"};
static_assert(std::size(kPrizeKinds) == size_t(PrizeKind::Count));
static_assert(std::size(kGoals) == size_t(QuestGoal::Count) && std::size(kGoalNames) == size_t(QuestGoal::Count));
static_assert(std::size(kRepeats) == size_t(EventRepeat::Count) && std::size(kThemes) == size_t(EventTheme::Count));

// The boosts a box gives when it names none: the ones that help a fight or a payout, not a name.
constexpr const char* kUsualBoosts = "Points X2|Double UP|Quick weapon Switch|Headshot Points|Holy Bless|Prevent Team Kill";

template <size_t N>
int index_of(const char* const (&table)[N], std::string_view key) {
    for (size_t i = 0; i < N; ++i)
        if (eng::str::iequals(table[i], key)) return int(i);
    return -1;
}

using eng::str::thousands;
}  // namespace

const BoxInfo& box_info(u8 kind) { return kBoxes[kind < kBoxKinds ? kind : 0]; }

int box_by_key(std::string_view key) {
    for (int i = 0; i < kBoxKinds; ++i)
        if (eng::str::iequals(kBoxes[i].key, key)) return i;
    return -1;
}

BoxKind duffle_for_rank(int rank) {
    if (rank <= 4) return BoxKind::DuffleA;    // Private 2nd Class .. Sergeant
    if (rank <= 19) return BoxKind::DuffleB;   // Staff Sergeant .. Master Sergeant
    if (rank <= 34) return BoxKind::DuffleC;   // 2nd Lieutenant .. Captain
    return BoxKind::DuffleD;                   // Major ..
}

const char* prize_kind_name(u8 kind) {
    static const char* names[] = {"SP", "XP", "Boost item", "Character part", "Weapon", "Box"};
    return kind < std::size(names) ? names[kind] : "?";
}

std::string prize_text(const Prize& p) {
    auto range = [&](const char* unit) {
        return p.lo == p.hi ? thousands(p.lo) + unit : thousands(p.lo) + " - " + thousands(p.hi) + unit;
    };
    const std::string words = p.match.empty() ? std::string() : " (" + eng::str::format("%s", p.match.c_str()) + ")";
    switch (PrizeKind(p.kind)) {
        case PrizeKind::Sp: return range(" SP");
        case PrizeKind::Xp: return range(" XP");
        case PrizeKind::Boost: return "a boost item for " + range(p.hi == 1 ? " day" : " days") + (p.match.empty() ? std::string() : words);
        case PrizeKind::Part: return "a character part for " + range(p.hi == 1 ? " day" : " days") + words;
        case PrizeKind::Weapon: return std::string("a gun to keep") + (p.lo ? ", up to " + thousands(p.lo) + " SP" : std::string());
        case PrizeKind::Box: {
            const int b = box_by_key(p.match);
            return range(p.hi == 1 ? " " : " x ") + (b >= 0 ? kBoxes[b].name : "box");
        }
        default: return "?";
    }
}

// ── Quests ─────────────────────────────────────────────────────────────────────

const char* quest_goal_name(u8 goal) { return goal < std::size(kGoalNames) ? kGoalNames[goal] : "?"; }

std::string quest_text(const QuestDef& q) {
    if (!q.text.empty()) return q.text;
    const unsigned n = q.target;
    switch (QuestGoal(q.goal)) {
        case QuestGoal::Kills: return eng::str::format("Get %u kills", n);
        case QuestGoal::Headshots: return eng::str::format("Get %u head shot kills", n);
        case QuestGoal::Wins: return eng::str::format("Win %u match%s", n, n == 1 ? "" : "es");
        case QuestGoal::Matches: return eng::str::format("Play %u match%s to the end", n, n == 1 ? "" : "es");
        case QuestGoal::Minutes: return eng::str::format("Play for %u minutes", n);
        case QuestGoal::EarnSp: return eng::str::format("Earn %s SP in matches", thousands(n).c_str());
        case QuestGoal::MultiKills: return eng::str::format("Get %u multi kills", n);
        case QuestGoal::KnifeKills: return eng::str::format("Get %u kills with a knife", n);
        case QuestGoal::GrenadeKills: return eng::str::format("Get %u kills with grenades", n);
        case QuestGoal::PistolKills: return eng::str::format("Get %u kills with a pistol", n);
        case QuestGoal::RifleKills: return eng::str::format("Get %u kills with a rifle", n);
        case QuestGoal::SmgKills: return eng::str::format("Get %u kills with an SMG", n);
        case QuestGoal::SniperKills: return eng::str::format("Get %u kills with a sniper rifle", n);
        case QuestGoal::ShotgunKills: return eng::str::format("Get %u kills with a shotgun", n);
        case QuestGoal::MachineGunKills: return eng::str::format("Get %u kills with a machine gun", n);
        default: return "?";
    }
}

u32 quest_count(const QuestDef& q, const QuestCounts& c) {
    switch (QuestGoal(q.goal)) {
        case QuestGoal::Kills: return c.kills;
        case QuestGoal::Headshots: return c.headshots;
        case QuestGoal::Wins: return c.wins;
        case QuestGoal::Matches: return c.matches;
        case QuestGoal::Minutes: return c.minutes;
        case QuestGoal::EarnSp: return c.sp;
        case QuestGoal::MultiKills: return c.multi_kills;
        case QuestGoal::KnifeKills: return c.knife;
        case QuestGoal::GrenadeKills: return c.grenade;
        case QuestGoal::PistolKills: return c.pistol;
        case QuestGoal::RifleKills: return c.rifle;
        case QuestGoal::SmgKills: return c.smg;
        case QuestGoal::SniperKills: return c.sniper;
        case QuestGoal::ShotgunKills: return c.shotgun;
        case QuestGoal::MachineGunKills: return c.machine_gun;
        default: return 0;
    }
}

// ── The calendar (UTC) ─────────────────────────────────────────────────────────

namespace {
// Days from 1970-01-01 to a civil date, and back (Howard Hinnant's algorithms).
i64 days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const i64 era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = unsigned((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + i64(doe) - 719468;
}
void civil_from_days(i64 z, int& y, int& m, int& d) {
    z += 719468;
    const i64 era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(i64(yoe) + era * 400 + (m <= 2));
}
constexpr u64 kDay = 86400, kWeek = 7 * kDay;
}  // namespace

u64 utc_time(int year, int month, int day, int hour, int minute) {
    const i64 days = days_from_civil(year, month, day);
    return u64(std::max<i64>(0, days * i64(kDay) + hour * 3600 + minute * 60));
}

void utc_date(u64 t, int& year, int& month, int& day, int& hour, int& minute) {
    civil_from_days(i64(t / kDay), year, month, day);
    const u64 s = t % kDay;
    hour = int(s / 3600), minute = int(s / 60 % 60);
}

std::string utc_text(u64 t) {
    int y, m, d, h, mi;
    utc_date(t, y, m, d, h, mi);
    return eng::str::format("%04d-%02d-%02d %02d:%02d", y, m, d, h, mi);
}

bool parse_utc(std::string_view text, u64& out) {
    int y = 0, m = 0, d = 0, h = 0, mi = 0;
    const std::string s(eng::str::trim(text));
    const int n = std::sscanf(s.c_str(), "%d-%d-%d %d:%d", &y, &m, &d, &h, &mi);
    if (n < 3 || y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return false;
    out = utc_time(y, m, d, h, mi);
    return true;
}

u32 reward_day(u64 t, u8 day_starts) {
    const u64 shift = u64(std::min<u8>(day_starts, 23)) * 3600;
    return u32((t >= shift ? t - shift : 0) / kDay);
}
u64 reward_day_start(u32 day, u8 day_starts) { return u64(day) * kDay + u64(std::min<u8>(day_starts, 23)) * 3600; }

// ── Events ─────────────────────────────────────────────────────────────────────

const char* event_repeat_name(u8 r) { return r < std::size(kRepeatNames) ? kRepeatNames[r] : "?"; }
const char* event_theme_name(u8 t) { return t < std::size(kThemeNames) ? kThemeNames[t] : "?"; }

namespace {
// A yearly event's run that begins in `year`.
bool yearly_run(const EventDef& e, int year, u64& from, u64& to) {
    int y, m, d, h, mi;
    utc_date(e.start, y, m, d, h, mi);
    if (m == 2 && d == 29) d = 28;   // a leap day keeps to the 28th in other years
    from = utc_time(year, m, d, h, mi);
    to = from + (e.end - e.start);
    return true;
}
}  // namespace

bool event_window(const EventDef& e, u64 now, u64* from, u64* to) {
    if (!e.enabled || e.end <= e.start) return false;
    u64 f = 0, t = 0;
    switch (EventRepeat(e.repeat)) {
        case EventRepeat::Weekly: {
            if (now < e.start || e.end - e.start > kWeek) return false;
            const u64 k = (now - e.start) / kWeek;
            f = e.start + k * kWeek, t = f + (e.end - e.start);
            break;
        }
        case EventRepeat::Yearly: {
            int y, m, d, h, mi;
            utc_date(now, y, m, d, h, mi);
            if (e.end - e.start > 366 * kDay) return false;
            bool hit = false;
            for (int year : {y, y - 1}) {
                yearly_run(e, year, f, t);
                if (now >= f && now < t) {
                    hit = true;
                    break;
                }
            }
            if (!hit) return false;
            break;
        }
        default: f = e.start, t = e.end; break;
    }
    if (now < f || now >= t) return false;
    if (from) *from = f;
    if (to) *to = t;
    return true;
}

u64 event_next(const EventDef& e, u64 now) {
    if (!e.enabled || e.end <= e.start) return 0;
    switch (EventRepeat(e.repeat)) {
        case EventRepeat::Weekly:
            if (now < e.start) return e.start;
            return e.start + ((now - e.start) / kWeek + 1) * kWeek;
        case EventRepeat::Yearly: {
            int y, m, d, h, mi;
            utc_date(now, y, m, d, h, mi);
            u64 f, t;
            for (int year : {y, y + 1})
                if (yearly_run(e, year, f, t) && f > now) return f;
            return 0;
        }
        default: return e.start > now ? e.start : 0;
    }
}

EventDef event_template(EventTheme theme, u64 now) {
    int y, m, d, h, mi;
    utc_date(now, y, m, d, h, mi);
    EventDef e;
    e.theme = u8(theme);
    e.repeat = u8(EventRepeat::Yearly);
    auto quest = [](QuestGoal goal, u16 target, u32 sp, std::string text = {}) {
        QuestDef q;
        q.goal = u8(goal), q.target = target, q.sp = sp, q.text = std::move(text);
        return q;
    };
    switch (theme) {
        case EventTheme::Christmas:
            e.name = "Christmas";
            e.banner = "Merry Christmas, soldiers! SP x1.5 and a Gift Box chance every match, until January 3.";
            e.start = utc_time(y, 12, 20), e.end = utc_time(y + 1, 1, 3);
            e.sp_pct = 150, e.drop_box = u8(BoxKind::GiftBox), e.drop_permille = 150, e.sign_in_sp = 1000;
            e.quests = {quest(QuestGoal::Kills, 25, 1500, "Get 25 kills for the holidays")};
            break;
        case EventTheme::Halloween:
            e.name = "Halloween";
            e.banner = "Halloween: XP x1.5, and a Horror Gift Box chance every match, until November 2.";
            e.start = utc_time(y, 10, 24), e.end = utc_time(y, 11, 2);
            e.xp_pct = 150, e.drop_box = u8(BoxKind::HorrorBox), e.drop_permille = 150;
            e.quests = {quest(QuestGoal::KnifeKills, 3, 1200, "Get 3 knife kills in the dark")};
            break;
        case EventTheme::Easter:
            e.name = "Easter";
            e.banner = "Easter: a Rabbit Gift Box chance every match for two weeks.";
            e.start = utc_time(y, 4, 1), e.end = utc_time(y, 4, 15);
            e.drop_box = u8(BoxKind::RabbitBox), e.drop_permille = 150, e.sign_in_sp = 500;
            break;
        case EventTheme::Valentine:
            e.name = "Valentine's";
            e.banner = "Valentine's week: a Black Devil Gift Box chance every match.";
            e.start = utc_time(y, 2, 10), e.end = utc_time(y, 2, 17);
            e.drop_box = u8(BoxKind::DevilBox), e.drop_permille = 120;
            break;
        case EventTheme::Summer:
            e.name = "Summer";
            e.banner = "Summer: SP x1.25 and a Pirate Gift Box chance every match through July.";
            e.start = utc_time(y, 7, 1), e.end = utc_time(y, 8, 1);
            e.sp_pct = 125, e.drop_box = u8(BoxKind::PirateBox), e.drop_permille = 100;
            break;
        case EventTheme::NewYear:
            e.name = "New Year";
            e.banner = "Happy New Year! A White Tiger Lucky Bag chance every match, and more for signing in.";
            e.start = utc_time(y, 12, 31), e.end = utc_time(y + 1, 1, 8);
            e.drop_box = u8(BoxKind::TigerBox), e.drop_permille = 120, e.sign_in_sp = 1000;
            break;
        case EventTheme::Anniversary:
            e.name = "Anniversary";
            e.banner = "Happy birthday, Soldier Front Legacy: SP and XP x2 all week.";
            e.repeat = u8(EventRepeat::Once);
            e.start = utc_time(y, m, d), e.end = e.start + kWeek;
            e.sp_pct = 200, e.xp_pct = 200, e.drop_box = u8(BoxKind::GiftBox), e.drop_permille = 100;
            break;
        case EventTheme::Weekend: {
            // From this week's Saturday 00:00 to Monday 00:00, every week.
            e.name = "Weekend";
            e.banner = "Weekend: XP x1.5 until Monday.";
            e.repeat = u8(EventRepeat::Weekly);
            const i64 today = i64(now / kDay);
            const int weekday = int((today + 4) % 7);   // 1970-01-01 was a Thursday; 0 = Sunday
            e.start = u64(today + (6 - weekday)) * kDay, e.end = e.start + 2 * kDay;
            e.xp_pct = 150;
            break;
        }
        default:
            e.name = "Event";
            e.repeat = u8(EventRepeat::Once);
            e.start = (now / kDay + 1) * kDay, e.end = e.start + 3 * kDay;
            break;
    }
    return e;
}

// ── The defaults (Docs/Rewards.md has the arithmetic) ──────────────────────────

const LootTable* RewardsConfig::table(u8 box) const {
    for (const LootTable& t : boxes)
        if (t.box == box) return &t;
    return nullptr;
}

RewardsConfig default_rewards() {
    RewardsConfig c;
    auto prize = [](PrizeKind k, u16 weight, u32 lo, u32 hi, std::string match = {}) { return Prize{u8(k), weight, lo, hi, std::move(match)}; };
    using P = PrizeKind;
    auto table = [&](BoxKind b, std::vector<Prize> prizes) { c.boxes.push_back({u8(b), std::move(prizes)}); };
    // Promotions. A rank step up to Sergeant takes under a match; a sergeant's one or two; an
    // officer's from 3 to 33; a Major's 40 and more. Each bag is worth about what that wait was.
    table(BoxKind::DuffleA, {prize(P::Sp, 60, 300, 900), prize(P::Boost, 25, 1, 1), prize(P::Part, 15, 3, 3)});
    table(BoxKind::DuffleB, {prize(P::Sp, 60, 400, 1000), prize(P::Boost, 25, 1, 1), prize(P::Part, 15, 3, 3)});
    table(BoxKind::DuffleC, {prize(P::Sp, 55, 2000, 6000), prize(P::Boost, 20, 5, 5), prize(P::Part, 20, 10, 10), prize(P::Weapon, 5, 35000, 15000)});
    table(BoxKind::DuffleD, {prize(P::Sp, 45, 10000, 30000), prize(P::Boost, 20, 30, 30), prize(P::Part, 15, 30, 30), prize(P::Weapon, 15, 0, 40000),
                             prize(P::Sp, 5, 100000, 100000)});
    // The events' boxes (and the day's quests all done): the season's own parts, SP, a boost.
    // One comes every few matches while an event runs, so each is worth a match or so.
    auto gift = [&](BoxKind b, const char* parts) { table(b, {prize(P::Sp, 45, 300, 1200), prize(P::Part, 35, 3, 3, parts), prize(P::Boost, 20, 1, 1)}); };
    gift(BoxKind::GiftBox, "Santa|X-MAS");
    gift(BoxKind::HorrorBox, "Pumpkin|Scream|Skull|Angel Wings|Devil Wings|blackdevil");
    gift(BoxKind::RabbitBox, "Rabbit");
    gift(BoxKind::PirateBox, "Straw Hat|Snorkel|Sunglasses|Cowboy");
    gift(BoxKind::DevilBox, "blackdevil|Devil Wings|Angel Wings|Love");
    gift(BoxKind::TigerBox, "White Tiger");
    // An hour of matches pays, four times a day; the fourth a bag. Signing in pays every day, the
    // seventh the most.
    c.play = {{60, 300, kNoBox}, {120, 400, kNoBox}, {180, 500, kNoBox}, {240, 600, u8(BoxKind::DuffleA)}};
    c.week = {{300, kNoBox}, {400, kNoBox}, {500, kNoBox}, {600, kNoBox}, {800, kNoBox}, {1000, kNoBox}, {0, u8(BoxKind::DuffleB)}};
    // The pool the day's three are drawn from.
    auto quest = [&](u16 id, QuestGoal goal, u16 target, u32 sp, u16 weight = 10) {
        QuestDef q;
        q.id = id, q.goal = u8(goal), q.target = target, q.sp = sp, q.weight = weight;
        c.quests.push_back(q);
    };
    quest(1, QuestGoal::Kills, 20, 500, 14);
    quest(2, QuestGoal::Kills, 40, 900, 6);
    quest(3, QuestGoal::Headshots, 8, 700);
    quest(4, QuestGoal::Wins, 2, 600);
    quest(5, QuestGoal::Matches, 3, 400, 12);
    quest(6, QuestGoal::Minutes, 45, 500);
    quest(7, QuestGoal::KnifeKills, 3, 800, 6);
    quest(8, QuestGoal::GrenadeKills, 3, 700, 6);
    quest(9, QuestGoal::SniperKills, 10, 700, 8);
    quest(10, QuestGoal::PistolKills, 8, 700, 8);
    quest(11, QuestGoal::SmgKills, 15, 600, 8);
    quest(12, QuestGoal::RifleKills, 15, 500, 10);
    quest(13, QuestGoal::ShotgunKills, 10, 700, 6);
    quest(14, QuestGoal::MultiKills, 3, 800, 6);
    quest(15, QuestGoal::EarnSp, 3000, 600, 8);
    // The seasons, every year; the weekend's and the anniversary's are there to be switched on.
    const u64 now = utc_time(2026, 10, 1);
    u32 id = 1;
    for (EventTheme t : {EventTheme::NewYear, EventTheme::Valentine, EventTheme::Easter, EventTheme::Summer, EventTheme::Halloween, EventTheme::Christmas,
                         EventTheme::Weekend}) {
        EventDef e = event_template(t, now);
        e.id = id++;
        e.enabled = t == EventTheme::Christmas || t == EventTheme::Halloween || t == EventTheme::NewYear || t == EventTheme::Easter;
        c.events.push_back(std::move(e));
    }
    return c;
}

void sanitize(RewardsConfig& c) {
    // A table for every box, in order; prizes within their ranges.
    std::vector<LootTable> boxes;
    const RewardsConfig d = default_rewards();
    for (int b = 0; b < kBoxKinds; ++b) {
        const LootTable* t = c.table(u8(b));
        LootTable out = t ? *t : *d.table(u8(b));
        out.box = u8(b);
        if (out.prizes.size() > 16) out.prizes.resize(16);
        for (Prize& p : out.prizes) {
            if (p.kind >= u8(PrizeKind::Count)) p.kind = 0;
            p.weight = std::clamp<u16>(p.weight, 1, 1000);
            const u32 cap = p.kind == u8(PrizeKind::Sp) || p.kind == u8(PrizeKind::Xp) ? 1000000u
                            : p.kind == u8(PrizeKind::Box)                           ? 10u
                            : p.kind == u8(PrizeKind::Weapon)                        ? 1000000u
                                                                                     : 365u;
            p.lo = std::min(p.lo, cap), p.hi = std::min(p.hi, cap);
            if (p.kind != u8(PrizeKind::Weapon)) {
                if (p.hi < p.lo) std::swap(p.lo, p.hi);
                if ((p.kind == u8(PrizeKind::Boost) || p.kind == u8(PrizeKind::Part) || p.kind == u8(PrizeKind::Box)) && p.lo == 0) p.lo = 1;
                p.hi = std::max(p.hi, p.lo);
            }
            if (p.kind == u8(PrizeKind::Box) && box_by_key(p.match) < 0) p.match = kBoxes[int(BoxKind::GiftBox)].key;
            p.match = eng::str::sanitize_line(p.match, 80);
        }
        if (out.prizes.empty()) out.prizes.push_back({u8(PrizeKind::Sp), 1, 500, 500, {}});
        boxes.push_back(std::move(out));
    }
    c.boxes = std::move(boxes);
    if (c.play.size() > 6) c.play.resize(6);
    std::sort(c.play.begin(), c.play.end(), [](const PlayStep& a, const PlayStep& b) { return a.minutes < b.minutes; });
    for (PlayStep& s : c.play) {
        s.minutes = std::clamp<u16>(s.minutes, 5, 1440), s.sp = std::min(s.sp, 100000u);
        if (s.box >= kBoxKinds) s.box = kNoBox;
    }
    c.week.resize(7);
    for (SignInDay& s : c.week) {
        s.sp = std::min(s.sp, 100000u);
        if (s.box >= kBoxKinds) s.box = kNoBox;
    }
    if (c.quests.size() > 48) c.quests.resize(48);
    auto fix_quest = [](QuestDef& q) {
        if (q.goal >= u8(QuestGoal::Count)) q.goal = 0;
        q.target = std::max<u16>(q.target, 1);
        q.sp = std::min(q.sp, 100000u), q.xp = std::min(q.xp, 100000u);
        q.weight = std::clamp<u16>(q.weight, 1, 1000);
        q.text = eng::str::sanitize_line(q.text, 80);
    };
    // Ids: one each (a day's draw is kept by them).
    u16 next_id = 1;
    for (const QuestDef& q : c.quests) next_id = std::max<u16>(next_id, u16(q.id + 1));
    for (size_t i = 0; i < c.quests.size(); ++i) {
        QuestDef& q = c.quests[i];
        fix_quest(q);
        bool clash = q.id == 0;
        for (size_t j = 0; j < i; ++j) clash |= c.quests[j].id == q.id;
        if (clash) q.id = next_id++;
    }
    c.quests_a_day = std::clamp<u8>(c.quests_a_day, 1, 5);
    c.all_quests_sp = std::min(c.all_quests_sp, 100000u);
    if (c.all_quests_box >= kBoxKinds) c.all_quests_box = kNoBox;
    c.day_starts = std::min<u8>(c.day_starts, 23);
    if (c.events.size() > 64) c.events.resize(64);
    u32 next_event = 1;
    for (const EventDef& e : c.events) next_event = std::max(next_event, e.id + 1);
    for (size_t i = 0; i < c.events.size(); ++i) {
        EventDef& e = c.events[i];
        bool clash = e.id == 0;
        for (size_t j = 0; j < i; ++j) clash |= c.events[j].id == e.id;
        if (clash) e.id = next_event++;
        e.name = eng::str::sanitize_line(e.name, 40);
        if (e.name.empty()) e.name = "Event";
        e.banner = eng::str::sanitize_line(e.banner, 160);
        if (e.theme >= u8(EventTheme::Count)) e.theme = 0;
        if (e.repeat >= u8(EventRepeat::Count)) e.repeat = 0;
        if (e.end <= e.start) e.end = e.start + kDay;
        if (e.repeat == u8(EventRepeat::Weekly)) e.end = std::min(e.end, e.start + kWeek);
        if (e.repeat == u8(EventRepeat::Yearly)) e.end = std::min(e.end, e.start + 366 * kDay);
        e.sp_pct = std::clamp<u16>(e.sp_pct, 100, 500), e.xp_pct = std::clamp<u16>(e.xp_pct, 100, 500);
        if (e.drop_box >= kBoxKinds) e.drop_box = kNoBox;
        e.drop_permille = std::min<u16>(e.drop_permille, 1000);
        e.sign_in_sp = std::min(e.sign_in_sp, 100000u);
        if (e.quests.size() > 6) e.quests.resize(6);
        for (QuestDef& q : e.quests) fix_quest(q), q.id = 0;
    }
}

// ── rewards.cfg ────────────────────────────────────────────────────────────────

namespace {
std::string box_key(u8 b) { return b < kBoxKinds ? kBoxes[b].key : "none"; }
u8 box_from(std::string_view s) {
    const int b = box_by_key(eng::str::trim(s));
    return b < 0 ? kNoBox : u8(b);
}
u32 number(std::string_view s) {
    const std::string t(eng::str::trim(s));
    return u32(std::strtoul(t.c_str(), nullptr, 10));
}
// The first `n` words, and the rest of the line as one.
std::vector<std::string> words(std::string_view line, size_t n) {
    std::vector<std::string> out;
    std::string_view s = eng::str::trim(line);
    while (!s.empty() && out.size() < n) {
        const size_t sp = s.find_first_of(" \t");
        out.emplace_back(s.substr(0, sp));
        s = sp == std::string_view::npos ? std::string_view() : eng::str::trim(s.substr(sp));
    }
    out.emplace_back(s);
    return out;
}
std::string quest_line(const QuestDef& q) {
    return eng::str::format("%s %u %u %u %u %s", kGoals[q.goal < std::size(kGoals) ? q.goal : 0], unsigned(q.target), q.sp, q.xp, unsigned(q.weight), q.text.c_str());
}
bool quest_from(std::string_view line, QuestDef& q) {
    const auto w = words(line, 5);
    const int g = index_of(kGoals, w[0]);
    if (g < 0) return false;
    q.goal = u8(g), q.target = u16(number(w[1])), q.sp = number(w[2]), q.xp = number(w[3]), q.weight = u16(number(w[4]));
    q.text = w[5];
    return true;
}
}  // namespace

std::string rewards_text(const RewardsConfig& c) {
    eng::ConfigFile cfg;
    eng::ConfigSection& root = cfg.section("");
    root.set("day_starts", std::to_string(c.day_starts));
    root.set("quests_a_day", std::to_string(c.quests_a_day));
    root.set("all_quests_sp", std::to_string(c.all_quests_sp));
    root.set("all_quests_box", box_key(c.all_quests_box));
    for (const LootTable& t : c.boxes) {
        eng::ConfigSection s;
        s.name = "box";
        s.set("box", box_key(t.box));
        for (const Prize& p : t.prizes)
            s.values.emplace_back("prize", eng::str::format("%s %u %u %u %s", kPrizeKinds[p.kind < std::size(kPrizeKinds) ? p.kind : 0], unsigned(p.weight), p.lo, p.hi,
                                                            p.match.c_str()));
        cfg.sections.push_back(std::move(s));
    }
    {
        eng::ConfigSection s;
        s.name = "play";
        for (const PlayStep& p : c.play) s.values.emplace_back("step", eng::str::format("%u %u %s", unsigned(p.minutes), p.sp, p.box == kNoBox ? "" : box_key(p.box).c_str()));
        cfg.sections.push_back(std::move(s));
    }
    {
        eng::ConfigSection s;
        s.name = "week";
        for (const SignInDay& d : c.week) s.values.emplace_back("day", eng::str::format("%u %s", d.sp, d.box == kNoBox ? "" : box_key(d.box).c_str()));
        cfg.sections.push_back(std::move(s));
    }
    for (const QuestDef& q : c.quests) {
        eng::ConfigSection s;
        s.name = "quest";
        s.set("id", std::to_string(q.id));
        s.set("quest", quest_line(q));
        cfg.sections.push_back(std::move(s));
    }
    for (const EventDef& e : c.events) {
        eng::ConfigSection s;
        s.name = "event";
        s.set("id", std::to_string(e.id));
        s.set("name", e.name);
        s.set("banner", e.banner);
        s.set("theme", kThemes[e.theme < std::size(kThemes) ? e.theme : 0]);
        s.set("repeat", kRepeats[e.repeat < std::size(kRepeats) ? e.repeat : 0]);
        s.set("start", utc_text(e.start));
        s.set("end", utc_text(e.end));
        s.set("sp_pct", std::to_string(e.sp_pct));
        s.set("xp_pct", std::to_string(e.xp_pct));
        if (e.drop_box != kNoBox) s.set("drop", box_key(e.drop_box) + " " + std::to_string(e.drop_permille));
        if (e.sign_in_sp) s.set("sign_in_sp", std::to_string(e.sign_in_sp));
        for (const QuestDef& q : e.quests) s.values.emplace_back("quest", quest_line(q));
        s.set("enabled", e.enabled ? "true" : "false");
        cfg.sections.push_back(std::move(s));
    }
    return "# Soldier Front Legacy rewards and events. Written by the server; a Game Master edits them in the game\n"
           "# (F9, Events and Rewards). Times are UTC. Docs/Rewards.md has what each line means.\n" +
           cfg.serialize();
}

bool rewards_from_text(std::string_view text, RewardsConfig& out, std::string* why) {
    eng::ConfigFile cfg;
    if (!eng::ConfigFile::parse(text, cfg, why)) return false;
    RewardsConfig c;
    const eng::ConfigSection& root = cfg.root();
    c.day_starts = u8(root.get_int("day_starts", 0));
    c.quests_a_day = u8(root.get_int("quests_a_day", 3));
    c.all_quests_sp = u32(std::max(0, root.get_int("all_quests_sp", 800)));
    c.all_quests_box = box_from(root.get("all_quests_box", "gift"));
    for (const eng::ConfigSection* s : cfg.all("box")) {
        LootTable t;
        const int b = box_by_key(s->get("box"));
        if (b < 0) continue;
        t.box = u8(b);
        for (std::string_view line : s->get_all("prize")) {
            const auto w = words(line, 4);
            const int k = index_of(kPrizeKinds, w[0]);
            if (k < 0) continue;
            t.prizes.push_back({u8(k), u16(number(w[1])), number(w[2]), number(w[3]), w[4]});
        }
        c.boxes.push_back(std::move(t));
    }
    if (const eng::ConfigSection* s = cfg.find("play"))
        for (std::string_view line : s->get_all("step")) {
            const auto w = words(line, 2);
            c.play.push_back({u16(number(w[0])), number(w[1]), box_from(w[2])});
        }
    if (const eng::ConfigSection* s = cfg.find("week"))
        for (std::string_view line : s->get_all("day")) {
            const auto w = words(line, 1);
            c.week.push_back({number(w[0]), box_from(w[1])});
        }
    for (const eng::ConfigSection* s : cfg.all("quest")) {
        QuestDef q;
        if (!quest_from(s->get("quest"), q)) continue;
        q.id = u16(s->get_int("id", 0));
        c.quests.push_back(std::move(q));
    }
    for (const eng::ConfigSection* s : cfg.all("event")) {
        EventDef e;
        e.id = u32(std::max(0, s->get_int("id", 0)));
        e.name = s->get_string("name");
        e.banner = s->get_string("banner");
        e.theme = u8(std::max(0, index_of(kThemes, s->get("theme"))));
        e.repeat = u8(std::max(0, index_of(kRepeats, s->get("repeat"))));
        if (!parse_utc(s->get("start"), e.start) || !parse_utc(s->get("end"), e.end)) continue;
        e.sp_pct = u16(s->get_int("sp_pct", 100)), e.xp_pct = u16(s->get_int("xp_pct", 100));
        if (s->has("drop")) {
            const auto w = words(s->get("drop"), 1);
            e.drop_box = box_from(w[0]), e.drop_permille = u16(number(w[1]));
        }
        e.sign_in_sp = u32(std::max(0, s->get_int("sign_in_sp", 0)));
        for (std::string_view line : s->get_all("quest")) {
            QuestDef q;
            if (quest_from(line, q)) e.quests.push_back(std::move(q));
        }
        e.enabled = s->get_bool("enabled", true);
        c.events.push_back(std::move(e));
    }
    if (c.week.empty()) c.week = default_rewards().week;
    sanitize(c);
    out = std::move(c);
    return true;
}

}  // namespace lsf
