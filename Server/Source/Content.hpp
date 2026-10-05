// A server's packs (Docs/UniversalServerDeploy.md §11): the built packs in packs/ (lsfpack's), checked
// when the server loads them (PK-4), the manifest made from them (§11.6), and the session's registry
// of what they add (Game/Registry.hpp). Packs and manifest never change while the server runs (PK-11).
#pragma once

#include "ServerOnly.hpp"

#include "Game/Pack.hpp"
#include "Game/PackMount.hpp"
#include "Game/Registry.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace lsfs {

struct ServerPack {
    std::string id;              // the pack's own id (NM-9)
    std::string sha256;          // its identity (PK-5)
    lsf::u32 size = 0;
    std::filesystem::path file;
    std::vector<lsf::u8> bytes;  // the whole file, as it is sent (PK-11: it never changes while the server runs)
};

class MapCache;

// Tests only (Tools/packcheck's hostile server, SC-5): what a server that means harm might do. The
// game must refuse each, cleanly, with a reason.
enum Evil : lsf::u32 {
    kEvilUnchecked = 1,        // serves whatever is in packs/, read or not, checked by nobody
    kEvilManifestHuge = 2,     // a manifest in more pieces than one may come in
    kEvilManifestOther = 4,    // a manifest that is not the one it announced
    kEvilCorruptChunks = 8,    // every piece of a pack with a byte wrong
    kEvilStall = 16,           // a download that never sends anything
    kEvilOversize = 32,        // packs said to be over 50 MB
    kEvilRegistryLies = 64,    // a manifest whose numbers are not the packs' own
};

class ServerContent {
public:
    ~ServerContent();
    // Every packs/*.lsfpack: read, checked as a set (lsf::pack::check_set), their definitions
    // numbered into this session's registry, their files laid over `maps`' data so the maps they
    // add are read like any other. False and `why` on the first that fails (DS-1: the server
    // refuses to start rather than serve a broken pack). An empty or missing folder: no packs, no
    // manifest.
    bool load(const std::filesystem::path& dir, MapCache& maps, std::string* why, lsf::u32 evil = 0);
    // Takes the packs' files off `maps`' data again and forgets what they added (the server stopping).
    void unload(MapCache& maps);
    const std::string& manifest_hash() const { return manifest_hash_; }
    const std::vector<lsf::u8>& manifest() const { return manifest_; }
    const std::vector<ServerPack>& packs() const { return packs_; }
    lsf::u64 total_bytes() const;
    const ServerPack* pack(std::string_view sha256) const;
    // The packs as the heartbeat names them ("id:sha256"), for TVAS's takedowns (RT-3, MN-7).
    std::vector<std::string> heartbeat_list() const;

private:
    std::vector<ServerPack> packs_;
    std::vector<lsf::u8> manifest_;
    std::string manifest_hash_;
    std::filesystem::path layer_;   // where the packs' files lie as archives, while the server runs
    bool registry_set_ = false;     // the session's registry is ours (a server with no packs leaves it alone)
};

}  // namespace lsfs
