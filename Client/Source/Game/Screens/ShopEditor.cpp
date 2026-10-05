// The staff panel's Shop tab (Screens/Staff.cpp): a Game Master keeps the shop (Game/Shop.hpp) in
// the game, and Save sends the whole of it; the server checks every number again, keeps it in
// shop.cfg, uses it at once and tells every client (the shops' cards, the line along the bottom).
// A Moderator sees it and cannot change it.
//
//   Catalog       every weapon, force, item and spray the game has: on sale or not, and its offers
//                 (up to four lengths -- 1 to 365 days, or for good -- each at its own price); a gun
//                 made rentable for 7, 30 or 90 days in one step, or sold for good only
//   Coins, gifts  what a capsule coin costs, and what comes off anything bought for a friend
//   Capsules      the machine's capsules: name, picture, coins a turn, on or off, and their prizes
//                 with each one's weight (the odds are shown as they come out)
//   Lobby line    the red line that pans along the bottom of every lobby page
#include "Game/Screens/Screens.hpp"

#include "Game/Items.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace lsf {

namespace {

using ui::Align;

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kDim = VAN_COL32(150, 150, 142, 255);
constexpr VanU32 kLime = VAN_COL32(170, 220, 70, 255);
constexpr VanU32 kWarn = VAN_COL32(240, 190, 70, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);
constexpr VanU32 kMarqueeRed = VAN_COL32(251, 74, 74, 255);   // the scripts' "Enjoy SpecialForce!!" 251 74 74
constexpr float kRowH = 24;

void label(float x0, float y, float x1, std::string_view s, VanU32 c = kDim) { ui::text_at(x0, y, x1, y + 19, s, c, Align::Left, true, 12.0f); }

// A number typed into a kit edit box (as the events editor's): the typist's text while typed, the
// value taken whenever it reads as one, held to lo..hi. True when it changed.
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

std::string pct(u32 per_mille) { return eng::str::format(per_mille % 10 ? "%.1f%%" : "%.0f%%", double(per_mille) / 10.0); }

// The copy being edited: taken from the server's whenever there are no changes of our own.
ShopConfig& editing(App& app) {
    ScreenState& st = app.state();
    if (!st.shop_cfg || !st.shop_dirty) st.shop_cfg = app.session().shop_config();
    return *st.shop_cfg;
}

void save_bar(App& app, float x0, float y, float x1, bool gm) {
    ScreenState& st = app.state();
    Session& s = app.session();
    ui::fill_at(x0, y - 6, x1, y - 5, kRule);
    if (!gm) {
        label(x0, y + 4, x1, "Only a Game Master can change the shop. What you see is what the server uses.");
        return;
    }
    if (ui::text_button(12990, x1 - 170, y, x1, y + 26, "Save to the server", st.shop_dirty, "In every shop at once, and kept")) {
        s.staff_save_shop(*st.shop_cfg);
        st.shop_dirty = false;
    }
    if (ui::text_button(12991, x1 - 300, y, x1 - 176, y + 26, "Undo changes", st.shop_dirty, "Back to what the server has")) {
        st.shop_dirty = false;
        st.shop_cfg.reset();
        g_typing = -1;
        s.request_shop();
    }
    label(x0, y + 4, x1 - 310, st.shop_dirty ? "Changes not saved yet." : s.shop ? "As the server has it." : "Waiting for the server's shop...", st.shop_dirty ? kWarn : kDim);
}

// ── The catalog ────────────────────────────────────────────────────────────────

struct Ware {
    u16 id;
    std::string name;
};

std::vector<Ware> wares(ShopKind kind, std::string_view find) {
    std::vector<Ware> out;
    auto add = [&](u16 id) {
        if (default_offers(kind, id).empty()) return;
        std::string name = ware_name(kind, id);
        if (!find.empty() && eng::str::lower(name).find(eng::str::lower(find)) == std::string::npos && eng::str::lower(ware_code(kind, id)) != eng::str::lower(find)) return;
        out.push_back({id, std::move(name)});
    };
    switch (kind) {
        case ShopKind::Weapon:
            for (const WeaponDef& w : weapons()) add(w.id);
            break;
        case ShopKind::Force:
            for (const ForceDef& f : forces()) add(f.id);
            break;
        case ShopKind::Item:
            for (const ItemDef& d : items()) add(d.id);
            break;
        case ShopKind::Spray:
            for (const SprayDef& sp : sprays()) add(sp.id);
            break;
        default: break;
    }
    return out;
}

std::string offers_summary(ShopKind kind, u16 id, const ShopLine& line) {
    std::string out;
    for (const ShopOffer& o : line.offers) {
        const std::string d = o.days ? std::to_string(o.days) + "d" : (kind == ShopKind::Item && item(id) && item(id)->one_use() ? "one" : "good");
        out += (out.empty() ? "" : "  ") + d + " " + eng::str::thousands(o.price);
    }
    return out;
}

bool changed(const ShopConfig& c, ShopKind kind, u16 id) {
    for (const ShopEntry& e : c.entries)
        if (e.kind == u8(kind) && e.id == id) return true;
    return false;
}

void catalog_page(App& app, ShopConfig& cfg, float X0, float Y0, float X1, float Y1, bool gm) {
    ScreenState& st = app.state();
    bool& dirty = st.shop_dirty;
    static const char* kinds[] = {"Weapons", "Forces", "Items", "Sprays"};
    for (int k = 0; k < int(ShopKind::Count); ++k)
        if (ui::tab_button(12100 + k, X0 + float(k) * 82, Y0, X0 + 78 + float(k) * 82, Y0 + 22, kinds[k], st.shop_kind == k) && st.shop_kind != k)
            st.shop_kind = k, st.shop_ware = -1, g_typing = -1;
    const ShopKind kind = ShopKind(std::clamp(st.shop_kind, 0, int(ShopKind::Count) - 1));
    const float LX1 = X0 + 330;
    (void)ui::edit_at(12105, X0, Y0 + 28, LX1, Y0 + 48, st.shop_find, 24, "find by name or code");
    const std::vector<Ware> list = wares(kind, st.shop_find);
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (int(list[i].id) == st.shop_ware) sel = int(i);
    const int clicked = ui::rows(12106, X0, Y0 + 54, LX1, Y1, int(list.size()), 34, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const Ware& w = list[size_t(i)];
        const ShopLine line = shop_line(cfg, kind, w.id);
        ui::text_at(x0 + 6, y0 + 2, x1 - 70, y0 + 17, w.name, line.listed ? kInk : kDim, Align::Left, true, 12.0f);
        ui::text_at(x1 - 76, y0 + 2, x1 - 6, y0 + 17, !line.listed ? "OFF" : changed(cfg, kind, w.id) ? "CHANGED" : "on sale", !line.listed ? kDim : changed(cfg, kind, w.id) ? kWarn : kLime,
                    Align::Right, true, 10.0f);
        ui::text_at(x0 + 6, y0 + 17, x1 - 6, y1, offers_summary(kind, w.id, line), kDim, Align::Left, false, 10.5f);
    });
    if (clicked >= 0) st.shop_ware = int(list[size_t(clicked)].id), g_typing = -1;
    if (list.empty()) ui::text_at(X0, Y0 + 90, LX1, Y0 + 110, "Nothing by that name.", kDim, Align::Center);

    const float RX0 = LX1 + 18, RX1 = X1;
    if (sel < 0) {
        ui::text_at(RX0, Y0 + 80, RX1, Y0 + 100, "Pick something to change how it is sold.", kDim, Align::Center);
        return;
    }
    const u16 id = list[size_t(sel)].id;
    ShopLine line = shop_line(cfg, kind, id);
    const std::vector<ShopOffer> usual = default_offers(kind, id);
    const ItemDef* d = kind == ShopKind::Item ? item(id) : nullptr;
    const bool one_use = d && d->one_use();
    float y = Y0;
    ui::heading(RX0, y, RX1, ware_name(kind, id));
    y += 22;
    label(RX0, y, RX1, std::string("Code ") + ware_code(kind, id) + (changed(cfg, kind, id) ? "   (changed from the game's own)" : "   (as the game had it)"),
          changed(cfg, kind, id) ? kWarn : kDim);
    y += kRowH;
    bool listed = line.listed;
    bool giftable = line.giftable;
    std::vector<ShopOffer> offers = line.offers;
    bool edit = false;
    if (ui::check(12110, RX0, y, "On sale", listed, gm)) edit = true;
    if (ui::check(12111, RX0 + 150, y, "Can be gifted", giftable, gm, "Whether a soldier may buy it for a friend (the buy dialog's \"Send it to a friend as a gift\")")) edit = true;
    y += kRowH + 4;
    ui::heading(RX0, y, RX1, one_use ? "OFFER  (one use)" : "OFFERS  (how long, for how much)");
    y += 22;
    for (size_t i = 0; i < offers.size(); ++i) {
        ShopOffer& o = offers[i];
        const int k = 12120 + int(i) * 4;
        if (!one_use) {
            int at = 0;
            for (int j = 0; j < int(std::size(kShopDays)); ++j)
                if (kShopDays[j] == o.days) at = j;
            if (const int dd = ui::arrows(k, RX0, y, RX0 + 150, y + 19, offer_days_text(kind, id, o.days), gm); dd)
                o.days = kShopDays[(at + dd + int(std::size(kShopDays))) % int(std::size(kShopDays))], edit = true;
        } else {
            label(RX0, y, RX0 + 150, "One use");
        }
        label(RX0 + 160, y, RX0 + 190, "SP");
        if (number_of(k + 1, RX0 + 190, y, RX0 + 290, o.price, 0, kShopPriceMax, gm)) edit = true;
        if (o.days && !one_use) label(RX0 + 298, y, RX1 - 30, eng::str::format("%s SP a day", eng::str::thousands(o.price / std::max<u16>(1, o.days)).c_str()));
        if (offers.size() > 1 && ui::text_button(k + 2, RX1 - 24, y, RX1, y + 19, "x", gm, "Take this offer off")) {
            offers.erase(offers.begin() + std::ptrdiff_t(i));
            edit = true, g_typing = -1;
            break;
        }
        y += kRowH;
    }
    if (!one_use && offers.size() < kShopOffers && ui::text_button(12140, RX0, y, RX0 + 130, y + 21, "Add an offer", gm)) {
        // The next length not offered yet.
        for (u16 days : kShopDays)
            if (std::none_of(offers.begin(), offers.end(), [&](const ShopOffer& o) { return o.days == days; })) {
                offers.push_back({days, offers.empty() ? 1000 : offers.back().price});
                break;
            }
        edit = true;
    }
    y += kRowH + 8;
    // One step to the usual shapes.
    const ShopOffer* good = nullptr;
    for (const ShopOffer& o : offers)
        if (o.days == 0) good = &o;
    const u32 base = good ? good->price : kind == ShopKind::Weapon && weapon(id) ? weapon(id)->price : kind == ShopKind::Force && force(u8(id)) ? force(u8(id))->price : 0;
    if (!one_use && (kind == ShopKind::Weapon || kind == ShopKind::Force)) {
        ui::heading(RX0, y, RX1, "IN ONE STEP");
        y += 22;
        const std::string tip = eng::str::format("7, 30 and 90 days off SP %s for good: 12%%, 35%% and 70%% of it", eng::str::thousands(base).c_str());
        if (ui::text_button(12141, RX0, y, RX0 + 200, y + 23, "Rentable 7 / 30 / 90 days", gm && base > 0, tip.c_str())) {
            offers = rental_ladder(base);
            offers.push_back({0, base});
            edit = true, g_typing = -1;
        }
        if (ui::text_button(12142, RX0 + 206, y, RX0 + 360, y + 23, "For good only", gm && base > 0)) {
            offers = {{0, base}};
            edit = true, g_typing = -1;
        }
        if (ui::text_button(12143, RX0 + 366, y, RX1, y + 23, "As the game had it", gm && changed(cfg, kind, id))) {
            offers = usual, listed = true, giftable = true;
            edit = true, g_typing = -1;
        }
        y += 30;
    } else if (ui::text_button(12143, RX0, y, RX0 + 180, y + 23, "As the game had it", gm && changed(cfg, kind, id))) {
        offers = usual, listed = true, giftable = true;
        edit = true, g_typing = -1;
    }
    if (edit) {
        if (offers.empty()) listed = false;
        set_shop_line(cfg, kind, id, listed, offers, giftable);
        dirty = true;
    }
    ui::text_wrapped(RX0, Y1 - 50, RX1, Y1, "A rental already bought keeps its days; a change here is for what is bought from now on. A gun or a force taken off sale stays with whoever owns it.",
                     kDim, 11.0f);
}

// ── Coins and gifts ────────────────────────────────────────────────────────────

void coins_page(App& app, ShopConfig& cfg, float X0, float Y0, float X1, float Y1, bool gm) {
    bool& dirty = app.state().shop_dirty;
    float y = Y0;
    ui::heading(X0, y, X1, "CAPSULE COINS");
    y += 24;
    label(X0, y, X0 + 120, "SP a coin", kInk);
    dirty |= number_of(12270, X0 + 120, y, X0 + 220, cfg.coin_price, 1, kShopPriceMax, gm);
    // Gifts: what comes off anything bought for a friend.
    ui::heading(X0 + 260, y - 24, X1, "GIFTS");
    label(X0 + 260, y, X0 + 420, "If gifted: % discount", kInk);
    dirty |= number_of(12271, X0 + 420, y, X0 + 500, cfg.gift_discount, 0, u32(kGiftDiscountMax), gm);
    label(X0 + 508, y, X1, cfg.gift_discount ? eng::str::format("a 10,000 SP thing is sent for %s", eng::str::thousands(gift_price(cfg, 10000)).c_str()) : std::string("gifts cost what the thing costs"),
          kInk);
    (void)Y1;
}

// ── Capsules ───────────────────────────────────────────────────────────────────

// The things a prize of each kind may be: guns sold for SP, boosts on sale for days, any spray.
std::vector<u16> prize_choices(u8 kind) {
    std::vector<u16> out;
    switch (CapsulePrizeKind(kind)) {
        case CapsulePrizeKind::Weapon:
            for (const WeaponDef& w : weapons())
                if (!w.admin_only && w.price > 0) out.push_back(w.id);
            break;
        case CapsulePrizeKind::Boost:
            for (const ItemDef& d : items())
                if (d.kind == ItemKind::Boost && !d.one_use() && item_listed(d)) out.push_back(d.id);
            break;
        case CapsulePrizeKind::Spray:
            out.push_back(0);
            for (const SprayDef& sp : sprays()) out.push_back(sp.id);
            break;
        default: break;
    }
    return out;
}

std::string prize_thing(const CapsulePrize& p) {
    switch (CapsulePrizeKind(p.kind)) {
        case CapsulePrizeKind::Weapon: return ware_name(ShopKind::Weapon, p.id);
        case CapsulePrizeKind::Boost: return ware_name(ShopKind::Item, p.id);
        case CapsulePrizeKind::Spray: return p.id ? ware_name(ShopKind::Spray, p.id) : std::string("Any spray");
        default: return {};
    }
}

// The machine's capsules, or (`bags`) the Duffle Bags the shop sells for SP: the same list on the
// left and the same prizes on the right; a capsule costs coins a turn, a bag an SP price.
void capsules_page(App& app, ShopConfig& cfg, float X0, float Y0, float X1, float Y1, bool gm, bool bags) {
    ScreenState& st = app.state();
    bool& dirty = st.shop_dirty;
    std::vector<CapsuleDef>& caps = bags ? cfg.bags : cfg.capsules;
    int& sel = bags ? st.shop_bag : st.shop_capsule;
    sel = std::clamp(sel, caps.empty() ? -1 : 0, int(caps.size()) - 1);
    const float LX1 = X0 + 230;
    ui::heading(X0, Y0, LX1, bags ? (gm ? "THE SHOP'S DUFFLE BAGS" : "TEAM VANILLA'S BAGS") : "THE MACHINE");
    const int clicked = ui::rows(12300, X0, Y0 + 22, LX1, Y1 - 64, int(caps.size()), 36, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const CapsuleDef& c = caps[size_t(i)];
        ui::text_at(x0 + 6, y0 + 2, x1 - 40, y0 + 18, c.name, c.listed ? kInk : kDim, Align::Left, true, 12.0f);
        ui::text_at(x1 - 44, y0 + 2, x1 - 6, y0 + 18, c.listed ? "on" : "OFF", c.listed ? kLime : kDim, Align::Right, true, 10.0f);
        ui::text_at(x0 + 6, y0 + 18, x1 - 6, y1,
                    bags ? eng::str::format("SP %s, %zu things", eng::str::thousands(c.price).c_str(), c.prizes.size())
                         : eng::str::format("%u coin%s, %zu prizes", unsigned(c.coins), c.coins == 1 ? "" : "s", c.prizes.size()),
                    kDim, Align::Left, false, 10.5f);
    });
    if (clicked >= 0) sel = clicked, g_typing = -1;
    const float by = Y1 - 58;
    if (ui::text_button(12301, X0, by, X0 + 110, by + 23, bags ? "New bag" : "New capsule", gm && caps.size() < 32)) {
        CapsuleDef c;
        u16 next = 1;
        for (const CapsuleDef& o : caps) next = std::max<u16>(next, u16(o.id + 1));
        c.id = next, c.name = bags ? "New Duffle Bag" : "New Capsule", c.coins = 1;
        if (bags) c.price = 10000, c.picture = kBagPicture;
        c.prizes.push_back({u8(CapsulePrizeKind::Sp), 0, 0, 500, 1500, 1});
        caps.push_back(std::move(c));
        sel = int(caps.size()) - 1, dirty = true, g_typing = -1;
    }
    static double armed = -10;
    const bool sure = app.now() - armed < 3.0;
    if (ui::text_button(12302, X0 + 116, by, LX1, by + 23, sure ? "Sure? Delete" : "Delete", gm && sel >= 0)) {
        if (sure) caps.erase(caps.begin() + sel), sel--, dirty = true, armed = -10, g_typing = -1;
        else armed = app.now();
    }
    if (sel < 0 || sel >= int(caps.size())) {
        // Read only (another server's view of TV's): nothing to make here.
        const char* none = bags ? (gm ? "No Duffle Bags yet. Make one: it is sold in the Item Shop and opened from the Gift tab." : "Team Vanilla sells no Duffle Bags right now.")
                                : (gm ? "No capsules. Make one." : "No capsules.");
        ui::text_at(LX1 + 18, Y0 + 80, X1, Y0 + 100, none, kDim, Align::Center);
        return;
    }
    CapsuleDef& c = caps[size_t(sel)];
    const float RX0 = LX1 + 18, RX1 = X1, CX = RX0 + 70;
    float y = Y0;
    ui::heading(RX0, y, RX1, bags ? "THE DUFFLE BAG" : "THE CAPSULE");
    y += 24;
    label(RX0, y, CX, "Name");
    {
        const std::string before = c.name;
        (void)ui::edit_at(12310, CX, y, RX1 - 180, y + 19, c.name, 40, bags ? "Rifleman's Duffle Bag" : "UZI Capsule", false, gm);
        dirty |= c.name != before;
    }
    {
        bool on = c.listed;
        if (ui::check(12311, RX1 - 170, y, bags ? "On sale" : "In the machine", on, gm)) c.listed = on, dirty = true;
    }
    y += kRowH;
    label(RX0, y, CX, "Picture");
    {
        const std::string before = c.picture;
        (void)ui::edit_at(12312, CX, y, RX1 - 180, y + 19, c.picture, 64, bags ? "the lobby's art: dufflebag_a_lb.bmp (a to d)" : "the lobby's art: gold uzi_cs.tga", false, gm);
        dirty |= c.picture != before;
    }
    label(RX1 - 170, y, RX1 - 90, bags ? "SP price" : "Coins a turn");
    if (bags) dirty |= number_of(12314, RX1 - 90, y, RX1, c.price, 1, kShopPriceMax, gm);
    else dirty |= number_of(12313, RX1 - 80, y, RX1, c.coins, 1, 20, gm);
    y += kRowH;
    if (const ui::Picture pic = app.atlas().picture(sf::Pack::Lobby, c.picture); pic.valid()) ui::picture_at(pic, CX, y, CX + 128, y + 64, 0xFFFFFFFF, true);
    else label(CX, y + 20, RX1, c.picture.empty() ? "(no picture)" : "(the lobby has no picture by that name)", kWarn);
    y += 72;
    ui::heading(RX0, y, RX1, bags ? "WHAT IT MAY HOLD  (weight: how often each comes out, against the others)" : "PRIZES  (weight: how many of the machine's balls are this)");
    y += 22;
    u32 total = 0;
    for (const CapsulePrize& p : c.prizes) total += p.weight;
    for (size_t i = 0; i < c.prizes.size(); ++i) {
        CapsulePrize& p = c.prizes[i];
        const int k = 12400 + int(i) * 8;
        if (const int d = ui::arrows(k, RX0, y, RX0 + 96, y + 19, capsule_prize_kind_name(p.kind), gm); d) {
            p.kind = u8((int(p.kind) + d + int(CapsulePrizeKind::Count)) % int(CapsulePrizeKind::Count));
            const auto choices = prize_choices(p.kind);
            p.id = choices.empty() ? 0 : choices.front();
            if (p.kind == u8(CapsulePrizeKind::Sp)) p.lo = 500, p.hi = 1500, p.days = 0;
            if (p.kind == u8(CapsulePrizeKind::Coin)) p.lo = 1, p.hi = 2, p.days = 0;
            if (p.kind == u8(CapsulePrizeKind::Boost) || p.kind == u8(CapsulePrizeKind::Spray)) p.days = std::max<u16>(1, p.days);
            dirty = true;
        }
        const float tx = RX0 + 102;
        if (p.kind <= u8(CapsulePrizeKind::Spray)) {
            const auto choices = prize_choices(p.kind);
            int at = 0;
            for (int j = 0; j < int(choices.size()); ++j)
                if (choices[size_t(j)] == p.id) at = j;
            if (const int d = ui::arrows(k + 1, tx, y, tx + 170, y + 19, prize_thing(p), gm && !choices.empty()); d && !choices.empty())
                p.id = choices[size_t((at + d + int(choices.size())) % int(choices.size()))], dirty = true;
            dirty |= number_of(k + 2, tx + 176, y, tx + 220, p.days, 0, 365, gm);
            label(tx + 224, y, tx + 290, p.days ? "days" : "for good");
        } else {
            dirty |= number_of(k + 3, tx, y, tx + 80, p.lo, p.kind == u8(CapsulePrizeKind::Coin) ? 1 : 0, p.kind == u8(CapsulePrizeKind::Coin) ? 100 : 1000000, gm);
            label(tx + 84, y, tx + 100, "to");
            dirty |= number_of(k + 4, tx + 100, y, tx + 180, p.hi, p.lo, p.kind == u8(CapsulePrizeKind::Coin) ? 100 : 1000000, gm);
            label(tx + 184, y, tx + 290, p.kind == u8(CapsulePrizeKind::Coin) ? "coins" : "SP");
        }
        dirty |= number_of(k + 5, RX1 - 128, y, RX1 - 84, p.weight, 1, 1000, gm);
        label(RX1 - 80, y, RX1 - 28, eng::str::format("%.1f%%", 100.0 * double(p.weight) / double(std::max(1u, total))), kLime);
        if (c.prizes.size() > 1 && ui::text_button(k + 6, RX1 - 24, y, RX1, y + 19, "x", gm, "Take this prize out")) {
            c.prizes.erase(c.prizes.begin() + std::ptrdiff_t(i));
            dirty = true, g_typing = -1;
            break;
        }
        y += kRowH;
    }
    if (c.prizes.size() < 16 && ui::text_button(12399, RX0, y, RX0 + 120, y + 21, "Add a prize", gm)) {
        c.prizes.push_back({u8(CapsulePrizeKind::Sp), 0, 0, 500, 1500, 10});
        dirty = true;
    }
    (void)Y1;
}

// ── The line along the bottom ──────────────────────────────────────────────────

void marquee_page(App& app, ShopConfig& cfg, float X0, float Y0, float X1, float Y1, bool gm) {
    bool& dirty = app.state().shop_dirty;
    float y = Y0;
    ui::heading(X0, y, X1, "THE LINE ALONG THE BOTTOM  (red, panning across every lobby page)");
    y += 26;
    const std::string before = cfg.marquee;
    (void)ui::edit_at(12500, X0, y, X1, y + 22, cfg.marquee, int(kMarqueeMax), default_marquee(), false, gm);
    if (cfg.marquee != before) dirty = true;
    y += 28;
    label(X0, y, X1, eng::str::format("%zu of %zu letters. Spaces between sentences keep them apart as they pass.", cfg.marquee.size(), kMarqueeMax));
    y += 30;
    // How it will look: panning, in the scripts' red, over the bottom strip's black.
    ui::heading(X0, y, X1, "AS PLAYERS WILL SEE IT");
    y += 24;
    ui::fill_at(X0, y, X1, y + 26, VAN_COL32(6, 6, 5, 255));
    ui::clip_begin(X0, y, X1, y + 26);
    const std::string shown = cfg.marquee.empty() ? std::string(default_marquee()) : cfg.marquee;
    const float width = float(shown.size()) * 6.6f + 60.0f;
    const float run = (X1 - X0) + width;
    const float x = X1 - float(std::fmod(app.now() * 60.0, double(run)));
    ui::text_at(x, y + 3, x + width, y + 23, shown, kMarqueeRed, Align::Left, true, 12.0f);
    ui::clip_end();
    y += 40;
    if (ui::text_button(12501, X0, y, X0 + 180, y + 24, "The house's own line", gm && cfg.marquee != default_marquee())) cfg.marquee = default_marquee(), dirty = true;
    y += 40;
    ui::text_wrapped(X0, y, X1, y + 60,
                     "While an event runs, its banner (the Events tab) goes before this line. Saved, it is on every player's screen at once, in the lobby, the rooms and the shops.",
                     kDim, 11.0f);
    (void)Y1;
}

}  // namespace

void staff_shop_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const bool gm = s.profile.game_master();
    ShopConfig& cfg = editing(app);
    static const char* subs[] = {"Catalog", "Coins and gifts", "Capsules", "Lobby line", "Duffle Bags"};
    for (int t = 0; t < 5; ++t)
        if (ui::tab_button(12000 + t, X0 + float(t) * 132, Y0 - 4, X0 + 126 + float(t) * 132, Y0 + 20, subs[t], st.shop_sub == t) && st.shop_sub != t)
            st.shop_sub = t, g_typing = -1;
    const float y0 = Y0 + 32, y1 = Y1 - 40;
    // Duffle Bags and TV's capsules are sold by TVAS from Team Vanilla's own shop, which only the
    // official server keeps (its saves are pushed there). On any other server they are shown as TVAS
    // sells them, not to be changed here: a copy kept by this server would never be sold.
    const bool tv_shop_here = s.on_official_server();
    auto tv_note = [&](const char* text) { ui::text_at(X0, y0 - 4, X1, y0 + 14, text, kWarn, Align::Left, true, 11.0f); };
    switch (st.shop_sub) {
        case 0: catalog_page(app, cfg, X0, y0, X1, y1, gm); break;
        case 1: coins_page(app, cfg, X0, y0, X1, y1, gm); break;
        case 2:
            if (!tv_shop_here) tv_note("Team Vanilla's capsules are kept by the official server: only this server's own (its pack guns, numbered from 1000) are made here.");
            capsules_page(app, cfg, X0, tv_shop_here ? y0 : y0 + 22, X1, y1, gm, false);
            break;
        case 4:
            if (tv_shop_here) {
                capsules_page(app, cfg, X0, y0, X1, y1, gm, true);
            } else {
                s.refresh_tv_shop();
                tv_note("Duffle Bags are Team Vanilla's: every server sells the same ones, and only the official server's Game Masters change them. As sold now:");
                ShopConfig sold = s.tv_shop ? *s.tv_shop : ShopConfig{};
                capsules_page(app, sold, X0, y0 + 22, X1, y1, false, true);
            }
            break;
        default: marquee_page(app, cfg, X0, y0, X1, y1, gm); break;
    }
    save_bar(app, X0, Y1 - 28, X1, gm);
}

}  // namespace lsf
