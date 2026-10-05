// SPDX-License-Identifier: MIT
// sf1/data/cft.hpp — collision geometry of a <map>_c.cft file, version 6.
//
//   u32 version (6)
//   u32 mesh_count (≤ 10,000)
//   mesh_count × { char name[256]; 12 opaque bytes;
//                  u32 v; v × float3;
//                  u32 i; i × u16;              (multiple of 3, each < v)
//                  u32 f; f × float3 }          (f == i / 3, face normals)
//   trailing data (undecoded spatial tree)
//
// One mesh per map sector. Everything is checked finite. The 12 bytes per mesh and
// the data from spatial_offset onwards are not decoded, which is why no collision
// writer exists: rewriting geometry without rebuilding the tree is unsafe.
#pragma once

#include "../result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf1::data::cft {

inline constexpr std::uint32_t kVersion = 6;

struct Mesh {
    std::string                          name;
    std::array<std::byte, 12>            coefficients{};   // unverified
    std::vector<std::array<float, 3>>    vertices;
    std::vector<std::uint16_t>           indices;
    std::vector<std::array<float, 3>>    face_normals;
    std::uint64_t                        vertex_offset = 0;
    [[nodiscard]] std::size_t triangle_count() const noexcept { return indices.size() / 3; }
};

struct Collision {
    std::uint32_t        version = 0;
    std::vector<Mesh>    meshes;
    std::uint64_t        spatial_offset = 0;   // first byte of the undecoded trailing data
    std::uint64_t        size = 0;
};

[[nodiscard]] Result<Collision> read(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::cft
