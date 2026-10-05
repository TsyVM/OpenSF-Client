// SPDX-License-Identifier: MIT
// sf1/data/map_bundle.hpp — everything one map needs, loaded from an AssetLibrary.
//
// For a map key like "ground/sf_m_satellite/sf_m_satellite.map" (stem = key minus ".map"):
//
//   texture table   resolve(stem + ".msf"), else resolve("<basename>.msf")      required
//   collision       find(stem + "_c.cft")                                        optional
//   world           resolve("world/<basename>.wld")                              optional
//   objects         resolve(stem + "_obj.env")                                   optional
//   world script    resolve(stem + ".xml")                                        optional
//   lightmaps       resolve(stem's folder + "/level1/" + sector lightmap name)   per sector
//
// The first three rules are the Python reference editor's. The lightmap rule is a
// preview lookup found by census: it resolves 790 of the 875 sector lightmaps the
// installed client's maps name (the rest are absent from the archives). How the
// client itself locates lightmaps is unverified.
#pragma once

#include "asset_library.hpp"
#include "cft.hpp"
#include "env.hpp"
#include "map.hpp"
#include "msf.hpp"
#include "wld.hpp"
#include "worldscript.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data {

struct MapBundle {
    std::string                   map_key;
    map::Map                      map;

    std::string                   mapping_key;
    std::vector<msf::Mapping>     materials;

    std::string                   collision_key;      // empty when the map has none
    std::optional<cft::Collision> collision;

    std::string                   world_key;          // empty when no world file resolved
    std::optional<wld::World>     world;
    std::vector<std::byte>        world_bytes;        // the file as read, for in-place export edits
    Error                         world_error = Error::Ok;   // set when world_key resolved but failed to parse

    std::string                   objects_key;        // "<stem>_obj.env", empty when the map has none
    std::optional<env::Objects>   objects;
    Error                         objects_error = Error::Ok;

    std::string                   script_key;         // "<stem>.xml", empty when the map has none
    std::optional<script::WorldScript> script;
    std::vector<std::byte>        script_bytes;       // the document as read, for lossless editing
    Error                         script_error = Error::Ok;
};

// `map_name` is a full key held by the primary archive, or a substring matching
// exactly one .map key of the primary archive. Geometry, texture-table and
// collision failures fail the load; a world file, object list or world script that
// does not parse is reported in its *_error field and the map still loads.
[[nodiscard]] Result<MapBundle> load_map_bundle(const AssetLibrary& library, std::string_view map_name) noexcept;

// "ground/sf_m_satellite/sf_m_satellite.map" -> "ground/sf_m_satellite/sf_m_satellite"
[[nodiscard]] std::string_view map_stem(std::string_view map_key) noexcept;

// "ground/sf_m_satellite/sf_m_satellite.map" -> "sf_m_satellite"
[[nodiscard]] std::string_view map_basename(std::string_view map_key) noexcept;

// Where a sector's lightmap is looked for (see the file comment).
[[nodiscard]] Result<AssetLocation> resolve_lightmap(const AssetLibrary& library, std::string_view map_key,
                                                     std::string_view lightmap_name) noexcept;

}  // namespace sf1::data
