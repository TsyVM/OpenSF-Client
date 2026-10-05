#include "Game/Ballistics.hpp"

#include <algorithm>
#include <cmath>

namespace lsf {

using eng::Vec3;

bool ray_soldier(const Vec3& from, const Vec3& dir, const Vec3& feet, u16 flags, const MovementDef& def, float max_dist, float& t, proto::HitZone& zone) {
    using proto::HitZone;
    const float h = hull_height_by_flags(def, flags);
    const float r = 20.0f;
    struct Shape {
        eng::Aabb box;
        HitZone zone;
    };
    const Shape shapes[3] = {
        {{feet + Vec3{-10, h - 26, -10}, feet + Vec3{10, h - 2, 10}}, HitZone::Head},
        {{feet + Vec3{-r, h * 0.48f, -r}, feet + Vec3{r, h - 26, r}}, HitZone::Chest},
        {{feet + Vec3{-r * 0.9f, 0, -r * 0.9f}, feet + Vec3{r * 0.9f, h * 0.48f, r * 0.9f}}, HitZone::Legs},
    };
    const Vec3 inv{dir.x != 0 ? 1.0f / dir.x : 1e30f, dir.y != 0 ? 1.0f / dir.y : 1e30f, dir.z != 0 ? 1.0f / dir.z : 1e30f};
    float best = max_dist;
    bool hit = false;
    for (const Shape& s : shapes) {
        const float at = eng::ray_aabb(from, inv, s.box, best);
        if (at >= 0 && at < best) {
            best = at;
            zone = s.zone;
            hit = true;
        }
    }
    if (hit) t = best;
    return hit;
}

float penetration_cm(const WeaponDef& w) {
    switch (w.klass) {
        case WeaponClass::Sniper: return 60;
        case WeaponClass::MachineGun: return 48;
        case WeaponClass::Rifle: return 40;
        case WeaponClass::Smg: return 24;
        case WeaponClass::Pistol: return 20;
        default: return 0;
    }
}

float wall_damage_scale(float thickness, float limit) {
    if (thickness <= 0 || limit <= 0) return 1.0f;
    return 1.0f - 0.6f * std::clamp(thickness / limit, 0.0f, 1.0f);
}

float wall_thickness(const eng::CollisionMesh& map, const Vec3& from, const Vec3& to, float limit) {
    constexpr float kSheet = 4.0f, kStep = 0.5f;
    const Vec3 way = to - from;
    const float length = eng::length(way);
    if (length < 1.0f) return 0;
    const Vec3 dir = way * (1.0f / length);
    float total = 0;
    Vec3 at = from;
    for (int walls = 0; walls < 8; ++walls) {
        if (eng::length(to - at) < kStep * 2) break;
        const eng::TraceResult in = map.trace_ray(at, to);
        if (!in.hit()) break;
        if (in.back_face) {
            // Out of something the ray began inside (or a face seen from behind): a sheet.
            total += kSheet;
            at = in.end + dir * kStep;
        } else {
            // In through a face: the wall ends where the ray next comes out, a face met from behind.
            const Vec3 inside = in.end + dir * kStep;
            if (eng::length(to - inside) < kStep * 2) {
                total += kSheet;
                break;
            }
            const eng::TraceResult out = map.trace_ray(inside, to);
            if (out.hit() && out.back_face) {
                total += std::max(kSheet, eng::length(out.end - in.end));
                at = out.end + dir * kStep;
            } else if (out.hit()) {
                // Another face from the front before any way out: the first was a sheet.
                total += kSheet;
                at = inside;
            } else {
                // Nothing more before the target: a sheet, with the target right behind it -- or
                // the target stands inside something solid, which a sheet's worth also covers.
                total += kSheet;
                break;
            }
        }
        if (total > limit) return -1;
    }
    return total > limit ? -1 : total;
}

}  // namespace lsf
