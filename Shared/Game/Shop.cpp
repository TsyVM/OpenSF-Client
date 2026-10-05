#include "Game/Shop.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsf {

namespace {

// spraytex.kst's sixteen: CODE, NAME, TEX_FILENAME; the lobby's can for the ones it has a picture of.
const SprayDef kSprays[] = {
    {1, "E131", "Spray 01", "spray/spray_ch_001.tga", "spraycan_001_lb.bmp"}, {2, "E132", "Spray 02", "spray/spray_ch_002.tga", "spraycan_002_lb.bmp"},
    {3, "E133", "Spray 03", "spray/spray_ch_003.tga", "spraycan_003_lb.bmp"}, {4, "E134", "Spray 04", "spray/spray_ch_004.tga", "spraycan_004_lb.bmp"},
    {5, "E152", "Spray 05", "spray/spray_ch_005.tga", "spraycan_005_lb.bmp"}, {6, "E170", "Spray 06", "spray/spray_ch_006.tga", "spraycan_006_lb.bmp"},
    {7, "E171", "Spray 07", "spray/spray_ch_007.tga", "spraycan_007_lb.bmp"}, {8, "E172", "Spray 08", "spray/spray_ch_008.tga", "spraycan_008_lb.bmp"},
    {9, "E180", "Spray 09", "spray/spray_ch_009.tga", "spraycan_009_lb.bmp"}, {10, "E181", "Spray 10", "spray/spray_ch_010.tga", "spraycan_010_lb.bmp"},
    {11, "E182", "Spray 11", "spray/spray_ch_011.tga", "spraycan_011_lb.bmp"}, {12, "E193", "Spray 12", "spray/spray_ch_012.tga", ""},
    {13, "E194", "Spray 13", "spray/spray_ch_013.tga", ""},                    {14, "E195", "Spray 14", "spray/spray_ch_014.tga", "spraycan_014_lb.bmp"},
    {15, "E199", "Spray 15", "spray/spray_ch_015.tga", "spraycan_015_lb.bmp"}, {16, "E381", "Spray 16", "spray/spray_ch_016.tga", "spraycan_016_lb.bmp"},
};

const char* const kKinds[] = {"weapon", "force", "item", "spray"};
const char* const kPrizeKinds[] = {"weapon", "boost", "spray", "sp", "coin"};

u32 round_price(double p) { return std::max<u32>(100, u32(p / 100.0 + 0.5) * 100); }

std::vector<std::string> words(std::string_view line, size_t n) {
    std::vector<std::string> out;
    std::string_view s = eng::str::trim(line);
    while (!s.empty() && out.size() < n) {
        const size_t sp = s.find_first_of(" \t");
        out.emplace_back(s.substr(0, sp));
        s = sp == std::string_view::npos ? std::string_view() : eng::str::trim(s.substr(sp));
    }
    out.resize(n);
    return out;
}

u32 number(std::string_view s) {
    const std::string t(eng::str::trim(s));
    return u32(std::min<unsigned long long>(std::strtoull(t.c_str(), nullptr, 10), 0xFFFFFFFFull));
}

int kind_of(const char* const* table, size_t n, std::string_view key) {
    for (size_t i = 0; i < n; ++i)
        if (eng::str::iequals(table[i], key)) return int(i);
    return -1;
}

const ItemDef* boost_item(Boost b) {
    for (const ItemDef& d : items())
        if (d.kind == ItemKind::Boost && d.boost == b && !d.one_use()) return &d;
    return nullptr;
}

std::string days_words(u16 days) { return days == 1 ? std::string("a day") : std::to_string(unsigned(days)) + " days"; }

// The capsules of capsule.kst that hold a gun of the roster: each is named for its gun (the
// original's were named for a skinned edition of it, which the roster keeps for the staff).
struct CapsuleSeed {
    const char* name;
    const char* picture;
    const char* gun;
};
const CapsuleSeed kCapsules[] = {
    {"UZI Capsule", "gold uzi_cs.tga", "B204"},          {"M4A1 Capsule", "skull m4a1_cs.tga", "A006"},
    {"FN FAL Capsule", "phantom fn fal_cs.tga", "B101"},  {"AK-74 Capsule", "reaper ak74_cs.tga", "A013"},
    {"HK416 Capsule", "supernova hk416_cs.tga", "A277"},  {"SCAR-H Capsule", "gold scar-h_cs.tga", "B104"},
    {"MG36 Capsule", "big crocodile mg36_cs.tga", "A014"}, {"Steyr AUG Capsule", "horror_tar-21_cs.tga", "A008"},
    {"WA2000 Capsule", "dragon wa 2000_cs.tga", "A153"},  {"PSG-1 Capsule", "phoenix psg-1_cs.tga", "A009"},
    {"K2 Capsule", "thunder k2_cs.tga", "A011"},          {"M110 Capsule", "black m110_cs.tga", "B302"},
};

}  // namespace

std::span<const SprayDef> sprays() { return kSprays; }

const SprayDef* spray(u16 id) { return id >= 1 && id <= std::size(kSprays) ? &kSprays[id - 1] : nullptr; }

const SprayDef* spray_by_code(std::string_view code) {
    for (const SprayDef& s : kSprays)
        if (eng::str::iequals(s.code, code)) return &s;
    return nullptr;
}

const char* shop_kind_name(u8 kind) { return kind < std::size(kKinds) ? kKinds[kind] : "?"; }
const char* capsule_prize_kind_name(u8 kind) {
    static const char* const kNames[] = {"Weapon", "Boost", "Spray", "SP", "Coins"};
    return kind < std::size(kNames) ? kNames[kind] : "?";
}

std::string capsule_prize_text(const CapsulePrize& p) {
    switch (CapsulePrizeKind(p.kind)) {
        case CapsulePrizeKind::Weapon: {
            const WeaponDef* w = weapon(p.id);
            return (w ? w->name : std::string("a weapon")) + (p.days ? " for " + days_words(p.days) : std::string(" for good"));
        }
        case CapsulePrizeKind::Boost: {
            const ItemDef* d = item(p.id);
            return std::string(d ? d->name : "a boost") + " for " + days_words(std::max<u16>(1, p.days));
        }
        case CapsulePrizeKind::Spray: {
            const SprayDef* s = spray(p.id);
            return std::string(s ? s->name : "a spray") + " for " + days_words(std::max<u16>(1, p.days));
        }
        case CapsulePrizeKind::Sp:
            return (p.hi > p.lo ? eng::str::thousands(p.lo) + " - " + eng::str::thousands(p.hi) : eng::str::thousands(p.lo)) + " SP";
        case CapsulePrizeKind::Coin:
            return p.hi > p.lo ? eng::str::format("%u - %u coins", p.lo, p.hi) : eng::str::format("%u coin%s", p.lo, p.lo == 1 ? "" : "s");
        default: return "nothing";
    }
}

const CapsuleDef* ShopConfig::bag(u16 id) const {
    for (const CapsuleDef& c : bags)
        if (c.id == id) return &c;
    return nullptr;
}

const CapsuleDef* ShopConfig::capsule(u16 id) const {
    for (const CapsuleDef& c : capsules)
        if (c.id == id) return &c;
    return nullptr;
}

const char* default_marquee() { return "Enjoy Soldier Front Legacy!!     Report cheaters to the staff.     Invite your friends!"; }

std::vector<ShopOffer> rental_ladder(u32 price) {
    return {{7, round_price(double(price) * 0.12)}, {30, round_price(double(price) * 0.35)}, {90, round_price(double(price) * 0.70)}};
}

std::vector<ShopOffer> default_offers(ShopKind kind, u16 id) {
    std::vector<ShopOffer> out;
    switch (kind) {
        case ShopKind::Weapon: {
            const WeaponDef* w = weapon(id);
            if (!w || w->admin_only || w->price == 0) break;
            // A grenade is a few hundred SP: for good, as it always was. Everything else is rented
            // for 7, 30 or 90 days, or bought.
            if (w->klass != WeaponClass::Grenade) out = rental_ladder(w->price);
            out.push_back({0, w->price});
            break;
        }
        case ShopKind::Force: {
            const ForceDef* f = force(u8(id));
            if (f && id < 256 && f->price > 0) out.push_back({0, f->price});
            break;
        }
        case ShopKind::Item: {
            const ItemDef* d = item(id);
            if (!d || !item_listed(*d)) break;
            for (int i = 0; i < d->offer_count(); ++i) out.push_back({d->offers[i].days, d->offers[i].price});
            break;
        }
        case ShopKind::Spray:
            if (spray(id)) out = {{1, 300}, {7, 1500}, {30, 5000}};
            break;
        default: break;
    }
    return out;
}

const ShopOffer* ShopLine::cheapest() const {
    const ShopOffer* best = nullptr;
    for (const ShopOffer& o : offers)
        if (!best || o.price < best->price) best = &o;
    return best;
}

ShopLine shop_line(const ShopConfig& c, ShopKind kind, u16 id) {
    ShopLine line;
    line.offers = default_offers(kind, id);
    line.exists = !line.offers.empty();
    line.listed = line.exists;
    if (!line.exists) return line;
    for (const ShopEntry& e : c.entries)
        if (e.kind == u8(kind) && e.id == id) {
            line.listed = e.listed && !e.offers.empty();
            if (!e.offers.empty()) line.offers = e.offers;
            line.giftable = e.giftable;
            break;
        }
    return line;
}

u32 for_good_price(const ShopConfig& c, ShopKind kind, u16 id) {
    for (const ShopOffer& o : shop_line(c, kind, id).offers)
        if (o.days == 0) return o.price;
    switch (kind) {
        case ShopKind::Weapon: return weapon(id) ? weapon(id)->price : 0;
        case ShopKind::Force: return force(u8(id)) ? force(u8(id))->price : 0;
        case ShopKind::Item:
            if (const ItemDef* d = item(id))
                for (int k = 0; k < d->offer_count(); ++k)
                    if (d->offers[k].days == 0) return d->offers[k].price;
            return 0;
        default: return 0;
    }
}

u32 gift_price(const ShopConfig& c, u32 price) {
    const u32 pct = std::min<u32>(c.gift_discount, u32(kGiftDiscountMax));
    if (price == 0 || pct == 0) return price;
    return std::max<u32>(1, u32(u64(price) * (100u - pct) / 100u));
}

u32 gun_resale(const ShopConfig& c, const WeaponDef& w, u8 durability) { return resale_price(for_good_price(c, ShopKind::Weapon, w.id), durability); }

std::vector<u16> crate_parts(u8 force) {
    std::vector<u16> out;
    for (const ItemDef& d : items())
        if (d.kind == ItemKind::Part && d.force == force && !default_offers(ShopKind::Item, d.id).empty()) out.push_back(d.id);
    return out;
}

std::vector<u16> crate_weapons() {
    // supplycrate_weapon.kst's fourteen, less those the roster does not have.
    std::vector<u16> out;
    for (const char* model : {"dragunov", "g3a3", "awp", "type89", "m110", "m945c", "mp7a1", "desperado", "k7", "wa2000", "spas12"})
        if (const WeaponDef* w = weapon_by_model(model); w && !w->admin_only) out.push_back(w->id);
    return out;
}

void set_shop_line(ShopConfig& c, ShopKind kind, u16 id, bool listed, std::vector<ShopOffer> offers, bool giftable) {
    std::erase_if(c.entries, [&](const ShopEntry& e) { return e.kind == u8(kind) && e.id == id; });
    const std::vector<ShopOffer> usual = default_offers(kind, id);
    if (usual.empty()) return;
    if (offers.size() > kShopOffers) offers.resize(kShopOffers);
    if (listed && giftable && offers == usual) return;
    ShopEntry e;
    e.kind = u8(kind), e.id = id, e.listed = listed, e.offers = std::move(offers), e.giftable = giftable;
    c.entries.push_back(std::move(e));
}

std::string ware_name(ShopKind kind, u16 id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (const WeaponDef* w = weapon(id)) return w->name;
            break;
        case ShopKind::Force:
            if (const ForceDef* f = id < 256 ? force(u8(id)) : nullptr) return f->name;
            break;
        case ShopKind::Item:
            if (const ItemDef* d = item(id)) return d->name;
            break;
        case ShopKind::Spray:
            if (const SprayDef* s = spray(id)) return s->name;
            break;
        default: break;
    }
    return "?";
}

std::string ware_code(ShopKind kind, u16 id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (const WeaponDef* w = weapon(id)) return w->code;
            break;
        case ShopKind::Force:
            if (const ForceDef* f = id < 256 ? force(u8(id)) : nullptr) return f->model;
            break;
        case ShopKind::Item:
            if (const ItemDef* d = item(id)) return d->code;
            break;
        case ShopKind::Spray:
            if (const SprayDef* s = spray(id)) return s->code;
            break;
        default: break;
    }
    return {};
}

bool ware_from_code(ShopKind kind, std::string_view code, u16& id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (const WeaponDef* w = weapon_by_code(code)) return id = w->id, true;
            break;
        case ShopKind::Force:
            if (const ForceDef* f = force_by_model(code)) return id = f->id, true;
            break;
        case ShopKind::Item:
            for (const ItemDef& d : items())
                if (eng::str::iequals(d.code, code)) return id = d.id, true;
            break;
        case ShopKind::Spray:
            if (const SprayDef* s = spray_by_code(code)) return id = s->id, true;
            break;
        default: break;
    }
    return false;
}

std::string offer_days_text(ShopKind kind, u16 id, u16 days) {
    if (kind == ShopKind::Item)
        if (const ItemDef* d = item(id); d && d->one_use()) return "One use";
    return days ? (days == 1 ? std::string("1 day") : eng::str::format("%u days", unsigned(days))) : std::string("For good");
}

ShopConfig default_shop() {
    ShopConfig c;
    c.marquee = default_marquee();
    c.coin_price = 3000;
    const ItemDef* double_up = boost_item(Boost::DoubleUp);
    u16 id = 1;
    for (const CapsuleSeed& seed : kCapsules) {
        const WeaponDef* w = weapon_by_code(seed.gun);
        if (!w) continue;
        CapsuleDef cap;
        cap.id = id++;
        cap.name = seed.name;
        cap.picture = seed.picture;
        const u8 gun = u8(CapsulePrizeKind::Weapon);
        cap.prizes = {{gun, w->id, 0, 0, 0, 2}, {gun, w->id, 30, 0, 0, 8}, {gun, w->id, 7, 0, 0, 20}, {gun, w->id, 1, 0, 0, 30}};
        cap.prizes.push_back({u8(CapsulePrizeKind::Spray), 0, 1, 0, 0, 12});
        cap.prizes.push_back({u8(CapsulePrizeKind::Spray), 0, 7, 0, 0, 5});
        if (double_up) cap.prizes.push_back({u8(CapsulePrizeKind::Boost), double_up->id, 1, 0, 0, 10});
        cap.prizes.push_back({u8(CapsulePrizeKind::Sp), 0, 0, 500, 1500, 13});
        c.capsules.push_back(std::move(cap));
    }
    return c;
}

void sanitize(ShopConfig& c) {
    c.marquee = eng::str::sanitize_line(c.marquee, kMarqueeMax);
    if (c.marquee.empty()) c.marquee = default_marquee();
    // One entry a thing, for things that are there; its offers within range, none twice for the
    // same length; an entry that only repeats the catalog is not one.
    std::vector<ShopEntry> kept;
    for (ShopEntry& e : c.entries) {
        if (e.kind >= u8(ShopKind::Count)) continue;
        const std::vector<ShopOffer> usual = default_offers(ShopKind(e.kind), e.id);
        if (usual.empty()) continue;
        if (std::any_of(kept.begin(), kept.end(), [&](const ShopEntry& o) { return o.kind == e.kind && o.id == e.id; })) continue;
        const ItemDef* one_use = e.kind == u8(ShopKind::Item) ? item(e.id) : nullptr;
        std::vector<ShopOffer> offers;
        for (ShopOffer o : e.offers) {
            o.price = std::min(o.price, kShopPriceMax);
            o.days = std::min<u16>(o.days, 3650);
            if (one_use && one_use->one_use()) o.days = 0;
            if (std::any_of(offers.begin(), offers.end(), [&](const ShopOffer& x) { return x.days == o.days; })) continue;
            if (offers.size() < kShopOffers) offers.push_back(o);
        }
        // For good last, the rentals shortest first.
        std::stable_sort(offers.begin(), offers.end(), [](const ShopOffer& a, const ShopOffer& b) { return (a.days ? a.days : 0xFFFFu) < (b.days ? b.days : 0xFFFFu); });
        e.offers = std::move(offers);
        if (e.offers.empty()) e.listed = false;
        if (e.listed && e.giftable && e.offers == usual) continue;
        kept.push_back(std::move(e));
    }
    c.entries = std::move(kept);
    c.coin_price = std::clamp<u32>(c.coin_price, 1, kShopPriceMax);
    c.gift_discount = u8(std::min<int>(c.gift_discount, kGiftDiscountMax));
    // The machine's capsules and the shop's Duffle Bags are kept alike: each a name, a picture, what
    // it costs, and its prizes.
    auto tidy = [](std::vector<CapsuleDef>& list, bool bag) {
        if (list.size() > 32) list.resize(32);
        u16 next = 1;
        for (const CapsuleDef& cap : list) next = std::max<u16>(next, u16(cap.id + 1));
        for (size_t i = 0; i < list.size(); ++i) {
            CapsuleDef& cap = list[i];
            const bool twice = std::any_of(list.begin(), list.begin() + std::ptrdiff_t(i), [&](const CapsuleDef& o) { return o.id == cap.id; });
            if (cap.id == 0 || twice) cap.id = next++;
            cap.name = eng::str::sanitize_line(cap.name, 40);
            if (cap.name.empty()) cap.name = bag ? "Duffle Bag" : "Capsule";
            cap.picture = eng::str::sanitize_line(cap.picture, 64);
            cap.coins = u8(std::clamp<int>(cap.coins, 1, 20));
            cap.price = bag ? std::clamp<u32>(cap.price, 1, kShopPriceMax) : 0;
            if (bag && cap.picture.empty()) cap.picture = kBagPicture;
            if (cap.prizes.size() > 16) cap.prizes.resize(16);
            std::erase_if(cap.prizes, [](const CapsulePrize& p) {
                switch (CapsulePrizeKind(p.kind)) {
                    case CapsulePrizeKind::Weapon: {
                        const WeaponDef* w = weapon(p.id);
                        return !w || w->admin_only || w->price == 0;   // never a staff variant, never the issued knife
                    }
                    case CapsulePrizeKind::Boost: {
                        const ItemDef* d = item(p.id);
                        return !d || d->kind != ItemKind::Boost || d->one_use() || !item_listed(*d);
                    }
                    case CapsulePrizeKind::Spray: return p.id != 0 && !spray(p.id);
                    case CapsulePrizeKind::Sp:
                    case CapsulePrizeKind::Coin: return false;
                    default: return true;
                }
            });
            for (CapsulePrize& p : cap.prizes) {
                p.weight = std::clamp<u16>(p.weight, 1, 1000);
                p.days = std::min<u16>(p.days, 365);
                if (p.kind != u8(CapsulePrizeKind::Weapon)) p.days = p.kind == u8(CapsulePrizeKind::Sp) || p.kind == u8(CapsulePrizeKind::Coin) ? u16(0) : std::max<u16>(1, p.days);
                const u32 most = p.kind == u8(CapsulePrizeKind::Coin) ? 100u : 1000000u;
                p.lo = std::min(p.lo, most), p.hi = std::clamp(p.hi, p.lo, most);
                if (p.kind == u8(CapsulePrizeKind::Coin)) p.lo = std::max(1u, p.lo), p.hi = std::max(p.lo, p.hi);
            }
        }
    };
    tidy(c.capsules, false);
    tidy(c.bags, true);
}

std::string shop_text(const ShopConfig& c) {
    eng::ConfigFile cfg;
    eng::ConfigSection& root = cfg.section("");
    root.set("marquee", c.marquee);
    root.set("coin_price", std::to_string(c.coin_price));
    if (c.gift_discount) root.set("gift_discount", std::to_string(unsigned(c.gift_discount)));   // per cent off anything sent as a gift
    for (const ShopEntry& e : c.entries) {
        const std::string code = ware_code(ShopKind(e.kind), e.id);
        if (code.empty()) continue;
        eng::ConfigSection s;
        s.name = "entry";
        s.set("ware", std::string(shop_kind_name(e.kind)) + " " + code);
        s.set("name", ware_name(ShopKind(e.kind), e.id));   // for whoever reads the file; not read back
        s.set("listed", e.listed ? "true" : "false");
        if (!e.giftable) s.set("giftable", "false");   // not to be bought for a friend
        for (const ShopOffer& o : e.offers) s.values.emplace_back("offer", eng::str::format("%u %u", unsigned(o.days), o.price));
        cfg.sections.push_back(std::move(s));
    }
    for (const bool bag : {false, true})
        for (const CapsuleDef& cap : bag ? c.bags : c.capsules) {
            eng::ConfigSection s;
            s.name = bag ? "bag" : "capsule";
            s.set("id", std::to_string(cap.id));
            s.set("name", cap.name);
            s.set("picture", cap.picture);
            if (bag) s.set("price", std::to_string(cap.price));
            else s.set("coins", std::to_string(cap.coins));
            s.set("listed", cap.listed ? "true" : "false");
            for (const CapsulePrize& p : cap.prizes) {
                std::string code = "-";
                if (p.kind == u8(CapsulePrizeKind::Weapon)) code = ware_code(ShopKind::Weapon, p.id);
                else if (p.kind == u8(CapsulePrizeKind::Boost)) code = ware_code(ShopKind::Item, p.id);
                else if (p.kind == u8(CapsulePrizeKind::Spray) && p.id) code = ware_code(ShopKind::Spray, p.id);
                s.values.emplace_back("prize", eng::str::format("%s %s %u %u %u %u", kPrizeKinds[p.kind < std::size(kPrizeKinds) ? p.kind : 0], code.c_str(), unsigned(p.days),
                                                                p.lo, p.hi, unsigned(p.weight)));
            }
            cfg.sections.push_back(std::move(s));
        }
    return "# Soldier Front Legacy shop. Written by the server; a Game Master edits it in the game (F9, Shop).\n"
           "# [entry]: a thing whose sale differs from the game's own catalog (offer = days price; 0 days: for good).\n"
           "# Docs/Shop.md has what each line means.\n" +
           cfg.serialize();
}

bool shop_from_text(std::string_view text, ShopConfig& out, std::string* why) {
    eng::ConfigFile cfg;
    if (!eng::ConfigFile::parse(text, cfg, why)) return false;
    ShopConfig c = default_shop();
    const eng::ConfigSection& root = cfg.root();
    if (root.has("marquee")) c.marquee = root.get_string("marquee");
    if (root.has("coin_price")) c.coin_price = number(root.get("coin_price"));
    if (root.has("gift_discount")) c.gift_discount = u8(std::min<u32>(number(root.get("gift_discount")), u32(kGiftDiscountMax)));
    // A shop.cfg written while guns could be upgraded still reads: its attach, enchant and element
    // lines are left unread, and gone at the next save.
    for (const eng::ConfigSection* s : cfg.all("entry")) {
        const auto w = words(s->get("ware"), 2);
        const int kind = kind_of(kKinds, std::size(kKinds), w[0]);
        u16 id = 0;
        if (kind < 0 || !ware_from_code(ShopKind(kind), w[1], id)) continue;
        ShopEntry e;
        e.kind = u8(kind), e.id = id, e.listed = s->get_bool("listed", true), e.giftable = s->get_bool("giftable", true);
        for (std::string_view line : s->get_all("offer")) {
            const auto o = words(line, 2);
            e.offers.push_back({u16(std::min<u32>(number(o[0]), 65535)), number(o[1])});
        }
        c.entries.push_back(std::move(e));
    }
    // A file with capsules of its own has those, and only those; its Duffle Bags are [bag] sections, kept the same way.
    if (!cfg.all("capsule").empty()) c.capsules.clear();
    for (const bool bag : {false, true})
        for (const eng::ConfigSection* s : cfg.all(bag ? "bag" : "capsule")) {
            CapsuleDef cap;
            cap.price = bag ? number(s->get("price")) : 0;
            cap.id = u16(std::max(0, s->get_int("id", 0)));
            cap.name = s->get_string("name");
            cap.picture = s->get_string("picture");
            cap.coins = u8(std::clamp(s->get_int("coins", 1), 1, 20));
            cap.listed = s->get_bool("listed", true);
            for (std::string_view line : s->get_all("prize")) {
                const auto w = words(line, 6);
                const int kind = kind_of(kPrizeKinds, std::size(kPrizeKinds), w[0]);
                if (kind < 0) continue;
                CapsulePrize p;
                p.kind = u8(kind);
                if (w[1] != "-" && !w[1].empty()) {
                    const ShopKind ware = kind == int(CapsulePrizeKind::Weapon) ? ShopKind::Weapon : kind == int(CapsulePrizeKind::Boost) ? ShopKind::Item : ShopKind::Spray;
                    if (kind <= int(CapsulePrizeKind::Spray) && !ware_from_code(ware, w[1], p.id)) continue;
                } else if (kind != int(CapsulePrizeKind::Spray) && kind <= int(CapsulePrizeKind::Boost)) {
                    continue;
                }
                p.days = u16(std::min<u32>(number(w[2]), 365));
                p.lo = number(w[3]), p.hi = number(w[4]);
                p.weight = u16(std::clamp<u32>(number(w[5]), 1, 1000));
                cap.prizes.push_back(p);
            }
            (bag ? c.bags : c.capsules).push_back(std::move(cap));
        }
    sanitize(c);
    out = std::move(c);
    return true;
}

}  // namespace lsf
