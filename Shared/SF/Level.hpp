// A Soldier Front map, loaded straight from the client's area archives into memory: the
// lightmapped level geometry, the placed props, the collision, the gameplay layer (spawns,
// bomb sites and other mission objects) and the environment (sky, sun, loading picture).
//
// Everything is in the rewrite's units: centimetres (Soldier Front measures in half
// centimetres), left-handed, Y up — the same space Soldier Front uses, scaled by 0.5.
//
// Sources, per map key "ground/sf_m_x/sf_m_x.map" (stem = key without ".map"):
//   geometry     the .map: sectors, 40-byte vertices, index groups per material, a lightmap each
//   materials    <stem>.msf (else <basename>.msf): texture paths, resolved by exact name, else by
//                stem in the archives that shipped the table (CrossRoads names .jpg, ships .dds)
//   lightmaps    per sector, next to the map (SFSDK resolve_lightmap)
//   collision    <stem>_c.cft plus every blocking prop's hull
//   props        world/<basename>.wld object records (pos, scale, euler, 4x4, bbox, name index),
//                else <stem>_obj.env; a prop is object/**/<name>.val (+ _c.val hull, .osf setup)
//   gameplay     <stem>.xml: SpawnTeam_Red/Blue, SpawnPersonal, MissionObjectInfo, LoadImage
//   sky          the .wld's six faces (right, left, top, bottom, back, front)
#pragma once

#include "SF/Data.hpp"

#include "Engine/Core/Math.hpp"
#include "Engine/Physics/CollisionMesh.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

using eng::Aabb;
using eng::Vec3;
using eng::i32;

enum class Surface : u8 { Concrete, Metal, Wood, Dirt, Grass, Sand, Water, Glass, Tile, Snow, Ladder, Count };
const char* surface_name(Surface s);

// What a surface sounds like: SFSound.xml's MaterialList ids, which the map's texture table (.msf)
// writes after each texture ("stground01.JPG  8" is concrete). SoundTables names them.
enum SoundMaterial : u8 {
    kSoundSoil = 0, kSoundMetal = 1, kSoundGrass = 2, kSoundWater = 3, kSoundRock = 4, kSoundWood = 5, kSoundSnow = 6,
    kSoundMud = 7, kSoundConcrete = 8, kSoundGlass = 9, kSoundFabric = 11, kSoundSand = 12,
};
// The nearest sound material for a surface guessed from names (props without a table entry).
u8 sound_material_of(Surface s);

struct LevelVertex {
    float position[3];
    float normal[3];
    float uv0[2];   // the material's texture
    float uv1[2];   // the sector's lightmap (0,0 on props)
};
static_assert(sizeof(LevelVertex) == 40);

struct LevelMaterial {
    std::string texture;          // area archive key; empty when the client ships no such texture
    std::optional<AssetLocation> where;
    Surface surface = Surface::Concrete;
    u8 sound = kSoundConcrete;    // SoundMaterial: footsteps, bullet impacts
    bool prop = false;            // a placed prop's skin (lit by the sun, not a lightmap)
    // A prop whose .osf says [TransParent] 1 and [ZWriteEnable] 0 is a glow (the tunnel's light
    // cones, plasma fire, neon signs): added onto what is behind it rather than painted over it,
    // at the strength of its mesh's vertex colour (glowlight_01 is white at 20 of 255), lit by
    // nothing when [LightEnable] is 0.
    bool glow = false;
    bool unlit = false;
    u32 colour = 0xFFFFFFFF;      // the mesh's vertex colour, ARGB
    // A prop with more than one picture (a string of lights that blinks: 2; a signboard: 4 or 5;
    // fire: 12; smoke: 16) shows each for `frame_seconds` in turn, over and over. `frames` is all
    // of them, `texture` the first; empty for the one-picture prop nearly all are.
    struct Frame {
        std::string texture;
        std::optional<AssetLocation> where;
    };
    std::vector<Frame> frames;
    float frame_seconds = 0;
};

// A prop whose shape moves: a flag in the wind (30 shapes, 33 ms each), the Pirate Ship's pennant
// (60, 50 ms), Nerve Gas Horror's hanging thing (31, 80 ms). Each shape is the whole mesh again
// (their vertices need not even be as many), here as triangles ready to draw, in the level's space.
struct LevelFlag {
    u32 material = 0;             // Level::materials
    float shape_seconds = 0;
    std::vector<std::vector<LevelVertex>> shapes;
    Aabb bounds;
};

// A prop that turns to whoever looks at it: one flat card with a picture ([Object_Type]
// billboard: a fire, facing the eye outright; billboard_fix_y: chimney smoke, a plant, standing
// upright and turning about its foot only). Not in the level's vertices: drawn afresh each frame.
struct LevelCard {
    Vec3 at;                      // its middle; an upright one's: the middle of its foot. Centimetres
    float width = 0, height = 0;
    bool upright = false;
    u32 material = 0;             // Level::materials
    float uv[4] = {0, 0, 1, 1};   // left, top, right, bottom
};

struct LevelBatch {
    u32 material = 0;
    i32 lightmap = -1;            // index into Level::lightmaps, -1 for props
    u32 first_index = 0;
    u32 index_count = 0;
    Aabb bounds;
};

enum class Team : u8 { Red = 0, Blue = 1, Any = 2 };

struct LevelSpawn {
    Team team = Team::Any;
    Vec3 position;                // feet, centimetres
    float yaw = 0;                // degrees, engine convention (0 faces +Z, grows to the right)
    std::string sector;
};

struct LevelObjective {
    std::string type;             // "Bomb", "Take", "Escape", ... as the world script writes it
    std::string file;             // the prop that marks it ("Bomb_01.osf")
    Vec3 position;
    Vec3 angles;                  // degrees as written
    std::string sector;
};

// A round piece of ground a side is sent to (EvacuationData): an escape's way out, where a taken
// item is brought home (each side has one on a dual map), the silo's missile consoles.
struct LevelZone {
    Team team = Team::Red;
    Vec3 position;                // centimetres
    float radius = 0;             // centimetres
};

// A named round place (WaterMarkPos): the Pirate Ship's strongholds, "red_location",
// "center_location", "blue_location".
struct LevelPlace {
    std::string name;
    Vec3 position;
    float radius = 0;
};

// A training target (SpawnTarget): standing at `a`, or walking between `a` and `b`.
struct LevelTarget {
    Vec3 a, b;
    float yaw = 0;
};

struct LevelSound {
    std::string file;
    Vec3 position;
    float volume = 1, min_distance = 0, max_distance = 0;
    bool loop = false;
};

struct Level {
    std::string key;              // "ground/sf_m_crossroad/sf_m_crossroad.map"
    std::string id;               // "crossroad"
    std::string title;            // "Crossroad"
    std::string objective;        // world script ObjectiveType: Destroy, Escape, Takeback, Dual, ...

    // Drawing.
    std::vector<LevelVertex> vertices;
    std::vector<u32> indices;
    std::vector<LevelBatch> batches;
    std::vector<LevelMaterial> materials;
    std::vector<LevelCard> cards;
    std::vector<LevelFlag> flags;
    std::vector<std::string> lightmaps;                 // area archive keys
    std::vector<std::optional<AssetLocation>> lightmap_where;
    std::array<std::string, 6> sky;                     // right left top bottom back front; empty = none
    std::string load_image;                             // menu archive picture ("SF_L_Crossroad.jpg")
    Vec3 sun_direction{-0.35f, -0.85f, 0.4f};           // the way the light travels
    Vec3 sun_colour{0.75f, 0.72f, 0.66f};
    Vec3 ambient{0.55f, 0.55f, 0.58f};

    // Collision: triangles, a surface each.
    std::vector<Vec3> collision_vertices;
    std::vector<u32> collision_indices;
    std::vector<Surface> collision_surfaces;
    // What each collision triangle is part of: an index into collision_sources ("map" for the
    // level's own hull, else the prop it belongs to: "kh_plank_01").
    std::vector<u16> collision_source;
    std::vector<std::string> collision_sources{"map"};

    // Gameplay.
    std::vector<LevelSpawn> spawns;
    std::vector<LevelObjective> objectives;
    std::vector<LevelZone> zones;
    std::vector<LevelPlace> places;
    std::vector<LevelTarget> targets;
    std::vector<Vec3> npc_spots;      // NpcSpawnPos: Horror Mode 2's girl, the Pirate Ship's treasure
    std::vector<Vec3> ammo_spots;     // AmmoboxSpawnPos: supply boxes
    std::vector<LevelSpawn> zombie_spawns, human_spawns;   // SpawnZM2_Zombie / SpawnZM2_Human
    std::vector<LevelSound> sounds;
    std::vector<std::pair<std::string, std::string>> sector_names;   // sector -> "Fire House"

    Aabb bounds;
    // Where each glow prop (LevelMaterial::glow) stands: its bounds' centre, in centimetres.
    std::vector<Vec3> glows;
    // The same glows as the lamps they are, for a renderer that lights from them. A headlight's
    // cone or a spot's is a `beam`: it fans out from the bulb at `source` along `direction` for
    // `length` (a glow prop can hold several: a lorry's two headlights); anything else (a tube,
    // a sign, a fire) glows all round from its middle.
    struct Lamp {
        Vec3 source;              // the bulb (a beam's narrow end; else the glow's middle)
        Vec3 direction{0, -1, 0}; // the way a beam shines
        float length = 0;         // a beam's reach; else the glow's longest side
        float width = 0;          // across, at its widest
        bool beam = false;
        u32 colour = 0xFFFFFFFF;  // the glow's own ARGB (its vertex colour)
    };
    std::vector<Lamp> lamps;
    struct Stats {
        size_t sectors = 0, triangles = 0, missing_textures = 0, recovered_textures = 0, missing_lightmaps = 0;
        size_t props = 0, props_without_mesh = 0, prop_colliders = 0;
        size_t props_animated = 0;   // placed props with more than one picture
        // The props that draw nothing, by name: how many of each, and what the archives have of it
        // ("no files", "osf only: <Object_Type>", "val unread").
        std::map<std::string, std::pair<size_t, std::string>> props_missing;
    } stats;
};

enum LevelParts : u32 {
    kLevelGeometry = 1,    // vertices, batches, materials, lightmaps, sky, props' meshes
    kLevelCollision = 2,   // level and prop collision
    kLevelGameplay = 4,    // spawns, objectives, sounds, sector names, load image
    kLevelAll = 7,
};

struct LevelListing {
    std::string id;         // "crossroad"
    std::string key;        // the .map key
    std::string title;      // "Crossroad"
    bool playable = false;  // it has a world script (spawns)
};

// Every map the area archives carry, one per id, preferring the ground/ tree.
std::vector<LevelListing> list_levels(const Data& data);

// `name` is an id ("crossroad"), a basename ("sf_m_crossroad") or a full .map key.
std::optional<Level> load_level(const Data& data, std::string_view name, u32 parts = kLevelAll, std::string* error = nullptr);

// How bright a level's bake is: the mean luma of its lightmaps' lit texels (the black padding
// between charts left out), times the two the game modulates by. What a time-of-day grade
// normalises, so every map's night is as dark as every other's. -1 when there are no lightmaps.
float measure_baked_level(const Data& data, const Level& level);

// The map as drawn (its render geometry, props and all), each triangle carrying its material's
// sound (LevelMaterial::sound in CollisionMesh's surface slot): what a footstep or a bullet meets.
// The collision file has no materials, so this is the mesh those questions are asked of.
void build_sound_mesh(const Level& level, eng::CollisionMesh& out);

// "ground/sf_m_crossroad/sf_m_crossroad.map" -> "crossroad"
std::string level_id(std::string_view map_key);
// World-script Angle (degrees, facing (cos a, 0, -sin a)) into an engine yaw.
float sf_angle_to_yaw(float degrees);

}  // namespace sf
