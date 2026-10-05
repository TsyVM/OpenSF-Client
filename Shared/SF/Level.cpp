#include "SF/Level.hpp"

#include "SF/Image.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include "sf1/data/cft.hpp"
#include "sf1/data/env.hpp"
#include "sf1/data/map.hpp"
#include "sf1/data/map_bundle.hpp"
#include "sf1/data/msf.hpp"
#include "sf1/data/osf.hpp"
#include "sf1/data/wld.hpp"
#include "sf1/data/worldscript.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace sf {

namespace sfd = sf1::data;
using eng::Mat4;

const char* surface_name(Surface s) {
    switch (s) {
        case Surface::Concrete: return "concrete";
        case Surface::Metal: return "metal";
        case Surface::Wood: return "wood";
        case Surface::Dirt: return "dirt";
        case Surface::Grass: return "grass";
        case Surface::Sand: return "sand";
        case Surface::Water: return "water";
        case Surface::Glass: return "glass";
        case Surface::Tile: return "tile";
        case Surface::Snow: return "snow";
        case Surface::Ladder: return "ladder";
        default: return "?";
    }
}

u8 sound_material_of(Surface s) {
    switch (s) {
        case Surface::Metal: case Surface::Ladder: return kSoundMetal;
        case Surface::Wood: return kSoundWood;
        case Surface::Dirt: return kSoundSoil;
        case Surface::Grass: return kSoundGrass;
        case Surface::Sand: return kSoundSand;
        case Surface::Water: return kSoundWater;
        case Surface::Glass: return kSoundGlass;
        case Surface::Tile: return kSoundRock;
        case Surface::Snow: return kSoundSnow;
        default: return kSoundConcrete;
    }
}

std::string level_id(std::string_view map_key) {
    std::string base = lower(sfd::map_basename(map_key));
    if (base.starts_with("sf_m_")) base.erase(0, 5);
    // A server's own map (x/<pack>/...: Docs/UniversalServerDeploy.md NM-8) is x-<pack>-<map>, so
    // it can never be taken for one of the client's.
    if (map_key.starts_with("x/"))
        if (const size_t slash = map_key.find('/', 2); slash != std::string_view::npos) return "x-" + lower(map_key.substr(2, slash - 2)) + "-" + base;
    return base;
}

float sf_angle_to_yaw(float degrees) {
    const float a = degrees * eng::kDegToRad;
    return eng::forward_to_yaw(Vec3{std::cos(a), 0.0f, -std::sin(a)});
}

namespace {

// ── .val prop meshes ───────────────────────────────────────────────────────────
//
// Checked against every .val the area archives ship: 1,095 of 1,117 are exactly
//
//   u32 version (1, 5 or 6)   u32 type (1)   u32 (1)   u32 0   u32 0   u32 256
//   char name[256]
//   u32 (0..3)   u32 (255)   u32 (1)
//   u32 pictures (1)   u32 milliseconds each picture is shown (0)   u32 picture[pictures] (0, 1, 2 ...)
//   f32 (1, 1, 1)   u32 (1)
//   u32 vertex_count   u32 index_count
//   vertex_count x { f32 position[3]; f32 normal[3]; u32 colour; f32 uv[2] }
//   index_count x u16
//   u32 triangle_count (= index_count / 3)
//
// A few have more than one shape (the third number of the header says how many, the fifth how many
// milliseconds each is shown): the whole of { vertex_count, index_count, vertices, indices,
// triangle_count } again for each, one after another. Two flags (30 shapes, 33 ms), the Pirate
// Ship's pennant (60, 50 ms) and Nerve Gas Horror's hanging thing (31, 80 ms).
//
// Fourteen have more than one picture, shown in turn (the .osf lists them, [Mapping Source Count]
// of them): Snow Camp's strings of lights (2, 700 ms), signboards (4 and 5, 500 ms), fires (12,
// 80 ms) and chimney smoke (16, 120 ms), a CCTV screen (27), water (10, 11). The rest that do not
// fit are flags (their cloth's frames follow the mesh) and particle stubs.
struct ValVertex {
    Vec3 position;
    Vec3 normal;
    float uv[2];
};

struct ValMesh {
    std::vector<ValVertex> vertices;
    std::vector<u16> indices;
    // Each vertex's ARGB colour at +24, one for the whole mesh in every file seen; 0xCDCDCDCD (the
    // exporter's unset memory) on all but the glows.
    u32 colour = 0xFFFFFFFF;
    u32 frames = 1;            // its pictures
    float frame_seconds = 0;   // how long each is shown
    // A flag's further shapes (the mesh above is its first), and how long each is shown.
    struct Shape {
        std::vector<ValVertex> vertices;
        std::vector<u16> indices;
    };
    std::vector<Shape> shapes;
    float shape_seconds = 0;
};

std::optional<ValMesh> read_val(std::span<const std::byte> bytes) {
    const size_t n = bytes.size();
    const auto* p = reinterpret_cast<const u8*>(bytes.data());
    auto u32_at = [&](size_t o) {
        u32 v;
        std::memcpy(&v, p + o, 4);
        return v;
    };
    if (n < 332) return std::nullopt;
    // A prop with more than one picture (a light that blinks: 2; a signboard: 4; fire: 12; smoke:
    // 16; its .osf's Mapping Source Count) numbers them 0, 1, 2 ... at 300, one u32 each, and all
    // that follows sits that much further on. Most props have the one, and its 0.
    size_t shift = 0;
    u32 vc = 0, ic = 0, pictures = 1;
    bool fits = false;
    for (u32 frames = 1; frames <= 64 && !fits; ++frames) {
        shift = size_t(frames - 1) * 4;
        if (332 + shift > n) break;
        if (u32_at(300 + shift) != frames - 1) break;   // the numbers run on from 0: this is not one of them
        vc = u32_at(320 + shift), ic = u32_at(324 + shift);
        fits = vc != 0 && ic != 0 && ic % 3 == 0 && vc <= 65536 && ic <= 3000000 && 332 + shift + size_t(vc) * 36 + size_t(ic) * 2 == n;
        pictures = frames;
    }
    const bool waves = !fits;
    if (!fits) {
        // A flag: its cloth as it hangs, then the shapes of its waving after it.
        shift = 0;
        vc = u32_at(320), ic = u32_at(324);
        if (vc == 0 || ic == 0 || ic % 3 || vc > 65536 || ic > 3000000 || 332 + size_t(vc) * 36 + size_t(ic) * 2 > n) return std::nullopt;
    }
    ValMesh mesh;
    if (fits && pictures > 1) {
        const u32 ms = u32_at(296);
        mesh.frames = pictures;
        mesh.frame_seconds = float(ms >= 10 && ms <= 60000 ? ms : 500) / 1000.0f;
    }
    mesh.vertices.resize(vc);
    for (u32 i = 0; i < vc; ++i) {
        const u8* v = p + 328 + shift + size_t(i) * 36;
        float f[8];
        std::memcpy(f, v, 24);
        std::memcpy(f + 6, v + 28, 8);
        ValVertex& out = mesh.vertices[i];
        out.position = Vec3(f);
        out.normal = Vec3(f + 3);
        out.uv[0] = f[6];
        out.uv[1] = f[7];
        if (!eng::finite(out.position)) return std::nullopt;
        if (!std::isfinite(out.uv[0]) || !std::isfinite(out.uv[1])) out.uv[0] = out.uv[1] = 0;
        u32 c;
        std::memcpy(&c, v + 24, 4);
        if (i == 0 && c != 0xCDCDCDCDu) mesh.colour = c;
    }
    mesh.indices.resize(ic);
    std::memcpy(mesh.indices.data(), p + 328 + shift + size_t(vc) * 36, size_t(ic) * 2);
    for (u16 i : mesh.indices)
        if (i >= vc) return std::nullopt;
    // A few meshes (the Chevy vans, the bicycle, a drum) carry a stray vertex thousands of
    // kilometres out that a triangle or two still reach; drawn, it smears a sliver across the
    // whole level. No prop is anywhere near ten kilometres across, so those triangles go.
    constexpr float kFar = 1.0e6f;
    auto far = [&](u16 i) {
        const Vec3& q = mesh.vertices[i].position;
        return std::fabs(q.x) > kFar || std::fabs(q.y) > kFar || std::fabs(q.z) > kFar;
    };
    std::vector<u16> kept;
    kept.reserve(mesh.indices.size());
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
        if (!far(mesh.indices[t]) && !far(mesh.indices[t + 1]) && !far(mesh.indices[t + 2]))
            kept.insert(kept.end(), mesh.indices.begin() + t, mesh.indices.begin() + t + 3);
    if (kept.empty()) return std::nullopt;
    mesh.indices = std::move(kept);
    if (waves) {
        const u32 count = u32_at(8), ms = u32_at(16);
        size_t at = 328 + size_t(vc) * 36 + size_t(ic) * 2 + 4;
        while (count > 1 && count <= 256 && mesh.shapes.size() + 1 < count && at + 8 <= n) {
            u32 svc, sic;
            std::memcpy(&svc, p + at, 4);
            std::memcpy(&sic, p + at + 4, 4);
            const size_t size = 8 + size_t(svc) * 36 + size_t(sic) * 2 + 4;
            if (svc == 0 || sic == 0 || sic % 3 || svc > 65536 || at + size > n) break;
            ValMesh::Shape shape;
            shape.vertices.resize(svc);
            bool good = true;
            for (u32 i = 0; i < svc; ++i) {
                const u8* v = p + at + 8 + size_t(i) * 36;
                float f[8];
                std::memcpy(f, v, 24);
                std::memcpy(f + 6, v + 28, 8);
                ValVertex& out = shape.vertices[i];
                out.position = Vec3(f);
                out.normal = Vec3(f + 3);
                out.uv[0] = f[6], out.uv[1] = f[7];
                good = good && eng::finite(out.position);
            }
            shape.indices.resize(sic);
            std::memcpy(shape.indices.data(), p + at + 8 + size_t(svc) * 36, size_t(sic) * 2);
            for (u16 i : shape.indices) good = good && i < svc;
            if (!good) break;
            mesh.shapes.push_back(std::move(shape));
            at += size;
        }
        if (!mesh.shapes.empty()) mesh.shape_seconds = float(ms >= 10 && ms <= 5000 ? ms : 33) / 1000.0f;
    }
    return mesh;
}

// Soldier Front has no ladder volume: a ladder is an ordinary prop, and the only thing marking
// it out is that its asset name, its Object_Type or its texture says "ladder".
bool names_a_ladder(std::string_view text) { return lower(text).find("ladder") != std::string::npos; }

Surface surface_from_object_type(std::string_view type) {
    const std::string t = lower(type);
    if (t.starts_with("ladder")) return Surface::Ladder;
    if (t.starts_with("metal")) return Surface::Metal;
    if (t.starts_with("wood")) return Surface::Wood;
    if (t.starts_with("glass")) return Surface::Glass;
    if (t.starts_with("mud")) return Surface::Dirt;
    if (t.starts_with("water")) return Surface::Water;
    return Surface::Concrete;
}

Surface surface_from_texture(std::string_view path) {
    const std::string t = lower(path);
    auto has = [&](const char* word) { return t.find(word) != std::string::npos; };
    if (has("metal") || has("iron") || has("steel") || has("container") || has("pipe")) return Surface::Metal;
    if (has("wood") || has("crate") || has("box")) return Surface::Wood;
    if (has("grass") || has("leaf") || has("tree")) return Surface::Grass;
    if (has("sand") || has("desert")) return Surface::Sand;
    if (has("dirt") || has("mud") || has("soil") || has("ground")) return Surface::Dirt;
    if (has("glass") || has("window")) return Surface::Glass;
    if (has("water")) return Surface::Water;
    if (has("tile")) return Surface::Tile;
    if (has("snow") || has("ice")) return Surface::Snow;
    return Surface::Concrete;
}

Vec3 sf_point(const float* p) { return Vec3(p) * kSfToCm; }
Vec3 sf_point(const std::array<float, 3>& p) { return Vec3(p.data()) * kSfToCm; }

bool usable_normal(const Vec3& n) {
    if (!eng::finite(n)) return false;
    const float l2 = eng::length_sq(n);
    return l2 > 0.25f && l2 < 2.25f;
}

struct PropFamily {
    std::string name;
    std::optional<ValMesh> render;
    std::optional<ValMesh> collide;
    bool blocks = true;
    bool skip = false;
    Surface surface = Surface::Concrete;
    std::string texture;
    std::optional<AssetLocation> where;
    bool glow = false, unlit = false;
    std::string why_no_mesh;   // what the archives have of a family that draws nothing
    std::vector<LevelMaterial::Frame> frames;   // all its pictures, when it has more than one
    int card = 0;                               // 1: turns to the eye outright; 2: upright, about its foot
    int material = -1;                          // its own in the level, once made (one with pictures in turn shares none)
};

class Loader {
public:
    Loader(const Data& data, Level& level, u32 parts) : data_(data), held_(data.library(Pack::Area)), lib_(*held_), level_(level), parts_(parts) {}

    bool load(const std::string& map_key, std::string* error);

private:
    u32 material(const std::string& texture, std::optional<AssetLocation> where, Surface surface, bool prop, int sound = -1);
    PropFamily& family(const std::string& raw_name);
    void add_prop(PropFamily& fam, const Mat4& world, std::map<u32, std::vector<u32>>& prop_batches);
    u32 prop_material(PropFamily& fam);
    void add_card(PropFamily& fam, const Mat4& world);
    void add_flag(PropFamily& fam, const Mat4& world);
    std::optional<std::vector<std::byte>> read_key(std::string_view key) const {
        auto where = lib_.find(key);
        if (!where) return std::nullopt;
        auto bytes = lib_.read(*where);
        if (!bytes) return std::nullopt;
        return std::move(*bytes);
    }
    std::optional<std::vector<std::byte>> read_at(AssetLocation where) const {
        auto bytes = lib_.read(where);
        if (!bytes) return std::nullopt;
        return std::move(*bytes);
    }
    std::optional<AssetLocation> resolve(std::string_view key) const {
        return data_.resolve(Pack::Area, key);   // NM-3: never into another's pack
    }
    std::optional<AssetLocation> resolve_any(std::string_view key) const { return data_.resolve_any_extension(Pack::Area, key); }

    const Data& data_;
    std::shared_ptr<const AssetLibrary> held_;   // the library as it was when the load began
    const AssetLibrary& lib_;
    Level& level_;
    u32 parts_;
    std::map<std::pair<std::string, bool>, u32> materials_;
    std::unordered_map<std::string, PropFamily> families_;
    std::unordered_map<std::string, u8> sound_by_leaf_;   // the texture table's sound material, by texture file stem
};

u32 Loader::material(const std::string& texture, std::optional<AssetLocation> where, Surface surface, bool prop, int sound) {
    auto key = std::make_pair(texture, prop);
    if (auto it = materials_.find(key); it != materials_.end()) return it->second;
    LevelMaterial m;
    m.texture = texture;
    m.where = where;
    m.surface = surface;
    m.prop = prop;
    // The texture table's own word on the sound, else the one a prop's texture has in it, else a guess.
    if (sound < 0)
        if (auto it = sound_by_leaf_.find(stem_of(lower(texture))); it != sound_by_leaf_.end()) sound = it->second;
    m.sound = sound >= 0 ? u8(sound) : sound_material_of(surface);
    level_.materials.push_back(std::move(m));
    const u32 index = u32(level_.materials.size() - 1);
    materials_.emplace(key, index);
    return index;
}

PropFamily& Loader::family(const std::string& raw_name) {
    const std::string key = lower(raw_name);
    if (auto it = families_.find(key); it != families_.end()) return it->second;
    PropFamily fam;
    fam.name = key;

    std::string mesh_dir;
    fam.why_no_mesh = "no files";
    if (auto val = data_.find_leaf(Pack::Area, key + ".val", "object/")) {
        mesh_dir = parent_key(lib_.key(*val));
        fam.why_no_mesh = "val unread";
        if (parts_ & (kLevelGeometry | kLevelCollision))
            if (auto bytes = read_at(*val)) fam.render = read_val(*bytes);
    }
    if (parts_ & kLevelCollision)
        if (auto val = data_.find_leaf(Pack::Area, key + "_c.val", "object/"))
            if (auto bytes = read_at(*val)) fam.collide = read_val(*bytes);

    std::vector<std::string> textures;
    if (auto osf_loc = data_.find_leaf(Pack::Area, key + ".osf", "object/")) {
        if (auto bytes = read_at(*osf_loc)) {
            if (auto doc = sfd::osf::read(*bytes)) {
                fam.blocks = doc->collision().value_or(true);
                const std::string type = doc->value("Object_Type").value_or("");
                if (fam.why_no_mesh == "no files") fam.why_no_mesh = "osf only: " + (type.empty() ? std::string("no type") : type);
                fam.surface = surface_from_object_type(type);
                const std::string t = lower(type);
                fam.skip = t.starts_with("particle") || t.starts_with("light");
                fam.card = t.starts_with("billboard_fix_y") ? 2 : t.starts_with("billboard") ? 1 : 0;
                textures = doc->textures();
                if (mesh_dir.empty()) mesh_dir = parent_key(lib_.key(*osf_loc));
                // A prop with no mesh of its own name takes the one its script names ([Object Data
                // Name]): Village Horror's fourth red lantern is the third's under another script,
                // S-Center's second saloon the first's in another colour.
                if (!fam.render && (parts_ & (kLevelGeometry | kLevelCollision))) {
                    std::string named = lower(doc->value("Object Data Name").value_or(""));
                    while (!named.empty() && (named.back() == ' ' || named.back() == '\r' || named.back() == '\t' || named.back() == '\0')) named.pop_back();
                    while (!named.empty() && named.front() == ' ') named.erase(0, 1);
                    if (!named.empty() && named != key + ".val") {
                        std::optional<AssetLocation> at;
                        if (auto hit = lib_.find(mesh_dir + "/" + named)) at = *hit;
                        if (!at) at = data_.find_leaf(Pack::Area, named, "object/");
                        if (at) {
                            fam.why_no_mesh = "val unread";
                            if (auto mesh_bytes = read_at(*at)) fam.render = read_val(*mesh_bytes);
                            if (!fam.collide && (parts_ & kLevelCollision) && named.size() > 4)
                                if (auto hull = lib_.find(mesh_dir + "/" + named.substr(0, named.size() - 4) + "_c.val"))
                                    if (auto hull_bytes = read_at(*hull)) fam.collide = read_val(*hull_bytes);
                        }
                    }
                }
                auto flag = [&](const char* k) { const auto v = doc->value(k); return v ? std::optional<bool>(!v->empty() && v->front() != '0') : std::nullopt; };
                fam.glow = flag("TransParent").value_or(false) && !flag("ZWriteEnable").value_or(true);
                fam.unlit = !flag("LightEnable").value_or(true);
            }
        }
    }
    if (!textures.empty() && (parts_ & kLevelGeometry)) {
        auto find = [&](const std::string& name) {
            LevelMaterial::Frame f;
            if (!mesh_dir.empty()) {
                if (auto hit = lib_.find(mesh_dir + "/" + name)) f.where = *hit;
                else if (auto any = resolve_any(mesh_dir + "/" + name)) f.where = *any;
            }
            if (!f.where) f.where = data_.find_leaf(Pack::Area, name, "object/");
            if (!f.where) f.where = resolve_any(name);
            f.texture = f.where ? lib_.key(*f.where) : lower(name);
            return f;
        };
        LevelMaterial::Frame first = find(textures.front());
        fam.where = first.where;
        fam.texture = first.texture;
        // Its other pictures, as many as the mesh says it shows.
        if (fam.render && fam.render->frames > 1 && textures.size() > 1) {
            const size_t count = std::min<size_t>(textures.size(), fam.render->frames);
            fam.frames.push_back(std::move(first));
            for (size_t i = 1; i < count; ++i) fam.frames.push_back(find(textures[i]));
        }
    }
    // One flat card that says it faces whoever looks ([Object_Type] billboard, billboard_fix_y).
    // Every one the client ships is modelled standing in its own x and y, with no depth; anything
    // else that says so (Sentosa's plant is a bush of crossed leaves) stays where it was put.
    if (fam.card) {
        Aabb local;
        if (fam.render)
            for (const ValVertex& v : fam.render->vertices) local.add(v.position);
        const Vec3 e = local.valid() ? local.max - local.min : Vec3{0, 0, 0};
        if (!fam.render || fam.render->vertices.size() != 4 || fam.render->indices.size() != 6 || e.x < 1.0f || e.y < 1.0f || e.z > 0.05f * std::min(e.x, e.y))
            fam.card = 0;
    }
    if (fam.surface != Surface::Ladder && (names_a_ladder(key) || names_a_ladder(fam.texture))) fam.surface = Surface::Ladder;
    return families_.emplace(key, std::move(fam)).first->second;
}

// A prop's material. One whose pictures take turns has its own: another prop with the same first
// picture may show that one alone (whred_02 is whred_01's light, never blinking).
u32 Loader::prop_material(PropFamily& fam) {
    if (fam.frames.size() < 2 || !fam.render) return material(fam.texture, fam.where, fam.surface, true);
    if (fam.material < 0) {
        LevelMaterial m = level_.materials[material(fam.texture, fam.where, fam.surface, true)];
        m.frames = fam.frames;
        m.frame_seconds = fam.render->frame_seconds;
        level_.materials.push_back(std::move(m));
        fam.material = int(level_.materials.size() - 1);
    }
    return u32(fam.material);
}

// A card as it is placed: how wide and tall, where its middle (or its foot) is, and which part of
// the picture it shows (a fire's stops short of the top edge).
void Loader::add_card(PropFamily& fam, const Mat4& world) {
    Aabb local;
    for (const ValVertex& v : fam.render->vertices) local.add(v.position);
    const Vec3 mid = local.center();
    LevelCard card;
    card.upright = fam.card == 2;
    card.material = prop_material(fam);
    card.width = eng::length(world.transform_vector(Vec3{local.max.x - local.min.x, 0, 0})) * kSfToCm;
    card.height = eng::length(world.transform_vector(Vec3{0, local.max.y - local.min.y, 0})) * kSfToCm;
    card.at = world.transform_point(card.upright ? Vec3{mid.x, local.min.y, mid.z} : mid) * kSfToCm;
    for (const ValVertex& v : fam.render->vertices) {
        card.uv[v.position.x < mid.x ? 0 : 2] = v.uv[0];
        card.uv[v.position.y > mid.y ? 1 : 3] = v.uv[1];
    }
    LevelMaterial& m = level_.materials[card.material];
    m.glow = m.glow || fam.glow;
    m.unlit = m.unlit || fam.unlit;
    if (fam.glow || fam.unlit) m.colour = fam.render->colour;
    if (card.width <= 0.5f || card.height <= 0.5f) return;
    level_.cards.push_back(card);
    if (fam.glow) {
        const Vec3 centre = card.at + Vec3{0, card.upright ? card.height * 0.5f : 0.0f, 0};
        level_.glows.push_back(centre);
        Level::Lamp lamp;
        lamp.source = centre;
        lamp.colour = fam.render->colour;
        lamp.length = std::max(card.width, card.height);
        lamp.width = std::min(card.width, card.height);
        level_.lamps.push_back(lamp);
    }
}

// A flag as it is placed: every shape of it as triangles, in the level's space.
void Loader::add_flag(PropFamily& fam, const Mat4& world) {
    LevelFlag flag;
    flag.material = prop_material(fam);
    flag.shape_seconds = fam.render->shape_seconds;
    auto put = [&](const std::vector<ValVertex>& vertices, const std::vector<u16>& indices) {
        std::vector<LevelVertex> out;
        out.reserve(indices.size());
        for (u16 i : indices) {
            const ValVertex& v = vertices[i];
            LevelVertex lv{};
            const Vec3 at = world.transform_point(v.position) * kSfToCm;
            Vec3 n = eng::normalize(world.transform_vector(v.normal));
            if (!usable_normal(n)) n = {0, 1, 0};
            std::memcpy(lv.position, &at.x, 12);
            std::memcpy(lv.normal, &n.x, 12);
            lv.uv0[0] = v.uv[0], lv.uv0[1] = v.uv[1];
            flag.bounds.add(at);
            out.push_back(lv);
        }
        flag.shapes.push_back(std::move(out));
    };
    put(fam.render->vertices, fam.render->indices);
    for (const ValMesh::Shape& s : fam.render->shapes) put(s.vertices, s.indices);
    LevelMaterial& m = level_.materials[flag.material];
    m.glow = m.glow || fam.glow;
    m.unlit = m.unlit || fam.unlit;
    level_.bounds.add(flag.bounds);
    level_.flags.push_back(std::move(flag));
}

void Loader::add_prop(PropFamily& fam, const Mat4& world, std::map<u32, std::vector<u32>>& prop_batches) {
    ++level_.stats.props;
    if (fam.skip) return;
    if (parts_ & kLevelGeometry) {
        if (!fam.render) {
            ++level_.stats.props_without_mesh;
            auto& missing = level_.stats.props_missing[fam.name];
            ++missing.first;
            missing.second = fam.why_no_mesh;
        } else if (fam.card) {
            if (fam.frames.size() > 1) ++level_.stats.props_animated;
            add_card(fam, world);
        } else if (!fam.render->shapes.empty()) {
            ++level_.stats.props_animated;
            add_flag(fam, world);
        } else {
            const u32 mat = prop_material(fam);
            if (fam.frames.size() > 1) ++level_.stats.props_animated;
            if (fam.glow) {
                Aabb box;
                std::vector<Vec3> pts;
                pts.reserve(fam.render->vertices.size());
                for (const ValVertex& v : fam.render->vertices) {
                    pts.push_back(world.transform_point(v.position) * kSfToCm);
                    box.add(pts.back());
                }
                const Vec3 centre = (box.min + box.max) * 0.5f;
                level_.glows.push_back(centre);
                // The lamps it is. A beam's cone (a headlight's, a spot's) fans out from its bulb: a
                // point several of the mesh's triangles meet at, all opening the same way. One glow
                // prop can hold more than one (a lorry's two headlights). A glow with no such point
                // (a tube, a sign, a fire) shines all round from its middle.
                struct Tip {
                    Vec3 at;
                    Vec3 sum{};
                    int triangles = 0;
                    float reach = 0, spread = 0;
                };
                std::vector<Tip> tips;
                auto tip_at = [&](const Vec3& q) -> size_t {
                    for (size_t k = 0; k < tips.size(); ++k)
                        if (eng::length_sq(tips[k].at - q) < 4.0f) return k;
                    tips.push_back({q});
                    return tips.size() - 1;
                };
                const std::vector<u16>& idx = fam.render->indices;
                for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                    const u16 tri[3] = {idx[t], idx[t + 1], idx[t + 2]};
                    if (tri[0] >= pts.size() || tri[1] >= pts.size() || tri[2] >= pts.size()) continue;
                    for (int k = 0; k < 3; ++k) {
                        Tip& tip = tips[tip_at(pts[tri[k]])];
                        ++tip.triangles;
                        for (int j = 1; j <= 2; ++j) {
                            const Vec3 d = pts[tri[(k + j) % 3]] - tip.at;
                            tip.sum = tip.sum + d;
                            tip.reach = std::max(tip.reach, eng::length(d));
                        }
                    }
                }
                size_t beams = 0;
                for (const Tip& tip : tips) {
                    if (tip.triangles < 3 || tip.reach < 100.0f) continue;
                    const float along = eng::length(tip.sum) / float(tip.triangles * 2);
                    if (along < tip.reach * 0.3f) continue;   // its triangles open every way: not a fan
                    Level::Lamp lamp;
                    lamp.beam = true;
                    lamp.source = tip.at;
                    lamp.direction = eng::normalize(tip.sum);
                    lamp.length = tip.reach;
                    lamp.colour = fam.render->colour;
                    for (const Vec3& q : pts) {
                        const Vec3 d = q - tip.at;
                        const float t = eng::dot(d, lamp.direction);
                        if (t > 0 && t <= tip.reach) lamp.width = std::max(lamp.width, 2.0f * eng::length(d - lamp.direction * t));
                    }
                    level_.lamps.push_back(lamp);
                    ++beams;
                }
                if (beams == 0) {
                    Level::Lamp lamp;
                    lamp.source = centre;
                    lamp.colour = fam.render->colour;
                    const Vec3 e = box.max - box.min;
                    lamp.length = std::max(e.x, std::max(e.y, e.z));
                    lamp.width = std::min(e.x, std::min(e.y, e.z));
                    level_.lamps.push_back(lamp);
                }
            }
            if (fam.glow || fam.unlit) {
                LevelMaterial& m = level_.materials[mat];
                m.glow = m.glow || fam.glow;
                m.unlit = m.unlit || fam.unlit;
                m.colour = fam.render->colour;
            }
            auto& indices = prop_batches[mat];
            const u32 base = u32(level_.vertices.size());
            for (const ValVertex& v : fam.render->vertices) {
                LevelVertex out{};
                const Vec3 p = world.transform_point(v.position) * kSfToCm;
                Vec3 n = eng::normalize(world.transform_vector(v.normal));
                if (!usable_normal(n)) n = {0, 1, 0};
                std::memcpy(out.position, &p.x, 12);
                std::memcpy(out.normal, &n.x, 12);
                out.uv0[0] = v.uv[0];
                out.uv0[1] = v.uv[1];
                level_.vertices.push_back(out);
            }
            for (u16 i : fam.render->indices) indices.push_back(base + i);
        }
    }
    if (!(parts_ & kLevelCollision) || !fam.blocks) return;
    const ValMesh* shape = fam.collide ? &*fam.collide : (fam.render ? &*fam.render : nullptr);
    if (!shape) return;
    ++level_.stats.prop_colliders;
    u16 source = 0;
    if (auto it = std::find(level_.collision_sources.begin(), level_.collision_sources.end(), fam.name); it != level_.collision_sources.end()) {
        source = u16(it - level_.collision_sources.begin());
    } else if (level_.collision_sources.size() < 65535) {
        source = u16(level_.collision_sources.size());
        level_.collision_sources.push_back(fam.name);
    }
    const u32 base = u32(level_.collision_vertices.size());
    for (const ValVertex& v : shape->vertices) level_.collision_vertices.push_back(world.transform_point(v.position) * kSfToCm);
    // Not every prop in the ladder folder is a ladder: sf_o_ladder02 (KF815, Predator B) is a ship's
    // stairway, a walkable ramp between rails. One whose hull has a face a soldier can walk up is
    // walked, and its rails are only rails (held as a ladder, they would catch whoever took the stairs).
    Surface surface = fam.surface;
    if (surface == Surface::Ladder)
        for (size_t t = 0; t + 2 < shape->indices.size(); t += 3) {
            const Vec3& a = level_.collision_vertices[base + shape->indices[t]];
            const Vec3 n = eng::normalize(eng::cross(level_.collision_vertices[base + shape->indices[t + 1]] - a,
                                                     level_.collision_vertices[base + shape->indices[t + 2]] - a));
            if (std::fabs(n.y) >= 0.5f && std::fabs(n.y) < 0.95f) {
                surface = Surface::Metal;
                break;
            }
        }
    for (size_t t = 0; t + 2 < shape->indices.size(); t += 3) {
        for (int k = 0; k < 3; ++k) level_.collision_indices.push_back(base + shape->indices[t + k]);
        level_.collision_surfaces.push_back(surface);
        level_.collision_source.push_back(source);
    }
}

bool Loader::load(const std::string& map_key, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = map_key + ": " + why;
        return false;
    };
    // A server's own map reads its own pack's files by name; no other map ever does (NM-3).
    const PackScope scope(map_key);
    const std::string stem(sfd::map_stem(map_key));
    const std::string base(sfd::map_basename(map_key));
    level_.key = map_key;
    level_.id = level_id(map_key);
    level_.title = sfd::map_label(map_key);

    // ── Gameplay first: it decides whether the map is playable at all ─────────
    std::optional<sfd::script::WorldScript> script;
    {
        auto loc = resolve(stem + ".xml");
        if (!loc) loc = resolve(base + ".xml");
        if (loc)
            if (auto bytes = read_at(*loc))
                if (auto parsed = sfd::script::read(*bytes)) script = std::move(*parsed);
    }
    if (script) {
        level_.objective = script->objective;
        level_.load_image = script->load_image;
    }
    if ((parts_ & kLevelGameplay) && script) {
        auto add_spawns = [&](const std::vector<sfd::script::Spawn>& list, Team team) {
            for (const auto& s : list) level_.spawns.push_back({team, sf_point(s.position), sf_angle_to_yaw(s.angle), s.sector});
        };
        add_spawns(script->spawn_red, Team::Red);
        add_spawns(script->spawn_blue, Team::Blue);
        add_spawns(script->spawn_personal, Team::Any);
        for (const auto& o : script->mission_objects)
            level_.objectives.push_back({o.type, o.file, sf_point(o.position), Vec3(o.angles.data()), o.sector});
        for (const auto& e : script->evacuations)
            level_.zones.push_back({lower(e.team) == "blue" ? Team::Blue : Team::Red, sf_point(e.position), e.radius * kSfToCm});
        for (const auto& l : script->locations) level_.places.push_back({lower(l.name), sf_point(l.position), l.radius * kSfToCm});
        for (const auto& t : script->targets) level_.targets.push_back({sf_point(t.a), sf_point(t.b), sf_angle_to_yaw(t.angle)});
        for (const auto& s : script->npc_spawn) level_.npc_spots.push_back(sf_point(s.position));
        for (const auto& s : script->ammobox_spawn) level_.ammo_spots.push_back(sf_point(s.position));
        for (const auto& s : script->zm2_zombie) level_.zombie_spawns.push_back({Team::Red, sf_point(s.position), sf_angle_to_yaw(s.angle), s.sector});
        for (const auto& s : script->zm2_human) level_.human_spawns.push_back({Team::Blue, sf_point(s.position), sf_angle_to_yaw(s.angle), s.sector});
        for (const auto& s : script->sounds)
            level_.sounds.push_back({s.file, sf_point(s.position), s.volume, s.min_distance * kSfToCm, s.max_distance * kSfToCm, s.loop_type != 0});
        for (const auto& l : script->sector_labels) level_.sector_names.emplace_back(l.sector, l.english);
    }

    std::optional<sfd::wld::World> world;
    if (auto loc = resolve("world/" + base + ".wld"))
        if (auto bytes = read_at(*loc))
            if (auto parsed = sfd::wld::read(*bytes)) world = std::move(*parsed);

    // ── Level geometry ─────────────────────────────────────────────────────────
    if (parts_ & kLevelGeometry) {
        auto map_bytes = read_key(map_key);
        if (!map_bytes) return fail("cannot read the map");
        auto sfmap = sfd::map::read(*map_bytes);
        if (!sfmap) return fail(std::string(sf1::describe(sfmap.error())));
        map_bytes->clear();

        auto msf_loc = resolve(stem + ".msf");
        if (!msf_loc) msf_loc = resolve(base + ".msf");
        if (!msf_loc) return fail("no texture table");
        const std::string mapping_key = lib_.key(*msf_loc);
        auto msf_bytes = read_at(*msf_loc);
        auto mappings = msf_bytes ? sfd::msf::read(*msf_bytes) : sf1::Result<std::vector<sfd::msf::Mapping>>(sf1::err(sf1::Error::IoError));
        if (!mappings) return fail("texture table unreadable");

        // A texture the archives that shipped this map's texture table hold under another
        // extension beats a same-named file from a different map's later patch: CrossRoads
        // names cr_floor_001.jpg and ships .dds, while Sentosa later shipped its own .jpg.
        std::vector<u32> homes;
        for (u32 a = 0; a < lib_.archives().size(); ++a)
            if (lib_.archives()[a].find(mapping_key)) homes.push_back(a);
        auto is_home = [&](u32 archive) { return std::find(homes.begin(), homes.end(), archive) != homes.end(); };
        auto from_home = [&](const std::string& path) -> std::optional<AssetLocation> {
            std::string key = sfd::normalize_entry_name(path);
            const auto dot = key.rfind('.');
            if (dot == std::string::npos || key.find('/', dot) != std::string::npos) return std::nullopt;
            const std::string named = key.substr(dot);
            key.resize(dot);
            for (u32 home : homes)
                for (const char* ext : {".dds", ".jpg", ".tga", ".bmp", ".png"}) {
                    if (named == ext) continue;
                    if (auto entry = lib_.archives()[home].find(key + ext)) return AssetLocation{home, u32(*entry)};
                }
            return std::nullopt;
        };

        std::vector<i32> material_of(mappings->size(), -1);
        for (size_t i = 0; i < mappings->size(); ++i) {
            const std::string& path = (*mappings)[i].path;
            auto where = resolve(path);
            if (where && !is_home(where->archive))
                if (auto own = from_home(path)) where = *own;
            const bool exact = where.has_value();
            if (!where) where = resolve_any(path);
            if (where && !exact) ++level_.stats.recovered_textures;
            if (!where) ++level_.stats.missing_textures;
            const std::string key = where ? lib_.key(*where) : sfd::normalize_entry_name(path);
            const auto& flag = (*mappings)[i].native_flag;
            const int sound = flag && *flag <= 12 && *flag != 10 ? int(*flag) : -1;
            if (sound >= 0) sound_by_leaf_[stem_of(sfd::normalize_entry_name(path))] = u8(sound);
            material_of[i] = i32(material(key, where, surface_from_texture(path), false, sound));
        }

        std::map<std::string, i32> lightmap_slots;
        level_.stats.sectors = sfmap->sectors.size();
        for (auto& sec : sfmap->sectors) {
            i32 lightmap = -1;
            if (!sec.lightmaps.empty()) {
                auto where = sfd::resolve_lightmap(lib_, map_key, sec.lightmaps.front());
                if (!where) {
                    ++level_.stats.missing_lightmaps;
                } else {
                    const std::string key = lib_.key(*where);
                    auto [it, fresh] = lightmap_slots.emplace(key, i32(level_.lightmaps.size()));
                    if (fresh) {
                        level_.lightmaps.push_back(key);
                        level_.lightmap_where.push_back(*where);
                    }
                    lightmap = it->second;
                }
            }
            // Shipped vertex normals are sometimes the 0xCDCDCDCD fill; rebuild those from faces.
            std::vector<Vec3> accumulated(sec.vertices.size());
            for (const auto& g : sec.groups)
                for (size_t t = 0; t < g.triangle_count(); ++t)
                    for (int k = 0; k < 3; ++k) accumulated[g.indices[t * 3 + k]] += Vec3(g.face_normals[t].data());

            std::vector<u32> remap(sec.vertices.size(), UINT32_MAX);
            std::map<u32, std::vector<u32>> by_material;
            for (const auto& g : sec.groups) {
                if (g.material == sfd::map::kNoMaterial || g.material >= material_of.size()) continue;
                auto& out = by_material[u32(material_of[g.material])];
                for (u16 i : g.indices) {
                    if (remap[i] == UINT32_MAX) {
                        const auto& in = sec.vertices[i];
                        LevelVertex v{};
                        const Vec3 p = sf_point(in.position);
                        Vec3 n(in.normal);
                        if (!usable_normal(n)) n = accumulated[i];
                        n = eng::normalize(n);
                        if (eng::length_sq(n) < 0.5f) n = {0, 1, 0};
                        std::memcpy(v.position, &p.x, 12);
                        std::memcpy(v.normal, &n.x, 12);
                        v.uv0[0] = in.uv0[0];
                        v.uv0[1] = in.uv0[1];
                        const bool lm = std::abs(in.uv1[0]) <= 64.0f && std::abs(in.uv1[1]) <= 64.0f;
                        v.uv1[0] = lm ? in.uv1[0] : 0.0f;
                        v.uv1[1] = lm ? in.uv1[1] : 0.0f;
                        remap[i] = u32(level_.vertices.size());
                        level_.vertices.push_back(v);
                    }
                    out.push_back(remap[i]);
                }
                level_.stats.triangles += g.triangle_count();
            }
            for (auto& [mat, indices] : by_material) {
                LevelBatch batch;
                batch.material = mat;
                batch.lightmap = lightmap;
                batch.first_index = u32(level_.indices.size());
                batch.index_count = u32(indices.size());
                for (u32 i : indices) batch.bounds.add(Vec3(level_.vertices[i].position));
                level_.indices.insert(level_.indices.end(), indices.begin(), indices.end());
                level_.bounds.add(batch.bounds);
                level_.batches.push_back(batch);
            }
        }
    }

    // ── Collision ──────────────────────────────────────────────────────────────
    if (parts_ & kLevelCollision) {
        if (auto bytes = read_key(stem + "_c.cft")) {
            if (auto collision = sfd::cft::read(*bytes)) {
                for (const auto& mesh : collision->meshes) {
                    const u32 first = u32(level_.collision_vertices.size());
                    for (const auto& v : mesh.vertices) level_.collision_vertices.push_back(sf_point(v));
                    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                        for (int k = 0; k < 3; ++k) level_.collision_indices.push_back(first + mesh.indices[t + k]);
                        level_.collision_surfaces.push_back(Surface::Concrete);
                        level_.collision_source.push_back(0);
                    }
                }
            }
        }
    }

    // ── Props ──────────────────────────────────────────────────────────────────
    if (parts_ & (kLevelGeometry | kLevelCollision)) {
        std::map<u32, std::vector<u32>> prop_batches;
        bool placed = false;
        if (world) {
            for (const auto& sector : world->sectors)
                for (const auto& record : sector.objects) {
                    auto rec = sfd::wld::decode_object(record);
                    if (!rec || rec->name_index >= world->object_names.size()) continue;
                    Mat4 m;
                    std::memcpy(&m.m[0][0], rec->world.data(), sizeof(float) * 16);
                    add_prop(family(world->object_names[rec->name_index]), m, prop_batches);
                    placed = true;
                }
        }
        if (!placed) {
            auto env_loc = resolve(stem + "_obj.env");
            if (!env_loc) env_loc = resolve(base + "_obj.env");
            if (env_loc)
                if (auto bytes = read_at(*env_loc))
                    if (auto objects = sfd::env::read(*bytes))
                        for (const auto& p : objects->placements) {
                            Mat4 m;
                            std::memcpy(&m.m[0][0], p.world.data(), sizeof(float) * 16);
                            add_prop(family(std::string(sfd::env::object_stem(p.source))), m, prop_batches);
                        }
        }
        for (auto& [mat, indices] : prop_batches) {
            LevelBatch batch;
            batch.material = mat;
            batch.lightmap = -1;
            batch.first_index = u32(level_.indices.size());
            batch.index_count = u32(indices.size());
            for (u32 i : indices) batch.bounds.add(Vec3(level_.vertices[i].position));
            level_.indices.insert(level_.indices.end(), indices.begin(), indices.end());
            level_.bounds.add(batch.bounds);
            level_.batches.push_back(batch);
        }
    }
    for (const Vec3& v : level_.collision_vertices) level_.bounds.add(v);

    // ── Environment ────────────────────────────────────────────────────────────
    if (world) {
        for (const auto& sector : world->sectors) {
            bool found = false;
            for (const auto& light : sector.lights) {
                if (light.type != sfd::wld::LightType::Directional) continue;
                Vec3 dir(light.direction.data());
                if (!eng::finite(dir) || eng::length_sq(dir) < 1e-4f) continue;
                level_.sun_direction = eng::normalize(dir);
                level_.sun_colour = {eng::clampf(light.diffuse[0], 0.2f, 1.2f), eng::clampf(light.diffuse[1], 0.2f, 1.2f),
                                     eng::clampf(light.diffuse[2], 0.2f, 1.2f)};
                found = true;
                break;
            }
            if (found) break;
        }
        // The world names its sky's faces from the sky folder ("morning2\\right.bmp"). Asked for
        // there first: the client ships morning2 twice (under sky/ and again under
        // data_adballoon/sky/), and found by its name alone it is two skies and so none --
        // Sentosa, the Hospital and the Nuclear plant stood under a black one.
        if ((parts_ & kLevelGeometry) && world->sky.size() == 6)
            for (int f = 0; f < 6; ++f) {
                auto where = resolve("sky/" + world->sky[size_t(f)]);
                if (!where) where = resolve_any("sky/" + world->sky[size_t(f)]);
                if (!where) where = resolve_any(world->sky[size_t(f)]);
                if (where) level_.sky[size_t(f)] = lib_.key(*where);
            }
    }
    level_.sun_direction = eng::normalize(level_.sun_direction);
    return true;
}

}  // namespace

// The prop mesh reader on bytes alone, for the fuzz harness (sfcheck fuzz val: SC-4).
bool probe_val(std::span<const std::byte> bytes) { return read_val(bytes).has_value(); }

std::vector<LevelListing> list_levels(const Data& data) {
    std::vector<LevelListing> out;
    const auto lib = data.library(Pack::Area);
    if (!lib) return out;
    std::map<std::string, std::string> maps;
    for (const auto& [key, where] : lib->keys("", ".map")) {
        const std::string id = level_id(key);
        auto it = maps.find(id);
        if (it == maps.end() || (!it->second.starts_with("ground/") && key.starts_with("ground/"))) maps[id] = key;
    }
    for (const auto& [id, key] : maps) {
        LevelListing l;
        l.id = id;
        l.key = key;
        l.title = sfd::map_label(key);
        const std::string stem(sfd::map_stem(key));
        const std::string base(sfd::map_basename(key));
        l.playable = lib->resolve(stem + ".xml").has_value() || lib->resolve(base + ".xml").has_value();
        out.push_back(std::move(l));
    }
    return out;
}

std::optional<Level> load_level(const Data& data, std::string_view name, u32 parts, std::string* error) {
    const auto lib = data.library(Pack::Area);
    if (!lib) {
        if (error) *error = "the client has no area archives";
        return std::nullopt;
    }
    std::string key;
    const std::string want = lower(name);
    if (want.ends_with(".map")) {
        key = want;
    } else {
        std::string id = want;
        if (id.starts_with("sf_m_")) id.erase(0, 5);
        for (const LevelListing& l : list_levels(data))
            if (l.id == id) {
                key = l.key;
                break;
            }
    }
    if (key.empty()) {
        if (error) *error = "no map called " + std::string(name);
        return std::nullopt;
    }
    Level level;
    Loader loader(data, level, parts);
    if (!loader.load(key, error)) return std::nullopt;
    return level;
}

void build_sound_mesh(const Level& level, eng::CollisionMesh& out) {
    std::vector<Vec3> vertices(level.vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) vertices[i] = Vec3(level.vertices[i].position);
    std::vector<u32> indices;
    std::vector<u8> sounds;
    for (const LevelBatch& b : level.batches) {
        const u8 s = b.material < level.materials.size() ? level.materials[b.material].sound : u8(kSoundConcrete);
        for (u32 k = 0; k + 2 < b.index_count; k += 3) {
            for (u32 j = 0; j < 3; ++j) indices.push_back(level.indices[b.first_index + k + j]);
            sounds.push_back(s);
        }
    }
    out.clear();
    if (!indices.empty()) out.build(vertices, indices, sounds);
}

float measure_baked_level(const Data& data, const Level& level) {
    double sum = 0;
    size_t count = 0;
    for (size_t k = 0; k < level.lightmaps.size(); ++k) {
        std::optional<std::vector<std::byte>> bytes;
        if (k < level.lightmap_where.size() && level.lightmap_where[k]) bytes = data.read(Pack::Area, *level.lightmap_where[k]);
        else bytes = data.read(Pack::Area, level.lightmaps[k]);
        eng::Image img;
        if (!bytes || !decode_image(*bytes, img) || img.rgba.empty()) continue;
        // Every fourth texel each way is plenty for a mean.
        for (u32 y = 0; y < img.height; y += 4)
            for (u32 x = 0; x < img.width; x += 4) {
                const u8* px = &img.rgba[(size_t(y) * img.width + x) * 4];
                const float l = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
                if (l < 0.02f) continue;   // the padding between charts
                sum += l;
                ++count;
            }
    }
    return count ? float(sum / double(count)) * 2.0f : -1.0f;
}

}  // namespace sf
