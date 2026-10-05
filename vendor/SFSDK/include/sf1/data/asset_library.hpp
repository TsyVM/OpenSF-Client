// SPDX-License-Identifier: MIT
// sf1/data/asset_library.hpp — several archives searched as one, and map discovery.
//
// Maps in data/area reference textures and manifests that live in other area
// archives. AssetLibrary searches the archive you chose first, then its sibling
// .sff files in descending file-name order, and the first archive holding a key
// wins.
//
// That order is a PREVIEW LOOKUP POLICY carried over from the Python reference
// editor, where it resolves every texture the installed client ships. It is not a
// verified description of the order in which the game client mounts archives.
#pragma once

#include "archive.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sf1::data {

struct AssetLocation {
    std::uint32_t archive = 0;   // index into AssetLibrary::archives()
    std::uint32_t entry = 0;     // index into that archive's entries()
    friend bool operator==(const AssetLocation&, const AssetLocation&) = default;
};

class AssetLibrary {
public:
    // `primary` plus every other *.sff in its folder, in descending file-name order.
    // An archive that fails to open is recorded in skipped() rather than failing
    // the library; only the primary archive is required.
    [[nodiscard]] static Result<AssetLibrary> open(const std::filesystem::path& primary) noexcept;

    // Exactly these archives, first = highest priority.
    [[nodiscard]] static Result<AssetLibrary> open(std::span<const std::filesystem::path> archives) noexcept;

    [[nodiscard]] std::span<const Archive> archives() const noexcept { return archives_; }
    [[nodiscard]] const Archive& primary() const noexcept { return archives_.front(); }
    [[nodiscard]] std::span<const std::pair<std::filesystem::path, Error>> skipped() const noexcept { return skipped_; }

    // Exact key match across all archives (first archive holding the key wins).
    [[nodiscard]] Result<AssetLocation> find(std::string_view name) const noexcept;

    // Exact key match, otherwise the one key ending in "/<key>" across all archives.
    [[nodiscard]] Result<AssetLocation> resolve(std::string_view name) const noexcept;

    // resolve(), then the same lookup again ignoring the file extension.
    //
    // Two shipped texture tables name files the client does not have under that
    // name: sf_m_crossroad.msf asks for 63 ".jpg" of which 53 exist only as
    // ".dds", and sf_m_neoshanghai.msf asks for 61 of which 27 do. The other 68
    // .msf files in the installed client resolve exactly, and no .msf names a
    // texture that is absent under every extension — so the shipped data only
    // makes sense if the consumer matches on the stem. How the game's own loader
    // does it is unverified; this is a preview lookup policy, like the archive
    // search order above.
    [[nodiscard]] Result<AssetLocation> resolve_any_extension(std::string_view name) const noexcept;

    // Every key starting with `prefix` and ending with `suffix` (both compared as
    // normalised keys), in key order, each with the archive that wins it. Ambiguous
    // keys are left out. "sky/" lists the sky sets; ("object/", "/drum_01.osf") finds
    // a prop's setup file without knowing its family folder.
    [[nodiscard]] std::vector<std::pair<std::string, AssetLocation>> keys(std::string_view prefix,
                                                                          std::string_view suffix = {}) const;

    [[nodiscard]] Result<std::vector<std::byte>> read(AssetLocation where) const noexcept;
    [[nodiscard]] const std::string& key(AssetLocation where) const noexcept;
    [[nodiscard]] const sff::Entry& entry(AssetLocation where) const noexcept;

private:
    Result<void> index_all() noexcept;

    std::vector<Archive>                                  archives_;
    std::vector<std::pair<std::filesystem::path, Error>>  skipped_;
    std::unordered_map<std::string, AssetLocation>        merged_;   // entry 0xFFFFFFFF = ambiguous
    std::unordered_map<std::string, AssetLocation>        stems_;    // key minus its extension
};

// The other *.sff files beside `primary`, in descending file-name order.
[[nodiscard]] std::vector<std::filesystem::path> sibling_archives(const std::filesystem::path& primary);

struct MapListing {
    std::filesystem::path archive;   // the .sff holding the map
    std::string           entry;     // the map's key, e.g. "ground/sf_m_satellite/sf_m_satellite.map"
    std::string           label;     // "Satellite"
};

// Every .map entry in every *.sff of `folder`, archives in ascending file-name
// order and entries in key order. Archives that fail to open are skipped.
[[nodiscard]] std::vector<MapListing> discover_maps(const std::filesystem::path& folder);

// "ground/sf_m_night_hawk/sf_m_night_hawk.map" -> "Night Hawk"
[[nodiscard]] std::string map_label(std::string_view entry_key);

}  // namespace sf1::data
