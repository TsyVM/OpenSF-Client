// A gun's wear, its mending, and what anything sells back for (sf_arms.pduration; gametext 343-344,
// 652-653, 783, 1083, 1528-1532; 345-346, 798-806).
//
// A gun bought from the shop wears as it is used: a point for every match played to its end with
// it carried, another for every kWearRounds rounds fired from it, and kWearLeftEarly more for
// leaving a game before its end (the original's "gun-endurance penalty"). Training wears nothing;
// an issued gun (the starter kit's M4A1 and Glock 23: Rules.hpp issued()), a knife and a grenade
// never wear, so there is always a whole gun to carry. At 0 it is broken: it is not carried into a
// match (the starter's gun takes its slot) until it is mended. Mending costs kRepairShare of the
// gun's price for the whole of it, a point's share for each point mended (a broken 30,000 SP rifle
// comes back for 4,500).
#pragma once

#include "Game/Rules.hpp"

namespace lsf {

inline constexpr u8 kDurabilityFull = 100;
inline constexpr u8 kWearMatch = 1;
inline constexpr u32 kWearRounds = 150;
inline constexpr u8 kWearLeftEarly = 5;
inline constexpr u8 kDurabilityLow = 20;      // the lobby warns from here down
inline constexpr float kRepairShare = 0.15f;
// Whether a gun wears at all: one bought (a price, and not the starter kit's), a primary or a sidearm.
bool wears(const WeaponDef& w);
// SP to mend a gun of this price from `durability` to full (0 when there is nothing to mend).
u32 repair_cost(u32 price, u8 durability);
// Selling back: a share of what it costs now, less its wear; a rented thing, an issued one, a staff
// variant or anything bought with cash cannot be sold.
inline constexpr float kResaleShare = 0.3f;
u32 resale_price(u32 price, u8 durability = kDurabilityFull);

// How far a shot is heard (centimetres).
inline constexpr float kShotEarshot = 3000.0f;

}  // namespace lsf
