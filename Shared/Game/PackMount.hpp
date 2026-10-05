// A server's packs, checked as a set and laid over the client's data for one session
// (Docs/UniversalServerDeploy.md §11.9, §11.10). The same code runs three times (PK-4): lsfpack when
// it builds a pack, the server when it loads its packs, the game when it mounts what a server sent
// -- the game never takes the server's word for any of it.
//
// A pack is never read as a Soldier Front archive (§11.3). To mount a set, its files are written
// out as archives of our own making in a folder of the session's -- <dir>/<library>/x<n>.sff, each
// file under its own key, x/<pack>/... -- and that folder becomes the data layer's session
// (sf::Data::set_session): it follows every archive of the client's, so nothing of the client's
// moves, and nothing of the client's is looked up in it (NM-3, sf::PackScope).
#pragma once

#include "Game/Pack.hpp"
#include "SF/Data.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsf::pack {

// One pack as it is on disk: its table, its identity, its whole file.
struct Loaded {
    Pack pack;
    std::string sha256;
    std::vector<u8> bytes;
};

// The library of the data layer a pack's library is.
sf::Pack data_pack(Library l);

// The client's own file names, counted once (NM-2). A pack's files, were any mounted, are not counted.
class BaseIndex {
public:
    void build(const sf::Data& data);
    bool built() const { return built_; }
    BaseNames names(Library l, std::string_view leaf) const;
    BaseLookup lookup() const {
        return [this](Library l, std::string_view leaf) { return names(l, leaf); };
    }

private:
    std::array<std::unordered_map<std::string, u32>, size_t(Library::Count)> same_, stem_;
    bool built_ = false;
};

// One pack's file: its table, every file's SHA-256, every file's kind (check_files).
bool load_bytes(std::vector<u8> bytes, Loaded& out, std::string* why);
bool load_file(const std::filesystem::path& file, Loaded& out, std::string* why);

// The set: no two packs of one id (NM-9), 50 MB together (DL-6), each one's definitions within
// §11.5's bounds and its names free (NM-2). `base` may be empty (a server with no data folder).
bool check_set(const std::vector<Loaded>& packs, const BaseLookup& base, std::string* why);

// MT-1: the set's files as archives under `dir` (made; whatever was there goes first).
bool write_layer(const std::filesystem::path& dir, const std::vector<Loaded>& packs, std::string* why);
void remove_layer(const std::filesystem::path& dir);

// What a character's definition says of its model, measured from the model itself.
struct ForceMeasure {
    u32 bones = 0;
    float height = 0, width = 0;       // centimetres, as it was modelled (arms out)
    std::vector<std::string> clips;    // the leg clips its skeleton plays
};
bool measure_force(const sf::Data& data, std::string_view folder, ForceMeasure& out, std::string* why);

// With the layer mounted: every gun, character and map the session's registry names is there and
// reads, and every character is what its definition says (128 bones at most, PK-6, its clips).
bool check_mounted(const sf::Data& data, const SessionContent& content, std::string* why);

// The game's side of a join (PK-3, PK-4, MT-1), whole: the cached packs read and checked as if
// nobody had checked them before; the manifest made here from the packs themselves held against
// the one the server sent (its numbers and definitions are never taken on its word); their files
// laid out under `layer` and mounted on `data`; and what the definitions say of the models
// measured. `out` is the session's content. On false, `why` says what was wrong and nothing is
// left mounted. `data` has no session mounted when this is called, and nothing else is reading it.
bool mount_session(const std::vector<std::filesystem::path>& files, const eng::json::Value& manifest, sf::Data& data, const BaseIndex& base,
                   const std::filesystem::path& layer, u32 max_texture, SessionContent& out, std::string* why);

// Whether two JSON values say the same (numbers by value: a manifest made here against one sent).
bool same_json(const eng::json::Value& a, const eng::json::Value& b);

}  // namespace lsf::pack
