// Drawing the match: a Soldier Front level (lightmapped geometry, sun-lit props, the sky box),
// skinned models (characters, first-person weapons) and debug lines, through the device on any
// of its three back ends.
//
// Textures come from the archives (area, weapon, force) or the game's Content folder and are
// cached by key; a level uploads in slices so the loading screen keeps drawing.
#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Render/Device.hpp"
#include "Game/Rules.hpp"
#include "SF/Data.hpp"
#include "SF/Image.hpp"
#include "SF/Level.hpp"
#include "SF/Model.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lsf {

using eng::u32;
using eng::u8;

struct Camera {
    eng::Vec3 eye;
    float yaw = 0, pitch = 0;   // degrees, engine convention
    float fov_x = 80;           // horizontal degrees at 16:9 (wider screens see more)
    float znear = 5, zfar = 60000;
    eng::Mat4 view, proj, view_proj;
    void update(float aspect);
    eng::Vec3 forward() const { return eng::angles_to_forward(yaw, pitch); }
};

struct TexInfo {
    eng::TextureRef tex;
    bool cutout = false, translucent = false, invisible = false;
};

class TextureCache {
public:
    void init(eng::Device* device, const sf::Data* data);
    // From an archive, by location (or key when the location is unknown). Missing: nullptr tex.
    const TexInfo& get(sf::Pack pack, const std::optional<sf::AssetLocation>& where, const std::string& key);
    // A greyscale picture as a mask (the original's effect/shadow/shadow.bmp: white where it is
    // strongest): white with its brightness as the alpha, to be tinted by whoever draws it.
    const TexInfo& get_mask(sf::Pack pack, const std::string& key);
    // A picture on black meant to be seen without it (the effect archive's sprays, 24-bit on a
    // black ground): its colours kept, black turned transparent, dark parts faded toward it.
    const TexInfo& get_keyed(sf::Pack pack, const std::string& key);
    // A texture from a decoded image (skins, UI), cached under `key`.
    const TexInfo& white();
    void clear() { cache_.clear(); }
    size_t size() const { return cache_.size(); }

private:
    eng::Device* device_ = nullptr;
    const sf::Data* data_ = nullptr;
    std::map<std::string, TexInfo> cache_;
    TexInfo white_;
};

// A level on the card.
struct LevelGpu {
    eng::BufferRef vb, ib;
    struct Batch {
        u32 first = 0, count = 0;
        const TexInfo* tex = nullptr;
        const TexInfo* lightmap = nullptr;
        bool prop = false;
        bool glow = false, unlit = false;   // sf::LevelMaterial's: added light, and lit by nothing
        float shine = 0, rough = 1;         // Fidelity's lights: how much it glints, how widely (its surface's)
        bool water = false;                 // Fidelity draws it as water
        u32 colour = 0xFFFFFFFF;            // its ARGB vertex colour (a glow's strength)
        eng::Aabb bounds;
        // A prop with pictures shown in turn (sf::LevelMaterial::frames): all of them, and how long each.
        std::vector<const TexInfo*> frames;
        float frame_seconds = 0;
    };
    std::vector<Batch> batches;
    // The level's cards that face the eye (sf::LevelCard): chimney smoke, fires. `look` is how one
    // is drawn (its first and count are not used: its corners are made each frame).
    struct Card {
        eng::Vec3 at;
        float width = 0, height = 0;
        bool upright = false;
        float uv[4] = {0, 0, 1, 1};
        Batch look;
    };
    std::vector<Card> cards;
    // The level's flags (sf::LevelFlag): every shape of each, and where this frame's lies in the
    // cards' buffer (filled as it is drawn).
    struct Flag {
        Batch look;
        float shape_seconds = 0;
        std::vector<std::vector<sf::LevelVertex>> shapes;
        mutable u32 first = 0, count = 0;
    };
    std::vector<Flag> flags;
    std::array<const TexInfo*, 6> sky{};
    bool ready = false;
};

// A model on the card, with its materials resolved.
struct ModelGpu {
    const sf::Model* model = nullptr;
    eng::BufferRef vb, ib;
    struct Part {
        u32 first = 0, count = 0;
        const TexInfo* tex = nullptr;
        std::string name;
        bool hidden = false;
    };
    std::vector<Part> parts;
    bool skinned = false;
};

struct FrameLight {
    eng::Vec3 sun_direction{-0.35f, -0.85f, 0.4f};
    eng::Vec3 sun_colour{0.75f, 0.72f, 0.66f};
    eng::Vec3 ambient{0.55f, 0.55f, 0.58f};
    float exposure = 1.0f;
    eng::Vec3 fog_colour{0.6f, 0.62f, 0.66f};
    float fog_density = 0;
};

class WorldRenderer {
public:
    bool init(eng::Device& device);
    TextureCache& textures() { return textures_; }
    // How the map's and the models' textures are filtered at a slant: 1 trilinear, else
    // anisotropic to that many samples (the options' texture filter).
    void set_texture_filter(int anisotropy);
    // The frame's uniforms bound again, after a pass of someone else's (FramePipe::shade) took the slots.
    void resume();
    // Fidelity's surface targets are bound beside the picture (FramePipe::surfaces): the solid
    // world writes them too (world.hlsl GOut). Off again once the lights have been laid on.
    void set_surfaces(bool on) { surfaces_ = on && level_g_prog_ && model_g_prog_; }
    // Fidelity draws its own moon: the night sky's small one is left out.
    void set_sky_moon(bool on) { sky_moon_ = on; }
    // Fidelity's cloud over the map's sky, and its water (drawn with the surface targets).
    void set_fidelity_sky(bool clouds, bool water) { fid_clouds_ = clouds, fid_water_ = water; }
    // This frame's light as the hour has it, and where the night's moon hangs.
    eng::Vec3 light_colour() const { return {frame_.sun_colour[0], frame_.sun_colour[1], frame_.sun_colour[2]}; }
    eng::Vec3 moon_toward() const { return {frame_.moon_dir[0], frame_.moon_dir[1], frame_.moon_dir[2]}; }
    // The room's night laid over a day's bake (the sky is graded and has a moon of its own place).
    bool night_over_day() const { return grade_.on && !grade_.relight; }

    // Level upload: begin, then step() until it returns true (a slice of textures each call).
    void begin_level(const sf::Level& level, LevelGpu& out);
    bool step_level(float budget_ms = 12.0f);
    float level_progress() const;

    std::unique_ptr<ModelGpu> upload_model(const sf::Model& model, sf::Pack pack);

    void begin_frame(const Camera& cam, const FrameLight& light, double time);
    void draw_sky(const LevelGpu& level, const Camera& cam);
    void draw_level(const LevelGpu& level, const Camera& cam);
    // A model at `world` with skinning matrices (bone count = model bones), or none (rigid).
    void draw_model(const ModelGpu& model, const eng::Mat4& world, const std::vector<eng::Mat4>* skin, const eng::Vec4& tint = {1, 1, 1, 1},
                    float rim = 0, float flash = 0);
    // Lines (debug): pairs of points with colours, drawn at the end of the frame.
    void line(const eng::Vec3& a, const eng::Vec3& b, eng::u32 abgr);
    void flush_lines();
    // Additive sprites, Soldier Front's flare/ art (muzzle flashes, tracers, glows): a billboard
    // turned to face the camera (spun by `spin` radians), or a beam from a to b turned about its
    // own line to face it. `uv` picks a cell of a sheet {u0, v0, u1, v1}. Drawn by flush_sprites()
    // with whatever projection is current, so the view model's flash is flushed in its own pass.
    // `alpha`: painted over by the art's alpha instead of added (smoke, blood); `aspect`: height
    // over width (the explosion/ plume frames are twice as tall as wide).
    void sprite(const TexInfo& tex, const eng::Vec3& centre, float size, eng::u32 abgr, float spin = 0, const float* uv = nullptr, bool alpha = false,
                float aspect = 1);
    void beam(const TexInfo& tex, const eng::Vec3& a, const eng::Vec3& b, float width, eng::u32 abgr, const float* uv = nullptr);
    // A mark laid flat on a surface (a bullet hole, blood): facing along `normal`, painted by its alpha.
    void decal(const TexInfo& tex, const eng::Vec3& centre, const eng::Vec3& normal, float size, eng::u32 abgr, float spin = 0, const float* uv = nullptr);
    void flush_sprites(const Camera& cam);
    // The first-person pass: its own projection, depth cleared (and no soldiers' shadow on it).
    void begin_view_model(const Camera& cam, float fov_y_degrees);

    // Soldiers' shadows from the map's sun (Settings::Shadows::Sun; TacticalFPS's): a depth map of
    // what moves, seen from the sun, laid on what the bake left in the sun. Between begin_shadows
    // and end_shadows, draw_model writes depth from the sun and nothing else, so what casts is
    // whatever is drawn there: the soldiers and what they carry, never the map (its own shadows
    // are in its lightmaps). Centred on `focus`, out to `radius` round it. False (and nothing to
    // draw) when the map's sun is too weak to cast a shadow worth the name: a night.
    bool begin_shadows(const eng::Vec3& focus, float radius);
    void end_shadows();
    bool shadows_live() const { return frame_.shadow[0] > 0.0f; }

    // The room's hour, as a grade of the level's bake (Rules.hpp TimeOfDay). `night_bake` says
    // which hour the lightmaps were made for -- that hour is drawn exactly as baked -- and
    // `baked_level` how bright they are, so every map's night is equally dark
    // (sf::measure_baked_level). Until this is called the level draws as baked.
    void set_time_of_day(TimeOfDay hour, bool night_bake, float baked_level);

private:
    struct FrameCB {
        eng::Mat4 view_proj;
        float camera[4];
        float sun_dir[4];
        float sun_colour[4];
        float ambient[4];
        float fog[4];
        float params[4];
        float grade_sun[4];
        float grade_shade[4];
        float grade_fill[4];
        float grade_misc[4];
        float sky_zenith[4];
        float sky_horizon[4];
        float sky_extra[4];
        float moon_dir[4];
        float fid_sky[4];
        eng::Mat4 sun_view_proj;   // soldiers' shadows: the shadow map's view from the sun
        float shadow[4];           // x strength (0: none this frame), y a texel in UV, z the bias in its depth
    };
    // What set_time_of_day() worked out, applied every frame in begin_frame().
    struct DayGrade {
        bool on = false;
        bool relight = false;            // a night bake asked for day: the sun and sky are replaced, not inked
        eng::Vec3 sun_ink{1, 1, 1}, shade_ink{1, 1, 1}, fill{0, 0, 0};
        float knee_lo = 0.55f, knee_hi = 1.15f;
        float unlit = 1, saturation = 1, exposure = 1;
        eng::Vec3 sun{1, 1, 1}, ambient{1, 1, 1};   // relight: the flat sun and ambient outright
        eng::Vec3 zenith, horizon;
        float recolour = 0, sky_gain = 1, procedural = 0, stars = 0, moon = 0;
        float moon_elevation = 48;
        eng::Vec3 fog{0.5f, 0.5f, 0.5f};
        float haze = 0;                  // fog density (exp2, per cm) for a map with none of its own
    };
    struct ObjectCB {
        eng::Mat4 world;
        float tint[4];
        float flags[4];
        float extra[4];
    };
    static constexpr int kMaxBones = 128;

    eng::Device* device_ = nullptr;
    TextureCache textures_;
    eng::ProgramRef level_prog_, sky_prog_, model_prog_, line_prog_, sprite_prog_, sprite_alpha_prog_;
    eng::ProgramRef level_g_prog_, model_g_prog_;   // the same into Fidelity's surface targets
    // Soldiers' shadows: the casters' program, the map (and a 1 x 1 stand-in bound while there is
    // none, so the slot always holds a depth texture), how it is sampled and drawn into.
    static constexpr int kShadowSize = 2048;
    eng::ProgramRef shadow_prog_;
    eng::TextureRef shadow_tex_, shadow_none_;
    const eng::Sampler* samp_shadow_ = nullptr;
    const eng::RasterState* raster_shadow_ = nullptr;
    bool casting_ = false, shadow_failed_ = false;
    bool night_baked_ = false;   // a map baked for night, drawn as baked: no sun to cast from
    float shadow_strength_ = 0, shadow_bias_ = 0, logged_strength_ = -1;
    eng::Mat4 sun_view_proj_;
    eng::Device::Targets saved_targets_;
    eng::Device::Viewport saved_viewport_;
    bool surfaces_ = false, sky_moon_ = true, fid_clouds_ = false, fid_water_ = false;
    const eng::BlendState* blend_first_ = nullptr;  // painted over, the picture only (the sky, with surface targets bound)
    eng::BufferRef frame_cb_, object_cb_, bone_cb_, sky_vb_, line_vb_, sprite_vb_, card_vb_;
    size_t line_capacity_ = 0, sprite_capacity_ = 0, card_capacity_ = 0;
    std::vector<sf::LevelVertex> card_verts_;   // the level's cards, turned to this frame's eye
    std::vector<u32> card_order_;               // furthest first
    double time_ = 0;                           // this frame's, for the props whose pictures take turns
    const eng::Sampler* samp_wrap_ = nullptr;
    const eng::Sampler* samp_clamp_ = nullptr;
    const eng::BlendState* blend_alpha_ = nullptr;
    const eng::BlendState* blend_add_ = nullptr;
    const eng::BlendState* blend_glow_ = nullptr;   // colour x alpha added: the level's glows
    struct SpriteRec {
        const TexInfo* tex = nullptr;
        bool beam = false, decal = false, alpha = false;
        eng::Vec3 a, b;              // a decal: b is its surface's normal
        float size = 0, spin = 0, aspect = 1;
        eng::u32 colour = 0xFFFFFFFF;
        float uv[4] = {0, 0, 1, 1};
    };
    std::vector<SpriteRec> sprites_;
    struct SpriteVertex {
        float p[3];
        float uv[2];
        eng::u32 c;
    };
    std::vector<SpriteVertex> sprite_verts_;
    FrameCB frame_{};
    DayGrade grade_;
    struct LineVertex {
        float p[3];
        eng::u32 c;
    };
    std::vector<LineVertex> lines_;
    // Level upload in progress.
    const sf::Level* up_level_ = nullptr;
    LevelGpu* up_out_ = nullptr;
    size_t up_next_ = 0, up_total_ = 0;
};

}  // namespace lsf
