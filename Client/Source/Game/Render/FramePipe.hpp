// The match's picture on its way to the window. The world is drawn into targets of its own, at
// the size the player chose (the window's, or a size of another shape or another count of pixels
// when the game has the whole screen), with as many samples a pixel as the options ask, and then
// set into the window: over all of it, or kept in shape between black bars.
//
// Every renderer goes this way (OpenGL's window has no depth of its own to draw a world into),
// so the three draw the same picture.
//
// Fidelity, when the player has found it and turned it on, finishes that same picture more
// finely before it goes out, each part of it the player's to set (Settings.hpp FidelitySettings):
//
//   surface targets   the solid world also writes its normals, how much each surface shines and
//                     its own colour (world.hlsl GOut), so that
//   lights            a muzzle flash, a grenade's blast and the map's own lamps light what they
//                     are next to and glint off what shines, as does the map's sun
//   contact shadow    where things meet
//   the sun and moon  a disc where the map's light comes from, its corona, the moon in tonight's
//                     phase; shafts of light past rooftops, looking toward it
//   the finish        a glow off the brightest light, the highlights' curve, colour, a vignette,
//                     smoothed edges, sharpening; more or fewer pixels than the picture has
//
// The map's bake, hour and sky are untouched by any of it: the sun stands where the map's light
// comes from and the moon is out only when the room's hour is night.
#pragma once

#include "Engine/Render/Device.hpp"
#include "Game/Render/WorldRenderer.hpp"
#include "Game/Settings.hpp"

#include <filesystem>
#include <vector>

namespace lsf {

struct FrameView {
    int width = 0, height = 0;           // the picture, in its own pixels
    int samples = 1;                     // asked for (the card may take fewer)
    bool fidelity = false;
    FidelitySettings finish;             // Fidelity's parts, as the player set them
    float x = 0, y = 0, w = 0, h = 0;    // where it goes in the window, in the window's pixels
};

// A light for this frame (Fidelity's lights): a muzzle flash, a blast, a lamp.
struct PipeLight {
    eng::Vec3 position;
    float radius = 400;                  // it reaches this far (cm), fading to nothing
    eng::Vec3 colour{1, 1, 1};           // times its strength
    float pool = 1;                      // how much of it lights a surface outright (a lamp the bake
                                         // already lit pools little and mostly glints)
};

// What stands in the sky where the map's light comes from, for this frame.
struct PipeSky {
    eng::Vec3 light_travel{0, -1, 0};    // the way the map's light goes
    eng::Vec3 light_colour{1, 1, 1};     // as the hour has it
    eng::Vec3 body_toward{0, 1, 0};      // toward the sun's disc or the moon
    bool night = false;                  // the room's hour: the moon, not the sun
};

class FramePipe {
public:
    bool init(eng::Device& device);
    // The corona and the moon's phases (Content/fidelity). Without them the sun is its disc alone.
    void load_art(const std::filesystem::path& content_dir);
    // The picture's targets bound and cleared, the viewport over all of it.
    void begin(const FrameView& view, const eng::Color& clear);
    // Whether the solid world is to write its surface targets this frame (WorldRenderer::set_surfaces).
    bool surfaces() const { return surfaces_; }
    // Whether Fidelity draws the sun or the moon this frame (the night sky then leaves its own moon out).
    bool sky_body() const { return view_.fidelity && fidelity_ok_ && lights_ok_ && view_.finish.sun; }
    // Fidelity as it is this frame (off: nothing of it is drawn), and its corona picture for the lamps.
    bool fidelity() const { return view_.fidelity && fidelity_ok_; }
    const FidelitySettings& finish() const { return view_.finish; }
    const eng::TextureRef& corona() const { return corona_; }
    void add_light(const PipeLight& light) { lights_.push_back(light); }
    // Between the solid world and what is painted over it (smoke, flashes, the gun in hand):
    // Fidelity's contact shadow, lights and sky, laid on what is drawn so far. Nothing otherwise.
    // From here on only the picture is bound: the world's shaders go back to their plain selves.
    void shade(const Camera& camera, const PipeSky& sky);
    // Out to the window, which is left bound with the viewport over all of it.
    void end();
    bool drawing() const { return drawing_; }

    // The picture being drawn: its size and shape (the world's cameras take the shape).
    int width() const { return view_.width; }
    int height() const { return view_.height; }
    float aspect() const { return view_.height > 0 ? float(view_.width) / float(view_.height) : 16.0f / 9.0f; }
    // The samples the picture has (what the card gave).
    int samples() const { return samples_made_; }
    size_t gpu_bytes() const;
    int lights_drawn() const { return lights_drawn_; }   // last frame's (the profiler, tests)
    int lights_peak() const { return lights_peak_; }     // the most any frame has drawn

private:
    struct Target {
        eng::TextureRef tex;
        int w = 0, h = 0;
    };
    bool make(Target& t, int w, int h, eng::Format format, int samples = 1);
    bool make_targets(int w, int h, int samples);
    void make_fidelity_targets();
    bool make_surface_targets();
    void pass(Target& out, const eng::ProgramRef& program, std::initializer_list<const eng::Texture*> inputs, const Target* src,
              const eng::BlendState* blend = nullptr);
    void upload(int dst_w, int dst_h, int src_w, int src_h);
    void bind_scene(bool surfaces);
    void quad_states();
    void draw_lights(const Camera& cam, const PipeSky& sky);
    void draw_sky_body(const Camera& cam, const PipeSky& sky);
    void draw_reflections();
    // A pass over part of the picture: the viewport and Rect for a view-space sphere (false: none of it shows).
    bool rect_for(const eng::Vec3& view_centre, float radius, float tx, float ty);

    eng::Device* device_ = nullptr;
    FrameView view_;
    bool drawing_ = false;
    int samples_asked_ = 0, samples_made_ = 1;
    Target colour_, resolved_, ldr_, fxaa_, lin_depth_, ao_, ao_blur_;
    Target bloom_[5], shaft_[2];
    // The surface targets, as drawn (multisampled with the picture) and as read.
    Target surf_, albedo_, surf_read_, albedo_read_;
    Target scene_copy_, ssr_;            // reflections: the picture so far, and what mirrors of it (half size)
    eng::TextureRef depth_;
    eng::TextureRef corona_, moon_;
    eng::ProgramRef ps_copy_, ps_linearize_, ps_linearize_ms_, ps_ao_, ps_ao_blur_, ps_ao_apply_, ps_prefilter_, ps_down_, ps_up_, ps_final_,
        ps_fxaa_, ps_shaft_mask_, ps_shaft_blur_, ps_light_, ps_sun_glint_, ps_sky_body_, ps_ssr_, ps_ssr_apply_;
    bool fidelity_ok_ = false, lights_ok_ = false;
    bool surfaces_ = false;              // this frame: the surface targets are bound with the picture
    bool depth_read_ = false;            // this frame's depth is in lin_depth_
    float sun_u_ = 0.5f, sun_v_ = 0.5f, sun_seen_ = 0;   // the sun on the picture; how squarely it is faced (0: behind)
    std::vector<PipeLight> lights_;
    int lights_drawn_ = 0, lights_peak_ = 0;
    eng::BufferRef cb_;
    const eng::Sampler *lin_ = nullptr, *pnt_ = nullptr;
    const eng::BlendState *add_ = nullptr, *multiply_ = nullptr, *over_ = nullptr;
    struct Constants {
        float dst[4], src[4], proj[4], bloom[4], ao[4], grade[4], finish[4], shaft[4], counts[4];
        float rect[4], cam_r[4], cam_u[4], cam_f[4], light_pos[4], light_col[4], light_aux[4];
        float sun_v[4], sun_col[4], sun_up[4], body[4], body_col[4], body_uv[4];
    } cb_data_{};
};

}  // namespace lsf
