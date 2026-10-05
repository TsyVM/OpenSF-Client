// The staff panel's Events and Rewards tabs (Screens/Staff.cpp): a Game Master edits the server's
// rewards and events (Game/Events.hpp) in the game, and Save sends the whole of them; the server
// checks every number again and uses them at once. A Moderator sees them and cannot change them.
//
//   Events    the calendar: every event (a season's every year, a weekend's every week, one of
//             the staff's own), made from a template, its window, its pay, its box, its quests
//   Rewards   what each box holds and how often; the hours of play; the week of signing in; the
//             pool the day's quests are drawn from; the day itself
#include "Game/Screens/Screens.hpp"

#include "Game/App.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cstdlib>
#include <map>

namespace lsf {

using namespace proto;

namespace {

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kDim = VAN_COL32(150, 150, 142, 255);
constexpr VanU32 kLime = VAN_COL32(170, 220, 70, 255);
constexpr VanU32 kWarn = VAN_COL32(240, 190, 70, 255);
constexpr VanU32 kBad = VAN_COL32(235, 90, 70, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);
constexpr float kRowH = 24;

void label(float x0, float y, float x1, std::string_view s, VanU32 c = kDim) { ui::text_at(x0, y, x1, y + 19, s, c, ui::Align::Left, true, 12.0f); }

// A number typed into a kit edit box. While it is being typed the text is the typist's; the value
// is taken whenever the text reads as a number, held to lo..hi. Returns true when it changed.
std::map<int, std::string> g_texts;
int g_typing = -1;
bool number(int key, float x0, float y, float x1, u32& v, u32 lo, u32 hi, bool enabled) {
    std::string& t = g_texts[key];
    if (g_typing != key) t = std::to_string(v);
    (void)ui::edit_at(key, x0, y, x1, y + 19, t, 9, nullptr, false, enabled);
    if (VanGui::IsItemActive()) g_typing = key;
    else if (g_typing == key) g_typing = -1;
    if (g_typing != key || t.empty()) return false;
    char* end = nullptr;
    const unsigned long n = std::strtoul(t.c_str(), &end, 10);
    if (!end || *end) return false;
    const u32 c = u32(std::clamp<unsigned long>(n, lo, hi));
    if (c == v) return false;
    v = c;
    return true;
}
template <class T>
bool number_of(int key, float x0, float y, float x1, T& v, u32 lo, u32 hi, bool enabled) {
    u32 w = u32(v);
    const bool ch = number(key, x0, y, x1, w, lo, hi, enabled);
    v = T(w);
    return ch;
}

// A UTC date and time typed as "2026-12-20 00:00"; red until it reads as one.
bool date(int key, float x0, float y, float x1, u64& v, bool enabled) {
    std::string& t = g_texts[key];
    if (g_typing != key) t = utc_text(v);
    u64 parsed = 0;
    const bool ok = parse_utc(t, parsed);
    (void)ui::edit_at(key, x0, y, x1, y + 19, t, 16, "YYYY-MM-DD HH:MM", false, enabled, ok ? VAN_COL32(0, 0, 0, 170) : VAN_COL32(90, 20, 16, 200));
    if (VanGui::IsItemActive()) g_typing = key;
    else if (g_typing == key) g_typing = -1;
    if (g_typing != key || !parse_utc(t, parsed) || parsed == v) return false;
    v = parsed;
    return true;
}

// Steps through a box kind, or none (kNoBox).
bool box_arrows(int key, float x0, float y, float x1, u8& box, bool allow_none, bool enabled) {
    const int n = kBoxKinds + (allow_none ? 1 : 0);
    int at = box < kBoxKinds ? int(box) + (allow_none ? 1 : 0) : 0;
    const std::string name = box < kBoxKinds ? box_info(box).name : "None";
    const int d = ui::arrows(key, x0, y, x1, y + 19, name, enabled);
    if (!d) return false;
    at = (at + d + n) % n;
    box = allow_none ? (at == 0 ? kNoBox : u8(at - 1)) : u8(at);
    return true;
}

template <class T>
bool enum_arrows(int key, float x0, float y, float x1, u8& v, int count, T name_of, bool enabled) {
    const int d = ui::arrows(key, x0, y, x1, y + 19, name_of(v), enabled);
    if (!d) return false;
    v = u8((int(v) + d + count) % count);
    return true;
}

std::string span_text(u64 s) {
    if (s >= 2 * 86400) return eng::str::format("%u days", unsigned(s / 86400));
    if (s >= 3600) return eng::str::format("%u h %u min", unsigned(s / 3600), unsigned(s / 60 % 60));
    return eng::str::format("%u min", unsigned(s / 60));
}

VanU32 theme_ink(u8 t) {
    switch (EventTheme(t)) {
        case EventTheme::Christmas: return VAN_COL32(232, 84, 72, 255);
        case EventTheme::Halloween: return VAN_COL32(240, 146, 40, 255);
        case EventTheme::Easter: return VAN_COL32(180, 156, 236, 255);
        case EventTheme::Valentine: return VAN_COL32(240, 110, 170, 255);
        case EventTheme::Summer: return VAN_COL32(80, 200, 236, 255);
        case EventTheme::NewYear: return VAN_COL32(236, 200, 96, 255);
        case EventTheme::Weekend: return VAN_COL32(120, 170, 255, 255);
        default: return kLime;
    }
}

// The copy being edited: taken from the server's whenever there are no changes of our own.
RewardsConfig* editing(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!st.staff_cfg_asked) s.staff_request_rewards(), st.staff_cfg_asked = true;
    if (s.staff_rewards && (!st.staff_cfg || !st.staff_cfg_dirty)) st.staff_cfg = s.staff_rewards->config;
    return st.staff_cfg ? &*st.staff_cfg : nullptr;
}

// Save / Undo along the bottom, with what is waiting.
void save_bar(App& app, float x0, float y, float x1, bool gm) {
    ScreenState& st = app.state();
    Session& s = app.session();
    ui::fill_at(x0, y - 6, x1, y - 5, kRule);
    if (!gm) {
        label(x0, y + 4, x1, "Only a Game Master can change these. What you see is what the server uses.");
        return;
    }
    if (ui::text_button(10990, x1 - 170, y, x1, y + 26, "Save to the server", st.staff_cfg_dirty, "Every player's from now on")) {
        s.staff_save_rewards(*st.staff_cfg);
        st.staff_cfg_dirty = false;
    }
    if (ui::text_button(10991, x1 - 300, y, x1 - 176, y + 26, "Undo changes", st.staff_cfg_dirty, "Back to what the server has")) {
        st.staff_cfg_dirty = false;
        st.staff_cfg.reset();
        g_typing = -1;
        s.staff_request_rewards();
    }
    label(x0, y + 4, x1 - 310, st.staff_cfg_dirty ? "Changes not saved yet." : "As the server has them. Times are UTC.", st.staff_cfg_dirty ? kWarn : kDim);
}

}  // namespace

// ── Events ─────────────────────────────────────────────────────────────────────

void staff_events_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const bool gm = s.profile.game_master();
    RewardsConfig* cfg = editing(app);
    if (!cfg) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, "Asking the server...", kDim, ui::Align::Center);
        return;
    }
    bool& dirty = st.staff_cfg_dirty;
    const u64 now = s.staff_rewards ? s.staff_rewards->now + u64(std::max(0.0, app.now() - s.staff_rewards_at)) : 0;
    std::vector<EventDef>& events = cfg->events;
    st.staff_event_sel = std::clamp(st.staff_event_sel, events.empty() ? -1 : 0, int(events.size()) - 1);

    // The calendar, running first.
    const float LX1 = X0 + 300, bottom = Y1 - 40;
    ui::heading(X0, Y0, LX1, "CALENDAR");
    const int clicked = ui::rows(10000, X0, Y0 + 22, LX1, bottom - 64, int(events.size()), 38, st.staff_event_sel,
                                 [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
                                     const EventDef& e = events[size_t(i)];
                                     const bool runs = event_window(e, now);
                                     ui::text_at(x0 + 8, y0 + 2, x1 - 60, y0 + 19, e.name, e.enabled ? theme_ink(e.theme) : kDim, ui::Align::Left, true, 13.0f);
                                     if (runs) ui::text_at(x1 - 70, y0 + 2, x1 - 6, y0 + 19, "RUNNING", kLime, ui::Align::Right, true, 10.0f);
                                     else if (!e.enabled) ui::text_at(x1 - 70, y0 + 2, x1 - 6, y0 + 19, "OFF", kDim, ui::Align::Right, true, 10.0f);
                                     int y, m, d, h, mi, y2, m2, d2;
                                     utc_date(e.start, y, m, d, h, mi);
                                     utc_date(e.end, y2, m2, d2, h, mi);
                                     static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                                     const std::string when = e.repeat == u8(EventRepeat::Weekly)
                                                                  ? "every week, " + span_text(e.end - e.start)
                                                                  : eng::str::format("%s%d %s - %d %s", e.repeat == u8(EventRepeat::Yearly) ? "every year, " : "", d, mon[m - 1], d2, mon[m2 - 1]);
                                     ui::text_at(x0 + 8, y0 + 19, x1 - 6, y1 - 1, when, kDim, ui::Align::Left, false, 11.0f);
                                 });
    if (clicked >= 0) st.staff_event_sel = clicked, g_typing = -1;
    // A new one from a template, a copy, or none.
    float by = bottom - 58;
    const int d = ui::arrows(10001, X0, by, X0 + 190, by + 19, std::string("Template: ") + event_theme_name(u8(st.staff_template)), gm);
    if (d) st.staff_template = (st.staff_template + d + int(EventTheme::Count)) % int(EventTheme::Count);
    if (ui::text_button(10002, X0 + 196, by - 2, LX1, by + 21, "New event", gm && events.size() < 64, "From the template, dated for its next season")) {
        EventDef e = event_template(EventTheme(st.staff_template), now ? now : utc_time(2026, 10, 1));
        u32 id = 1;
        for (const EventDef& o : events) id = std::max(id, o.id + 1);
        e.id = id;
        events.push_back(std::move(e));
        st.staff_event_sel = int(events.size()) - 1, dirty = true, g_typing = -1;
    }
    by += 26;
    const bool has = st.staff_event_sel >= 0;
    if (ui::text_button(10003, X0, by, X0 + 145, by + 23, "Copy", gm && has && events.size() < 64)) {
        EventDef e = events[size_t(st.staff_event_sel)];
        u32 id = 1;
        for (const EventDef& o : events) id = std::max(id, o.id + 1);
        e.id = id, e.name += " (copy)";
        events.push_back(std::move(e));
        st.staff_event_sel = int(events.size()) - 1, dirty = true, g_typing = -1;
    }
    const bool armed = app.now() - st.staff_delete_armed < 3.0;
    if (ui::text_button(10004, X0 + 155, by, LX1, by + 23, armed ? "Sure? Delete" : "Delete", gm && has)) {
        if (armed) events.erase(events.begin() + st.staff_event_sel), st.staff_event_sel--, dirty = true, st.staff_delete_armed = -10, g_typing = -1;
        else st.staff_delete_armed = app.now();
    }

    // The event in hand.
    const float RX0 = LX1 + 18, RX1 = X1, CX = RX0 + 92;
    if (!has || st.staff_event_sel >= int(events.size())) {
        ui::text_at(RX0, Y0 + 80, RX1, Y0 + 100, events.empty() ? "No events. Make one from a template." : "Pick an event to edit it.", kDim, ui::Align::Center);
        save_bar(app, X0, Y1 - 28, X1, gm);
        return;
    }
    EventDef& e = events[size_t(st.staff_event_sel)];
    const int k = 10100;
    float y = Y0;
    ui::heading(RX0, y, RX1, "THE EVENT");
    y += 24;
    label(RX0, y, CX, "Name");
    {
        const std::string before = e.name;
        (void)ui::edit_at(k + 1, CX, y, RX1, y + 19, e.name, 40, "Christmas", false, gm);
        dirty |= e.name != before;
    }
    y += kRowH;
    label(RX0, y, CX, "Banner");
    {
        const std::string before = e.banner;
        (void)ui::edit_at(k + 2, CX, y, RX1, y + 19, e.banner, 160, "the lobby's line while it runs", false, gm);
        dirty |= e.banner != before;
    }
    y += kRowH;
    const float mid = (CX + RX1) * 0.5f;
    label(RX0, y, CX, "Theme");
    dirty |= enum_arrows(k + 3, CX, y, mid - 8, e.theme, int(EventTheme::Count), event_theme_name, gm);
    label(mid, y, mid + 60, "Repeats");
    dirty |= enum_arrows(k + 4, mid + 62, y, RX1, e.repeat, int(EventRepeat::Count), event_repeat_name, gm);
    y += kRowH;
    label(RX0, y, CX, "Starts");
    dirty |= date(k + 5, CX, y, mid - 8, e.start, gm);
    label(mid, y, mid + 60, "Ends");
    dirty |= date(k + 6, mid + 62, y, RX1, e.end, gm);
    y += kRowH;
    {
        // Its next run, as the server's clock has it.
        u64 f = 0, t = 0;
        std::string when;
        VanU32 c = kDim;
        if (!e.enabled) when = "Switched off.";
        else if (e.end <= e.start) when = "It ends before it starts.", c = kBad;
        else if (event_window(e, now, &f, &t)) when = "Running now: ends in " + span_text(t - now) + ".", c = kLime;
        else if (const u64 next = event_next(e, now)) when = "Next runs " + utc_text(next) + " UTC, in " + span_text(next - now) + ".";
        else when = "It will not run again.", c = kWarn;
        label(CX, y - 2, RX1, when, c);
    }
    y += kRowH;
    label(RX0, y, CX, "SP %");
    dirty |= number_of(k + 7, CX, y, CX + 70, e.sp_pct, 100, 500, gm);
    label(CX + 80, y, CX + 140, "XP %");
    dirty |= number_of(k + 8, CX + 140, y, CX + 210, e.xp_pct, 100, 500, gm);
    label(CX + 220, y, RX1, "100: as usual; 200: double", kDim);
    y += kRowH;
    label(RX0, y, CX, "Drops");
    dirty |= box_arrows(k + 9, CX, y, mid + 30, e.drop_box, true, gm);
    dirty |= number_of(k + 10, mid + 40, y, mid + 100, e.drop_permille, 0, 1000, gm);
    label(mid + 106, y, RX1, eng::str::format("in 1,000 (%.1f%%)", double(e.drop_permille) / 10.0));
    y += kRowH;
    label(RX0, y, CX, "Sign-in SP");
    dirty |= number_of(k + 11, CX, y, CX + 90, e.sign_in_sp, 0, 100000, gm);
    label(CX + 100, y, mid + 60, "more for the day's sign-in");
    {
        bool on = e.enabled;
        if (ui::check(k + 12, mid + 70, y, "Switched on", on, gm)) e.enabled = on, dirty = true;
    }
    y += kRowH + 6;
    ui::heading(RX0, y, RX1, "ITS QUESTS  (added to the day's while it runs)");
    y += 22;
    for (size_t i = 0; i < e.quests.size(); ++i) {
        QuestDef& q = e.quests[i];
        const int qk = k + 20 + int(i) * 6;
        dirty |= enum_arrows(qk, RX0, y, RX0 + 170, q.goal, int(QuestGoal::Count), quest_goal_name, gm);
        dirty |= number_of(qk + 1, RX0 + 176, y, RX0 + 230, q.target, 1, 9999, gm);
        dirty |= number_of(qk + 2, RX0 + 236, y, RX0 + 300, q.sp, 0, 100000, gm);
        label(RX0 + 304, y, RX0 + 330, "SP");
        const std::string before = q.text;
        (void)ui::edit_at(qk + 3, RX0 + 332, y, RX1 - 30, y + 19, q.text, 80, quest_text(QuestDef{0, q.goal, q.target}).c_str(), false, gm);
        dirty |= q.text != before;
        if (ui::text_button(qk + 4, RX1 - 24, y, RX1, y + 19, "x", gm, "Take it off")) {
            e.quests.erase(e.quests.begin() + std::ptrdiff_t(i)), dirty = true, g_typing = -1;
            break;
        }
        y += kRowH;
    }
    if (e.quests.size() < 6 && ui::text_button(k + 60, RX0, y, RX0 + 130, y + 21, "Add a quest", gm)) {
        QuestDef q;
        q.goal = u8(QuestGoal::Kills), q.target = 20, q.sp = 1000;
        e.quests.push_back(q), dirty = true;
    }
    if (e.quests.empty()) label(RX0 + 140, y + 1, RX1, "none: the day's three as usual");
    save_bar(app, X0, Y1 - 28, X1, gm);
}

// ── Rewards ────────────────────────────────────────────────────────────────────

void staff_rewards_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const bool gm = s.profile.game_master();
    RewardsConfig* cfg = editing(app);
    if (!cfg) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, "Asking the server...", kDim, ui::Align::Center);
        return;
    }
    bool& dirty = st.staff_cfg_dirty;
    static const char* subs[] = {"Boxes", "Play time", "Signing in", "Quests", "The day"};
    for (int t = 0; t < 5; ++t)
        if (ui::tab_button(10200 + t, X0 + float(t) * 112, Y0 - 4, X0 + 106 + float(t) * 112, Y0 + 20, subs[t], st.staff_rewards_sub == t))
            st.staff_rewards_sub = t, g_typing = -1;
    float y = Y0 + 32;
    const int k = 10300;
    switch (st.staff_rewards_sub) {
        case 0: {
            // A box's table: each line's kind, weight (its chance against the others'), its amount.
            const float LX1 = X0 + 190;
            const int pick = ui::rows(k, X0, y, LX1, Y1 - 50, kBoxKinds, 28, st.staff_box_sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
                ui::text_at(x0 + 8, y0, x1 - 4, y1, box_info(u8(i)).name, kInk, ui::Align::Left, true, 12.0f);
            });
            if (pick >= 0) st.staff_box_sel = pick, g_typing = -1;
            st.staff_box_sel = std::clamp(st.staff_box_sel, 0, kBoxKinds - 1);
            LootTable* t = nullptr;
            for (LootTable& lt : cfg->boxes)
                if (lt.box == u8(st.staff_box_sel)) t = &lt;
            if (!t) break;
            const float RX0 = LX1 + 16;
            ui::heading(RX0, y, X1, std::string(box_info(u8(st.staff_box_sel)).name) + ":  " + box_info(u8(st.staff_box_sel)).about);
            y += 24;
            const float c_kind = RX0, c_w = RX0 + 136, c_ch = RX0 + 190, c_lo = RX0 + 240, c_hi = RX0 + 318, c_m = RX0 + 396;
            label(c_kind, y, c_w, "Prize"), label(c_w, y, c_ch, "Weight"), label(c_ch, y, c_lo, "Chance"), label(c_lo, y, c_hi, "From"), label(c_hi, y, c_m, "To");
            label(c_m, y, X1, "Words (a part's or a boost's name; empty: the usual)");
            y += 22;
            u32 total = 0;
            for (const Prize& p : t->prizes) total += p.weight;
            for (size_t i = 0; i < t->prizes.size(); ++i) {
                Prize& p = t->prizes[i];
                const int pk = k + 20 + int(i) * 8;
                dirty |= enum_arrows(pk, c_kind, y, c_w - 6, p.kind, int(PrizeKind::Count), prize_kind_name, gm);
                dirty |= number_of(pk + 1, c_w, y, c_ch - 6, p.weight, 1, 1000, gm);
                label(c_ch, y, c_lo, eng::str::format("%.1f%%", 100.0 * double(p.weight) / double(std::max(1u, total))), kInk);
                dirty |= number(pk + 2, c_lo, y, c_hi - 6, p.lo, 0, 1000000, gm);
                dirty |= number(pk + 3, c_hi, y, c_m - 6, p.hi, 0, 1000000, gm);
                if (p.kind == u8(PrizeKind::Box)) {
                    u8 b = u8(std::max(0, box_by_key(p.match)));
                    if (box_arrows(pk + 4, c_m, y, X1 - 30, b, false, gm)) p.match = box_info(b).key, dirty = true;
                } else {
                    const std::string before = p.match;
                    (void)ui::edit_at(pk + 5, c_m, y, X1 - 30, y + 19, p.match, 80, p.kind == u8(PrizeKind::Weapon) ? "(no words: any weapon)" : "", false,
                                      gm && (p.kind == u8(PrizeKind::Part) || p.kind == u8(PrizeKind::Boost)));
                    dirty |= p.match != before;
                }
                if (ui::text_button(pk + 6, X1 - 24, y, X1, y + 19, "x", gm && t->prizes.size() > 1, "Take it off")) {
                    t->prizes.erase(t->prizes.begin() + std::ptrdiff_t(i)), dirty = true, g_typing = -1;
                    break;
                }
                label(c_lo, y + 18, X1, prize_text(p), VAN_COL32(120, 120, 114, 255));
                y += kRowH + 14;
            }
            if (t->prizes.size() < 16 && ui::text_button(k + 200, RX0, y, RX0 + 130, y + 21, "Add a prize", gm))
                t->prizes.push_back({u8(PrizeKind::Sp), 10, 500, 1000, {}}), dirty = true;
            label(RX0 + 140, y + 1, X1, "SP, XP: an amount. Boost, part: days. Weapon: the most it may cost (0: any), then SP if every one is owned.");
            break;
        }
        case 1: {
            ui::heading(X0, y, X1, "HOURS OF PLAY  (minutes of matches played to their end, each step once a day)");
            y += 24;
            label(X0, y, X0 + 100, "Minutes"), label(X0 + 110, y, X0 + 200, "SP"), label(X0 + 210, y, X0 + 400, "and a box");
            y += 22;
            for (size_t i = 0; i < cfg->play.size(); ++i) {
                PlayStep& p = cfg->play[i];
                const int pk = k + 20 + int(i) * 4;
                dirty |= number_of(pk, X0, y, X0 + 100, p.minutes, 5, 1440, gm);
                dirty |= number(pk + 1, X0 + 110, y, X0 + 200, p.sp, 0, 100000, gm);
                dirty |= box_arrows(pk + 2, X0 + 210, y, X0 + 440, p.box, true, gm);
                if (ui::text_button(pk + 3, X0 + 450, y, X0 + 474, y + 19, "x", gm, "Take it off")) {
                    cfg->play.erase(cfg->play.begin() + std::ptrdiff_t(i)), dirty = true, g_typing = -1;
                    break;
                }
                y += kRowH;
            }
            if (cfg->play.size() < 6 && ui::text_button(k + 200, X0, y, X0 + 130, y + 21, "Add a step", gm))
                cfg->play.push_back({u16(cfg->play.empty() ? 60 : cfg->play.back().minutes + 60), 500, kNoBox}), dirty = true;
            label(X0, y + 34, X1, "Saved in order of their minutes.");
            break;
        }
        case 2: {
            ui::heading(X0, y, X1, "A WEEK OF SIGNING IN  (the next day's pay each day a soldier signs in; then the week again)");
            y += 24;
            for (size_t i = 0; i < cfg->week.size(); ++i) {
                SignInDay& d = cfg->week[i];
                const int pk = k + 20 + int(i) * 4;
                label(X0, y, X0 + 60, eng::str::format("Day %zu", i + 1), kInk);
                dirty |= number(pk, X0 + 70, y, X0 + 160, d.sp, 0, 100000, gm);
                label(X0 + 166, y, X0 + 196, "SP");
                dirty |= box_arrows(pk + 1, X0 + 200, y, X0 + 430, d.box, true, gm);
                y += kRowH;
            }
            break;
        }
        case 3: {
            ui::heading(X0, y, X1, eng::str::format("THE QUEST POOL  (%zu; %u are drawn for each soldier each day, by weight)", cfg->quests.size(), unsigned(cfg->quests_a_day)));
            y += 22;
            const int per_page = 13, pages = std::max(1, (int(cfg->quests.size()) + per_page - 1) / per_page);
            st.staff_quest_page = std::clamp(st.staff_quest_page, 0, pages - 1);
            label(X0, y, X0 + 170, "Goal"), label(X0 + 176, y, X0 + 230, "Target"), label(X0 + 236, y, X0 + 300, "SP"), label(X0 + 306, y, X0 + 370, "XP");
            label(X0 + 376, y, X0 + 430, "Weight"), label(X0 + 436, y, X1, "Its own words (empty: the goal's)");
            y += 22;
            const size_t first = size_t(st.staff_quest_page * per_page);
            for (size_t i = first; i < cfg->quests.size() && i < first + per_page; ++i) {
                QuestDef& q = cfg->quests[i];
                const int qk = k + 20 + int(i - first) * 8;
                dirty |= enum_arrows(qk, X0, y, X0 + 170, q.goal, int(QuestGoal::Count), quest_goal_name, gm);
                dirty |= number_of(qk + 1, X0 + 176, y, X0 + 230, q.target, 1, 9999, gm);
                dirty |= number(qk + 2, X0 + 236, y, X0 + 300, q.sp, 0, 100000, gm);
                dirty |= number(qk + 3, X0 + 306, y, X0 + 370, q.xp, 0, 100000, gm);
                dirty |= number_of(qk + 4, X0 + 376, y, X0 + 430, q.weight, 1, 1000, gm);
                const std::string before = q.text;
                (void)ui::edit_at(qk + 5, X0 + 436, y, X1 - 30, y + 19, q.text, 80, quest_text(QuestDef{0, q.goal, q.target}).c_str(), false, gm);
                dirty |= q.text != before;
                if (ui::text_button(qk + 6, X1 - 24, y, X1, y + 19, "x", gm && cfg->quests.size() > size_t(cfg->quests_a_day), "Take it out of the pool")) {
                    cfg->quests.erase(cfg->quests.begin() + std::ptrdiff_t(i)), dirty = true, g_typing = -1;
                    break;
                }
                y += kRowH;
            }
            const float by = Y1 - 76;
            if (cfg->quests.size() < 48 && ui::text_button(k + 200, X0, by, X0 + 130, by + 21, "Add a quest", gm)) {
                QuestDef q;
                u16 id = 1;
                for (const QuestDef& o : cfg->quests) id = std::max<u16>(id, u16(o.id + 1));
                q.id = id, q.goal = u8(QuestGoal::Kills), q.target = 20, q.sp = 500;
                cfg->quests.push_back(q), dirty = true;
                st.staff_quest_page = (int(cfg->quests.size()) - 1) / per_page;
            }
            if (pages > 1) {
                if (ui::text_button(k + 201, X1 - 230, by, X1 - 150, by + 21, "Previous", st.staff_quest_page > 0)) --st.staff_quest_page, g_typing = -1;
                label(X1 - 144, by + 1, X1 - 86, eng::str::format("%d / %d", st.staff_quest_page + 1, pages), kInk);
                if (ui::text_button(k + 202, X1 - 80, by, X1, by + 21, "Next", st.staff_quest_page + 1 < pages)) ++st.staff_quest_page, g_typing = -1;
            }
            break;
        }
        default: {
            ui::heading(X0, y, X1, "THE DAY");
            y += 26;
            label(X0, y, X0 + 220, "Quests drawn a day");
            dirty |= number_of(k + 20, X0 + 230, y, X0 + 290, cfg->quests_a_day, 1, 5, gm);
            y += kRowH;
            label(X0, y, X0 + 220, "All of them done pays");
            dirty |= number(k + 21, X0 + 230, y, X0 + 320, cfg->all_quests_sp, 0, 100000, gm);
            label(X0 + 326, y, X0 + 356, "SP");
            dirty |= box_arrows(k + 22, X0 + 360, y, X0 + 590, cfg->all_quests_box, true, gm);
            y += kRowH;
            label(X0, y, X0 + 220, "A day begins at (UTC)");
            dirty |= number_of(k + 23, X0 + 230, y, X0 + 290, cfg->day_starts, 0, 23, gm);
            label(X0 + 296, y, X1, ":00. The quests, the hours and the sign-in turn over then.");
            y += kRowH + 12;
            label(X0, y, X1, "A promotion's duffle bag is by the rank reached: A up to Sergeant, B the sergeants above, C the lieutenants and");
            label(X0, y + 18, X1, "captains, D Major and up. What each holds is under Boxes. Docs/Rewards.md has the arithmetic behind the defaults.");
            break;
        }
    }
    save_bar(app, X0, Y1 - 28, X1, gm);
}

}  // namespace lsf
