#include "Game/Wear.hpp"

#include <algorithm>
#include <cmath>

namespace lsf {

bool wears(const WeaponDef& w) { return w.price > 0 && !w.admin_only && !issued(w.id) && (w.slot == Slot::Primary || w.slot == Slot::Secondary); }

u32 repair_cost(u32 price, u8 durability) {
    if (durability >= kDurabilityFull) return 0;
    const float missing = float(kDurabilityFull - durability) / float(kDurabilityFull);
    return std::max<u32>(1, u32(std::ceil(float(price) * kRepairShare * missing)));
}

u32 resale_price(u32 price, u8 durability) {
    return u32(std::floor(float(price) * kResaleShare * float(std::min(durability, kDurabilityFull)) / float(kDurabilityFull)));
}

}  // namespace lsf
