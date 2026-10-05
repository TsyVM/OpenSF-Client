// The graphics device: what every renderer draws with, the same on each platform. Direct3D 12
// on Windows (D3D12/Device12.cpp; Direct3D 11 where a card or driver has no 12, D3D11/Device11.cpp);
// OpenGL ES 3 on Android.
//
// Resources (textures, buffers, programs) are shared by reference: the last owner lets go of one
// on the card. Fixed-function states (blend, raster, depth, samplers) are made from their
// descriptions and kept by the device; the same description gives back the same object, so a
// renderer can tell what the pipeline holds by comparing pointers.
//
// The conventions are Direct3D's: clip-space depth 0..1, texture and target rows top first,
// clockwise front faces, a shader's resources in numbered slots. A backend that differs makes up
// the difference itself.
#pragma once

#include "Engine/Asset/ImageDecode.hpp"
#include "Engine/Core/Math.hpp"

#include <memory>
#include <span>
#include <string>

namespace eng {

// ── Resources ──────────────────────────────────────────────────────────────────

enum class Format : u8 { RGBA8, R8, R16F, RG16F, RGBA16F, R11G11B10F, R32F, BC1, BC2, BC3, D24S8, D32F };
inline bool is_depth(Format f) { return f == Format::D24S8 || f == Format::D32F; }
inline bool is_block_compressed(Format f) { return f == Format::BC1 || f == Format::BC2 || f == Format::BC3; }

enum class TextureType : u8 { Plain, Array, Cube };

struct TextureDesc {
    TextureType type = TextureType::Plain;
    Format format = Format::RGBA8;
    int width = 1, height = 1;
    int layers = 1;          // an array's slices (a cube has six, in Direct3D's face order)
    int mips = 1;
    int samples = 1;         // above 1: multisampled
    bool target = false;     // drawn into: a colour target, or a depth one by its format
    bool dynamic = false;    // rewritten by the CPU as it goes (update_texture)
};

// One mip level of one layer: layer by layer, each layer's mips from the largest.
struct TextureData {
    const void* data = nullptr;
    u32 row_pitch = 0;       // bytes from one row (of blocks, when compressed) to the next
};

class Texture {
public:
    virtual ~Texture() = default;
    const TextureDesc& desc() const { return desc_; }
    int width() const { return desc_.width; }
    int height() const { return desc_.height; }
    size_t bytes() const { return bytes_; }   // what it takes on the card
    // How VanGUI draws it (an image in the menus and the HUD).
    virtual u64 ui_id() const = 0;

protected:
    TextureDesc desc_;
    size_t bytes_ = 0;
};
using TextureRef = std::shared_ptr<Texture>;

enum class BufferKind : u8 { Vertex, Index, Uniform };

class Buffer {
public:
    virtual ~Buffer() = default;
    BufferKind kind() const { return kind_; }
    size_t bytes() const { return bytes_; }

protected:
    BufferKind kind_ = BufferKind::Vertex;
    size_t bytes_ = 0;
};
using BufferRef = std::shared_ptr<Buffer>;

// A vertex's parts as a program reads them.
enum class Semantic : u8 { Position, Normal, TexCoord, Colour, BlendIndices, BlendWeight };
enum class AttrFormat : u8 { Float2, Float3, Float4, UNorm8x4, UInt8x4 };
struct VertexAttr {
    Semantic semantic;
    AttrFormat format;
    u32 offset;
    u8 index = 0;   // the semantic's number: TEXCOORD1 for a lightmap's coordinates
};

// One entry of a shader as GLSL ES, made from its HLSL at build time (Tools/shaders_gen.py).
struct GlslEntry {
    const char* entry = "";
    const char* glsl = "";
    int version = 300;        // the GLSL ES version it needs (310: multisampled textures)
};

// One entry of a shader as Shader Model 6 bytecode (DXIL, signed), compiled from its HLSL by DXC
// at build time (Tools/shaders_gen.py): what Direct3D 12 runs.
struct DxilEntry {
    const char* entry = "";
    const unsigned char* code = nullptr;
    size_t size = 0;
};

// A shader's source: HLSL, its vertex and pixel entries named when a program is made from it.
// Direct3D 12 takes each entry's DXIL made at build time; OpenGL ES the GLSL made from each entry
// at build time, with the HLSL's registers ("t:Tex=0,...;s:Samp=0,...;b:type_Frame=0,...") to
// bind it by.
struct ShaderSource {
    const char* name = "";
    const char* hlsl = "";
    const GlslEntry* glsl = nullptr;
    size_t glsl_count = 0;
    const char* bindings = "";
    const DxilEntry* dxil = nullptr;
    size_t dxil_count = 0;
};

class Program {
public:
    virtual ~Program() = default;
};
using ProgramRef = std::shared_ptr<Program>;

// ── Fixed-function state ───────────────────────────────────────────────────────

enum class BlendFactor : u8 { Zero, One, SrcColour, SrcAlpha, InvSrcAlpha, DestColour };
enum class BlendOp : u8 { Add, RevSubtract };

struct BlendDesc {
    bool enable = false;
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    BlendOp op = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One, dst_alpha = BlendFactor::Zero;
    BlendOp op_alpha = BlendOp::Add;
    // Which targets take the draw (bit per target). The blend applies to the first; the others
    // are written as they are or not at all (a blended draw leaves a scene's side targets alone).
    u8 write = 0xF;
    bool alpha_to_coverage = false;
    bool operator==(const BlendDesc&) const = default;
};

enum class Cull : u8 { None, Back, Front };
struct RasterDesc {
    Cull cull = Cull::Back;
    bool wireframe = false;                    // debug views (not every backend has it)
    float slope_bias = 0, bias_clamp = 0;      // depth pushed back by the triangle's slope (shadow casters)
    bool operator==(const RasterDesc&) const = default;
};

// Depth compares less-or-equal.
struct DepthDesc {
    bool test = true, write = true;
    bool operator==(const DepthDesc&) const = default;
};

enum class Filter : u8 { Point, Linear, Anisotropic };
enum class Address : u8 { Wrap, Clamp, Border };   // Border: white outside (a shadow map's edge is lit)
struct SamplerDesc {
    Filter filter = Filter::Linear;
    Address address = Address::Wrap;
    u8 anisotropy = 1;
    bool compare = false;    // a depth comparison (less-or-equal), filtered 2x2
    bool operator==(const SamplerDesc&) const = default;
};

// Made by the device from a description (above); each backend its own.
class BlendState {
public:
    virtual ~BlendState() = default;
};
class RasterState {
public:
    virtual ~RasterState() = default;
};
class DepthState {
public:
    virtual ~DepthState() = default;
};
class Sampler {
public:
    virtual ~Sampler() = default;
};

enum class Topology : u8 { Triangles, TriangleStrip, Lines };

// Something drawn into: a colour or depth texture (a slice of an array, a face of a cube).
struct Target {
    Texture* texture = nullptr;
    int layer = 0;
    bool operator==(const Target&) const = default;
};

// A frame's GPU timestamps, read back some frames later without waiting.
class TimerFrame {
public:
    virtual ~TimerFrame() = default;
    virtual void begin() = 0;
    virtual void stamp(int index) = 0;
    virtual void end() = 0;
    // Whether the card is done with it. Once it is, `ms` takes each stamp's time after the
    // first, and `valid` says whether to trust them (not when the card's clock changed meanwhile).
    virtual bool poll(double* ms, int count, bool& valid) = 0;
};

// ── The device ─────────────────────────────────────────────────────────────────

enum class Api : u8 { Default, D3D11, D3D12, GLES };
// What the player reads: "DirectX 12", "DirectX 11", "OpenGL" (Windows) or "OpenGL ES" (a phone).
const char* api_name(Api api);

class Device {
public:
    // The platform's own (Default): Direct3D 12 on Windows (11 on a card or driver without it),
    // OpenGL ES 3 on Android. Windows can also run Direct3D 11, or the OpenGL ES renderer on
    // the card's OpenGL driver through ANGLE (the player's choice in Options, or --d3d11/--gles).
    static std::unique_ptr<Device> make(Api api = Api::Default);
    virtual ~Device() = default;
    virtual Api api() const = 0;

    struct Options {
        bool vsync = true;
        bool debug = false;   // the API's own checks into the log (--d3ddebug)
        bool warp = false;    // a software renderer in place of the card (--warp: a check against drivers)
        // OpenGL on Windows runs through ANGLE: which of its back ends draws. "opengl" is the
        // card's own OpenGL driver; "d3d11" and "vulkan" are there for checks (--angle).
        const char* angle_backend = "opengl";
        // The display the window is on, for EGL: Linux's X11 Display (null: EGL's default).
        void* native_display = nullptr;
    };
    virtual bool create(void* native_window, int width, int height, const Options& options) = 0;
    virtual void destroy() = 0;
    virtual bool resize(int width, int height) = 0;
    // Android takes the window away while the app is in the background and gives a new one on
    // return; everything made on the device survives in between. Nothing draws while detached.
    virtual bool attach_window(void* native_window) { return native_window != nullptr; }
    virtual void detach_window() {}

    // The window's targets bound and cleared, the viewport over all of it.
    virtual void begin_frame(const Color& clear) = 0;
    virtual void present() = 0;
    void set_vsync(bool on) { vsync_ = on; }
    // The driver reset the card: the game can only save and stop.
    bool lost() const { return lost_; }
    unsigned vendor() const { return vendor_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Reads the window's picture (screenshots and automated checks).
    virtual bool capture(Image& out) = 0;
    // The window's colour and depth, as targets.
    virtual Texture* back_buffer() = 0;
    virtual Texture* back_depth() = 0;

    // Resources.
    virtual TextureRef create_texture(const TextureDesc& desc, std::span<const TextureData> init = {}) = 0;
    // A dynamic texture's picture (its only level), rows `row_pitch` bytes apart.
    virtual void update_texture(Texture& texture, const void* data, u32 row_pitch) = 0;
    virtual BufferRef create_buffer(BufferKind kind, const void* data, size_t bytes, bool dynamic = false) = 0;
    virtual void update_buffer(Buffer& buffer, const void* data, size_t bytes) = 0;
    // A program from a shader's vertex and pixel entries; `layout` empty for one that reads no
    // vertices (it makes its own from the vertex number).
    virtual ProgramRef create_program(const ShaderSource& source, const char* vs_entry, const char* ps_entry,
                                      std::span<const VertexAttr> layout, std::string* error = nullptr) = 0;
    // The most samples `format` takes as a target, at most `want`.
    virtual int samples_for(Format format, int want) = 0;

    // Fixed-function state, made once per description and kept.
    virtual const BlendState* blend(const BlendDesc& desc) = 0;
    virtual const RasterState* raster(const RasterDesc& desc) = 0;
    virtual const DepthState* depth(const DepthDesc& desc) = 0;
    virtual const Sampler* sampler(const SamplerDesc& desc) = 0;

    // Drawing. Up to four colour targets (an empty one is skipped) and a depth one.
    virtual void set_targets(std::span<const Target> colour, Target depth = {}) = 0;
    struct Targets {
        Target colour[4];
        int count = 0;
        Target depth;
    };
    const Targets& targets() const { return targets_; }
    void set_targets(const Targets& t) { set_targets(std::span<const Target>(t.colour, size_t(t.count)), t.depth); }
    // Nothing bound to draw into (before a resolve, or once a pass's target is to be read).
    void unbind_targets() { set_targets(std::span<const Target>(), Target{}); }
    struct Viewport {
        float x = 0, y = 0, w = 0, h = 0;
    };
    virtual void set_viewport(const Viewport& v) = 0;
    const Viewport& viewport() const { return viewport_; }
    virtual void clear(Target target, const Color& colour) = 0;
    virtual void clear_depth(Target target, float depth = 1) = 0;
    virtual void set_program(const Program* program) = 0;
    virtual void set_blend(const BlendState* state) = 0;
    virtual void set_raster(const RasterState* state) = 0;
    virtual void set_depth(const DepthState* state) = 0;
    // Uniforms (a constant buffer) for both stages; textures and samplers for the pixel stage.
    virtual void set_uniforms(int slot, const Buffer* buffer) = 0;
    virtual void set_texture(int slot, const Texture* texture) = 0;
    virtual void set_sampler(int slot, const Sampler* sampler) = 0;
    virtual void set_vertices(const Buffer* buffer, u32 stride) = 0;
    virtual void set_indices(const Buffer* buffer) = 0;   // 32-bit indices
    virtual void draw(Topology topology, u32 count, u32 first = 0) = 0;
    virtual void draw_indexed(Topology topology, u32 count, u32 first = 0) = 0;
    // A multisampled target's samples into a plain one of its size and format.
    virtual void resolve(Texture& dst, const Texture& src) = 0;
    // A texture just drawn into (or resolved), made ready for the interface to draw as an image.
    // A draw readies what it reads by itself; VanGUI's pass does not, so Direct3D 12 needs telling.
    virtual void shader_read(Texture& texture) { (void)texture; }

    // VanGUI's renderer, drawing into whatever is bound.
    virtual bool ui_init() = 0;
    virtual void ui_shutdown() = 0;
    virtual void ui_new_frame() = 0;
    virtual void ui_render() = 0;

    // Profiling: GPU timestamps (null where there are none) and the card's memory.
    virtual std::unique_ptr<TimerFrame> create_timer_frame(int stamps) = 0;
    virtual bool video_memory(size_t& used, size_t& budget) const = 0;
    const std::string& adapter_name() const { return adapter_name_; }
    size_t adapter_memory() const { return adapter_memory_; }

    // Common states, for passes that want nothing special.
    const BlendState* blend_opaque() { return blend(BlendDesc{}); }
    const RasterState* raster_cull_none() { return raster(RasterDesc{Cull::None}); }
    const DepthState* depth_none() { return depth(DepthDesc{false, false}); }
    const Sampler* sampler_clamp() { return sampler(SamplerDesc{Filter::Linear, Address::Clamp}); }

protected:
    int width_ = 0, height_ = 0;
    bool vsync_ = true;
    bool lost_ = false;
    unsigned vendor_ = 0;
    std::string adapter_name_;
    size_t adapter_memory_ = 0;
    Targets targets_;
    Viewport viewport_;
};

// Bytes a texture of this description takes (all mips, layers and samples).
size_t texture_bytes(const TextureDesc& desc);
// Bytes of one texel of an uncompressed format.
u32 texel_bytes(Format format);

}  // namespace eng
