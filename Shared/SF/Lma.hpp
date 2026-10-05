// Soldier Front's .lma model files.
//
// Despite what the encyclopedia guesses, these are not a NetImmerse stream: the magic is
// "LMA " and the files carry an "EMFX Default" material, so they are EMotion FX actors — the
// animation middleware Soldier Front was built on. Weapon and character geometry, the bone
// rigs and the attachment points ("flame" at the muzzle, "cartridge" at the ejection port)
// all live in these.
//
// Layout, worked out from the files and checked against every .lma the client ships:
//
//   char   magic[4]      "LMA "
//   u8     version_hi, version_lo, endian
//   chunks, to end of file:
//     u32 id, u32 size, u32 version, then `size` bytes
//
//   id 0  NODE (v1, 184 bytes)
//     char  name[40]        a fixed buffer, so a short name keeps the tail of a longer one
//     char  parent[40]      empty for a root
//     float position[3]     @80
//     float rotation[4]     @92   x, y, z, w — unit in every file seen
//     float scale[3]        @108
//     (the remaining 64 bytes are not needed to place a model and are left alone)
//
//   id 3  MESH (v3)
//     u32 node, org_vertices, vertices, indices, submeshes, uv_sets, unknown   (28 bytes)
//     submeshes x {
//       u32 tag, u32 indices, u32 vertices   tag's low byte is the material, its high byte
//                                            repeats the uv-set count
//       vertices x { u32 org_vertex; float position[3]; float normal[3]; float uv[2] x uv_sets }
//       indices  x u32                        indices are local to the submesh
//     }
//     A vertex is 28 bytes plus 8 per uv set, so the stride is 36 for the usual single-set
//     model and 44 for the two-set reskins. The tag's high byte carries the set count too.
//
//   id 4  SKIN (v1)   follows the mesh it belongs to
//     u32 node
//     org_vertices x { u8 influences; influences x { u16 node; u16 unused; float weight } }
//     The second u16 is one value repeated for a whole file, so it carries nothing. Weights
//     sum to one. Characters are skinned to 3ds Max biped bones ("Bip01 Spine2").
//
//   id 6  MATERIAL (v3)        char name[] at the start of the payload
//   id 7  MATERIAL LAYER (v2)  char texture[] at the start of the payload; belongs to the
//                              material it follows, and names an image without its extension
//
// A motion clip is the same container holding only these two, a part followed by its tracks:
//
//   id 1  MOTION PART (v1, 80 bytes)   the same shape as a node: name[40], then the pose the
//                                      clip was authored against (position, rotation, scale)
//   id 2  TRACK (v1)
//     u32 keys, u32 unused
//     keys x { float time; float value[3] }   16 bytes a key: translation, then scale
//     keys x { float time; float value[4] }   20 bytes a key: rotation, x y z w
//
// The key size says which of the three a track is, and a part's tracks follow it in that
// order. Times are seconds from the start of the clip; the game authored them at 30fps.
#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sf {

using eng::u32;
using eng::u8;
using eng::Vec3;

struct LmaNode {
    std::string name;
    std::string parent;
    Vec3 position;
    float rotation[4] = {0, 0, 0, 1};   // x, y, z, w
    Vec3 scale{1, 1, 1};
};

struct LmaVertex {
    Vec3 position;
    Vec3 normal;
    float uv[2] = {0, 0};
    u32 original = 0;   // the vertex this one was split from: what the skin is keyed on
};

// One submesh, already flattened: indices are local to `vertices`.
struct LmaSubmesh {
    u32 material = 0;   // index into LmaModel::materials
    std::vector<LmaVertex> vertices;
    std::vector<u32> indices;
};

// A material and the texture layers under it, in file order. The first layer is the one
// this engine draws; the rest are effects it has no use for.
struct LmaMaterial {
    std::string name;
    std::vector<std::string> layers;
    const std::string* texture() const { return layers.empty() ? nullptr : &layers.front(); }
};

struct LmaInfluence {
    u32 bone = 0;   // index into LmaModel::nodes
    float weight = 0;
};

struct LmaMesh {
    u32 node = 0;   // index into LmaModel::nodes
    u32 original_vertices = 0;
    std::vector<LmaSubmesh> submeshes;
    // One list per original vertex, empty on a rigid mesh: the weapons are rigid, the hands
    // and every character piece are skinned. Weights sum to one.
    std::vector<std::vector<LmaInfluence>> skin;
    bool skinned() const { return !skin.empty(); }
};

// One animated bone of a clip. A track left empty means the clip does not drive that
// channel and the bone keeps its bind value there.
struct LmaMotionPart {
    std::string name;
    Vec3 position;
    float rotation[4] = {0, 0, 0, 1};
    Vec3 scale{1, 1, 1};
    std::vector<float> position_times;
    std::vector<Vec3> positions;
    std::vector<float> rotation_times;
    std::vector<std::array<float, 4>> rotations;
};

struct LmaModel {
    std::vector<LmaNode> nodes;
    std::vector<LmaMesh> meshes;
    std::vector<LmaMaterial> materials;
    std::vector<LmaMotionPart> motion;   // empty unless the file is a clip

    // The node a name belongs to, or -1. Weapons use this for "flame" and "cartridge".
    int find_node(std::string_view name) const;
    size_t triangle_count() const;
    // The last keyed time of any track, in seconds.
    float duration() const;
};

std::optional<LmaModel> read_lma(std::span<const std::byte> bytes, std::string* error = nullptr);

}  // namespace sf
