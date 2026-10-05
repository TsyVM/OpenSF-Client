// SPDX-License-Identifier: MIT
// sf1/data/wld.hpp — world description of a map (world/<map>.wld), version 15.
//
// Structure taken from the client's loader (0x490ED0 and callees) by the Python
// reference work. This reader consumes every byte of the 18 sample worlds and of
// all 76 worlds the installed client ships:
//
//   u8  version (15)
//   char mapping[256]                           the .msf name
//   u32 n; n × char object_name[256]
//   u32 sectors; sectors × {
//       char name[100]
//       u32 n; n × 128-byte object record      (vectors/matrix/mapping index; unverified)
//       u32 n; n × 116-byte secondary record   (unverified)
//       3 index groups: u32 count (group 1 may be 0xFFFFFFFF = absent), then count ×
//           { u32 a; a × u32; and for groups 0 and 1 also u32 b; b × u32 }
//   }
//   u32 n; n × char object_path[256]
//   u32 len (≤ 4096); len bytes world name (trailing NULs dropped)
//   u32 sky_enabled; if non-zero, 6 × char sky[256]   (right, left, top, bottom, back, front in all 76 shipped worlds)
//   sectors × { 0x1458-byte environment; 16 opaque bytes }
//   end of file — trailing bytes are an error
//
// The environment block is u32 active_count (≤ 50), 50 × 104-byte D3DLIGHT9 slots and
// a 4-byte flag. All 1,109 active lights in the samples validate against the
// documented D3DLIGHT9 layout. How the engine uses them (baking vs. dynamic) is
// unverified, and so are spawn, trigger and camera semantics: this reader decodes
// structure only and nothing is written back.
#pragma once

#include "../result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf1::data::wld {

inline constexpr std::uint8_t  kVersion             = 15;
inline constexpr std::size_t   kNameWidth           = 256;
inline constexpr std::size_t   kSectorNameWidth     = 100;
inline constexpr std::size_t   kObjectRecordSize    = 128;
inline constexpr std::size_t   kSecondaryRecordSize = 116;
inline constexpr std::size_t   kEnvironmentSize     = 0x1458;
inline constexpr std::size_t   kLightSlots          = 50;
inline constexpr std::size_t   kLightSize           = 104;

// D3DLIGHTTYPE
enum class LightType : std::uint32_t { Point = 1, Spot = 2, Directional = 3 };

struct Light {                        // D3DLIGHT9
    std::uint64_t          offset = 0;   // file offset of the record
    LightType              type = LightType::Point;
    std::array<float, 4>   diffuse{};
    std::array<float, 4>   specular{};
    std::array<float, 4>   ambient{};
    std::array<float, 3>   position{};
    std::array<float, 3>   direction{};
    float                  range = 0;
    float                  falloff = 0;
    std::array<float, 3>   attenuation{};
    float                  theta = 0;
    float                  phi = 0;
};

struct Record {
    std::uint64_t          offset = 0;
    std::vector<std::byte> raw;           // 128 or 116 bytes, unverified meaning
};

struct IndexGroup {
    bool                   present = true;   // false when group 1 holds the 0xFFFFFFFF sentinel
    std::uint32_t          count = 0;
    std::uint64_t          offset = 0;
    std::uint64_t          size = 0;
};

struct Sector {
    std::string                  name;
    std::uint64_t                offset = 0;
    std::uint64_t                end = 0;
    std::vector<Record>          objects;
    std::vector<Record>          secondary_objects;
    std::array<IndexGroup, 3>    groups{};
    std::uint64_t                environment_offset = 0;
    std::vector<std::byte>       environment;          // kEnvironmentSize bytes
    std::array<std::byte, 16>    environment_extra{};  // unverified
    std::vector<Light>           lights;
};

struct World {
    std::uint8_t                 version = 0;
    std::string                  mapping;
    std::vector<std::string>     object_names;
    std::vector<Sector>          sectors;
    std::vector<std::string>     object_paths;
    std::string                  name;
    std::uint32_t                sky_enabled = 0;
    std::vector<std::string>     sky;                  // empty unless sky_enabled
    std::uint64_t                sky_offset = 0;       // file offset of the first sky name slot
    std::uint64_t                size = 0;
};

// One placed prop, decoded from a sector's 128-byte object record. The layout is
// checked against every record of every world the client ships by
// tests/test_roundtrip.cpp (row 3 of the matrix equals the position, the name index
// is inside object_names, the box is ordered):
//
//   float position[3]     float scale[3]     float euler[3]   (radians, x y z)
//   float world[16]       row-major for row vectors, row 3 = position
//   float bounds_max[3]   float bounds_min[3]                 world-space box of the mesh
//   u32   name_index      into World::object_names ("drum_01" -> object/env/.../drum_01.osf)
//
// The same props appear in "<map>_obj.env"; this copy is the one with the mesh's
// real extent, which is why the editor draws props from here.
struct ObjectRecord {
    std::uint64_t          offset = 0;
    std::array<float, 3>   position{};
    std::array<float, 3>   scale{};
    std::array<float, 3>   euler{};
    std::array<float, 16>  world{};
    std::array<float, 3>   bounds_min{};
    std::array<float, 3>   bounds_max{};
    std::uint32_t          name_index = 0;
};

[[nodiscard]] Result<ObjectRecord> decode_object(const Record& record) noexcept;

// The active lights of one environment block. `base_offset` is added to each
// light's offset so it reports file offsets.
[[nodiscard]] Result<std::vector<Light>> read_lights(std::span<const std::byte> environment,
                                                     std::uint64_t base_offset = 0) noexcept;

[[nodiscard]] Result<World> read(std::span<const std::byte> bytes) noexcept;

// In-place edits of a world file's bytes, for the export path. Each writes only the
// bytes of the field it names, so every structure this reader does not decode comes
// through untouched. `bytes` must be the file `world` was read from.
//
// The six sky names, right left top bottom back front. False when the world has no
// sky block or a name does not fit its 256-byte slot.
[[nodiscard]] bool write_sky(std::vector<std::byte>& bytes, const World& world,
                             const std::vector<std::string>& faces) noexcept;
// One D3DLIGHT9 slot at light.offset.
[[nodiscard]] bool write_light(std::vector<std::byte>& bytes, const Light& light) noexcept;

}  // namespace sf1::data::wld
