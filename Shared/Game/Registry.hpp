// This session's own content: the weapons, characters, parts and maps a server's packs add
// (Docs/UniversalServerDeploy.md §11), numbered by the server's manifest from kFirstPackWeapon,
// kFirstPackForce and kFirstPackItem up. Base content is never here: it keeps its fixed numbers in
// Rules.hpp and Items.hpp, and weapon(), force() and item() find both.
//
// The registry is rebuilt per session (MT-4): set once a server's packs are mounted, cleared when
// the server is left; on a dedicated server, set at start and never changed while it runs (PK-11).
// It changes only at a safe point (MT-2), with nothing else reading, so lookups take no lock.
#pragma once

#include "Game/Items.hpp"
#include "Game/Rules.hpp"

#include <span>
#include <string>
#include <vector>

namespace lsf {

// The pack format's version (separate from the protocol's and the build's: DS-5).
inline constexpr u32 kPackFormat = 1;

// A character a pack adds: the force's numbers, and its model and motions in the pack's own keys.
struct PackForce {
    ForceDef def{};             // def.id from kFirstPackForce; def.model points into `model`
    std::string pack;           // the pack's id
    std::string code;           // x:<pack>:<name> (NM-7)
    std::string model;          // the pack's model folder for it (x/<pack>/<folder> in the force library)
    std::string name, nation;
    std::string art;            // the base force model its HUD art and voice come from (ForceDef::art)
};

// A part a pack adds (for one of its own characters, or a new one for a base character: PK-6a/b).
struct PackItem {
    ItemDef def{};              // def.id from kFirstPackItem; its strings point into the fields below
    std::string pack, code, name, info, tab, model, picture;
};

// A map a pack adds.
struct PackMap {
    std::string id;             // x-<pack>-<map>, at most 32 characters (NM-8)
    std::string pack;
    std::string title;
    std::string folder;         // x/<pack>/ground/sf_m_<map> in the area library
    bool night = false;
};

struct SessionContent {
    std::vector<WeaponDef> weapons;   // ids from kFirstPackWeapon; codes x:<pack>:<name>
    std::vector<PackForce> forces;
    std::vector<PackItem> items;
    std::vector<PackMap> maps;
    std::string manifest_hash;        // hex SHA-256 of the manifest these came from
};

namespace registry {
// Replaces this session's content (nothing may be reading it).
void set(SessionContent content);
void clear();
const SessionContent& content();
bool empty();
const PackForce* force(u8 id);
const PackItem* item(u16 id);
const PackMap* map(std::string_view id);
// By the saved code (x:<pack>:<name>), null when this session has no such thing.
const WeaponDef* weapon_by_code(std::string_view code);
const PackForce* force_by_code(std::string_view code);
const PackItem* item_by_code(std::string_view code);
}  // namespace registry

// Everything this session has, for a list to show: the base roster, then the joined server's own.
// The pointers hold until the registry next changes; nothing keeps them across a disconnect (MT-4).
std::vector<const WeaponDef*> session_weapons();
std::vector<const ForceDef*> session_forces();
std::vector<const ItemDef*> session_items();

// Whether a code names pack content ("x:<pack>:<name>") rather than base content.
inline bool is_pack_code(std::string_view code) { return code.size() > 2 && code[0] == 'x' && code[1] == ':'; }

}  // namespace lsf
