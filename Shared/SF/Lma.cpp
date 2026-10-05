#include "SF/Lma.hpp"

#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sf {

namespace {

using eng::u32;

// A cursor that refuses to read past the end, so a truncated or mis-sized file fails cleanly
// rather than walking off into whatever follows it.
class Reader {
public:
    Reader(const u8* data, size_t size) : p_(data), n_(size) {}

    bool ok() const { return ok_; }
    size_t left() const { return at_ <= n_ ? n_ - at_ : 0; }
    size_t at() const { return at_; }

    u32 u32v() {
        u32 v = 0;
        if (!take(&v, 4)) return 0;
        return v;
    }
    u32 u16v() {
        eng::u16 v = 0;
        if (!take(&v, 2)) return 0;
        return v;
    }
    u32 u8v() {
        u8 v = 0;
        if (!take(&v, 1)) return 0;
        return v;
    }
    float f32() {
        float v = 0;
        if (!take(&v, 4)) return 0;
        if (!std::isfinite(v)) {
            ok_ = false;
            return 0;
        }
        return v;
    }
    Vec3 vec3() {
        Vec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }
    // A fixed-size character buffer. The tail after the NUL is whatever was in the buffer
    // before, so it is dropped rather than kept.
    std::string fixed_string(size_t size) {
        if (at_ + size > n_) {
            ok_ = false;
            return {};
        }
        const char* s = reinterpret_cast<const char*>(p_ + at_);
        at_ += size;
        size_t len = 0;
        while (len < size && s[len]) ++len;
        return std::string(s, len);
    }
    void skip(size_t size) {
        if (at_ + size > n_) ok_ = false;
        else at_ += size;
    }

private:
    bool take(void* out, size_t size) {
        if (at_ + size > n_) {
            ok_ = false;
            return false;
        }
        std::memcpy(out, p_ + at_, size);
        at_ += size;
        return true;
    }

    const u8* p_;
    size_t n_;
    size_t at_ = 0;
    bool ok_ = true;
};

constexpr u32 kChunkNode = 0;
constexpr u32 kChunkMotionPart = 1;
constexpr u32 kChunkTrack = 2;
constexpr u32 kChunkMesh = 3;
constexpr u32 kChunkSkin = 4;
constexpr u32 kChunkMaterial = 6;
constexpr u32 kMaxInfluences = 8;
constexpr u32 kChunkMaterialLayer = 7;

// A motion part is a node's name and pose without the trailing fields a node carries.
constexpr size_t kMotionPartPayload = 80;
constexpr size_t kTrackHeader = 8;
constexpr size_t kVec3Key = 16;   // time plus three floats
constexpr size_t kQuatKey = 20;   // time plus four

constexpr size_t kNodeNameSize = 40;
constexpr size_t kNodePayload = 184;

// A vertex without its uv sets: original index, position, normal.
constexpr size_t kVertexBase = 28;
constexpr u32 kMaxUvSets = 8;

bool fail(std::string* error, std::string text) {
    if (error) *error = std::move(text);
    return false;
}

}  // namespace

int LmaModel::find_node(std::string_view name) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (eng::str::iequals(nodes[i].name, name)) return int(i);
    return -1;
}

float LmaModel::duration() const {
    float end = 0;
    for (const LmaMotionPart& p : motion) {
        if (!p.position_times.empty()) end = std::max(end, p.position_times.back());
        if (!p.rotation_times.empty()) end = std::max(end, p.rotation_times.back());
    }
    return end;
}

size_t LmaModel::triangle_count() const {
    size_t n = 0;
    for (const LmaMesh& m : meshes)
        for (const LmaSubmesh& s : m.submeshes) n += s.indices.size() / 3;
    return n;
}

std::optional<LmaModel> read_lma(std::span<const std::byte> bytes, std::string* error) {
    const u8* data = reinterpret_cast<const u8*>(bytes.data());
    const size_t size = bytes.size();
    // FXA is the later EMotion FX actor the newer forces (ARTC, Force Recon, Mulan, PSU) ship as:
    // "FXA " and two version bytes, an info chunk (16), and nodes (v3) and materials (v5, v4 layers)
    // whose names are length-prefixed; its meshes (v3) and skins (v1) are LMA's byte for byte.
    // FXM is its motion (the undead's clips, motion/zman/*.fxm): the same header and info chunk, motion
    // parts (v3) with the name last and length-prefixed, and tracks as LMA's.
    const bool fxa = size >= 6 && (std::memcmp(data, "FXA ", 4) == 0 || std::memcmp(data, "FXM ", 4) == 0);
    if (!fxa && (size < 7 || std::memcmp(data, "LMA ", 4) != 0)) {
        fail(error, "not an LMA, FXA or FXM file");
        return std::nullopt;
    }

    LmaModel model;
    size_t at = fxa ? 6 : 7;   // magic plus the version bytes
    while (at + 12 <= size) {
        u32 id, chunk_size, version;
        std::memcpy(&id, data + at, 4);
        std::memcpy(&chunk_size, data + at + 4, 4);
        std::memcpy(&version, data + at + 8, 4);
        const size_t payload = at + 12;
        if (chunk_size > size - payload) {
            fail(error, eng::str::format("chunk %u at %zu runs past the end", id, at));
            return std::nullopt;
        }
        Reader r(data + payload, chunk_size);

        switch (id) {
            case kChunkNode: {
                if (fxa || version >= 3) {
                    // position, rotation (x y z w), scale rotation, scale, 12 bytes, then the
                    // name and the parent's name, each a u32 length and its characters.
                    LmaNode node;
                    node.position = r.vec3();
                    for (float& f : node.rotation) f = r.f32();
                    r.skip(16);
                    node.scale = r.vec3();
                    r.skip(12);
                    const u32 name_len = r.u32v();
                    node.name = r.fixed_string(std::min<size_t>(name_len, r.left()));
                    const u32 parent_len = r.u32v();
                    node.parent = r.fixed_string(std::min<size_t>(parent_len, r.left()));
                    if (!r.ok()) {
                        fail(error, "truncated node");
                        return std::nullopt;
                    }
                    model.nodes.push_back(std::move(node));
                    break;
                }
                if (chunk_size < kNodePayload) break;   // an older or shorter node: skip it
                LmaNode node;
                node.name = r.fixed_string(kNodeNameSize);
                node.parent = r.fixed_string(kNodeNameSize);
                node.position = r.vec3();
                for (float& f : node.rotation) f = r.f32();
                node.scale = r.vec3();
                if (!r.ok()) {
                    fail(error, "truncated node");
                    return std::nullopt;
                }
                model.nodes.push_back(std::move(node));
                break;
            }
            case kChunkMesh: {
                LmaMesh mesh;
                mesh.node = r.u32v();
                mesh.original_vertices = r.u32v();   // before hard edges split them
                const u32 total_vertices = r.u32v();
                const u32 total_indices = r.u32v();
                const u32 submeshes = r.u32v();
                const u32 uv_sets = r.u32v();
                r.u32v();
                if (!r.ok() || submeshes > 4096 || uv_sets > kMaxUvSets) {
                    fail(error, "bad mesh header");
                    return std::nullopt;
                }
                // A vertex carries one uv pair per set. Most models have one set; the reskinned
                // weapons ("black dragon", "cutie", the gold guns) carry two, and the second is
                // a lightmap-style overlay this engine has no use for, so only the first is kept.
                // Collision hulls, bone shapes and the three-vertex marker triangles have none.
                const size_t stride = kVertexBase + 8 * size_t(uv_sets);
                u32 seen_vertices = 0, seen_indices = 0;
                for (u32 s = 0; s < submeshes; ++s) {
                    const u32 tag = r.u32v();
                    const u32 index_count = r.u32v();
                    const u32 vertex_count = r.u32v();
                    if (!r.ok() || index_count % 3 != 0) {
                        fail(error, "bad submesh header");
                        return std::nullopt;
                    }
                    // Refuse anything that cannot fit rather than allocating on a number read
                    // out of a corrupt file.
                    if (size_t(vertex_count) * stride + size_t(index_count) * 4 > r.left()) {
                        fail(error, "submesh is bigger than the chunk");
                        return std::nullopt;
                    }
                    LmaSubmesh sub;
                    sub.material = tag & 0xff;
                    sub.vertices.resize(vertex_count);
                    for (LmaVertex& v : sub.vertices) {
                        v.original = r.u32v();
                        v.position = r.vec3();
                        v.normal = r.vec3();
                        if (uv_sets > 0) {
                            v.uv[0] = r.f32();
                            v.uv[1] = r.f32();
                            r.skip(8 * (uv_sets - 1));
                        }
                    }
                    sub.indices.resize(index_count);
                    for (u32& i : sub.indices) {
                        i = r.u32v();
                        if (i >= vertex_count) {
                            fail(error, "submesh index out of range");
                            return std::nullopt;
                        }
                    }
                    if (!r.ok()) {
                        fail(error, "truncated submesh");
                        return std::nullopt;
                    }
                    seen_vertices += vertex_count;
                    seen_indices += index_count;
                    mesh.submeshes.push_back(std::move(sub));
                }
                if (seen_vertices != total_vertices || seen_indices != total_indices) {
                    fail(error, eng::str::format("submeshes hold %u/%u vertices and %u/%u indices", seen_vertices,
                                                 total_vertices, seen_indices, total_indices));
                    return std::nullopt;
                }
                model.meshes.push_back(std::move(mesh));
                break;
            }
            case kChunkMotionPart: {
                if (fxa && version >= 3) {
                    // FXM: the pose (position, rotation x y z w, scale), the bind pose the same
                    // way, then the node's name, a u32 length and its characters.
                    LmaMotionPart part;
                    part.position = r.vec3();
                    for (float& f : part.rotation) f = r.f32();
                    part.scale = r.vec3();
                    r.skip(40);
                    const u32 name_len = r.u32v();
                    part.name = r.fixed_string(std::min<size_t>(name_len, r.left()));
                    if (!r.ok()) {
                        fail(error, "truncated motion part");
                        return std::nullopt;
                    }
                    model.motion.push_back(std::move(part));
                    break;
                }
                if (chunk_size < kMotionPartPayload) break;
                LmaMotionPart part;
                part.name = r.fixed_string(kNodeNameSize);
                part.position = r.vec3();
                for (float& f : part.rotation) f = r.f32();
                part.scale = r.vec3();
                if (!r.ok()) {
                    fail(error, "truncated motion part");
                    return std::nullopt;
                }
                model.motion.push_back(std::move(part));
                break;
            }
            case kChunkTrack: {
                if (model.motion.empty()) {
                    fail(error, "track before any motion part");
                    return std::nullopt;
                }
                const u32 keys = r.u32v();
                r.u32v();
                if (!r.ok() || keys == 0) {
                    fail(error, "empty track");
                    return std::nullopt;
                }
                const size_t body = chunk_size - kTrackHeader;
                if (body % keys != 0) {
                    fail(error, "track does not divide into its keys");
                    return std::nullopt;
                }
                LmaMotionPart& part = model.motion.back();
                switch (body / keys) {
                    case kQuatKey:
                        part.rotation_times.resize(keys);
                        part.rotations.resize(keys);
                        for (u32 k = 0; k < keys; ++k) {
                            part.rotation_times[k] = r.f32();
                            for (float& f : part.rotations[k]) f = r.f32();
                        }
                        break;
                    case kVec3Key: {
                        // Translation comes first; a second such track is the scale, which
                        // nothing in this content animates, so it is read past and dropped.
                        const bool have_translation = !part.positions.empty();
                        std::vector<float> times(keys);
                        std::vector<Vec3> values(keys);
                        for (u32 k = 0; k < keys; ++k) {
                            times[k] = r.f32();
                            values[k] = r.vec3();
                        }
                        if (!have_translation) {
                            part.position_times = std::move(times);
                            part.positions = std::move(values);
                        }
                        break;
                    }
                    default:
                        fail(error, eng::str::format("track key is %zu bytes", body / keys));
                        return std::nullopt;
                }
                if (!r.ok()) {
                    fail(error, "truncated track");
                    return std::nullopt;
                }
                break;
            }
            case kChunkSkin: {
                // The skin names the mesh it belongs to by node, and every file seen puts it
                // straight after that mesh, so the newest match is the right one.
                const u32 node = r.u32v();
                LmaMesh* mesh = nullptr;
                for (size_t i = model.meshes.size(); i-- > 0;)
                    if (model.meshes[i].node == node) {
                        mesh = &model.meshes[i];
                        break;
                    }
                if (!mesh) {
                    fail(error, "skin for a mesh that is not there");
                    return std::nullopt;
                }
                std::vector<std::vector<LmaInfluence>> skin(mesh->original_vertices);
                for (auto& influences : skin) {
                    const u32 count = r.u8v();
                    if (!r.ok() || count == 0 || count > kMaxInfluences) {
                        fail(error, "bad influence count");
                        return std::nullopt;
                    }
                    influences.resize(count);
                    for (LmaInfluence& in : influences) {
                        in.bone = r.u16v();
                        r.u16v();
                        in.weight = r.f32();
                        if (in.bone >= model.nodes.size()) {
                            fail(error, "influence on a node that is not there");
                            return std::nullopt;
                        }
                    }
                }
                if (!r.ok() || r.left() != 0) {
                    fail(error, "skin does not cover its mesh exactly");
                    return std::nullopt;
                }
                mesh->skin = std::move(skin);
                break;
            }
            case kChunkMaterial:
                if (fxa || version >= 5) {
                    // 68 bytes of colours and factors, then the name.
                    r.skip(68);
                    const u32 len = r.u32v();
                    model.materials.push_back({r.fixed_string(std::min<size_t>(len, r.left())), {}});
                } else {
                    model.materials.push_back({r.fixed_string(chunk_size), {}});
                }
                break;
            case kChunkMaterialLayer:
                if (fxa || version >= 4) {
                    // u8 type, u8, u16 the material it belongs to, six floats (amount, offsets,
                    // tiling, rotation), then the texture's name.
                    r.u8v();
                    r.u8v();
                    const u32 owner = r.u16v();
                    r.skip(24);
                    const u32 len = r.u32v();
                    std::string name = r.fixed_string(std::min<size_t>(len, r.left()));
                    if (owner < model.materials.size()) model.materials[owner].layers.push_back(std::move(name));
                    else if (!model.materials.empty()) model.materials.back().layers.push_back(std::move(name));
                    break;
                }
                if (model.materials.empty()) break;   // a layer with no material owns nothing
                model.materials.back().layers.push_back(r.fixed_string(chunk_size));
                break;
            default:
                break;   // animation and the rest: not needed to build a model
        }
        at = payload + chunk_size;
    }
    if (at != size) {
        fail(error, eng::str::format("%zu trailing bytes", size - at));
        return std::nullopt;
    }
    // A file with neither is one of the motion clips the .sfm manifests list: same container,
    // only animation chunks inside. That is a valid read of a file with no model in it.
    return model;
}

}  // namespace sf
