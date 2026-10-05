// Soldier Front's models put back together in memory: a first-person weapon from its rig,
// hands, gun and clips (data/weapon/bhw/sf_a_<id>/), and a character from its force's bone
// rig, body pieces and accessories (data/force/SF_C_<FORCE>/), animated by the shared
// character motions (data/force/Motion/).
//
// The frame: Soldier Front authors a model with +X along the barrel (or the body's facing),
// +Y up and +Z across; the rewrite wants +X right, +Y up, +Z forward, so every position and
// rotation goes through engine = (-z, y, x) — a rotation, not a mirror, so winding survives.
// Settled on the AK: its `cartridge` (ejection) node must land on the right and
// SF_hand_left01 left of SF_hand_right01.
//
// Scale: a view model is authored at 0.233 cm a unit (the AK-74 is 405 units and 94.3 cm);
// characters and props at 0.5 cm a unit, like the maps.
#pragma once

#include "SF/Data.hpp"
#include "SF/Lma.hpp"

#include "Engine/Core/Math.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

using eng::Aabb;
using eng::Mat4;
using eng::Quat;
using eng::Vec3;

struct ModelBone {
    std::string name;
    int parent = -1;          // always before the bone itself
    Mat4 bind_local;          // scale * rotation * translation, relative to the parent
    Mat4 inverse_bind;        // model space -> bone space at bind
};

struct ModelVertex {
    float position[3];        // model space at bind
    float normal[3];
    float uv[2];
    u8 bones[4];
    u8 weights[4];            // sum to 255
};
static_assert(sizeof(ModelVertex) == 40);

struct ModelMaterial {
    std::string texture;                 // archive key, empty when not shipped
    std::optional<AssetLocation> where;
    Pack pack = Pack::Weapon;
};

struct ModelMesh {
    std::string name;         // the node it hangs on; weapons prefix "hands:" or "gun:"
    u32 material = 0;
    std::vector<ModelVertex> vertices;
    std::vector<u32> indices;
    Aabb bounds;
};

struct ModelSocket {
    std::string name;         // "flame" (muzzle), "cartridge" (ejection), "Camera01" (view), "grip"
    int bone = -1;
    Mat4 local;
};

// One bone's channel of a clip: 7 floats a key (position xyz, rotation xyzw) in the bone's
// parent space, on the clip's shared key times; a single key when the bone holds still.
struct AnimTrack {
    u32 bone = 0;
    std::vector<float> keys;
};

struct ModelAnimation {
    std::string name;         // "reload", "u_04_shoot00", ...
    std::vector<float> times; // seconds, already divided by the clip's playback rate
    float duration = 0;
    std::vector<AnimTrack> tracks;
};

struct Model {
    std::vector<ModelBone> bones;
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelSocket> sockets;
    std::vector<ModelAnimation> animations;
    // A weapon's first-person eye, in the model's own space: where its .sfc puts the weapon from
    // the camera, turned round. The original client frames every gun by it; `Camera01` in the
    // files is the artists' own camera, and in the oldest guns one left standing at the elbow.
    bool has_view_eye = false;
    Vec3 view_eye{};

    int find_bone(std::string_view name) const;
    const ModelSocket* socket(std::string_view name) const;
    const ModelAnimation* animation(std::string_view name) const;
    Aabb bounds() const;
    size_t triangle_count() const;
};

// Samples `anim` at `t` (clamped, or wrapped when `loop`) into per-bone local transforms,
// starting from each bone's bind. `mask`, when given, says which bones this clip may drive.
void sample_animation(const Model& model, const ModelAnimation& anim, float t, bool loop, std::vector<Mat4>& local,
                      const std::vector<bool>* mask = nullptr);
// Local transforms into model-space bone matrices, and those into skinning matrices.
void pose_to_model(const Model& model, const std::vector<Mat4>& local, std::vector<Mat4>& model_space);
void skin_matrices(const Model& model, const std::vector<Mat4>& model_space, std::vector<Mat4>& out);

// ── Weapons (data/weapon) ──────────────────────────────────────────────────────

struct WeaponSet {
    std::string id;           // "ak74"
    std::string folder;       // "bhw/sf_a_ak74"
    std::string rig, gun, hands, generic_hands, manifest;
    std::string config;       // its .sfc: the weapon's first-person place from the camera, its pieces
    std::string config_id;    // what its .sfc calls itself
    std::string borrowed_from;
    std::map<std::string, std::string> clips;   // action -> archive key
    bool usable() const { return !gun.empty(); }
};

std::vector<WeaponSet> discover_weapons(const Data& data);
// The view scale every weapon shares, measured off the AK-74 (0.233 cm a unit).
float derive_view_scale(const Data& data);
std::optional<Model> load_weapon(const Data& data, const WeaponSet& weapon, float scale, std::string* error = nullptr);
// The class a weapon id reads as: rifle, smg, sniper, machinegun, shotgun, pistol, knife, grenade.
std::string weapon_class_for(std::string_view id);


// ── Characters (data/force) ────────────────────────────────────────────────────

struct ForcePiece {
    std::string key;          // archive key
    std::string name;         // "head", "upperbody", "jacket1", "camoface", ...
};

struct ForceSet {
    std::string id;           // "delta"
    std::string folder;       // "sf_c_delta"
    std::string prefix;       // what its pieces are really called: "sf_c_deltaforce_"
    std::string bone;         // the rig
    std::vector<ForcePiece> pieces;
    std::vector<std::pair<std::string, std::string>> accessories;   // item, key
};

std::vector<ForceSet> discover_forces(const Data& data);
// The outfits a force has pieces for ("" = the plain one).
std::vector<std::string> force_outfits(const Data& data, const ForceSet& force);
std::optional<Model> load_force(const Data& data, const ForceSet& force, std::string_view outfit, std::string* error = nullptr);
// The same with character parts on (Game/Items.hpp's `model` stems, lower case): a piece of the
// force ("sf_c_deltaforce_foot_jungle2") takes the place of what that body slot wore, an
// accessory ("sf_o_gsg9_cap_viperset") goes on over the rest. A stem the archives lack is passed over.
std::optional<Model> load_force(const Data& data, const ForceSet& force, std::string_view outfit, std::span<const std::string> worn,
                                std::string* error = nullptr);

// A shared character clip ("l_urf_01", "u_04_shoot00", "a_ud_03") bound to `model`'s rig by bone
// name, at the characters' scale, at the rate the motion manifests give it.
std::optional<ModelAnimation> load_character_motion(const Data& data, const Model& model, std::string_view clip);
// Every character clip the force archives carry, by lower-case name ("l_urf_01" -> key).
std::map<std::string, std::string> character_motion_keys(const Data& data);

// The undead (Horror's zombies, Horror Mode 2's classes: sf_c_zman, sf_c_zboss, ...): a body and
// whatever rides on it (hair, teeth, eyes, a drill), every piece of the folder at once -- they have
// no outfits, and the force loader's slots would keep one piece of a body that is several. A piece
// its makers marked "[not use]" is left out. `id` is the folder's ("zman").
std::optional<Model> load_undead(const Data& data, std::string_view id, std::string* error = nullptr);
// An undead's own clip, motion/<set>/<set>_<clip>.fxm (or .lma): ("zman", "l_urf01").
std::optional<ModelAnimation> load_undead_motion(const Data& data, const Model& model, std::string_view set, std::string_view clip);

// Upper body (spine and up, arms, head, the weapon) versus lower body, for the split clips.
std::vector<bool> upper_body_mask(const Model& model);

// ── What a soldier is seen carrying (data/force weapon/ and point/) ─────────────
//
// weapon/<stem>.lma (weapon.kst's OBJECT: "sf_a_rifle_m4a1") is the gun at the characters' scale,
// rigid, with its `flame` (muzzle) and `cartridge` nodes; point/<stem>_point.lma is a whole
// skeleton with <stem>_point hung off `Bip01 R Hand`: exactly where the gun sits in the hand.
struct CarriedModel {
    Model model;              // sockets "flame", "cartridge"
    Mat4 attach;              // the gun's root in the right hand's space
};
std::optional<CarriedModel> load_carried(const Data& data, std::string_view stem, std::string* error = nullptr);
// A thing of the force archives that stands on its own, in centimetres, at rest: the Pirate Ship's
// cannon ("weapon/sf_c_cannon_body" on the skeleton "weapon/sf_c_cannon_bone") and its ball
// ("weapon/sf_a_cannon_bullet"). Keys without their extension (.lma or .fxa).
std::optional<Model> load_force_prop(const Data& data, std::string_view mesh, std::string_view skeleton = {}, std::string* error = nullptr);

// A spent case (weapon cartridge/<stem>.fpd and its .jpg: "cartridge", "cartridge_sg" for the
// shotguns): rigid, in the guns' view-model units (0.233 cm), its length along -z from the base.
std::optional<Model> load_cartridge(const Data& data, std::string_view stem, std::string* error = nullptr);

}  // namespace sf
