#include "Game/Registry.hpp"

#include "Engine/Core/Strings.hpp"

namespace lsf {

namespace {
SessionContent g_content;
}  // namespace

namespace registry {

void set(SessionContent content) {
    g_content = std::move(content);
    // The strings a def points at live in the entries themselves: point them again after the move.
    for (PackForce& f : g_content.forces) {
        f.def.model = f.model.c_str();
        f.def.name = f.name.c_str();
        f.def.nation = f.nation.c_str();
        f.def.art = f.art.c_str();
    }
    for (PackItem& i : g_content.items) {
        i.def.code = i.code.c_str();
        i.def.name = i.name.c_str();
        i.def.info = i.info.c_str();
        i.def.tab = i.tab.c_str();
        i.def.model = i.model.c_str();
        i.def.picture = i.picture.c_str();
    }
}

void clear() { g_content = SessionContent{}; }
const SessionContent& content() { return g_content; }
bool empty() { return g_content.weapons.empty() && g_content.forces.empty() && g_content.items.empty() && g_content.maps.empty(); }

const PackForce* force(u8 id) {
    for (const PackForce& f : g_content.forces)
        if (f.def.id == id) return &f;
    return nullptr;
}

const PackItem* item(u16 id) {
    for (const PackItem& i : g_content.items)
        if (i.def.id == id) return &i;
    return nullptr;
}

const PackMap* map(std::string_view id) {
    for (const PackMap& m : g_content.maps)
        if (eng::str::iequals(m.id, id)) return &m;
    return nullptr;
}

const WeaponDef* weapon_by_code(std::string_view code) {
    for (const WeaponDef& w : g_content.weapons)
        if (w.code == code) return &w;
    return nullptr;
}

const PackForce* force_by_code(std::string_view code) {
    for (const PackForce& f : g_content.forces)
        if (f.code == code) return &f;
    return nullptr;
}

const PackItem* item_by_code(std::string_view code) {
    for (const PackItem& i : g_content.items)
        if (i.code == code) return &i;
    return nullptr;
}

}  // namespace registry

std::vector<const WeaponDef*> session_weapons() {
    std::vector<const WeaponDef*> out;
    for (const WeaponDef& w : weapons()) out.push_back(&w);
    for (const WeaponDef& w : g_content.weapons) out.push_back(&w);
    return out;
}

std::vector<const ForceDef*> session_forces() {
    std::vector<const ForceDef*> out;
    for (const ForceDef& f : forces()) out.push_back(&f);
    for (const PackForce& f : g_content.forces) out.push_back(&f.def);
    return out;
}

std::vector<const ItemDef*> session_items() {
    std::vector<const ItemDef*> out;
    for (const ItemDef& d : items()) out.push_back(&d);
    for (const PackItem& i : g_content.items) out.push_back(&i.def);
    return out;
}

const WeaponDef* pack_weapon(u16 id) {
    const auto& w = g_content.weapons;
    if (id < kFirstPackWeapon) return nullptr;
    const size_t at = size_t(id - kFirstPackWeapon);
    if (at < w.size() && w[at].id == id) return &w[at];
    for (const WeaponDef& d : w)
        if (d.id == id) return &d;
    return nullptr;
}

}  // namespace lsf
