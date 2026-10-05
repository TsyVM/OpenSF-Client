// SPDX-License-Identifier: MIT
// sf1/data/worldscript.hpp — a map's gameplay layer ("ground/<map>/<map>.xml").
//
// This is the part of a map that is neither geometry nor art: where players and
// bombs start, where the hostages stand, which sector is called "Bank", where the
// spectator cameras sit. It is plain UTF-8 XML rooted at <SFWorldScript>, shipped
// from area_041.sff onward — 43 files covering all 40 maps the client installs.
//
//   <ObjectiveType>Destroy</ObjectiveType>      Destroy Escape Sniper Takeback
//   <WorldFileName>SF_M_Crossroad.wld</...>     Dual Deathmatch occupy PirateShip
//   <LoadImage>SF_L_Crossroad.jpg</LoadImage>
//   <SpawnPersonal> <SpawnData SectorName X Y Z Angle/> ... </SpawnPersonal>
//   <SpawnTeam_Red> ... </SpawnTeam_Red>   <SpawnTeam_Blue> ... </SpawnTeam_Blue>
//   <NpcSpawn/> <AmmoboxSpawn/>  <SpawnZM2_Zombie/> <SpawnZM2_Human/>  <SpawnTarget/>
//   <EvacuationInfo> <EvacuationData Team PosX PosY PosZ Radius/> ...
//   <MissionObjectInfo> <MissionObjectData Type FileName SectorName Pos* Angle*/> ...
//   <PPLObjectInfo_N>  <PPLObjectData_M FileName SectorName Pos* Angle*/> ...
//   <SoundInfo>        <SoundData FileName SectorName Volume ... MinDist MaxDist/> ...
//   <SoundEffectInfo>  <SoundEffectData SectorName Code Volume/> ...
//   <SectorInfo>       <SectorInfoData SectorName EngInfo LocalizeInfo/> ...
//   <ClanMark_Red>     <ClanMarkData_Red SectorName X Y Z/> ...        (quads, 4 per mark)
//   <ClanMark_Blue>    <ClanMarkData_Blue .../>     <WaterMark/>
//   <CCTV>             <CCTVTopView Pos* AngleY AngleZ/> <CCTVData .../> ...
//
// Element and attribute names are taken from the shipped files. What the engine
// does with Code, LoopType, Third or the CCTV angles is not verified here: this
// reader reports the document, and the editor never writes one back.
#pragma once

#include "../result.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sf1::data::script {

struct Spawn {
    std::string          sector;
    std::array<float, 3> position{};
    float                angle = 0;
};

struct MissionObject {
    std::string          type;        // "Bomb"
    std::string          file;        // "Bomb_01.osf"
    std::string          sector;
    std::array<float, 3> position{};
    std::array<float, 3> angles{};    // degrees, x/y/z as written
};

// A hostage-style prop. The file's grouping (<PPLObjectInfo_2> holding
// <PPLObjectData_1>) is flattened; `group` keeps the number from the parent tag.
struct PplObject {
    int                  group = 0;
    std::string          file;
    std::string          sector;
    std::array<float, 3> position{};
    std::array<float, 3> angles{};
};

struct SoundEmitter {
    std::string          file;
    std::string          sector;
    std::array<float, 3> position{};
    float                volume = 0;
    float                effect_volume = 0;
    float                min_distance = 0;
    float                max_distance = 0;
    int                  loop_type = 0;
    int                  delay_time = 0;
    int                  third = 0;
};

struct SectorEffect {
    std::string sector;
    float       code = 0;      // an index into something in the engine; unverified
    float       volume = 0;
};

struct SectorLabel {
    std::string sector;
    std::string english;       // EngInfo,      e.g. "Fire House"
    std::string localized;     // LocalizeInfo, e.g. "Fire station"
};

struct ClanMark {
    std::string          sector;
    std::array<float, 3> position{};
};

struct Camera {
    std::array<float, 3> position{};
    float                angle_y = 0;
    float                angle_z = 0;
    bool                 top_view = false;
};

// <EvacuationInfo><EvacuationData Team="RED" PosX PosY PosZ Radius/>: a side's ground to reach
// (an escape's way out, where a taken item is brought home; a dual map has one for each side).
struct Evacuation {
    std::string          team;        // "RED", "BLUE"
    std::array<float, 3> position{};
    float                radius = 0;
};

// <WaterMark><WaterMarkPos Location="center_location" X Y Z Radius .../>: a named round place
// (the Pirate Ship's three strongholds).
struct Location {
    std::string          name;
    std::array<float, 3> position{};
    float                radius = 0;
};

// <SpawnTarget><SpawnData X1 Y1 Z1 X2 Y2 Z2 Angle/>: a training target, standing at the first
// point or walking between the two.
struct Target {
    std::array<float, 3> a{}, b{};
    float                angle = 0;
};

struct WorldScript {
    std::string               objective;      // as written: "Destroy", "occupy", ...
    std::string               world_file;
    std::string               load_image;

    std::vector<Spawn>        spawn_personal;
    std::vector<Spawn>        spawn_red;
    std::vector<Spawn>        spawn_blue;
    std::vector<Spawn>        npc_spawn;       // <SpawnData> or <NpcSpawnPos> rows
    std::vector<Spawn>        ammobox_spawn;   // <SpawnData> or <AmmoboxSpawnPos> rows
    std::vector<Spawn>        zm2_zombie;      // <SpawnZM2_Zombie>: Horror Mode 2's undead
    std::vector<Spawn>        zm2_human;       // <SpawnZM2_Human>
    std::vector<Evacuation>   evacuations;
    std::vector<Location>     locations;
    std::vector<Target>       targets;

    std::vector<MissionObject> mission_objects;
    std::vector<PplObject>     ppl_objects;
    std::vector<SoundEmitter>  sounds;
    std::vector<SectorEffect>  sector_effects;
    std::vector<SectorLabel>   sector_labels;
    std::vector<ClanMark>      clan_marks_red;
    std::vector<ClanMark>      clan_marks_blue;
    std::vector<ClanMark>      water_marks;
    std::vector<Camera>        cameras;

    std::uint64_t              size = 0;

    // The label the file gives a sector, or an empty view when it names none.
    [[nodiscard]] std::string_view label_for(std::string_view sector) const noexcept;
};

[[nodiscard]] Result<WorldScript> read(std::string_view text) noexcept;
[[nodiscard]] Result<WorldScript> read(std::span<const std::byte> bytes) noexcept;

}  // namespace sf1::data::script
