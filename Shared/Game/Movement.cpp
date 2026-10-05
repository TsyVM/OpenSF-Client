#include "Game/Movement.hpp"

#include "SF/Level.hpp"   // Surface, to know a climbable triangle

#include <algorithm>
#include <cmath>

namespace lsf {

using namespace eng;

namespace {

constexpr int kMaxBumps = 4;
constexpr int kMaxPlanes = 5;
constexpr float kGroundProbe = 2.0f;
constexpr float kJumpGroundGrace = 180.0f;   // upward speed above which we are not on the ground, with nothing under the feet to say
constexpr float kLeaveGround = 30.0f;        // speed away from the ground's own face above which it has been left

Vec3 clip_velocity(const Vec3& v, const Vec3& normal, float overbounce) {
    const float backoff = dot(v, normal) * overbounce;
    Vec3 out = v - normal * backoff;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(out[i]) < 0.01f) out[i] = 0.0f;
    return out;
}

struct Mover {
    const MovementDef& def;
    const CollisionMesh& world;
    bool ducked;
    bool tucked;

    Vec3 half() const { return hull_half_extents(def, ducked, tucked); }
    Vec3 center(const Vec3& origin) const { return hull_center(def, origin, ducked, tucked); }
    Vec3 origin_from_center(const Vec3& c) const { return c - Vec3{0, hull_height(def, ducked, tucked) * 0.5f, 0}; }

    TraceResult trace(const Vec3& from_origin, const Vec3& to_origin) const {
        TraceResult t = world.trace_box(center(from_origin), center(to_origin), half());
        t.end = origin_from_center(t.end);
        return t;
    }

    // A move clipped against a face. One too steep to stand on is a wall to a soldier's legs: it
    // never lifts him. Clipped as a slope, a run into it turned into a climb, a hop at a time --
    // up a rock, a hull, a planter's side -- a slow stutter up something he could not stand on.
    // Sliding down it, and a jump's own rise, are his still.
    Vec3 clip_against(const Vec3& v, const Vec3& n) const {
        Vec3 out = clip_velocity(v, n, 1.0f);
        if (n.y > 0.0f && n.y < def.max_slope_normal_y && out.y > std::max(v.y, 0.0f)) {
            const Vec3 flat{n.x, 0.0f, n.z};
            if (length_sq(flat) > 1e-6f) out = clip_velocity(v, normalize(flat), 1.0f);
        }
        return out;
    }

    void slide(Vec3& origin, Vec3& velocity, float dt) const {
        Vec3 planes[kMaxPlanes];
        int num_planes = 0;
        const Vec3 primal = velocity;
        Vec3 original = velocity;
        float time_left = dt;
        for (int bump = 0; bump < kMaxBumps; ++bump) {
            if (length_sq(velocity) < 1e-6f) break;
            const Vec3 end = origin + velocity * time_left;
            const TraceResult tr = trace(origin, end);
            if (tr.start_solid) {
                velocity = Vec3{0, velocity.y < 0 ? 0.0f : velocity.y, 0};
                return;
            }
            if (tr.fraction > 0.0f) {
                origin = tr.end;
                original = velocity;
                num_planes = 0;
            }
            if (tr.fraction >= 1.0f) break;
            time_left -= time_left * tr.fraction;
            if (num_planes >= kMaxPlanes) {
                velocity = {};
                break;
            }
            planes[num_planes++] = tr.normal;
            int i;
            for (i = 0; i < num_planes; ++i) {
                velocity = clip_against(original, planes[i]);
                int j;
                for (j = 0; j < num_planes; ++j)
                    if (j != i && dot(velocity, planes[j]) < 0.0f) break;
                if (j == num_planes) break;
            }
            if (i == num_planes) {
                if (num_planes != 2) {
                    velocity = {};
                    break;
                }
                const Vec3 dir = normalize(cross(planes[0], planes[1]));
                velocity = dir * dot(dir, velocity);
            }
            if (dot(velocity, primal) <= 0.0f) {
                velocity = {};
                break;
            }
        }
    }

    void step_slide(Vec3& origin, Vec3& velocity, float dt) const {
        const Vec3 start_o = origin, start_v = velocity;
        Vec3 down_o = start_o, down_v = start_v;
        slide(down_o, down_v, dt);
        Vec3 up_o = start_o, up_v = start_v;
        const TraceResult up = trace(up_o, up_o + Vec3{0, def.step_height, 0});
        if (up.start_solid) {
            origin = down_o;
            velocity = down_v;
            return;
        }
        const float raised = up.end.y - up_o.y;
        up_o = up.end;
        slide(up_o, up_v, dt);
        const TraceResult down = trace(up_o, up_o - Vec3{0, raised + kGroundProbe, 0});
        if (!down.start_solid) up_o = down.end;
        const bool landed_on_walkable = down.fraction < 1.0f && down.normal.y >= def.max_slope_normal_y;
        const float down_dist = (down_o.x - start_o.x) * (down_o.x - start_o.x) + (down_o.z - start_o.z) * (down_o.z - start_o.z);
        const float up_dist = (up_o.x - start_o.x) * (up_o.x - start_o.x) + (up_o.z - start_o.z) * (up_o.z - start_o.z);
        if (!landed_on_walkable || up_dist <= down_dist + 0.01f) {
            origin = down_o;
            velocity = down_v;
        } else {
            origin = up_o;
            velocity = up_v;
            velocity.y = down_v.y;
        }
    }
};

void apply_friction(Vec3& v, const MovementDef& def, float dt) {
    const float speed = std::sqrt(v.x * v.x + v.z * v.z);
    if (speed < 0.1f) {
        v.x = v.z = 0.0f;
        return;
    }
    const float control = std::max(speed, def.stop_speed);
    const float drop = control * def.friction * dt;
    const float scale = std::max(speed - drop, 0.0f) / speed;
    v.x *= scale;
    v.z *= scale;
}

void accelerate(Vec3& v, const Vec3& wish_dir, float wish_speed, float accel, float dt, float cap) {
    const float capped = std::min(wish_speed, cap);
    const float current = dot(v, wish_dir);
    const float add = capped - current;
    if (add <= 0.0f) return;
    v += wish_dir * std::min(accel * dt * wish_speed, add);
}

bool fits(const MovementDef& def, const CollisionMesh& world, const Vec3& origin, bool ducked, bool tucked = false) {
    return !world.box_overlaps(hull_center(def, origin, ducked, tucked), hull_half_extents(def, ducked, tucked));
}

// A climbable face against the soldier's hull. The probe is the hull widened by `ladder_reach` (a
// hull at rest sits CollisionMesh::kSkin from what it touches, so a probe the size of the hull
// would never reach the rungs; off his feet, by `ladder_air_reach`) and dropped by
// `ladder_mount_drop`, so someone on the lip of a shaft takes hold of the ladder under his feet.
bool find_ladder(const MovementDef& def, const CollisionMesh& world, const Vec3& origin, bool ducked, bool tucked, bool airborne, Vec3& normal_out) {
    const float reach = std::max(airborne ? std::max(def.ladder_air_reach, def.ladder_reach) : def.ladder_reach, CollisionMesh::kSkin * 2.0f);
    const float drop = std::max(def.ladder_mount_drop, 0.0f);
    const Vec3 half = hull_half_extents(def, ducked, tucked) + Vec3{reach, drop * 0.5f, reach};
    const Vec3 center = hull_center(def, origin, ducked, tucked) - Vec3{0, drop * 0.5f, 0};
    return world.box_overlaps_surface(center, half, u8(sf::Surface::Ladder), normal_out);
}

// Climbing, after Quake's ladder move: the wished velocity splits into the part pressing into the
// rungs and the part sliding across them, and the part pressing in turns through a right angle into
// going up or down the face. Looking at a ladder and going forward climbs it; looking down it and
// going forward climbs down.
void ladder_move(MoveState& s, const Vec3& wish, const Vec3& normal, const MovementDef& def) {
    Vec3 flat = normal;
    flat.y = 0.0f;
    const float flat_len = length(flat);
    if (flat_len < 1e-4f) {
        s.velocity = {};
        return;
    }
    flat = flat / flat_len;
    // `across` runs along the face, `up_face` up it (world up on an upright ladder, the lean on a
    // leaning one).
    const Vec3 across = normalize(cross(Vec3{0, 1, 0}, flat));
    const Vec3 up_face = cross(flat, across);
    const float into = dot(wish, flat);          // negative when pressing into the rungs
    const Vec3 sideways = wish - flat * into;    // the rest slides across the face
    Vec3 along = sideways + up_face * -into;
    // Looking up the ladder and going forward counts twice (pressing in and sliding up), so cap it.
    const float speed = length(along);
    if (speed > def.ladder_speed) along = along * (def.ladder_speed / speed);
    // Held against the rungs, so a ladder in open air is not drifted off sideways.
    s.velocity = along - flat * 20.0f;
}

}  // namespace

float hull_height(const MovementDef& def, bool ducked, bool tucked) { return !ducked ? def.stand_height : tucked ? def.tuck_height : def.crouch_height; }
Vec3 hull_half_extents(const MovementDef& def, bool ducked, bool tucked) {
    return {def.hull_half_width, hull_height(def, ducked, tucked) * 0.5f, def.hull_half_width};
}
Vec3 hull_center(const MovementDef& def, const Vec3& origin, bool ducked, bool tucked) { return origin + Vec3{0, hull_height(def, ducked, tucked) * 0.5f, 0}; }
// Tucked, the hull is shorter than a crouch by the legs drawn up: the eye is that much nearer the feet.
float eye_height(const MovementDef& def, float duck_amount, float tuck_amount) {
    return lerpf(def.stand_eye, def.crouch_eye, saturate(duck_amount)) - saturate(tuck_amount) * (def.crouch_height - def.tuck_height);
}
Vec3 eye_position(const MovementDef& def, const MoveState& state) { return state.origin + Vec3{0, eye_height(def, state.duck_amount, state.tuck_amount), 0}; }
float hull_height_by_flags(const MovementDef& def, u16 flags) { return hull_height(def, flags & proto::kFlagCrouched, tucked_by_flags(flags)); }
float eye_height_by_flags(const MovementDef& def, u16 flags) {
    const bool crouched = flags & proto::kFlagCrouched;
    return eye_height(def, crouched ? 1.0f : 0.0f, tucked_by_flags(flags) ? 1.0f : 0.0f);
}

MoveEvents simulate_move(MoveState& s, const MoveCmd& cmd, float dt, const MovementDef& def, const CollisionMesh& world, float speed_scale) {
    MoveEvents events;
    // How far the feet come up: from standing to tucked, and from a crouch to tucked.
    const float stand_tuck = def.stand_height - def.tuck_height;
    const float crouch_tuck = def.crouch_height - def.tuck_height;

    // Unstick: if we spawned badly or something moved into us, push up out of it.
    if (!fits(def, world, s.origin, s.ducked, s.tucked)) {
        for (float lift : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 24.0f, 32.0f}) {
            const Vec3 o = s.origin + Vec3{0, lift, 0};
            if (fits(def, world, o, s.ducked, s.tucked)) {
                s.origin = o;
                break;
            }
        }
    }

    // Crouch. On the ground the hull shrinks from the top; in the air the feet tuck up, the head
    // where it was. The eye's share of a tuck moves with the feet in the same tick, so the view holds
    // still through it.
    const bool want_duck = cmd.buttons & proto::kButtonCrouch;
    if (want_duck && !s.ducked) {
        if (s.on_ground) {
            s.ducked = true;
        } else {
            const Vec3 up = s.origin + Vec3{0, stand_tuck, 0};
            if (fits(def, world, up, true, true)) s.origin = up;
            s.ducked = s.tucked = true;
            s.duck_amount = s.tuck_amount = 1.0f;
        }
    } else if (!want_duck && s.ducked) {
        if (s.on_ground) {
            if (fits(def, world, s.origin, false)) s.ducked = s.tucked = false;
        } else {
            const Vec3 lowered = s.origin - Vec3{0, s.tucked ? stand_tuck : def.stand_height - def.crouch_height, 0};
            if (fits(def, world, lowered, false)) {
                s.origin = lowered;
                s.ducked = s.tucked = false;
                s.duck_amount = s.tuck_amount = 0.0f;
            } else if (fits(def, world, s.origin, false)) {
                s.ducked = s.tucked = false;
            }
        }
    }
    // Crouched and off the ground some other way (a jump from a crouch, a crouch walked off a ledge):
    // the legs come up as they would for a crouch in the air.
    if (s.ducked && !s.tucked && !s.on_ground && !s.on_ladder) {
        const Vec3 up = s.origin + Vec3{0, crouch_tuck, 0};
        if (fits(def, world, up, true, true)) {
            s.origin = up;
            s.tucked = true;
            s.tuck_amount = 1.0f;
        }
    }
    // Tucked on the ground (he came down crouched): the legs go down into a crouch where there is the
    // room, the view rising with them over the crouch's own moment.
    if (s.tucked && s.on_ground && fits(def, world, s.origin, true, false)) s.tucked = false;
    const float duck_rate = def.duck_time > 0 ? dt / def.duck_time : 1.0f;
    const auto ease = [duck_rate](float now, float target) { return now < target ? std::min(target, now + duck_rate) : std::max(target, now - duck_rate); };
    s.duck_amount = ease(s.duck_amount, s.ducked ? 1.0f : 0.0f);
    s.tuck_amount = ease(s.tuck_amount, s.tucked ? 1.0f : 0.0f);

    // Ladders. Whether one is held is settled before the move, so a jump can let go of it and the
    // branch below knows which rules to run. Losing the ladder while still going up it means the
    // soldier climbed off the top: he is carried over the lip rather than shot up and dropped back.
    // A ladder jumped off is let go of for a moment, or the push off it would be caught at once.
    // Only the jump: walking off the top toward the drop, to climb down, moves away from the rungs
    // just the same, and has to take hold.
    const bool was_on_ladder = s.on_ladder;
    s.ladder_release = std::max(0.0f, s.ladder_release - dt);
    Vec3 ladder_normal;
    const bool touching_ladder = s.ladder_release <= 0.0f && find_ladder(def, world, s.origin, s.ducked, s.tucked, !s.on_ground, ladder_normal);
    s.on_ladder = touching_ladder;
    if (s.on_ladder) {
        s.ladder_normal = ladder_normal;
        events.grabbed_ladder = !was_on_ladder;
    } else if (was_on_ladder && s.velocity.y > 0.0f) {
        // The floor at the top is behind the rungs: the push is into the face.
        Vec3 out = s.ladder_normal;
        out.y = 0.0f;
        if (length_sq(out) > 1e-6f) s.velocity -= normalize(out) * def.ladder_speed;
    }

    // Wish direction from the view's yaw only; looking up never slows running.
    const float yaw = cmd.yaw * kDegToRad;
    const Vec3 forward{std::sin(yaw), 0, std::cos(yaw)};
    const Vec3 right{std::cos(yaw), 0, -std::sin(yaw)};
    const float fmove = std::clamp(cmd.forward, -1.0f, 1.0f), smove = std::clamp(cmd.side, -1.0f, 1.0f);
    const Vec3 wish = forward * fmove + right * smove;
    const float wish_len = length(wish);
    const Vec3 wish_dir = wish_len > 1e-4f ? wish / wish_len : Vec3{};
    float max_speed = def.run_speed * speed_scale;
    // Backing away is slower, the more so the straighter back (the run-back clip's own pace).
    if (cmd.forward < 0 && wish_len > 1e-4f) max_speed *= lerpf(1.0f, def.back_multiplier, std::min(1.0f, -cmd.forward / wish_len));
    if (cmd.buttons & proto::kButtonWalk) max_speed *= def.walk_multiplier;
    if (s.ducked && s.on_ground) max_speed *= def.crouch_multiplier;
    const float wish_speed = std::min(wish_len, 1.0f) * max_speed;

    // Jump on press, not hold. On a ladder it lets go and pushes off the rungs.
    if (cmd.buttons & proto::kButtonJump) {
        if (!s.jump_held && s.on_ladder) {
            Vec3 out = s.ladder_normal;
            out.y = 0.0f;
            if (length_sq(out) > 1e-6f) out = normalize(out);
            s.velocity = out * def.ladder_push_off + Vec3{0, def.jump_speed * 0.5f, 0};
            s.on_ladder = false;
            s.on_ground = false;
            s.ladder_release = def.ladder_release;
            events.jumped = true;
        } else if (!s.jump_held && s.on_ground) {
            // From a crouch, the 37 cm the crouch is down besides (its tuck in the air is 23 cm, a
            // stand's 60): the K-jump's ledge whether the crouch or the jump was pressed first.
            const float from_crouch = s.ducked ? def.stand_height - def.crouch_height : 0.0f;
            s.velocity.y = std::sqrt(def.jump_speed * def.jump_speed + 2.0f * def.gravity * from_crouch);
            s.on_ground = false;
            events.jumped = true;
        }
        s.jump_held = true;
    } else {
        s.jump_held = false;
    }

    const Mover mover{def, world, s.ducked, s.tucked};
    const bool was_on_ground = s.on_ground;
    const float fall_speed = -s.velocity.y;
    if (s.on_ladder) {
        // The climb reads the whole look, not only its yaw: looking up or down the ladder is how a
        // soldier says which way to go (pitch up is positive here, as the view's own).
        const Vec3 look = angles_to_forward(cmd.yaw, cmd.pitch);
        Vec3 climb_wish = look * fmove + right * smove;
        const float climb_len = length(climb_wish);
        const float climb_speed = def.ladder_speed * (cmd.buttons & proto::kButtonWalk ? def.walk_multiplier : 1.0f);
        climb_wish = climb_len > 1e-4f ? climb_wish / climb_len * std::min(climb_len, 1.0f) * climb_speed : Vec3{};
        // At the foot of a ladder, moving away from it is walking off, not climbing.
        Vec3 flat_normal = s.ladder_normal;
        flat_normal.y = 0.0f;
        const bool stepping_off = s.on_ground && length_sq(flat_normal) > 1e-6f && dot(climb_wish, normalize(flat_normal)) > 1.0f;
        if (stepping_off) {
            s.on_ladder = false;
            s.velocity.y = 0.0f;
            apply_friction(s.velocity, def, dt);
            accelerate(s.velocity, wish_dir, wish_speed, def.ground_accel, dt, 1e9f);
            mover.step_slide(s.origin, s.velocity, dt);
        } else {
            ladder_move(s, climb_wish, s.ladder_normal, def);
            const float start_y = s.origin.y;
            mover.slide(s.origin, s.velocity, dt);
            events.climbed = std::fabs(s.origin.y - start_y) > 0.01f;
        }
    } else if (s.on_ground) {
        s.velocity.y = 0.0f;
        apply_friction(s.velocity, def, dt);
        accelerate(s.velocity, wish_dir, wish_speed, def.ground_accel, dt, 1e9f);
        const float speed = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
        const float cap = std::max(max_speed, 1.0f);
        if (speed > cap) {
            s.velocity.x *= cap / speed;
            s.velocity.z *= cap / speed;
        }
        mover.step_slide(s.origin, s.velocity, dt);
    } else {
        // The keys steer him in the air: his run turns toward where they point (W and D held carry a
        // run's jump off to the right) as fast as air_control allows, and turning the view with them
        // held bends the jump. The turn keeps his speed; he only gathers speed up to his run (a jump
        // from a standstill), and only loses it pushing against the way he flies (S in a jump ahead).
        // No key, and he flies on as he left the ground.
        if (wish_len > 1e-4f) {
            const float most = def.air_control * dt;
            const float sp = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z);
            float x = wish_dir.x, z = wish_dir.z, speed = std::min(most, wish_speed);
            if (sp > 1.0f) {
                const float dx = s.velocity.x / sp, dz = s.velocity.z / sp;
                const float cos_a = std::clamp(dx * wish_dir.x + dz * wish_dir.z, -1.0f, 1.0f);
                const float angle = std::acos(cos_a);
                const float step = std::min(angle, most / sp);
                // Turned toward the keys by `step`, the shorter way round.
                const float side = dz * wish_dir.x - dx * wish_dir.z >= 0.0f ? 1.0f : -1.0f;
                const float c = std::cos(step), sn = std::sin(step) * side;
                x = dx * c + dz * sn, z = -dx * sn + dz * c;
                speed = cos_a < 0.0f ? std::max(0.0f, sp + most * cos_a) : sp < wish_speed ? std::min(wish_speed, sp + most) : sp;
            }
            s.velocity.x = x * speed;
            s.velocity.z = z * speed;
        }
        s.velocity.y -= def.gravity * dt * 0.5f;
        mover.slide(s.origin, s.velocity, dt);
        s.velocity.y -= def.gravity * dt * 0.5f;
        s.velocity.y = std::max(s.velocity.y, -def.max_fall_speed);
    }

    // Ground check; walking off a stair edge snaps down a step instead of falling. Going up a ladder
    // (even slowly, walking) is never stood on the floor it left.
    const float probe = (was_on_ground && !events.jumped && !s.on_ladder) ? def.step_height + kGroundProbe : kGroundProbe;
    // A soldier on his feet leaves the ground by jumping, by a ladder, or by running out of
    // ground: never by how fast the ground he walks is rising. A run up a plank rises fast (237
    // cm/s up a lean of 42.5 degrees), and that rise used to be read as a jump: on every ramp
    // steeper than some 25 degrees, and again at the top of each where it meets the level, he was
    // thrown into the air a tick at a time -- a stutter of little hops at half the pace. So while
    // he was on the ground and did not jump, the floor within a step below him is his floor.
    // In the air it is the ground's own face that says whether he has come down on it: moving
    // into it, not away.
    s.on_ground = false;
    if (!events.jumped && !(s.on_ladder && s.velocity.y > 1.0f)) {
        const TraceResult down = mover.trace(s.origin, s.origin - Vec3{0, probe, 0});
        const bool leaving = !was_on_ground && (down.fraction < 1.0f ? dot(s.velocity, down.normal) > kLeaveGround : s.velocity.y > kJumpGroundGrace);
        if (!leaving && !down.start_solid && down.fraction < 1.0f && down.normal.y >= def.max_slope_normal_y) {
            s.origin = down.end;
            s.on_ground = true;
            // Climbing down to the floor is an arrival, not a fall: no landing thump, no damage.
            if (!was_on_ground && !s.on_ladder && !was_on_ladder) {
                events.landed = true;
                events.landing_speed = std::max(fall_speed, -s.velocity.y);
            }
            if (s.velocity.y < 0) s.velocity.y = 0;
        }
    }
    return events;
}

float stair_rise(const MovementDef& def, const Vec3& from, const Vec3& to) {
    const float rise = to.y - from.y;
    const float ground = std::sqrt((to.x - from.x) * (to.x - from.x) + (to.z - from.z) * (to.z - from.z));
    const float ny = std::clamp(def.max_slope_normal_y, 0.1f, 1.0f);
    const float slope_rise = ground * std::sqrt(1.0f - ny * ny) / ny;   // tan of the steepest walkable slope
    if (std::fabs(rise) <= std::max(4.0f, slope_rise + 2.0f) || std::fabs(rise) > def.step_height + 4.0f) return 0.0f;
    return rise;
}

Vec3 settle_spawn(const MovementDef& def, const CollisionMesh& world, const Vec3& point) {
    Vec3 start = point + Vec3{0, 40, 0};
    for (int attempt = 0; attempt < 8 && !fits(def, world, start, false); ++attempt) start.y += 20;
    const TraceResult down = world.trace_box(hull_center(def, start, false), hull_center(def, start - Vec3{0, 600, 0}, false),
                                             hull_half_extents(def, false));
    if (down.start_solid) return point;
    return down.end - Vec3{0, def.stand_height * 0.5f, 0};
}

int fall_damage(float drop, int parts_fall_pct) {
    if (!(drop > kFallSafe)) return 0;
    const float scale = std::clamp(1.0f + float(parts_fall_pct) / 100.0f, 0.0f, 1.0f);
    // No map is 100 m tall: a drop past that is a report from nowhere, and a kill at most.
    return int(std::lround((std::min(drop, 10000.0f) - kFallSafe) * kFallDamage * scale));
}

}  // namespace lsf
