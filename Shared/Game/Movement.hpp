// Player movement, shared by the client (it moves its own soldier) and the server (it checks
// the positions clients send). Deterministic for a given state, command and collision mesh.
// After TacticalFPS's, which is after Quake's: slide along up to five planes a tick, try every
// move flat and a step up and keep the better, snap down stairs.
//
// The origin is the centre of the feet; the hull is an axis-aligned box. Tucking the feet up
// when crouching in the air is what lets a crouch-jump land on a ledge a plain jump cannot
// reach: Soldier Front's K-jump falls out of the same rule.
//
// Ladders are TacticalFPS's (after Quake's): a soldier touching a near-vertical face the level
// tags Surface::Ladder (a prop named or skinned "ladder": sfcheck ladders) holds on, gravity is
// off, and looking up or down it while going forward climbs that way.
#pragma once

#include "Engine/Physics/CollisionMesh.hpp"
#include "Game/Protocol.hpp"

namespace lsf {

struct MovementDef {
    float hull_half_width = 18;
    float stand_height = 178;     // a Soldier Front soldier is 357 half-centimetres
    // The original's crouch (l_ds) carries the head 37 cm lower than its stand (l_us): Bip01 Head
    // 118.6 cm over the feet against 155.5 (sfcheck heads l_). At 118 and an eye of 104 the view sat
    // 23 cm under the soldier's own head, and the hit boxes (Ballistics.cpp, from these heights) put
    // a crouched head where the drawn one has its chest (players, 2026-10-04: "crouching too low").
    float crouch_height = 141;
    // Off his feet the legs come up further than a crouch on the ground bends them: crouching in the
    // air takes the hull down to this from the top of the head, the feet tucked up under him, so a
    // crouch-jump clears 60 cm more than a jump (the K-jump).
    float tuck_height = 118;
    float stand_eye = 164;
    float crouch_eye = 127;       // 14 cm under the top of the hull, as standing
    // The tallest ledge walked onto without a jump. Soldier Front's maps are full of steps and kerbs
    // of 39 to 45 cm (sfcheck stairs all: 146 runs stopped at 38-39 cm, 81 at 44-45, with 38 here),
    // so a flight had to be hopped up a step at a time. 45 cm is a quarter of a soldier's height,
    // Counter-Strike's own proportion; the 50 cm crates still want a jump.
    float step_height = 45;
    // The run. The original's own clips were authored slower (the planted foot's speed: l_urf_01
    // runs 385 cm/s, l_urb_01 back 285, l_uwf_01 walks 115, l_dwf_01 crouch-walks 80), and at those
    // paces soldiers felt slow; at TacticalFPS's 500, and at 590, they still did (players,
    // 2026-10-02). 635 still did, a little (2026-10-04: "bump" it, to 700), and then 700 did too
    // (the same day: "increase the run speed by 20%"), but 840 was "a little too fast", and 770
    // (a tenth over 700) was slowed 8 % (2026-10-05): 708, the M4A1 at 673 cm/s, a pistol at 743
    // and a blade or a grenade in hand at 814 (Rules.hpp kLightCarry). The legs play at the
    // soldier's speed over them (GameWorld.cpp kRunPace), so the feet still hold the floor.
    float run_speed = 708;        // cm/s at WEAPON_SPEED 1 (the M4A1's 0.95: 673)
    float back_multiplier = 0.82f;
    // Walking (the M4A1's 403) and crouching (329): at 0.45 and 0.38 both felt like wading.
    float walk_multiplier = 0.55f;
    float crouch_multiplier = 0.45f;
    // Ground friction stops a soldier from a run in about a third of a second; below stop_speed it
    // bites as though he were going that fast. The accel must out-pull that at the slowest pace (a
    // crouch-walk, ~180 cm/s: 14 x 180 > 5.2 x 200), or crouching would not move at all.
    float ground_accel = 14;
    // In the air the keys steer: the run turns toward where they point, this many cm/s a second
    // (Movement.cpp). Counter-Strike's air (here until 2026-10-04: an accel of 2, then 10, with a
    // 60 cm/s cap) adds speed only along a key's way and only up to the cap, so W and A held through
    // a running jump did nothing and the jump went on toward the crosshair (players: "if you press
    // space and are pressing A or D, your character will move with you; on this client it moves with
    // the crosshair"). Turning the view with a key held bends the jump, as it did. At 1600 (sfcheck
    // moves; SFCHECK_AIR_CONTROL to try others): W and D held through a running jump land it 2.6 m
    // right of its line, D from a standstill carries 3.4 m, the view turned half round a second with
    // W held bends a run's jump 84 degrees.
    float air_control = 1600;
    float friction = 5.2f;
    float stop_speed = 200;
    float gravity = 2000;
    // A jump's height is jump_speed^2 / (2 gravity): 688 lifts the feet 118 cm, 12 % higher than
    // 650's 106 (players, 2026-10-04: "increase the jump height by 12%"; 600 lifted 90). The crouch
    // in the air adds the 60 cm the hull shrinks, for a ledge of about 1.8 m (the K-jump). A jump
    // from a crouch springs the crouch's 37 cm higher (its legs then tuck only 23), so crouch and
    // jump pressed together reach the same ledge whichever key went first.
    float jump_speed = 688;
    float max_fall_speed = 4000;
    float duck_time = 0.15f;
    // The steepest a soldier stands on: 47 degrees. The maps' ramps run up to 45 (Kill House's
    // planks 42.5), some built of two triangles a little either side of it (Bunker Buster's 43.8
    // and 46.4); past 47 they are hull sides, rocks, planters and sandbags (sfcheck ramps all).
    float max_slope_normal_y = 0.68f;
    // Ladders. `ladder_reach` is how far past the hull a climbable face is still held; it has to
    // clear CollisionMesh::kSkin, the gap a hull comes to rest at. `ladder_mount_drop` lets a
    // soldier standing on the lip of a shaft take the ladder below his feet.
    float ladder_speed = 300;
    float ladder_reach = 4;
    // And off his feet: a soldier walking over the top of a ladder whose top is the floor's edge
    // leaves the lip a tick's move out from the rungs (5 cm at a walk, 10 at a run), past
    // `ladder_reach`, and fell the whole flight (Plasma's, at 332 cm/s). In the air the reach is this.
    float ladder_air_reach = 14;
    // Soldier Front's ladders often end below the edge they serve (Bridge's 33 cm under the deck):
    // reached for this far below the feet, so stepping off the edge over one takes hold of it.
    float ladder_mount_drop = 48;
    float ladder_push_off = 220;  // speed away from the ladder when jumping off it
    float ladder_release = 0.35f; // and how long after that jump it is not taken hold of again
};

struct MoveCmd {
    float yaw = 0, pitch = 0;     // degrees; pitch up is positive (eng::angles_to_forward)
    float forward = 0, side = 0;  // -1..1
    u16 buttons = 0;              // proto::Buttons
};

struct MoveState {
    eng::Vec3 origin;
    eng::Vec3 velocity;
    bool on_ground = false;
    bool ducked = false;
    bool jump_held = false;
    bool on_ladder = false;       // climbing: gravity is off and `ladder_normal` faces out of the rungs
    bool tucked = false;          // crouched off the ground: the hull is tuck_height, the feet up under him
    float duck_amount = 0;        // 0 standing .. 1 crouched: drives the eye height
    float tuck_amount = 0;        // 0 .. 1 tucked: the eye's part of the tuck, eased back down on landing
    float ladder_release = 0;     // seconds left in which a ladder jumped off is not taken hold of again
    eng::Vec3 ladder_normal;      // only meaningful while on_ladder
};

struct MoveEvents {
    bool jumped = false;
    bool landed = false;
    float landing_speed = 0;
    bool grabbed_ladder = false;  // took hold of a ladder this tick
    bool climbed = false;         // moved along a ladder this tick
};

// Another soldier, known by his flags (proto::PlayerFlags, on the server or in a snapshot): crouched
// off his feet is tucked. His hull's height and his eye's, so.
inline bool tucked_by_flags(u16 flags) { return (flags & proto::kFlagCrouched) && !(flags & proto::kFlagOnGround); }
float hull_height_by_flags(const MovementDef& def, u16 flags);
float eye_height_by_flags(const MovementDef& def, u16 flags);
float hull_height(const MovementDef& def, bool ducked, bool tucked = false);
eng::Vec3 hull_half_extents(const MovementDef& def, bool ducked, bool tucked = false);
eng::Vec3 hull_center(const MovementDef& def, const eng::Vec3& origin, bool ducked, bool tucked = false);
float eye_height(const MovementDef& def, float duck_amount, float tuck_amount = 0);
eng::Vec3 eye_position(const MovementDef& def, const MoveState& state);

// One tick. `speed_scale` carries the weapon's and the force's speed.
MoveEvents simulate_move(MoveState& state, const MoveCmd& cmd, float dt, const MovementDef& def, const eng::CollisionMesh& world,
                         float speed_scale);

// A stair stepped up or down between two ticks on the ground: the rise, when it is more than the
// steepest walkable slope gives over the ground covered (a ramp's rise is the feet's own, and a
// fixed 8 cm took a 45 degree ramp at 603 cm/s for a flight); else 0. The eye takes it over a
// moment instead of at once, trailing the feet by at most kStairEyeTrail steps (GameWorld.cpp):
// still inside the hull, so never in the stairs. `sfcheck stairs all`, an M4A1 at a run: the eye's
// worst move between frames at 144 a second 7.2 cm (the feet's 20); SFCHECK_SPEED=1.15, a blade:
// 12.5 (23, Bunker Buster's flight of 42 cm steps).
float stair_rise(const MovementDef& def, const eng::Vec3& from, const eng::Vec3& to);
inline constexpr float kStairEyeTrail = 2.5f;
inline constexpr float kStairEyeCatchUp = 11.0f;   // the eye's lag shrinks by e^-this a second

// Drops a spawn point onto the floor below it and nudges it out of walls.
eng::Vec3 settle_spawn(const MovementDef& def, const eng::CollisionMesh& world, const eng::Vec3& point);

// A fall (Server/Match.cpp track_fall): feet landing more than kFallSafe below the highest they were
// since leaving the ground hurt kFallDamage a centimetre past it (a 6 m drop 40, 8.8 m a soldier's
// whole 100; a jump's own top is 118 cm, so a jump off a 3 m ledge is still safe). The parts worn take
// their share off (Game/Items.hpp fall_damage, -50: half). The original's: gametext 1514-1515.
inline constexpr float kFallSafe = 420.0f;
inline constexpr float kFallDamage = 0.22f;
int fall_damage(float drop, int parts_fall_pct);

}  // namespace lsf
