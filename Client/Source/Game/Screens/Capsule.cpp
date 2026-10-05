// The capsule machine, on the client's own page for it (PageLottoShop) in the capsule kit
// (sourcebandi: the machine's shake frames, the coin and the pile of SP, the glowing panels). The
// capsules are the shop's (Game/Shop.hpp: a Game Master keeps them): each one's picture is the
// client's own art for it (capsule.kst's), what it holds is listed on the right with the odds of
// each, and a turn costs capsule coins, bought with SP along the bottom. The server picks the prize
// (Server/Shop.cpp); the machine shakes while it does, then the capsule opens on it.
#include "Game/Screens/Screens.hpp"

#include "Game/Items.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lsf {

ui::Picture weapon_icon(App& app, const WeaponDef& w);

namespace {

using ui::Align;
using proto::CapsuleResult;

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kSoft = VAN_COL32(200, 200, 194, 255);
constexpr VanU32 kDim = VAN_COL32(140, 140, 132, 255);
constexpr VanU32 kLime = VAN_COL32(173, 239, 16, 255);
constexpr VanU32 kGold = VAN_COL32(240, 206, 96, 255);
constexpr VanU32 kBad = VAN_COL32(236, 84, 64, 255);
constexpr VanU32 kCyan = VAN_COL32(96, 226, 220, 255);

constexpr double kShake = 1.7;   // the machine shakes this long before it opens on the prize
constexpr int kPerPage = 6;
constexpr int kCoinSteps[] = {1, 5, 10, 20, 50};

// A turn on its way: asked, answered, opened.
struct Turn {
    u16 capsule = 0;
    double asked = -1;
    std::optional<CapsuleResult> result;
    double shown = -1;
    bool active() const { return asked >= 0; }
};
Turn g_turn;

ui::Picture prize_picture(App& app, const CapsulePrize& p) {
    switch (CapsulePrizeKind(p.kind)) {
        case CapsulePrizeKind::Weapon:
            if (const WeaponDef* w = weapon(p.id)) return weapon_icon(app, *w);
            break;
        case CapsulePrizeKind::Boost:
            if (const ItemDef* d = item(p.id); d && *d->picture) return app.atlas().picture(sf::Pack::Lobby, d->picture);
            break;
        case CapsulePrizeKind::Spray:
            if (const SprayDef* sp = spray(p.id)) return spray_picture(app, *sp);
            return app.atlas().picture(sf::Pack::Lobby, "spraycan_001_lb.bmp");   // any one: a can
        default: break;
    }
    return {};
}

// What a prize is drawn as where it has no picture of its own: the coin, the pile of SP, a spray can.
void prize_art(App& app, const CapsulePrize& p, float x0, float y0, float x1, float y1) {
    if (const ui::Picture pic = prize_picture(app, p); pic.valid()) {
        ui::picture_at(pic, x0, y0, x1, y1, 0xFFFFFFFF, true);
        return;
    }
    const float s = std::min(x1 - x0, y1 - y0), cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    ui::sprite_at(p.kind == u8(CapsulePrizeKind::Coin) ? "capsule_coin_image" : "capsule_sp_image", 0, 1, cx - s * 0.5f, cy - s * 0.45f, cx + s * 0.5f, cy + s * 0.45f);
}

VanU32 prize_ink(const CapsulePrize& p) {
    switch (CapsulePrizeKind(p.kind)) {
        case CapsulePrizeKind::Weapon: return p.days == 0 ? kGold : kLime;
        case CapsulePrizeKind::Spray: return kCyan;
        case CapsulePrizeKind::Boost: return VAN_COL32(150, 190, 255, 255);
        default: return kSoft;
    }
}

// A capsule card in the grid: its art on the glowing panel, its name, what a turn costs.
bool capsule_card(App& app, const CapsuleDef& cap, int key, float x0, float y0, float x1, float y1, bool chosen) {
    bool hovered = false;
    const bool clicked = ui::region(key, x0, y0, x1, y1, &hovered);
    ui::sprite_at("capsule_subrareitem_btn", chosen ? 2 : hovered ? 1 : 0, 3, x0, y0, x1, y1, chosen ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 170));
    const float pw = (y1 - y0 - 30) * 2.0f;
    const float px0 = (x0 + x1) * 0.5f - pw * 0.5f;
    if (const ui::Picture pic = app.atlas().picture(sf::Pack::Lobby, cap.picture); pic.valid()) ui::picture_at(pic, px0, y0 + 4, px0 + pw, y1 - 26, 0xFFFFFFFF, true);
    ui::text_at(x0 + 6, y1 - 24, x1 - 70, y1 - 4, cap.name, chosen ? kLime : kInk, Align::Left, true, 12.0f);
    ui::text_at(x1 - 76, y1 - 24, x1 - 6, y1 - 4, eng::str::format("%u coin%s", unsigned(cap.coins), cap.coins == 1 ? "" : "s"), kGold, Align::Right, true, 11.0f);
    if (chosen) VanGui::GetWindowDrawList()->AddRect(ui::pg(x0, y0), ui::pg(x1, y1), VAN_COL32(214, 236, 60, 255), 0, 0, 2.0f);
    return clicked;
}

// The capsule opened: what it held, in the middle of the page, until closed or turned again.
void reveal(App& app, const CapsuleDef* cap) {
    Session& s = app.session();
    const CapsuleResult& r = *g_turn.result;
    constexpr float X0 = 302, Y0 = 200, X1 = 722, Y1 = 560;
    // The staff panel (F9) opened over it: the prize is put away, or the two dialogs would each
    // shut the other every frame and neither be drawn.
    if (app.state().staff_open) {
        g_turn = Turn{};
        return;
    }
    bool open = true;
    if (!ui::dialog_begin("Capsule", X0, Y0, X1, Y1, "THE CAPSULE HELD", &open)) {
        if (!open) g_turn = Turn{};
        return;
    }
    const float t = float(std::min(1.0, (app.now() - g_turn.shown) / 0.45));
    const float grow = 0.6f + 0.4f * t;
    const float cx = (X0 + X1) * 0.5f, cy = Y0 + 140, hw = 150 * grow, hh = 90 * grow;
    // The light behind it: the kit's success burst for a gun, the plain glow for the rest.
    if (r.prize.kind == u8(CapsulePrizeKind::Weapon)) ui::sprite_at("DlgEnchant2nd_Img_ResultBGSuccess", 0, 1, X0 + 16, Y0 + 40, X1 - 16, Y0 + 240, VAN_COL32(255, 255, 255, int(200 * t)));
    else ui::sprite_at("capsule_subrareitem_btn", 1, 3, X0 + 16, Y0 + 40, X1 - 16, Y0 + 240, VAN_COL32(255, 255, 255, int(220 * t)));
    prize_art(app, r.prize, cx - hw, cy - hh, cx + hw, cy + hh);
    ui::text_at(X0 + 16, Y0 + 248, X1 - 16, Y0 + 274, r.text, prize_ink(r.prize), Align::Center, true, 16.0f);
    ui::text_at(X0 + 16, Y0 + 276, X1 - 16, Y0 + 296, eng::str::format("Coins left: %u", s.coins), kDim, Align::Center, false, 11.0f);
    const bool again = cap && cap->listed && s.coins >= cap->coins;
    if (ui::text_button(9890, X0 + 16, Y1 - 52, X0 + 186, Y1 - 16, "Turn it again", again, again ? nullptr : "Not enough coins")) {
        g_turn = Turn{};
        app.state().capsule_sel = cap->id;
        (void)capsule_turn(app);
        ui::dialog_close();
    }
    if (ui::kit_button(9891, "close_1", X1 - 90, Y1 - 56, X1 - 17, Y1 - 15)) {
        g_turn = Turn{};
        ui::dialog_close();
    }
    ui::dialog_end();
}

void take_results(App& app) {
    Session& s = app.session();
    while (!s.capsule_results.empty()) {
        CapsuleResult r = std::move(s.capsule_results.front());
        s.capsule_results.pop_front();
        if (g_turn.active() && !g_turn.result && r.capsule == g_turn.capsule && r.ok) {
            g_turn.result = std::move(r);
            continue;
        }
        if (g_turn.active() && !g_turn.result && r.capsule == g_turn.capsule) g_turn = Turn{};
        ui::toast(r.ok ? ui::Toast::Good : ui::Toast::Warning, "%s", r.text.c_str());
    }
    if (g_turn.active() && g_turn.result && g_turn.shown < 0 && app.now() - g_turn.asked >= kShake) {
        g_turn.shown = app.now();
        app.sounds().play(Sounds::Menu::CapsuleOpen);
        const CapsulePrize& p = g_turn.result->prize;
        const bool big = p.kind == u8(CapsulePrizeKind::Weapon) || p.kind == u8(CapsulePrizeKind::Spray) || p.kind == u8(CapsulePrizeKind::Boost);
        app.sounds().play(big ? Sounds::Menu::CapsuleWin : Sounds::Menu::CapsuleLose);
    }
}

}  // namespace

bool capsule_turn(App& app) {
    Session& s = app.session();
    const CapsuleDef* cap = s.machine_capsule(u16(std::max(0, app.state().capsule_sel)));
    if (!cap || !cap->listed || (g_turn.active() && g_turn.shown < 0) || s.coins < cap->coins) return false;
    s.turn_capsule(cap->id);
    g_turn = Turn{cap->id, app.now()};
    app.sounds().play(Sounds::Menu::CapsuleBuy);
    app.sounds().play(Sounds::Menu::CapsuleShake);
    return true;
}

void capsule_shop(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    // TV's capsules as TVAS turns them, and this server's own (Session::machine_capsules).
    s.refresh_tv_shop();
    take_results(app);
    const ui::Page& page = lobby_page(app, "PageLottoShop");
    ui::page_begin("##page_lotto");
    const Nav nav = common_chrome(app, 2);
    ui::draw_static(page, {6003, 6015, 6016, 6017, 6020, 6021});

    std::vector<const CapsuleDef*> list;
    for (const CapsuleDef* c : s.machine_capsules())
        if (c->listed && !c->prizes.empty()) list.push_back(c);
    const CapsuleDef* picked = nullptr;
    for (const CapsuleDef* c : list)
        if (int(c->id) == st.capsule_sel) picked = c;
    if (!picked && !list.empty()) picked = list.front(), st.capsule_sel = picked->id;

    // The left: the title, the tab, the capsules a page at a time, the machine.
    VanDrawList* dl = VanGui::GetWindowDrawList();
    ui::text_at(44, 88, 300, 120, "CAPSULE MACHINE", kGold, Align::Left, true, 15.0f);
    ui::text_at(300, 88, 556, 120, eng::str::format("A coin is SP %s", eng::str::thousands(s.coin_price()).c_str()), kSoft, Align::Right, true, 11.0f);
    (void)ui::tab_button(9851, 57, 143, 174, 171, "Weapon", true);
    ui::text_at(186, 143, 556, 171, "A capsule holds its gun for a while or for good, and the odds are on the right.", kDim, Align::Left, false, 10.5f);
    const int pages = std::max(1, (int(list.size()) + kPerPage - 1) / kPerPage);
    st.capsule_page = std::clamp(st.capsule_page, 0, pages - 1);
    for (int i = 0; i < kPerPage; ++i) {
        const int at = st.capsule_page * kPerPage + i;
        if (at >= int(list.size())) break;
        const float x0 = 44 + float(i % 2) * 258, y0 = 178 + float(i / 2) * 114;
        if (capsule_card(app, *list[size_t(at)], 9852 + i, x0, y0, x0 + 252, y0 + 110, list[size_t(at)] == picked) && st.capsule_sel != int(list[size_t(at)]->id)) {
            st.capsule_sel = list[size_t(at)]->id;
            app.sounds().play(Sounds::Menu::CapsuleList);
        }
    }
    if (list.empty()) ui::text_at(44, 260, 556, 290, "The machine is empty for now.", kDim, Align::Center, true);
    if (pages > 1)
        if (const int d = ui::arrows(9860, 200, 522, 400, 542, eng::str::format("Page %d of %d", st.capsule_page + 1, pages)); d)
            st.capsule_page = std::clamp(st.capsule_page + d, 0, pages - 1);

    // The machine: shaking while a turn is out.
    const bool shaking = g_turn.active() && g_turn.shown < 0;
    const int frame = shaking ? 1 + int(app.now() * 14.0) % 4 : 1;
    const float jig = shaking ? 2.5f * std::sin(float(app.now()) * 60.0f) : 0.0f;
    // The machine in its well, under the kit's strip of lights (blinking faster while it shakes).
    ui::well(44, 556, 172, 676);
    const int blink = int(app.now() * (shaking ? 10.0 : 2.5)) & 1;
    ui::sprite_at(blink ? "capsule_bar_ani_image_column" : "capsule_bar_ani_image_column2", 0, 1, 44, 547, 556, 555);
    ui::sprite_at(eng::str::format("capsule_ani_shake_image_%d", frame), 0, 1, 66 + jig, 562, 150 + jig, 668);
    if (picked) {
        ui::text_at(184, 556, 556, 576, picked->name, kLime, Align::Left, true, 13.0f);
        ui::text_at(184, 578, 556, 596, eng::str::format("%u coin%s a turn.  You have %u.", unsigned(picked->coins), picked->coins == 1 ? "" : "s", s.coins),
                    s.coins >= picked->coins ? kSoft : kBad, Align::Left, false, 11.0f);
        const bool can = !shaking && s.coins >= picked->coins;
        if (ui::text_button(9861, 184, 606, 380, 646, shaking ? "Shaking..." : "Turn it", can, can ? "Take a capsule out of the machine" : shaking ? nullptr : "Buy coins along the bottom first"))
            (void)capsule_turn(app);
        if (shaking) {
            const float f = float(std::min(1.0, (app.now() - g_turn.asked) / kShake));
            ui::fill_at(390, 622, 556, 630, VAN_COL32(20, 20, 18, 255));
            ui::fill_at(390, 622, 390 + 166 * f, 630, kCyan);
        }
    }

    // The right: what the picked capsule holds, the most precious first, with the odds.
    ui::text_at(580, 88, 990, 120, picked ? "WHAT IT HOLDS" : "", kGold, Align::Left, true, 13.0f);
    if (picked) {
        u32 total = 0;
        for (const CapsulePrize& p : picked->prizes) total += p.weight;
        std::vector<const CapsulePrize*> prizes;
        for (const CapsulePrize& p : picked->prizes) prizes.push_back(&p);
        std::stable_sort(prizes.begin(), prizes.end(), [](const CapsulePrize* a, const CapsulePrize* b) { return a->weight < b->weight; });
        (void)ui::rows(9870, 571, 124, 994, 532, int(prizes.size()), 50, -1, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
            const CapsulePrize& p = *prizes[size_t(i)];
            prize_art(app, p, x0 + 4, y0 + 2, x0 + 124, y1 - 2);
            ui::text_at(x0 + 132, y0 + 6, x1 - 90, y0 + 26, capsule_prize_text(p), prize_ink(p), Align::Left, true, 12.0f);
            ui::text_at(x0 + 132, y0 + 26, x1 - 90, y1 - 2, capsule_prize_kind_name(p.kind), kDim, Align::Left, false, 10.0f);
            ui::text_at(x1 - 88, y0, x1 - 8, y1, eng::str::format("%.1f%%", 100.0 * double(p.weight) / double(std::max(1u, total))), kInk, Align::Right, true, 13.0f);
        });
    }
    page_chat(app, page, 6032, 6025, 6033, false);

    // Along the bottom: coins, buying more, SP.
    ui::text_at(214, 694, 290, 737, eng::str::format("%u", s.coins), kGold, Align::Left, true, 16.0f);
    ui::sprite_at("capsule_coin_image", 0, 1, 180, 696, 210, 726);
    int step = 0;
    for (int i = 0; i < int(std::size(kCoinSteps)); ++i)
        if (kCoinSteps[i] == st.coin_buy) step = i;
    if (const int d = ui::arrows(9880, 286, 700, 394, 722, eng::str::format("%d coin%s", kCoinSteps[step], kCoinSteps[step] == 1 ? "" : "s")); d)
        st.coin_buy = kCoinSteps[std::clamp(step + d, 0, int(std::size(kCoinSteps)) - 1)];
    const u64 cost = u64(st.coin_buy) * s.coin_price();
    const bool afford = s.profile.sp >= cost;
    const std::string tip = eng::str::format("%d coin%s for SP %s", st.coin_buy, st.coin_buy == 1 ? "" : "s", eng::str::thousands(cost).c_str());
    if (ui::kit_button(9881, "capsule_coin_charge_btn", 400, 697, 517, 725, afford, afford ? tip.c_str() : "Not enough SP")) {
        s.buy_coins(u32(st.coin_buy));
        app.sounds().play(Sounds::Menu::Spend);
    }
    ui::text_at(600, 705, 745, 735, "SP : " + eng::str::thousands(s.profile.sp), kInk, Align::Left, true, 12.0f);
    ui::text_at(750, 705, 930, 735, "Coins cost " + eng::str::thousands(cost) + " SP", afford ? kDim : kBad, Align::Left, false, 11.0f);
    if (ui::button(page, 6031, !shaking, nullptr, "Back to the Item Shop")) st.shop_tab = 3, g_turn = Turn{};
    bottom_strip(app, "Capsule Machine");
    (void)dl;
    ui::page_end();
    if (g_turn.active() && g_turn.shown >= 0) reveal(app, picked && int(picked->id) == int(g_turn.capsule) ? picked : s.machine_capsule(g_turn.capsule));
    handle_nav(app, nav);
}

}  // namespace lsf
