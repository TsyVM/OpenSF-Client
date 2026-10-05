// A server's pack (Docs/UniversalServerDeploy.md §11): one file of data -- models, textures, sounds,
// maps -- and the definitions of what it adds. Our own container, never a Soldier Front archive, so
// it can never replace base content by its number (§11.3, PK-2).
//
//   "LSFPACK1"                       magic
//   u32 format                       kPackFormat
//   u32 header_bytes                 everything up to the first file's data
//   str id                           NM-9: [a-z0-9_], 3..24
//   u32 version
//   u32 + bytes definitions          JSON (what it adds: §11.5)
//   u32 entries
//   entries x { u8 library, str key, u64 offset, u32 size, 32-byte SHA-256 }
//   ...the files, each where its entry says
//
// Strings are u16-length-prefixed UTF-8. The whole file's SHA-256 is the pack's identity (PK-5).
// Every count, offset and size is checked against what is there before anything is read (SC-1,
// SC-2); keys are never paths on disk (SC-3). The same checks run three times (PK-4): lsfpack when
// it builds, the server when it loads, the game when it mounts -- the game never trusts the server's.
#pragma once

#include "Engine/Core/Crypto.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Types.hpp"
#include "Game/Registry.hpp"

#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lsf::pack {

inline constexpr char kMagic[8] = {'L', 'S', 'F', 'P', 'A', 'C', 'K', '1'};
inline constexpr u64 kMaxPackBytes = 50ull << 20;      // DL-6: a server's packs together, 50 MB
inline constexpr u32 kMaxEntries = 4096;               // SC-2
inline constexpr size_t kMaxKey = 120;                 // every key fits an archive's name slot (127) when mounted
inline constexpr u32 kMaxSoundBytes = 8u << 20;        // SC-2: one sound
inline constexpr size_t kMaxDefinitions = 512 * 1024;
inline constexpr u32 kMaxTexture = 4096, kMaxTextureAndroid = 2048;
inline constexpr u32 kMaxBones = 128;                  // WorldRenderer::kMaxBones
// PK-6: a character stands as tall and as wide (its arms out, as it was modelled) as the base
// forces do, give or take: every soldier is hit by the same boxes.
inline constexpr float kMinForceHeight = 150, kMaxForceHeight = 200, kMinForceWidth = 30, kMaxForceWidth = 220;

// The library a file belongs in: where the game's data layer would look for it.
enum class Library : u8 { Area = 0, Weapon, Force, Sound, Effect, Lobby, Menu, Count };
const char* library_name(Library l);   // "area", "weapon", ...

struct Entry {
    Library library = Library::Area;
    std::string key;          // NM-1: x/<pack id>/... in its library
    u64 offset = 0;
    u32 size = 0;
    eng::crypto::Sha256 sha{};
};

struct Pack {
    u32 format = kPackFormat;
    std::string id;
    u32 version = 1;
    std::string definitions;  // JSON
    std::vector<Entry> entries;
};

bool id_ok(std::string_view id);
// NM-1, SC-3: x/<pack>/<path>: lower case letters, digits and _ . - /, no "..", no "//", nothing
// absolute, no backslash, no drive letter, at most kMaxKey.
bool key_ok(std::string_view key, std::string_view pack_id, std::string* why = nullptr);
std::string_view leaf(std::string_view key);

// Reads a pack's table from its whole file (no file is decoded here). False and `why` on anything
// out of bounds, a file whose SHA-256 is not its entry's (with `check_files`), or a key that breaks
// the rules.
bool read(std::span<const u8> file, Pack& out, std::string* why, bool check_files = true);
// One file's bytes, as its entry says (bounds checked again).
std::span<const u8> file_data(std::span<const u8> file, const Entry& e);
// A pack's whole file from its parts (lsfpack).
std::vector<u8> write(const Pack& p, const std::vector<std::vector<u8>>& files);
std::string sha256_hex(std::span<const u8> bytes);

// How the client's own files stand to a file name (NM-2): how many in that library are called
// exactly that, and how many pictures are but for the extension (a table that names .jpg finds
// whichever of .dds, .tga, .jpg, .png, .bmp is shipped).
struct BaseNames {
    size_t same = 0, stem = 0;
};
bool is_picture_name(std::string_view leaf);
using BaseLookup = std::function<BaseNames(Library, std::string_view leaf)>;

// §11.5's bounds on what a pack adds, and the names it may use. A name the client's own content
// finds by leaf (one such file; for a picture, one of that name under any extension) is never a
// pack's too; a name the client has not at all is one pack's file only (`taken`: the names the
// session's packs before this one took, added to); a name the client has several of (every map's
// "sector_01lightingmap") is found by its folder and free. With no `base` (a server with no data folder) only what needs none is checked. False and
// `why` on the first broken rule.
bool validate(const Pack& p, const eng::json::Value& defs, const BaseLookup& base, std::set<std::string>& taken, std::string* why);
// PK-1, SC-2: every file is of a kind its library holds (data only: no script, shader or program
// has a place), no picture is over `max_texture` a side (a phone's renderer takes less: PF-4), no
// sound over kMaxSoundBytes. `file` is the whole pack.
bool check_files(const Pack& p, std::span<const u8> file, std::string* why, u32 max_texture = kMaxTexture);
// A base weapon's numbers as a pack's definition writes them (lsfpack: a pack weapon starts from
// the gun it is `like`).
eng::json::Value weapon_json(const WeaponDef& w);

// The manifest (§11.6): every pack (id, version, SHA-256, size) and the registry of what they add,
// with this session's numbers (NM-4..NM-8). Made by the server from its packs, read by the game.
eng::json::Value manifest(const std::vector<std::pair<Pack, std::string>>& packs_and_sha, const std::vector<u32>& sizes);
// The session's content from a manifest (Game/Registry.hpp): definitions bounded again (PK-4).
bool registry_from_manifest(const eng::json::Value& manifest, SessionContent& out, std::string* why);

}  // namespace lsf::pack
