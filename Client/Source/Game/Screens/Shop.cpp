// The shops and the inventory, drawn from the client's own pages (Game/Ui/Page.hpp) under
// pagecommon's plates: PageWPShop (the weapon shop), PageCharShop (the character shop and its
// parts), PageItemShop (the Item Shop and its sprays), and PageWeapon / PageCharacter / PageItem
// (what you own). What is on sale, for how long and for how much is the server's catalog
// (Game/Shop.hpp: a Game Master keeps it): a gun is rented for 7, 30 or 90 days or bought for
// good, as the catalog has it. The item grids are the kit's own cards; what a card's Buy / Use does
// is ours. A gun you own shows its wear (Game/Wear.hpp) as the bar on its card, mended by Repair,
// and Sell takes it back for SP.
#include "Game/Screens/Screens.hpp"

#include "Game/Items.hpp"
#include "Game/Render/ModelStage.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

ui::Picture weapon_icon(App& app, const WeaponDef& w);

std::string time_left(u32 left) {
    if (left == proto::OwnedItem::kForGood) return "For good";
    // Rounded up: a gun rented for 7 days says 7 until a whole day of it has gone.
    if (left > 86400) return eng::str::format("%u days left", unsigned((left + 86399) / 86400));
    if (left >= 3600) return eng::str::format("%u hours left", unsigned((left + 3599) / 3600));
    return eng::str::format("%u min left", unsigned(std::max(1u, left / 60)));
}

ui::Picture spray_picture(App& app, const SprayDef& s) {
    ui::Atlas& a = app.atlas();
    if (*s.picture)
        if (ui::Picture p = a.picture(sf::Pack::Lobby, s.picture); p.valid()) return p;
    return a.picture(sf::Pack::Effect, s.texture);
}

namespace {

using ui::Align;
using proto::OwnedItem;

constexpr VanU32 kWhite = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kSoft = VAN_COL32(200, 200, 194, 255);
constexpr VanU32 kDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kLime = VAN_COL32(173, 239, 16, 255);    // "Total SP:" (255 173 239 16 in the scripts)
constexpr VanU32 kGreen = VAN_COL32(96, 208, 72, 255);
constexpr VanU32 kOrange = VAN_COL32(236, 160, 48, 255);
constexpr VanU32 kRed = VAN_COL32(236, 80, 64, 255);

// The Gift tab's pick (ScreenState::gift_sel): a box kind below this, a friend's gift's id above,
// a shop's Duffle Bag (ShopConfig::bags) above that.
constexpr int kGiftPick = 1000;
constexpr int kBagPick = 1000000;

// What a Duffle Bag (or a capsule) may hold, a line each with how often it comes out.
std::string holds_text(const CapsuleDef& c) {
    u32 total = 0;
    for (const CapsulePrize& p : c.prizes) total += p.weight;
    std::string t;
    for (const CapsulePrize& p : c.prizes) t += eng::str::format("\n  %.1f%%  ", 100.0 * double(p.weight) / double(std::max(1u, total))) + capsule_prize_text(p);
    return t;
}

VanU32 rgb_ink(u32 rgb) { return VAN_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

// The weapon shop's tabs: Primary-Rifle (rifles and SMGs), Primary-Snipe, Primary-Machine Gun
// (machine guns and shotguns), Secondary, Melee, Throwing.
bool in_shop_tab(int tab, WeaponClass c) {
    switch (tab) {
        case 0: return c == WeaponClass::Rifle || c == WeaponClass::Smg;
        case 1: return c == WeaponClass::Sniper;
        case 2: return c == WeaponClass::MachineGun || c == WeaponClass::Shotgun;
        case 3: return c == WeaponClass::Pistol;
        case 4: return c == WeaponClass::Knife;
        default: return c == WeaponClass::Grenade;
    }
}

// The inventory's: All, Primary, Secondary, Knife, Throwing.
bool in_inventory_tab(int tab, const WeaponDef& w) {
    switch (tab) {
        case 1: return w.slot == Slot::Primary;
        case 2: return w.slot == Slot::Secondary;
        case 3: return w.slot == Slot::Melee;
        case 4: return w.slot == Slot::Throw;
        default: return true;
    }
}

// Each force's emblem (75x71, its name on it).
const char* emblem_of(const ForceDef& f) {
    const std::string_view m = art_model(f);
    if (m == "delta") return "mark_deltaforce";
    if (m == "rokmc") return "mark_rok";
    static std::string name;
    name = "mark_" + std::string(m);
    return name.c_str();
}

void equip(App& app, const WeaponDef& w) {
    Session& s = app.session();
    auto lo = s.profile.loadout;
    // A throwable takes the first of the three throwable cells that is free; with all three full,
    // the first one's place.
    const size_t cell = equip_cell(lo, w);
    const WeaponDef* was = w.slot == Slot::Throw ? weapon(lo[cell]) : nullptr;
    lo[cell] = w.id;
    s.set_loadout(s.profile.force, lo);
    app.sounds().play(Sounds::Menu::WeaponEquip);
    if (was && was->id != w.id) ui::toast(ui::Toast::Good, "%s equipped in place of the %s.", w.name.c_str(), was->name.c_str());
    else ui::toast(ui::Toast::Good, "%s equipped as your %s weapon.", w.name.c_str(), slot_name(w.slot));
}

// A throwable out of the kit, its cell left free (the others close up behind it).
void unequip_throwable(App& app, const WeaponDef& w) {
    Session& s = app.session();
    Loadout lo = s.profile.loadout;
    std::vector<u16> kept;
    for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c)
        if (lo[c] != kNoWeapon && lo[c] != w.id) kept.push_back(lo[c]);
    for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c) lo[c] = c - kFirstThrowCell < kept.size() ? kept[c - kFirstThrowCell] : kNoWeapon;
    s.set_loadout(s.profile.force, lo);
    app.sounds().play(Sounds::Menu::WeaponEquip);
    ui::toast(ui::Toast::Good, "%s taken out of your kit.", w.name.c_str());
}

// The soldier turning in a page's preview, in the parts he wears (and `trying`, a part picked in
// the shop, in place of whatever its slot holds).
void preview(App& app, u8 force_id, float x0, float y0, float x1, float y1, u16 trying = 0) {
    std::vector<u16> parts = app.session().worn(force_id);
    if (const ItemDef* t = trying ? item(trying) : nullptr; t && t->kind == ItemKind::Part && t->force == force_id) {
        std::erase_if(parts, [&](u16 id) {
            const ItemDef* o = item(id);
            return o && o->slot == t->slot;
        });
        parts.push_back(t->id);
    }
    const ui::Picture pic = app.stage().live(force_id, 12.0f * std::sin(float(app.now()) * 0.6f), parts);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->PushClipRect(ui::pg(x0, y0), ui::pg(x1, y1), true);
    if (pic.valid()) ui::picture_at(pic, x0, y0, x1, y1, 0xFFFFFFFF, true);
    else if (app.stage().loading(force_id, parts)) ui::text_at(x0, y0, x1, y1, "Loading...", kDim, Align::Center, true);
    dl->PopClipRect();
}

// A force's numbers in a stat box (the character pages' 201,533 - 341,653), with what the parts
// worn for it add (their best rolls).
void force_stats(const ForceDef& f, const PartTotals& parts, float x0, float y0, float x1) {
    const float speed = (f.speed - 1.0f) * 10.0f + parts.speed;
    const float head = f.avoid_headshot * 100 + parts.head, upper = f.upper_defense * 100 + parts.upper, legs = f.lower_defense * 100 + parts.legs;
    const struct {
        const char* label;
        std::string value;
        bool up;
    } lines[] = {{"Moving speed", eng::str::format("%+.1f", double(speed)), speed > 0},
                 {"Special point", eng::str::format("+%.0f%%", double(parts.point)), parts.point > 0},
                 {"Avoid headshot", eng::str::format("+%.0f%%", double(head)), head > 0},
                 {"Upper Defense", eng::str::format("+%.0f%%", double(upper)), upper > 0},
                 {"Legs Defense", eng::str::format("+%.0f%%", double(legs)), legs > 0}};
    for (int i = 0; i < 5; ++i) {
        const float y = y0 + 8 + 21.0f * float(i);
        ui::text_at(x0 + 6, y, x1, y + 18, lines[i].label, kSoft, Align::Left, true, 11.0f);
        ui::text_at(x0, y, x1 - 6, y + 18, lines[i].value, lines[i].up ? kGreen : kSoft, Align::Right, true, 11.0f);
    }
}

// The "SP : ..." line along the bottom box. SP is the only currency: the page's second slot (its
// AP / eCoin) shows the capsule coins.
void holdings(App& app, const ui::Page& page, int sp_id, int coin_id) {
    ui::text(page, sp_id, eng::str::format("SP : %s", eng::str::thousands(app.session().profile.sp).c_str()), kWhite);
    ui::text(page, coin_id, eng::str::format("Coins : %u", app.session().coins), kSoft);
}

// A card's name strip and price line.
void card_head(const ui::Card& c, std::string_view name, std::string_view price, VanU32 price_colour, VanU32 name_colour = kWhite) {
    ui::text_at(c.x0 + 4, c.y0 + 2, c.x1 - 4, c.y0 + 22, name, c.selected ? VAN_COL32(224, 232, 90, 255) : name_colour, Align::Center, true);
    ui::text_at(c.x0 + 6, c.y0 + 24, c.x1 - 6, c.y0 + 40, price, price_colour, Align::Left, true, 11.0f);
}

// The cheapest way in, as a card says it: "SP 1,200 / 7 days", "SP 9,000".
std::string offer_text(ShopKind kind, u16 id, const ShopOffer& o) {
    if (kind == ShopKind::Item)
        if (const ItemDef* d = item(id); d && d->one_use()) return "SP " + eng::str::thousands(o.price);
    return o.days ? eng::str::format("SP %s / %s", eng::str::thousands(o.price).c_str(), offer_days_text(kind, id, o.days).c_str())
                  : "SP " + eng::str::thousands(o.price);
}

std::string offers_lines(ShopKind kind, u16 id, const ShopLine& line) {
    std::string t;
    for (const ShopOffer& o : line.offers) t += "\n" + offer_days_text(kind, id, o.days) + ":  SP " + eng::str::thousands(o.price);
    return t;
}

std::string weapon_tooltip(const WeaponDef& w, const ShopLine& line) {
    std::string t;
    if (w.klass == WeaponClass::Grenade)
        t = eng::str::format("%s\nDamage %.0f   Radius %.1f m   Fuse %.1f s", w.name.c_str(), double(w.damage), double(w.blast_radius / 100), double(w.fuse));
    else if (w.klass == WeaponClass::Knife)
        t = eng::str::format("%s\nDamage %.0f   Reach %.1f m", w.name.c_str(), double(w.damage), double(w.melee_range / 100));
    else
        t = eng::str::format("%s  (%s)\nDamage %.0f   Fire rate %.0f rpm   Magazine %u / %u\nRange %.0f m   Mobility %.0f%%", w.name.c_str(),
                             weapon_class_name(w.klass), double(w.damage), double(w.rpm), unsigned(w.magazine), unsigned(w.reserve), double(w.range / 100),
                             double(w.move_speed * 100));
    if (line.listed && !line.offers.empty()) t += "\n" + offers_lines(ShopKind::Weapon, w.id, line);
    return t;
}

ui::Picture item_picture(const ItemDef& d) {
    ui::Atlas* a = ui::atlas();
    return a && *d.picture ? a->picture(sf::Pack::Lobby, d.picture) : ui::Picture{};
}

void open_buy(App& app, ShopKind kind, u16 id) {
    ScreenState& st = app.state();
    st.buy_kind = int(kind);
    st.buy_item = id;
    st.buy_offer = 0;
    st.buy_gift = false;
    st.buy_friend.clear();
}

// Mending and selling back are asked about first (deal_dialog): one spends SP, the other is for good.
void open_deal(App& app, ShopKind kind, u16 id, bool sell) {
    ScreenState& st = app.state();
    st.deal_kind = int(kind);
    st.deal_item = id;
    st.deal_op = sell ? 1 : 0;
}

// A wear bar's tint (the kit's own colour until it is low, then orange, red when broken), the
// same for the number beside it, and the number.
VanU32 wear_tint(u8 left) { return left == 0 ? kRed : left <= kDurabilityLow ? kOrange : 0xFFFFFFFF; }
VanU32 wear_ink(u8 left) { return left > kDurabilityLow ? kSoft : wear_tint(left); }
std::string wear_text(u8 left) { return left ? eng::str::format("%u%%", unsigned(left)) : std::string("BROKEN"); }

// Why a gun cannot be sold, or nullptr when it can (the server's own rules, Server/Shop.cpp sell).
const char* gun_unsellable(App& app, const WeaponDef& w) {
    Session& s = app.session();
    if (w.admin_only || w.price == 0 || issued(w.id)) return "It was issued to you: it cannot be sold";
    if (s.weapon_seconds_left(w.id, app.now()) != OwnedItem::kForGood) return "A rented gun cannot be sold";
    if (w.slot == Slot::Primary || w.slot == Slot::Secondary) {
        bool other = false;
        for (u16 id : s.owned_weapons)
            if (const WeaponDef* o = weapon(id); o && id != w.id && o->slot == w.slot) other = true;
        if (!other) return w.slot == Slot::Primary ? "Your last primary weapon: you must keep one" : "Your last sidearm: you must keep one";
    }
    return nullptr;
}

// One weapon's card: the shop's (Buy, View) or the inventory's (Use, Repair, Sell).
void weapon_card(App& app, const ui::Card& c, const WeaponDef& w, bool shop) {
    Session& s = app.session();
    const bool owned = s.owns_weapon(w.id);
    const u32 left = owned ? s.weapon_seconds_left(w.id, app.now()) : 0;
    const bool for_good = owned && left == OwnedItem::kForGood;
    const bool equipped = in_kit(s.profile.loadout, w.id);
    const ShopLine line = s.shop_line(ShopKind::Weapon, w.id);
    std::string price;
    VanU32 price_ink = kWhite;
    if (w.admin_only) price = "ADMIN", price_ink = kRed;
    else if (!w.price) price = "ISSUED";
    else if (!line.listed) price = "NOT ON SALE", price_ink = kDim;
    else if (const ShopOffer* o = line.cheapest()) price = offer_text(ShopKind::Weapon, w.id, *o);
    if (!shop && owned && !for_good) price = time_left(left), price_ink = left < 86400 ? kOrange : kGreen;
    card_head(c, w.name, price, price_ink, kWhite);
    // In the inventory a gun that wears shows what is left of it under its picture, and its three
    // plates take the card's foot: what it is to you (EQUIPPED, OWNED) moves up beside the price.
    const bool wearing = !shop && owned && wears(w);
    const u8 wear = s.durability_of(w.id);
    ui::picture_at(weapon_icon(app, w), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - (wearing ? 43 : 30), 0xFFFFFFFF, true);
    if (wearing) {
        ui::meter_at(c.x0 + 12, c.y1 - 40, c.x1 - 54, c.y1 - 31, float(wear) / float(kDurabilityFull), wear_tint(wear));
        ui::text_at(c.x1 - 52, c.y1 - 44, c.x1 - 8, c.y1 - 28, wear_text(wear), wear_ink(wear), Align::Right, true, 10.0f);
    }
    const float sx0 = shop ? c.x0 + 6 : c.x0 + 100, sx1 = shop ? c.x0 + 100 : c.x1 - 6;
    const float sy0 = shop ? c.y1 - 26 : c.y0 + 24, sy1 = shop ? c.y1 - 4 : c.y0 + 40;
    const Align side = shop ? Align::Left : Align::Right;
    if (equipped) ui::text_at(sx0, sy0, sx1, sy1, "EQUIPPED", kLime, side, true, 11.0f);
    else if (for_good) ui::text_at(sx0, sy0, sx1, sy1, "OWNED", kGreen, side, true, 11.0f);
    else if (owned) ui::text_at(sx0, sy0, sx1, sy1, shop ? time_left(left) : std::string("RENTED"), left < 86400 ? kOrange : kGreen, side, true, 10.0f);
    const float by = c.y1 - 27, bx = shop ? c.x1 - 96 : c.x1 - 144;
    const int key = int(w.id) * 4;
    if (shop) {
        const bool can = line.listed && !line.offers.empty() && !for_good;
        const char* tip = for_good ? "You own it for good" : !line.listed ? "Not on sale" : owned ? "Rent it for longer, or buy it for good" : "Rent it or buy it";
        if (ui::card_button(key, "buy_1", bx, by, can, tip)) open_buy(app, ShopKind::Weapon, w.id);
        const std::string vt = weapon_tooltip(w, line);
        (void)ui::card_button(key + 1, "view_1", bx + 48, by, true, vt.c_str());
    } else {
        // A throwable in the kit comes out again the same way (three cells to fill as you like).
        if (w.slot == Slot::Throw && equipped) {
            if (ui::card_button(key, "using_1", bx, by, true, "In your kit: take it out")) unequip_throwable(app, w);
        } else if (ui::card_button(key, "using_1", bx, by, owned && !equipped, equipped ? "In your kit" : "Put it in your kit")) {
            equip(app, w);
        }
        // Mending (the gun's wear) and selling back (what it is worth now).
        // A knife, a grenade and an issued gun never wear: no bar and no Repair on their cards.
        const bool worn = wearing && wear < kDurabilityFull;
        const std::string mend = worn ? eng::str::format("Mend it: %u%% left, SP %s to make it whole", unsigned(wear), eng::str::thousands(s.repair_price(w.id)).c_str())
                                      : std::string("It needs no mending");
        if (wearing && ui::card_button(key + 2, "repair_1", bx + 48, by, worn, mend.c_str())) open_deal(app, ShopKind::Weapon, w.id, false);
        const char* why = owned ? gun_unsellable(app, w) : "You do not own it";
        const std::string sell = why ? std::string(why) : "Sell it back for SP " + eng::str::thousands(s.resale_of(ShopKind::Weapon, w.id));
        if (ui::card_button(key + 3, "sell_1", bx + 96, by, !why, sell.c_str())) open_deal(app, ShopKind::Weapon, w.id, true);
    }
}

// One force's card: its own model photographed, and Buy or Use (and, in the inventory, Sell).
void force_card(App& app, const ui::Card& c, const ForceDef& f, bool shop) {
    Session& s = app.session();
    const u32 left = s.force_seconds_left(f.id, app.now());
    const bool owned = left > 0;
    const bool for_good = left == OwnedItem::kForGood;
    const bool using_it = s.profile.force == f.id;
    const ShopLine line = s.shop_line(ShopKind::Force, f.id);
    std::string price = "Free";
    VanU32 price_ink = kWhite;
    if (f.price) {
        if (!line.listed) price = "NOT ON SALE", price_ink = kDim;
        else if (const ShopOffer* o = line.cheapest()) price = offer_text(ShopKind::Force, f.id, *o);
    }
    card_head(c, f.name, price, price_ink);
    ui::picture_at(app.stage().photo(f.id), c.x0 + 10, c.y0 + 40, c.x1 - 10, c.y1 - 28, 0xFFFFFFFF, true);
    if (using_it) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, owned && !for_good ? time_left(left) : std::string("IN USE"), kLime, Align::Left, true, 10.0f);
    else if (for_good) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, "OWNED", kGreen, Align::Left, true, 11.0f);
    else if (owned) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, time_left(left), left < 86400 ? kOrange : kGreen, Align::Left, true, 10.0f);
    const float by = c.y1 - 27, bx = c.x1 - 96;
    const int key = 5000 + int(f.id) * 4;
    if (!for_good && (!owned || line.listed)) {
        const bool can = line.listed && !line.offers.empty();
        if (ui::card_button(key, "buy_1", bx, by, can, !line.listed ? "Not on sale" : owned ? "Keep it longer, or buy it for good" : "Join this force"))
            open_buy(app, ShopKind::Force, f.id);
    } else if (ui::card_button(key, "using_1", bx, by, !using_it, using_it ? "In use" : "Play as this force")) {
        s.set_loadout(f.id, s.profile.loadout);
    }
    std::string tip = eng::str::format("%s  (%s)\nSpeed %.2f   Upper armour %.0f%%   Lower armour %.0f%%", f.name, f.nation, double(f.speed),
                                       double(f.upper_defense * 100), double(f.lower_defense * 100));
    if (f.price && line.listed) tip += "\n" + offers_lines(ShopKind::Force, f.id, line);
    (void)ui::card_button(key + 1, "view_1", bx + 48, by, true, tip.c_str());
    // Leaving a force bought for good, for SP.
    if (!shop && f.price && owned) {
        const std::string sell = for_good ? "Leave this force for SP " + eng::str::thousands(s.resale_of(ShopKind::Force, f.id)) : std::string("A rented force cannot be sold");
        if (ui::card_button(key + 2, "sell_1", bx - 48, by, for_good, sell.c_str())) open_deal(app, ShopKind::Force, f.id, true);
    }
}

// The emblem strip (*HORIZBARCTRL): the forces across its slots, the arrows scrolling them.
// Returns the force clicked, or -1.
int emblem_strip(App& app, const ui::Page& page, int id, int& first, int picked, bool owned_only_bright) {
    const sf::PageNode* n = page.find(id, "HORIZBARCTRL");
    if (!n) return -1;
    struct Slot {
        float x0, y0, x1, y1;
    };
    std::vector<Slot> slots;
    Slot left{}, right{};
    for (const sf::PageNode& c : n->children) {
        int a, b, cc, d;
        if (!c.rect(a, b, cc, d)) continue;
        const Slot r{float(a), float(b), float(cc), float(d)};
        if (c.kind == "LEFT") left = r;
        else if (c.kind == "RIGHT") right = r;
        else if (c.kind == "IMAGE") slots.push_back(r);
    }
    const auto all = session_forces();
    const int most = std::max(0, int(all.size()) - int(slots.size()));
    first = std::clamp(first, 0, most);
    int clicked = -1;
    if (ui::region(id * 10 + 1, left.x0 - 2, left.y0 - 6, left.x1 + 2, left.y1 + 6)) first = std::max(0, first - 1);
    if (ui::region(id * 10 + 2, right.x0 - 2, right.y0 - 6, right.x1 + 2, right.y1 + 6)) first = std::min(most, first + 1);
    const Session& s = app.session();
    for (size_t i = 0; i < slots.size(); ++i) {
        const size_t fi = size_t(first) + i;
        if (fi >= all.size()) break;
        const ForceDef& f = *all[fi];
        const Slot& r = slots[i];
        bool hovered = false;
        if (ui::region(id * 100 + int(fi), r.x0, r.y0, r.x1, r.y1, &hovered)) clicked = int(f.id);
        const bool owned = f.price == 0 || s.owns_force(f.id);
        const VanU32 tint = owned_only_bright && !owned ? VAN_COL32(120, 120, 120, 255) : 0xFFFFFFFF;
        ui::sprite_at(emblem_of(f), 0, 1, r.x0 + 0.5f, r.y0 + 0.5f, r.x1 - 0.5f, r.y1 - 0.5f, tint);
        if (int(f.id) == picked) {
            VanGui::GetWindowDrawList()->AddRect(ui::pg(r.x0, r.y0), ui::pg(r.x1, r.y1), VAN_COL32(214, 236, 60, 255), 0, 0, 2.0f);
        } else if (hovered) {
            ui::fill_at(r.x0, r.y0, r.x1, r.y1, VAN_COL32(255, 255, 255, 22));
        }
    }
    return clicked;
}

// ── Items (Game/Items.hpp) ──

std::string left_text(const Session& s, const ItemDef& d, double now) {
    const proto::OwnedItem* o = s.owned_item(d.id);
    if (!o) return {};
    if (d.one_use()) return eng::str::format("%u to use", unsigned(o->uses));
    return time_left(s.item_seconds_left(d.id, now));
}

std::string range_text(const ItemRange& r) {
    return r.min == r.max ? eng::str::format("%g%%", double(r.max)) : eng::str::format("%g-%g%%", double(r.min), double(r.max));
}

// A part's numbers, a line each (its View and the preview's description).
std::string part_numbers(const ItemDef& d) {
    std::string out;
    auto line = [&](const std::string& l) { out += (out.empty() ? "" : "\n") + l; };
    if (d.speed != 0) line(eng::str::format("Moving speed %+.1f", double(d.speed)));
    if (d.point.any()) line("Special point +" + range_text(d.point));
    if (d.head.any()) line("Avoid headshot " + range_text(d.head));
    if (d.upper.any()) line("Upper defense " + range_text(d.upper));
    if (d.legs.any()) line("Legs defense " + range_text(d.legs));
    if (d.clan_point) line(eng::str::format("Clan point +%d", d.clan_point));
    if (d.fall_damage) line(eng::str::format("Falling damage %d%%", d.fall_damage));
    if (d.rank > 0) line(std::string("Needs the rank of ") + rank_name(d.rank));
    return out;
}

std::string item_tooltip(const ItemDef& d, const ShopLine& line) {
    std::string t = std::string(d.name) + "\n" + d.info;
    if (d.kind == ItemKind::Part)
        if (std::string n = part_numbers(d); !n.empty()) t += "\n\n" + n;
    if (line.listed) t += "\n" + offers_lines(ShopKind::Item, d.id, line);
    return t;
}

// The boosts that ask for something when used (a colour, a rank mark, a name, a reset).
bool asks_when_used(const ItemDef& d) {
    switch (d.boost) {
        case Boost::ColorName:
        case Boost::ColorClanName:
        case Boost::FakeRank:
        case Boost::NameChange:
        case Boost::ResetKillDeath:
        case Boost::ResetVictory:
        case Boost::ResetDesertion: return true;
        default: return false;
    }
}

void open_use(App& app, const ItemDef& d) {
    ScreenState& st = app.state();
    st.use_item = d.id;
    st.use_text = d.boost == Boost::NameChange ? app.session().profile.code_name : std::string();
    st.use_value = d.boost == Boost::ColorName        ? app.session().profile.name_colour
                   : d.boost == Boost::ColorClanName  ? app.session().profile.clan_colour
                   : d.boost == Boost::FakeRank       ? (app.session().profile.fake_rank == 0xFF ? kFakeRankFirst : app.session().profile.fake_rank)
                                                      : 0;
}

// One item's card: the shop's (Buy, View) or the inventory's (Wear / Use, View).
void item_card(App& app, const ui::Card& c, const ItemDef& d, bool shop) {
    Session& s = app.session();
    const double now = app.now();
    const bool owned = s.owned_item(d.id) != nullptr;
    const bool worn = d.kind == ItemKind::Part && s.wears(d.id);
    const ShopLine line = s.shop_line(ShopKind::Item, d.id);
    const ShopOffer* first = line.offers.empty() ? nullptr : &line.offers.front();
    const std::string price = first ? offer_text(ShopKind::Item, d.id, *first) : std::string();
    card_head(c, d.name, shop ? price : left_text(s, d, now), kWhite);
    ui::picture_at(item_picture(d), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
    // The part's biggest number on the card itself (the rest under View).
    if (d.kind == ItemKind::Part) {
        std::string tag;
        if (d.upper.any()) tag = "Upper " + range_text(d.upper);
        else if (d.legs.any()) tag = "Legs " + range_text(d.legs);
        else if (d.head.any()) tag = "Head " + range_text(d.head);
        else if (d.point.any()) tag = "SP +" + range_text(d.point);
        else if (d.speed != 0) tag = eng::str::format("Speed %+.1f", double(d.speed));
        if (!tag.empty()) ui::text_at(c.x0 + 6, c.y0 + 40, c.x1 - 6, c.y0 + 56, tag, kGreen, Align::Right, true, 10.0f);
    }
    if (worn) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, "WORN", kLime, Align::Left, true, 11.0f);
    else if (owned) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, shop ? left_text(s, d, now) : "OWNED", kGreen, Align::Left, true, 10.0f);
    const float by = c.y1 - 27, bx = c.x1 - 96;
    const int key = 20000 + int(d.id) * 4;
    const bool rank_ok = d.rank <= 0 || rank_for_xp(s.profile.xp) >= d.rank;
    if (shop) {
        const bool can = first && line.listed && rank_ok && s.profile.sp >= first->price;
        const char* tip = !line.listed ? "Not on sale" : !rank_ok ? "Your rank is too low" : can ? "Buy it" : "Not enough SP";
        if (ui::card_button(key, "buy_1", bx, by, first && line.listed && rank_ok, tip)) open_buy(app, ShopKind::Item, d.id);
    } else if (d.kind == ItemKind::Part) {
        if (ui::card_button(key, "using_1", bx, by, owned, worn ? "Take it off" : "Wear it")) s.equip_item(d.id);
        // The shop rents parts by the day, and a rented part cannot be sold: Sell is only on one
        // owned for good (a gift, a prize).
        if (owned && s.item_seconds_left(d.id, now) == OwnedItem::kForGood) {
            const std::string sell = "Sell it back for SP " + eng::str::thousands(s.resale_of(ShopKind::Item, d.id));
            if (ui::card_button(key + 2, "sell_1", bx - 48, by, true, sell.c_str())) open_deal(app, ShopKind::Item, d.id, true);
        }
    } else {
        const bool usable = asks_when_used(d);
        if (ui::card_button(key, "using_1", bx, by, owned && usable, usable ? "Use it" : "It works by itself, from the moment it was bought"))
            open_use(app, d);
    }
    const std::string tip = item_tooltip(d, line);
    (void)ui::card_button(key + 1, "view_1", bx + 48, by, true, tip.c_str());
}

// The picked item on the page's left: its picture and what it says.
void item_preview(App& app, const ui::Page& page, int picture_id, int text_id, const ItemDef* d) {
    if (!d) return;
    ui::picture(page, picture_id, item_picture(*d), true);
    std::string t = std::string(d->name) + "\n\n" + d->info;
    if (d->kind == ItemKind::Part)
        if (std::string n = part_numbers(*d); !n.empty()) t += "\n\n" + n;
    if (const std::string left = left_text(app.session(), *d, app.now()); !left.empty()) t += "\n\nYou own it: " + left + ".";
    ui::paragraph(page, text_id, t, kSoft);
}

// ── Sprays (Game/Shop.hpp) ──

constexpr const char* kSprayAbout =
    "Your mark on the wall in front of you: once a life, close enough to touch it, with the Spray key (T). Everyone in the match sees it.";

// One spray's card: the shop's (Buy, View) or the inventory's (Carry, View).
void spray_card(App& app, const ui::Card& c, const SprayDef& sp, bool shop) {
    Session& s = app.session();
    const u32 left = s.spray_seconds_left(sp.id, app.now());
    const bool owned = left > 0;
    const bool carried = owned && s.spray == sp.id;
    const ShopLine line = s.shop_line(ShopKind::Spray, sp.id);
    std::string price = !line.listed ? std::string("NOT ON SALE") : line.cheapest() ? offer_text(ShopKind::Spray, sp.id, *line.cheapest()) : std::string();
    if (!shop) price = time_left(left);
    card_head(c, sp.name, price, !line.listed && shop ? kDim : kWhite);
    ui::picture_at(spray_picture(app, sp), c.x0 + 30, c.y0 + 42, c.x1 - 30, c.y1 - 30, 0xFFFFFFFF, true);
    if (carried) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, "CARRIED", kLime, Align::Left, true, 11.0f);
    else if (owned && shop) ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, time_left(left), left < 86400 ? kOrange : kGreen, Align::Left, true, 10.0f);
    const float by = c.y1 - 27, bx = c.x1 - 96;
    const int key = 40000 + int(sp.id) * 4;
    if (shop) {
        const bool for_good = left == OwnedItem::kForGood;
        if (ui::card_button(key, "buy_1", bx, by, line.listed && !for_good, for_good ? "You own it for good" : !line.listed ? "Not on sale" : "Buy it"))
            open_buy(app, ShopKind::Spray, sp.id);
    } else if (ui::card_button(key, "using_1", bx, by, owned, carried ? "Put it away" : "Carry it into your matches")) {
        s.set_spray(carried ? kNoSpray : sp.id);
    }
    const std::string tip = std::string(sp.name) + "\n" + kSprayAbout + (line.listed ? "\n" + offers_lines(ShopKind::Spray, sp.id, line) : std::string());
    (void)ui::card_button(key + 1, "view_1", bx + 48, by, true, tip.c_str());
}

void spray_preview(App& app, const ui::Page& page, int picture_id, int text_id, const SprayDef* sp) {
    if (!sp) return;
    Session& s = app.session();
    ui::picture(page, picture_id, spray_picture(app, *sp), true);
    std::string t = std::string(sp->name) + "\n\n" + kSprayAbout;
    if (const u32 left = s.spray_seconds_left(sp->id, app.now())) t += "\n\nYou own it: " + time_left(left) + (s.spray == sp->id ? ", and carry it." : ".");
    ui::paragraph(page, text_id, t, kSoft);
}

// ── Buying: how long (or how many), for how much ──

ui::Picture ware_picture(App& app, ShopKind kind, u16 id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (const WeaponDef* w = weapon(id)) return weapon_icon(app, *w);
            break;
        case ShopKind::Force: return app.stage().photo(u8(id));
        case ShopKind::Item:
            if (const ItemDef* d = item(id)) return item_picture(*d);
            break;
        case ShopKind::Spray:
            if (const SprayDef* sp = spray(id)) return spray_picture(app, *sp);
            break;
        default: break;
    }
    return {};
}

// What you have of it already, for the dialog: seconds left (kForGood), 0 none.
u32 have_of(App& app, ShopKind kind, u16 id) {
    Session& s = app.session();
    switch (kind) {
        case ShopKind::Weapon: return s.weapon_seconds_left(id, app.now());
        case ShopKind::Force: return s.force_seconds_left(u8(id), app.now());
        case ShopKind::Item: {
            const ItemDef* d = item(id);
            return d && !d->one_use() ? s.item_seconds_left(id, app.now()) : 0;
        }
        case ShopKind::Spray: return s.spray_seconds_left(id, app.now());
        default: return 0;
    }
}

void buy_dialog(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ShopKind kind = ShopKind(std::clamp(st.buy_kind, 0, int(ShopKind::Count) - 1));
    const u16 id = u16(std::max(0, st.buy_item));
    const ShopLine line = s.shop_line(kind, id);
    if (!line.exists) {
        st.buy_item = -1;
        return;
    }
    // Sending it to a friend makes room for the list of them.
    const float X0 = 302, X1 = 722, Y0 = st.buy_gift ? 150.0f : 196.0f, Y1 = st.buy_gift ? 660.0f : 554.0f;
    bool open = true;
    if (!ui::dialog_begin("Buy", X0, Y0, X1, Y1, st.buy_gift ? "SEND GIFT" : kind == ShopKind::Weapon ? "RENT OR BUY" : "BUY", &open)) {
        if (!open) st.buy_item = -1;
        return;
    }
    const ItemDef* d = kind == ShopKind::Item ? item(id) : nullptr;
    const bool one_use = d && d->one_use();
    ui::well(X0 + 16, Y0 + 44, X0 + 146, Y0 + 174);
    ui::picture_at(ware_picture(app, kind, id), X0 + 18, Y0 + 46, X0 + 144, Y0 + 172, 0xFFFFFFFF, true);
    ui::text_at(X0 + 160, Y0 + 44, X1 - 16, Y0 + 64, ware_name(kind, id), VAN_COL32(224, 232, 90, 255), Align::Left, true);
    ui::heading(X0 + 160, Y0 + 76, X1 - 16, one_use ? "HOW MANY" : "HOW LONG");
    const int n = int(line.offers.size());
    st.buy_offer = std::clamp(st.buy_offer, 0, std::max(0, n - 1));
    for (int i = 0; i < n; ++i) {
        const ShopOffer& o = line.offers[size_t(i)];
        const std::string label = offer_days_text(kind, id, o.days) + "   SP " + eng::str::thousands(o.price);
        if (ui::radio(700 + i, X0 + 166, Y0 + 92 + 24.0f * float(i), label, st.buy_offer == i)) st.buy_offer = i;
    }
    // Sent as a gift, the shop's gift discount comes off (a Game Master sets it: Game/Shop.hpp gift_price).
    const u32 full = n ? line.offers[size_t(st.buy_offer)].price : 0;
    const u32 price = st.buy_gift ? gift_price(s.shop_config(), full) : full;
    const u32 have = have_of(app, kind, id);
    bool can = false;
    const char* why_not = "Not enough SP";
    if (st.buy_gift) {
        // The friends on your list (gametext 508: "You must choose the friend who will receive the gift").
        ui::heading(X0 + 16, Y0 + 190, X1 - 16, "TO WHICH FRIEND");
        std::vector<std::string> names, shown;
        if (s.friends)
            for (const proto::FriendEntry& e : s.friends->entries)
                if (e.state == u8(proto::FriendState::Friend)) names.push_back(e.name), shown.push_back(e.name + (e.online ? "   (on duty)" : ""));
        int chosen = -1;
        for (int i = 0; i < int(names.size()); ++i)
            if (names[size_t(i)] == st.buy_friend) chosen = i;
        if (const int clicked = ui::pick_list(713, X0 + 16, Y0 + 210, X1 - 16, Y1 - 132, shown, chosen); clicked >= 0) st.buy_friend = names[size_t(clicked)];
        if (names.empty()) ui::text_at(X0 + 16, Y0 + 210, X1 - 16, Y1 - 132, "Nobody is on your friends list yet.", kDim, Align::Center, true);
        can = n && line.listed && chosen >= 0 && s.profile.sp >= price;
        if (chosen < 0) why_not = "Choose the friend who will receive the gift";
    } else {
        // What you have of it already: a rental adds its days to what is left; for good ends that.
        std::string note;
        if (have == OwnedItem::kForGood) note = "You own it for good already.";
        else if (have) note = "You have it: " + time_left(have) + ". Renting again adds the days to what is left; for good keeps it.";
        else if (kind == ShopKind::Weapon) note = "A rented gun goes back when its days run out; what you made of it goes with it.";
        if (!note.empty()) ui::text_wrapped(X0 + 16, Y0 + 186, X1 - 16, Y0 + 222, note, kDim, 11.0f);
        can = n && line.listed && have != OwnedItem::kForGood && s.profile.sp >= price;
        if (!line.listed) why_not = "Not on sale";
    }
    // A gift is a Master Sergeant's to send, to a friend (gametext 1561-1564), of a thing the shop
    // lets be gifted (a Game Master says which: ShopLine::giftable).
    const bool ranked = rank_for_xp(s.profile.xp) >= kGiftRank;
    const bool may_gift = ranked && line.giftable;
    if (!may_gift) st.buy_gift = false;
    const std::string gift_tip = !line.giftable ? std::string("This cannot be sent as a gift")
                                 : ranked       ? std::string("Pay for it and send it to a friend: it waits in their inventory's Gift tab")
                                                : std::string("Sending a gift takes the rank of ") + rank_name(kGiftRank) + " or higher";
    (void)ui::check(712, X0 + 16, Y1 - 124, "Send it to a friend as a gift", st.buy_gift, may_gift, gift_tip.c_str());
    if (!line.giftable) ui::text_at(X0 + 230, Y1 - 124, X1 - 16, Y1 - 104, "Cannot be gifted", kDim, Align::Right, true, 11.0f);
    else if (const u8 off = s.shop_config().gift_discount; off && may_gift)
        ui::text_at(X0 + 230, Y1 - 124, X1 - 16, Y1 - 104,
                    st.buy_gift ? eng::str::format("Gift discount %u%%: SP %s", unsigned(off), eng::str::thousands(price).c_str()) : eng::str::format("%u%% off as a gift", unsigned(off)),
                    kGreen, Align::Right, true, 11.0f);
    ui::text_at(X0 + 16, Y1 - 92, X1 - 16, Y1 - 72,
                eng::str::format("You have SP %s; after it, SP %s", eng::str::thousands(s.profile.sp).c_str(),
                                 eng::str::thousands(can ? s.profile.sp - price : s.profile.sp).c_str()),
                can || (!st.buy_gift && have == OwnedItem::kForGood) ? kSoft : kRed, Align::Left, true, 11.0f);
    if (ui::kit_button(710, "confirm_1", X1 - 170, Y1 - 56, X1 - 97, Y1 - 15, can, can ? (st.buy_gift ? "Send the gift" : "Buy it") : why_not)) {
        if (st.buy_gift) s.send_gift(kind, id, u8(st.buy_offer), st.buy_friend);
        else s.buy(kind, id, u8(st.buy_offer));
        app.sounds().play(Sounds::Menu::Spend);
        st.buy_item = -1;
        ui::dialog_close();
    }
    if (ui::kit_button(711, "cancel_1", X1 - 90, Y1 - 56, X1 - 17, Y1 - 15)) {
        st.buy_item = -1;
        ui::dialog_close();
    }
    ui::dialog_end();
}

// Mending a gun, or selling a gun, a force or a part back: what it costs or brings, and a yes.
void deal_dialog(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ShopKind kind = ShopKind(std::clamp(st.deal_kind, 0, int(ShopKind::Count) - 1));
    const u16 id = u16(std::max(0, st.deal_item));
    const bool sell = st.deal_op == 1;
    const WeaponDef* w = kind == ShopKind::Weapon ? weapon(id) : nullptr;
    if (kind == ShopKind::Weapon ? !w || !s.owns_weapon(id) : ware_name(kind, id).empty()) {
        st.deal_item = -1;
        return;
    }
    constexpr float X0 = 302, Y0 = 230, X1 = 722, Y1 = 520;
    bool open = true;
    if (!ui::dialog_begin("Deal", X0, Y0, X1, Y1, sell ? "SELL" : "REPAIR", &open)) {
        if (!open) st.deal_item = -1;
        return;
    }
    ui::well(X0 + 16, Y0 + 44, X0 + 146, Y0 + 174);
    ui::picture_at(ware_picture(app, kind, id), X0 + 18, Y0 + 46, X0 + 144, Y0 + 172, 0xFFFFFFFF, true);
    ui::text_at(X0 + 160, Y0 + 44, X1 - 16, Y0 + 64, w ? w->name : ware_name(kind, id), VAN_COL32(224, 232, 90, 255), Align::Left, true);
    float y = Y0 + 76;
    if (w && wears(*w)) {
        const u8 wear = s.durability_of(id);
        ui::heading(X0 + 160, y, X1 - 16, "WHAT IS LEFT OF IT");
        ui::meter_at(X0 + 166, y + 20, X1 - 74, y + 31, float(wear) / float(kDurabilityFull), wear_tint(wear));
        ui::text_at(X1 - 70, y + 16, X1 - 16, y + 34, wear_text(wear), wear_ink(wear), Align::Right, true, 11.0f);
        y += 42;
    }
    const u32 amount = sell ? s.resale_of(kind, id) : s.repair_price(id);
    const char* note = !sell                      ? "Mended to 100%. A gun wears with every match it is carried through and with the rounds fired from it."
                       : kind == ShopKind::Weapon ? "Sold for good, with what you made of it. A worn gun brings less."
                       : kind == ShopKind::Force  ? "You leave this force for good. Its parts stay in your inventory."
                                                  : "Sold for good.";
    ui::text_wrapped(X0 + 160, y, X1 - 16, Y0 + 178, note, kDim, 11.0f);
    const bool can = sell || (amount > 0 && s.profile.sp >= amount);
    const u32 after = sell ? s.profile.sp + amount : can ? s.profile.sp - amount : s.profile.sp;
    ui::text_at(X0 + 16, Y1 - 116, X1 - 16, Y1 - 96, eng::str::format(sell ? "It brings SP %s" : "It costs SP %s", eng::str::thousands(amount).c_str()), sell ? kGreen : kWhite,
                Align::Left, true);
    ui::text_at(X0 + 16, Y1 - 92, X1 - 16, Y1 - 72,
                eng::str::format("You have SP %s; after it, SP %s", eng::str::thousands(s.profile.sp).c_str(), eng::str::thousands(after).c_str()), can ? kSoft : kRed,
                Align::Left, true, 11.0f);
    if (ui::kit_button(720, "confirm_1", X1 - 170, Y1 - 56, X1 - 97, Y1 - 15, can, can ? (sell ? "Sell it" : "Mend it") : "Not enough SP")) {
        if (sell) s.sell(kind, id);
        else s.repair(id);
        st.deal_item = -1;
        ui::dialog_close();
    }
    if (ui::kit_button(721, "cancel_1", X1 - 90, Y1 - 56, X1 - 17, Y1 - 15)) {
        st.deal_item = -1;
        ui::dialog_close();
    }
    ui::dialog_end();
}

// Using an item that asks for something.
void use_dialog(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ItemDef* d = st.use_item >= 0 ? item(u16(st.use_item)) : nullptr;
    if (!d) {
        st.use_item = -1;
        return;
    }
    constexpr float X0 = 292, Y0 = 200, X1 = 732, Y1 = 540;
    bool open = true;
    if (!ui::dialog_begin("Use an item", X0, Y0, X1, Y1, "USE", &open)) {
        if (!open) st.use_item = -1;
        return;
    }
    ui::text_at(X0 + 16, Y0 + 44, X1 - 16, Y0 + 64, d->name, VAN_COL32(224, 232, 90, 255), Align::Left, true);
    bool ok = true;
    u8 value = 0;
    switch (d->boost) {
        case Boost::ColorName:
        case Boost::ColorClanName: {
            ui::heading(X0 + 16, Y0 + 78, X1 - 16, "THE COLOUR");
            for (int i = 0; i < kNameColourCount; ++i) {
                const float x = X0 + 22 + 44.0f * float(i), y = Y0 + 96;
                bool hovered = false;
                if (ui::region(740 + i, x, y, x + 36, y + 36, &hovered)) st.use_value = i;
                const u32 c = kNameColours[i];
                ui::fill_at(x, y, x + 36, y + 36, VAN_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255));
                if (st.use_value == i) VanGui::GetWindowDrawList()->AddRect(ui::pg(x - 2, y - 2), ui::pg(x + 38, y + 38), VAN_COL32(214, 236, 60, 255), 0, 0, 2.0f);
                else if (hovered) ui::fill_at(x, y, x + 36, y + 36, VAN_COL32(255, 255, 255, 40));
            }
            // How it looks: glowing, the way the lists and the match show it.
            const std::string who = d->boost == Boost::ColorName ? s.profile.code_name : s.profile.clan;
            ui::well(X0 + 16, Y0 + 148, X1 - 16, Y0 + 196);
            ui::name_text_at(X0 + 16, Y0 + 148, X1 - 16, Y0 + 196, who, u8(st.use_value), Align::Center, 16.0f);
            value = u8(st.use_value);
            break;
        }
        case Boost::FakeRank: {
            ui::heading(X0 + 16, Y0 + 78, X1 - 16, "THE RANK MARK OTHERS SEE");
            const int r = std::clamp(st.use_value, kFakeRankFirst, kFakeRankLast);
            if (const int step = ui::arrows(760, X0 + 60, Y0 + 100, X1 - 60, Y0 + 124, rank_name(r)); step)
                st.use_value = std::clamp(r + step, kFakeRankFirst, kFakeRankLast);
            if (ui::Atlas* a = ui::atlas()) ui::picture_at(a->rank_badge(r), X0 + 196, Y0 + 136, X0 + 244, Y0 + 184, 0xFFFFFFFF, true);
            value = u8(r);
            if (ui::text_button(761, X0 + 16, Y1 - 56, X0 + 156, Y1 - 15, "Wear my own", s.profile.fake_rank != 0xFF)) {
                s.use_item(d->id, 0xFF);
                st.use_item = -1;
                ui::dialog_close();
            }
            break;
        }
        case Boost::NameChange: {
            ui::heading(X0 + 16, Y0 + 78, X1 - 16, "YOUR NEW CODE NAME");
            ui::edit_at(770, X0 + 60, Y0 + 104, X1 - 60, Y0 + 130, st.use_text, 16, "A new code name");
            ok = !st.use_text.empty() && st.use_text != s.profile.code_name;
            ui::text_at(X0 + 16, Y0 + 150, X1 - 16, Y0 + 200, "Your rank, record and points stay as they are. The item is used up.", kDim, Align::Center, true, 11.0f);
            break;
        }
        default: {
            const char* what = d->boost == Boost::ResetKillDeath ? "Your kills, deaths and head shots go back to zero."
                               : d->boost == Boost::ResetVictory ? "Your wins and losses go back to zero."
                                                                 : "Your record of leaving games and team kills is cleared.";
            ui::text_at(X0 + 16, Y0 + 90, X1 - 16, Y0 + 140, what, kSoft, Align::Center, true);
            ui::text_at(X0 + 16, Y0 + 140, X1 - 16, Y0 + 170, "Rank and points stay as they are. The item is used up.", kDim, Align::Center, true, 11.0f);
            break;
        }
    }
    if (ui::kit_button(780, "confirm_1", X1 - 170, Y1 - 56, X1 - 97, Y1 - 15, ok)) {
        s.use_item(d->id, value, st.use_text);
        st.use_item = -1;
        ui::dialog_close();
    }
    if (ui::kit_button(781, "cancel_1", X1 - 90, Y1 - 56, X1 - 17, Y1 - 15)) {
        st.use_item = -1;
        ui::dialog_close();
    }
    ui::dialog_end();
}

// The parts `force` can wear in a Character Shop tab (1 Head .. 7 Accessory), or those you own of
// them (a part off the catalog is not shown in the shop).
std::vector<const ItemDef*> parts_for(const Session& s, u8 force, int tab, bool owned_only) {
    static const char* const kTabs[] = {"Head", "Face", "Torso", "Arms", "Legs", "Feet", "Accessory"};
    std::vector<const ItemDef*> out;
    if (tab < 1 || tab > 7) return out;
    for (const ItemDef* d : session_items())
        if (d->kind == ItemKind::Part && d->force == force && std::string_view(d->tab) == kTabs[tab - 1] &&
            (owned_only ? s.owned_item(d->id) != nullptr : s.shop_line(ShopKind::Item, d->id).listed))
            out.push_back(d);
    std::stable_sort(out.begin(), out.end(), [](const ItemDef* a, const ItemDef* b) { return a->offers[0].price < b->offers[0].price; });
    return out;
}

void leave_shop(App& app) {
    ScreenState& st = app.state();
    st.inv_snapshot.reset();
    st.view_force = -1;
    app.go(st.shop_return == Screen::Shop ? Screen::Channels : st.shop_return);
}

// ── The weapon shop (PageWPShop) ──

void weapon_shop(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageWPShop");
    ui::page_begin("##page_wpshop");
    const Nav nav = common_chrome(app, 1);
    ui::draw_static(page, {2016, 2017});

    // The soldier to see them on.
    const u8 view = u8(st.view_force >= 0 ? st.view_force : s.profile.force);
    const ForceDef* vf = force(view);
    if (const int d = ui::selector(page, 2019, vf ? vf->name : "?"); d != 0) {
        // By its place in the list: a server's own characters follow the base twelve, and their numbers do not.
        const auto all = session_forces();
        const int n = int(all.size());
        int at = 0;
        for (int i = 0; i < n; ++i)
            if (all[size_t(i)]->id == view) at = i;
        st.view_force = int(all[size_t((at + d + n) % n)]->id);
    }
    preview(app, view, 30, 212, 346, 734);

    // The grid for this tab. The kit's tabs say "Primary- Rifle" and the like; ours say what they hold.
    static const std::string kTabs[] = {"Rifle", "Sniper", "Machine Gun", "Secondary", "Melee", "Throwing"};
    if (const int t = ui::tabs(page, 2012, st.shop_cat, {}, kTabs); t >= 0 && t != st.shop_cat) {
        st.shop_cat = t;
        app.sounds().play(Sounds::Menu::WeaponType);
        st.shop_sel = -1;
    }
    // What the catalog has on sale, and the staff's own guns to those who own them.
    std::vector<const WeaponDef*> items;
    for (const WeaponDef* w : session_weapons())
        if (((w->shop && s.shop_line(ShopKind::Weapon, w->id).listed) || (w->admin_only && s.owns_weapon(w->id))) && in_shop_tab(st.shop_cat, w->klass))
            items.push_back(w);
    int chosen = -1;
    for (int i = 0; i < int(items.size()); ++i)
        if (int(items[size_t(i)]->id) == st.shop_sel) chosen = i;
    const int clicked = ui::card_grid(page, 2013, int(items.size()), chosen, [&](const ui::Card& c) { weapon_card(app, c, *items[size_t(c.index)], true); });
    if (clicked >= 0) st.shop_sel = int(items[size_t(clicked)]->id);
    if (items.empty()) ui::text_at(359, 240, 996, 270, "Nothing of this kind is on sale.", kDim, Align::Center, true);

    // What the Buy on the left would spend: the card picked, its cheapest way in.
    const WeaponDef* picked = st.shop_sel >= 0 ? weapon(u16(st.shop_sel)) : nullptr;
    const ShopLine line = picked ? s.shop_line(ShopKind::Weapon, picked->id) : ShopLine{};
    const bool for_good = picked && s.weapon_seconds_left(picked->id, app.now()) == OwnedItem::kForGood;
    const ShopOffer* cheapest = line.cheapest();
    ui::text(page, 2016, picked && cheapest && !for_good ? eng::str::thousands(cheapest->price) : "0", kWhite);
    if (ui::button(page, 2001, picked && line.listed && cheapest && !for_good, nullptr, "Rent or buy the weapon picked")) open_buy(app, ShopKind::Weapon, picked->id);
    if (ui::button(page, 2002, true, nullptr, "Put it back")) st.shop_sel = -1, st.view_force = -1;
    holdings(app, page, 2009, 2010);
    if (ui::button(page, 2008, true, nullptr, "Back")) leave_shop(app);
    bottom_strip(app, "Weapon Shop");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The character shop (PageCharShop) ──

void character_shop(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageCharShop");
    ui::page_begin("##page_charshop");
    const Nav nav = common_chrome(app, 0);
    ui::draw_static(page, {1012, 1013});

    if (st.view_force < 0) st.view_force = s.profile.force;
    const ForceDef* vf = force(u8(st.view_force));
    if (const int f = emblem_strip(app, page, 1006, st.emblem_first, st.view_force, false); f >= 0) {
        st.view_force = f;
        st.shop_sel = f;
    }
    preview(app, u8(st.view_force), 30, 188, 346, 734, st.char_tab > 0 && st.item_sel > 0 ? u16(st.item_sel) : u16(0));
    if (vf) force_stats(*vf, part_totals(s.worn(vf->id), vf->id), 201, 533, 341);

    // The Character tab sells the force itself; the others its parts (Game/Items.hpp).
    if (const int t = ui::tabs(page, 1004, st.char_tab); t >= 0 && t != st.char_tab) st.char_tab = t, st.item_sel = -1;
    const ItemDef* picked_part = nullptr;
    if (st.char_tab == 0) {
        std::vector<const ForceDef*> items;
        if (vf) items.push_back(vf);
        const int clicked = ui::card_grid(page, 1007, int(items.size()), st.shop_sel == st.view_force ? 0 : -1,
                                          [&](const ui::Card& c) { force_card(app, c, *items[size_t(c.index)], true); });
        if (clicked >= 0) st.shop_sel = int(items[size_t(clicked)]->id);
    } else if (vf) {
        const auto parts = parts_for(s, vf->id, st.char_tab, false);
        int chosen = -1;
        for (int i = 0; i < int(parts.size()); ++i)
            if (int(parts[size_t(i)]->id) == st.item_sel) chosen = i, picked_part = parts[size_t(i)];
        const int clicked = ui::card_grid(page, 1007, int(parts.size()), chosen, [&](const ui::Card& c) { item_card(app, c, *parts[size_t(c.index)], true); });
        if (clicked >= 0) st.item_sel = int(parts[size_t(clicked)]->id);
        if (parts.empty()) ui::text_at(359, 240, 996, 270, "Nothing for this force in this tab.", kDim, Align::Center, true);
    }

    if (picked_part) {
        const ShopLine pl = s.shop_line(ShopKind::Item, picked_part->id);
        ui::text(page, 1012, pl.offers.empty() ? "0" : eng::str::thousands(pl.offers.front().price), kWhite);
        if (ui::button(page, 1001, pl.listed, nullptr, "Buy the part picked")) open_buy(app, ShopKind::Item, picked_part->id);
    } else {
        const ShopLine fl = vf ? s.shop_line(ShopKind::Force, vf->id) : ShopLine{};
        const bool for_good = vf && s.force_seconds_left(vf->id, app.now()) == OwnedItem::kForGood;
        const ShopOffer* cheapest = fl.cheapest();
        ui::text(page, 1012, vf && cheapest && !for_good ? eng::str::thousands(cheapest->price) : "0", kWhite);
        if (ui::button(page, 1001, vf && fl.listed && cheapest && !for_good, nullptr, "Join this force")) open_buy(app, ShopKind::Force, vf->id);
    }
    if (ui::button(page, 1002, true, nullptr, "Back to your own")) st.view_force = s.profile.force, st.shop_sel = -1;
    holdings(app, page, 1009, 1010);
    if (ui::button(page, 1008, true, nullptr, "Back")) leave_shop(app);
    bottom_strip(app, "Character Shop");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The inventory (PageWeapon, PageCharacter) ──

// The pieces every inventory page shares: Save / Return, the tabs across the top (and the
// Spray item tab after the script's four), holdings, Out.
void inventory_common(App& app, const ui::Page& page) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!st.inv_snapshot) st.inv_snapshot = std::make_pair(s.profile.force, s.profile.loadout);
    if (ui::button(page, 1, true, nullptr, "Save your kit")) ui::toast(ui::Toast::Good, "Your kit is saved.");
    if (ui::button(page, 2, true, nullptr, "Put your kit back as you found it")) s.set_loadout(st.inv_snapshot->first, st.inv_snapshot->second);
    switch (ui::tabs(page, 3, st.inv_tab)) {
        case 0: st.inv_tab = 0, st.inv_sub = 0, st.shop_sel = -1; break;
        case 1: st.inv_tab = 1, st.inv_sub = 0, st.shop_sel = -1; break;
        case 2: st.inv_tab = 2, st.inv_sub = 0, st.item_sel = -1; break;
        case 3: st.inv_tab = 3, st.gift_sel = -1; break;
        default: break;
    }
    if (ui::tab_button(9801, 785, 109, 889, 139, "Spray item", st.inv_tab == 4) && st.inv_tab != 4) st.inv_tab = 4, st.item_sel = -1;
    holdings(app, page, 9, 10);
    if (ui::button(page, 8, true, nullptr, "Back")) leave_shop(app);
}

void weapon_inventory(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageWeapon");
    ui::page_begin("##page_weapon");
    const Nav nav = common_chrome(app, 3);
    ui::draw_static(page);
    inventory_common(app, page);

    // Your force, and the four things you carry.
    std::vector<u8> owned;
    for (const ForceDef* f : session_forces())
        if (f->price == 0 || s.owns_force(f->id)) owned.push_back(f->id);
    const ForceDef* mf = force(s.profile.force);
    if (const int d = ui::selector(page, 13, mf ? mf->name : "?", owned.size() > 1); d != 0) {
        auto it = std::find(owned.begin(), owned.end(), s.profile.force);
        int i = it == owned.end() ? 0 : int(it - owned.begin());
        i = (i + d + int(owned.size())) % int(owned.size());
        s.set_loadout(owned[size_t(i)], s.profile.loadout);
    }
    // The page's own boxes: the primary, the sidearm, the blade, and along the bottom the three
    // throwables' cells (the frame's lines at 134 and 241 split that row in three).
    const struct {
        float x0, y0, x1, y1;
    } slots[kLoadoutSlots] = {{29, 214, 346, 372}, {29, 372, 346, 532}, {29, 531, 346, 633}, {29, 632, 134, 738}, {134, 632, 241, 738}, {241, 632, 346, 738}};
    for (int k = 0; k < int(kLoadoutSlots); ++k) {
        const auto& r = slots[k];
        if (k <= int(kFirstThrowCell)) ui::text_at(r.x0 + 8, r.y0 + 4, r.x1 - 8, r.y0 + 22, slot_name(cell_slot(size_t(k))), kLime, Align::Left, true, 11.0f);
        if (const WeaponDef* w = weapon(s.profile.loadout[size_t(k)])) {
            if (k < 3) ui::text_at(r.x0 + 8, r.y0 + 4, r.x1 - 8, r.y0 + 22, w->name, kWhite, Align::Right, true, 11.0f);
            const float bottom = k < 2 ? r.y1 - 22 : r.y1 - 6;
            ui::picture_at(weapon_icon(app, *w), r.x0 + 16, r.y0 + 24, r.x1 - 16, bottom, 0xFFFFFFFF, true);
            // What is left of it on the page's own bar, the number and its mending to the bar's left.
            if (k < 2) {
                const u8 wear = s.durability_of(w->id);
                ui::meter(page, 14 + k, float(wear) / float(kDurabilityFull), wear_tint(wear));
                if (wears(*w)) {
                    ui::text_at(r.x0 + 56, r.y1 - 22, r.x0 + 98, r.y1 - 6, wear_text(wear), wear_ink(wear), Align::Right, true, 10.0f);
                    const std::string mend = eng::str::format("Mend it for SP %s", eng::str::thousands(s.repair_price(w->id)).c_str());
                    if (wear < kDurabilityFull && ui::card_button(9820 + k, "repair_1", r.x0 + 8, r.y1 - 27, true, mend.c_str())) open_deal(app, ShopKind::Weapon, w->id, false);
                }
            }
        } else if (throw_cell(size_t(k))) {
            ui::text_at(r.x0, r.y0 + 20, r.x1, r.y1, "Empty", kDim, Align::Center, true, 11.0f);
        }
    }

    // What you own.
    if (const int t = ui::tabs(page, 4, st.inv_sub); t >= 0 && t != st.inv_sub) {
        st.inv_sub = t, st.shop_sel = -1;
        app.sounds().play(Sounds::Menu::WeaponType);
    }
    std::vector<const WeaponDef*> items;
    for (const WeaponDef* w : session_weapons())
        if (s.owns_weapon(w->id) && in_inventory_tab(st.inv_sub, *w)) items.push_back(w);
    int chosen = -1;
    for (int i = 0; i < int(items.size()); ++i)
        if (int(items[size_t(i)]->id) == st.shop_sel) chosen = i;
    const int clicked = ui::card_grid(page, 7, int(items.size()), chosen, [&](const ui::Card& c) { weapon_card(app, c, *items[size_t(c.index)], false); });
    if (clicked >= 0) st.shop_sel = int(items[size_t(clicked)]->id);
    if (items.empty()) ui::text_at(359, 190, 996, 220, "You own nothing in this category yet.", kDim, Align::Center, true);
    bottom_strip(app, "Inventory");
    ui::page_end();
    handle_nav(app, nav);
}

void character_inventory(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageCharacter");
    ui::page_begin("##page_character");
    const Nav nav = common_chrome(app, 3);
    ui::draw_static(page);
    inventory_common(app, page);

    if (st.view_force < 0) st.view_force = s.profile.force;
    const ForceDef* vf = force(u8(st.view_force));
    if (const int f = emblem_strip(app, page, 6, st.emblem_first, st.view_force, true); f >= 0) st.view_force = f;
    preview(app, u8(st.view_force), 30, 188, 346, 734);
    if (vf) force_stats(*vf, part_totals(s.worn(vf->id), vf->id), 201, 533, 341);

    // The forces you own (All), then the parts you own for the force on show, by tab.
    if (const int t = ui::tabs(page, 4, st.inv_sub); t >= 0 && t != st.inv_sub) st.inv_sub = t, st.item_sel = -1;
    if (st.inv_sub == 0) {
        std::vector<const ForceDef*> items;
        for (const ForceDef* f : session_forces())
            if (f->price == 0 || s.owns_force(f->id)) items.push_back(f);
        int chosen = -1;
        for (int i = 0; i < int(items.size()); ++i)
            if (int(items[size_t(i)]->id) == st.view_force) chosen = i;
        const int clicked = ui::card_grid(page, 7, int(items.size()), chosen, [&](const ui::Card& c) { force_card(app, c, *items[size_t(c.index)], false); });
        if (clicked >= 0) st.view_force = int(items[size_t(clicked)]->id);
        if (items.empty()) ui::text_at(359, 262, 996, 292, "You own nothing in this category yet.", kDim, Align::Center, true);
    } else if (vf) {
        const auto parts = parts_for(s, vf->id, st.inv_sub, true);
        int chosen = -1;
        for (int i = 0; i < int(parts.size()); ++i)
            if (int(parts[size_t(i)]->id) == st.item_sel) chosen = i;
        const int clicked = ui::card_grid(page, 7, int(parts.size()), chosen, [&](const ui::Card& c) { item_card(app, c, *parts[size_t(c.index)], false); });
        if (clicked >= 0) st.item_sel = int(parts[size_t(clicked)]->id);
        if (parts.empty()) ui::text_at(359, 262, 996, 292, "No parts of this kind for this force yet: the Character Shop sells them.", kDim, Align::Center, true);
    }
    bottom_strip(app, "Inventory");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The Item Shop (PageItemShop): game items, sprays, and the way to the capsule machine ──

void item_shop(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageItemShop");
    ui::page_begin("##page_itemshop");
    const Nav nav = common_chrome(app, 2);
    ui::draw_static(page, {3011, 3012, 3013, 4013, 17});
    // The script's one tab (Game Item), Spray item beside it, and Capsule, which is a page of its own.
    if (ui::tab_button(9811, 360, 111, 464, 141, "Game Item", st.item_tab == 0) && st.item_tab != 0) st.item_tab = 0, st.item_sel = -1;
    if (ui::tab_button(9812, 464, 111, 568, 141, "Spray item", st.item_tab == 1) && st.item_tab != 1) st.item_tab = 1, st.item_sel = -1;
    if (ui::tab_button(9813, 568, 111, 672, 141, "Capsule", false)) {
        st.shop_tab = 4;
        app.sounds().play(Sounds::Menu::CapsuleList);
    }
    if (ui::tab_button(9814, 672, 111, 776, 141, "Horror item", st.item_tab == 2) && st.item_tab != 2) st.item_tab = 2, st.item_sel = -1;
    if (ui::tab_button(9815, 776, 111, 880, 141, "Supply Crate", st.item_tab == 3) && st.item_tab != 3) st.item_tab = 3, st.item_sel = -1;
    if (ui::tab_button(9816, 880, 111, 984, 141, "Duffle Bag", st.item_tab == 4) && st.item_tab != 4) st.item_tab = 4, st.item_sel = -1;
    u32 price = 0;
    bool can = false;
    if (st.item_tab == 4) {
        // Team Vanilla's Duffle Bags, as TVAS sells them (the official server's Game Masters keep
        // them): bought for SP, kept in the Gift tab, and opened there for one of the things the bag
        // may hold. Another server's own copy of them is never listed: TVAS would not sell it.
        s.refresh_tv_shop();
        std::vector<const CapsuleDef*> list;
        for (const CapsuleDef& b : s.bags_on_sale())
            if (b.listed && !b.prizes.empty()) list.push_back(&b);
        int chosen = -1;
        for (int i = 0; i < int(list.size()); ++i)
            if (int(list[size_t(i)]->id) == st.item_sel) chosen = i;
        if (chosen < 0 && !list.empty()) chosen = 0, st.item_sel = int(list[0]->id);
        auto owned = [&](u16 id) {
            for (const proto::OwnedItem& o : s.bags)
                if (o.id == id) return unsigned(o.uses);
            return 0u;
        };
        ui::Atlas* a = ui::atlas();
        const int clicked = ui::card_grid(page, 3007, int(list.size()), chosen, [&](const ui::Card& c) {
            const CapsuleDef& b = *list[size_t(c.index)];
            card_head(c, b.name, "SP " + eng::str::thousands(b.price), kWhite);
            if (a) ui::picture_at(a->picture(sf::Pack::Lobby, b.picture), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
            const unsigned have = owned(b.id);
            ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 110, c.y1 - 4, eng::str::format("YOU HAVE %u", have), have ? kLime : kDim, Align::Left, true, 10.0f);
            const bool ok = s.profile.sp >= b.price && have < unsigned(kBagsOwnedMax);
            if (ui::card_button(43000 + c.index * 2, "buy_1", c.x1 - 48, c.y1 - 27, ok, ok ? "Buy one: it goes to your inventory's Gift tab" : s.profile.sp < b.price ? "Not enough SP" : "You can keep no more"))
                s.buy_bag(b.id, 1);
        });
        if (clicked >= 0) st.item_sel = int(list[size_t(clicked)]->id), chosen = clicked;
        if (list.empty()) ui::text_at(359, 240, 996, 270, "No Duffle Bags are on sale.", kDim, Align::Center, true);
        const CapsuleDef* picked = chosen >= 0 ? list[size_t(chosen)] : nullptr;
        if (picked) {
            if (a) ui::picture(page, 4013, a->picture(sf::Pack::Lobby, picked->picture), true);
            ui::paragraph(page, 3013, picked->name + "\n\nBought, it waits in your inventory's Gift tab; opened, it holds one of these:" + holds_text(*picked), kSoft);
            price = picked->price;
            can = true;
        }
        ui::text(page, 3011, eng::str::thousands(price), kWhite);
        if (ui::button(page, 3001, can && picked, nullptr, "Buy one of the bag picked")) s.buy_bag(picked->id, 1);
    } else if (st.item_tab == 2) {
        // Horror Mode's seven items (Game/Rules.hpp HorrorItem): bought one at a time, carried into
        // Horror Mode and Horror Mode 2, each spent there on its key.
        if (st.item_sel < 0 || st.item_sel >= kHorrorItems) st.item_sel = 0;
        ui::Atlas* a = ui::atlas();
        const int clicked = ui::card_grid(page, 3007, kHorrorItems, st.item_sel, [&](const ui::Card& c) {
            const HorrorItem it = HorrorItem(c.index);
            const HorrorItemInfo& info = horror_item(it);
            const u16 have = s.horror_items[size_t(c.index)];
            card_head(c, info.name, "SP " + eng::str::thousands(info.price), kWhite);
            if (a) ui::picture_at(a->picture(sf::Pack::Menu, info.icon), c.x0 + 70, c.y0 + 46, c.x1 - 70, c.y1 - 34, 0xFFFFFFFF, true);
            ui::text_at(c.x0 + 6, c.y0 + 40, c.x1 - 6, c.y0 + 56, info.undead ? "An undead's" : "A human's", info.undead ? kOrange : kGreen, Align::Right, true, 10.0f);
            ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, eng::str::format("YOU HAVE %u", unsigned(have)), have ? kLime : kDim, Align::Left, true, 10.0f);
            const bool room = have < kHorrorItemMax && s.profile.sp >= info.price;
            if (ui::card_button(41000 + c.index * 2, "buy_1", c.x1 - 48, c.y1 - 27, room, room ? "Buy one" : have >= kHorrorItemMax ? "You can carry no more" : "Not enough SP"))
                s.buy_horror_item(it, 1);
        });
        if (clicked >= 0) st.item_sel = clicked;
        const HorrorItemInfo& info = horror_item(HorrorItem(st.item_sel));
        if (a) ui::picture(page, 4013, a->picture(sf::Pack::Menu, info.icon), true);
        ui::paragraph(page, 3013,
                      std::string(info.name) + "\n\n" + info.about + "\n\nFor Horror Mode and Horror Mode 2. " +
                          (HorrorItem(st.item_sel) == HorrorItem::Rebirth ? "It is spent by itself when the undead bring you down."
                                                                         : "It is spent on its key (Options, Controls); each use takes one.") +
                          eng::str::format("\n\nYou have %u.", unsigned(s.horror_items[size_t(st.item_sel)])),
                      kSoft);
        price = info.price;
        can = s.horror_items[size_t(st.item_sel)] < kHorrorItemMax;
        ui::text(page, 3011, eng::str::thousands(price), kWhite);
        if (ui::button(page, 3001, can, nullptr, "Buy one of the item picked")) s.buy_horror_item(HorrorItem(st.item_sel), 1);
    } else if (st.item_tab == 3) {
        // The Supply Crate (Game/Shop.hpp): a random part for the force you play, for 7 or 30 days,
        // or once in a long while one of the crates' guns for good.
        if (st.item_sel != 30) st.item_sel = st.item_sel == 30 ? 30 : 7;
        ui::Atlas* a = ui::atlas();
        const size_t parts = crate_parts(s.profile.force).size();
        const ForceDef* mine = force(s.profile.force);
        const int clicked = ui::card_grid(page, 3007, 2, st.item_sel == 30 ? 1 : 0, [&](const ui::Card& c) {
            const u16 days = c.index == 1 ? 30 : 7;
            const u32 cost = days == 30 ? kCratePrice30 : kCratePrice7;
            card_head(c, eng::str::format("Supply Crate (%u days)", unsigned(days)), "SP " + eng::str::thousands(cost), kWhite);
            if (a) ui::picture_at(a->picture(sf::Pack::Lobby, days == 30 ? "supplycrate_001_30d_lb.bmp" : "supplycrate_001_7d_lb.bmp"), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
            const bool ok = parts > 0 && s.profile.sp >= cost;
            if (ui::card_button(42000 + c.index * 2, "buy_1", c.x1 - 48, c.y1 - 27, ok, !parts ? "Your force cannot use the Supply Crate" : ok ? "Buy it and open it" : "Not enough SP"))
                s.supply_crate(days);
        });
        if (clicked >= 0) st.item_sel = clicked == 1 ? 30 : 7;
        const u16 days = st.item_sel == 30 ? 30 : 7;
        if (a) ui::picture(page, 4013, a->picture(sf::Pack::Lobby, days == 30 ? "supplycrate_001_30d_lb.bmp" : "supplycrate_001_7d_lb.bmp"), true);
        std::string t = eng::str::format("Supply Crate (%u days)\n\nThe Supply Crate contains various pieces of equipment that users can use regardless of rank limitations.", unsigned(days));
        if (parts)
            t += eng::str::format("\n\nOpened at once: one of %zu pieces of equipment for %s, for %u days, put on as it comes; or, %d times in a hundred, one of the crates' %zu guns, yours for good.",
                                  parts, mine ? mine->name : "your force", unsigned(days), kCrateWeaponRate, crate_weapons().size());
        else
            t += std::string("\n\n") + (mine ? mine->name : "Your force") + " cannot use the Supply Crate. Please purchase another character before you purchase a Supply Crate.";
        ui::paragraph(page, 3013, t, kSoft);
        price = days == 30 ? kCratePrice30 : kCratePrice7;
        can = parts > 0;
        ui::text(page, 3011, eng::str::thousands(price), kWhite);
        if (ui::button(page, 3001, can, nullptr, "Buy the crate picked and open it")) s.supply_crate(days);
    } else if (st.item_tab == 1) {
        std::vector<const SprayDef*> list;
        for (const SprayDef& sp : sprays())
            if (s.shop_line(ShopKind::Spray, sp.id).listed) list.push_back(&sp);
        int chosen = -1;
        const SprayDef* picked = nullptr;
        for (int i = 0; i < int(list.size()); ++i)
            if (int(list[size_t(i)]->id) == st.item_sel) chosen = i, picked = list[size_t(i)];
        const int clicked = ui::card_grid(page, 3007, int(list.size()), chosen, [&](const ui::Card& c) { spray_card(app, c, *list[size_t(c.index)], true); });
        if (clicked >= 0) st.item_sel = int(list[size_t(clicked)]->id);
        if (list.empty()) ui::text_at(359, 240, 996, 270, "No sprays are on sale.", kDim, Align::Center, true);
        spray_preview(app, page, 4013, 3013, picked);
        if (picked) {
            const ShopLine line = s.shop_line(ShopKind::Spray, picked->id);
            price = line.cheapest() ? line.cheapest()->price : 0;
            can = line.listed && s.spray_seconds_left(picked->id, app.now()) != OwnedItem::kForGood;
        }
        ui::text(page, 3011, eng::str::thousands(price), kWhite);
        if (ui::button(page, 3001, can, nullptr, "Buy the spray picked")) open_buy(app, ShopKind::Spray, picked->id);
    } else {
        std::vector<const ItemDef*> list;
        for (const ItemDef& d : items())
            if (d.kind == ItemKind::Boost && s.shop_line(ShopKind::Item, d.id).listed) list.push_back(&d);
        int chosen = -1;
        const ItemDef* picked = nullptr;
        for (int i = 0; i < int(list.size()); ++i)
            if (int(list[size_t(i)]->id) == st.item_sel) chosen = i, picked = list[size_t(i)];
        const int clicked = ui::card_grid(page, 3007, int(list.size()), chosen, [&](const ui::Card& c) { item_card(app, c, *list[size_t(c.index)], true); });
        if (clicked >= 0) st.item_sel = int(list[size_t(clicked)]->id);
        item_preview(app, page, 4013, 3013, picked);
        if (picked) {
            const ShopLine line = s.shop_line(ShopKind::Item, picked->id);
            price = line.offers.empty() ? 0 : line.offers.front().price;
            can = line.listed;
        }
        ui::text(page, 3011, eng::str::thousands(price), kWhite);
        if (ui::button(page, 3001, can, nullptr, "Buy the item picked")) open_buy(app, ShopKind::Item, picked->id);
    }
    if (ui::button(page, 3002, true, nullptr, "Put it back")) st.item_sel = -1;
    holdings(app, page, 3009, 3010);
    if (ui::button(page, 3008, true, nullptr, "Back")) leave_shop(app);
    bottom_strip(app, "Item Shop");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The inventory's items (PageItem) ──

void item_inventory(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageItem");
    ui::page_begin("##page_item");
    const Nav nav = common_chrome(app, 3);
    ui::draw_static(page, {11, 111});
    inventory_common(app, page);
    std::vector<const ItemDef*> list;
    for (const proto::OwnedItem& o : s.owned_items)
        if (const ItemDef* d = item(o.id); d && d->kind == ItemKind::Boost && item_listed(*d)) list.push_back(d);
    int chosen = -1;
    const ItemDef* picked = nullptr;
    for (int i = 0; i < int(list.size()); ++i)
        if (int(list[size_t(i)]->id) == st.item_sel) chosen = i, picked = list[size_t(i)];
    const int clicked = ui::card_grid(page, 7, int(list.size()), chosen, [&](const ui::Card& c) { item_card(app, c, *list[size_t(c.index)], false); });
    if (clicked >= 0) st.item_sel = int(list[size_t(clicked)]->id);
    if (list.empty()) ui::text_at(359, 190, 996, 220, "You own no items yet: the Item Shop sells them.", kDim, Align::Center, true);
    item_preview(app, page, 111, 11, picked);
    bottom_strip(app, "Inventory");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The inventory's sprays (PageItem's grid): which one you carry into a match ──

void spray_inventory(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageItem");
    ui::page_begin("##page_sprays");
    const Nav nav = common_chrome(app, 3);
    ui::draw_static(page, {11, 111});
    inventory_common(app, page);
    std::vector<const SprayDef*> list;
    for (const proto::OwnedItem& o : s.owned_sprays)
        if (const SprayDef* sp = spray(o.id); sp && s.spray_seconds_left(sp->id, app.now())) list.push_back(sp);
    int chosen = -1;
    const SprayDef* picked = nullptr;
    for (int i = 0; i < int(list.size()); ++i)
        if (int(list[size_t(i)]->id) == st.item_sel) chosen = i, picked = list[size_t(i)];
    if (!picked)
        for (int i = 0; i < int(list.size()); ++i)
            if (list[size_t(i)]->id == s.spray) chosen = i, picked = list[size_t(i)];
    const int clicked = ui::card_grid(page, 7, int(list.size()), chosen, [&](const ui::Card& c) { spray_card(app, c, *list[size_t(c.index)], false); });
    if (clicked >= 0) st.item_sel = int(list[size_t(clicked)]->id);
    if (list.empty()) ui::text_at(359, 190, 996, 240, "You have no sprays: the Item Shop sells them, and the capsules hold some.", kDim, Align::Center, true);
    spray_preview(app, page, 111, 11, picked);
    bottom_strip(app, "Inventory");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The Gift tab: duffle bags and gift boxes (Game/Events.hpp), opened for a prize ──

void gift_inventory(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ui::Page& page = lobby_page(app, "PageItem");
    ui::page_begin("##page_gift");
    const Nav nav = common_chrome(app, 3);
    ui::draw_static(page, {11, 111});
    inventory_common(app, page);
    s.refresh_tv_shop();   // the Duffle Bags kept here are TV's: what each holds is TVAS's word
    // What waits here: friends' gifts first (each its own card), then the boxes by kind.
    struct Entry {
        const proto::GiftInfo* gift = nullptr;
        u8 box = 0;
        const proto::OwnedItem* bag = nullptr;   // one of the shop's Duffle Bags, bought
        int pick() const { return bag ? kBagPick + int(bag->id) : gift ? kGiftPick + int(gift->id) : int(box); }
    };
    std::vector<Entry> list;
    for (const proto::GiftInfo& g : s.gifts) list.push_back({&g, 0, nullptr});
    for (const proto::OwnedItem& o : s.bags)
        if (o.uses) list.push_back({nullptr, 0, &o});
    if (s.rewards)
        for (u8 b = 0; b < kBoxKinds; ++b)
            if (s.rewards->boxes[b]) list.push_back({nullptr, b, nullptr});
    int chosen = -1;
    for (int i = 0; i < int(list.size()); ++i)
        if (list[size_t(i)].pick() == st.gift_sel) chosen = i;
    if (chosen < 0 && !list.empty()) chosen = 0, st.gift_sel = list[0].pick();
    const int clicked = ui::card_grid(page, 7, int(list.size()), chosen, [&](const ui::Card& c) {
        const Entry& e = list[size_t(c.index)];
        const float by = c.y1 - 27, bx = c.x1 - 96;
        if (e.bag) {
            const CapsuleDef* def = s.bag_def(e.bag->id);
            card_head(c, def ? def->name : std::string("Duffle Bag"), eng::str::format("x %u", unsigned(e.bag->uses)), kWhite);
            ui::picture_at(ui::atlas()->picture(sf::Pack::Lobby, def ? def->picture : std::string(kBagPicture)), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
            if (ui::card_button(32000 + int(e.bag->id % 2000) * 2, "using_1", bx + 48, by, def != nullptr, def ? "Open it" : "The shop no longer knows this bag")) {
                st.gift_sel = e.pick();
                s.open_bag(e.bag->id);
            }
            return;
        }
        if (e.gift) {
            const proto::GiftInfo& g = *e.gift;
            const ShopKind kind = ShopKind(std::min<u8>(g.kind, u8(ShopKind::Count) - 1));
            card_head(c, ware_name(kind, g.item), "From " + g.from, kGreen);
            ui::picture_at(ware_picture(app, kind, g.item), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
            ui::text_at(c.x0 + 6, c.y1 - 26, c.x0 + 100, c.y1 - 4, "GIFT", kLime, Align::Left, true, 11.0f);
            if (ui::card_button(31000 + int(g.id % 4000) * 2, "using_1", bx + 48, by, true, "Open the gift: it is yours")) {
                st.gift_sel = e.pick();
                s.open_gift(g.id);
            }
            return;
        }
        const BoxInfo& info = box_info(e.box);
        card_head(c, info.name, eng::str::format("x %u", unsigned(s.rewards->boxes[e.box])), kWhite);
        ui::picture_at(ui::atlas()->picture(sf::Pack::Lobby, info.picture), c.x0 + 10, c.y0 + 42, c.x1 - 10, c.y1 - 30, 0xFFFFFFFF, true);
        if (ui::card_button(30000 + e.box * 2, "using_1", bx, by, !s.box_opened, "Open it")) {
            st.gift_sel = e.box;
            st.box_opened_at = -1;
            s.open_box(e.box);
        }
    });
    if (clicked >= 0) st.gift_sel = list[size_t(clicked)].pick();
    if (list.empty())
        ui::text_wrapped(420, 190, 940, 260, "Nothing here yet. A friend's gift and a Duffle Bag from the Item Shop wait here to be opened; a promotion brings a Duffle Bag; events and the day's quests bring gift boxes.", kDim);
    if (st.gift_sel >= kBagPick) {
        if (const CapsuleDef* def = s.bag_def(u16(st.gift_sel - kBagPick))) {
            ui::picture(page, 111, ui::atlas()->picture(sf::Pack::Lobby, def->picture), true);
            ui::paragraph(page, 11, def->name + "\n\nOne of the shop's Duffle Bags. Opened, it holds one of these:" + holds_text(*def), kSoft);
        }
    } else if (st.gift_sel >= kGiftPick) {
        for (const proto::GiftInfo& g : s.gifts) {
            if (kGiftPick + int(g.id) != st.gift_sel) continue;
            const ShopKind kind = ShopKind(std::min<u8>(g.kind, u8(ShopKind::Count) - 1));
            ui::picture(page, 111, ware_picture(app, kind, g.item), true);
            const ItemDef* d = kind == ShopKind::Item ? item(g.item) : nullptr;
            std::string t = ware_name(kind, g.item) + "\n\nA gift from " + g.from + ": " + offer_days_text(kind, g.item, g.days) + ".";
            if (d) t += std::string("\n\n") + d->info;
            t += "\n\nOpen it and it is yours, as if you had bought it. What you already own for good comes as SP instead.";
            ui::paragraph(page, 11, t, kSoft);
        }
    } else if (st.gift_sel >= 0 && st.gift_sel < kBoxKinds) {
        const BoxInfo& info = box_info(u8(st.gift_sel));
        ui::picture(page, 111, ui::atlas()->picture(sf::Pack::Lobby, info.picture), true);
        std::string t = std::string(info.name) + "\n\n" + info.about + "\n\nWhat one holds, and how often:";
        if (s.rewards)
            for (const LootTable& table : s.rewards->tables)
                if (table.box == u8(st.gift_sel)) {
                    u32 total = 0;
                    for (const Prize& p : table.prizes) total += p.weight;
                    for (const Prize& p : table.prizes)
                        t += eng::str::format("\n  %.1f%%  ", 100.0 * double(p.weight) / double(std::max(1u, total))) + prize_text(p);
                }
        ui::paragraph(page, 11, t, kSoft);
    }
    bottom_strip(app, "Inventory");
    ui::page_end();
    handle_nav(app, nav);
}

}  // namespace

void draw_shop(App& app) {
    switch (app.state().shop_tab) {
        case 0: weapon_shop(app); break;
        case 1: character_shop(app); break;
        case 3: item_shop(app); break;
        case 4: capsule_shop(app); break;
        default:
            if (app.state().inv_tab == 0) character_inventory(app);
            else if (app.state().inv_tab == 2) item_inventory(app);
            else if (app.state().inv_tab == 3) gift_inventory(app);
            else if (app.state().inv_tab == 4) spray_inventory(app);
            else weapon_inventory(app);
            break;
    }
    if (app.state().buy_item >= 0) buy_dialog(app);
    if (app.state().use_item >= 0) use_dialog(app);
    if (app.state().deal_item >= 0) deal_dialog(app);
}

}  // namespace lsf
