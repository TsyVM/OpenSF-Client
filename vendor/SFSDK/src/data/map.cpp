// SPDX-License-Identifier: MIT
#include "sf1/data/map.hpp"
#include "geometry_common.hpp"

namespace sf1::data::map {

static_assert(sizeof(Vertex) == 40);

Result<Map> read(std::span<const std::byte> bytes) noexcept {
    using detail::BinaryReader;
    BinaryReader r(bytes);
    Map m;
    m.size = bytes.size();

    auto version = r.u32();
    auto type = r.u32();
    if (!version || !type) return err(Error::ShortRead);
    if (*version != kVersion || *type != kType) return err(Error::UnsupportedVersion);
    m.version = *version;
    m.type = *type;

    auto sector_count = r.count(10000);
    if (!sector_count) return err(sector_count.error());
    m.sectors.resize(*sector_count);
    for (auto& s : m.sectors) {
        auto name = r.name(kNameWidth);
        auto header = r.fixed<16>();
        if (!name || !header) return err(Error::ShortRead);
        s.name = std::move(*name);
        s.header = *header;

        auto record_count = r.count(10000);
        if (!record_count) return err(record_count.error());
        auto records = r.take_array(*record_count, 20);
        if (!records) return err(records.error());
        s.records = detail::copy_pod<std::array<std::byte, 20>>(*records);

        auto lightmap_count = r.count(10000);
        if (!lightmap_count) return err(lightmap_count.error());
        for (std::uint32_t i = 0; i < *lightmap_count; ++i) {
            auto lm = r.name(kNameWidth);
            if (!lm) return err(lm.error());
            s.lightmaps.push_back(std::move(*lm));
        }
    }

    auto portal_count = r.count(10000);
    if (!portal_count) return err(portal_count.error());
    m.portals.resize(*portal_count);
    for (auto& p : m.portals) {
        auto name = r.name(kNameWidth);
        if (!name) return err(name.error());
        p.name = std::move(*name);
        auto verts = detail::read_float3s(r, 1000000);
        if (!verts) return err(verts.error());
        p.vertices = std::move(*verts);
        auto idx = detail::read_indices(r, p.vertices.size());
        if (!idx) return err(idx.error());
        p.indices = std::move(*idx);
        auto trailer = r.fixed<24>();
        if (!trailer) return err(trailer.error());
        p.trailer = *trailer;
    }

    for (auto& s : m.sectors) {
        auto vertex_count = r.count(1000000);
        if (!vertex_count) return err(vertex_count.error());
        s.vertex_offset = r.pos();
        auto raw = r.take_array(*vertex_count, sizeof(Vertex));
        if (!raw) return err(raw.error());
        s.vertices = detail::copy_pod<Vertex>(*raw);
        for (const auto& v : s.vertices) {
            if (!detail::all_finite(v.position, 3) || !detail::all_finite(v.uv0, 2) || !detail::all_finite(v.uv1, 2))
                return err(Error::NonFinite);
        }

        auto group_count = r.count(10000);
        if (!group_count) return err(group_count.error());
        s.groups.resize(*group_count);
        for (auto& g : s.groups) {
            auto material = r.u32();
            if (!material) return err(material.error());
            g.material = *material;
            auto idx = detail::read_indices(r, s.vertices.size());
            if (!idx) return err(idx.error());
            g.indices = std::move(*idx);
            auto normal_count = r.count(1000000);
            if (!normal_count) return err(normal_count.error());
            if (*normal_count != g.triangle_count()) return err(Error::Malformed);
            auto normals = detail::read_float3_array(r, *normal_count);
            if (!normals) return err(normals.error());
            g.face_normals = std::move(*normals);
        }
    }

    m.geometry_end = r.pos();
    return m;
}

}  // namespace sf1::data::map
