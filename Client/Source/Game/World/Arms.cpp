// Guns on the floor (proto::DropWeapon, PickUpWeapon; the original's config.cfg WEAPON_DROP and
// ACQUIRE_WEAPON): the gun in hand put down, the one at your feet taken up, each drawn lying where
// it fell, and the line that says which key takes it. The server keeps what lies where (an
// Objective::Weapon each, `who` the weapon's id) and says who holds what (proto::Rearm).
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

using eng::Mat4;
using eng::Vec3;
using namespace proto;

void GameWorld::drop_weapon() {
    if (!alive_ || offline_ || watching_ || me_undead()) return;
    // A primary or a sidearm: the knife and the grenades stay with you.
    if (slot_ > 1 || loadout_[size_t(slot_)] == kNoWeapon) return;
    DropWeapon m;
    m.slot = u8(slot_);
    m.clip = u16(std::clamp(clip_[size_t(slot_)], 0, 65535));
    m.reserve = u16(std::clamp(reserve_[size_t(slot_)], 0, 65535));
    app_.session().send(m);
}

void GameWorld::pick_up_weapon() {
    if (!alive_ || offline_ || watching_ || me_undead() || !weapon_at_feet()) return;
    // What is left in your own two, for whichever goes down in its place.
    PickUpWeapon m;
    for (size_t k = 0; k < 2; ++k) {
        m.clip[k] = u16(std::clamp(clip_[k], 0, 65535));
        m.reserve[k] = u16(std::clamp(reserve_[k], 0, 65535));
    }
    app_.session().send(m);
}

const ObjectiveNow* GameWorld::weapon_at_feet() const {
    const ObjectiveNow* best = nullptr;
    float best_d = kPickUpReach;
    for (const ObjectiveNow& o : mode_state_.objectives)
        if (Objective(o.kind) == Objective::Weapon)
            if (const float d = eng::length(o.at - move_.origin); d <= best_d) best = &o, best_d = d;
    return best;
}

int GameWorld::weapons_on_floor() const {
    int n = 0;
    for (const ObjectiveNow& o : mode_state_.objectives) n += Objective(o.kind) == Objective::Weapon;
    return n;
}

void GameWorld::draw_floor_weapons() {
    const double now = app_.now();
    for (const ObjectiveNow& o : mode_state_.objectives) {
        if (Objective(o.kind) != Objective::Weapon) continue;
        Carried* c = carried_for(u16(o.who));
        if (!c) continue;
        // The size it has in a soldier's hand: the hand's own scale, measured off anyone posed.
        if (c->hand_scale <= 0)
            for (auto& [id, p] : players_) {
                const int hand = p.body.model ? p.body.model->find_bone("Bip01 R Hand") : -1;
                if (hand < 0 || size_t(hand) >= p.body.model_space.size()) continue;
                const Mat4 m = c->model->attach * p.body.model_space[size_t(hand)];
                c->hand_scale = eng::length(m.transform_vector(Vec3{1, 0, 0}));
                break;
            }
        if (c->hand_scale <= 0) continue;
        // On its side: the thinnest way up, the rest along the floor, turned as it fell.
        const eng::Aabb box = c->model->model.bounds();
        if (!box.valid()) continue;
        const Vec3 ext = box.max - box.min;
        Mat4 lay;
        float thin = ext.y;
        if (ext.x <= ext.y && ext.x <= ext.z) lay = Mat4::rotation_z(1.5708f), thin = ext.x;
        else if (ext.z <= ext.y) lay = Mat4::rotation_x(1.5708f), thin = ext.z;
        const float s = c->hand_scale;
        const float spin = float((o.who * 73u + u32(std::fabs(o.at.x) + std::fabs(o.at.z))) % 360u);
        const Mat4 world = Mat4::translation(c->centre * -1.0f) * lay * Mat4::scale(Vec3{s, s, s}) * Mat4::yaw(spin) *
                           Mat4::translation(o.at + Vec3{0, thin * 0.5f * s + 0.5f, 0});
        // In its last five seconds it blinks before it goes.
        if (o.timer < 5.0f && std::fmod(now * 4.0, 1.0) < 0.35) continue;
        renderer_.draw_model(*c->gpu, world, nullptr);
    }
}

void GameWorld::draw_pick_up_prompt() {
    if (!alive_ || me_undead()) return;
    const ObjectiveNow* o = weapon_at_feet();
    const WeaponDef* w = o ? weapon(u16(o->who)) : nullptr;
    if (!w) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const float W = ui::stage_w(), H = ui::stage_h();
    const std::string key = eng::Input::binding_name(app_.settings().bind(Action::PickUpWeapon));
    const std::string line = key.empty() ? "Bind a key to Pick up a weapon to take the " + w->name : key + "   Pick up the " + w->name;
    const float y = H - 238;
    ui::text(dl, ui::font_bold(), 17, W * 0.5f + 1.5f, y + 1.5f, VAN_COL32(0, 0, 0, 200), line, ui::Align::Center, false);
    ui::text(dl, ui::font_bold(), 17, W * 0.5f, y, VAN_COL32(255, 222, 96, 255), line, ui::Align::Center, false);
}

}  // namespace lsf
