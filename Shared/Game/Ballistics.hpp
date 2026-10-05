// What a bullet meets, the same for the game (it traces its own shots) and the server (it checks
// them, and traces the ones a client could not): a soldier's three hit shapes, how much wall a
// weapon's bullet goes through, how thick the wall between two points is, and what the wall takes
// off the damage.
//
// Wall shots are Soldier Front's (a door, a crate, a thin wall never stopped a rifle), but nothing
// in the client's tables says how deep: weapon.kst has no column for it and soldierfront.exe is
// packed. The depths here are ours, by weapon class.
#pragma once

#include "Engine/Physics/CollisionMesh.hpp"
#include "Game/Movement.hpp"
#include "Game/Protocol.hpp"
#include "Game/Rules.hpp"

namespace lsf {

// Where a ray meets a soldier standing at `feet` (as tall as his proto::PlayerFlags make him:
// standing, crouched, tucked in the air): the head (a box at the top), the chest, the legs. `t` is
// the distance along `dir` (a unit vector), no further than `max_dist`.
bool ray_soldier(const eng::Vec3& from, const eng::Vec3& dir, const eng::Vec3& feet, u16 flags, const MovementDef& def, float max_dist, float& t, proto::HitZone& zone);

// The wall (cm of solid along the bullet's way) a weapon's bullet passes: a sniper rifle 60, a
// machine gun 48, a rifle 40, a sub-machine gun 24, a pistol 20; a shotgun's pellets, a blade and
// a grenade none.
float penetration_cm(const WeaponDef& w);

// What is left of the damage after `thickness` cm of a wall the bullet could pass up to `limit` of:
// all of it through a sheet, two fifths at the limit.
float wall_damage_scale(float thickness, float limit);

// The solid between two points, in cm: 0 when the way is clear, -1 when it is more than `limit`
// (the bullet stops). A wall is measured from where the ray goes in to where it comes out (the
// far face, met from behind); a face with nothing behind it is a sheet (4 cm).
float wall_thickness(const eng::CollisionMesh& map, const eng::Vec3& from, const eng::Vec3& to, float limit);

}  // namespace lsf
