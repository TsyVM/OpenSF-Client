// The graphics device on OpenGL ES 3 (Android; on Windows through ANGLE, --gles).
//
// The device's conventions are Direct3D's (Device.hpp); this is where OpenGL meets them:
//   - The programs are the GLSL made from each HLSL entry at build time (Tools/shaders_gen.py).
//     Their depth is already -1..1, and each carries `dev_Flip`: drawing into a texture turns the
//     picture upside down (x = -1), so a texture drawn into keeps its top row first as in
//     Direct3D, and a later pass reads it the same way it reads a texture loaded from a file.
//     Front faces turn with it. The window is drawn the right way up (x = 1), and gl_FragCoord.y
//     is counted from its top by y and z.
//   - The HLSL's slots: each combined sampler (SPIRV_Cross_Combined<texture><sampler>) gets a
//     texture unit of its own, fed from the texture slot and sampler slot it names; each uniform
//     block (type_<cbuffer>) is bound to its cbuffer's slot.
//   - Block-compressed textures (DXT1/3/5) go to the card as they are where it takes them, and
//     are decoded to RGBA8 where it does not (many phones).
#include "Engine/Render/Device.hpp"

#include "Engine/Core/Log.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(_WIN32) || defined(__APPLE__)
#include <EGL/eglext_angle.h>
#endif
#include <GLES3/gl32.h>

#include <vangui/vangui.h>
#include <vangui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

namespace {

// ── Formats ────────────────────────────────────────────────────────────────────

struct GlFormat {
    GLenum internal = 0, format = 0, type = 0;
    bool compressed = false;
};

GlFormat gl_format(Format f) {
    switch (f) {
        case Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case Format::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case Format::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT};
        case Format::RG16F: return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
        case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case Format::R11G11B10F: return {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV};
        case Format::R32F: return {GL_R32F, GL_RED, GL_FLOAT};
        case Format::BC1: return {0x83F1, 0, 0, true};   // COMPRESSED_RGBA_S3TC_DXT1_EXT
        case Format::BC2: return {0x83F2, 0, 0, true};   // DXT3
        case Format::BC3: return {0x83F3, 0, 0, true};   // DXT5
        case Format::D24S8: return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
        case Format::D32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
    }
    return {};
}

// ── DXT decoding, for cards without S3TC ───────────────────────────────────────

void rgb565(u16 c, u8* out) {
    out[0] = u8(((c >> 11) & 31) * 255 / 31);
    out[1] = u8(((c >> 5) & 63) * 255 / 63);
    out[2] = u8((c & 31) * 255 / 31);
}

// One 4x4 block's colours (the DXT1 part every format shares). `four` = four colours always (DXT3/5).
void colour_block(const u8* b, bool four, u8 rgba[16][4]) {
    const u16 c0 = u16(b[0] | (b[1] << 8)), c1 = u16(b[2] | (b[3] << 8));
    u8 p[4][4];
    rgb565(c0, p[0]);
    rgb565(c1, p[1]);
    p[0][3] = p[1][3] = 255;
    if (c0 > c1 || four) {
        for (int k = 0; k < 3; ++k) {
            p[2][k] = u8((2 * p[0][k] + p[1][k] + 1) / 3);
            p[3][k] = u8((p[0][k] + 2 * p[1][k] + 1) / 3);
        }
        p[2][3] = p[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) p[2][k] = u8((p[0][k] + p[1][k]) / 2);
        p[2][3] = 255;
        p[3][0] = p[3][1] = p[3][2] = p[3][3] = 0;
    }
    const u32 bits = u32(b[4]) | (u32(b[5]) << 8) | (u32(b[6]) << 16) | (u32(b[7]) << 24);
    for (int i = 0; i < 16; ++i) std::memcpy(rgba[i], p[(bits >> (2 * i)) & 3], 4);
}

// A DXT level to RGBA8 (rows `w` texels apart).
std::vector<u8> decode_dxt(Format f, const u8* src, int w, int h, u32 row_pitch) {
    std::vector<u8> out(size_t(w) * size_t(h) * 4, 0);
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const int block = f == Format::BC1 ? 8 : 16;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const u8* b = src + size_t(by) * row_pitch + size_t(bx) * size_t(block);
            u8 px[16][4];
            if (f == Format::BC1) {
                colour_block(b, false, px);
            } else {
                colour_block(b + 8, true, px);
                if (f == Format::BC2) {
                    for (int i = 0; i < 16; ++i) {
                        const int a = (b[i / 2] >> ((i & 1) * 4)) & 15;
                        px[i][3] = u8(a * 17);
                    }
                } else {
                    const int a0 = b[0], a1 = b[1];
                    int a[8] = {a0, a1};
                    if (a0 > a1) {
                        for (int k = 1; k < 7; ++k) a[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7;
                    } else {
                        for (int k = 1; k < 5; ++k) a[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5;
                        a[6] = 0;
                        a[7] = 255;
                    }
                    u64 bits = 0;
                    for (int k = 0; k < 6; ++k) bits |= u64(b[2 + k]) << (8 * k);
                    for (int i = 0; i < 16; ++i) px[i][3] = u8(a[(bits >> (3 * i)) & 7]);
                }
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) std::memcpy(&out[(size_t(y) * size_t(w) + size_t(x)) * 4], px[i], 4);
            }
        }
    return out;
}

// ── Resources ──────────────────────────────────────────────────────────────────

class DeviceGLES;

class TextureGL final : public Texture {
public:
    TextureGL(const TextureDesc& d) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    ~TextureGL() override;
    u64 ui_id() const override { return u64(name); }
    void describe(const TextureDesc& d) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    GLenum target() const {
        if (desc_.type == TextureType::Cube) return GL_TEXTURE_CUBE_MAP;
        if (desc_.type == TextureType::Array) return GL_TEXTURE_2D_ARRAY;
        return desc_.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    }
    GLuint name = 0;
    bool window = false;       // the window's own (framebuffer 0), not a texture
    bool filterable = true;    // R32F without OES_texture_float_linear, depth without compare
    DeviceGLES* owner = nullptr;
    u32 id = 0;                // for the framebuffer cache
};

class BufferGL final : public Buffer {
public:
    BufferGL(BufferKind k, size_t n) {
        kind_ = k;
        bytes_ = n;
    }
    ~BufferGL() override {
        if (name) glDeleteBuffers(1, &name);
    }
    GLuint name = 0;
    GLenum target = GL_ARRAY_BUFFER;
    bool dynamic = false;
    // A dynamic uniform buffer streamed through the ring: its latest bytes, and where in the
    // ring they went (ring_version == version: already there this frame).
    std::vector<u8> shadow;
    u64 version = 1, ring_version = 0, ring_frame = 0;
    size_t ring_offset = 0;
};

struct Attr {
    GLint location = -1;
    AttrFormat format = AttrFormat::Float3;
    u32 offset = 0;
};

// A texture unit a program samples: fed from a texture slot with a sampler slot's state.
struct Unit {
    int texture = -1, sampler = -1;
};

class ProgramGL final : public Program {
public:
    ~ProgramGL() override {
        if (name) glDeleteProgram(name);
    }
    GLuint name = 0;
    std::vector<Attr> attrs;
    std::vector<Unit> units;
    GLint flip = -1;
    float flip_value[3] = {0, 0, 0};
};

class BlendGL final : public BlendState {
public:
    BlendDesc d;
};
class RasterGL final : public RasterState {
public:
    RasterDesc d;
};
class DepthGL final : public DepthState {
public:
    DepthDesc d;
};
class SamplerGL final : public Sampler {
public:
    SamplerDesc d;
    GLuint name = 0, nearest = 0;   // `nearest`: the same, unfiltered (textures that cannot be filtered)
};

GLenum blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColour: return GL_SRC_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DestColour: return GL_DST_COLOR;
    }
    return GL_ONE;
}

GLenum primitive(Topology t) {
    switch (t) {
        case Topology::Triangles: return GL_TRIANGLES;
        case Topology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case Topology::Lines: return GL_LINES;
    }
    return GL_TRIANGLES;
}

// The semantic's name as the GLSL's vertex inputs carry it (in_var_<SEMANTIC>[0]).
const char* semantic_name(Semantic s) {
    switch (s) {
        case Semantic::Position: return "POSITION";
        case Semantic::Normal: return "NORMAL";
        case Semantic::TexCoord: return "TEXCOORD";
        case Semantic::Colour: return "COLOR";
        case Semantic::BlendIndices: return "BLENDINDICES";
        case Semantic::BlendWeight: return "BLENDWEIGHT";
    }
    return "";
}

// "t:Tex=0,EnvTex=1;s:Samp=0;b:type_Frame=0" into its three lists.
struct Bindings {
    std::vector<std::pair<std::string, int>> t, s, b;
};
Bindings parse_bindings(std::string_view text) {
    Bindings out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(';', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view part = text.substr(at, end - at);
        if (part.size() > 2 && part[1] == ':') {
            auto& list = part[0] == 't' ? out.t : part[0] == 's' ? out.s : out.b;
            size_t p = 2;
            while (p < part.size()) {
                size_t q = part.find(',', p);
                if (q == std::string_view::npos) q = part.size();
                const std::string_view item = part.substr(p, q - p);
                if (const size_t eq = item.find('='); eq != std::string_view::npos)
                    list.emplace_back(std::string(item.substr(0, eq)), std::atoi(std::string(item.substr(eq + 1)).c_str()));
                p = q + 1;
            }
        }
        at = end + 1;
    }
    return out;
}

// OpenGL ES 3.1 and 3.2 calls, taken from the driver where it has them.
struct Late {
    PFNGLTEXSTORAGE2DMULTISAMPLEPROC tex_storage_ms = nullptr;
    PFNGLCOLORMASKIPROC color_mask_i = nullptr;
    // EXT_buffer_storage: a buffer mapped once and written for good (the uniform ring).
    void (*buffer_storage)(GLenum, GLsizeiptr, const void*, GLbitfield) = nullptr;
};

// The per-draw uniforms (each object's constants, a character's bones) go into one big buffer
// mapped for good, a third of it per frame, each piece bound by its range: a copy in memory and
// one bind a draw, where making each buffer anew for every draw cost a phone's driver more than
// the drawing (16 fps with 3,600 draws). A fence keeps a third from being written while the
// card still reads it. Without EXT_buffer_storage the buffers are made anew as before.
struct UniformRing {
    static constexpr int kParts = 3;
    static constexpr size_t kPart = 8u << 20;
    GLuint name = 0;
    u8* mapped = nullptr;
    size_t align = 256, head = 0;
    int part = 0;
    GLsync fence[kParts]{};
    u64 frame = 1;   // counts presents: a buffer's bytes are rewritten once each frame they are used
    bool ok() const { return mapped != nullptr; }
};

class DeviceGLES final : public Device {
public:
    ~DeviceGLES() override { destroy(); }
    Api api() const override { return Api::GLES; }

    bool create(void* window, int width, int height, const Options& o) override;
    void destroy() override;
    bool resize(int width, int height) override;
    bool attach_window(void* window) override;
    void detach_window() override;
    void begin_frame(const Color& clear) override;
    void present() override;
    bool capture(Image& out) override;
    Texture* back_buffer() override { return &back_; }
    Texture* back_depth() override { return &back_depth_; }

    TextureRef create_texture(const TextureDesc& desc, std::span<const TextureData> init) override;
    void update_texture(Texture& texture, const void* data, u32 row_pitch) override;
    BufferRef create_buffer(BufferKind kind, const void* data, size_t bytes, bool dynamic) override;
    void update_buffer(Buffer& buffer, const void* data, size_t bytes) override;
    ProgramRef create_program(const ShaderSource& source, const char* vs_entry, const char* ps_entry, std::span<const VertexAttr> layout,
                              std::string* error) override;
    int samples_for(Format format, int want) override;

    const BlendState* blend(const BlendDesc& desc) override;
    const RasterState* raster(const RasterDesc& desc) override;
    const DepthState* depth(const DepthDesc& desc) override;
    const Sampler* sampler(const SamplerDesc& desc) override;

    void set_targets(std::span<const Target> colour, Target depth) override;
    void set_viewport(const Viewport& v) override;
    void clear(Target target, const Color& colour) override;
    void clear_depth(Target target, float depth) override;
    void set_program(const Program* program) override { program_ = static_cast<const ProgramGL*>(program); }
    void set_blend(const BlendState* state) override { blend_ = static_cast<const BlendGL*>(state); }
    void set_raster(const RasterState* state) override { raster_ = static_cast<const RasterGL*>(state); }
    void set_depth(const DepthState* state) override { depth_state_ = static_cast<const DepthGL*>(state); }
    void set_uniforms(int slot, const Buffer* buffer) override;
    void set_texture(int slot, const Texture* texture) override {
        if (slot >= 0 && slot < kSlots) textures_[slot] = static_cast<const TextureGL*>(texture);
    }
    void set_sampler(int slot, const Sampler* sampler) override {
        if (slot >= 0 && slot < kSlots) samplers_[slot] = static_cast<const SamplerGL*>(sampler);
    }
    void set_vertices(const Buffer* buffer, u32 stride) override {
        vb_ = static_cast<const BufferGL*>(buffer);
        stride_ = stride;
    }
    void set_indices(const Buffer* buffer) override { ib_ = static_cast<const BufferGL*>(buffer); }
    void draw(Topology t, u32 count, u32 first) override;
    void draw_indexed(Topology t, u32 count, u32 first) override;
    void resolve(Texture& dst, const Texture& src) override;

    bool ui_init() override {
        ui_ = VanGui_ImplOpenGL3_Init("#version 300 es");
        return ui_;
    }
    void ui_shutdown() override {
        if (ui_) VanGui_ImplOpenGL3_Shutdown();
        ui_ = false;
    }
    void ui_new_frame() override { VanGui_ImplOpenGL3_NewFrame(); }
    void ui_render() override {
        // VanGUI draws into the window, the right way up, with its own state.
        bind_framebuffer(0);
        glViewport(0, 0, width_, height_);
        VanGui_ImplOpenGL3_RenderDrawData(VanGui::GetDrawData());
        forget();
    }

    std::unique_ptr<TimerFrame> create_timer_frame(int) override { return nullptr; }
    bool video_memory(size_t&, size_t&) const override { return false; }

    // A texture going away: the framebuffers that hold it go too.
    void forget_texture(u32 id);

private:
    static constexpr int kSlots = 16;
    bool make_surface(void* window);
    void forget();
    void apply_state();
    void bind_framebuffer(GLuint fbo) {
        if (fbo != bound_fbo_) glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        bound_fbo_ = fbo;
    }
    GLuint framebuffer_for(std::span<const Target> colour, Target depth);
    void attach(GLenum point, const Target& t);
    bool has_ext(const char* name) const { return extensions_.find(std::string(" ") + name + " ") != std::string::npos; }
    GLuint compile(const ShaderSource& src, const char* entry, GLenum stage, std::string* error, int version = 300);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    std::string backend_;   // Windows: ANGLE's back end in use
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    int gl_major_ = 3, gl_minor_ = 0;
    std::string extensions_;
    bool s3tc_ = false, float_linear_ = false, border_ = false, aniso_ = false;
    int max_samples_ = 1;
    int swap_interval_ = -1;
    Late late_;
    UniformRing ring_;
    void make_ring();
    void next_ring_part();
    // Where a dynamic uniform buffer's latest bytes are in the ring (written there now if not yet).
    size_t ring_place(BufferGL& b);
    bool ui_ = false;
    TextureGL back_{TextureDesc{}}, back_depth_{TextureDesc{TextureType::Plain, Format::D24S8}};
    GLuint vao_ = 0;
    u32 next_texture_id_ = 1;

    std::vector<std::pair<BlendDesc, std::unique_ptr<BlendGL>>> blends_;
    std::vector<std::pair<RasterDesc, std::unique_ptr<RasterGL>>> rasters_;
    std::vector<std::pair<DepthDesc, std::unique_ptr<DepthGL>>> depths_;
    std::vector<std::pair<SamplerDesc, std::unique_ptr<SamplerGL>>> samplers_list_;
    std::map<std::pair<const char*, std::string>, GLuint> shaders_;

    // Framebuffers by what is attached: (texture id, layer) per colour slot, then the depth.
    struct FboKey {
        u32 c[4][2]{};
        u32 d[2]{};
        bool operator<(const FboKey& o) const { return std::memcmp(this, &o, sizeof(FboKey)) < 0; }
    };
    std::map<FboKey, GLuint> fbos_;
    GLuint scratch_fbo_ = 0;

    // What the draws ask for.
    const ProgramGL* program_ = nullptr;
    const BlendGL* blend_ = nullptr;
    const RasterGL* raster_ = nullptr;
    const DepthGL* depth_state_ = nullptr;
    const TextureGL* textures_[kSlots]{};
    const SamplerGL* samplers_[kSlots]{};
    const BufferGL* uniforms_[kSlots]{};
    const BufferGL* vb_ = nullptr;
    const BufferGL* ib_ = nullptr;
    u32 stride_ = 0;
    GLuint bound_fbo_ = 0;
    bool flipped_ = false;             // drawing into a texture (upside down)
    u8 target_mask_ = 0;               // which colour slots have a texture
    int target_count_ = 0;

    // What OpenGL holds.
    struct {
        const ProgramGL* program = nullptr;
        const BlendGL* blend = nullptr;
        u8 draw_mask = 0xFF;
        bool a2c = false;
        const RasterGL* raster = nullptr;
        bool flipped = false;
        const DepthGL* depth = nullptr;
        GLuint unit_tex[32]{};
        GLuint unit_samp[32]{};
        GLenum unit_target[32]{};
        GLuint ubo[kSlots]{};
        size_t ubo_offset[kSlots]{}, ubo_size[kSlots]{};
        const BufferGL* vb = nullptr;
        const ProgramGL* vb_program = nullptr;
        u32 attrs = 0;
        u32 stride = 0;
        GLuint ib = 0;
        bool valid = false;
    } gl_;
};

TextureGL::~TextureGL() {
    if (owner) owner->forget_texture(id);
    if (name && !window) glDeleteTextures(1, &name);
}

bool DeviceGLES::make_surface(void* window) {
#if defined(_WIN32) || defined(__ANDROID__) || defined(__APPLE__)
    surface_ = eglCreateWindowSurface(display_, config_, EGLNativeWindowType(window), nullptr);
#else
    // Linux: the X11 window's id (an integer the window layer carries as a pointer).
    surface_ = eglCreateWindowSurface(display_, config_, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
#endif
    if (surface_ == EGL_NO_SURFACE) {
        LOG_ERROR("eglCreateWindowSurface failed (0x%x)", unsigned(eglGetError()));
        return false;
    }
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOG_ERROR("eglMakeCurrent failed (0x%x)", unsigned(eglGetError()));
        return false;
    }
    EGLint w = 0, h = 0;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
    if (w > 0 && h > 0) {
        width_ = w;
        height_ = h;
    }
    eglSwapInterval(display_, vsync_ ? 1 : 0);
    return true;
}

bool DeviceGLES::create(void* window, int width, int height, const Options& o) {
    vsync_ = o.vsync;
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    EGLint major = 0, minor = 0;
#ifdef _WIN32
    // ANGLE: the back end asked for (the card's desktop OpenGL driver unless a check wants
    // another), then its own choice (Direct3D 11) when that one cannot start.
    {
        const std::string_view want = o.angle_backend ? o.angle_backend : "opengl";
        const EGLint type = want == "d3d11"    ? EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE
                            : want == "vulkan" ? EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE
                                               : EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE;
        const auto get_platform_display =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (get_platform_display) {
            const EGLint attrs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, type, EGL_NONE};
            display_ = get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), attrs);
            if (display_ != EGL_NO_DISPLAY && eglInitialize(display_, &major, &minor)) {
                backend_ = std::string(want);
            } else {
                LOG_WARN("ANGLE: its %.*s back end did not start (0x%x); trying its default", int(want.size()), want.data(),
                         unsigned(eglGetError()));
                display_ = EGL_NO_DISPLAY;
            }
        }
    }
    if (display_ == EGL_NO_DISPLAY) {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        backend_ = "default";
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor)) {
            LOG_ERROR("EGL: no display (0x%x)", unsigned(eglGetError()));
            return false;
        }
    }
#elif defined(__APPLE__)
    // macOS: ANGLE over Metal (never Apple's own OpenGL, which is deprecated and frozen).
    {
        const auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        const EGLint attrs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
        if (get_platform_display) display_ = get_platform_display(EGL_PLATFORM_ANGLE_ANGLE, reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), attrs);
        backend_ = "metal";
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor)) {
            LOG_ERROR("EGL: ANGLE's Metal back end did not start (0x%x)", unsigned(eglGetError()));
            return false;
        }
    }
#else
    // Android: the system's display. Linux: the X11 display the window is on.
    display_ = eglGetDisplay(o.native_display ? static_cast<EGLNativeDisplayType>(o.native_display) : EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor)) {
        LOG_ERROR("EGL: no display (0x%x)", unsigned(eglGetError()));
        return false;
    }
#endif
    eglBindAPI(EGL_OPENGL_ES_API);
    // The window: 8-bit colour, no depth of its own (the scene has its own targets).
    const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE};
    EGLint count = 0;
    EGLConfig configs[64];
    if (!eglChooseConfig(display_, attribs, configs, 64, &count) || count == 0) {
        LOG_ERROR("EGL: no OpenGL ES 3 configuration");
        return false;
    }
    // The closest to plain RGB888 (drivers list deeper ones first).
    config_ = configs[0];
    for (int i = 0; i < count; ++i) {
        EGLint r = 0, g = 0, b = 0, a = 0, d = 0;
        eglGetConfigAttrib(display_, configs[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(display_, configs[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(display_, configs[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(display_, configs[i], EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(display_, configs[i], EGL_DEPTH_SIZE, &d);
        if (r == 8 && g == 8 && b == 8 && d == 0) {
            config_ = configs[i];
            if (a == 0) break;
        }
    }
    // The newest OpenGL ES 3 the driver makes.
    for (const int v : {2, 1, 0}) {
        const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, v, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx);
        if (context_ != EGL_NO_CONTEXT) break;
    }
    if (context_ == EGL_NO_CONTEXT) {
        const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctx);
    }
    if (context_ == EGL_NO_CONTEXT) {
        LOG_ERROR("EGL: no OpenGL ES 3 context (0x%x)", unsigned(eglGetError()));
        return false;
    }
    if (!make_surface(window)) return false;

    glGetIntegerv(GL_MAJOR_VERSION, &gl_major_);
    glGetIntegerv(GL_MINOR_VERSION, &gl_minor_);
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    extensions_ = " ";
    for (GLint i = 0; i < n; ++i)
        if (const auto* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)))) extensions_ += std::string(e) + " ";
    s3tc_ = has_ext("GL_EXT_texture_compression_s3tc") ||
            (has_ext("GL_EXT_texture_compression_dxt1") && has_ext("GL_ANGLE_texture_compression_dxt3") && has_ext("GL_ANGLE_texture_compression_dxt5"));
    float_linear_ = has_ext("GL_OES_texture_float_linear");
    const bool es32 = gl_major_ > 3 || (gl_major_ == 3 && gl_minor_ >= 2);
    border_ = es32 || has_ext("GL_EXT_texture_border_clamp") || has_ext("GL_OES_texture_border_clamp");
    aniso_ = has_ext("GL_EXT_texture_filter_anisotropic");
    if (gl_major_ > 3 || gl_minor_ >= 1) {
        late_.tex_storage_ms = reinterpret_cast<PFNGLTEXSTORAGE2DMULTISAMPLEPROC>(eglGetProcAddress("glTexStorage2DMultisample"));
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples_);
    }
    if (es32) late_.color_mask_i = reinterpret_cast<PFNGLCOLORMASKIPROC>(eglGetProcAddress("glColorMaski"));
    // Render targets in half and full floats (bloom, HDR, depth, motion) need this before 3.2.
    if (!es32 && !has_ext("GL_EXT_color_buffer_float") && !has_ext("GL_EXT_color_buffer_half_float"))
        LOG_WARN("OpenGL ES: no float render targets (EXT_color_buffer_float): effects will be missing");

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    adapter_name_ = renderer ? renderer : "OpenGL ES";
    const std::string v = vendor ? vendor : "";
    vendor_ = v.find("Qualcomm") != std::string::npos ? 0x5143 : v.find("ARM") != std::string::npos ? 0x13B5 : v.find("Google") != std::string::npos ? 0x1AE0 : 0;
    LOG_INFO("GPU: %s (%s), %s; EGL %d.%d; DXT %s, float filtering %s, %d samples at most", adapter_name_.c_str(), vendor ? vendor : "?",
             version ? version : "?", major, minor, s3tc_ ? "on the card" : "decoded", float_linear_ ? "yes" : "no", max_samples_);
#ifdef _WIN32
    // ANGLE's vendor is "Google Inc. (Intel)", its renderer names the card underneath:
    // "ANGLE (Intel, Intel(R) Arc(TM) 140V GPU ... OpenGL 4.6 ...)".
    if (vendor_ == 0 || vendor_ == 0x1AE0) {
        vendor_ = 0;
        if (adapter_name_.find("Intel") != std::string::npos) vendor_ = 0x8086;
        else if (adapter_name_.find("NVIDIA") != std::string::npos) vendor_ = 0x10DE;
        else if (adapter_name_.find("AMD") != std::string::npos || adapter_name_.find("Radeon") != std::string::npos) vendor_ = 0x1002;
    }
    LOG_INFO("OpenGL through ANGLE's %s back end", backend_.c_str());
#endif

    if (has_ext("GL_EXT_buffer_storage"))
        late_.buffer_storage = reinterpret_cast<void (*)(GLenum, GLsizeiptr, const void*, GLbitfield)>(eglGetProcAddress("glBufferStorageEXT"));
    make_ring();
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenFramebuffers(1, &scratch_fbo_);
    // Direct3D ends a strip at index 0xFFFFFFFF.
    glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    back_.window = true;
    back_depth_.window = true;
    TextureDesc bd;
    bd.width = width_;
    bd.height = height_;
    bd.target = true;
    back_.describe(bd);
    return true;
}

bool DeviceGLES::attach_window(void* window) {
    if (!window || context_ == EGL_NO_CONTEXT) return false;
    detach_window();
    if (!make_surface(window)) return false;
    forget();
    return true;
}

void DeviceGLES::detach_window() {
    if (display_ == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
}

void DeviceGLES::make_ring() {
    if (!late_.buffer_storage) {
        LOG_INFO("OpenGL ES: no EXT_buffer_storage; uniforms are made anew for each draw");
        return;
    }
    GLint align = 256;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    ring_.align = size_t(std::max(align, 16));
    constexpr GLbitfield kWrite = GL_MAP_WRITE_BIT | 0x0040 /* PERSISTENT */ | 0x0080 /* COHERENT */;
    const size_t total = UniformRing::kPart * UniformRing::kParts;
    glGenBuffers(1, &ring_.name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, ring_.name);
    late_.buffer_storage(GL_COPY_WRITE_BUFFER, GLsizeiptr(total), nullptr, kWrite);
    ring_.mapped = static_cast<u8*>(glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, GLsizeiptr(total), kWrite));
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    if (!ring_.mapped) {
        LOG_WARN("OpenGL ES: the uniform ring could not be mapped (0x%x); uniforms are made anew for each draw", unsigned(glGetError()));
        glDeleteBuffers(1, &ring_.name);
        ring_.name = 0;
        return;
    }
    LOG_INFO("OpenGL ES: uniforms stream through a %zu MB ring (offsets aligned to %zu)", total >> 20, ring_.align);
}

void DeviceGLES::next_ring_part() {
    if (!ring_.ok()) return;
    ring_.fence[ring_.part] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    ring_.part = (ring_.part + 1) % UniformRing::kParts;
    ring_.head = 0;
    // The card is done with this third before it is written again (it nearly always is).
    if (GLsync f = ring_.fence[ring_.part]) {
        glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(f);
        ring_.fence[ring_.part] = nullptr;
    }
}

size_t DeviceGLES::ring_place(BufferGL& b) {
    const size_t size = b.shadow.size();
    // Placed this frame and unchanged since: the same bytes serve (a third older is written over).
    if (b.ring_version == b.version && b.ring_frame == ring_.frame) return b.ring_offset;
    size_t at = (ring_.head + ring_.align - 1) / ring_.align * ring_.align;
    if (at + size > UniformRing::kPart) {
        // A frame fuller than a third: on to the next (waiting for the card if it must).
        next_ring_part();
        at = 0;
    }
    const size_t offset = size_t(ring_.part) * UniformRing::kPart + at;
    std::memcpy(ring_.mapped + offset, b.shadow.data(), size);
    ring_.head = at + size;
    b.ring_offset = offset;
    b.ring_version = b.version;
    b.ring_frame = ring_.frame;
    return offset;
}

void DeviceGLES::destroy() {
    if (display_ == EGL_NO_DISPLAY) return;
    if (surface_ != EGL_NO_SURFACE || context_ != EGL_NO_CONTEXT) {
        if (ring_.name) {
            for (GLsync& f : ring_.fence)
                if (f) glDeleteSync(f), f = nullptr;
            glBindBuffer(GL_COPY_WRITE_BUFFER, ring_.name);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            glDeleteBuffers(1, &ring_.name);
            ring_ = {};
        }
        for (auto& [k, v] : samplers_list_) {
            glDeleteSamplers(1, &v->name);
            glDeleteSamplers(1, &v->nearest);
        }
        for (auto& [k, f] : fbos_) glDeleteFramebuffers(1, &f);
        for (auto& [k, s] : shaders_) glDeleteShader(s);
        if (scratch_fbo_) glDeleteFramebuffers(1, &scratch_fbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
    }
    samplers_list_.clear();
    blends_.clear();
    rasters_.clear();
    depths_.clear();
    fbos_.clear();
    shaders_.clear();
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    eglTerminate(display_);
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
}

bool DeviceGLES::resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    width_ = width;
    height_ = height;
    TextureDesc bd = back_.desc();
    bd.width = width_;
    bd.height = height_;
    back_.describe(bd);
    return true;
}

void DeviceGLES::forget() {
    gl_ = {};
    bound_fbo_ = ~0u;
    glBindVertexArray(vao_);
}

void DeviceGLES::begin_frame(const Color& clear_colour) {
    // The window's size as it is now (a phone turning, a window resized).
    if (surface_ != EGL_NO_SURFACE) {
        EGLint w = 0, h = 0;
        eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
        if (w > 0 && h > 0 && (w != width_ || h != height_)) resize(w, h);
    }
    const Target colour{&back_};
    set_targets(std::span<const Target>(&colour, 1), Target{});
    clear(colour, clear_colour);
    set_viewport({0, 0, float(width_), float(height_)});
}

void DeviceGLES::present() {
    if (surface_ == EGL_NO_SURFACE || lost_) return;
    next_ring_part();
    ++ring_.frame;
    // Nothing of the frame's targets is needed once it is shown (tiled GPUs skip writing them back).
    if (!eglSwapBuffers(display_, surface_)) {
        const EGLint e = eglGetError();
        if (e == EGL_CONTEXT_LOST) {
            lost_ = true;
            LOG_ERROR("The graphics device was lost (EGL_CONTEXT_LOST)");
        } else if (e != EGL_BAD_SURFACE && e != EGL_BAD_NATIVE_WINDOW) {
            LOG_WARN("eglSwapBuffers failed (0x%x)", unsigned(e));
        }
    }
    if (swap_interval_ != int(vsync_)) {
        eglSwapInterval(display_, vsync_ ? 1 : 0);
        swap_interval_ = int(vsync_);
    }
}

bool DeviceGLES::capture(Image& out) {
    bind_framebuffer(0);
    out.width = u32(width_);
    out.height = u32(height_);
    out.rgba.resize(size_t(width_) * size_t(height_) * 4);
    std::vector<u8> raw(out.rgba.size());
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    // OpenGL's rows start at the bottom.
    const size_t row = size_t(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        u8* dst = out.rgba.data() + size_t(y) * row;
        std::memcpy(dst, raw.data() + size_t(height_ - 1 - y) * row, row);
        for (int x = 0; x < width_; ++x) dst[x * 4 + 3] = 255;
    }
    return glGetError() == GL_NO_ERROR;
}

TextureRef DeviceGLES::create_texture(const TextureDesc& d, std::span<const TextureData> init) {
    const bool compressed = is_block_compressed(d.format);
    const bool decode = compressed && !s3tc_;
    TextureDesc real = d;
    if (decode) real.format = Format::RGBA8;
    auto t = std::make_shared<TextureGL>(real);
    t->owner = this;
    t->id = next_texture_id_++;
    const GlFormat f = gl_format(real.format);
    const int layers = d.type == TextureType::Cube ? 6 : std::max(1, d.layers);
    const int mips = std::max(1, d.mips);
    const int w = std::max(1, d.width), h = std::max(1, d.height);
    if (!init.empty() && init.size() != size_t(layers) * size_t(mips)) return nullptr;
    t->filterable = !(real.format == Format::R32F && !float_linear_) && !is_depth(real.format);
    const GLenum target = t->target();
    glGenTextures(1, &t->name);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, t->name);
    gl_.unit_tex[0] = ~0u;
    if (d.samples > 1) {
        if (!late_.tex_storage_ms) return nullptr;
        late_.tex_storage_ms(target, d.samples, f.internal, w, h, GL_TRUE);
    } else if (target == GL_TEXTURE_2D_ARRAY) {
        glTexStorage3D(target, mips, f.internal, w, h, layers);
    } else {
        glTexStorage2D(target, mips, f.internal, w, h);
    }
    if (d.samples <= 1) {
        // What VanGUI draws it with (the device's own draws use sampler objects).
        const bool linear = t->filterable;
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, mips - 1);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, !linear ? GL_NEAREST : mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    }
    for (int layer = 0; layer < layers && !init.empty(); ++layer) {
        int mw = w, mh = h;
        for (int m = 0; m < mips; ++m) {
            const TextureData& s = init[size_t(layer) * size_t(mips) + size_t(m)];
            const GLenum face = target == GL_TEXTURE_CUBE_MAP ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer) : target;
            if (decode) {
                const std::vector<u8> rgba = decode_dxt(d.format, static_cast<const u8*>(s.data), mw, mh, s.row_pitch);
                if (target == GL_TEXTURE_2D_ARRAY) glTexSubImage3D(target, m, 0, 0, layer, mw, mh, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                else glTexSubImage2D(face, m, 0, 0, mw, mh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            } else if (compressed) {
                const GLsizei size = GLsizei(s.row_pitch) * GLsizei((mh + 3) / 4);
                if (target == GL_TEXTURE_2D_ARRAY) glCompressedTexSubImage3D(target, m, 0, 0, layer, mw, mh, 1, f.internal, size, s.data);
                else glCompressedTexSubImage2D(face, m, 0, 0, mw, mh, f.internal, size, s.data);
            } else {
                const u32 tb = texel_bytes(real.format);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(s.row_pitch / std::max<u32>(1, tb)));
                if (target == GL_TEXTURE_2D_ARRAY) glTexSubImage3D(target, m, 0, 0, layer, mw, mh, 1, f.format, f.type, s.data);
                else glTexSubImage2D(face, m, 0, 0, mw, mh, f.format, f.type, s.data);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
        }
    }
    if (const GLenum e = glGetError(); e != GL_NO_ERROR) {
        LOG_WARN("OpenGL ES: texture %dx%d format %d failed (0x%x)", w, h, int(real.format), unsigned(e));
        return nullptr;
    }
    return t;
}

void DeviceGLES::update_texture(Texture& texture, const void* data, u32 row_pitch) {
    auto& t = static_cast<TextureGL&>(texture);
    if (!t.name || t.desc().type != TextureType::Plain) return;
    const GlFormat f = gl_format(t.desc().format);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.name);
    gl_.unit_tex[0] = ~0u;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(row_pitch / std::max<u32>(1, texel_bytes(t.desc().format))));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.width(), t.height(), f.format, f.type, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

BufferRef DeviceGLES::create_buffer(BufferKind kind, const void* data, size_t bytes, bool dynamic) {
    auto b = std::make_shared<BufferGL>(kind, bytes);
    b->target = kind == BufferKind::Vertex ? GL_ARRAY_BUFFER : kind == BufferKind::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_UNIFORM_BUFFER;
    b->dynamic = dynamic;
    if (kind == BufferKind::Uniform && dynamic && ring_.ok()) {
        b->shadow.assign(bytes, 0);
        if (data) std::memcpy(b->shadow.data(), data, bytes);
        return b;
    }
    glGenBuffers(1, &b->name);
    // Index buffers bind to the vertex array; made on the copy target so none is disturbed.
    glBindBuffer(GL_COPY_WRITE_BUFFER, b->name);
    glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(bytes), data, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    return b;
}

void DeviceGLES::update_buffer(Buffer& buffer, const void* data, size_t bytes) {
    auto& b = static_cast<BufferGL&>(buffer);
    if (!b.shadow.empty()) {
        std::memcpy(b.shadow.data(), data, std::min(bytes, b.shadow.size()));
        ++b.version;
        return;
    }
    // A new store each time (the old one stays with the draws that used it): no waiting on the card.
    glBindBuffer(GL_COPY_WRITE_BUFFER, b.name);
    glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(b.bytes()), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_COPY_WRITE_BUFFER, 0, GLsizeiptr(std::min(bytes, b.bytes())), data);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

const GlslEntry* find_glsl(const ShaderSource& src, const char* entry) {
    for (size_t i = 0; i < src.glsl_count; ++i)
        if (std::strcmp(src.glsl[i].entry, entry) == 0) return &src.glsl[i];
    return nullptr;
}

GLuint DeviceGLES::compile(const ShaderSource& src, const char* entry, GLenum stage, std::string* error, int version) {
    const GlslEntry* e = find_glsl(src, entry);
    const int want = e ? std::max(version, e->version) : version;
    const auto key = std::make_pair(src.name, std::string(entry) + "@" + std::to_string(want));
    if (auto it = shaders_.find(key); it != shaders_.end()) return it->second;
    if (!e) {
        const std::string msg = std::string("no GLSL for ") + src.name + " " + entry;
        LOG_ERROR("Shader %s", msg.c_str());
        if (error) *error = msg;
        return 0;
    }
    if (want > 300 && gl_major_ == 3 && gl_minor_ < 1) {
        if (error) *error = "needs OpenGL ES 3.1";
        return 0;
    }
    // A program's stages must share one version (desktop drivers under ANGLE hold to it): a stage
    // made for an older one than its partner is compiled as the newer.
    std::string text = e->glsl;
    if (want != e->version)
        if (const auto at = text.find("#version "); at != std::string::npos) {
            const auto end = text.find('\n', at);
            text.replace(at, end - at, "#version " + std::to_string(want) + " es");
        }
    const char* source = text.c_str();
    const GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOG_ERROR("Shader %s %s: %s", src.name, entry, log);
        if (error) *error = log;
        glDeleteShader(s);
        return 0;
    }
    shaders_[key] = s;
    return s;
}

ProgramRef DeviceGLES::create_program(const ShaderSource& source, const char* vs_entry, const char* ps_entry, std::span<const VertexAttr> layout,
                                      std::string* error) {
    const GlslEntry* ve = find_glsl(source, vs_entry);
    const GlslEntry* pe = find_glsl(source, ps_entry);
    const int version = std::max(ve ? ve->version : 300, pe ? pe->version : 300);
    const GLuint vs = compile(source, vs_entry, GL_VERTEX_SHADER, error, version);
    const GLuint ps = vs ? compile(source, ps_entry, GL_FRAGMENT_SHADER, error, version) : 0;
    if (!vs || !ps) return nullptr;
    auto p = std::make_shared<ProgramGL>();
    p->name = glCreateProgram();
    glAttachShader(p->name, vs);
    glAttachShader(p->name, ps);
    glLinkProgram(p->name);
    GLint ok = 0;
    glGetProgramiv(p->name, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetProgramInfoLog(p->name, sizeof(log), nullptr, log);
        LOG_ERROR("Program %s %s/%s: %s", source.name, vs_entry, ps_entry, log);
        if (error) *error = log;
        return nullptr;
    }
    glDetachShader(p->name, vs);
    glDetachShader(p->name, ps);
    // The vertex's parts, by semantic.
    for (const VertexAttr& a : layout) {
        const std::string base = std::string("in_var_") + semantic_name(a.semantic);
        GLint loc = glGetAttribLocation(p->name, (base + std::to_string(int(a.index))).c_str());
        if (loc < 0 && a.index == 0) loc = glGetAttribLocation(p->name, base.c_str());
        if (loc >= 0) p->attrs.push_back({loc, a.format, a.offset});
    }
    const Bindings b = parse_bindings(source.bindings ? source.bindings : "");
    // Uniform blocks to their cbuffer slots.
    for (const auto& [name, slot] : b.b) {
        const GLuint index = glGetUniformBlockIndex(p->name, name.c_str());
        if (index != GL_INVALID_INDEX) glUniformBlockBinding(p->name, index, GLuint(slot));
    }
    // Each combined sampler a unit, fed from the texture and sampler slots its name joins.
    glUseProgram(p->name);
    gl_.program = nullptr;
    GLint count = 0;
    glGetProgramiv(p->name, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
        char name[256];
        GLsizei len = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(p->name, GLuint(i), sizeof(name), &len, &size, &type, name);
        std::string_view n(name, size_t(len));
        if (n == "dev_Flip") {
            p->flip = glGetUniformLocation(p->name, name);
            continue;
        }
        constexpr std::string_view prefix = "SPIRV_Cross_Combined";
        if (!n.starts_with(prefix)) continue;
        n.remove_prefix(prefix.size());
        // The texture's name, then the sampler's (or SPIRV-Cross's stand-in for a sampler-less read).
        Unit u;
        size_t best = 0;
        for (const auto& [tex, slot] : b.t) {
            if (!n.starts_with(tex) || tex.size() <= best) continue;
            const std::string_view rest = n.substr(tex.size());
            int samp = -2;
            if (rest == "SPIRV_Cross_DummySampler") samp = -1;
            for (const auto& [s, sslot] : b.s)
                if (rest == s) samp = sslot;
            if (samp == -2) continue;
            u.texture = slot;
            u.sampler = samp;
            best = tex.size();
        }
        if (u.texture < 0) {
            LOG_WARN("Program %s: sampler %s matches no register", source.name, name);
            continue;
        }
        glUniform1i(glGetUniformLocation(p->name, name), GLint(p->units.size()));
        p->units.push_back(u);
    }
    return p;
}

int DeviceGLES::samples_for(Format, int want) {
    if (!late_.tex_storage_ms) return 1;
    int n = std::max(1, want);
    while (n > max_samples_) n /= 2;
    return std::max(1, n);
}

const BlendState* DeviceGLES::blend(const BlendDesc& d) {
    for (auto& [k, v] : blends_)
        if (k == d) return v.get();
    auto s = std::make_unique<BlendGL>();
    s->d = d;
    blends_.emplace_back(d, std::move(s));
    return blends_.back().second.get();
}

const RasterState* DeviceGLES::raster(const RasterDesc& d) {
    for (auto& [k, v] : rasters_)
        if (k == d) return v.get();
    auto s = std::make_unique<RasterGL>();
    s->d = d;
    rasters_.emplace_back(d, std::move(s));
    return rasters_.back().second.get();
}

const DepthState* DeviceGLES::depth(const DepthDesc& d) {
    for (auto& [k, v] : depths_)
        if (k == d) return v.get();
    auto s = std::make_unique<DepthGL>();
    s->d = d;
    depths_.emplace_back(d, std::move(s));
    return depths_.back().second.get();
}

const Sampler* DeviceGLES::sampler(const SamplerDesc& d) {
    for (auto& [k, v] : samplers_list_)
        if (k == d) return v.get();
    auto s = std::make_unique<SamplerGL>();
    s->d = d;
    GLuint names[2];
    glGenSamplers(2, names);
    s->name = names[0];
    s->nearest = names[1];
    GLint wrap = d.address == Address::Wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    if (d.address == Address::Border && border_) wrap = 0x812D;   // CLAMP_TO_BORDER
    for (int k = 0; k < 2; ++k) {
        const GLuint n = names[k];
        const bool point = k == 1 || d.filter == Filter::Point;
        glSamplerParameteri(n, GL_TEXTURE_MIN_FILTER, point ? GL_NEAREST_MIPMAP_NEAREST : d.compare ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(n, GL_TEXTURE_MAG_FILTER, point ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(n, GL_TEXTURE_WRAP_S, wrap);
        glSamplerParameteri(n, GL_TEXTURE_WRAP_T, wrap);
        glSamplerParameteri(n, GL_TEXTURE_WRAP_R, wrap);
        if (wrap == 0x812D) {
            const float white[4] = {1, 1, 1, 1};
            glSamplerParameterfv(n, 0x1004, white);   // TEXTURE_BORDER_COLOR
        }
        if (d.compare) {
            glSamplerParameteri(n, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(n, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        if (aniso_ && d.filter == Filter::Anisotropic && k == 0) glSamplerParameterf(n, 0x84FE, float(std::clamp<int>(d.anisotropy, 1, 16)));
    }
    samplers_list_.emplace_back(d, std::move(s));
    return samplers_list_.back().second.get();
}

void DeviceGLES::attach(GLenum point, const Target& t) {
    auto* tex = static_cast<const TextureGL*>(t.texture);
    if (!tex || tex->window) return;
    if (tex->desc().type == TextureType::Cube)
        glFramebufferTexture2D(GL_FRAMEBUFFER, point, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + t.layer), tex->name, 0);
    else if (tex->desc().type == TextureType::Array)
        glFramebufferTextureLayer(GL_FRAMEBUFFER, point, tex->name, 0, t.layer);
    else
        glFramebufferTexture2D(GL_FRAMEBUFFER, point, tex->target(), tex->name, 0);
}

GLuint DeviceGLES::framebuffer_for(std::span<const Target> colour, Target depth) {
    FboKey key;
    for (size_t i = 0; i < colour.size() && i < 4; ++i)
        if (auto* t = static_cast<const TextureGL*>(colour[i].texture)) {
            key.c[i][0] = t->id;
            key.c[i][1] = u32(colour[i].layer);
        }
    if (auto* t = static_cast<const TextureGL*>(depth.texture); t && !t->window) {
        key.d[0] = t->id;
        key.d[1] = u32(depth.layer);
    }
    if (auto it = fbos_.find(key); it != fbos_.end()) return it->second;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    bound_fbo_ = fbo;
    for (size_t i = 0; i < colour.size() && i < 4; ++i) attach(GLenum(GL_COLOR_ATTACHMENT0 + i), colour[i]);
    if (auto* t = static_cast<const TextureGL*>(depth.texture); t && !t->window)
        attach(t->desc().format == Format::D24S8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, depth);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) LOG_WARN("OpenGL ES: framebuffer incomplete (0x%x)", unsigned(status));
    fbos_[key] = fbo;
    return fbo;
}

void DeviceGLES::forget_texture(u32 id) {
    for (auto it = fbos_.begin(); it != fbos_.end();) {
        const FboKey& k = it->first;
        bool hit = k.d[0] == id;
        for (const auto& c : k.c) hit |= c[0] == id;
        if (hit) {
            if (bound_fbo_ == it->second) bound_fbo_ = ~0u;
            glDeleteFramebuffers(1, &it->second);
            it = fbos_.erase(it);
        } else {
            ++it;
        }
    }
}

void DeviceGLES::set_targets(std::span<const Target> colour, Target depth) {
    targets_ = {};
    const int n = std::min<int>(int(colour.size()), 4);
    for (int i = 0; i < n; ++i) targets_.colour[i] = colour[size_t(i)];
    targets_.count = n;
    targets_.depth = depth;
    target_mask_ = 0;
    target_count_ = n;
    for (int i = 0; i < n; ++i)
        if (colour[size_t(i)].texture) target_mask_ |= u8(1u << i);
    const auto* first = n > 0 ? static_cast<const TextureGL*>(colour[0].texture) : nullptr;
    if (first && first->window) {
        bind_framebuffer(0);
        flipped_ = false;
        set_viewport(viewport_);   // OpenGL's viewport counts from the bottom of what is drawn into
        return;
    }
    if (n == 0 && !depth.texture) {
        // Nothing to draw into: the pipeline lets go of the last targets.
        flipped_ = true;
        return;
    }
    const GLuint fbo = framebuffer_for(std::span<const Target>(colour.data(), size_t(n)), depth);
    bind_framebuffer(fbo);
    flipped_ = true;
    set_viewport(viewport_);
    gl_.draw_mask = 0xFF;   // the draw buffers are the framebuffer's own until a draw sets them
}

void DeviceGLES::set_viewport(const Viewport& v) {
    viewport_ = v;
    if (flipped_) {
        glViewport(GLint(v.x), GLint(v.y), GLsizei(v.w), GLsizei(v.h));
    } else {
        glViewport(GLint(v.x), GLint(float(height_) - v.y - v.h), GLsizei(v.w), GLsizei(v.h));
    }
}

void DeviceGLES::clear(Target target, const Color& c) {
    auto* t = static_cast<const TextureGL*>(target.texture);
    if (!t) return;
    const float f[4] = {c.r, c.g, c.b, c.a};
    // Clears ignore the blend state's masks (as in Direct3D).
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl_.valid = false;
    gl_.draw_mask = 0xFF;
    if (t->window) {
        if (bound_fbo_ != 0) {
            const GLuint prev = bound_fbo_;
            bind_framebuffer(0);
            glClearBufferfv(GL_COLOR, 0, f);
            bind_framebuffer(prev);
        } else {
            glClearBufferfv(GL_COLOR, 0, f);
        }
        return;
    }
    // A target of the framebuffer bound: cleared in place.
    for (int i = 0; i < targets_.count; ++i)
        if (targets_.colour[i] == target && bound_fbo_ != 0 && bound_fbo_ != ~0u) {
            const GLenum bufs[4] = {GLenum(i == 0 ? GL_COLOR_ATTACHMENT0 : GL_NONE), GLenum(i == 1 ? GL_COLOR_ATTACHMENT1 : GL_NONE),
                                    GLenum(i == 2 ? GL_COLOR_ATTACHMENT2 : GL_NONE), GLenum(i == 3 ? GL_COLOR_ATTACHMENT3 : GL_NONE)};
            glDrawBuffers(i + 1, bufs);
            glClearBufferfv(GL_COLOR, i, f);
            return;
        }
    const GLuint prev = bound_fbo_;
    glBindFramebuffer(GL_FRAMEBUFFER, scratch_fbo_);
    attach(GL_COLOR_ATTACHMENT0, target);
    const GLenum buf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &buf);
    glClearBufferfv(GL_COLOR, 0, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, prev == ~0u ? 0 : prev);
    bound_fbo_ = prev;
}

void DeviceGLES::clear_depth(Target target, float value) {
    auto* t = static_cast<const TextureGL*>(target.texture);
    if (!t || t->window) return;
    glDepthMask(GL_TRUE);
    gl_.valid = false;
    if (targets_.depth == target && bound_fbo_ != 0 && bound_fbo_ != ~0u) {
        if (t->desc().format == Format::D24S8) glClearBufferfi(GL_DEPTH_STENCIL, 0, value, 0);
        else glClearBufferfv(GL_DEPTH, 0, &value);
        return;
    }
    const GLuint prev = bound_fbo_;
    glBindFramebuffer(GL_FRAMEBUFFER, scratch_fbo_);
    const GLenum point = t->desc().format == Format::D24S8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    attach(point, target);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    if (t->desc().format == Format::D24S8) glClearBufferfi(GL_DEPTH_STENCIL, 0, value, 0);
    else glClearBufferfv(GL_DEPTH, 0, &value);
    glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, prev == ~0u ? 0 : prev);
    bound_fbo_ = prev;
}

void DeviceGLES::set_uniforms(int slot, const Buffer* buffer) {
    if (slot < 0 || slot >= kSlots) return;
    uniforms_[slot] = static_cast<const BufferGL*>(buffer);
}

void DeviceGLES::apply_state() {
    // The program, and which way up it draws.
    bool program_changed = false;
    if (program_ != gl_.program) {
        glUseProgram(program_ ? program_->name : 0);
        gl_.program = program_;
        gl_.vb = nullptr;
        program_changed = true;
    }
    if (program_ && program_->flip >= 0) {
        const float f[3] = {flipped_ ? -1.0f : 1.0f, flipped_ ? 0.0f : float(height_), flipped_ ? 1.0f : -1.0f};
        auto* p = const_cast<ProgramGL*>(program_);
        // Sent again whenever the program is bound: ANGLE on a desktop OpenGL driver lost the value
        // of a program that went a while unused, and the movies drew as a line across the screen.
        if (program_changed || std::memcmp(p->flip_value, f, sizeof(f)) != 0) {
            glUniform4f(p->flip, f[0], f[1], f[2], 0);
            std::memcpy(p->flip_value, f, sizeof(f));
        }
    }
    // Blending, and which of the targets take the draw.
    const BlendDesc bd = blend_ ? blend_->d : BlendDesc{};
    if (blend_ != gl_.blend || !gl_.valid) {
        if (bd.enable) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(blend_factor(bd.src), blend_factor(bd.dst), blend_factor(bd.src_alpha), blend_factor(bd.dst_alpha));
            glBlendEquationSeparate(bd.op == BlendOp::RevSubtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD,
                                    bd.op_alpha == BlendOp::RevSubtract ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (bd.alpha_to_coverage != gl_.a2c || !gl_.valid) {
            if (bd.alpha_to_coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            gl_.a2c = bd.alpha_to_coverage;
        }
        gl_.blend = blend_;
    }
    if (bound_fbo_ != 0) {
        const u8 mask = u8(bd.write & target_mask_);
        if (mask != gl_.draw_mask) {
            GLenum bufs[4];
            int count = 0;
            for (int i = 0; i < std::max(1, target_count_); ++i) bufs[count++] = (mask >> i) & 1 ? GLenum(GL_COLOR_ATTACHMENT0 + i) : GLenum(GL_NONE);
            glDrawBuffers(count, bufs);
            gl_.draw_mask = mask;
        }
    }
    // Culling (the faces turn with the picture) and the shadow casters' slope bias.
    if (raster_ != gl_.raster || flipped_ != gl_.flipped || !gl_.valid) {
        const RasterDesc rd = raster_ ? raster_->d : RasterDesc{};
        if (rd.cull == Cull::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(rd.cull == Cull::Back ? GL_BACK : GL_FRONT);
        }
        glFrontFace(flipped_ ? GL_CCW : GL_CW);
        if (rd.slope_bias != 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(rd.slope_bias, 0);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        gl_.raster = raster_;
        gl_.flipped = flipped_;
    }
    if (depth_state_ != gl_.depth || !gl_.valid) {
        const DepthDesc dd = depth_state_ ? depth_state_->d : DepthDesc{};
        if (dd.test || dd.write) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(dd.test ? GL_LEQUAL : GL_ALWAYS);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(dd.write ? GL_TRUE : GL_FALSE);
        gl_.depth = depth_state_;
    }
    gl_.valid = true;
    // Uniform blocks: a streamed one by its range in the ring, the rest whole.
    for (int s = 0; s < kSlots; ++s) {
        auto* u = const_cast<BufferGL*>(uniforms_[s]);
        if (u && !u->shadow.empty()) {
            const size_t offset = ring_place(*u), size = u->shadow.size();
            if (gl_.ubo[s] != ring_.name || gl_.ubo_offset[s] != offset || gl_.ubo_size[s] != size) {
                glBindBufferRange(GL_UNIFORM_BUFFER, GLuint(s), ring_.name, GLintptr(offset), GLsizeiptr(size));
                gl_.ubo[s] = ring_.name;
                gl_.ubo_offset[s] = offset;
                gl_.ubo_size[s] = size;
            }
            continue;
        }
        const GLuint name = u ? u->name : 0;
        if (name != gl_.ubo[s] || gl_.ubo_size[s] != 0) {
            glBindBufferBase(GL_UNIFORM_BUFFER, GLuint(s), name);
            gl_.ubo[s] = name;
            gl_.ubo_size[s] = 0;
        }
    }
    // Textures and samplers, unit by unit as the program samples them.
    if (program_) {
        for (size_t u = 0; u < program_->units.size() && u < 32; ++u) {
            const Unit& unit = program_->units[u];
            const TextureGL* t = textures_[unit.texture];
            // One being drawn into reads as nothing (Direct3D unbinds it; OpenGL would loop).
            if (t && (t == targets_.depth.texture || t == targets_.colour[0].texture || t == targets_.colour[1].texture ||
                      t == targets_.colour[2].texture || t == targets_.colour[3].texture))
                t = nullptr;
            const GLuint tname = t && !t->window ? t->name : 0;
            const GLenum target = t ? t->target() : GL_TEXTURE_2D;
            const SamplerGL* s = unit.sampler >= 0 ? samplers_[unit.sampler] : nullptr;
            GLuint sname = s ? ((t && !t->filterable && !s->d.compare) ? s->nearest : s->name) : 0;
            if (tname != gl_.unit_tex[u] || target != gl_.unit_target[u]) {
                glActiveTexture(GLenum(GL_TEXTURE0 + u));
                if (gl_.unit_target[u] && gl_.unit_target[u] != target) glBindTexture(gl_.unit_target[u], 0);
                glBindTexture(target, tname);
                gl_.unit_tex[u] = tname;
                gl_.unit_target[u] = target;
            }
            if (sname != gl_.unit_samp[u]) {
                glBindSampler(GLuint(u), sname);
                gl_.unit_samp[u] = sname;
            }
        }
    }
    // The vertices, as the program reads them.
    if (vb_ != gl_.vb || stride_ != gl_.stride || program_ != gl_.vb_program) {
        u32 want = 0;
        if (vb_ && program_)
            for (const Attr& a : program_->attrs) want |= 1u << a.location;
        for (GLuint i = 0; i < 16; ++i)
            if (((gl_.attrs ^ want) >> i) & 1) {
                if ((want >> i) & 1) glEnableVertexAttribArray(i);
                else glDisableVertexAttribArray(i);
            }
        gl_.attrs = want;
        if (vb_ && program_) {
            glBindBuffer(GL_ARRAY_BUFFER, vb_->name);
            for (const Attr& a : program_->attrs) {
                const auto loc = GLuint(a.location);
                const void* off = reinterpret_cast<const void*>(uintptr_t(a.offset));
                switch (a.format) {
                    case AttrFormat::Float2: glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, GLsizei(stride_), off); break;
                    case AttrFormat::Float3: glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, GLsizei(stride_), off); break;
                    case AttrFormat::Float4: glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, GLsizei(stride_), off); break;
                    case AttrFormat::UNorm8x4: glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, GLsizei(stride_), off); break;
                    case AttrFormat::UInt8x4: glVertexAttribIPointer(loc, 4, GL_UNSIGNED_BYTE, GLsizei(stride_), off); break;
                }
            }
        }
        gl_.vb = vb_;
        gl_.stride = stride_;
        gl_.vb_program = program_;
    }
}

void DeviceGLES::draw(Topology t, u32 count, u32 first) {
    if (bound_fbo_ == ~0u || !program_) return;
    apply_state();
    glDrawArrays(primitive(t), GLint(first), GLsizei(count));
}

void DeviceGLES::draw_indexed(Topology t, u32 count, u32 first) {
    if (bound_fbo_ == ~0u || !program_ || !ib_) return;
    apply_state();
    if (ib_->name != gl_.ib) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib_->name);
        gl_.ib = ib_->name;
    }
    glDrawElements(primitive(t), GLsizei(count), GL_UNSIGNED_INT, reinterpret_cast<const void*>(uintptr_t(first) * 4));
}

void DeviceGLES::resolve(Texture& dst, const Texture& src) {
    const Target s{const_cast<Texture*>(&src)}, d{&dst};
    const GLuint read = framebuffer_for(std::span<const Target>(&s, 1), {});
    const GLuint draw = framebuffer_for(std::span<const Target>(&d, 1), {});
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    const GLenum buf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &buf);
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bound_fbo_ = ~0u;
    gl_.draw_mask = 0xFF;
}

}  // namespace

std::unique_ptr<Device> make_gles() { return std::make_unique<DeviceGLES>(); }

#ifndef _WIN32
std::unique_ptr<Device> Device::make(Api) { return make_gles(); }
#endif

}  // namespace eng
