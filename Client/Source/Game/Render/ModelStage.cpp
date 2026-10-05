#include "Game/Render/ModelStage.hpp"

#include "Game/App.hpp"
#include "Game/Items.hpp"
#include "Game/Rules.hpp"

#include "Engine/Core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace lsf {

using eng::Mat4;
using eng::Vec3;

namespace {

// The lobby's own character-viewer clips first (Soldier Front posed its soldiers with these),
// then the rifle stance the match stands them in.
constexpr const char* kPoses[] = {"a_interface_01", "u_01_wait"};
// Three-quarters on. The menus' camera sees a model's back at no turn at all, so its front is a
// half turn round, and three-quarters a little past that.
constexpr float kThreeQuarter = 180.0f + 28.0f;
// Stills are taken this far into the pose clip (its first frames can be a settle).
constexpr float kPhotoTime = 0.6f;
constexpr int kPhotoW = 256, kPhotoH = 384;
constexpr int kLiveW = 512, kLiveH = 768;

}  // namespace

ModelStage::ModelStage(App& app) : app_(app) { thread_ = std::thread([this] { load_thread(); }); }

ModelStage::~ModelStage() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool ModelStage::ready() {
    if (!renderer_tried_) {
        renderer_tried_ = true;
        eng::Device& d = app_.device();
        renderer_ok_ = renderer_.init(d);
        renderer_.textures().init(&d, &app_.data());
        samples_ = std::min(d.samples_for(eng::Format::RGBA8, 4), d.samples_for(eng::Format::D32F, 4));
        if (!renderer_ok_) LOG_WARN("Model stage: the renderer could not start; the menus show no soldiers");
    }
    return renderer_ok_;
}

std::string ModelStage::look_key(u8 force, std::span<const u16> parts) {
    std::vector<u16> sorted(parts.begin(), parts.end());
    std::sort(sorted.begin(), sorted.end());
    std::string key = std::to_string(force);
    for (u16 p : sorted) key += "," + std::to_string(p);
    return key;
}

void ModelStage::request(const std::string& key, u8 force, std::span<const u16> parts) {
    std::lock_guard lock(mutex_);
    if (asked_[key]) return;
    asked_[key] = true;
    looks_[key] = {force, {parts.begin(), parts.end()}};
    queue_.push_back(key);
    wake_.notify_one();
}

bool ModelStage::loading(u8 force, std::span<const u16> parts) const {
    std::lock_guard lock(mutex_);
    auto it = soldiers_.find(look_key(force, parts));
    return it == soldiers_.end() || (!it->second.gpu && !it->second.failed);
}

void ModelStage::load_thread() {
    std::vector<sf::ForceSet> sets;
    for (;;) {
        std::string key;
        Look look;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            key = queue_.front();
            queue_.pop_front();
            look = looks_[key];
        }
        if (sets.empty()) sets = sf::discover_forces(app_.data());
        Loaded out;
        std::vector<std::string> worn;
        for (u16 id : look.parts)
            if (const ItemDef* d = item(id); d && d->kind == ItemKind::Part && d->force == look.force) worn.push_back(sf::lower(d->model));
        if (const ForceDef* def = force(look.force)) {
            for (const sf::ForceSet& s : sets) {
                if (s.id != def->model) continue;
                std::string err;
                if (auto m = sf::load_force(app_.data(), s, "", worn, &err)) out.model = std::make_shared<sf::Model>(std::move(*m));
                else LOG_WARN("Model stage: %s: %s", def->name, err.c_str());
                break;
            }
            if (!out.model) LOG_WARN("Model stage: no model for %s", def->name);
        }
        if (out.model) {
            std::vector<std::string> poses;
            if (!app_.options().pose.empty()) poses.push_back(app_.options().pose);
            poses.insert(poses.end(), std::begin(kPoses), std::end(kPoses));
            for (const std::string& clip : poses)
                if (auto a = sf::load_character_motion(app_.data(), *out.model, clip)) {
                    out.pose = std::make_shared<sf::ModelAnimation>(std::move(*a));
                    break;
                }
        }
        std::lock_guard lock(mutex_);
        loaded_[key] = std::move(out);
    }
}

void ModelStage::forget_oldest(const std::string& keep) {
    // Under mutex_.
    while (soldiers_.size() > kKeepLooks) {
        auto oldest = soldiers_.end();
        for (auto it = soldiers_.begin(); it != soldiers_.end(); ++it)
            if (it->first != keep && it->first.find(',') != std::string::npos && (oldest == soldiers_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == soldiers_.end()) return;   // only bare looks left
        asked_.erase(oldest->first);             // asked for again, it loads again
        looks_.erase(oldest->first);
        soldiers_.erase(oldest);                 // the device buries its buffers once the frames using them are done
    }
}

ModelStage::Soldier* ModelStage::soldier(const std::string& id) {
    {
        std::lock_guard lock(mutex_);
        if (auto it = soldiers_.find(id); it != soldiers_.end()) {
            it->second.used = app_.now();
            return it->second.gpu ? &it->second : nullptr;
        }
    }
    Loaded got;
    {
        std::lock_guard lock(mutex_);
        auto it = loaded_.find(id);
        if (it == loaded_.end()) return nullptr;
        got = std::move(it->second);
        loaded_.erase(it);
    }
    // Onto the card, here on the main thread.
    Soldier s;
    s.model = std::move(got.model);
    s.pose = std::move(got.pose);
    if (s.model) {
        s.gpu = renderer_.upload_model(*s.model, sf::Pack::Force);
        // Framed by where the posed soldier really is: the bind pose's box is a different size
        // from one force to the next (and cuts some heads off), so skin the vertices once here.
        std::vector<Mat4> skin;
        pose(s, kPhotoTime, skin);
        const auto& meshes = s.model->meshes;
        for (size_t i = 0; i < meshes.size(); ++i) {
            // Only what is drawn: a part with no texture is skipped by the renderer.
            if (s.gpu && s.gpu->parts.size() == meshes.size() && (s.gpu->parts[i].hidden || !s.gpu->parts[i].tex)) continue;
            for (const sf::ModelVertex& v : meshes[i].vertices) {
                const Vec3 p(v.position);
                if (skin.empty()) {
                    s.box.add(p);
                    continue;
                }
                Vec3 q{0, 0, 0};
                float total = 0;
                for (int k = 0; k < 4; ++k)
                    if (v.weights[k] && v.bones[k] < skin.size()) {
                        const float w = v.weights[k] / 255.0f;
                        q = q + skin[v.bones[k]].transform_point(p) * w;
                        total += w;
                    }
                s.box.add(total > 0 ? q * (1.0f / total) : p);
            }
        }
    }
    s.failed = !s.gpu || !s.box.valid();
    s.used = app_.now();
    std::lock_guard lock(mutex_);
    Soldier& kept = soldiers_[id] = std::move(s);
    forget_oldest(id);
    return kept.failed ? nullptr : &kept;
}

void ModelStage::pose(const Soldier& s, float t, std::vector<Mat4>& skin) const {
    const sf::Model& m = *s.model;
    std::vector<Mat4> local(m.bones.size()), model_space;
    for (size_t i = 0; i < m.bones.size(); ++i) local[i] = m.bones[i].bind_local;
    if (s.pose) sf::sample_animation(m, *s.pose, t, true, local);
    sf::pose_to_model(m, local, model_space);
    sf::skin_matrices(m, model_space, skin);
}

ModelStage::Shot& ModelStage::shot_for(std::map<std::string, Shot>& shots, const std::string& id, int w, int h) {
    Shot& s = shots[id];
    if (s.picture && s.w == w && s.h == h) return s;
    eng::Device& d = app_.device();
    eng::TextureDesc cd;
    cd.format = eng::Format::RGBA8;
    cd.width = w;
    cd.height = h;
    cd.target = true;
    s.picture = d.create_texture(cd);
    if (samples_ > 1) {
        cd.samples = samples_;
        s.msaa = d.create_texture(cd);
    }
    eng::TextureDesc dd = cd;
    dd.format = eng::Format::D32F;
    dd.samples = samples_ > 1 ? samples_ : 1;
    s.depth = d.create_texture(dd);
    s.w = w;
    s.h = h;
    s.drawn = false;
    return s;
}

void ModelStage::draw(Soldier& s, Shot& shot, float yaw, double time) {
    if (!shot.picture || !shot.depth || (samples_ > 1 && !shot.msaa)) return;
    eng::Device& d = app_.device();
    const eng::Device::Targets saved = d.targets();
    const eng::Device::Viewport saved_vp = d.viewport();

    const eng::Target colour{shot.msaa ? shot.msaa.get() : shot.picture.get()};
    const eng::Target depth{shot.depth.get()};
    d.set_targets(std::span<const eng::Target>(&colour, 1), depth);
    d.set_viewport({0, 0, float(shot.w), float(shot.h)});
    d.clear(colour, {0, 0, 0, 0});
    d.clear_depth(depth);

    std::vector<Mat4> skin;
    pose(s, float(time), skin);

    // Framed whole, head to boots, whichever way it faces: the camera stands off on +Z (the way
    // the models face) far enough for the height and for the widest turn.
    const Vec3 lo = s.box.min, hi = s.box.max;
    const Vec3 centre = (lo + hi) * 0.5f;
    const float tall = (hi.y - lo.y) * 1.08f;
    const float wide = std::max(hi.x - lo.x, hi.z - lo.z) * 1.08f;
    const float aspect = float(shot.w) / float(shot.h);
    Camera cam;
    cam.fov_x = 30.0f;
    const float tan_v = std::tan(cam.fov_x * 0.5f * eng::kDegToRad) / (16.0f / 9.0f);
    const float tan_h = tan_v * aspect;
    const float dist = std::max(tall * 0.5f / tan_v, wide * 0.5f / tan_h) + (hi.z - lo.z) * 0.5f;
    cam.eye = {centre.x, centre.y, centre.z + dist};
    cam.yaw = 180.0f;
    cam.pitch = 0.0f;
    cam.znear = std::max(1.0f, dist * 0.05f);
    cam.zfar = dist * 4.0f;
    cam.update(aspect);

    // A catalogue light: a key from the front left and above, a generous fill.
    FrameLight light;
    light.sun_direction = {0.45f, -0.5f, -0.75f};
    light.sun_colour = {0.85f, 0.83f, 0.8f};
    light.ambient = {0.62f, 0.62f, 0.64f};
    light.exposure = 1.0f;
    renderer_.begin_frame(cam, light, time);
    const Mat4 world = Mat4::translation({-centre.x, 0, -centre.z}) * Mat4::yaw(kThreeQuarter + yaw) * Mat4::translation({centre.x, 0, centre.z});
    renderer_.draw_model(*s.gpu, world, &skin);

    d.unbind_targets();
    if (shot.msaa) d.resolve(*shot.picture, *shot.msaa);
    d.shader_read(*shot.picture);
    d.set_targets(saved);
    d.set_viewport(saved_vp);
}

ui::Picture ModelStage::photo(u8 force) {
    if (!ready()) return {};
    const std::string id = look_key(force, {});
    request(id, force, {});
    Soldier* s = soldier(id);
    if (!s) return {};
    Shot& shot = shot_for(photos_, id, kPhotoW, kPhotoH);
    if (!shot.drawn) {
        draw(*s, shot, 0.0f, kPhotoTime);
        shot.drawn = true;
    }
    return {shot.picture->ui_id(), float(shot.w), float(shot.h)};
}

ui::Picture ModelStage::live(u8 force, float yaw, std::span<const u16> parts) {
    if (!ready()) return {};
    const std::string id = look_key(force, parts);
    request(id, force, parts);
    Soldier* s = soldier(id);
    if (!s) {
        // The new look is on its way: the soldier as he was, meanwhile, rather than nothing.
        if (!parts.empty()) return live(force, yaw, {});
        return {};
    }
    // One turning picture a force: the look drawn into it is whichever was asked for last.
    Shot& shot = shot_for(lives_, std::to_string(force), kLiveW, kLiveH);
    draw(*s, shot, yaw, app_.now());
    shot.drawn = true;
    return {shot.picture->ui_id(), float(shot.w), float(shot.h)};
}

}  // namespace lsf
