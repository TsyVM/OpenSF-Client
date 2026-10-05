// SPDX-License-Identifier: MIT
#include "sf1/data/map_bundle.hpp"
#include <algorithm>

namespace sf1::data {

std::string_view map_stem(std::string_view map_key) noexcept {
    if (auto dot = map_key.rfind('.'); dot != std::string_view::npos && map_key.find('/', dot) == std::string_view::npos)
        return map_key.substr(0, dot);
    return map_key;
}

std::string_view map_basename(std::string_view map_key) noexcept {
    auto stem = map_stem(map_key);
    if (auto slash = stem.rfind('/'); slash != std::string_view::npos) stem.remove_prefix(slash + 1);
    return stem;
}

Result<AssetLocation> resolve_lightmap(const AssetLibrary& library, std::string_view map_key,
                                       std::string_view lightmap_name) noexcept {
    auto stem = map_stem(map_key);
    std::string folder;
    if (auto slash = stem.rfind('/'); slash != std::string_view::npos) folder.assign(stem.substr(0, slash + 1));
    // Extension-insensitive: sf_m_crossroad's sector names end ".tga" while the
    // archive ships the same lightmaps as ".dds".
    return library.resolve_any_extension(folder + "level1/" + std::string(lightmap_name));
}

Result<MapBundle> load_map_bundle(const AssetLibrary& library, std::string_view map_name) noexcept {
    const Archive& primary = library.primary();
    const std::string wanted = normalize_entry_name(map_name);

    std::size_t hits = 0;
    std::string key;
    if (wanted.ends_with(".map") && primary.find(wanted)) {
        key = wanted;
        hits = 1;
    } else {
        std::vector<std::string> seen;
        for (std::size_t i = 0; i < primary.size(); ++i) {
            const auto& k = primary.key(i);
            if (!k.ends_with(".map") || k.find(wanted) == std::string::npos) continue;
            if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
            seen.push_back(k);
            key = k;
            ++hits;
        }
    }
    if (hits == 0) return err(Error::EntryNotFound);
    if (hits > 1) return err(Error::AmbiguousEntry);

    MapBundle b;
    b.map_key = key;

    auto map_loc = library.find(key);
    if (!map_loc) return err(map_loc.error());
    auto map_bytes = library.read(*map_loc);
    if (!map_bytes) return err(map_bytes.error());
    auto parsed = map::read(*map_bytes);
    if (!parsed) return err(parsed.error());
    b.map = std::move(*parsed);

    const std::string stem(map_stem(key));
    const std::string base(map_basename(key));

    auto mapping = library.resolve(stem + ".msf");
    if (!mapping) mapping = library.resolve(base + ".msf");
    if (!mapping) return err(Error::EntryNotFound);
    b.mapping_key = library.key(*mapping);
    auto msf_bytes = library.read(*mapping);
    if (!msf_bytes) return err(msf_bytes.error());
    auto materials = msf::read(*msf_bytes);
    if (!materials) return err(materials.error());
    b.materials = std::move(*materials);

    for (const auto& s : b.map.sectors)
        for (const auto& g : s.groups)
            if (g.material != map::kNoMaterial && g.material >= b.materials.size())
                return err(Error::IndexOutOfRange);

    if (auto cft_loc = library.find(stem + "_c.cft")) {
        auto bytes = library.read(*cft_loc);
        if (!bytes) return err(bytes.error());
        auto collision = cft::read(*bytes);
        if (!collision) return err(collision.error());
        b.collision_key = library.key(*cft_loc);
        b.collision = std::move(*collision);
    }

    if (auto world_loc = library.resolve("world/" + base + ".wld")) {
        b.world_key = library.key(*world_loc);
        auto bytes = library.read(*world_loc);
        if (!bytes) {
            b.world_error = bytes.error();
        } else if (auto world = wld::read(*bytes)) {
            b.world = std::move(*world);
            b.world_bytes = std::move(*bytes);
        } else {
            b.world_error = world.error();
        }
    }

    // The props standing in the map. Only 28 of the client's maps ship one, and a
    // map without it is simply a map with no placed objects.
    auto objects_loc = library.resolve(stem + "_obj.env");
    if (!objects_loc) objects_loc = library.resolve(base + "_obj.env");
    if (objects_loc) {
        b.objects_key = library.key(*objects_loc);
        auto bytes = library.read(*objects_loc);
        if (!bytes) {
            b.objects_error = bytes.error();
        } else if (auto objects = env::read(*bytes)) {
            b.objects = std::move(*objects);
        } else {
            b.objects_error = objects.error();
        }
    }

    // Spawns, bomb sites, sounds, sector names and cameras.
    auto script_loc = library.resolve(stem + ".xml");
    if (!script_loc) script_loc = library.resolve(base + ".xml");
    if (script_loc) {
        b.script_key = library.key(*script_loc);
        auto bytes = library.read(*script_loc);
        if (!bytes) {
            b.script_error = bytes.error();
        } else if (auto parsed_script = script::read(*bytes)) {
            b.script = std::move(*parsed_script);
            b.script_bytes = std::move(*bytes);
        } else {
            b.script_error = parsed_script.error();
        }
    }

    return b;
}

}  // namespace sf1::data
