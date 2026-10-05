// SPDX-License-Identifier: MIT
// sf1/data/map.hpp — visual geometry of a .map file, version 6 / type 1.
//
// Structure verified by parsing all 76 map entries in the installed client and the
// 16 sample maps with the Python reference reader and this one (identical counts):
//
//   u32 version (6)   u32 type (1)
//   u32 sector_count (≤ 10,000)
//   sector_count × { char name[256]; 16 opaque bytes;
//                    u32 n; n × 20 opaque bytes;
//                    u32 m; m × char lightmap[256] }
//   u32 portal_count  × { char name[256]; u32 v; v × float3; u32 i; i × u16; 24 opaque bytes }
//   sector_count × { u32 v; v × 40-byte Vertex;
//                    u32 g; g × { u32 material; u32 i; i × u16; u32 f; f × float3 face normal } }
//   trailing data (undecoded spatial structures)
//
// Rules: index counts are multiples of 3 and every index is below its vertex
// count; face-normal counts equal triangle counts; positions, UVs and face normals
// are finite. Vertex normals are NOT checked: some shipped vertices carry the
// 0xCDCDCDCD fill pattern there (and in the second UV set), so consumers must
// repair them. material 0xFFFFFFFF means "no material".
//
// Meaning still unverified: the 16-byte sector header, the 20-byte sector records,
// the 24-byte portal trailer, and everything after geometry_end.
#pragma once

#include "../result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf1::data::map {

inline constexpr std::uint32_t kVersion    = 6;
inline constexpr std::uint32_t kType       = 1;
inline constexpr std::uint32_t kNoMaterial = 0xFFFFFFFF;
inline constexpr std::size_t   kNameWidth  = 256;

struct Vertex {
    float position[3];
    float normal[3];   // may be non-finite in shipped data
    float uv0[2];      // material texture
    float uv1[2];      // lightmap; 0xCDCDCDCD fill where a vertex has none
};

struct Group {
    std::uint32_t                        material = kNoMaterial;   // index into the .msf table
    std::vector<std::uint16_t>           indices;                  // triangle list
    std::vector<std::array<float, 3>>    face_normals;             // one per triangle
    [[nodiscard]] std::size_t triangle_count() const noexcept { return indices.size() / 3; }
};

struct Sector {
    std::string                          name;
    std::array<std::byte, 16>            header{};       // unverified
    std::vector<std::array<std::byte, 20>> records;      // unverified
    std::vector<std::string>             lightmaps;      // file names, e.g. "SECTOR_01LightingMap.tga"
    std::vector<Vertex>                  vertices;
    std::vector<Group>                   groups;
    std::uint64_t                        vertex_offset = 0;   // file offset of the first vertex
};

struct Portal {
    std::string                          name;
    std::vector<std::array<float, 3>>    vertices;
    std::vector<std::uint16_t>           indices;
    std::array<std::byte, 24>            trailer{};      // unverified
};

struct Map {
    std::uint32_t        version = 0;
    std::uint32_t        type = 0;
    std::vector<Sector>  sectors;
    std::vector<Portal>  portals;
    std::uint64_t        geometry_end = 0;   // first byte of the undecoded trailing data
    std::uint64_t        size = 0;           // total file size
};

[[nodiscard]] Result<Map> read(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::map
