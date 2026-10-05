// The Soldier Front client's data folder, read in place: every archive of each folder
// (data/area, data/weapon, data/force, data/lobby, data/menu, ...) searched as one library.
//
// A folder's archives are patch layers: area_047.sff re-ships what it replaces from
// area_001.sff, lobbydata103.mrg from lobbydata1.mrg. So the library puts the highest archive
// NUMBER first (numerically: "lobbydata103" beats "lobbydata99", which a name sort gets wrong)
// and the first archive holding a key wins. The map editor's Build Patch writes a higher
// area_NNN.sff, which is why its patches show up here without anything else knowing.
//
// Keys are the archives' own names normalised: backslashes to '/', ASCII lower case
// ("Ground\SF_M_Bridge\sf_m_bridge.map" -> "ground/sf_m_bridge/sf_m_bridge.map").
// Libraries open on first use and are safe to read from several threads.
//
// The game's own content ships the same way, in a data folder of its own beside the exe
// ; add_layer()
// puts its archives in each library after the client's, so the same keys and loaders reach both.
#pragma once

#include "Engine/Core/Types.hpp"

#include "sf1/data/asset_library.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf {

using eng::u16;
using eng::u32;
using eng::u64;
using eng::u8;
using sf1::data::AssetLibrary;
using sf1::data::AssetLocation;

// Soldier Front measures in half centimetres; the rewrite works in centimetres.
inline constexpr float kSfToCm = 0.5f;

enum class Pack : u8 { Area, Weapon, Force, Lobby, Menu, Effect, Sound, Clan, Scr, Count };
const char* pack_folder(Pack pack);   // "area", "weapon", ...

// A server's pack lives under "x/<pack>/" in each library (Docs/UniversalServerDeploy.md NM-1). A
// name the client's own content looks up never lands there (NM-3): a lookup finds a pack's file
// only by its whole key, or while this thread reads that pack's own content -- a PackScope says so.
std::string pack_prefix(std::string_view key);   // "x/<pack>/" of a key under one, else empty
class PackScope {
public:
    explicit PackScope(std::string_view key);   // the pack `key` lies in; any other key: none
    ~PackScope();
    PackScope(const PackScope&) = delete;
    PackScope& operator=(const PackScope&) = delete;
    static const std::string& prefix();         // this thread's: "x/<pack>/", or empty

private:
    std::string before_;
};

class Data {
public:
    // Whether `dir` is a client data folder (it holds area/ and lobby/ archives).
    static bool is_client_data(const std::filesystem::path& dir);

    // `client_data` is the client's data/ folder. Fails when it is not one.
    bool open(const std::filesystem::path& client_data);
    // Another SF-shaped data folder (area/, weapon/, sound/, ... any of them may be missing) whose
    // archives follow the client's in every library. Call before the first read.
    void add_layer(const std::filesystem::path& data);
    // The session's own layer: a server's packs, written out as archives for as long as that server
    // is joined (Game/PackMount.hpp; Docs/UniversalServerDeploy.md MT-1). Its archives follow every
    // other in each library, so no location of the client's own data moves. Empty: none. Safe while
    // another thread reads: a reader keeps the library it began with, and every library opens again
    // on its next use.
    void set_session(const std::filesystem::path& data);
    const std::filesystem::path& session() const { return session_; }
    bool is_open() const { return !root_.empty(); }
    const std::filesystem::path& root() const { return root_; }

    // The folder's library, opened now if it was not yet; null when the folder has no archives.
    std::shared_ptr<const AssetLibrary> library(Pack pack) const;

    // Exact key, else the one key ending in "/<key>".
    std::optional<AssetLocation> resolve(Pack pack, std::string_view key) const;
    // resolve(), then the same ignoring the extension (tables that name .jpg for a shipped .dds).
    std::optional<AssetLocation> resolve_any_extension(Pack pack, std::string_view key) const;
    // Keys ending in "/<leaf>", preferring one under `prefer_prefix`.
    std::optional<AssetLocation> find_leaf(Pack pack, std::string_view leaf, std::string_view prefer_prefix = {}) const;
    // Every key starting with `prefix` and ending with `suffix`, in key order.
    std::vector<std::pair<std::string, AssetLocation>> keys(Pack pack, std::string_view prefix, std::string_view suffix = {}) const;

    std::optional<std::vector<std::byte>> read(Pack pack, AssetLocation where) const;
    std::optional<std::vector<std::byte>> read(Pack pack, std::string_view key) const;   // resolve()d
    std::string key(Pack pack, AssetLocation where) const;
    // Which archive holds an entry (for the map loader's "texture from the map's own patch" rule).
    std::filesystem::path archive_path(Pack pack, AssetLocation where) const;
    // Whether an entry is the session's (a pack's file), and whether a lookup that asked for
    // `asked` may have it (NM-3).
    bool in_session(Pack pack, AssetLocation where) const;
    bool allowed(Pack pack, std::string_view asked, AssetLocation where) const;

private:
    std::filesystem::path root_;
    std::vector<std::filesystem::path> layers_;
    std::filesystem::path session_;
    mutable std::mutex mutex_;
    mutable std::array<std::shared_ptr<const AssetLibrary>, size_t(Pack::Count)> libs_;
    mutable std::array<bool, size_t(Pack::Count)> tried_{};
    mutable std::array<u32, size_t(Pack::Count)> session_first_{};   // the first archive that is the session's
};

// ── Small helpers every loader uses ────────────────────────────────────────────
std::string lower(std::string_view s);
std::string parent_key(std::string_view key);   // "a/b/c.jpg" -> "a/b"
std::string leaf_of(std::string_view key);      // "a/b/c.jpg" -> "c.jpg"
std::string stem_of(std::string_view key);      // "a/b/c.jpg" -> "c"
// The number in an archive's file name ("area_047.sff" -> 47, "lobbydata103.mrg" -> 103).
int archive_number(const std::filesystem::path& file);
// Text in the client's own encoding (Korean code page 949) as UTF-8.
std::string cp949_to_utf8(std::span<const std::byte> bytes);
std::string cp949_to_utf8(std::string_view text);

}  // namespace sf
