// SPDX-License-Identifier: MIT
#include "sf1/data/cft.hpp"
#include "geometry_common.hpp"

namespace sf1::data::cft {

Result<Collision> read(std::span<const std::byte> bytes) noexcept {
    detail::BinaryReader r(bytes);
    Collision c;
    c.size = bytes.size();

    auto version = r.u32();
    if (!version) return err(version.error());
    if (*version != kVersion) return err(Error::UnsupportedVersion);
    c.version = *version;

    auto mesh_count = r.count(10000);
    if (!mesh_count) return err(mesh_count.error());
    c.meshes.resize(*mesh_count);
    for (auto& m : c.meshes) {
        auto name = r.name(256);
        auto coefficients = r.fixed<12>();
        if (!name || !coefficients) return err(Error::ShortRead);
        m.name = std::move(*name);
        m.coefficients = *coefficients;

        auto vertex_count = r.count(1000000);
        if (!vertex_count) return err(vertex_count.error());
        m.vertex_offset = r.pos();
        auto verts = detail::read_float3_array(r, *vertex_count);
        if (!verts) return err(verts.error());
        m.vertices = std::move(*verts);

        auto idx = detail::read_indices(r, m.vertices.size());
        if (!idx) return err(idx.error());
        m.indices = std::move(*idx);

        auto normal_count = r.count(1000000);
        if (!normal_count) return err(normal_count.error());
        if (*normal_count != m.triangle_count()) return err(Error::Malformed);
        auto normals = detail::read_float3_array(r, *normal_count);
        if (!normals) return err(normals.error());
        m.face_normals = std::move(*normals);
    }

    c.spatial_offset = r.pos();
    return c;
}

}  // namespace sf1::data::cft
