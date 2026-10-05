#include "Game/Items.hpp"

#include "Game/Registry.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lsf {

namespace {

const ItemDef kItems[] = {
#include "Game/SfItems.inl"
};

const std::unordered_map<u16, const ItemDef*>& by_id() {
    static const auto map = [] {
        std::unordered_map<u16, const ItemDef*> m;
        for (const ItemDef& d : kItems) m.emplace(d.id, &d);
        return m;
    }();
    return map;
}

const ItemRange& zone_range(const ItemDef& d, int zone) { return zone == 0 ? d.head : zone == 1 ? d.upper : d.legs; }

template <typename Fn>
void each_part(std::span<const u16> equipped, u8 force, Fn fn) {
    for (u16 id : equipped)
        if (const ItemDef* d = item(id); d && d->kind == ItemKind::Part && d->force == force) fn(*d);
}

}  // namespace

std::span<const ItemDef> items() { return kItems; }

const ItemDef* item(u16 id) {
    auto it = by_id().find(id);
    if (it != by_id().end()) return it->second;
    // A server's own part (ids from kFirstPackItem: NM-6), for as long as its packs are mounted.
    if (id >= kFirstPackItem)
        if (const PackItem* p = registry::item(id)) return &p->def;
    return nullptr;
}

PartTotals part_totals(std::span<const u16> equipped, u8 force) {
    PartTotals t;
    float cap[3] = {0, 0, 0};
    each_part(equipped, force, [&](const ItemDef& d) {
        t.speed += d.speed;
        t.head += d.head.max, t.upper += d.upper.max, t.legs += d.legs.max;
        cap[0] = std::max(cap[0], d.head.cap), cap[1] = std::max(cap[1], d.upper.cap), cap[2] = std::max(cap[2], d.legs.cap);
        t.point += d.point.max;
        t.clan_point += d.clan_point;
        t.fall_damage = std::min(t.fall_damage, d.fall_damage);
    });
    t.head = std::min(t.head, cap[0]), t.upper = std::min(t.upper, cap[1]), t.legs = std::min(t.legs, cap[2]);
    return t;
}

const SetInfo& set_info(ItemSet s) {
    static const SetInfo kSets[size_t(ItemSet::Count)] = {{"", 0}, {"Viper", 100}, {"Black Dragon", 0}, {"White Tiger", 50}, {"Rabbit", 50}, {"Santa", 0}};
    return kSets[size_t(s) < size_t(ItemSet::Count) ? size_t(s) : 0];
}

ItemSet set_of(const ItemDef& d) {
    if (d.kind != ItemKind::Part) return ItemSet::None;
    std::string model = d.model;
    for (char& c : model) c = char(std::tolower((unsigned char)c));
    if (model.find("viperset") != std::string::npos) return ItemSet::Viper;
    if (model.find("blackdragonset") != std::string::npos) return ItemSet::BlackDragon;
    if (model.find("rabbitset") != std::string::npos) return ItemSet::Rabbit;
    if (model.ends_with("_tiger")) return ItemSet::WhiteTiger;
    if (std::string_view(d.name).starts_with("Santa ")) return ItemSet::Santa;
    return ItemSet::None;
}

ItemSet full_set(std::span<const u16> equipped, u8 force) {
    // The slots each set has for each force, from the table, once.
    static const auto kSlots = [] {
        std::unordered_map<u32, std::vector<i16>> out;
        for (const ItemDef& d : items())
            if (const ItemSet s = set_of(d); s != ItemSet::None) {
                auto& slots = out[(u32(s) << 8) | d.force];
                if (std::find(slots.begin(), slots.end(), d.slot) == slots.end()) slots.push_back(d.slot);
            }
        return out;
    }();
    for (int k = 1; k < int(ItemSet::Count); ++k) {
        const auto need = kSlots.find((u32(k) << 8) | force);
        if (need == kSlots.end() || need->second.size() < 3) continue;
        size_t covered = 0;
        for (i16 slot : need->second) {
            bool on = false;
            for (u16 id : equipped)
                if (const ItemDef* d = item(id); d && d->force == force && d->slot == slot && set_of(*d) == ItemSet(k)) on = true;
            covered += on;
        }
        if (covered == need->second.size()) return ItemSet(k);
    }
    return ItemSet::None;
}

float roll_protection(std::span<const u16> equipped, u8 force, int zone, float (*random)()) {
    float total = 0, cap = 0;
    each_part(equipped, force, [&](const ItemDef& d) {
        const ItemRange& r = zone_range(d, zone);
        if (!r.any()) return;
        total += r.min + (r.max - r.min) * random();
        cap = std::max(cap, r.cap);
    });
    return std::clamp(std::min(total, cap), 0.0f, 90.0f) / 100.0f;
}

float roll_point(std::span<const u16> equipped, u8 force, float (*random)()) {
    float total = 0;
    each_part(equipped, force, [&](const ItemDef& d) {
        if (d.point.any()) total += d.point.min + (d.point.max - d.point.min) * random();
    });
    return std::max(0.0f, total) / 100.0f;
}

bool boost_includes(Boost owned, Boost wanted) {
    if (owned == wanted) return true;
    if (owned == Boost::SpecialPackage)
        return wanted == Boost::PointsX2 || wanted == Boost::DoubleUp || wanted == Boost::QuickSwitch || wanted == Boost::HeadshotPoints;
    return false;
}

const char* name_colour_name(int index) {
    static const char* const kNames[] = {"Plain", "Gold", "Lime", "Sky", "Orange", "Pink", "Red", "Violet", "White"};
    return index >= 0 && index < kNameColourCount ? kNames[index] : "Plain";
}

}  // namespace lsf
