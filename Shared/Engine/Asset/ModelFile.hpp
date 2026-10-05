// .tmdl — the engine's model format (weapons, characters, props). Chunked like .tmap:
//
//   u32 magic 'TMDL'   u32 version (1)   u32 chunk_count   chunks { u32 id; u32 size; bytes }
//
//   'MATL'  u32 n; n x { string texture; u32 flags }
//   'MESH'  u32 n; n x { string name; u32 material; u32 v; v x ModelVertex (40 bytes);
//                         u32 i; i x u32; vec3 min; vec3 max }
//   'SKEL'  u32 n; n x { string name; i32 parent; f32[16] bind_local; f32[16] inverse_bind }
//   'SOCK'  u32 n; n x { string name; i32 bone; f32[16] local }
//   'ANIM'  u32 n; n x { string name; f32 duration; u32 keys; keys x f32 time; u32 tracks;
//                         tracks x { u32 bone; u32 count;
//                                    count x (vec3 translation, f32[4] rotation xyzw) } }
//
// A clip's key times are shared by every track and need not be evenly spaced, because the
// animation carried over from a game usually is not. A track holding one key is constant
// for the whole clip; a bone with no track at all stays in its bind pose.
//
// Matrices are row-major for row vectors (Engine/Core/Math.hpp). Bones are ordered so
// a parent always precedes its children. Rigid meshes have all weight on bone 0 or no skeleton.
#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Types.hpp"

#include <span>
#include <string>
#include <vector>

namespace eng {

inline constexpr u32 kModelMagic = fourcc('T', 'M', 'D', 'L');
inline constexpr u32 kModelVersion = 2;

#pragma pack(push, 1)
struct ModelVertex {
    float position[3];
    float normal[3];
    float uv[2];
    u8 bones[4];
    u8 weights[4];   // sum to 255 when skinned
};
#pragma pack(pop)

struct ModelMaterial {
    std::string texture;
    u32 flags = 0;   // MaterialFlag bits from MapFile.hpp
};

struct ModelMesh {
    std::string name;
    u32 material = 0;
    std::vector<ModelVertex> vertices;
    std::vector<u32> indices;
    Aabb bounds;
};

struct ModelBone {
    std::string name;
    i32 parent = -1;
    Mat4 bind_local;
    Mat4 inverse_bind;
};

struct ModelSocket {
    std::string name;
    i32 bone = -1;
    Mat4 local;
};

struct AnimTrack {
    u32 bone = 0;
    std::vector<float> keys;   // count x 7: tx ty tz qx qy qz qw
    size_t count() const { return keys.size() / 7; }
    bool constant() const { return count() == 1; }
};

struct ModelAnimation {
    std::string name;
    float duration = 0;          // seconds
    std::vector<float> times;    // seconds, ascending, one per key
    std::vector<AnimTrack> tracks;
};

struct ModelData {
    std::vector<ModelMaterial> materials;
    std::vector<ModelMesh> meshes;
    std::vector<ModelBone> bones;
    std::vector<ModelSocket> sockets;
    std::vector<ModelAnimation> animations;
    Aabb bounds() const {
        Aabb b;
        for (const auto& m : meshes) b.add(m.bounds);
        return b;
    }
};

bool read_model(std::span<const u8> bytes, ModelData& out, std::string* error = nullptr);
std::vector<u8> write_model(const ModelData& model);

}  // namespace eng
