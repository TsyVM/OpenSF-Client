// The shops as a Game Master keeps them: what is on sale, for how long and for how much, the
// capsule machine's capsules, and the line that pans along the bottom of the lobby. The server keeps it (shop.cfg beside
// accounts.cfg), tells every client, and decides every sale by it; the client shows it, and a Game
// Master edits it in the staff panel (F9, Shop). Docs/Shop.md has the numbers and where they come from.
//
// The catalog starts as the game's own: every weapon of the roster (Game/Rules.hpp) for 7, 30 or
// 90 days or for good, every force for good, every item with the offers the client's tables give
// it (Game/Items.hpp), every spray for 1, 7 or 30 days. What a Game Master changes is kept as an
// entry over that; an entry that says what the catalog already says is not kept.
#pragma once

#include "Game/Wear.hpp"
#include "Game/Items.hpp"
#include "Game/Rules.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lsf {

// ── Sprays (spraytex.kst; the effect archive's spray/) ─────────────────────────

struct SprayDef {
    u16 id;                // 1 .. 16
    const char* code;      // the client's item code ("E131")
    const char* name;      // "Spray 01"
    const char* texture;   // the effect archive's ("spray/spray_ch_001.tga")
    const char* picture;   // the lobby's can ("spraycan_001_lb.bmp")
};
std::span<const SprayDef> sprays();
const SprayDef* spray(u16 id);
const SprayDef* spray_by_code(std::string_view code);
inline constexpr u16 kNoSpray = 0;
// How near a wall a soldier must stand to spray it, and how big it comes out (centimetres).
inline constexpr float kSprayReach = 180.0f, kSpraySize = 90.0f;

// ── The catalog ────────────────────────────────────────────────────────────────

enum class ShopKind : u8 { Weapon = 0, Force = 1, Item = 2, Spray = 3, Count };
const char* shop_kind_name(u8 kind);   // "weapon"

struct ShopOffer {
    u16 days = 0;      // 0: for good (a one-use item: one use)
    u32 price = 0;     // SP
    template <typename S> void serialize(S& s) { s.u16(days), s.u32(price); }
    bool operator==(const ShopOffer&) const = default;
};
inline constexpr size_t kShopOffers = 4;
// The lengths a Game Master may put a thing on sale for (0: for good).
inline constexpr u16 kShopDays[] = {1, 3, 7, 15, 30, 60, 90, 180, 365, 0};
inline constexpr u32 kShopPriceMax = 10000000;

// A Game Master's word on one thing: whether it is on sale, and its offers.
struct ShopEntry {
    u8 kind = 0;       // ShopKind
    u16 id = 0;
    bool listed = true;
    std::vector<ShopOffer> offers;
    bool giftable = true;   // whether it may be bought for a friend (a Game Master's to say; as sold: it may)
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(id), s.boolean(listed), s.list(offers, kShopOffers), s.boolean(giftable); }
    bool operator==(const ShopEntry&) const = default;
};

// ── The capsule machine (capsule.kst; the lobby's PageLottoShop) ───────────────

enum class CapsulePrizeKind : u8 { Weapon, Boost, Spray, Sp, Coin, Count };
const char* capsule_prize_kind_name(u8 kind);
struct CapsulePrize {
    u8 kind = 0;        // CapsulePrizeKind
    u16 id = 0;         // the weapon, the boost (an item), the spray (0: any one)
    u16 days = 0;       // Weapon, Boost, Spray: for how long (a weapon's 0: for good)
    u32 lo = 0, hi = 0; // Sp, Coin: how much
    u16 weight = 1;
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(id), s.u16(days), s.u32(lo), s.u32(hi), s.u16(weight); }
    bool operator==(const CapsulePrize&) const = default;
};
struct CapsuleDef {
    u16 id = 0;
    std::string name;       // "UZI Capsule"
    std::string picture;    // the lobby's capsule art ("gold uzi_cs.tga")
    u8 coins = 1;           // what a turn of it costs
    bool listed = true;
    std::vector<CapsulePrize> prizes;
    u32 price = 0;          // a Duffle Bag's (ShopConfig::bags): SP for one; a capsule's is its coins
    template <typename S> void serialize(S& s) { s.u16(id), s.str(name, 40), s.str(picture, 64), s.u8(coins), s.boolean(listed), s.list(prizes, 16), s.u32(price); }
    bool operator==(const CapsuleDef&) const = default;
};
inline constexpr int kBagsOwnedMax = 99;        // of one Duffle Bag, unopened
inline constexpr const char* kBagPicture = "dufflebag_a_lb.bmp";   // the lobby's, when a bag names none
// "M4A1 for 7 days", "Spray 03 for a day", "500 - 1,500 SP".
std::string capsule_prize_text(const CapsulePrize& p);

// ── All of it ──────────────────────────────────────────────────────────────────

inline constexpr size_t kMarqueeMax = 240;
struct ShopConfig {
    std::vector<ShopEntry> entries;                        // where the catalog was changed
    std::string marquee;                                   // the lobby's line along the bottom
    u32 coin_price = 3000;                                 // SP a capsule coin
    std::vector<CapsuleDef> capsules;
    // "If gifted: % discount": what a Game Master takes off anything bought for a friend (0: none).
    u8 gift_discount = 0;
    // The Duffle Bags a Game Master makes and sells (the Item Shop's Duffle Bag tab): each a name, a
    // picture, an SP price and what it may hold, a prize drawn by weight when it is opened from the
    // inventory's Gift tab. Kept as the capsules are (CapsuleDef: `price` instead of `coins`).
    std::vector<CapsuleDef> bags;
    template <typename S> void serialize(S& s) {
        s.list(entries, 1024), s.str(marquee, kMarqueeMax);
        s.u32(coin_price), s.list(capsules, 32);
        s.u8(gift_discount), s.list(bags, 32);
    }
    bool operator==(const ShopConfig&) const = default;
    const CapsuleDef* capsule(u16 id) const;
    const CapsuleDef* bag(u16 id) const;
};
ShopConfig default_shop();
// The house's own line, when a Game Master has written none.
const char* default_marquee();
// Everything within its range; entries for things that are not there, and those that only repeat
// the catalog, dropped.
void sanitize(ShopConfig& c);
// The server's shop.cfg, and back.
std::string shop_text(const ShopConfig& c);
bool shop_from_text(std::string_view text, ShopConfig& out, std::string* why = nullptr);

// One thing as the shops have it now.
struct ShopLine {
    bool exists = false;     // the game has such a thing, and it may be sold at all
    bool listed = false;
    std::vector<ShopOffer> offers;
    bool giftable = true;    // may be bought for a friend
    const ShopOffer* cheapest() const;
};
// The game's own offers for a thing (empty: not for sale, whatever a Game Master says).
std::vector<ShopOffer> default_offers(ShopKind kind, u16 id);
ShopLine shop_line(const ShopConfig& c, ShopKind kind, u16 id);
// What a thing costs bought for good now: the shop's for-good offer, else what the game lists it
// at. Mending a gun and selling anything back are shares of it (Game/Wear.hpp).
u32 for_good_price(const ShopConfig& c, ShopKind kind, u16 id);
// What an offer costs sent as a gift: its price less the shop's gift discount (never under 1 SP
// for a thing that costs anything).
inline constexpr int kGiftDiscountMax = 90;   // per cent
u32 gift_price(const ShopConfig& c, u32 price);
// What selling a gun back brings: its own share less its wear.
u32 gun_resale(const ShopConfig& c, const WeaponDef& w, u8 durability);

// ── The Supply Crate (supplycrate*.kst; gametext 495, 853) ─────────────────────
//
// "The Supply Crate contains various pieces of equipment that users can use regardless of rank
// limitations": one opened gives a random part for the force you play, for 7 or 30 days as the
// crate says, or (2 in a hundred, the tables' own rate) one of the crates' guns for good. A force
// with no parts cannot use one ("ARTC cannot use the Supply Crate"). The original sold them for
// cash; here they cost SP.
inline constexpr u32 kCratePrice7 = 12000, kCratePrice30 = 30000;
inline constexpr int kCrateWeaponRate = 2;   // in a hundred
// The parts a crate may hold for a force (those the shop lists for it), and the crates' guns that
// are in the roster (supplycrate_weapon.kst).
std::vector<u16> crate_parts(u8 force);
std::vector<u16> crate_weapons();
// A Game Master's edit of one thing (dropped again when it says what the catalog says).
void set_shop_line(ShopConfig& c, ShopKind kind, u16 id, bool listed, std::vector<ShopOffer> offers, bool giftable = true);
// A weapon's rentals as the catalog prices them off what it costs for good: 7, 30 and 90 days.
std::vector<ShopOffer> rental_ladder(u32 price_for_good);
// "M4A1", "Delta Force", "Points X2", "Spray 03"; and the code a config file names it by.
std::string ware_name(ShopKind kind, u16 id);
std::string ware_code(ShopKind kind, u16 id);
bool ware_from_code(ShopKind kind, std::string_view code, u16& id);
// "7 days", "For good", "One use".
std::string offer_days_text(ShopKind kind, u16 id, u16 days);

}  // namespace lsf
