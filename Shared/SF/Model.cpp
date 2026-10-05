#include "SF/Model.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

namespace sf {

namespace {

// ── The frame ──────────────────────────────────────────────────────────────────
Vec3 to_engine(const Vec3& v) { return {-v.z, v.y, v.x}; }
// A rotation's axis moves with the frame; the angle stays.
Quat to_engine(const float q[4]) { return eng::normalize(Quat{-q[2], q[1], q[0], q[3]}); }

// ── The rig ────────────────────────────────────────────────────────────────────
//
// A model's skeleton, its geometry and every clip are separate files that agree only on node
// names, so the rig is collected by name across all of them: the file that defines a node
// first wins, and anything only a later file knows about is added rather than lost.
class Rig {
public:
    void collect(const LmaModel& model) {
        for (const LmaNode& n : model.nodes) {
            if (n.name.empty()) continue;
            const std::string key = lower(n.name);
            if (index_.contains(key)) continue;
            index_.emplace(key, entries_.size());
            entries_.push_back({n.name, lower(n.parent), n.position, to_engine(n.rotation), n.scale});
        }
    }

    // A clip may drive something no geometry file declares: `Camera01`, where the game puts the
    // view camera. Parts that already resolve (loosely too) are left alone, or the arms would
    // bind to a new empty bone instead of the one their geometry is skinned to.
    void collect_missing_motion(const LmaModel& clip) {
        for (const LmaMotionPart& p : clip.motion) {
            if (p.name.empty() || resolves(p.name)) continue;
            const std::string key = lower(p.name);
            if (index_.contains(key)) continue;
            index_.emplace(key, entries_.size());
            entries_.push_back({p.name, {}, p.position, to_engine(p.rotation), p.scale});
        }
    }

    // Orders the bones so a parent always comes before its child.
    bool finish(float scale, std::string* error) {
        const size_t n = entries_.size();
        std::vector<int> parent(n, -1);
        for (size_t i = 0; i < n; ++i)
            if (auto it = index_.find(entries_[i].parent); it != index_.end() && it->second != i) parent[i] = int(it->second);
        std::vector<int> depth(n, 0);
        for (size_t i = 0; i < n; ++i) {
            int hops = 0;
            for (int at = int(i); at >= 0; at = parent[size_t(at)])
                if (++hops > int(n)) {
                    if (error) *error = "the node parents form a loop";
                    return false;
                }
            depth[i] = hops;
        }
        std::vector<int> order(n);
        for (size_t i = 0; i < n; ++i) order[i] = int(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[size_t(a)] < depth[size_t(b)]; });
        remap_.assign(n, -1);
        for (size_t slot = 0; slot < n; ++slot) remap_[size_t(order[slot])] = int(slot);

        bones_.clear();
        world_.assign(n, Mat4::identity());
        for (size_t slot = 0; slot < n; ++slot) {
            const Entry& e = entries_[size_t(order[slot])];
            const int p = parent[size_t(order[slot])];
            const int parent_slot = p >= 0 ? remap_[size_t(p)] : -1;
            const Mat4 local = Mat4::scale(e.scale) * Mat4::from_quaternion(e.rotation) * Mat4::translation(to_engine(e.position) * scale);
            world_[slot] = parent_slot >= 0 ? local * world_[size_t(parent_slot)] : local;
            ModelBone bone;
            bone.name = e.name;
            bone.parent = parent_slot;
            bone.bind_local = local;
            bone.inverse_bind = world_[slot].inverse();
            bones_.push_back(std::move(bone));
        }
        alias_.clear();
        for (size_t slot = 0; slot < n; ++slot) {
            const std::string loose = loose_name(bones_[slot].name);
            if (loose.empty()) continue;
            auto [it, fresh] = alias_.emplace(loose, int(slot));
            if (!fresh) it->second = -1;
        }
        return true;
    }

    // Exact name first; failing that the loose form, only where it picks out one bone (a clip
    // calls the arm roots `sf_a_hand_right`, the rig `SF_hand_right01`).
    int find(std::string_view name) const {
        if (auto it = index_.find(lower(name)); it != index_.end()) return remap_[it->second];
        if (auto it = alias_.find(loose_name(name)); it != alias_.end() && it->second >= 0) return it->second;
        return -1;
    }
    std::vector<int> bind(const LmaModel& model) const {
        std::vector<int> out(model.nodes.size(), -1);
        for (size_t i = 0; i < model.nodes.size(); ++i) out[i] = find(model.nodes[i].name);
        return out;
    }
    const std::vector<ModelBone>& bones() const { return bones_; }
    const Mat4& world(int bone) const { return world_[size_t(bone)]; }
    size_t size() const { return entries_.size(); }

private:
    static std::string loose_name(std::string_view name) {
        std::string out = lower(name);
        for (std::string_view prefix : {"sf_a_", "sf_"})
            if (out.starts_with(prefix)) {
                out.erase(0, prefix.size());
                break;
            }
        while (!out.empty() && std::isdigit((unsigned char)out.back())) out.pop_back();
        while (!out.empty() && out.back() == '_') out.pop_back();
        return out;
    }
    struct Entry {
        std::string name;
        std::string parent;
        Vec3 position;
        Quat rotation;
        Vec3 scale;
    };
    bool resolves(std::string_view name) const {
        if (index_.contains(lower(name))) return true;
        const std::string loose = loose_name(name);
        if (loose.empty()) return false;
        int hits = 0;
        for (const Entry& e : entries_)
            if (loose_name(e.name) == loose) ++hits;
        return hits == 1;
    }
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
    std::vector<int> remap_;
    std::map<std::string, int> alias_;
    std::vector<ModelBone> bones_;
    std::vector<Mat4> world_;
};

// ── Materials ──────────────────────────────────────────────────────────────────
// A layer names a texture stem; the archive is asked which extension it really has.
class Materials {
public:
    Materials(const Data& data, Pack pack, std::string shared) : data_(data), pack_(pack), shared_(std::move(shared)) {}

    u32 index_for(const std::string& texture_name, const std::string& folder, Model& model) {
        const std::string key = lower(folder) + "|" + lower(texture_name);
        if (auto it = done_.find(key); it != done_.end()) return it->second;
        static const char* const kImageExtensions[] = {".dds", ".tga", ".jpg", ".png", ".bmp"};
        std::vector<std::string> where;
        if (!folder.empty()) where.push_back(folder + "/" + texture_name);
        if (!shared_.empty()) where.push_back(shared_ + texture_name);
        where.push_back(texture_name);
        ModelMaterial out;
        out.pack = pack_;
        if (!texture_name.empty())
            for (const std::string& stem : where) {
                for (const char* ext : kImageExtensions)
                    if (auto found = data_.resolve(pack_, stem + ext)) {
                        out.where = *found;
                        out.texture = data_.key(pack_, *found);
                        break;
                    }
                if (out.where) break;
            }
        const u32 index = u32(model.materials.size());
        model.materials.push_back(std::move(out));
        done_.emplace(key, index);
        return index;
    }

private:
    const Data& data_;
    Pack pack_;
    std::string shared_;
    std::map<std::string, u32> done_;
};

// The four heaviest influences, normalised to byte weights summing to 255.
void fill_skin(const std::vector<LmaInfluence>& influences, const std::vector<int>& to_bone, int own_bone, ModelVertex& out) {
    struct Pick {
        int bone;
        float weight;
    };
    Pick picks[8];
    size_t count = 0;
    for (const LmaInfluence& in : influences) {
        const int bone = in.bone < to_bone.size() ? to_bone[in.bone] : -1;
        if (bone >= 0 && bone <= 255 && in.weight > 0 && count < 8) picks[count++] = {bone, in.weight};
    }
    std::memset(out.bones, 0, 4);
    std::memset(out.weights, 0, 4);
    if (count == 0) {
        out.bones[0] = u8(std::clamp(own_bone, 0, 255));
        out.weights[0] = 255;
        return;
    }
    std::sort(picks, picks + count, [](const Pick& a, const Pick& b) { return a.weight > b.weight; });
    count = std::min<size_t>(count, 4);
    float total = 0;
    for (size_t i = 0; i < count; ++i) total += picks[i].weight;
    if (total <= 0) total = 1;
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
        const int w = i + 1 == count ? 255 - used : int(std::lround(double(picks[i].weight / total) * 255.0));
        out.bones[i] = u8(picks[i].bone);
        out.weights[i] = u8(std::clamp(w, 0, 255));
        used += out.weights[i];
    }
}

// Every submesh of `src` posed by the rig. A rigid mesh is drawn in its node's space and is
// carried into the model's; a skinned one is already in model space (its node is the Max mesh
// object, outside the hierarchy its skin points into).
void add_geometry(const LmaModel& src, const Rig& rig, float scale, const std::string& folder, Materials& materials, const char* tag,
                  Model& model) {
    const std::vector<int> to_bone = rig.bind(src);
    for (const LmaMesh& mesh : src.meshes) {
        const int bone = mesh.node < to_bone.size() ? to_bone[mesh.node] : -1;
        if (bone < 0) continue;
        const Mat4 place = mesh.skinned() ? Mat4::identity() : rig.world(bone);
        const std::string& node_name = src.nodes[mesh.node].name;
        const std::string lname = lower(node_name);
        // Hulls and marker shapes are not drawn.
        if (lname.find("collision") != std::string::npos || lname.ends_with("_point")) continue;
        for (const LmaSubmesh& sub : mesh.submeshes) {
            if (sub.indices.empty()) continue;
            ModelMesh out;
            out.name = tag ? tag + node_name : node_name;
            const std::string* texture = sub.material < src.materials.size() ? src.materials[sub.material].texture() : nullptr;
            out.material = materials.index_for(texture ? *texture : std::string(), folder, model);
            out.vertices.resize(sub.vertices.size());
            for (size_t i = 0; i < sub.vertices.size(); ++i) {
                const LmaVertex& v = sub.vertices[i];
                ModelVertex& o = out.vertices[i];
                const Vec3 p = place.transform_point(to_engine(v.position) * scale);
                Vec3 n = place.transform_vector(to_engine(v.normal));
                const float len = std::sqrt(eng::length_sq(n));
                n = len > 1e-6f ? n * (1.0f / len) : Vec3{0, 1, 0};
                std::memcpy(o.position, &p.x, 12);
                std::memcpy(o.normal, &n.x, 12);
                o.uv[0] = v.uv[0];
                o.uv[1] = v.uv[1];
                if (mesh.skinned() && v.original < mesh.skin.size()) {
                    fill_skin(mesh.skin[v.original], to_bone, bone, o);
                } else {
                    std::memset(o.bones, 0, 4);
                    std::memset(o.weights, 0, 4);
                    o.bones[0] = u8(std::clamp(bone, 0, 255));
                    o.weights[0] = 255;
                }
                out.bounds.add(p);
            }
            out.indices = sub.indices;
            model.meshes.push_back(std::move(out));
        }
    }
}

// ── Clips ──────────────────────────────────────────────────────────────────────

Vec3 sample_vec(const std::vector<float>& times, const std::vector<Vec3>& values, float t) {
    if (values.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    size_t i = 1;
    while (i + 1 < times.size() && times[i] < t) ++i;
    const float span = times[i] - times[i - 1];
    const float f = span > 1e-8f ? (t - times[i - 1]) / span : 0.0f;
    return values[i - 1] + (values[i] - values[i - 1]) * f;
}

Quat sample_quat(const std::vector<float>& times, const std::vector<std::array<float, 4>>& values, float t) {
    auto at = [&](size_t i) { return Quat{values[i][0], values[i][1], values[i][2], values[i][3]}; };
    if (values.size() == 1 || t <= times.front()) return at(0);
    if (t >= times.back()) return at(values.size() - 1);
    size_t i = 1;
    while (i + 1 < times.size() && times[i] < t) ++i;
    const float span = times[i] - times[i - 1];
    const float f = span > 1e-8f ? (t - times[i - 1]) / span : 0.0f;
    return eng::slerp(at(i - 1), at(i), f);
}

bool same_key(const float* a, const float* b) {
    for (int i = 0; i < 7; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-5f) return false;
    return true;
}

// Every track of a clip on one shared list of key times (each channel keys on its own in the
// file; they agree in practice). The clip's playback rate is folded into the times.
template <class Find>
ModelAnimation convert_clip(const LmaModel& clip, Find&& find_bone, float scale, const std::string& name, float speed) {
    std::set<float> stamps;
    for (const LmaMotionPart& p : clip.motion) {
        for (float t : p.position_times) stamps.insert(t);
        for (float t : p.rotation_times) stamps.insert(t);
    }
    ModelAnimation out;
    out.name = name;
    if (stamps.empty()) stamps.insert(0.0f);
    const std::vector<float> source_times(stamps.begin(), stamps.end());
    const float rate = speed > 0.01f ? speed : 1.0f;
    for (float t : source_times) out.times.push_back(t / rate);
    out.duration = out.times.back();
    for (const LmaMotionPart& part : clip.motion) {
        const int bone = find_bone(part.name);
        if (bone < 0) continue;
        AnimTrack track;
        track.bone = u32(bone);
        track.keys.resize(source_times.size() * 7);
        for (size_t k = 0; k < source_times.size(); ++k) {
            const float t = source_times[k];
            const Vec3 p = part.positions.empty() ? part.position : sample_vec(part.position_times, part.positions, t);
            const Vec3 pe = to_engine(p) * scale;
            Quat qe;
            if (part.rotations.empty()) {
                qe = to_engine(part.rotation);
            } else {
                const Quat q = sample_quat(part.rotation_times, part.rotations, t);
                const float raw[4] = {q.x, q.y, q.z, q.w};
                qe = to_engine(raw);
            }
            float* key = track.keys.data() + k * 7;
            key[0] = pe.x;
            key[1] = pe.y;
            key[2] = pe.z;
            key[3] = qe.x;
            key[4] = qe.y;
            key[5] = qe.z;
            key[6] = qe.w;
        }
        bool constant = true;
        for (size_t k = 1; k < source_times.size() && constant; ++k) constant = same_key(track.keys.data(), track.keys.data() + k * 7);
        if (constant) track.keys.resize(7);
        out.tracks.push_back(std::move(track));
    }
    return out;
}

std::optional<LmaModel> load_lma(const Data& data, Pack pack, const std::string& key) {
    auto bytes = data.read(pack, key);
    if (!bytes) return std::nullopt;
    std::string why;
    auto model = read_lma(*bytes, &why);
    if (!model) LOG_WARN("SF: %s: %s", key.c_str(), why.c_str());
    return model;
}

// A clip manifest (.sfm): a count, then that many length-prefixed names and a playback rate.
std::map<std::string, float> read_clip_speeds(const Data& data, Pack pack, const std::string& key) {
    std::map<std::string, float> out;
    auto bytes = data.read(pack, key);
    if (!bytes || bytes->size() < 4) return out;
    const auto* p = reinterpret_cast<const u8*>(bytes->data());
    const size_t size = bytes->size();
    u32 count = 0;
    std::memcpy(&count, p, 4);
    size_t at = 4;
    for (u32 i = 0; i < count; ++i) {
        if (at + 4 > size) break;
        u32 length = 0;
        std::memcpy(&length, p + at, 4);
        at += 4;
        if (length > 256 || at + length + 4 > size) break;
        std::string name(reinterpret_cast<const char*>(p + at), length);
        at += length;
        float speed = 1.0f;
        std::memcpy(&speed, p + at, 4);
        at += 4;
        name = lower(leaf_of(eng::str::normalize_path(name)));
        if (auto dot = name.rfind('.'); dot != std::string::npos) name.resize(dot);
        out.emplace(name, speed);
    }
    return out;
}

const char* const kActions[] = {
    "shoot_bayonet_01", "shoot_bayonet_02", "shoot_bayonet", "move_start", "move_end", "reload_01", "reload_02", "reload_03", "reload_04", "shoot_01", "shoot_l", "shoot_r", "gesture",
    "reload",        "shoot1",     "shoot",    "throw",     "wdraw",     "draw",      "idle",      "move",     "hold",    "wait",
};

std::string action_of(const std::string& leaf) {
    for (const char* action : kActions) {
        const size_t n = std::strlen(action);
        if (leaf.size() >= n && leaf.compare(leaf.size() - n, n, action) == 0) return action;
    }
    return {};
}

struct ClassRule {
    const char* token;
    const char* klass;
};
// First match wins, specific names before loose ones.
const ClassRule kClassRules[] = {
    {"flashbang", "grenade"},  {"smoke", "grenade"},      {"grenade", "grenade"},   {"bomb", "grenade"},     {"m67", "grenade"},
    {"rgd5", "grenade"},       {"m18", "grenade"},        {"vx", "grenade"},        {"knife", "knife"},      {"machete", "knife"},
    {"hatchet", "knife"},      {"shovel", "knife"},       {"sword", "knife"},       {"scissors", "knife"},   {"claw", "knife"},
    {"hand", "knife"},         {"psg1", "sniper"},        {"dragunov", "sniper"},   {"awp", "sniper"},       {"tango51", "sniper"},
    {"m200", "sniper"},        {"dsr1", "sniper"},        {"wa2000", "sniper"},     {"frf2", "sniper"},      {"kar98k", "sniper"},
    {"m110", "sniper"},        {"sv_98", "sniper"},       {"musket", "sniper"},     {"m14_ebr", "sniper"},   {"m249", "machinegun"},
    {"m134", "machinegun"},    {"mg36", "machinegun"},    {"cannon", "machinegun"}, {"spas12", "shotgun"},   {"m870", "shotgun"},
    {"benellim", "shotgun"},   {"blast", "shotgun"},      {"mp5", "smg"},           {"mp7", "smg"},          {"uzi", "smg"},
    {"p90", "smg"},            {"kriss", "smg"},          {"mac10", "smg"},         {"thompson", "smg"},     {"ump45", "smg"},
    {"k7", "smg"},             {"k1", "smg"},             {"beretta", "pistol"},    {"deserteagle", "pistol"}, {"dederteagle", "pistol"},
    {"colt45", "pistol"},      {"glock", "pistol"},       {"luger", "pistol"},      {"mk23", "pistol"},      {"mr73", "pistol"},
    {"m945c", "pistol"},       {"desperado", "pistol"},   {"anaconda", "pistol"},   {"m9", "knife"},
};

bool names_token(const std::string& id, const std::string& token) {
    for (size_t at = id.find(token); at != std::string::npos; at = id.find(token, at + 1)) {
        const bool left = at == 0 || !std::isalnum((unsigned char)id[at - 1]);
        const size_t end = at + token.size();
        const bool right = end == id.size() || !std::isalnum((unsigned char)id[end]);
        if (left && right) return true;
    }
    return false;
}

}  // namespace

// ── Model ──────────────────────────────────────────────────────────────────────

int Model::find_bone(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (eng::str::iequals(bones[i].name, name)) return int(i);
    return -1;
}

const ModelSocket* Model::socket(std::string_view name) const {
    for (const ModelSocket& s : sockets)
        if (eng::str::iequals(s.name, name)) return &s;
    return nullptr;
}

const ModelAnimation* Model::animation(std::string_view name) const {
    for (const ModelAnimation& a : animations)
        if (eng::str::iequals(a.name, name)) return &a;
    return nullptr;
}

Aabb Model::bounds() const {
    Aabb b;
    for (const ModelMesh& m : meshes) b.add(m.bounds);
    return b;
}

size_t Model::triangle_count() const {
    size_t n = 0;
    for (const ModelMesh& m : meshes) n += m.indices.size() / 3;
    return n;
}

void sample_animation(const Model& model, const ModelAnimation& anim, float t, bool loop, std::vector<Mat4>& local,
                      const std::vector<bool>* mask) {
    if (local.size() != model.bones.size()) {
        local.resize(model.bones.size());
        for (size_t i = 0; i < model.bones.size(); ++i) local[i] = model.bones[i].bind_local;
    }
    if (anim.times.empty()) return;
    if (loop && anim.duration > 1e-4f) {
        t = std::fmod(t, anim.duration);
        if (t < 0) t += anim.duration;
    }
    t = std::clamp(t, anim.times.front(), anim.times.back());
    size_t k = 0;
    while (k + 1 < anim.times.size() && anim.times[k + 1] <= t) ++k;
    const size_t k1 = std::min(k + 1, anim.times.size() - 1);
    const float span = anim.times[k1] - anim.times[k];
    const float f = span > 1e-6f ? (t - anim.times[k]) / span : 0.0f;
    for (const AnimTrack& tr : anim.tracks) {
        if (tr.bone >= model.bones.size()) continue;
        if (mask && tr.bone < mask->size() && !(*mask)[tr.bone]) continue;
        const float* a;
        const float* b;
        float ff = f;
        if (tr.keys.size() == 7) {
            a = b = tr.keys.data();
            ff = 0;
        } else {
            a = tr.keys.data() + k * 7;
            b = tr.keys.data() + k1 * 7;
        }
        const Vec3 p = eng::lerp(Vec3(a), Vec3(b), ff);
        const Quat q = eng::slerp(Quat{a[3], a[4], a[5], a[6]}, Quat{b[3], b[4], b[5], b[6]}, ff);
        // The bind's scale stays; clips drive position and rotation.
        const Mat4& bind = model.bones[tr.bone].bind_local;
        const Vec3 s{eng::length(Vec3{bind.m[0][0], bind.m[0][1], bind.m[0][2]}), eng::length(Vec3{bind.m[1][0], bind.m[1][1], bind.m[1][2]}),
                     eng::length(Vec3{bind.m[2][0], bind.m[2][1], bind.m[2][2]})};
        local[tr.bone] = Mat4::scale(s) * Mat4::from_quaternion(eng::normalize(q)) * Mat4::translation(p);
    }
}

void pose_to_model(const Model& model, const std::vector<Mat4>& local, std::vector<Mat4>& model_space) {
    model_space.resize(model.bones.size());
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const Mat4& l = i < local.size() ? local[i] : model.bones[i].bind_local;
        const int p = model.bones[i].parent;
        model_space[i] = p >= 0 ? l * model_space[size_t(p)] : l;
    }
}

void skin_matrices(const Model& model, const std::vector<Mat4>& model_space, std::vector<Mat4>& out) {
    out.resize(model.bones.size());
    for (size_t i = 0; i < model.bones.size(); ++i) out[i] = model.bones[i].inverse_bind * model_space[i];
}

// ── Weapons ────────────────────────────────────────────────────────────────────

namespace {

// A weapon's .sfc: a version, an id, a kind and a 255-byte name, then (at byte 267) three numbers
// of the gun's own (480, its magazine, its rate), a class, and where the weapon stands from the
// first-person camera, in the files' own axes and units; then its pieces' names. The place is
// what this reads: every gun has one, and the guns agree (the eye some 40 cm ahead of the elbow,
// 20 above it, 15 to its left), where their Camera01 nodes do not.
std::optional<Vec3> read_view_place(const Data& data, const std::string& key) {
    constexpr size_t kAt = 267 + 14;
    auto bytes = data.read(Pack::Weapon, key);
    if (!bytes || bytes->size() < kAt + 12) return std::nullopt;
    float v[3];
    std::memcpy(v, bytes->data() + kAt, 12);
    for (float f : v)
        if (!std::isfinite(f) || std::fabs(f) > 2000.0f) return std::nullopt;
    if (v[0] == 0 && v[1] == 0 && v[2] == 0) return std::nullopt;
    return Vec3{v[0], v[1], v[2]};
}

}  // namespace

std::string weapon_class_for(std::string_view id_view) {
    const std::string id = lower(id_view);
    for (const ClassRule& r : kClassRules)
        if (names_token(id, r.token)) return r.klass;
    return "rifle";
}

std::vector<WeaponSet> discover_weapons(const Data& data) {
    std::map<std::string, WeaponSet> found;
    // The client's own guns (bhw/...), and a joined server's (x/<pack>/bhw/...).
    auto all = data.keys(Pack::Weapon, "bhw/", "");
    for (auto& k : data.keys(Pack::Weapon, "x/", "")) all.push_back(std::move(k));
    for (const auto& [key, where] : all) {
        const size_t root = pack_prefix(key).size();
        if (!std::string_view(key).substr(root).starts_with("bhw/")) continue;
        const size_t slash = key.rfind('/');
        if (slash == std::string::npos) continue;
        const std::string folder = key.substr(0, slash);
        if (folder.find('/', root + 4) != std::string::npos) continue;   // a spare copy in a subfolder
        std::string leaf = lower(key.substr(slash + 1));
        const size_t dot = leaf.rfind('.');
        const std::string extension = dot == std::string::npos ? std::string() : leaf.substr(dot);
        if (dot != std::string::npos) leaf.resize(dot);
        std::string id = folder.substr(folder.rfind('/') + 1);
        if (id.starts_with("sf_a_")) id = id.substr(5);
        if (id.empty() || id == "texture") continue;
        WeaponSet& set = found[folder];
        set.id = id;
        set.folder = folder;
        if (extension == ".sfm") {
            set.manifest = key;
            continue;
        }
        if (extension == ".sfc") {
            // `_opt` lists a gun's attachment models (never fitted); a stray copy of another gun's
            // file is its second choice.
            if (leaf.ends_with("_opt")) continue;
            const bool own = leaf == "sf_a_" + id;
            if (!own && !set.config.empty()) continue;
            set.config = key;
            set.config_id = leaf.starts_with("sf_a_") ? leaf.substr(5) : leaf;
            continue;
        }
        if (extension != ".lma") continue;
        if (leaf.starts_with("sf_a_m_") || leaf.starts_with("sf_m_")) {
            if (std::string action = action_of(leaf); !action.empty()) set.clips.emplace(action, key);
        } else if (leaf.starts_with("sf_a_b_") || leaf.starts_with("sf_b_")) {
            set.rig = key;
        } else if (leaf.starts_with("sf_a_o_")) {
            // An attachment's model (a silencer, a sight): the game fits none.
        } else if (leaf == "sf_a_hand") {
            set.generic_hands = key;
        } else if (leaf.starts_with("sf_a_hand_")) {
            set.hands = key;
        } else if (leaf.starts_with("sf_a_g_") || leaf.starts_with("sf_a_a_") || leaf == "sf_a_" + id) {
            set.gun = key;
        }
    }
    // A re-release folder ships only new hands and an .sfc naming the original: borrow it.
    std::map<std::string, const WeaponSet*> by_id;
    for (const auto& [folder, set] : found) by_id.emplace(set.id, &set);
    for (auto& [folder, set] : found) {
        if (!set.gun.empty() || set.config_id.empty() || set.config_id == set.id) continue;
        auto base = by_id.find(set.config_id);
        if (base == by_id.end() || base->second->gun.empty()) continue;
        set.rig = base->second->rig;
        set.gun = base->second->gun;
        set.clips = base->second->clips;
        if (set.manifest.empty()) set.manifest = base->second->manifest;
        set.borrowed_from = base->second->id;
    }
    std::vector<WeaponSet> out;
    for (auto& [folder, set] : found) {
        if (set.hands.empty()) set.hands = set.generic_hands;
        // A server's gun goes by its whole folder: never taken for one of the client's.
        if (folder.starts_with("x/")) set.id = folder;
        if (set.usable()) out.push_back(std::move(set));
    }
    return out;
}

float derive_view_scale(const Data& data) {
    static std::mutex m;
    static float cached = 0;
    std::lock_guard lock(m);
    if (cached > 0) return cached;
    constexpr float kReferenceLengthCm = 94.3f;   // the AK-74
    cached = 0.233f;
    auto where = data.find_leaf(Pack::Weapon, "sf_a_g_ak74.lma", "bhw/");
    if (!where) return cached;
    auto bytes = data.read(Pack::Weapon, *where);
    if (!bytes) return cached;
    auto model = read_lma(*bytes, nullptr);
    if (!model || model->meshes.empty()) return cached;
    Rig rig;
    rig.collect(*model);
    if (!rig.finish(1.0f, nullptr)) return cached;
    const std::vector<int> to_bone = rig.bind(*model);
    Aabb box;
    for (const LmaMesh& mesh : model->meshes) {
        const int bone = mesh.node < to_bone.size() ? to_bone[mesh.node] : -1;
        if (bone < 0) continue;
        for (const LmaSubmesh& sub : mesh.submeshes)
            for (const LmaVertex& v : sub.vertices) box.add(rig.world(bone).transform_point(to_engine(v.position)));
    }
    if (!box.valid()) return cached;
    const Vec3 size = box.max - box.min;
    const float longest = std::max({size.x, size.y, size.z});
    if (longest >= 1.0f) cached = kReferenceLengthCm / longest;
    return cached;
}

std::optional<Model> load_weapon(const Data& data, const WeaponSet& w, float scale, std::string* error) {
    if (scale <= 0) scale = derive_view_scale(data);
    const PackScope scope(w.folder);   // a server's gun reads its own pack's textures by name (NM-3)
    Rig rig;
    std::vector<std::pair<std::string, LmaModel>> pieces;
    const std::vector<std::pair<std::string, std::string>> keys = {{"", w.rig}, {"hands:", w.hands}, {"gun:", w.gun}};
    for (const auto& [tag, key] : keys)
        if (!key.empty())
            if (auto model = load_lma(data, Pack::Weapon, key)) {
                rig.collect(*model);
                pieces.emplace_back(tag, std::move(*model));
            }
    if (pieces.empty()) {
        if (error) *error = w.id + ": no readable pieces";
        return std::nullopt;
    }
    std::map<std::string, LmaModel> clips;
    for (const auto& [action, key] : w.clips)
        if (auto clip = load_lma(data, Pack::Weapon, key); clip && !clip->motion.empty()) {
            rig.collect_missing_motion(*clip);
            clips.emplace(action, std::move(*clip));
        }
    std::string why;
    if (!rig.finish(scale, &why)) {
        if (error) *error = w.id + ": " + why;
        return std::nullopt;
    }
    if (rig.size() > 255) {
        if (error) *error = w.id + ": more bones than a vertex can index";
        return std::nullopt;
    }
    Model model;
    model.bones = rig.bones();
    Materials materials(data, Pack::Weapon, "bhw/sf_a_texture/");
    for (const auto& [tag, piece] : pieces) add_geometry(piece, rig, scale, w.folder, materials, tag.c_str(), model);
    if (model.meshes.empty()) {
        if (error) *error = w.id + ": no geometry";
        return std::nullopt;
    }
    for (const char* name : {"flame", "cartridge", "Camera01"})
        if (const int bone = rig.find(name); bone >= 0) model.sockets.push_back({name, bone, Mat4::identity()});
    // The eye: the weapon's place from the camera, turned round (the camera's from the weapon).
    if (!w.config.empty())
        if (const auto place = read_view_place(data, w.config)) {
            model.view_eye = to_engine(*place) * -scale;
            model.has_view_eye = true;
        }
    for (const auto& [tag, piece] : pieces) {
        if (std::string_view(tag) != "gun:") continue;
        for (const LmaNode& n : piece.nodes) {
            if (!n.parent.empty() || lower(n.name).find("trail") != std::string::npos) continue;
            if (const int bone = rig.find(n.name); bone >= 0) model.sockets.push_back({"grip", bone, Mat4::identity()});
            break;
        }
    }
    const auto speeds = w.manifest.empty() ? std::map<std::string, float>() : read_clip_speeds(data, Pack::Weapon, w.manifest);
    for (const auto& [action, clip] : clips) {
        const std::string stem = stem_of(lower(w.clips.at(action)));
        const auto speed = speeds.find(stem);
        ModelAnimation anim = convert_clip(clip, [&](std::string_view n) { return rig.find(n); }, scale, action,
                                           speed == speeds.end() ? 1.0f : speed->second);
        if (!anim.tracks.empty()) model.animations.push_back(std::move(anim));
    }
    // A blade is held in a fighter's stance, both forearms in the picture, and its .sfc says so:
    // every one's puts the right wrist some 20 to 30 degrees under the eye's line. The M9's does
    // not (48: both hands are under the picture and only the blade's tip shows): the place in it
    // is a gun's. The camera its artists left in the model (Camera01) frames it as the others
    // are, so a blade whose .sfc leaves the wrist out of the picture is framed by that instead.
    if (model.has_view_eye && weapon_class_for(leaf_of(w.folder)) == "knife")
        if (const int wrist = model.find_bone("SF_B_hand_right_17"); wrist >= 0 && model.socket("Camera01")) {
            std::vector<Mat4> local(model.bones.size()), in_model;
            for (size_t i = 0; i < local.size(); ++i) local[i] = model.bones[i].bind_local;
            if (const ModelAnimation* idle = model.animation("idle")) sample_animation(model, *idle, 0, false, local);
            pose_to_model(model, local, in_model);
            const Vec3 at = Vec3{in_model[size_t(wrist)].m[3][0], in_model[size_t(wrist)].m[3][1], in_model[size_t(wrist)].m[3][2]} - model.view_eye;
            if (at.z > 1.0f && -at.y > at.z) model.has_view_eye = false;   // more than 45 degrees down
        }
    return model;
}

// ── Characters ─────────────────────────────────────────────────────────────────

namespace {

std::string slot_of(const std::string& node, const std::string& prefix) {
    const std::string l = lower(node);
    return l.starts_with(prefix) ? l.substr(prefix.size()) : l;
}

std::string outfit_of(const std::string& piece, const std::string& slot) {
    if (piece == slot) return {};
    if (piece.starts_with(slot)) {
        std::string rest = piece.substr(slot.size());
        while (!rest.empty() && rest.front() == '_') rest.erase(0, 1);
        return rest;
    }
    return piece;
}

// The pieces of a force grouped by the body part they cover (read off the node the mesh sits
// on), each part's alternatives by outfit.
struct ForceParts {
    Rig rig;
    std::map<std::string, LmaModel> loaded;
    std::map<std::string, std::map<std::string, const LmaModel*>> by_slot;
};

bool collect_force(const Data& data, const ForceSet& force, ForceParts& parts, std::string* error) {
    if (!force.bone.empty())
        if (auto bone = load_lma(data, Pack::Force, force.bone)) parts.rig.collect(*bone);
    for (const ForcePiece& piece : force.pieces)
        if (auto model = load_lma(data, Pack::Force, piece.key)) {
            parts.rig.collect(*model);
            parts.loaded.emplace(piece.key, std::move(*model));
        }
    if (parts.loaded.empty()) {
        if (error) *error = force.id + ": no readable pieces";
        return false;
    }
    for (const ForcePiece& piece : force.pieces) {
        auto it = parts.loaded.find(piece.key);
        if (it == parts.loaded.end() || it->second.meshes.empty()) continue;
        const LmaMesh& mesh = it->second.meshes.front();
        if (mesh.node >= it->second.nodes.size()) continue;
        const std::string slot = slot_of(it->second.nodes[mesh.node].name, force.prefix);
        parts.by_slot[slot][outfit_of(piece.name, slot)] = &it->second;
    }
    return !parts.by_slot.empty();
}

}  // namespace

std::vector<ForceSet> discover_forces(const Data& data) {
    // A folder and its pieces need not agree on a name (sf_c_delta holds sf_c_deltaforce_*,
    // sf_c_rokmc holds sf_c_kormarine_*), so the prefix is read off the rig file.
    // A piece may ship as .lma and, in a later patch, as .fxa too: the .lma is the one the
    // other pieces of its force were made with, so it wins; a force with only .fxa (ARTC, Force
    // Recon, Mulan, PSU) is read from those.
    std::map<std::string, std::map<std::string, std::string>> by_folder;   // folder -> leaf -> key
    // The client's own (sf_c_...), and a joined server's (x/<pack>/sf_c_...).
    for (const char* ext : {".fxa", ".lma"})
        for (const char* under : {"sf_c_", "x/"})
            for (const auto& [key, where] : data.keys(Pack::Force, under, ext)) {
                const size_t root = pack_prefix(key).size();
                if (!std::string_view(key).substr(root).starts_with("sf_c_")) continue;
                const size_t slash = key.find('/', root);
                if (slash == std::string::npos || key.find('/', slash + 1) != std::string::npos) continue;
                std::string leaf = lower(key.substr(slash + 1));
                if (auto dot = leaf.rfind('.'); dot != std::string::npos) leaf.resize(dot);
                by_folder[key.substr(0, slash)][leaf] = key;   // .lma, read second, replaces .fxa
            }
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> folders;
    for (auto& [folder, leaves] : by_folder)
        for (auto& [leaf, key] : leaves) folders[folder].emplace_back(leaf, key);
    std::vector<ForceSet> out;
    for (const auto& [folder, leaves] : folders) {
        ForceSet force;
        force.folder = folder;
        // A server's character goes by its whole folder: never taken for one of the client's.
        const std::string named = leaf_of(folder);
        force.id = folder.starts_with("x/") ? folder : named.starts_with("sf_c_") ? named.substr(5) : named;
        force.prefix = named + "_";
        for (const auto& [leaf, key] : leaves)
            if (leaf.ends_with("_bone")) {
                force.prefix = leaf.substr(0, leaf.size() - 4);
                force.bone = key;
                break;
            }
        for (const auto& [leaf, key] : leaves) {
            if (key == force.bone) continue;
            if (leaf.starts_with("sf_o_")) {
                const size_t split = leaf.find('_', 5);
                if (split == std::string::npos || leaf.ends_with("_point")) continue;
                force.accessories.emplace_back(leaf.substr(split + 1), key);
            } else if (leaf.starts_with(force.prefix)) {
                const std::string name = leaf.substr(force.prefix.size());
                if (name == "collisionmesh") continue;
                force.pieces.push_back({key, name});
            }
        }
        if (!force.pieces.empty()) out.push_back(std::move(force));
    }
    return out;
}

std::vector<std::string> force_outfits(const Data& data, const ForceSet& force) {
    ForceParts parts;
    std::set<std::string> outfits{""};
    if (collect_force(data, force, parts, nullptr))
        for (const auto& [slot, by_outfit] : parts.by_slot)
            for (const auto& [outfit, model] : by_outfit) outfits.insert(outfit);
    return {outfits.begin(), outfits.end()};
}

std::optional<Model> load_force(const Data& data, const ForceSet& force, std::string_view outfit, std::string* error) {
    return load_force(data, force, outfit, {}, error);
}

std::optional<Model> load_force(const Data& data, const ForceSet& force, std::string_view outfit, std::span<const std::string> worn, std::string* error) {
    const PackScope scope(force.folder);   // a server's character reads its own pack's textures by name (NM-3)
    ForceParts parts;
    if (!collect_force(data, force, parts, error)) return std::nullopt;
    // Parts worn: a piece of the force takes its slot; an accessory is read and joins the rig.
    std::map<std::string, const LmaModel*> slot_override;
    std::vector<LmaModel> accessories;
    for (const std::string& stem : worn) {
        bool found = false;
        for (const auto& [key, model] : parts.loaded) {
            if (lower(stem_of(key)) != stem || model.meshes.empty() || model.meshes.front().node >= model.nodes.size()) continue;
            slot_override[slot_of(model.nodes[model.meshes.front().node].name, force.prefix)] = &model;
            found = true;
            break;
        }
        if (found) continue;
        for (const auto& [name, key] : force.accessories)
            if (lower(stem_of(key)) == stem)
                if (auto acc = load_lma(data, Pack::Force, key)) {
                    parts.rig.collect(*acc);
                    accessories.push_back(std::move(*acc));
                    break;
                }
    }
    std::string why;
    if (!parts.rig.finish(kSfToCm, &why)) {
        if (error) *error = force.id + ": " + why;
        return std::nullopt;
    }
    Model model;
    model.bones = parts.rig.bones();
    Materials materials(data, Pack::Force, "");
    const std::string want = lower(outfit);
    for (const auto& [slot, by_outfit] : parts.by_slot) {
        if (auto o = slot_override.find(slot); o != slot_override.end()) {
            add_geometry(*o->second, parts.rig, kSfToCm, force.folder, materials, nullptr, model);
            continue;
        }
        auto piece = by_outfit.find(want);
        if (piece == by_outfit.end()) piece = by_outfit.find("");
        if (piece == by_outfit.end()) continue;
        add_geometry(*piece->second, parts.rig, kSfToCm, force.folder, materials, nullptr, model);
    }
    for (const LmaModel& acc : accessories) add_geometry(acc, parts.rig, kSfToCm, force.folder, materials, nullptr, model);
    if (model.meshes.empty()) {
        if (error) *error = force.id + ": no geometry";
        return std::nullopt;
    }
    for (const char* name : {"Bip01 Head", "Bip01 R Hand", "Bip01 L Hand", "Bip01 Spine2"})
        if (const int bone = parts.rig.find(name); bone >= 0) model.sockets.push_back({name, bone, Mat4::identity()});
    return model;
}

std::optional<Model> load_undead(const Data& data, std::string_view id, std::string* error) {
    const std::vector<ForceSet> sets = discover_forces(data);
    const ForceSet* set = nullptr;
    for (const ForceSet& s : sets)
        if (s.id == id) set = &s;
    if (!set) {
        if (error) *error = std::string(id) + ": no such folder";
        return std::nullopt;
    }
    ForceSet only = *set;
    std::erase_if(only.pieces, [](const ForcePiece& p) { return lower(p.key).find("not use") != std::string::npos; });
    ForceParts parts;
    if (!collect_force(data, only, parts, error)) return std::nullopt;
    std::string why;
    if (!parts.rig.finish(kSfToCm, &why)) {
        if (error) *error = std::string(id) + ": " + why;
        return std::nullopt;
    }
    Model model;
    model.bones = parts.rig.bones();
    Materials materials(data, Pack::Force, "");
    for (const ForcePiece& piece : only.pieces)
        if (auto it = parts.loaded.find(piece.key); it != parts.loaded.end()) add_geometry(it->second, parts.rig, kSfToCm, only.folder, materials, nullptr, model);
    // Their files carry each bone's shape too ("Bip01 Head", a box with no material of its own):
    // never drawn.
    std::erase_if(model.meshes, [&](const ModelMesh& m) { return m.material >= model.materials.size() || model.materials[m.material].texture.empty(); });
    if (model.meshes.empty()) {
        if (error) *error = std::string(id) + ": no geometry";
        return std::nullopt;
    }
    for (const char* name : {"Bip01 Head", "Bip01 R Hand", "Bip01 L Hand", "Bip01 Spine2"})
        if (const int bone = parts.rig.find(name); bone >= 0) model.sockets.push_back({name, bone, Mat4::identity()});
    return model;
}

std::optional<ModelAnimation> load_undead_motion(const Data& data, const Model& model, std::string_view set, std::string_view clip) {
    const std::string stem = "motion/" + lower(set) + "/" + lower(set) + "_" + lower(clip);
    std::optional<LmaModel> lma;
    for (const char* ext : {".fxm", ".lma"})
        if ((lma = load_lma(data, Pack::Force, stem + ext)) && !lma->motion.empty()) break;
    if (!lma || lma->motion.empty()) return std::nullopt;
    auto find = [&](std::string_view n) { return model.find_bone(n); };
    return convert_clip(*lma, find, kSfToCm, lower(clip), 1.0f);
}

std::map<std::string, std::string> character_motion_keys(const Data& data) {
    std::map<std::string, std::string> out;
    for (const auto& [key, where] : data.keys(Pack::Force, "motion/", ".lma")) {
        if (key.find('/', 7) != std::string::npos) continue;   // zombie sets live in subfolders
        out.emplace(stem_of(key), key);
    }
    return out;
}

std::optional<ModelAnimation> load_character_motion(const Data& data, const Model& model, std::string_view clip_name) {
    static std::mutex m;
    static std::map<std::string, float> speeds;
    static bool speeds_read = false;
    {
        std::lock_guard lock(m);
        if (!speeds_read) {
            speeds_read = true;
            for (const char* manifest : {"motion/total_motion.sfm", "motion/sf_game_motion.sfm", "motion/sf_lobby_motion.sfm"})
                for (const auto& [k, v] : read_clip_speeds(data, Pack::Force, manifest)) speeds.emplace(k, v);
        }
    }
    const std::string name = lower(clip_name);
    auto clip = load_lma(data, Pack::Force, "motion/" + name + ".lma");
    if (!clip || clip->motion.empty()) return std::nullopt;
    float speed = 1.0f;
    {
        std::lock_guard lock(m);
        if (auto it = speeds.find(name); it != speeds.end()) speed = it->second;
    }
    auto find = [&](std::string_view n) { return model.find_bone(n); };
    return convert_clip(*clip, find, kSfToCm, name, speed);
}

std::vector<bool> upper_body_mask(const Model& model) {
    std::vector<bool> upper(model.bones.size(), false);
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const std::string n = lower(model.bones[i].name);
        bool up = n.find("spine") != std::string::npos;
        const int p = model.bones[i].parent;
        if (p >= 0 && upper[size_t(p)]) up = true;
        upper[i] = up;
    }
    return upper;
}

std::optional<CarriedModel> load_carried(const Data& data, std::string_view stem_view, std::string* error) {
    const std::string stem = lower(stem_view);
    auto read_either = [&](const std::string& base) {
        auto m = load_lma(data, Pack::Force, base + ".lma");
        return m ? m : load_lma(data, Pack::Force, base + ".fxa");
    };
    auto gun = read_either("weapon/" + stem);
    auto point = read_either("point/" + stem + "_point");
    if (!gun || !point) {
        if (error) *error = stem + (gun ? ": no point file" : ": no model");
        return std::nullopt;
    }
    // The gun's place: <stem>_point under the right hand (any *_point node there, failing that name).
    const LmaNode* at = nullptr;
    for (const LmaNode& n : point->nodes) {
        const std::string name = lower(n.name);
        if (name == stem + "_point") at = &n;
        else if (!at && name.ends_with("_point") && lower(n.parent) == "bip01 r hand") at = &n;
    }
    if (!at) {
        if (error) *error = stem + ": the point file names no place in the hand";
        return std::nullopt;
    }
    CarriedModel out;
    out.attach = Mat4::scale(at->scale) * Mat4::from_quaternion(to_engine(at->rotation)) * Mat4::translation(to_engine(at->position) * kSfToCm);
    // A few hang their mesh on a node called <stem>_point (the Ltd. Beretta), which the geometry
    // pass passes over as a marker: renamed here so the gun is drawn.
    for (LmaNode& n : gun->nodes)
        for (std::string* s : {&n.name, &n.parent})
            if (lower(*s).ends_with("_point")) s->resize(s->size() - 6);
    Rig rig;
    rig.collect(*gun);
    std::string why;
    if (!rig.finish(kSfToCm, &why)) {
        if (error) *error = stem + ": " + why;
        return std::nullopt;
    }
    out.model.bones = rig.bones();
    Materials materials(data, Pack::Force, "");
    add_geometry(*gun, rig, kSfToCm, "weapon", materials, nullptr, out.model);
    if (out.model.meshes.empty()) {
        if (error) *error = stem + ": no geometry";
        return std::nullopt;
    }
    for (const char* name : {"flame", "cartridge"})
        if (const int bone = rig.find(name); bone >= 0) out.model.sockets.push_back({name, bone, Mat4::identity()});
    return out;
}

std::optional<Model> load_force_prop(const Data& data, std::string_view mesh_key, std::string_view skeleton_key, std::string* error) {
    auto read_either = [&](std::string_view key) {
        const std::string base = lower(key);
        auto m = load_lma(data, Pack::Force, base + ".lma");
        return m ? m : load_lma(data, Pack::Force, base + ".fxa");
    };
    auto mesh = read_either(mesh_key);
    if (!mesh) {
        if (error) *error = std::string(mesh_key) + ": no model";
        return std::nullopt;
    }
    Rig rig;
    if (!skeleton_key.empty())
        if (auto bones = read_either(skeleton_key)) rig.collect(*bones);
    rig.collect(*mesh);
    std::string why;
    if (!rig.finish(kSfToCm, &why)) {
        if (error) *error = std::string(mesh_key) + ": " + why;
        return std::nullopt;
    }
    Model out;
    out.bones = rig.bones();
    Materials materials(data, Pack::Force, "");
    add_geometry(*mesh, rig, kSfToCm, "weapon", materials, nullptr, out);
    if (out.meshes.empty()) {
        if (error) *error = std::string(mesh_key) + ": no geometry";
        return std::nullopt;
    }
    return out;
}

std::optional<Model> load_cartridge(const Data& data, std::string_view stem, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<Model> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    const std::string key = "cartridge/" + std::string(stem);
    const auto bytes = data.read(Pack::Weapon, key + ".fpd");
    if (!bytes) return fail(key + ".fpd is not in the weapon archives");
    // "1.0", padding to 0xFF, u32 index count, that many u16 indices, then the triangles already
    // laid out one vertex an index: position xyz and uv, 20 bytes each.
    const std::byte* p = bytes->data();
    const size_t size = bytes->size();
    if (size < 0x103 || std::memcmp(p, "1.0", 3) != 0) return fail(key + ".fpd: not a 1.0 file");
    u32 count = 0;
    std::memcpy(&count, p + 0xFF, 4);
    const size_t verts_at = 0x103 + size_t(count) * 2;
    if (count == 0 || count % 3 || verts_at + size_t(count) * 20 > size) return fail(key + ".fpd: " + std::to_string(count) + " indices do not fit");
    Model out;
    ModelBone root;
    root.name = "cartridge";
    out.bones.push_back(root);
    ModelMaterial mat;
    mat.texture = key + ".jpg";
    mat.where = data.resolve(Pack::Weapon, mat.texture);
    if (!mat.where) mat.texture.clear();
    out.materials.push_back(mat);
    ModelMesh mesh;
    mesh.name = "cartridge";
    mesh.vertices.resize(count);
    for (u32 i = 0; i < count; ++i) {
        float f[5];
        std::memcpy(f, p + verts_at + size_t(i) * 20, 20);
        ModelVertex& v = mesh.vertices[i];
        v = {};
        v.position[0] = f[0], v.position[1] = f[1], v.position[2] = f[2];
        v.uv[0] = f[3], v.uv[1] = f[4];
        v.weights[0] = 255;
        mesh.indices.push_back(i);
        mesh.bounds.add(Vec3(v.position));
    }
    // No normals are shipped: the case's side faces out from its axis (z), its ends along it.
    for (u32 t = 0; t < count; t += 3) {
        ModelVertex* tri = &mesh.vertices[t];
        const Vec3 a(tri[0].position), b(tri[1].position), c(tri[2].position);
        const Vec3 face = eng::normalize(eng::cross(b - a, c - a));
        for (int k = 0; k < 3; ++k) {
            Vec3 n = std::fabs(face.z) > 0.7f ? face : eng::normalize(Vec3{tri[k].position[0], tri[k].position[1], 0});
            if (eng::length_sq(n) < 1e-6f) n = face;
            std::memcpy(tri[k].normal, &n.x, 12);
        }
    }
    out.meshes.push_back(std::move(mesh));
    return out;
}

}  // namespace sf
