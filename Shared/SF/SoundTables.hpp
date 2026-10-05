// What the client plays, as its own tables say:
//
//   SFSound.xml (sound archives)   named sounds (Walk, Jump, Impact, BodyImpact, Explosion, Kill,
//                                  ...), each with its files by material (Soil, Metal, Grass, ...)
//                                  or by event (HeadShotKill), how far it carries and how loud;
//   Weapon.kst (script archives)   every item's fire, reload (up to five stages), draw and rolling
//                                  sounds with their own distances, by item code and weapon folder;
//
// Files are the sound archives' names ("General\SF_S_D_W.MP3", "SF_S_G_ak74_F.mp3"); weapon
// sounds live under weapon/ in the archives, which is where file() puts them.
#pragma once

#include "SF/Data.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct SoundDef {
    std::string name;
    float min = 0, max = 0;      // metres: full volume inside min, fading out to max
    float volume = 1;
    bool positional = true;      // SoundType 3D
    bool loop = false;           // LoopCount 0
    std::map<std::string, std::vector<std::string>> files;   // by material or event, lower case
};

struct WeaponSound {
    std::string file;            // sound archive key, "weapon/sf_s_g_ak74_f.mp3"
    float min = 0, max = 0, volume = 1;
    bool valid() const { return !file.empty(); }
};

struct WeaponSounds {
    std::string code, name, bhw;
    int number = 0;              // the code's number (A013 -> 13): the base item has the lowest
    WeaponSound fire, draw, rolling;
    std::vector<WeaponSound> reload;   // the stages in order
};

class SoundTables {
public:
    bool load(const Data& data, std::string* error = nullptr);
    bool loaded() const { return !sounds_.empty(); }

    const SoundDef* sound(std::string_view name) const;
    // The files `name` plays on `key` (a material or an event name), else its Default ones.
    const std::vector<std::string>* files(std::string_view name, std::string_view key) const;
    // SFSound.xml's MaterialList name for a sound material id ("Concret" for 8).
    static const char* material_name(u8 id);

    // A weapon's sounds: its item code's row when that row fires from the same weapon folder,
    // else the row of its weapon folder (bhw/sf_a_<model>) with the lowest code, else the same
    // with a variant's prefix taken off (gold_, camo_, ...). Null when nothing matches.
    const WeaponSounds* weapon(std::string_view code, std::string_view model) const;
    // An SF gun of the same name (an admin variant borrowing one): names compared without case,
    // spaces or punctuation.
    const WeaponSounds* weapon_named(std::string_view name) const;
    const std::vector<WeaponSounds>& weapons() const { return weapons_; }

private:
    std::map<std::string, SoundDef> sounds_;          // by lower-case name
    std::vector<WeaponSounds> weapons_;
    std::map<std::string, size_t> by_code_, by_bhw_, by_name_;
};

}  // namespace sf
