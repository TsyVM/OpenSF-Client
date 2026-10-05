#include "Engine/Asset/ModelFile.hpp"

#include "Engine/Core/ByteStream.hpp"

namespace eng {

namespace {

constexpr u32 kMatl = fourcc('M', 'A', 'T', 'L');
constexpr u32 kMesh = fourcc('M', 'E', 'S', 'H');
constexpr u32 kSkel = fourcc('S', 'K', 'E', 'L');
constexpr u32 kSock = fourcc('S', 'O', 'C', 'K');
constexpr u32 kAnim = fourcc('A', 'N', 'I', 'M');

void read_mat(ByteReader& r, Mat4& m) { r.raw(&m.m[0][0], 16 * sizeof(float)); }
void write_mat(ByteWriter& w, const Mat4& m) { w.raw(&m.m[0][0], 16 * sizeof(float)); }

}  // namespace

bool read_model(std::span<const u8> bytes, ModelData& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    out = ModelData{};
    ByteReader r(bytes);
    if (r.u32() != kModelMagic) return fail("not a model");
    if (r.u32() != kModelVersion) return fail("unsupported model version");
    u32 chunks = r.u32();
    for (u32 c = 0; c < chunks && r.ok(); ++c) {
        u32 id = r.u32();
        u32 size = r.u32();
        auto body = r.view(size);
        if (!r.ok()) return fail("truncated chunk");
        ByteReader b(body);
        switch (id) {
            case kMatl:
                out.materials.resize(b.u32() & 0xFFFF);
                for (auto& m : out.materials) {
                    m.texture = b.string();
                    m.flags = b.u32();
                }
                break;
            case kMesh:
                out.meshes.resize(b.u32() & 0xFFFF);
                for (auto& m : out.meshes) {
                    m.name = b.string();
                    m.material = b.u32();
                    b.array(m.vertices, b.u32());
                    b.array(m.indices, b.u32());
                    m.bounds.min = b.vec3();
                    m.bounds.max = b.vec3();
                }
                break;
            case kSkel:
                out.bones.resize(b.u32() & 0xFFFF);
                for (auto& bone : out.bones) {
                    bone.name = b.string();
                    bone.parent = b.i32();
                    read_mat(b, bone.bind_local);
                    read_mat(b, bone.inverse_bind);
                }
                break;
            case kSock:
                out.sockets.resize(b.u32() & 0xFFFF);
                for (auto& s : out.sockets) {
                    s.name = b.string();
                    s.bone = b.i32();
                    read_mat(b, s.local);
                }
                break;
            case kAnim:
                out.animations.resize(b.u32() & 0xFFFF);
                for (auto& a : out.animations) {
                    a.name = b.string();
                    a.duration = b.f32();
                    b.array(a.times, b.u32());
                    a.tracks.resize(b.u32() & 0xFFFF);
                    for (auto& t : a.tracks) {
                        t.bone = b.u32();
                        b.array(t.keys, size_t(b.u32()) * 7);
                    }
                }
                break;
            default:
                break;
        }
        if (!b.ok()) return fail("corrupt chunk");
    }
    if (!r.ok()) return fail("truncated model");
    for (size_t i = 0; i < out.bones.size(); ++i)
        if (out.bones[i].parent >= int(i)) return fail("bone parent order");
    for (const auto& m : out.meshes) {
        if (!out.materials.empty() && m.material >= out.materials.size()) return fail("mesh material out of range");
        for (u32 i : m.indices)
            if (i >= m.vertices.size()) return fail("mesh index out of range");
        if (!out.bones.empty())
            for (const auto& v : m.vertices)
                for (int k = 0; k < 4; ++k)
                    if (v.weights[k] && v.bones[k] >= out.bones.size()) return fail("vertex bone out of range");
    }
    for (const auto& a : out.animations)
        for (const auto& t : a.tracks) {
            if (t.bone >= out.bones.size()) return fail("animation bone out of range");
            if (t.count() != 1 && t.count() != a.times.size()) return fail("animation track length");
        }
    return true;
}

std::vector<u8> write_model(const ModelData& model) {
    ByteWriter w;
    w.u32(kModelMagic);
    w.u32(kModelVersion);
    size_t count_at = w.size();
    w.u32(0);
    u32 chunks = 0;
    auto chunk = [&](u32 id, const ByteWriter& body) {
        w.u32(id);
        w.u32(u32(body.size()));
        w.raw(body.data().data(), body.size());
        ++chunks;
    };
    {
        ByteWriter b;
        b.u32(u32(model.materials.size()));
        for (const auto& m : model.materials) {
            b.string(m.texture);
            b.u32(m.flags);
        }
        chunk(kMatl, b);
    }
    {
        ByteWriter b;
        b.u32(u32(model.meshes.size()));
        for (const auto& m : model.meshes) {
            b.string(m.name);
            b.u32(m.material);
            b.u32(u32(m.vertices.size()));
            b.raw(m.vertices.data(), m.vertices.size() * sizeof(ModelVertex));
            b.u32(u32(m.indices.size()));
            b.raw(m.indices.data(), m.indices.size() * sizeof(u32));
            b.vec3(m.bounds.min);
            b.vec3(m.bounds.max);
        }
        chunk(kMesh, b);
    }
    if (!model.bones.empty()) {
        ByteWriter b;
        b.u32(u32(model.bones.size()));
        for (const auto& bone : model.bones) {
            b.string(bone.name);
            b.i32(bone.parent);
            write_mat(b, bone.bind_local);
            write_mat(b, bone.inverse_bind);
        }
        chunk(kSkel, b);
    }
    if (!model.sockets.empty()) {
        ByteWriter b;
        b.u32(u32(model.sockets.size()));
        for (const auto& s : model.sockets) {
            b.string(s.name);
            b.i32(s.bone);
            write_mat(b, s.local);
        }
        chunk(kSock, b);
    }
    if (!model.animations.empty()) {
        ByteWriter b;
        b.u32(u32(model.animations.size()));
        for (const auto& a : model.animations) {
            b.string(a.name);
            b.f32(a.duration);
            b.u32(u32(a.times.size()));
            b.raw(a.times.data(), a.times.size() * sizeof(float));
            b.u32(u32(a.tracks.size()));
            for (const auto& t : a.tracks) {
                b.u32(t.bone);
                b.u32(u32(t.count()));
                b.raw(t.keys.data(), t.keys.size() * sizeof(float));
            }
        }
        chunk(kAnim, b);
    }
    w.patch_u32(count_at, chunks);
    return w.take();
}

}  // namespace eng
