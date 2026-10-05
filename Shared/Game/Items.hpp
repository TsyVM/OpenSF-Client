// Soldier Front's own items: the character parts the Character Shop sells and the Item Shop's
// boosts, from the client's tables (Tools/sf_items.py writes SfItems.inl; Docs/SfItems.md lists
// them). Everything is bought with SP, for a number of days or for good.
//
// A part is one force's: the same helmet for GSG-9 and for Delta is two items. Its numbers, read
// off equipitems.kst, are
//
//   speed               added to moving speed the way the force's own is shown (+0.5 = 5 %)
//   head / upper / legs protection {cap, min, max} %: each part takes between min and max per cent
//                       off a hit on that zone, rolled per hit; a zone's parts together never
//                       take off more than its cap (30 in every row the client has)
//   point               special point {cap, min, max} %: more SP at the match's end, rolled per match
//   clan point          clan points a match
//   fall damage         per cent off falling damage (-50)
//
// A boost does what the Item Shop said it did (Boost below); it works from the moment it is bought
// until it runs out, or, for a one-use item (a code name change, a record reset), until it is used.
#pragma once

#include "Engine/Core/Types.hpp"

#include <span>
#include <string_view>

namespace lsf {

using eng::i16;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::u8;

enum class ItemKind : u8 { Part, Boost };

enum class Boost : u8 {
    None,
    PointsX2,          // double rank points (XP) for a match played to its end
    DoubleUp,          // 30 % more rank points and double SP, half of the extra SP into the recharge bar
    QuickSwitch,       // weapons are drawn in half the time
    HeadshotPoints,    // 5 % more SP for every head shot
    ColorName,         // your code name in a colour of your choosing, glowing
    ColorClanName,     // your clan's name the same
    FakeRank,          // a rank mark of your choosing (Staff Sergeant .. First Lieutenant) shown to others
    Crosshair,         // retired: the crosshair is every player's to shape, in the options (the id is kept: accounts hold it)
    PreventTeamKill,   // a friendly grenade does you no harm
    HolyBless,         // your deaths are not counted
    NameChange,        // one use: a new code name
    ResetKillDeath,    // one use: kills and deaths back to zero
    ResetVictory,      // one use: wins and losses back to zero
    ResetDesertion,    // one use: games left before their end and team kills back to zero
    SpecialPackage,    // Points X2, Double Up, Quick Switch and Headshot Points together
};

struct ItemRange {
    float cap = 0, min = 0, max = 0;
    bool any() const { return max > 0; }
};

struct ItemOffer {
    u16 days = 0;   // 0: for good (a part) or one use (a boost)
    u32 price = 0;  // SP
};

struct ItemDef {
    u16 id;
    const char* code;       // the client's own ("B2134", "E1010")
    const char* name;
    const char* info;       // the shop's description
    ItemKind kind;
    u8 force;               // the force a part fits (Rules' index); 0xFF: anyone
    i16 slot;               // equipitems' CHR_ITEM: one part a slot
    const char* tab;        // the Character Shop's tab: Head, Face, Torso, Arms, Legs, Feet, Accessory
    const char* model;      // the force archive's piece or accessory ("sf_c_gsg9_hand_winter")
    const char* picture;    // the lobby's picture ("..._lb.tga"); empty when none
    float speed;
    ItemRange head, upper, legs, point;
    int clan_point;
    int fall_damage;        // per cent, negative = less damage
    int rank;               // the rank it needs (0 = anyone)
    Boost boost;
    ItemOffer offers[4];

    int offer_count() const {
        int n = 0;
        while (n < 4 && (offers[n].price || offers[n].days)) ++n;
        return n;
    }
    bool one_use() const { return kind == ItemKind::Boost && offers[0].days == 0; }
};

std::span<const ItemDef> items();
const ItemDef* item(u16 id);
// Whether the shops sell it and the inventory lists it. The CrossHair item is neither: what it
// sold is free in the options now.
inline bool item_listed(const ItemDef& d) { return !(d.kind == ItemKind::Boost && d.boost == Boost::Crosshair); }

// What a soldier's parts for `force` add up to: speed, and each zone's protection and the special
// point at their most (the character shop's stat box shows these), capped.
struct PartTotals {
    float speed = 0;
    float head = 0, upper = 0, legs = 0;   // per cent
    float point = 0;                       // per cent
    int clan_point = 0;
    int fall_damage = 0;
};
PartTotals part_totals(std::span<const u16> equipped, u8 force);

// ── Set items (equipitems.kst: the Viper, Black Dragon, White Tiger, Rabbit and Santa pieces) ──
//
// A set is worn whole when a piece of it is on in every slot the set has for the soldier's force
// (as the shop lists them). Worn whole it marks his row on the Tab board, and the three the
// original's own descriptions give a number for pay extra rank points for a match played to its
// end: Viper 100% ("a 100% bonus to EXP when the full set of Viper gear is worn"), Rabbit 50%;
// White Tiger says "additional EXP" without a number, and has Rabbit's here. Black Dragon and
// Santa are a mark alone (the client's own icons for them).
enum class ItemSet : u8 { None, Viper, BlackDragon, WhiteTiger, Rabbit, Santa, Count };
struct SetInfo {
    const char* name;   // "Viper"
    int xp_pct;         // extra rank points, per cent
};
const SetInfo& set_info(ItemSet s);
ItemSet set_of(const ItemDef& d);
ItemSet full_set(std::span<const u16> equipped, u8 force);

// What marks a soldier's row on the Tab board (the client's inf/tab/icon art): his boosts, the
// parts' special point, a set worn whole, and an event running.
enum RowMark : u16 {
    kMarkPointsX2 = 1 << 0,      // point2x.bmp
    kMarkDoubleUp = 1 << 1,      // doubleup.bmp
    kMarkHolyBless = 1 << 2,     // holy bless_32.tga
    kMarkSpecial = 1 << 3,       // special_point.tga: parts that add special point, or a set that pays
    kMarkBlackDragon = 1 << 4,   // setitem_blackdragon.bmp
    kMarkSanta = 1 << 5,         // santa.bmp
    kMarkExpX3 = 1 << 6,         // pointx3.bmp: an event paying three times the rank points
    kMarkExpX5 = 1 << 7,         // pointx5.bmp: five times
    kMarkEvent = 1 << 8,         // guerillaevent.tga: an event paying more
};

// One hit's protection on a zone (0 head, 1 upper body, 2 legs), rolled from `random` (0..1 per
// part, in order): a fraction to take off the damage.
float roll_protection(std::span<const u16> equipped, u8 force, int zone, float (*random)());
// The match's special point bonus, rolled: a fraction to add to the SP.
float roll_point(std::span<const u16> equipped, u8 force, float (*random)());

// What a boost gives, as the Special Package counts: does `boost` (or a package holding it) apply?
bool boost_includes(Boost owned, Boost wanted);

// The colours a Colored Codename (and Clanname) picks from; 0 is the plain one. ARGB.
inline constexpr u32 kNameColours[] = {0xFFE2E3DC, 0xFFFFD24A, 0xFFB6F23A, 0xFF4AE2FF, 0xFFFF8A2A,
                                       0xFFFF5AC8, 0xFFFF4A3A, 0xFFB08CFF, 0xFFFFFFFF};
inline constexpr int kNameColourCount = int(sizeof(kNameColours) / sizeof(kNameColours[0]));
const char* name_colour_name(int index);

// The ranks a Fake Rank Mark may show: Staff Sergeant 1 .. First Lieutenant 5 (Rules' rank indices).
inline constexpr int kFakeRankFirst = 5, kFakeRankLast = 29;

}  // namespace lsf
