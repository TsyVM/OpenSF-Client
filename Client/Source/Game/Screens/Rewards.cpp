// Rewards: the lobby's Rewards plate opens the day as the server keeps it (Server/Rewards.cpp):
//
//   Today    the week of signing in (seven days, the seventh a duffle bag), and the day's minutes
//            of matches with the hours that pay
//   Quests   the day's three (and a running event's own), how far along, Collect; all of them
//            done is a bonus
//   Events   what runs now (its pay, its box, its quests) and what comes next, with the time left
//
// and the inventory's Gift tab (Screens/Shop.cpp) opens duffle bags and gift boxes: the reveal
// is here. The client asks; the server decides and says what came of it.
#include "Game/Screens/Screens.hpp"

#include "Game/App.hpp"
#include "Game/Items.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_icons.h>

#include <algorithm>
#include <cmath>

namespace lsf {

using namespace proto;

ui::Picture weapon_icon(App& app, const WeaponDef& w);   // Screens/Shop.cpp

namespace {

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kLabel = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kDim = VAN_COL32(140, 140, 132, 255);
constexpr VanU32 kLime = VAN_COL32(202, 228, 80, 255);
constexpr VanU32 kGold = VAN_COL32(236, 200, 96, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);

std::string thousands(u32 v) {
    std::string s = std::to_string(v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

// "5 h 12 min", "3 days", "40 s".
std::string span_text(u64 seconds) {
    if (seconds >= 2 * 86400) return eng::str::format("%u days", unsigned(seconds / 86400));
    if (seconds >= 3600) return eng::str::format("%u h %u min", unsigned(seconds / 3600), unsigned(seconds / 60 % 60));
    if (seconds >= 60) return eng::str::format("%u min", unsigned(seconds / 60));
    return eng::str::format("%u s", unsigned(seconds));
}

// An event's colour, by its theme (the only thing a theme changes).
VanU32 theme_ink(u8 theme) {
    switch (EventTheme(theme)) {
        case EventTheme::Christmas: return VAN_COL32(232, 84, 72, 255);
        case EventTheme::Halloween: return VAN_COL32(240, 146, 40, 255);
        case EventTheme::Easter: return VAN_COL32(180, 156, 236, 255);
        case EventTheme::Valentine: return VAN_COL32(240, 110, 170, 255);
        case EventTheme::Summer: return VAN_COL32(80, 200, 236, 255);
        case EventTheme::NewYear: return kGold;
        case EventTheme::Anniversary: return kLime;
        case EventTheme::Weekend: return VAN_COL32(120, 170, 255, 255);
        default: return kLime;
    }
}

std::string reward_text(u32 sp, u8 box) {
    std::string out = sp ? thousands(sp) + " SP" : std::string();
    if (box < kBoxKinds) out += (out.empty() ? "" : " + ") + std::string(box_info(box).name);
    return out.empty() ? std::string("-") : out;
}

// A coloured bar `fraction` full.
void bar(float x0, float y0, float x1, float y1, float fraction, VanU32 fill) {
    ui::fill_at(x0, y0, x1, y1, VAN_COL32(0, 0, 0, 150));
    if (fraction > 0) ui::fill_at(x0, y0, x0 + (x1 - x0) * std::clamp(fraction, 0.0f, 1.0f), y1, fill);
    ui::fill_at(x0, y0, x1, y0 + 1, kRule), ui::fill_at(x0, y1 - 1, x1, y1, kRule);
}

void tick_mark(float x, float y, VanU32 ink) {
    VanGui::DrawIcon(VanGui::GetWindowDrawList(), VanGui::VanIcon_Check, ui::pg(x, y), ui::pgy(14), ink);
}

void today_tab(App& app, const RewardsState& r, float X0, float Y0, float X1, double now) {
    Session& s = app.session();
    // ── The week of signing in ──
    ui::heading(X0, Y0, X1, "SIGNING IN");
    ui::text_at(X0, Y0 + 20, X1, Y0 + 36, "Every day you sign in pays; the seventh day pays the most, then the week begins again.", kDim, ui::Align::Left, true, 11.0f);
    const float cw = (X1 - X0 - 6 * 8) / 7.0f, cy = Y0 + 44;
    u32 extra = 0;
    for (const EventNow& e : r.events)
        if (e.running) extra = std::max(extra, e.sign_in_sp);
    for (int d = 0; d < 7 && d < int(r.week.size()); ++d) {
        const float x = X0 + float(d) * (cw + 8);
        // Days before the week's next are collected; today's is the next one until it is collected.
        const int today = r.signed_today ? (r.week_at + 6) % 7 : r.week_at;
        const bool done = d < r.week_at || (r.signed_today && d == today);
        const bool is_today = d == today;
        ui::well(x, cy, x + cw, cy + 92);
        if (is_today) ui::fill_at(x, cy, x + cw, cy + 2, kLime);
        ui::text_at(x, cy + 4, x + cw, cy + 20, eng::str::format("Day %d", d + 1), is_today ? kLime : kLabel, ui::Align::Center, true, 12.0f);
        const SignInDay& day = r.week[size_t(d)];
        if (day.box < kBoxKinds) {
            ui::picture_at(ui::atlas()->picture(sf::Pack::Lobby, box_info(day.box).picture), x + 4, cy + 22, x + cw - 4, cy + 62, 0xFFFFFFFF, true);
            ui::text_at(x, cy + 62, x + cw, cy + 78, box_info(day.box).name, kGold, ui::Align::Center, true, 10.0f);
        } else {
            ui::text_at(x, cy + 30, x + cw, cy + 54, thousands(day.sp), kInk, ui::Align::Center, true, 16.0f);
            ui::text_at(x, cy + 54, x + cw, cy + 70, "SP", kDim, ui::Align::Center, true, 11.0f);
        }
        if (done) tick_mark(x + cw - 12, cy + 12, kLime);
        if (is_today && !r.signed_today && extra)
            ui::text_at(x, cy + 76, x + cw, cy + 90, "+" + thousands(extra) + " event", kGold, ui::Align::Center, true, 10.0f);
    }
    const float by = cy + 100;
    if (r.signed_today)
        ui::text_at(X0, by, X1, by + 22, "Today's is collected. The next is tomorrow.", kDim, ui::Align::Left, true, 12.0f);
    else if (ui::text_button(9850, X0, by, X0 + 200, by + 24, "Collect today's", true, "Pays today's day of the week"))
        s.claim_reward(ClaimKind::SignIn), ui::click_sound();

    // ── The day's matches ──
    const float my = by + 40;
    ui::heading(X0, my, X1, "TODAY'S MATCHES");
    const u64 server = s.server_now(now);
    const u64 left = r.day_ends > server ? r.day_ends - server : 0;
    ui::text_at(X0, my + 20, X1, my + 36,
                eng::str::format("%u minutes played today (matches played to their end). The day begins again in %s.", unsigned(r.minutes), span_text(left).c_str()),
                kDim, ui::Align::Left, true, 11.0f);
    const u16 most = r.play.empty() ? 60 : std::max<u16>(60, r.play.back().minutes);
    const float bx0 = X0 + 4, bx1 = X1 - 4, bar_y = my + 46;
    bar(bx0, bar_y, bx1, bar_y + 10, float(r.minutes) / float(most), kLime);
    for (size_t i = 0; i < r.play.size(); ++i) {
        const PlayStep& st = r.play[i];
        const float x = bx0 + (bx1 - bx0) * float(st.minutes) / float(most);
        ui::fill_at(x - 1, bar_y - 4, x + 1, bar_y + 14, kInk);
        const float cx0 = std::clamp(x - 70, X0, X1 - 140), cx1 = cx0 + 140;
        ui::text_at(cx0, bar_y + 16, cx1, bar_y + 32, st.minutes % 60 == 0 ? eng::str::format("%u h", unsigned(st.minutes / 60)) : eng::str::format("%u min", unsigned(st.minutes)),
                    kLabel, ui::Align::Center, true, 11.0f);
        ui::text_at(cx0, bar_y + 32, cx1, bar_y + 48, reward_text(st.sp, st.box), st.box < kBoxKinds ? kGold : kInk, ui::Align::Center, true, 11.0f);
        const bool claimed = (r.play_claimed >> i) & 1;
        const bool ready = r.minutes >= st.minutes;
        if (claimed) {
            tick_mark((cx0 + cx1) * 0.5f, bar_y + 60, kLime);
        } else if (ui::text_button(9851 + int(i), cx0 + 30, bar_y + 50, cx1 - 30, bar_y + 72, ready ? "Collect" : "Not yet", ready,
                                   ready ? "Pays this hour" : "Play more matches to the end")) {
            s.claim_reward(ClaimKind::PlayStep, u8(i));
        }
    }
}

void quests_tab(App& app, const RewardsState& r, float X0, float Y0, float X1) {
    Session& s = app.session();
    ui::heading(X0, Y0, X1, "TODAY'S QUESTS");
    ui::text_at(X0, Y0 + 20, X1, Y0 + 36, "Drawn for you each day. A match counts toward them when it is played to its end.", kDim, ui::Align::Left, true, 11.0f);
    float y = Y0 + 44;
    size_t own = 0, own_paid = 0;
    for (size_t i = 0; i < r.quests.size(); ++i) {
        const QuestNow& q = r.quests[i];
        const bool event = q.event != 0;
        if (!event) ++own, own_paid += q.claimed;
        ui::well(X0, y, X1, y + 52);
        std::string from;
        VanU32 tag = kLime;
        for (const EventNow& e : r.events)
            if (e.id == q.event) from = e.name, tag = theme_ink(e.theme);
        ui::fill_at(X0, y, X0 + 3, y + 52, event ? tag : kLime);
        ui::text_at(X0 + 12, y + 4, X1 - 200, y + 22, quest_text(q.def), kInk, ui::Align::Left, true, 13.0f);
        if (event) ui::text_at(X1 - 330, y + 4, X1 - 200, y + 22, from + "'s", tag, ui::Align::Right, true, 11.0f);
        const float frac = float(q.progress) / float(std::max<u16>(1, q.def.target));
        bar(X0 + 12, y + 28, X1 - 200, y + 38, frac, q.progress >= q.def.target ? kLime : VAN_COL32(150, 170, 90, 255));
        ui::text_at(X0 + 12, y + 36, X1 - 200, y + 52, eng::str::format("%u / %u", q.progress, unsigned(q.def.target)), kDim, ui::Align::Left, true, 10.0f);
        std::string pay = thousands(q.def.sp) + " SP";
        if (q.def.xp) pay += " + " + thousands(q.def.xp) + " XP";
        ui::text_at(X1 - 194, y + 4, X1 - 8, y + 22, pay, kGold, ui::Align::Right, true, 12.0f);
        if (q.claimed) {
            tick_mark(X1 - 60, y + 36, kLime);
            ui::text_at(X1 - 194, y + 28, X1 - 70, y + 46, "Collected", kLime, ui::Align::Right, true, 11.0f);
        } else {
            const bool done = q.progress >= q.def.target;
            if (ui::text_button(9860 + int(i), X1 - 120, y + 26, X1 - 8, y + 48, done ? "Collect" : "Not yet", done, done ? "Pays this quest" : "Not done yet"))
                s.claim_reward(ClaimKind::Quest, u8(i));
        }
        y += 58;
    }
    if (r.quests.empty()) ui::text_at(X0, y, X1, y + 20, "No quests today.", kDim, ui::Align::Left, true);
    // All of the day's own done.
    y += 6;
    ui::well(X0, y, X1, y + 40);
    ui::fill_at(X0, y, X0 + 3, y + 40, kGold);
    ui::text_at(X0 + 12, y + 2, X1 - 200, y + 20, "Every one of today's quests", kInk, ui::Align::Left, true, 13.0f);
    ui::text_at(X0 + 12, y + 20, X1 - 200, y + 38, eng::str::format("%zu of %zu collected", own_paid, own), kDim, ui::Align::Left, true, 11.0f);
    ui::text_at(X1 - 330, y + 2, X1 - 8, y + 20, reward_text(r.all_quests_sp, r.all_quests_box), kGold, ui::Align::Right, true, 12.0f);
    if (r.all_claimed) {
        tick_mark(X1 - 60, y + 28, kLime);
    } else {
        const bool ready = own > 0 && own_paid == own;
        if (ui::text_button(9870, X1 - 120, y + 18, X1 - 8, y + 38, ready ? "Collect" : "Not yet", ready, ready ? "The day's bonus" : "Collect every quest first"))
            s.claim_reward(ClaimKind::AllQuests);
    }
}

void events_tab(App& app, const RewardsState& r, float X0, float Y0, float X1, float Y1, double now) {
    const u64 server = app.session().server_now(now);
    ui::heading(X0, Y0, X1, "EVENTS");
    ui::text_at(X0, Y0 + 20, X1, Y0 + 36, "Set by the staff: the seasons every year, and their own. Times count down from the server's clock.", kDim, ui::Align::Left, true, 11.0f);
    float y = Y0 + 44;
    bool any_running = false;
    for (const EventNow& e : r.events) {
        if (y > Y1 - 70) break;
        const VanU32 ink = theme_ink(e.theme);
        const float h = 74.0f;
        ui::well(X0, y, X1, y + h);
        ui::fill_at(X0, y, X0 + 3, y + h, ink);
        ui::text_at(X0 + 12, y + 4, X1 - 220, y + 24, e.name, ink, ui::Align::Left, true, 15.0f);
        std::string gives;
        auto add = [&](const std::string& t) { gives += (gives.empty() ? "" : "     ") + t; };
        if (e.sp_pct > 100) add(eng::str::format("SP x%.2g", double(e.sp_pct) / 100.0));
        if (e.xp_pct > 100) add(eng::str::format("XP x%.2g", double(e.xp_pct) / 100.0));
        if (e.drop_box < kBoxKinds && e.drop_permille) add(eng::str::format("%s: %.1f%% a match", box_info(e.drop_box).name, double(e.drop_permille) / 10.0));
        if (e.sign_in_sp) add("+" + thousands(e.sign_in_sp) + " SP signing in");
        if (e.running) {
            any_running = true;
            ui::text_at(X1 - 220, y + 6, X1 - 8, y + 22, "ends in " + span_text(e.to > server ? e.to - server : 0), kLime, ui::Align::Right, true, 11.0f);
            ui::text_at(X0 + 12, y + 26, X1 - 8, y + 44, e.banner, kInk, ui::Align::Left, true, 11.0f);
        } else {
            ui::text_at(X1 - 260, y + 6, X1 - 8, y + 22, "begins in " + span_text(e.from > server ? e.from - server : 0), kDim, ui::Align::Right, true, 11.0f);
            ui::text_at(X0 + 12, y + 26, X1 - 8, y + 44, utc_text(e.from) + " to " + utc_text(e.to) + " UTC", kDim, ui::Align::Left, true, 11.0f);
        }
        ui::text_at(X0 + 12, y + 48, X1 - 8, y + 66, gives.empty() ? std::string("Quests of its own.") : gives, e.running ? kGold : VAN_COL32(170, 150, 100, 255),
                    ui::Align::Left, true, 12.0f);
        y += h + 6;
    }
    if (!any_running) ui::text_at(X0, Y1 - 40, X1, Y1 - 20, "No event runs right now.", kDim, ui::Align::Left, true, 12.0f);
}

}  // namespace

bool rewards_waiting(const Session& s) {
    if (!s.rewards) return false;
    const RewardsState& r = *s.rewards;
    if (!r.signed_today) return true;
    for (size_t i = 0; i < r.play.size(); ++i)
        if (r.minutes >= r.play[i].minutes && !((r.play_claimed >> i) & 1)) return true;
    for (const QuestNow& q : r.quests)
        if (!q.claimed && q.progress >= q.def.target) return true;
    for (u16 n : r.boxes)
        if (n) return true;
    return false;
}

std::string events_banner(const Session& s) {
    std::string out;
    if (!s.rewards) return out;
    for (const EventNow& e : s.rewards->events)
        if (e.running && !e.banner.empty()) out += (out.empty() ? "" : "     ") + e.banner;
    return out;
}

void open_rewards(App& app, int tab) {
    ScreenState& st = app.state();
    st.rewards_open = true;
    if (tab >= 0) st.rewards_tab = tab;
    app.session().request_rewards();
}

void rewards_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 142, Y0 = 100, X1 = 882, Y1 = 668;
    if (!ui::dialog_begin("Rewards", X0, Y0, X1, Y1, "REWARDS", &st.rewards_open)) {
        st.rewards_open = false;
        return;
    }
    static const char* tabs[] = {"Today", "Quests", "Events"};
    for (int t = 0; t < 3; ++t)
        if (ui::tab_button(9840 + t, X0 + 16 + float(t) * 110, Y0 + 32, X0 + 120 + float(t) * 110, Y0 + 58, tabs[t], st.rewards_tab == t)) st.rewards_tab = t;
    // The boxes waiting, and where they are opened.
    int boxes = 0;
    if (s.rewards)
        for (u16 n : s.rewards->boxes) boxes += n;
    if (ui::text_button(9846, X1 - 230, Y0 + 32, X1 - 16, Y0 + 58, boxes ? eng::str::format("Open your boxes (%d)", boxes) : std::string("Your boxes"), true,
                        "Duffle bags and gift boxes: the inventory's Gift tab")) {
        st.rewards_open = false;
        if (app.screen() != Screen::Shop) st.shop_return = app.screen();
        st.shop_tab = 2, st.inv_tab = 3, st.gift_sel = -1;
        app.go(Screen::Shop);
        ui::dialog_close();
    }
    ui::fill_at(X0 + 12, Y0 + 62, X1 - 12, Y0 + 63, kRule);
    const float x0 = X0 + 20, y0 = Y0 + 76, x1 = X1 - 20, y1 = Y1 - 20;
    if (!s.rewards) {
        ui::text_at(X0, Y0 + 260, X1, Y0 + 290, "Asking the server...", kDim, ui::Align::Center, true);
    } else {
        const RewardsState& r = *s.rewards;
        if (st.rewards_tab == 0) today_tab(app, r, x0, y0, x1, app.now());
        else if (st.rewards_tab == 1) quests_tab(app, r, x0, y0, x1);
        else events_tab(app, r, x0, y0, x1, y1, app.now());
    }
    ui::dialog_end();
}

// A box opened (the inventory's Gift tab): it shakes, it opens, what it held.
void box_opened_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!s.box_opened) return;
    const BoxOpened o = *s.box_opened;
    if (st.box_opened_at < 0) st.box_opened_at = app.now();
    const float t = float(app.now() - st.box_opened_at);
    constexpr float X0 = 312, Y0 = 200, X1 = 712, Y1 = 520;
    bool open = true;
    if (!ui::dialog_begin("Box opened", X0, Y0, X1, Y1, o.ok ? box_info(o.box).name : "BOX", &open)) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float cx = (X0 + X1) * 0.5f;
    const ui::Picture box = ui::atlas()->picture(sf::Pack::Lobby, box_info(o.box).picture);
    if (!o.ok) {
        ui::text_at(X0 + 20, Y0 + 120, X1 - 20, Y0 + 150, o.text, kLabel, ui::Align::Center, true, 13.0f);
    } else if (t < 0.9f) {
        // It shakes a moment, harder, then opens.
        const float k = t / 0.9f, shake = std::sin(t * 48.0f) * 7.0f * k;
        ui::picture_at(box, cx - 110 + shake, Y0 + 70, cx + 110 + shake, Y0 + 180, 0xFFFFFFFF, true);
        ui::text_at(X0, Y0 + 200, X1, Y0 + 222, "Opening...", kDim, ui::Align::Center, true, 13.0f);
    } else {
        // A burst behind what it held, and its words.
        const float k = std::min(1.0f, (t - 0.9f) / 0.35f);
        const VanVec2 c = ui::pg(cx, Y0 + 120);
        for (int ray = 0; ray < 16; ++ray) {
            const float a = float(ray) * 0.3927f + t * 0.4f;
            const float r0 = ui::pgy(30), r1 = ui::pgy(30 + 52 * k);
            dl->AddLine({c.x + std::cos(a) * r0, c.y + std::sin(a) * r0}, {c.x + std::cos(a) * r1, c.y + std::sin(a) * r1}, VAN_COL32(236, 200, 96, int(120 * k)),
                        ui::pgy(3));
        }
        dl->AddCircleFilled(c, ui::pgy(48 * k), VAN_COL32(236, 200, 96, int(60 * k)), 48);
        ui::Picture prize;
        if (o.kind == u8(PrizeKind::Boost) || o.kind == u8(PrizeKind::Part)) {
            if (const ItemDef* d = item(o.item); d && *d->picture) prize = ui::atlas()->picture(sf::Pack::Lobby, d->picture);
        } else if (o.kind == u8(PrizeKind::Box)) {
            prize = ui::atlas()->picture(sf::Pack::Lobby, box_info(u8(o.item)).picture);
        } else if (o.kind == u8(PrizeKind::Weapon)) {
            if (const WeaponDef* w = weapon(o.item)) prize = weapon_icon(app, *w);
        }
        if (prize.valid()) {
            ui::picture_at(prize, cx - 80 * k, Y0 + 120 - 50 * k, cx + 80 * k, Y0 + 120 + 50 * k, 0xFFFFFFFF, true);
        } else {
            const char* unit = o.kind == u8(PrizeKind::Xp) ? "XP" : "SP";
            ui::text_at(X0, Y0 + 96, X1, Y0 + 140, thousands(o.amount), VAN_COL32(255, 230, 140, int(255 * k)), ui::Align::Center, true, 34.0f);
            ui::text_at(X0, Y0 + 140, X1, Y0 + 160, unit, kGold, ui::Align::Center, true, 14.0f);
        }
        ui::text_at(X0 + 16, Y0 + 196, X1 - 16, Y0 + 222, "You got " + o.text + ".", kInk, ui::Align::Center, true, 14.0f);
    }
    const int left = s.rewards ? s.rewards->boxes[std::min<size_t>(o.box, kBoxKinds - 1)] : 0;
    const float BY = Y1 - 50;
    if (ui::text_button(9950, X0 + 20, BY + 8, X0 + 190, BY + 34, left ? eng::str::format("Open another (%d)", left) : std::string("None left"), o.ok && left > 0 && t > 0.9f)) {
        s.box_opened.reset();
        st.box_opened_at = -1;
        s.open_box(o.box);
    }
    if (ui::kit_button(9951, "close_1", X1 - 91, BY, X1 - 18, BY + 41) || !open) {
        s.box_opened.reset();
        st.box_opened_at = -1;
        if (open) ui::dialog_close();
    }
    ui::dialog_end();
}

}  // namespace lsf
