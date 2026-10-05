// What a fight leaves in the air and on the walls, in the Soldier Front client's own art:
//
//   - spent cases (weapon cartridge/cartridge.fpd, cartridge_sg.fpd for the shotguns) thrown out of
//     the gun's `cartridge` node, right and up from the shooter, tumbling, bouncing off the map and
//     lying where they land a while;
//   - a bullet's mark where it struck the map (weapon mark/bulletmark01..03, painted dark) and the
//     dust it knocks off (effect dust/dust1, bulletcrashsmoke);
//   - what running feet and landings kick up, by what is underfoot: dust off soil, sand and mud,
//     grass, snow, a splash and droplets in water;
//   - blood where a soldier is hit (effect dust/blood, bloodcake) and splashed on the wall or the
//     floor behind him (weapon mark/blood1..6);
//   - a grenade in flight as the model it is carried as (weapon.kst OBJECT), and going off: a frag's
//     flash (objectsmoke/object_boom1), fireball (particle/explosion01), the rising plume
//     (explosion/boom1..13, two high by one wide), dust and a scorch; a flash-bang's light; a smoke
//     grenade's cloud (smokegrenade/smokegrenade).
//
// None of it is told to the server: every client draws it from the shots, hits and throws it hears of.
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace lsf {

using eng::Mat4;
using eng::Vec3;

namespace {

// A case's size: the .fpd is in the guns' view-model units (0.233 cm); a little larger than life
// so a case reads at arm's length, as the original's do.
constexpr float kCaseScale = 0.2f;
constexpr double kCaseLife = 6.0;
constexpr size_t kMaxCases = 96, kMaxMarks = 220, kMaxPuffs = 400;

std::mt19937 g_fx{0x5EED};
float frand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_fx); }

constexpr u32 abgr(int r, int g, int b, int a) { return u32(a) << 24 | u32(b) << 16 | u32(g) << 8 | u32(r); }
u32 with_alpha(u32 c, float a) { return (c & 0x00FFFFFFu) | u32(std::clamp(a, 0.0f, 1.0f) * float(c >> 24)) << 24; }

}  // namespace

void GameWorld::load_effect_art() {
    TextureCache& tc = renderer_.textures();
    auto weapon_art = [&](const std::string& key) { return &tc.get(sf::Pack::Weapon, std::nullopt, key); };
    auto effect_art = [&](const std::string& key) { return &tc.get(sf::Pack::Effect, std::nullopt, key); };
    for (int i = 0; i < 3; ++i) fx_.bullet_mark[i] = weapon_art(eng::str::format("mark/bulletmark%02d.tga", i + 1));
    for (int i = 0; i < 6; ++i) fx_.blood_mark[i] = weapon_art(eng::str::format("mark/blood%d.tga", i + 1));
    fx_.scorch = weapon_art("mark/dust1.tga");
    fx_.blood_puff = effect_art("dust/blood.tga");
    fx_.blood_cake = effect_art("dust/bloodcake1.tga");
    fx_.impact_smoke = effect_art("bulletcrashsmoke/bulletcrashsmoke1.tga");
    fx_.dust = effect_art("dust/dust1.tga");
    fx_.fireball = effect_art("particle/explosion01.tga");
    fx_.flash = effect_art("objectsmoke/object_boom1.tga");
    for (int i = 0; i < 13; ++i) fx_.plume[i] = effect_art(eng::str::format("explosion/boom%d.tga", i + 1));
    fx_.smoke = effect_art("smokegrenade/smokegrenade.tga");
    fx_.shadow = &tc.get_mask(sf::Pack::Effect, "shadow/shadow.bmp");
    for (int i = 0; i < 3; ++i) fx_.step_dust[i] = effect_art(eng::str::format("dust/dust%d.tga", i + 1));
    fx_.grass = effect_art("particle/particle_grass.tga");
    fx_.snow = effect_art("particle/particle_snow.tga");
    fx_.splash = effect_art("bulletcrashsmoke/water_splash.tga");
    fx_.drop = effect_art("particle/particle_water.tga");
    for (int k = 0; k < 2; ++k)
        if (case_models_[k] && !case_gpu_[k]) case_gpu_[k] = renderer_.upload_model(*case_models_[k], sf::Pack::Weapon);
    // The sprays the soldiers carry in (the effect archive's spray/).
    for (const auto& [id, p] : players_)
        if (const SprayDef* s = lsf::spray(p.spray); s && !spray_art_.contains(s->id)) spray_art_[s->id] = &tc.get_keyed(sf::Pack::Effect, s->texture);
}

bool GameWorld::spray() {
    PlayerView* me = players_.contains(me_) ? &players_[me_] : nullptr;
    const SprayDef* s = me ? lsf::spray(me->spray) : nullptr;
    if (!alive_ || !s) {
        if (alive_ && !s && !offline_) ui::toast(ui::Toast::Info, "You carry no spray: the Item Shop sells them (Inventory, Spray item, to carry one).");
        return false;
    }
    if (sprayed_) return false;   // once a life
    if (!collision_) return false;
    const Vec3 fwd = camera_.forward();
    const eng::TraceResult tr = collision_->trace_ray(camera_.eye, camera_.eye + fwd * kSprayReach);
    if (!tr.hit() || tr.start_solid) return false;
    proto::Spray m;
    m.at = tr.end;
    m.normal = tr.normal;
    sprayed_ = true;
    if (offline_) {
        spray_marks_.push_back({m.at, m.normal, s->id, me_, app_.now()});
        return true;
    }
    app_.session().send(m);
    return true;
}

void GameWorld::draw_sprays() {
    // Upright on a wall (the decal's own frame mirrors it: its u runs right to left as seen).
    static const float kUv[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    for (const SprayMark& m : spray_marks_) {
        auto it = spray_art_.find(m.spray);
        if (it == spray_art_.end() || !it->second) continue;
        renderer_.decal(*it->second, m.at, m.normal, kSpraySize, abgr(255, 255, 255, 235), 0, kUv);
    }
}

void GameWorld::eject_case(const Vec3& port, const Vec3& right, const Vec3& up, const Vec3& forward, const Vec3& carried_by, u16 wid) {
    const WeaponDef* w = weapon(wid);
    if (!app_.settings().spent_cases) return;
    if (!w || w->klass == WeaponClass::Knife || w->klass == WeaponClass::Grenade || w->magazine <= 0) return;
    Case c;
    c.kind = w->klass == WeaponClass::Shotgun ? 1 : 0;
    if (!case_gpu_[c.kind]) return;
    c.pos = port;
    // Out to the right and up, a little back, with whatever the shooter was moving at.
    c.vel = right * frand(150, 230) + up * frand(140, 220) - forward * frand(10, 50) + carried_by;
    c.yaw = std::atan2(right.x, right.z);   // its length across the gun, along the throw
    c.pitch = frand(-0.3f, 0.3f);
    c.pitch_rate = frand(14, 26) * (g_fx() & 1 ? 1.0f : -1.0f);
    c.yaw_rate = frand(-8, 8);
    c.born = app_.now();
    cases_.push_back(c);
    if (cases_.size() > kMaxCases) cases_.erase(cases_.begin());
}

void GameWorld::bullet_mark(const Vec3& at, const Vec3& normal) {
    const TexInfo* tex = fx_.bullet_mark[g_fx() % 3];
    if (tex && tex->tex && app_.settings().bullet_marks) {
        marks_.push_back({at, normal, tex, frand(7, 10), frand(0, 6.28f), abgr(34, 31, 28, 235)});
        if (marks_.size() > kMaxMarks) marks_.pop_front();
    }
    if (!app_.settings().impact_dust) return;
    const double now = app_.now();
    // The chips and dust it knocks off, drifting out of the wall and up.
    Puff dust;
    dust.pos = at + normal * 2.0f;
    dust.vel = normal * 30.0f + Vec3{0, 12, 0};
    dust.tex = fx_.dust;
    dust.at = now;
    dust.life = 0.45f;
    dust.size0 = 6, dust.size1 = 26;
    dust.spin = frand(0, 6.28f);
    dust.colour = abgr(200, 192, 180, 220);
    puffs_.push_back(dust);
    Puff smoke = dust;
    smoke.tex = fx_.impact_smoke;
    smoke.vel = normal * 18.0f + Vec3{0, 20, 0};
    smoke.life = 1.1f;
    smoke.size0 = 10, smoke.size1 = 48;
    smoke.spin_rate = frand(-0.6f, 0.6f);
    smoke.colour = abgr(170, 165, 158, 150);
    puffs_.push_back(smoke);
}

void GameWorld::blood(const Vec3& at, const Vec3& dir_in, bool spray) {
    if (!app_.settings().blood) return;
    const double now = app_.now();
    const Vec3 dir = eng::length_sq(dir_in) > 1e-6f ? eng::normalize(dir_in) : Vec3{0, 0, 1};
    if (spray) {
        Puff p;
        p.pos = at;
        p.vel = dir * 60.0f;
        p.tex = fx_.blood_puff;
        p.at = now;
        p.life = 0.35f;
        p.size0 = 18, p.size1 = 62;
        p.spin = frand(0, 6.28f);
        p.colour = abgr(255, 255, 255, 240);
        puffs_.push_back(p);
        Puff c = p;
        c.tex = fx_.blood_cake;
        c.vel = dir * 120.0f - Vec3{0, 40, 0};
        c.life = 0.5f;
        c.size0 = 10, c.size1 = 40;
        c.spin = frand(0, 6.28f);
        puffs_.push_back(c);
    }
    // Splashed on what is behind him, within reach; else on the floor under him.
    if (!collision_) return;
    const TexInfo* tex = fx_.blood_mark[g_fx() % 6];
    if (!tex || !tex->tex) return;
    eng::TraceResult tr = collision_->trace_ray(at, at + dir * 170.0f);
    float size = frand(50, 80);
    if (!tr.hit()) {
        tr = collision_->trace_ray(at + dir * 40.0f, at + dir * 40.0f - Vec3{0, 260, 0});
        size = frand(40, 65);
    }
    if (!tr.hit() || tr.start_solid) return;
    marks_.push_back({tr.end, tr.normal, tex, size, frand(0, 6.28f), abgr(255, 255, 255, 230)});
    if (marks_.size() > kMaxMarks) marks_.pop_front();
}

void GameWorld::footstep_fx(const Vec3& feet, u8 material, bool landing) {
    if (!app_.settings().step_effects) return;
    // Past 40 m a puff at the boots is a pixel.
    if (eng::length_sq(feet - camera_.eye) > 4000.0f * 4000.0f) return;
    const double now = app_.now();
    const float k = landing ? 1.7f : 1.0f;
    const int n = landing ? 4 : 2;
    if (material < step_fx_.size()) ++step_fx_[material];
    auto puff = [&](const TexInfo* tex, const Vec3& vel, float life, float size0, float size1, u32 colour, float lift) {
        if (!tex || !tex->tex) return;
        Puff p;
        p.pos = feet + Vec3{frand(-7, 7), lift, frand(-7, 7)};
        p.vel = vel;
        p.tex = tex;
        p.at = now;
        p.life = life;
        p.size0 = size0 * k, p.size1 = size1 * k;
        p.spin = frand(0, 6.28f);
        p.spin_rate = frand(-0.8f, 0.8f);
        p.colour = colour;
        puffs_.push_back(p);
    };
    auto out = [&](float lo, float hi, float up_lo, float up_hi) {
        const float a = frand(0, 6.2831853f), s = frand(lo, hi) * k;
        return Vec3{std::cos(a) * s, frand(up_lo, up_hi) * k, std::sin(a) * s};
    };
    switch (material) {
        case sf::kSoundSoil:
            for (int i = 0; i < n; ++i) puff(fx_.step_dust[g_fx() % 3], out(15, 35, 12, 30), 0.75f, 10, 44, abgr(168, 148, 118, 165), 6);
            break;
        case sf::kSoundSand:
            for (int i = 0; i < n; ++i) puff(fx_.step_dust[g_fx() % 3], out(20, 45, 15, 35), 0.85f, 12, 52, abgr(214, 192, 148, 175), 6);
            break;
        case sf::kSoundMud:
            // Wet: a low, dark kick that settles at once.
            for (int i = 0; i < n; ++i) puff(fx_.step_dust[g_fx() % 3], out(10, 25, 4, 12), 0.4f, 8, 26, abgr(88, 72, 54, 190), 3);
            break;
        case sf::kSoundGrass:
            for (int i = 0; i < n + 1; ++i) puff(fx_.grass, out(20, 45, 35, 70), 0.45f, 7, 11, abgr(225, 235, 205, 235), 8);
            break;
        case sf::kSoundSnow:
            for (int i = 0; i < n; ++i) puff(fx_.snow, out(15, 40, 18, 40), 0.7f, 8, 32, abgr(242, 246, 252, 140), 6);
            break;
        case sf::kSoundWater:
            puff(fx_.splash, Vec3{0, 45, 0} * k, 0.4f, 16, 54, abgr(220, 232, 242, 210), 2);
            for (int i = 0; i < n + 2; ++i) puff(fx_.drop, out(40, 90, 90, 160), 0.35f, 4, 7, abgr(230, 240, 250, 230), 6);
            break;
        default:
            break;   // concrete, rock, metal, wood, glass: the step is heard, not seen
    }
    if (puffs_.size() > kMaxPuffs) puffs_.erase(puffs_.begin(), puffs_.begin() + std::ptrdiff_t(puffs_.size() - kMaxPuffs));
}

void GameWorld::detonate(const Vec3& at, u16 wid) {
    const WeaponDef* w = weapon(wid);
    const GrenadeKind kind = w ? w->grenade : GrenadeKind::Frag;
    const double now = app_.now();
    if (kind == GrenadeKind::Smoke) {
        smokes_.push_back({at, now + 16.0, now});
        return;
    }
    blasts_.push_back({at, now, kind != GrenadeKind::Flash});
    // A blast close by shakes the view.
    const float d = eng::length(at - camera_.eye);
    const float reach = w && w->blast_radius > 0 ? w->blast_radius * 2.5f : 1200.0f;
    if (d < reach) {
        const float felt = (kind == GrenadeKind::Flash ? 0.4f : 1.0f) * (1.0f - d / reach);
        shake_ = std::max(shake_, felt);
        if (!watching_) app_.rumble(felt, 0.35f);
    }
    if (kind == GrenadeKind::Flash) return;
    // Dust thrown out round it along the ground, and a scorch where it lay.
    for (int i = 0; i < 10; ++i) {
        const float a = float(i) / 10.0f * 6.2831853f + frand(-0.2f, 0.2f);
        Puff p;
        p.pos = at + Vec3{0, 25, 0};
        p.vel = Vec3{std::cos(a), frand(0.15f, 0.5f), std::sin(a)} * frand(180, 320);
        p.tex = fx_.impact_smoke;
        p.at = now + frand(0, 0.08f);
        p.life = frand(2.2f, 3.2f);
        p.size0 = 70, p.size1 = frand(240, 320);
        p.spin = frand(0, 6.28f);
        p.spin_rate = frand(-0.4f, 0.4f);
        p.colour = abgr(120, 112, 100, 200);
        puffs_.push_back(p);
    }
    if (collision_ && fx_.scorch && fx_.scorch->tex) {
        const eng::TraceResult tr = collision_->trace_ray(at + Vec3{0, 30, 0}, at - Vec3{0, 120, 0});
        if (tr.hit() && !tr.start_solid) {
            marks_.push_back({tr.end, tr.normal, fx_.scorch, frand(200, 260), frand(0, 6.28f), abgr(18, 16, 14, 200)});
            if (marks_.size() > kMaxMarks) marks_.pop_front();
        }
    }
}

void GameWorld::update_effects(float dt, double now) {
    // Cases fly, tumble, bounce and settle.
    for (Case& c : cases_) {
        if (c.resting) continue;
        Vec3 next = c.pos + c.vel * dt;
        c.vel.y -= 980.0f * dt;
        c.pitch += c.pitch_rate * dt;
        c.yaw += c.yaw_rate * dt;
        if (collision_) {
            const eng::TraceResult tr = collision_->trace_ray(c.pos, next);
            if (tr.hit()) {
                next = tr.end + tr.normal * 0.6f;
                c.vel = (c.vel - tr.normal * (2.0f * eng::dot(c.vel, tr.normal))) * 0.35f;
                c.pitch_rate *= 0.5f, c.yaw_rate = c.yaw_rate * 0.5f + frand(-6, 6);
                // Slow on a floor: it lies down there.
                if (tr.normal.y > 0.6f && eng::length(c.vel) < 60.0f) {
                    c.resting = true;
                    c.pitch = 0;
                }
            }
        }
        c.pos = next;
    }
    std::erase_if(cases_, [&](const Case& c) { return now - c.born > kCaseLife; });
    for (Puff& p : puffs_) {
        if (now < p.at) continue;
        p.pos = p.pos + p.vel * dt;
        p.vel = p.vel * std::max(0.0f, 1.0f - 2.2f * dt);
        p.spin += p.spin_rate * dt;
    }
    std::erase_if(puffs_, [&](const Puff& p) { return now - p.at > p.life; });
    if (puffs_.size() > kMaxPuffs) puffs_.erase(puffs_.begin(), puffs_.begin() + std::ptrdiff_t(puffs_.size() - kMaxPuffs));
    std::erase_if(blasts_, [&](const Blast& b) { return now - b.at > 1.4; });
    for (Grenade& g : grenades_) g.tumble += dt * std::min(14.0f, eng::length(g.vel) * 0.012f);
    shake_ = std::max(0.0f, shake_ - dt * 1.6f);
}

void GameWorld::draw_grenades(double now) {
    (void)now;
    for (const Grenade& g : grenades_) {
        Carried* c = carried_for(g.weapon);
        if (!c) continue;
        const Mat4 world = Mat4::translation(-c->centre) * Mat4::rotation_x(g.tumble) * Mat4::rotation_y(g.tumble * 0.6f) *
                           Mat4::scale(Vec3{c->scale, c->scale, c->scale}) * Mat4::translation(g.pos);
        renderer_.draw_model(*c->gpu, world, nullptr);
    }
}

void GameWorld::draw_feet_shadows(double now, bool cast) {
    if (app_.settings().shadows == Settings::Shadows::Off || !fx_.shadow || !fx_.shadow->tex || !collision_) return;
    // The original's: a soft round shade on the floor under every soldier, so a soldier lit only
    // by the map's baked light stands on it rather than over it. It shrinks and fades as he leaves
    // the ground. Beside a shadow cast by the sun it stays, lighter: the cast one says where the sun
    // is not, this that the boots and the floor are touching.
    const float weight = cast ? 0.30f : 0.62f;
    for (auto& [id, p] : players_) {
        if (p.hidden || !p.body.model) continue;
        if (!p.alive && now - p.snap_time > 5.0 && id != me_) continue;
        const Vec3 feet = p.position;
        const eng::TraceResult ground = collision_->trace_ray(feet + Vec3{0, 20, 0}, feet - Vec3{0, 220, 0});
        if (!ground.hit()) continue;
        const float k = std::clamp(1.0f - (feet.y - ground.end.y) / 200.0f, 0.0f, 1.0f);
        if (k <= 0.02f) continue;
        // Lying down, the body is longer than it is wide: a larger shade.
        const float size = (p.alive ? 72.0f : 120.0f) * (0.55f + 0.45f * k);
        renderer_.decal(*fx_.shadow, ground.end, ground.normal, size, abgr(0, 0, 0, int(255.0f * weight * k)));
    }
}

void GameWorld::draw_effects(double now) {
    // Spent cases: fading for their last second.
    for (const Case& c : cases_) {
        const ModelGpu* gpu = case_gpu_[c.kind].get();
        if (!gpu) continue;
        const float fade = std::clamp(float(kCaseLife - (now - c.born)), 0.0f, 1.0f);
        const Mat4 world = Mat4::scale(Vec3{kCaseScale, kCaseScale, kCaseScale}) * Mat4::rotation_x(c.pitch) * Mat4::rotation_y(c.yaw) * Mat4::translation(c.pos);
        renderer_.draw_model(*gpu, world, nullptr, {1, 1, 1, fade});
    }
    for (const Mark& m : marks_) renderer_.decal(*m.tex, m.pos, m.normal, m.size, m.colour, m.spin);
    draw_sprays();
    for (const Puff& p : puffs_) {
        if (now < p.at || !p.tex) continue;
        const float t = std::clamp(float(now - p.at) / p.life, 0.0f, 1.0f);
        const float size = p.size0 + (p.size1 - p.size0) * (1.0f - (1.0f - t) * (1.0f - t));
        renderer_.sprite(*p.tex, p.pos, size, with_alpha(p.colour, 1.0f - t * t), p.spin, nullptr, p.alpha);
    }
    for (const Blast& b : blasts_) {
        const float t = float(now - b.at);
        if (!b.frag) {
            // The flash-bang: a white burst, gone in a quarter of a second.
            if (fx_.flash && t < 0.3f) renderer_.sprite(*fx_.flash, b.pos + Vec3{0, 20, 0}, 520.0f * (0.6f + t), with_alpha(0xFFFFFFFFu, 1.0f - t / 0.3f));
            continue;
        }
        if (fx_.flash && t < 0.2f) renderer_.sprite(*fx_.flash, b.pos + Vec3{0, 40, 0}, 420.0f * (0.7f + t * 2.0f), with_alpha(0xFFFFFFFFu, 1.0f - t / 0.2f));
        if (fx_.fireball && t < 0.75f) {
            const float k = t / 0.75f;
            renderer_.sprite(*fx_.fireball, b.pos + Vec3{0, 60 + 80 * k, 0}, 170.0f + 200.0f * k, abgr(255, 255, 255, int(255 * (1.0f - k * k))), 0.3f, nullptr,
                             true);
        }
        // The plume: thirteen frames over a second and a bit, standing on the blast.
        const int frame = int(t / 1.3f * 13.0f);
        if (frame >= 0 && frame < 13 && fx_.plume[frame]) {
            const float w = 200.0f + 80.0f * t;
            renderer_.sprite(*fx_.plume[frame], b.pos + Vec3{0, w * 0.85f, 0}, w, abgr(255, 255, 255, int(255 * std::min(1.0f, (1.3f - t) * 2.0f))), 0, nullptr,
                             true, 2.0f);
        }
    }
    // Smoke grenades: a cloud building for three seconds, thinning in its last three.
    if (fx_.smoke)
        for (const Smoke& s : smokes_) {
            const float age = float(now - s.from), left = float(s.until - now);
            const float grow = std::clamp(age / 3.0f, 0.15f, 1.0f), alpha = std::clamp(left / 3.0f, 0.0f, 1.0f) * 0.9f;
            std::mt19937 r(u32(s.from * 1000.0));
            for (int i = 0; i < 18; ++i) {
                const float a = std::uniform_real_distribution<float>(0, 6.2831853f)(r);
                const float d = std::uniform_real_distribution<float>(0, 260)(r) * grow;
                const float h = std::uniform_real_distribution<float>(30, 220)(r) * grow;
                const Vec3 at = s.pos + Vec3{std::cos(a) * d, h, std::sin(a) * d};
                renderer_.sprite(*fx_.smoke, at, 300.0f * (0.5f + 0.5f * grow), abgr(205, 205, 200, int(255 * alpha)), a + age * 0.05f, nullptr, true);
            }
        }
}

bool GameWorld::test_throw() {
    for (int s = 0; s < int(kLoadoutSlots); ++s)
        if (const WeaponDef* w = my_weapon(s); w && w->klass == WeaponClass::Grenade) {
            if (slot_ != s) set_slot(s);
            clip_[size_t(s)] = std::max(clip_[size_t(s)], 1);
            draw_left_ = fire_cooldown_ = 0;
            auto_fire_ = false;
            grenade_held_ = true;   // the next frame without the trigger lets it go
            vm_clip_ = "hold";
            vm_time_ = 0;
            vm_rate_ = 1;
            vm_loop_ = false;
            return true;
        }
    return false;
}

bool GameWorld::test_throw_kind(GrenadeKind kind) {
    for (const WeaponDef& w : weapons())
        if (w.klass == WeaponClass::Grenade && w.grenade == kind && carried_for(w.id)) {
            const Vec3 dir = camera_.forward();
            Grenade g;
            g.pos = camera_.eye + dir * 55.0f;
            g.vel = dir * 700.0f + Vec3{0, 200, 0};
            g.fuse = w.fuse;
            g.weapon = w.id;
            grenades_.push_back(g);
            LOG_INFO("test: threw a %s (%s)", w.name.c_str(), w.code.c_str());
            return true;
        }
    return false;
}

bool GameWorld::flashed() const { return app_.now() < flashed_until_; }

bool GameWorld::test_blood() {
    if (!collision_) return false;
    const Vec3 fwd = camera_.forward();
    const eng::TraceResult tr = collision_->trace_ray(camera_.eye, camera_.eye + fwd * 3000.0f);
    if (!tr.hit()) return false;
    const float d = 3000.0f * tr.fraction;
    blood(camera_.eye + fwd * std::max(60.0f, d - 90.0f), fwd, true);
    return true;
}

std::string GameWorld::effects_summary() const {
    size_t holes = 0, bloods = 0;
    for (const Mark& m : marks_) {
        for (const TexInfo* t : fx_.bullet_mark) holes += m.tex == t;
        for (const TexInfo* t : fx_.blood_mark) bloods += m.tex == t;
    }
    std::string steps;
    for (size_t m = 0; m < step_fx_.size(); ++m)
        if (step_fx_[m]) steps += eng::str::format("%s%s %d", steps.empty() ? "" : ", ", sf::SoundTables::material_name(u8(m)), step_fx_[m]);
    return eng::str::format("%zu cases, %zu bullet marks, %zu blood marks, %zu puffs, %zu blasts, %zu grenades in the air, %zu smokes; footsteps seen: %s",
                            cases_.size(), holes, bloods, puffs_.size(), blasts_.size(), grenades_.size(), smokes_.size(),
                            steps.empty() ? "none" : steps.c_str());
}

}  // namespace lsf
