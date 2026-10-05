#include "Game/Render/WorldRenderer.hpp"

#include "Game/Render/Shaders.hpp"
#include "Game/Render/Upload.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsf {

using eng::Mat4;
using eng::Vec3;

void Camera::update(float aspect) {
    const Vec3 f = forward();
    view = Mat4::look_to_lh(eye, f, Vec3{0, 1, 0});
    // The horizontal field is quoted at 16:9; the vertical follows from it so a wider screen
    // sees more to the sides rather than less above.
    const float fov_y = 2.0f * std::atan(std::tan(fov_x * 0.5f * eng::kDegToRad) / (16.0f / 9.0f));
    proj = Mat4::perspective_lh(fov_y, aspect, znear, zfar);
    view_proj = view * proj;
}

// ── Textures ───────────────────────────────────────────────────────────────────

void TextureCache::init(eng::Device* device, const sf::Data* data) {
    device_ = device;
    data_ = data;
    const eng::u8 px[4] = {255, 255, 255, 255};
    eng::Image img;
    img.width = img.height = 1;
    img.rgba.assign(px, px + 4);
    white_.tex = upload_image(*device_, img, false);
}

const TexInfo& TextureCache::white() { return white_; }

const TexInfo& TextureCache::get(sf::Pack pack, const std::optional<sf::AssetLocation>& where, const std::string& key) {
    const std::string id = std::string(sf::pack_folder(pack)) + "|" + key;
    if (auto it = cache_.find(id); it != cache_.end()) return it->second;
    TexInfo info;
    if (device_ && data_) {
        std::optional<std::vector<std::byte>> bytes;
        if (where) bytes = data_->read(pack, *where);
        else if (!key.empty()) bytes = data_->read(pack, key);
        sf::Texture t;
        std::string err;
        if (bytes && sf::load_texture(*bytes, t, true, &err)) {
            info.tex = upload_texture(*device_, t);
            info.cutout = t.cutout;
            info.translucent = t.translucent;
            info.invisible = t.invisible;
        } else if (!key.empty()) {
            LOG_WARN("Texture %s: %s", key.c_str(), bytes ? err.c_str() : "not found");
        }
    }
    return cache_.emplace(id, std::move(info)).first->second;
}

const TexInfo& TextureCache::get_mask(sf::Pack pack, const std::string& key) {
    const std::string id = std::string(sf::pack_folder(pack)) + "|mask|" + key;
    if (auto it = cache_.find(id); it != cache_.end()) return it->second;
    TexInfo info;
    if (device_ && data_) {
        const auto bytes = data_->read(pack, key);
        eng::Image img;
        std::string err;
        if (bytes && sf::decode_image(*bytes, img, &err)) {
            for (size_t p = 0; p + 3 < img.rgba.size(); p += 4) {
                const u8 a = std::max(img.rgba[p], std::max(img.rgba[p + 1], img.rgba[p + 2]));
                img.rgba[p] = img.rgba[p + 1] = img.rgba[p + 2] = 255;
                img.rgba[p + 3] = a;
            }
            info.tex = upload_image(*device_, img, true);
            info.translucent = true;
        } else {
            LOG_WARN("Texture %s: %s", key.c_str(), bytes ? err.c_str() : "not found");
        }
    }
    return cache_.emplace(id, std::move(info)).first->second;
}

const TexInfo& TextureCache::get_keyed(sf::Pack pack, const std::string& key) {
    const std::string id = std::string(sf::pack_folder(pack)) + "|keyed|" + key;
    if (auto it = cache_.find(id); it != cache_.end()) return it->second;
    TexInfo info;
    if (device_ && data_) {
        const auto bytes = data_->read(pack, key);
        eng::Image img;
        std::string err;
        if (bytes && sf::decode_image(*bytes, img, &err)) {
            for (size_t p = 0; p + 3 < img.rgba.size(); p += 4) {
                const int bright = std::max<int>(img.rgba[p], std::max(img.rgba[p + 1], img.rgba[p + 2]));
                img.rgba[p + 3] = u8(std::min(int(img.rgba[p + 3]), std::clamp((bright - 10) * 5, 0, 255)));
            }
            info.tex = upload_image(*device_, img, true);
            info.translucent = true;
        } else {
            LOG_WARN("Texture %s: %s", key.c_str(), bytes ? err.c_str() : "not found");
        }
    }
    return cache_.emplace(id, std::move(info)).first->second;
}

// ── Renderer ───────────────────────────────────────────────────────────────────

bool WorldRenderer::init(eng::Device& device) {
    device_ = &device;
    using eng::AttrFormat;
    using eng::Semantic;
    const eng::VertexAttr level_layout[] = {
        {Semantic::Position, AttrFormat::Float3, 0},
        {Semantic::Normal, AttrFormat::Float3, 12},
        {Semantic::TexCoord, AttrFormat::Float2, 24, 0},
        {Semantic::TexCoord, AttrFormat::Float2, 32, 1},
    };
    const eng::VertexAttr sky_layout[] = {
        {Semantic::Position, AttrFormat::Float3, 0},
        {Semantic::TexCoord, AttrFormat::Float2, 12},
    };
    const eng::VertexAttr model_layout[] = {
        {Semantic::Position, AttrFormat::Float3, 0},
        {Semantic::Normal, AttrFormat::Float3, 12},
        {Semantic::TexCoord, AttrFormat::Float2, 24},
        {Semantic::BlendIndices, AttrFormat::UInt8x4, 32},
        {Semantic::BlendWeight, AttrFormat::UNorm8x4, 36},
    };
    const eng::VertexAttr line_layout[] = {
        {Semantic::Position, AttrFormat::Float3, 0},
        {Semantic::Colour, AttrFormat::UNorm8x4, 12},
    };
    const eng::VertexAttr sprite_layout[] = {
        {Semantic::Position, AttrFormat::Float3, 0},
        {Semantic::TexCoord, AttrFormat::Float2, 12},
        {Semantic::Colour, AttrFormat::UNorm8x4, 20},
    };
    std::string err;
    level_prog_ = device.create_program(shaders::world, "vs_level", "ps_level", level_layout, &err);
    sky_prog_ = device.create_program(shaders::world, "vs_sky", "ps_sky", sky_layout, &err);
    model_prog_ = device.create_program(shaders::world, "vs_model", "ps_model", model_layout, &err);
    line_prog_ = device.create_program(shaders::world, "vs_lines", "ps_lines", line_layout, &err);
    sprite_prog_ = device.create_program(shaders::world, "vs_sprite", "ps_sprite", sprite_layout, &err);
    sprite_alpha_prog_ = device.create_program(shaders::world, "vs_sprite", "ps_sprite_alpha", sprite_layout, &err);
    if (!level_prog_ || !sky_prog_ || !model_prog_ || !line_prog_ || !sprite_prog_ || !sprite_alpha_prog_) {
        LOG_ERROR("World shaders: %s", err.c_str());
        return false;
    }
    // Fidelity's: the game is whole without them.
    level_g_prog_ = device.create_program(shaders::world, "vs_level", "ps_level_g", level_layout, &err);
    model_g_prog_ = device.create_program(shaders::world, "vs_model", "ps_model_g", model_layout, &err);
    if (!level_g_prog_ || !model_g_prog_) LOG_WARN("World shaders: Fidelity's surface targets will not be written (%s)", err.c_str());
    // Soldiers' shadows: the game is whole without them (the shade under the feet stays).
    shadow_prog_ = device.create_program(shaders::world, "vs_shadow", "ps_shadow", model_layout, &err);
    eng::TextureDesc none;
    none.format = eng::Format::D32F;
    none.target = true;
    shadow_none_ = device.create_texture(none);
    eng::SamplerDesc cmp;
    cmp.address = eng::Address::Border;   // outside the map is lit
    cmp.compare = true;
    samp_shadow_ = device.sampler(cmp);
    // Casters drawn both sides (Soldier Front's models are not all closed), pushed back by their slope.
    raster_shadow_ = device.raster({eng::Cull::None, false, 2.5f, 0.01f});
    if (!shadow_prog_ || !shadow_none_) {
        LOG_WARN("World shaders: soldiers will not cast shadows from the sun (%s)", err.c_str());
        shadow_failed_ = true;
    }
    frame_cb_ = device.create_buffer(eng::BufferKind::Uniform, nullptr, sizeof(FrameCB), true);
    object_cb_ = device.create_buffer(eng::BufferKind::Uniform, nullptr, sizeof(ObjectCB), true);
    bone_cb_ = device.create_buffer(eng::BufferKind::Uniform, nullptr, sizeof(Mat4) * kMaxBones, true);
    set_texture_filter(8);
    samp_clamp_ = device.sampler({eng::Filter::Linear, eng::Address::Clamp});
    // Everything blended, and the sky, writes the picture only: Fidelity's surface targets, when
    // they are bound beside it, hold the solid world and nothing else.
    eng::BlendDesc first;
    first.write = 0x1;
    blend_first_ = device.blend(first);
    eng::BlendDesc alpha;
    alpha.write = 0x1;
    alpha.enable = true;
    alpha.src = eng::BlendFactor::SrcAlpha;
    alpha.dst = eng::BlendFactor::InvSrcAlpha;
    alpha.src_alpha = eng::BlendFactor::One;
    alpha.dst_alpha = eng::BlendFactor::InvSrcAlpha;
    blend_alpha_ = device.blend(alpha);
    eng::BlendDesc add;
    add.write = 0x1;
    add.enable = true;
    add.src = eng::BlendFactor::One;
    add.dst = eng::BlendFactor::One;
    add.src_alpha = eng::BlendFactor::Zero;
    add.dst_alpha = eng::BlendFactor::One;
    blend_add_ = device.blend(add);
    eng::BlendDesc glow = add;
    glow.src = eng::BlendFactor::SrcAlpha;
    blend_glow_ = device.blend(glow);

    // The sky box: six faces of a unit cube, each its own texture, in the .wld's order
    // (right, left, top, bottom, back, front). Soldier Front stores the four sides turned half a
    // turn, so their coordinates run the other way.
    struct SV {
        float p[3], uv[2];
    };
    std::vector<SV> v;
    auto face = [&](Vec3 c, Vec3 u, Vec3 r, bool side) {
        const Vec3 corners[4] = {c - r + u, c + r + u, c + r - u, c - r - u};
        const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) {
            SV s{};
            std::memcpy(s.p, &corners[k].x, 12);
            s.uv[0] = side ? 1 - uvs[k][0] : uvs[k][0];
            s.uv[1] = side ? 1 - uvs[k][1] : uvs[k][1];
            v.push_back(s);
        }
    };
    face({1, 0, 0}, {0, 1, 0}, {0, 0, -1}, true);    // right  (+X)
    face({-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, true);    // left   (-X)
    face({0, 1, 0}, {0, 0, -1}, {1, 0, 0}, false);   // top    (+Y)
    face({0, -1, 0}, {0, 0, 1}, {1, 0, 0}, false);   // bottom (-Y)
    face({0, 0, -1}, {0, 1, 0}, {-1, 0, 0}, true);   // back   (-Z)
    face({0, 0, 1}, {0, 1, 0}, {1, 0, 0}, true);     // front  (+Z)
    sky_vb_ = device.create_buffer(eng::BufferKind::Vertex, v.data(), v.size() * sizeof(SV));
    return true;
}

void WorldRenderer::set_texture_filter(int anisotropy) {
    const int n = std::clamp(anisotropy, 1, 16);
    samp_wrap_ = n > 1 ? device_->sampler({eng::Filter::Anisotropic, eng::Address::Wrap, eng::u8(n)}) : device_->sampler({eng::Filter::Linear, eng::Address::Wrap});
}

void WorldRenderer::resume() {
    device_->set_uniforms(0, frame_cb_.get());
    device_->set_uniforms(1, object_cb_.get());
    device_->set_uniforms(2, bone_cb_.get());
    // Slot 3 always holds a depth texture for the shaders' shadow sampler; the map when it is live.
    device_->set_texture(3, shadows_live() && shadow_tex_ ? shadow_tex_.get() : shadow_none_.get());
    device_->set_sampler(2, samp_shadow_);
}

void WorldRenderer::begin_level(const sf::Level& level, LevelGpu& out) {
    up_level_ = &level;
    up_out_ = &out;
    up_next_ = 0;
    up_total_ = level.materials.size() + level.lightmaps.size() + 6;
    out = LevelGpu{};
    out.vb = device_->create_buffer(eng::BufferKind::Vertex, level.vertices.data(), level.vertices.size() * sizeof(sf::LevelVertex));
    out.ib = device_->create_buffer(eng::BufferKind::Index, level.indices.data(), level.indices.size() * sizeof(u32));
}

bool WorldRenderer::step_level(float budget_ms) {
    if (!up_level_ || !up_out_) return true;
    const sf::Level& L = *up_level_;
    const double start = eng::time::now();
    while (up_next_ < up_total_) {
        const size_t i = up_next_++;
        if (i < L.materials.size()) {
            textures_.get(sf::Pack::Area, L.materials[i].where, L.materials[i].texture);
        } else if (i < L.materials.size() + L.lightmaps.size()) {
            const size_t k = i - L.materials.size();
            textures_.get(sf::Pack::Area, L.lightmap_where[k], L.lightmaps[k]);
        } else {
            const size_t f = i - L.materials.size() - L.lightmaps.size();
            if (!L.sky[f].empty()) up_out_->sky[f] = &textures_.get(sf::Pack::Area, std::nullopt, L.sky[f]);
        }
        if ((eng::time::now() - start) * 1000.0 > budget_ms) return false;
    }
    // Every texture is on the card: the batches can point at them.
    LevelGpu& out = *up_out_;
    auto frames_of = [&](const sf::LevelMaterial& m, LevelGpu::Batch& gb) {
        if (m.frames.size() < 2 || m.frame_seconds <= 0) return;
        for (const sf::LevelMaterial::Frame& f : m.frames) gb.frames.push_back(&textures_.get(sf::Pack::Area, f.where, f.texture));
        gb.frame_seconds = m.frame_seconds;
    };
    for (const sf::LevelCard& c : L.cards) {
        const sf::LevelMaterial& m = L.materials[c.material];
        LevelGpu::Card gc;
        gc.at = c.at;
        gc.width = c.width, gc.height = c.height;
        gc.upright = c.upright;
        std::memcpy(gc.uv, c.uv, sizeof(gc.uv));
        gc.look.tex = &textures_.get(sf::Pack::Area, m.where, m.texture);
        gc.look.prop = true;
        gc.look.glow = m.glow;
        gc.look.unlit = m.unlit;
        gc.look.colour = m.colour;
        gc.look.shine = 0.0f, gc.look.rough = 1.0f;   // smoke and flame glint at nothing
        frames_of(m, gc.look);
        out.cards.push_back(std::move(gc));
    }
    for (const sf::LevelFlag& f : L.flags) {
        const sf::LevelMaterial& m = L.materials[f.material];
        LevelGpu::Flag gf;
        gf.look.tex = &textures_.get(sf::Pack::Area, m.where, m.texture);
        gf.look.prop = true;
        gf.look.glow = m.glow;
        gf.look.unlit = m.unlit;
        gf.look.colour = m.colour;
        gf.look.shine = 0.0f, gf.look.rough = 1.0f;   // cloth
        gf.look.bounds = f.bounds;
        gf.shape_seconds = f.shape_seconds;
        gf.shapes = f.shapes;
        out.flags.push_back(std::move(gf));
    }
    for (const sf::LevelBatch& b : L.batches) {
        const sf::LevelMaterial& m = L.materials[b.material];
        LevelGpu::Batch gb;
        gb.first = b.first_index;
        gb.count = b.index_count;
        gb.tex = &textures_.get(sf::Pack::Area, m.where, m.texture);
        frames_of(m, gb);
        if (b.lightmap >= 0) gb.lightmap = &textures_.get(sf::Pack::Area, L.lightmap_where[size_t(b.lightmap)], L.lightmaps[size_t(b.lightmap)]);
        gb.prop = m.prop;
        gb.glow = m.glow;
        gb.unlit = m.unlit;
        gb.colour = m.colour;
        // Water by the map's own texture table (its sound) or its name.
        gb.water = !m.prop && (m.surface == sf::Surface::Water || m.sound == sf::kSoundWater);
        // How each kind of surface takes a light (Fidelity): tile, metal, glass and water glint
        // sharply; concrete and wood a little, broadly; earth, grass and sand not to speak of.
        switch (m.surface) {
            case sf::Surface::Metal: gb.shine = 0.60f, gb.rough = 0.36f; break;
            case sf::Surface::Ladder: gb.shine = 0.50f, gb.rough = 0.40f; break;
            case sf::Surface::Tile: gb.shine = 0.55f, gb.rough = 0.26f; break;
            case sf::Surface::Glass: gb.shine = 0.80f, gb.rough = 0.12f; break;
            case sf::Surface::Water: gb.shine = 0.90f, gb.rough = 0.10f; break;
            case sf::Surface::Wood: gb.shine = 0.16f, gb.rough = 0.58f; break;
            case sf::Surface::Snow: gb.shine = 0.20f, gb.rough = 0.60f; break;
            case sf::Surface::Concrete: gb.shine = 0.12f, gb.rough = 0.70f; break;
            default: gb.shine = 0.03f, gb.rough = 0.90f; break;   // dirt, grass, sand
        }
        gb.bounds = b.bounds;
        out.batches.push_back(gb);
    }
    out.ready = true;
    up_level_ = nullptr;
    up_out_ = nullptr;
    return true;
}

float WorldRenderer::level_progress() const { return up_total_ ? float(up_next_) / float(up_total_) : 1.0f; }

std::unique_ptr<ModelGpu> WorldRenderer::upload_model(const sf::Model& model, sf::Pack pack) {
    auto gpu = std::make_unique<ModelGpu>();
    gpu->model = &model;
    std::vector<sf::ModelVertex> verts;
    std::vector<u32> idx;
    for (const sf::ModelMesh& mesh : model.meshes) {
        ModelGpu::Part part;
        part.first = u32(idx.size());
        part.count = u32(mesh.indices.size());
        part.name = mesh.name;
        const u32 base = u32(verts.size());
        verts.insert(verts.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (u32 i : mesh.indices) idx.push_back(base + i);
        if (mesh.material < model.materials.size()) {
            const sf::ModelMaterial& mat = model.materials[mesh.material];
            part.tex = mat.texture.empty() ? &textures_.white() : &textures_.get(mat.pack, mat.where, mat.texture);
        } else {
            part.tex = &textures_.white();
        }
        gpu->parts.push_back(std::move(part));
    }
    for (const sf::ModelVertex& v : verts)
        if (v.weights[0] != 255 || v.bones[0] != 0) gpu->skinned = true;
    if (model.bones.size() > 1) gpu->skinned = true;
    if (verts.empty()) return gpu;
    gpu->vb = device_->create_buffer(eng::BufferKind::Vertex, verts.data(), verts.size() * sizeof(sf::ModelVertex));
    gpu->ib = device_->create_buffer(eng::BufferKind::Index, idx.data(), idx.size() * sizeof(u32));
    (void)pack;
    return gpu;
}

// ── The hour ───────────────────────────────────────────────────────────────────

namespace {

// Where a bake's brightness is pulled toward: Soldier Front's median (sfcheck lightlevels,
// September 2026: 40 maps from 0.21, Village Horror, to 1.01, Sentosa).
constexpr float kTypicalBakedLevel = 0.67f;

void put(float* dst, const Vec3& v, float w) { dst[0] = v.x, dst[1] = v.y, dst[2] = v.z, dst[3] = w; }

}  // namespace

void WorldRenderer::set_time_of_day(TimeOfDay hour, bool night_bake, float baked_level) {
    grade_ = DayGrade{};
    const bool as_baked = (hour == TimeOfDay::Night) == night_bake;
    night_baked_ = as_baked && night_bake;
    if (as_baked) return;
    grade_.on = true;
    if (hour == TimeOfDay::Night) {
        // Moonlight over a day bake (TacticalFPS's night): what was in the sun goes pale blue, the
        // shade deep blue, colour drains, and the corners the bake left black get a little of the
        // sky's glow so there is still a floor to play on.
        grade_.sun_ink = {0.48f, 0.55f, 0.76f};
        grade_.shade_ink = {0.28f, 0.32f, 0.49f};
        grade_.fill = {0.05f, 0.06f, 0.10f};
        grade_.unlit = 0.55f;
        grade_.saturation = 0.55f;
        grade_.exposure = std::clamp(std::pow(kTypicalBakedLevel / std::max(baked_level, 0.05f), 0.85f), 0.5f, 2.0f);
        if (baked_level <= 0) grade_.exposure = 1;
        grade_.zenith = {0.02f, 0.03f, 0.08f};
        grade_.horizon = {0.07f, 0.09f, 0.16f};
        grade_.recolour = 0.85f;
        grade_.sky_gain = 0.16f;
        grade_.stars = 1;
        grade_.moon = 1;
        grade_.fog = {0.035f, 0.045f, 0.085f};
        grade_.haze = 5.0e-5f;
    } else {
        // Daylight over a night bake: the lamps stay where they are and the day is added over
        // them; the night's sky box has no day in it to bring out, so the sky is the gradient.
        grade_.relight = true;
        grade_.fill = {0.40f, 0.40f, 0.42f};
        grade_.sun = {0.95f, 0.92f, 0.85f};
        grade_.ambient = {0.55f, 0.56f, 0.60f};
        grade_.zenith = {0.28f, 0.48f, 0.82f};
        grade_.horizon = {0.70f, 0.78f, 0.88f};
        grade_.procedural = 1;
        grade_.fog = {0.66f, 0.72f, 0.80f};
        grade_.haze = 1.5e-5f;
    }
}

void WorldRenderer::begin_frame(const Camera& cam, const FrameLight& baked, double time) {
    time_ = time;
    // The hour's light: the flat sun and the ambient take the same inks as the bake.
    FrameLight light = baked;
    if (grade_.on) {
        if (grade_.relight) {
            light.sun_colour = grade_.sun;
            light.ambient = grade_.ambient;
        } else {
            light.sun_colour = baked.sun_colour * grade_.sun_ink;
            light.ambient = baked.ambient * grade_.shade_ink;
        }
        light.fog_colour = grade_.fog;
        if (!(light.fog_density > 0)) light.fog_density = grade_.haze;
    }
    put(frame_.grade_sun, grade_.sun_ink, grade_.knee_lo);
    put(frame_.grade_shade, grade_.shade_ink, grade_.knee_hi);
    put(frame_.grade_fill, grade_.fill, grade_.on ? 1.0f : 0.0f);
    frame_.grade_misc[0] = grade_.unlit, frame_.grade_misc[1] = grade_.saturation, frame_.grade_misc[2] = grade_.exposure, frame_.grade_misc[3] = 0;
    put(frame_.sky_zenith, grade_.zenith, grade_.recolour);
    put(frame_.sky_horizon, grade_.horizon, grade_.sky_gain);
    frame_.sky_extra[0] = grade_.procedural, frame_.sky_extra[1] = grade_.stars, frame_.sky_extra[2] = sky_moon_ ? grade_.moon : 0.0f, frame_.sky_extra[3] = 0;
    // The moon keeps the bearing of the map's sun, so it hangs over the side the shadows fall from.
    Vec3 flat{-baked.sun_direction.x, 0.0f, -baked.sun_direction.z};
    if (eng::length(flat) < 0.15f) flat = {0.55f, 0.0f, -0.62f};
    flat = eng::normalize(flat);
    const float e = grade_.moon_elevation * eng::kDegToRad;
    put(frame_.moon_dir, eng::normalize(flat * std::cos(e) + Vec3{0.0f, std::sin(e), 0.0f}), 0);

    // Half the sky under cloud, where Fidelity has it drift; the top face is bound in draw_level.
    frame_.fid_sky[0] = fid_clouds_ ? 0.5f : 0.0f, frame_.fid_sky[1] = fid_water_ ? 1.0f : 0.0f, frame_.fid_sky[2] = 0, frame_.fid_sky[3] = 0;
    frame_.view_proj = cam.view_proj;
    frame_.camera[0] = cam.eye.x, frame_.camera[1] = cam.eye.y, frame_.camera[2] = cam.eye.z, frame_.camera[3] = float(time);
    const Vec3 sd = eng::normalize(light.sun_direction);
    frame_.sun_dir[0] = sd.x, frame_.sun_dir[1] = sd.y, frame_.sun_dir[2] = sd.z, frame_.sun_dir[3] = 0;
    frame_.sun_colour[0] = light.sun_colour.x, frame_.sun_colour[1] = light.sun_colour.y, frame_.sun_colour[2] = light.sun_colour.z;
    frame_.ambient[0] = light.ambient.x, frame_.ambient[1] = light.ambient.y, frame_.ambient[2] = light.ambient.z, frame_.ambient[3] = light.exposure;
    frame_.fog[0] = light.fog_colour.x, frame_.fog[1] = light.fog_colour.y, frame_.fog[2] = light.fog_colour.z, frame_.fog[3] = light.fog_density;
    frame_.params[0] = 2.0f;    // lightmap gain: Soldier Front modulates by two
    frame_.params[1] = 1.05f;   // saturation
    frame_.params[2] = 1.0f;    // gamma
    frame_.params[3] = 0;
    // No soldiers' shadow until this frame's pass has run (begin_shadows).
    frame_.shadow[0] = frame_.shadow[1] = frame_.shadow[2] = frame_.shadow[3] = 0;
    device_->update_buffer(*frame_cb_, &frame_, sizeof(frame_));
    resume();
    lines_.clear();
    sprites_.clear();
}

void WorldRenderer::draw_sky(const LevelGpu& level, const Camera& cam) {
    bool any = false;
    for (const TexInfo* t : level.sky) any |= t && t->tex;
    if (!any) return;
    eng::Device& d = *device_;
    d.set_program(sky_prog_.get());
    d.set_blend(blend_first_);
    d.set_depth(d.depth_none());
    d.set_raster(d.raster_cull_none());
    d.set_sampler(0, samp_clamp_);
    d.set_vertices(sky_vb_.get(), sizeof(float) * 5);
    ObjectCB ob{};
    ob.world = Mat4::scale(Vec3{1000, 1000, 1000}) * Mat4::translation(cam.eye);
    ob.tint[0] = ob.tint[1] = ob.tint[2] = ob.tint[3] = 1;
    d.update_buffer(*object_cb_, &ob, sizeof(ob));
    for (int f = 0; f < 6; ++f) {
        const TexInfo* t = level.sky[size_t(f)];
        if (!t || !t->tex) continue;
        d.set_texture(0, t->tex.get());
        d.draw(eng::Topology::Triangles, 6, u32(f * 6));
    }
}

void WorldRenderer::draw_level(const LevelGpu& level, const Camera& cam) {
    if (!level.ready || !level.vb) return;
    eng::Device& d = *device_;
    d.set_program(surfaces_ ? level_g_prog_.get() : level_prog_.get());
    d.set_vertices(level.vb.get(), sizeof(sf::LevelVertex));
    d.set_indices(level.ib.get());
    d.set_sampler(0, samp_wrap_);
    d.set_sampler(1, samp_clamp_);
    // Water mirrors the sky's top face, where the map has a sky.
    const TexInfo* top = level.sky[2];
    const bool has_top = top && top->tex;
    if (surfaces_ && fid_water_) {
        d.set_texture(2, has_top ? top->tex.get() : textures_.white().tex.get());
        if ((frame_.fid_sky[2] > 0.5f) != has_top) {
            frame_.fid_sky[2] = has_top ? 1.0f : 0.0f;
            d.update_buffer(*frame_cb_, &frame_, sizeof(frame_));
        }
    }
    // Soldier Front's levels are modelled both ways round in places (fences, signs), so nothing is culled.
    d.set_raster(d.raster_cull_none());
    ObjectCB ob{};
    ob.world = Mat4::identity();
    ob.tint[0] = ob.tint[1] = ob.tint[2] = ob.tint[3] = 1;
    // The cards, turned to this frame's eye: an upright one about its foot (chimney smoke stands
    // up whichever side it is seen from), a free one flat to the view (a fire). Lit as a thing
    // facing the sky, so the light on one does not change as it turns.
    card_verts_.clear();
    card_order_.clear();
    if (!level.cards.empty()) {
        const Vec3 fwd = cam.forward();
        Vec3 right = eng::normalize(eng::cross(Vec3{0, 1, 0}, fwd));
        if (eng::length_sq(right) < 1e-6f) right = eng::yaw_to_right(cam.yaw);
        const Vec3 up = eng::cross(fwd, right);
        for (const LevelGpu::Card& c : level.cards) {
            Vec3 q[4];   // top left, top right, bottom right, bottom left
            if (c.upright) {
                Vec3 side = eng::cross(Vec3{0, 1, 0}, c.at - cam.eye);
                side = eng::length_sq(side) < 1e-6f ? right : eng::normalize(side);
                const Vec3 h = side * (c.width * 0.5f), rise{0, c.height, 0};
                q[0] = c.at + rise - h, q[1] = c.at + rise + h, q[2] = c.at + h, q[3] = c.at - h;
            } else {
                const Vec3 h = right * (c.width * 0.5f), v = up * (c.height * 0.5f);
                q[0] = c.at + v - h, q[1] = c.at + v + h, q[2] = c.at - v + h, q[3] = c.at - v - h;
            }
            const float uv[4][2] = {{c.uv[0], c.uv[1]}, {c.uv[2], c.uv[1]}, {c.uv[2], c.uv[3]}, {c.uv[0], c.uv[3]}};
            for (int k : {0, 1, 2, 0, 2, 3}) {
                sf::LevelVertex v{};
                std::memcpy(v.position, &q[k].x, 12);
                v.normal[1] = 1.0f;
                v.uv0[0] = uv[k][0], v.uv0[1] = uv[k][1];
                card_verts_.push_back(v);
            }
            card_order_.push_back(u32(card_order_.size()));
        }
        // The furthest first: one cloud of smoke is seen through another.
        std::sort(card_order_.begin(), card_order_.end(), [&](u32 x, u32 y) {
            return eng::length_sq(level.cards[x].at - cam.eye) > eng::length_sq(level.cards[y].at - cam.eye);
        });
    }
    // The flags, each in the shape it has now: the one whose turn it is, on its way to the next
    // where the two are the same cloth cut the same way (a flag's are; the hanging thing's are not).
    for (const LevelGpu::Flag& f : level.flags) {
        f.first = u32(card_verts_.size()), f.count = 0;
        if (f.shapes.empty()) continue;
        const double at = f.shape_seconds > 0 ? time_ / double(f.shape_seconds) : 0.0;
        const size_t now = size_t(at) % f.shapes.size(), next = (now + 1) % f.shapes.size();
        const float t = float(at - std::floor(at));
        const std::vector<sf::LevelVertex>& a = f.shapes[now];
        const std::vector<sf::LevelVertex>& b = f.shapes[next];
        if (a.size() == b.size()) {
            for (size_t i = 0; i < a.size(); ++i) {
                sf::LevelVertex v = a[i];
                for (int k = 0; k < 3; ++k) {
                    v.position[k] += (b[i].position[k] - a[i].position[k]) * t;
                    v.normal[k] += (b[i].normal[k] - a[i].normal[k]) * t;
                }
                card_verts_.push_back(v);
            }
        } else {
            card_verts_.insert(card_verts_.end(), a.begin(), a.end());
        }
        f.count = u32(card_verts_.size()) - f.first;
    }
    if (!card_verts_.empty()) {
        if (card_verts_.size() > card_capacity_) {
            card_capacity_ = std::max<size_t>(card_verts_.size() * 2, 96);
            card_vb_ = d.create_buffer(eng::BufferKind::Vertex, nullptr, card_capacity_ * sizeof(sf::LevelVertex), true);
        }
        d.update_buffer(*card_vb_, card_verts_.data(), card_verts_.size() * sizeof(sf::LevelVertex));
    }
    // The picture a batch shows now: its one, or whichever of its several has the turn.
    auto shown = [&](const LevelGpu::Batch& b) -> const TexInfo* {
        if (b.frames.size() < 2 || b.frame_seconds <= 0) return b.tex;
        return b.frames[size_t(time_ / double(b.frame_seconds)) % b.frames.size()];
    };
    // Opaque, then see-through (painted over what is behind), then the glows (added onto it, at
    // their vertex colour's strength, unlit when the prop says so).
    for (int pass = 0; pass < 3; ++pass) {
        const bool blended = pass == 1, glows = pass == 2;
        d.set_blend(glows ? blend_glow_ : blended ? blend_alpha_ : d.blend_opaque());
        d.set_depth(d.depth(eng::DepthDesc{true, pass == 0}));
        // Whether this batch is this pass's, with its state set if it is.
        auto set_up = [&](const LevelGpu::Batch& b) {
            const TexInfo* tex = shown(b);
            if (!tex || tex->invisible) return false;
            if (b.glow != glows || (!glows && tex->translucent != blended)) return false;
            ob.flags[0] = b.lightmap && b.lightmap->tex ? 1.0f : 0.0f;
            ob.flags[1] = glows ? -1.0f : tex->cutout ? 0.5f : (blended ? 0.02f : -1.0f);
            ob.flags[2] = b.unlit ? 1.0f : 0.0f;
            ob.flags[3] = b.water ? 1.0f : 0.0f;
            ob.extra[2] = b.shine, ob.extra[3] = b.rough;
            ob.tint[0] = float((b.colour >> 16) & 0xFF) / 255.0f;
            ob.tint[1] = float((b.colour >> 8) & 0xFF) / 255.0f;
            ob.tint[2] = float(b.colour & 0xFF) / 255.0f;
            ob.tint[3] = float((b.colour >> 24) & 0xFF) / 255.0f;
            d.update_buffer(*object_cb_, &ob, sizeof(ob));
            d.set_texture(0, tex->tex ? tex->tex.get() : textures_.white().tex.get());
            d.set_texture(1, b.lightmap && b.lightmap->tex ? b.lightmap->tex.get() : textures_.white().tex.get());
            return true;
        };
        for (const LevelGpu::Batch& b : level.batches)
            if (set_up(b)) d.draw_indexed(eng::Topology::Triangles, b.count, b.first);
        if (card_verts_.empty()) continue;
        d.set_vertices(card_vb_.get(), sizeof(sf::LevelVertex));
        for (const LevelGpu::Flag& f : level.flags)
            if (f.count && set_up(f.look)) d.draw(eng::Topology::Triangles, f.count, f.first);
        for (u32 k : card_order_)
            if (set_up(level.cards[k].look)) d.draw(eng::Topology::Triangles, 6, k * 6);
        d.set_vertices(level.vb.get(), sizeof(sf::LevelVertex));
    }
    if (surfaces_ && fid_water_) d.set_texture(2, nullptr);
}

void WorldRenderer::draw_model(const ModelGpu& model, const Mat4& world, const std::vector<Mat4>* skin, const eng::Vec4& tint, float rim,
                               float flash) {
    if (!model.vb) return;
    eng::Device& d = *device_;
    if (casting_) {
        // Into the shadow map: depth from the sun, every solid or cut-out part (a cut-out's
        // netting casts the shadow of its netting, not of the card it is painted on).
        d.set_program(shadow_prog_.get());
        d.set_vertices(model.vb.get(), sizeof(sf::ModelVertex));
        d.set_indices(model.ib.get());
        d.set_sampler(0, samp_wrap_);
        ObjectCB ob{};
        ob.world = world;
        ob.flags[3] = skin && !skin->empty() ? 1.0f : 0.0f;
        if (skin && !skin->empty()) {
            Mat4 bones[kMaxBones];
            const size_t n = std::min<size_t>(skin->size(), kMaxBones);
            for (size_t i = 0; i < n; ++i) bones[i] = (*skin)[i];
            for (size_t i = n; i < kMaxBones; ++i) bones[i] = Mat4::identity();
            d.update_buffer(*bone_cb_, bones, sizeof(bones));
        }
        for (const ModelGpu::Part& p : model.parts) {
            if (p.hidden || !p.tex || p.tex->translucent || p.tex->invisible) continue;
            ob.flags[1] = p.tex->cutout ? 0.5f : -1.0f;
            d.update_buffer(*object_cb_, &ob, sizeof(ob));
            d.set_texture(0, p.tex->tex ? p.tex->tex.get() : textures_.white().tex.get());
            d.draw_indexed(eng::Topology::Triangles, p.count, p.first);
        }
        return;
    }
    d.set_program(surfaces_ ? model_g_prog_.get() : model_prog_.get());
    d.set_vertices(model.vb.get(), sizeof(sf::ModelVertex));
    d.set_indices(model.ib.get());
    d.set_sampler(0, samp_wrap_);
    d.set_raster(d.raster_cull_none());
    ObjectCB ob{};
    ob.world = world;
    ob.tint[0] = tint.x, ob.tint[1] = tint.y, ob.tint[2] = tint.z, ob.tint[3] = tint.w;
    ob.flags[3] = skin && !skin->empty() ? 1.0f : 0.0f;
    ob.extra[0] = rim;
    ob.extra[1] = flash;
    ob.extra[2] = 0.14f, ob.extra[3] = 0.55f;   // cloth, skin and a gun's steel between them: a soft glint
    if (skin && !skin->empty()) {
        Mat4 bones[kMaxBones];
        const size_t n = std::min<size_t>(skin->size(), kMaxBones);
        for (size_t i = 0; i < n; ++i) bones[i] = (*skin)[i];
        for (size_t i = n; i < kMaxBones; ++i) bones[i] = Mat4::identity();
        d.update_buffer(*bone_cb_, bones, sizeof(bones));
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool blended = pass == 1;
        d.set_blend(blended || tint.w < 0.999f ? blend_alpha_ : d.blend_opaque());
        d.set_depth(d.depth(eng::DepthDesc{true, !blended}));
        for (const ModelGpu::Part& p : model.parts) {
            if (p.hidden || !p.tex) continue;
            if (p.tex->translucent != blended) continue;
            ob.flags[1] = p.tex->cutout ? 0.5f : (blended ? 0.02f : -1.0f);
            d.update_buffer(*object_cb_, &ob, sizeof(ob));
            d.set_texture(0, p.tex->tex ? p.tex->tex.get() : textures_.white().tex.get());
            d.draw_indexed(eng::Topology::Triangles, p.count, p.first);
        }
    }
}

void WorldRenderer::line(const Vec3& a, const Vec3& b, eng::u32 abgr) {
    lines_.push_back({{a.x, a.y, a.z}, abgr});
    lines_.push_back({{b.x, b.y, b.z}, abgr});
}

void WorldRenderer::flush_lines() {
    if (lines_.empty()) return;
    eng::Device& d = *device_;
    if (lines_.size() > line_capacity_) {
        line_capacity_ = std::max<size_t>(lines_.size() * 2, 1024);
        line_vb_ = d.create_buffer(eng::BufferKind::Vertex, nullptr, line_capacity_ * sizeof(LineVertex), true);
    }
    d.update_buffer(*line_vb_, lines_.data(), lines_.size() * sizeof(LineVertex));
    d.set_program(line_prog_.get());
    d.set_vertices(line_vb_.get(), sizeof(LineVertex));
    d.set_blend(blend_alpha_);
    d.set_depth(d.depth(eng::DepthDesc{true, false}));
    d.draw(eng::Topology::Lines, u32(lines_.size()));
    lines_.clear();
}

void WorldRenderer::sprite(const TexInfo& tex, const Vec3& centre, float size, eng::u32 abgr, float spin, const float* uv, bool alpha, float aspect) {
    if (!tex.tex || size <= 0) return;
    SpriteRec r;
    r.tex = &tex;
    r.a = centre;
    r.size = size;
    r.spin = spin;
    r.colour = abgr;
    r.alpha = alpha;
    r.aspect = aspect;
    if (uv) std::memcpy(r.uv, uv, sizeof(r.uv));
    sprites_.push_back(r);
}

void WorldRenderer::decal(const TexInfo& tex, const Vec3& centre, const Vec3& normal, float size, eng::u32 abgr, float spin, const float* uv) {
    if (!tex.tex || size <= 0 || eng::length_sq(normal) < 1e-6f) return;
    SpriteRec r;
    r.tex = &tex;
    if (uv) std::memcpy(r.uv, uv, sizeof(r.uv));
    r.decal = r.alpha = true;
    r.b = eng::normalize(normal);
    r.a = centre + r.b * 0.4f;   // off the surface, clear of its depth
    r.size = size;
    r.spin = spin;
    r.colour = abgr;
    sprites_.push_back(r);
}

void WorldRenderer::beam(const TexInfo& tex, const Vec3& a, const Vec3& b, float width, eng::u32 abgr, const float* uv) {
    if (!tex.tex || width <= 0 || eng::length_sq(b - a) < 1e-4f) return;
    SpriteRec r;
    r.tex = &tex;
    r.beam = true;
    r.a = a;
    r.b = b;
    r.size = width;
    r.colour = abgr;
    if (uv) std::memcpy(r.uv, uv, sizeof(r.uv));
    sprites_.push_back(r);
}

void WorldRenderer::flush_sprites(const Camera& cam) {
    if (sprites_.empty()) return;
    // The painted ones first (marks, smoke), then the added light; one run of quads a texture.
    std::stable_sort(sprites_.begin(), sprites_.end(),
                     [](const SpriteRec& x, const SpriteRec& y) { return x.alpha != y.alpha ? x.alpha : x.tex < y.tex; });
    const Vec3 fwd = cam.forward();
    Vec3 right = eng::normalize(eng::cross(Vec3{0, 1, 0}, fwd));
    if (eng::length_sq(right) < 1e-6f) right = eng::yaw_to_right(cam.yaw);
    const Vec3 up = eng::cross(fwd, right);
    sprite_verts_.clear();
    auto quad = [&](const Vec3 c[4], const float* uv, eng::u32 col) {
        const float u[4][2] = {{uv[0], uv[1]}, {uv[2], uv[1]}, {uv[2], uv[3]}, {uv[0], uv[3]}};
        for (int k : {0, 1, 2, 0, 2, 3}) {
            SpriteVertex v;
            std::memcpy(v.p, &c[k].x, 12);
            v.uv[0] = u[k][0], v.uv[1] = u[k][1];
            v.c = col;
            sprite_verts_.push_back(v);
        }
    };
    for (const SpriteRec& s : sprites_) {
        Vec3 c[4];
        if (s.beam) {
            // The art's width runs along the line (u), its height across it (v).
            const Vec3 d = eng::normalize(s.b - s.a);
            Vec3 side = eng::cross(d, cam.eye - (s.a + s.b) * 0.5f);
            side = eng::length_sq(side) < 1e-6f ? up : eng::normalize(side);
            const Vec3 h = side * (s.size * 0.5f);
            c[0] = s.a + h, c[1] = s.b + h, c[2] = s.b - h, c[3] = s.a - h;
        } else if (s.decal) {
            // In the surface's own plane, turned by `spin` about its normal.
            const Vec3 n = s.b;
            Vec3 t = eng::cross(std::fabs(n.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, n);
            t = eng::normalize(t);
            const Vec3 bt = eng::cross(n, t);
            const float cs = std::cos(s.spin), sn = std::sin(s.spin);
            const Vec3 rx = (t * cs + bt * sn) * (s.size * 0.5f), ry = (bt * cs - t * sn) * (s.size * 0.5f);
            c[0] = s.a - rx + ry, c[1] = s.a + rx + ry, c[2] = s.a + rx - ry, c[3] = s.a - rx - ry;
        } else {
            const float cs = std::cos(s.spin), sn = std::sin(s.spin);
            const Vec3 rx = (right * cs + up * sn) * (s.size * 0.5f), ry = (up * cs - right * sn) * (s.size * 0.5f * s.aspect);
            c[0] = s.a - rx + ry, c[1] = s.a + rx + ry, c[2] = s.a + rx - ry, c[3] = s.a - rx - ry;
        }
        quad(c, s.uv, s.colour);
    }
    eng::Device& d = *device_;
    if (sprite_verts_.size() > sprite_capacity_) {
        sprite_capacity_ = std::max<size_t>(sprite_verts_.size() * 2, 1536);
        sprite_vb_ = d.create_buffer(eng::BufferKind::Vertex, nullptr, sprite_capacity_ * sizeof(SpriteVertex), true);
    }
    d.update_buffer(*sprite_vb_, sprite_verts_.data(), sprite_verts_.size() * sizeof(SpriteVertex));
    d.set_vertices(sprite_vb_.get(), sizeof(SpriteVertex));
    d.set_depth(d.depth(eng::DepthDesc{true, false}));
    d.set_raster(d.raster_cull_none());
    d.set_sampler(0, samp_clamp_);
    size_t first = 0;
    int mode = -1;
    while (first < sprites_.size()) {
        if (int(sprites_[first].alpha) != mode) {
            mode = int(sprites_[first].alpha);
            d.set_program(mode ? sprite_alpha_prog_.get() : sprite_prog_.get());
            d.set_blend(mode ? blend_alpha_ : blend_add_);
        }
        size_t last = first;
        while (last < sprites_.size() && sprites_[last].tex == sprites_[first].tex && sprites_[last].alpha == sprites_[first].alpha) ++last;
        d.set_texture(0, sprites_[first].tex->tex.get());
        d.draw(eng::Topology::Triangles, u32((last - first) * 6), u32(first * 6));
        first = last;
    }
    sprites_.clear();
}

void WorldRenderer::begin_view_model(const Camera& cam, float fov_y_degrees) {
    eng::Device& d = *device_;
    d.clear_depth(eng::Target{d.targets().depth.texture ? d.targets().depth.texture : d.back_depth()});
    // The shape of what is being drawn into (the picture's, not the window's).
    const float aspect = d.viewport().h > 0 ? d.viewport().w / d.viewport().h : float(d.width()) / float(std::max(1, d.height()));
    const Mat4 proj = Mat4::perspective_lh(fov_y_degrees * eng::kDegToRad, aspect, 1.0f, 1000.0f);
    FrameCB f = frame_;
    f.view_proj = cam.view * proj;
    f.shadow[0] = 0;   // your own body's shadow is not laid on the gun in your hands
    d.update_buffer(*frame_cb_, &f, sizeof(f));
}

bool WorldRenderer::begin_shadows(const Vec3& focus, float radius) {
    shadow_strength_ = 0;
    if (shadow_failed_ || casting_ || radius <= 1.0f) return false;
    // How much sun there is to cast from. Soldier Front's maps carry a dim light of their own (it
    // lights their props: 0.4 on Desert Camp in full day), so the hour says it rather than that
    // light: a day's sun casts at 0.7, the room's night over a day's bake only moonlight's 0.3, and
    // a map baked for night (lamps, no sun) none: the shade under the feet is the honest answer there.
    const float lum = frame_.sun_colour[0] * 0.3f + frame_.sun_colour[1] * 0.6f + frame_.sun_colour[2] * 0.1f;
    const float strength = night_baked_ || lum < 0.08f ? 0.0f : night_over_day() ? 0.3f : 0.7f;
    if (std::fabs(strength - logged_strength_) > 0.01f) {
        LOG_INFO("Soldiers' shadows: the sun's light %.2f, so %s", double(lum),
                 strength > 0.02f ? eng::str::format("cast at %.2f", double(strength)).c_str() : "none cast (the shade under the feet stays)");
        logged_strength_ = strength;
    }
    if (strength <= 0.02f) return false;
    eng::Device& d = *device_;
    if (!shadow_tex_) {
        eng::TextureDesc td;
        td.format = eng::Format::D32F;
        td.width = td.height = kShadowSize;
        td.target = true;
        shadow_tex_ = d.create_texture(td);
        if (!shadow_tex_) {
            LOG_WARN("Soldiers' shadows: no %d x %d depth map on this card; the shade under the feet stays", kShadowSize, kShadowSize);
            shadow_failed_ = true;
            return false;
        }
    }
    // The sun looks down its own way at the ground round `focus`, from far enough back that
    // nothing between them is cut out of the map. A sun lower than about eleven degrees is raised
    // that far: its shadows would run across baked ones traced from much higher.
    Vec3 L = eng::normalize(Vec3{frame_.sun_dir[0], frame_.sun_dir[1], frame_.sun_dir[2]});
    if (L.y > -0.2f) L = eng::normalize(Vec3{L.x, -0.2f, L.z});
    const Vec3 up = std::fabs(L.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const float back = radius * 2.0f;
    const Mat4 view = Mat4::look_to_lh(focus - L * back, L, up);
    // The middle snapped to whole texels in the sun's own frame, so edges do not crawl as you walk.
    const float texel = 2.0f * radius / float(kShadowSize);
    Vec3 ls = view.transform_point(focus);
    ls.x = std::floor(ls.x / texel) * texel;
    ls.y = std::floor(ls.y / texel) * texel;
    const float far_z = back + radius + 400.0f;
    sun_view_proj_ = view * Mat4::ortho_offcenter_lh(ls.x - radius, ls.x + radius, ls.y - radius, ls.y + radius, 0.0f, far_z);
    shadow_strength_ = strength;
    // Three centimetres along the sun, in the map's depth (orthographic: it runs evenly to far_z).
    shadow_bias_ = 3.0f / far_z;

    saved_targets_ = d.targets();
    saved_viewport_ = d.viewport();
    d.set_texture(3, shadow_none_.get());   // the map is drawn into, not read, until end_shadows
    const eng::Target depth{shadow_tex_.get()};
    d.set_targets(std::span<const eng::Target>(), depth);
    d.clear_depth(depth);
    d.set_viewport({0, 0, float(kShadowSize), float(kShadowSize)});
    d.set_raster(raster_shadow_);
    d.set_depth(d.depth(eng::DepthDesc{true, true}));
    d.set_blend(d.blend_opaque());
    FrameCB f = frame_;
    f.view_proj = sun_view_proj_;
    d.update_buffer(*frame_cb_, &f, sizeof(f));
    casting_ = true;
    return true;
}

void WorldRenderer::end_shadows() {
    if (!casting_) return;
    casting_ = false;
    eng::Device& d = *device_;
    d.set_targets(saved_targets_);
    d.set_viewport(saved_viewport_);
    frame_.sun_view_proj = sun_view_proj_;
    frame_.shadow[0] = shadow_strength_;
    frame_.shadow[1] = 1.0f / float(kShadowSize);
    frame_.shadow[2] = shadow_bias_;
    frame_.shadow[3] = 0;
    d.update_buffer(*frame_cb_, &frame_, sizeof(frame_));
    resume();
}

}  // namespace lsf
