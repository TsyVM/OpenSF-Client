#include "Game/Render/FramePipe.hpp"

#include "Engine/Asset/ImageDecode.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Game/Render/Shaders.hpp"
#include "Game/Render/Upload.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace lsf {

namespace {

using eng::Format;
using eng::Vec3;

// Fidelity's finish is the player's to set (Settings.hpp FidelitySettings; its defaults are
// gentle on purpose: the picture is Soldier Front's, finished a little finer, not another
// game's). What is fixed is here, in the world's own units (centimetres).
constexpr float kAoBias = 2.0f, kAoPower = 1.3f;
constexpr float kShaftReach = 0.72f;   // how much of the way to the sun a shaft is smeared from
constexpr int kMaxLights = 24;         // the most lights a frame draws: the nearest and brightest
// The sun and the moon in the sky, as wide as a game's are (the real ones are half a degree).
constexpr float kSunRadius = 1.5f, kMoonRadius = 2.3f;       // degrees
constexpr float kSunCorona = 15.0f, kMoonCorona = 8.0f;      // degrees its corona reaches

void set4(float* d, float a, float b, float c, float e) { d[0] = a, d[1] = b, d[2] = c, d[3] = e; }
void set4(float* d, const Vec3& v, float w) { d[0] = v.x, d[1] = v.y, d[2] = v.z, d[3] = w; }

// Tonight's moon, as one of the twenty-four pictures of a month (0 new, 12 full): the real one's
// phase by the calendar, kept clear of the new moon's few dark nights so there is a moon to see.
int moon_picture() {
    using namespace std::chrono;
    const double days = duration<double>(system_clock::now().time_since_epoch()).count() / 86400.0;
    constexpr double kNewMoon = 10962.76;      // 6 January 2000, 18:14 UTC, in days since 1970
    constexpr double kMonth = 29.530588853;    // new moon to new moon
    double age = std::fmod(days - kNewMoon, kMonth);
    if (age < 0) age += kMonth;
    return std::clamp(int(age / kMonth * 24.0), 3, 20);
}

}  // namespace

bool FramePipe::init(eng::Device& device) {
    device_ = &device;
    std::string err;
    auto ps = [&](const char* entry, eng::ProgramRef& out) {
        out = device.create_program(shaders::post, "vs_full", entry, {}, &err);
        return out != nullptr;
    };
    if (!ps("ps_copy", ps_copy_)) {
        LOG_ERROR("Frame: the output shader did not compile: %s", err.c_str());
        return false;
    }
    // Fidelity's passes: without them the game is whole, so a card that will not take one only loses those.
    fidelity_ok_ = ps("ps_linearize", ps_linearize_) && ps("ps_ao", ps_ao_) && ps("ps_ao_blur", ps_ao_blur_) && ps("ps_ao_apply", ps_ao_apply_) &&
                   ps("ps_prefilter", ps_prefilter_) && ps("ps_down", ps_down_) && ps("ps_up", ps_up_) && ps("ps_final", ps_final_) &&
                   ps("ps_fxaa", ps_fxaa_) && ps("ps_shaft_mask", ps_shaft_mask_) && ps("ps_shaft_blur", ps_shaft_blur_);
    // Reading depth sample by sample needs multisampled textures in a shader (OpenGL ES 3.1 on a phone).
    if (fidelity_ok_ && device.samples_for(Format::RGBA8, 8) > 1) ps("ps_linearize_ms", ps_linearize_ms_);
    if (!fidelity_ok_) LOG_WARN("Frame: the finishing shaders did not compile (%s)", err.c_str());
    lights_ok_ = fidelity_ok_ && ps("ps_light", ps_light_) && ps("ps_sun_glint", ps_sun_glint_) && ps("ps_sky_body", ps_sky_body_) &&
                 ps("ps_ssr", ps_ssr_) && ps("ps_ssr_apply", ps_ssr_apply_);
    if (fidelity_ok_ && !lights_ok_) LOG_WARN("Frame: Fidelity's light shaders did not compile (%s)", err.c_str());
    cb_ = device.create_buffer(eng::BufferKind::Uniform, nullptr, sizeof(Constants), true);
    lin_ = device.sampler({eng::Filter::Linear, eng::Address::Clamp});
    pnt_ = device.sampler({eng::Filter::Point, eng::Address::Clamp});
    eng::BlendDesc add;
    add.enable = true;
    add.src = add.dst = eng::BlendFactor::One;
    add.src_alpha = add.dst_alpha = eng::BlendFactor::One;
    add_ = device.blend(add);
    // What is there, times what is drawn.
    eng::BlendDesc mul;
    mul.enable = true;
    mul.src = eng::BlendFactor::DestColour;
    mul.dst = eng::BlendFactor::Zero;
    mul.src_alpha = eng::BlendFactor::Zero;
    mul.dst_alpha = eng::BlendFactor::One;
    multiply_ = device.blend(mul);
    // Painted over what is there by its alpha.
    eng::BlendDesc over;
    over.enable = true;
    over.src = eng::BlendFactor::SrcAlpha;
    over.dst = eng::BlendFactor::InvSrcAlpha;
    over.src_alpha = eng::BlendFactor::Zero;
    over.dst_alpha = eng::BlendFactor::One;
    over_ = device.blend(over);
    return true;
}

void FramePipe::load_art(const std::filesystem::path& content_dir) {
    auto load = [&](const char* name, eng::TextureRef& out) {
        const std::filesystem::path path = content_dir / "fidelity" / name;
        const auto bytes = eng::fs::read_file(path);
        eng::Image img;
        std::string err;
        if (!bytes || !eng::decode_image(*bytes, img, &err)) {
            LOG_WARN("Frame: %s: %s", name, bytes ? err.c_str() : "not found (Fidelity's sky goes without it)");
            return;
        }
        out = upload_image(*device_, img, true);
    };
    load("corona.png", corona_);
    load("moon.png", moon_);
}

bool FramePipe::make(Target& t, int w, int h, Format format, int samples) {
    t = {};
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    eng::TextureDesc td;
    td.format = format;
    td.width = t.w;
    td.height = t.h;
    td.samples = samples;
    td.target = true;
    t.tex = device_->create_texture(td);
    return t.tex != nullptr;
}

bool FramePipe::make_targets(int w, int h, int samples) {
    const int made = device_->samples_for(Format::RGBA8, samples);
    bool ok = make(colour_, w, h, Format::RGBA8, made);
    if (made > 1) ok &= make(resolved_, w, h, Format::RGBA8);
    else resolved_ = {};
    Target depth;
    ok &= make(depth, w, h, Format::D24S8, made);
    depth_ = depth.tex;
    samples_asked_ = samples;
    samples_made_ = made;
    // Fidelity's are the picture's size: made again with it, when next wanted.
    for (Target* t : {&ldr_, &fxaa_, &lin_depth_, &ao_, &ao_blur_, &shaft_[0], &shaft_[1], &surf_, &albedo_, &surf_read_, &albedo_read_, &scene_copy_, &ssr_})
        *t = {};
    for (Target& b : bloom_) b = {};
    LOG_INFO("Frame: the picture is %dx%d, %d sample%s a pixel%s", w, h, made, made == 1 ? "" : "s", ok ? "" : " (FAILED)");
    return ok;
}

void FramePipe::make_fidelity_targets() {
    if (ldr_.tex) return;
    const int w = colour_.w, h = colour_.h;
    bool ok = make(ldr_, w, h, Format::RGBA8) && make(fxaa_, w, h, Format::RGBA8) && make(lin_depth_, w, h, Format::R32F) &&
              make(ao_, w / 2, h / 2, Format::R8) && make(ao_blur_, w / 2, h / 2, Format::R8);
    for (int k = 0; k < 5; ++k) ok &= make(bloom_[k], w >> (k + 1), h >> (k + 1), Format::R11G11B10F);
    for (Target& s : shaft_) ok &= make(s, w / 2, h / 2, Format::R11G11B10F);
    if (!ok) {
        LOG_WARN("Frame: the card would not make Fidelity's targets; it is off");
        fidelity_ok_ = false;
    }
}

bool FramePipe::make_surface_targets() {
    if (surf_.tex) return true;
    const int w = colour_.w, h = colour_.h;
    bool ok = make(surf_, w, h, Format::RGBA8, samples_made_) && make(albedo_, w, h, Format::RGBA8, samples_made_);
    if (samples_made_ > 1) ok = ok && make(surf_read_, w, h, Format::RGBA8) && make(albedo_read_, w, h, Format::RGBA8);
    else surf_read_ = surf_, albedo_read_ = albedo_;
    // Reflections read the picture so far: a multisampled one is resolved for it, a plain one copied.
    if (samples_made_ <= 1) ok = ok && make(scene_copy_, w, h, Format::RGBA8);
    ok = ok && make(ssr_, w / 2, h / 2, Format::RGBA8);
    if (!ok) {
        LOG_WARN("Frame: the card would not make Fidelity's surface targets; its lights are off");
        surf_ = albedo_ = surf_read_ = albedo_read_ = scene_copy_ = ssr_ = {};
        lights_ok_ = false;
    }
    return ok;
}

size_t FramePipe::gpu_bytes() const {
    size_t n = depth_ ? depth_->bytes() : 0;
    for (const Target* t : {&colour_, &resolved_, &ldr_, &fxaa_, &lin_depth_, &ao_, &ao_blur_, &shaft_[0], &shaft_[1], &surf_, &albedo_, &scene_copy_, &ssr_})
        if (t->tex) n += t->tex->bytes();
    if (samples_made_ > 1)
        for (const Target* t : {&surf_read_, &albedo_read_})
            if (t->tex) n += t->tex->bytes();
    for (const Target& b : bloom_)
        if (b.tex) n += b.tex->bytes();
    return n;
}

void FramePipe::bind_scene(bool surfaces) {
    eng::Device& dev = *device_;
    const eng::Target targets[3] = {{colour_.tex.get()}, {surf_.tex.get()}, {albedo_.tex.get()}};
    dev.set_targets(std::span<const eng::Target>(targets, surfaces ? 3 : 1), {depth_.get()});
    dev.set_viewport({0, 0, float(colour_.w), float(colour_.h)});
}

void FramePipe::begin(const FrameView& view, const eng::Color& clear) {
    view_ = view;
    view_.width = std::clamp(view.width, 16, 8192);
    view_.height = std::clamp(view.height, 16, 8192);
    const int want = std::clamp(view.samples, 1, 8);
    if (view_.width != colour_.w || view_.height != colour_.h || want != samples_asked_ || !colour_.tex) make_targets(view_.width, view_.height, want);
    surfaces_ = false;
    if (view_.fidelity && fidelity_ok_) {
        make_fidelity_targets();
        const FidelitySettings& f = view_.finish;
        // The surface targets serve the lights, the reflections and the water alike.
        const bool lights = f.lights && (f.lights_strength > 0 || f.shine > 0);
        if (fidelity_ok_ && lights_ok_ && (lights || f.reflections || f.water)) surfaces_ = make_surface_targets();
    }
    if (surfaces_) {
        // Nothing drawn: no surface, and nothing for a light to fall on (the sky).
        device_->unbind_targets();
        device_->clear({surf_.tex.get()}, {0, 0, 0, 0});
        device_->clear({albedo_.tex.get()}, {0, 0, 0, 0});
    }
    bind_scene(surfaces_);
    device_->clear({colour_.tex.get()}, clear);
    device_->clear_depth({depth_.get()}, 1.0f);
    drawing_ = true;
    depth_read_ = false;
    sun_seen_ = 0;
    lights_.clear();
    lights_drawn_ = 0;
}

void FramePipe::upload(int dst_w, int dst_h, int src_w, int src_h) {
    set4(cb_data_.dst, float(dst_w), float(dst_h), 1.0f / float(std::max(1, dst_w)), 1.0f / float(std::max(1, dst_h)));
    set4(cb_data_.src, float(src_w), float(src_h), 1.0f / float(std::max(1, src_w)), 1.0f / float(std::max(1, src_h)));
    device_->update_buffer(*cb_, &cb_data_, sizeof(cb_data_));
}

void FramePipe::quad_states() {
    eng::Device& dev = *device_;
    dev.set_vertices(nullptr, 0);
    dev.set_uniforms(0, cb_.get());
    dev.set_sampler(0, lin_);
    dev.set_sampler(1, pnt_);
    dev.set_raster(dev.raster_cull_none());
    dev.set_depth(dev.depth_none());
}

void FramePipe::pass(Target& out, const eng::ProgramRef& program, std::initializer_list<const eng::Texture*> inputs, const Target* src,
                     const eng::BlendState* blend) {
    eng::Device& dev = *device_;
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
    const eng::Target t{out.tex.get()};
    dev.set_targets(std::span<const eng::Target>(&t, 1));
    dev.set_viewport({0, 0, float(out.w), float(out.h)});
    set4(cb_data_.rect, 0, 0, 1, 1);
    upload(out.w, out.h, src ? src->w : out.w, src ? src->h : out.h);
    dev.set_blend(blend ? blend : dev.blend_opaque());
    dev.set_program(program.get());
    int slot = 0;
    for (const eng::Texture* v : inputs) dev.set_texture(slot++, v);
    dev.draw(eng::Topology::Triangles, 3);
}

bool FramePipe::rect_for(const Vec3& c, float radius, float tx, float ty) {
    const float W = float(colour_.w), H = float(colour_.h);
    float x0 = -1, x1 = 1, y0 = -1, y1 = 1;
    // A sphere the eye is not clear of covers the picture; one ahead, the box of it at its nearest.
    if (c.z - radius > 8.0f) {
        const float zn = c.z - radius;
        x0 = std::max(-1.0f, (c.x - radius) / (zn * tx)), x1 = std::min(1.0f, (c.x + radius) / (zn * tx));
        y0 = std::max(-1.0f, (c.y - radius) / (zn * ty)), y1 = std::min(1.0f, (c.y + radius) / (zn * ty));
        if (x0 >= x1 || y0 >= y1) return false;
    }
    const float px0 = std::floor((x0 * 0.5f + 0.5f) * W), px1 = std::ceil((x1 * 0.5f + 0.5f) * W);
    const float py0 = std::floor((0.5f - y1 * 0.5f) * H), py1 = std::ceil((0.5f - y0 * 0.5f) * H);
    if (px1 - px0 < 1 || py1 - py0 < 1) return false;
    device_->set_viewport({px0, py0, px1 - px0, py1 - py0});
    set4(cb_data_.rect, px0 / W, py0 / H, (px1 - px0) / W, (py1 - py0) / H);
    return true;
}

// What shines mirrors the picture so far. Leaves the picture bound again, the viewport over all of it.
void FramePipe::draw_reflections() {
    eng::Device& dev = *device_;
    Target* scene = &scene_copy_;
    if (samples_made_ > 1) {
        dev.unbind_targets();
        dev.resolve(*resolved_.tex, *colour_.tex);
        scene = &resolved_;
    } else {
        set4(cb_data_.grade, 1, 1, 0, 0);   // a plain copy: no sharpening
        pass(scene_copy_, ps_copy_, {colour_.tex.get()}, &colour_);
    }
    cb_data_.counts[2] = 1.0f;
    pass(ssr_, ps_ssr_, {scene->tex.get(), lin_depth_.tex.get(), surf_read_.tex.get()}, scene);
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
    bind_scene(false);
    set4(cb_data_.rect, 0, 0, 1, 1);
    upload(colour_.w, colour_.h, ssr_.w, ssr_.h);
    dev.set_blend(over_);
    dev.set_program(ps_ssr_apply_.get());
    dev.set_texture(0, ssr_.tex.get());
    dev.set_texture(1, lin_depth_.tex.get());
    dev.set_texture(2, surf_read_.tex.get());
    dev.draw(eng::Topology::Triangles, 3);
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
}

void FramePipe::draw_lights(const Camera& cam, const PipeSky& sky) {
    eng::Device& dev = *device_;
    const FidelitySettings& f = view_.finish;
    if (!f.lights) return;
    const float tx = cb_data_.proj[0], ty = cb_data_.proj[1];
    // The lights in view, the ones that matter most first: bright, wide, near.
    struct Pick {
        const PipeLight* light;
        Vec3 at;       // in view space
        float weight;
    };
    std::vector<Pick> picks;
    picks.reserve(lights_.size());
    for (const PipeLight& l : lights_) {
        const Vec3 at = cam.view.transform_point(l.position);
        const float r = l.radius;
        if (r <= 1 || at.z + r < cam.znear) continue;
        const float zf = std::max(at.z + r, 1.0f);
        if (std::fabs(at.x) - r > zf * tx || std::fabs(at.y) - r > zf * ty) continue;
        const float bright = std::max(l.colour.x, std::max(l.colour.y, l.colour.z));
        picks.push_back({&l, at, bright * r * r / std::max(eng::dot(at, at), r * r * 0.05f)});
    }
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.weight > b.weight; });
    if (picks.size() > size_t(kMaxLights)) picks.resize(size_t(kMaxLights));

    dev.set_blend(add_);
    dev.set_texture(0, lin_depth_.tex.get());
    dev.set_texture(1, surf_read_.tex.get());
    dev.set_texture(2, albedo_read_.tex.get());
    if (f.lights_strength > 0 && !picks.empty()) {
        dev.set_program(ps_light_.get());
        for (const Pick& p : picks) {
            if (!rect_for(p.at, p.light->radius, tx, ty)) continue;
            set4(cb_data_.light_pos, p.at, p.light->radius);
            set4(cb_data_.light_col, p.light->colour * f.lights_strength, p.light->pool);
            set4(cb_data_.light_aux, f.shine, 1, 0, 0);
            upload(colour_.w, colour_.h, colour_.w, colour_.h);
            dev.draw(eng::Topology::Triangles, 3);
            ++lights_drawn_;
        }
        lights_peak_ = std::max(lights_peak_, lights_drawn_);
    }
    // The map's own light, glinting off what shines where the bake says it falls.
    if (f.shine > 0) {
        dev.set_viewport({0, 0, float(colour_.w), float(colour_.h)});
        set4(cb_data_.rect, 0, 0, 1, 1);
        set4(cb_data_.sun_v, cam.view.transform_vector(eng::normalize(sky.light_travel * -1.0f)), f.shine);
        set4(cb_data_.sun_col, sky.light_colour, 0);
        upload(colour_.w, colour_.h, colour_.w, colour_.h);
        dev.set_program(ps_sun_glint_.get());
        dev.draw(eng::Topology::Triangles, 3);
    }
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
}

void FramePipe::draw_sky_body(const Camera& cam, const PipeSky& sky) {
    eng::Device& dev = *device_;
    const FidelitySettings& f = view_.finish;
    const Vec3 toward = cam.view.transform_vector(eng::normalize(sky.body_toward));
    if (toward.z <= 0.05f) return;   // behind the eye
    const bool moon = sky.night;
    const float disc = std::tan((moon ? kMoonRadius : kSunRadius) * eng::kDegToRad);
    const float reach = std::tan((moon ? kMoonCorona : kSunCorona) * eng::kDegToRad);
    // The whole of it, corona and all, as a sphere a way off.
    if (!rect_for(toward * 1000.0f, 1000.0f * reach * 1.45f, cb_data_.proj[0], cb_data_.proj[1])) return;
    Vec3 up = cam.view.transform_vector({0, 1, 0});
    if (std::fabs(eng::dot(up, toward)) > 0.98f) up = {1, 0, 0};
    set4(cb_data_.sun_v, toward, 0);
    set4(cb_data_.sun_up, up, 0);
    const bool corona = corona_ != nullptr && f.corona > 0;
    set4(cb_data_.body, disc, reach, corona ? f.corona * (moon ? 0.5f : 1.0f) : 0.0f, 0);
    if (moon) {
        // Pale, a little warm, as the moon is; its picture when there is one, else a plain disc.
        set4(cb_data_.body_col, Vec3{0.95f, 0.95f, 0.86f}, moon_ ? 1.0f : 0.0f);
        const int cell = moon_picture();
        set4(cb_data_.body_uv, float(cell % 4) * 0.25f, float(cell / 4) / 6.0f, 0.25f, 1.0f / 6.0f);
    } else {
        // The sun in the colour of the map's own light, most of the way to white.
        const float top = std::max(0.05f, std::max(sky.light_colour.x, std::max(sky.light_colour.y, sky.light_colour.z)));
        const Vec3 tone = sky.light_colour * (1.0f / top);
        set4(cb_data_.body_col, Vec3{1, 1, 1} * 0.6f + tone * 0.4f, 0);
    }
    upload(colour_.w, colour_.h, colour_.w, colour_.h);
    dev.set_blend(add_);
    dev.set_program(ps_sky_body_.get());
    dev.set_texture(0, lin_depth_.tex.get());
    dev.set_texture(1, corona ? corona_.get() : lin_depth_.tex.get());
    dev.set_texture(2, moon && moon_ ? moon_.get() : lin_depth_.tex.get());
    dev.draw(eng::Topology::Triangles, 3);
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
}

void FramePipe::shade(const Camera& cam, const PipeSky& sky) {
    if (!drawing_ || !view_.fidelity || !fidelity_ok_ || !lin_depth_.tex) return;
    const FidelitySettings& f = view_.finish;
    // Where the map's sun stands on the picture, and how squarely it is faced (the shafts' source).
    if (f.shafts && f.shafts_strength > 0) {
        const Vec3 to_sun = eng::normalize(sky.body_toward);
        const Vec3 at = cam.eye + to_sun * 10000.0f;
        const eng::Vec4 clip = cam.view_proj.transform({at.x, at.y, at.z, 1.0f});
        const float facing = eng::dot(cam.forward(), to_sun);
        if (clip.w > 1.0f && facing > 0.15f) {
            sun_u_ = clip.x / clip.w * 0.5f + 0.5f;
            sun_v_ = 0.5f - clip.y / clip.w * 0.5f;
            sun_seen_ = std::clamp((facing - 0.15f) / 0.45f, 0.0f, 1.0f);
        }
    }
    const bool contact = f.shade > 0 && f.shade_strength > 0;
    const bool body = f.sun && lights_ok_;
    // All of these read the depth; with none of them, nothing to do here.
    if (!contact && !surfaces_ && !body && sun_seen_ <= 0) return;
    eng::Device& dev = *device_;
    const float fov_y = 2.0f * std::atan(std::tan(cam.fov_x * 0.5f * eng::kDegToRad) / (16.0f / 9.0f));
    const float ty = std::tan(fov_y * 0.5f);
    set4(cb_data_.proj, ty * aspect(), ty, cam.znear, cam.zfar);
    set4(cb_data_.ao, f.shade_radius, f.shade_strength, kAoBias, kAoPower);
    set4(cb_data_.counts, f.shade >= 3 ? 20.0f : f.shade == 2 ? 12.0f : 8.0f, kShaftReach, 0, 0);
    // The view's axes in the world: what turns a surface's normal into the view's own space.
    const eng::Mat4& v = cam.view;
    set4(cb_data_.cam_r, v.m[0][0], v.m[1][0], v.m[2][0], 0);
    set4(cb_data_.cam_u, v.m[0][1], v.m[1][1], v.m[2][1], 0);
    set4(cb_data_.cam_f, v.m[0][2], v.m[1][2], v.m[2][2], 0);
    quad_states();
    dev.unbind_targets();
    bool depth_ok = true;
    if (samples_made_ > 1) {
        if (ps_linearize_ms_) {
            // The multisampled depth is read sample by sample (slot 4).
            dev.set_texture(4, depth_.get());
            pass(lin_depth_, ps_linearize_ms_, {}, nullptr);
            dev.set_texture(4, nullptr);
        } else {
            depth_ok = false;
        }
    } else {
        pass(lin_depth_, ps_linearize_, {depth_.get()}, nullptr);
    }
    depth_read_ = depth_ok;
    if (depth_ok && surfaces_ && samples_made_ > 1) {
        dev.unbind_targets();
        dev.resolve(*surf_read_.tex, *surf_.tex);
        dev.resolve(*albedo_read_.tex, *albedo_.tex);
    }
    if (depth_ok && contact) {
        pass(ao_, ps_ao_, {lin_depth_.tex.get()}, &lin_depth_);
        pass(ao_blur_, ps_ao_blur_, {ao_.tex.get(), lin_depth_.tex.get()}, &ao_);
    }
    // Onto the picture so far, and back to drawing it: the picture alone from here.
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
    bind_scene(false);
    set4(cb_data_.rect, 0, 0, 1, 1);
    if (depth_ok && contact) {
        upload(colour_.w, colour_.h, ao_blur_.w, ao_blur_.h);
        dev.set_blend(multiply_);
        dev.set_program(ps_ao_apply_.get());
        dev.set_texture(0, ao_blur_.tex.get());
        dev.draw(eng::Topology::Triangles, 3);
        dev.set_texture(0, nullptr);
    }
    if (depth_ok && surfaces_) draw_lights(cam, sky);
    if (depth_ok && surfaces_ && f.reflections) draw_reflections();
    if (depth_ok && body) draw_sky_body(cam, sky);
    dev.set_viewport({0, 0, float(colour_.w), float(colour_.h)});
    dev.set_blend(dev.blend_opaque());
    dev.set_depth(dev.depth(eng::DepthDesc{}));
}

void FramePipe::end() {
    if (!drawing_) return;
    drawing_ = false;
    eng::Device& dev = *device_;
    quad_states();
    Target* scene = &colour_;
    if (samples_made_ > 1 && resolved_.tex) {
        dev.unbind_targets();
        dev.resolve(*resolved_.tex, *colour_.tex);
        scene = &resolved_;
    }
    float sharpen = 0;
    if (view_.fidelity && fidelity_ok_ && ldr_.tex) {
        const FidelitySettings& f = view_.finish;
        const bool glow = f.glow && f.glow_strength > 0;
        const bool shafts = f.shafts && f.shafts_strength > 0 && sun_seen_ > 0 && depth_read_;
        // A narrow knee: the art is mostly under the threshold, so only the sky, lamps and flashes glow.
        set4(cb_data_.bloom, f.glow_threshold, f.glow_threshold * 0.12f, glow ? f.glow_strength : 0.0f, 0);
        set4(cb_data_.grade, f.saturation, f.contrast, f.vignette, 0);
        set4(cb_data_.finish, float(f.curve), f.dither ? 1.0f : 0.0f, f.exposure, 0);
        set4(cb_data_.shaft, sun_u_, sun_v_, shafts ? f.shafts_strength * sun_seen_ : 0.0f, 0);
        if (glow) {
            pass(bloom_[0], ps_prefilter_, {scene->tex.get()}, scene);
            for (int k = 1; k < 5; ++k) pass(bloom_[k], ps_down_, {bloom_[k - 1].tex.get()}, &bloom_[k - 1]);
            for (int k = 4; k > 0; --k) pass(bloom_[k - 1], ps_up_, {bloom_[k].tex.get()}, &bloom_[k], add_);
        } else {
            // The finish still reads it: black, so nothing is added.
            dev.unbind_targets();
            dev.clear({bloom_[0].tex.get()}, {0, 0, 0, 1});
        }
        if (shafts) {
            cb_data_.counts[1] = kShaftReach;
            pass(shaft_[0], ps_shaft_mask_, {scene->tex.get(), lin_depth_.tex.get()}, scene);
            pass(shaft_[1], ps_shaft_blur_, {shaft_[0].tex.get()}, &shaft_[0]);
            cb_data_.counts[1] = kShaftReach / 12.0f;
            pass(shaft_[0], ps_shaft_blur_, {shaft_[1].tex.get()}, &shaft_[1]);
        } else {
            dev.unbind_targets();
            dev.clear({shaft_[0].tex.get()}, {0, 0, 0, 1});
        }
        pass(ldr_, ps_final_, {scene->tex.get(), bloom_[0].tex.get(), shaft_[0].tex.get()}, scene);
        scene = &ldr_;
        if (f.smooth_edges) {
            pass(fxaa_, ps_fxaa_, {ldr_.tex.get()}, &ldr_);
            scene = &fxaa_;
        }
        sharpen = f.sharpen;
    }
    // Into the window, where the picture goes.
    for (int slot = 0; slot < 3; ++slot) dev.set_texture(slot, nullptr);
    const eng::Target back{dev.back_buffer()};
    dev.set_targets(std::span<const eng::Target>(&back, 1));
    const float w = view_.w > 0 ? view_.w : float(dev.width()), h = view_.h > 0 ? view_.h : float(dev.height());
    dev.set_viewport({std::floor(view_.x), std::floor(view_.y), std::floor(w), std::floor(h)});
    set4(cb_data_.grade, 1, 1, 0, sharpen);
    set4(cb_data_.rect, 0, 0, 1, 1);
    upload(int(w), int(h), scene->w, scene->h);
    dev.set_blend(dev.blend_opaque());
    dev.set_program(ps_copy_.get());
    dev.set_texture(0, scene->tex.get());
    dev.draw(eng::Topology::Triangles, 3);
    dev.set_texture(0, nullptr);
    dev.set_viewport({0, 0, float(dev.width()), float(dev.height())});
    dev.set_depth(dev.depth(eng::DepthDesc{}));
    dev.set_raster(dev.raster(eng::RasterDesc{}));
}

}  // namespace lsf
