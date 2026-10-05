// The graphics device on Direct3D 12 (Windows).
//
// The device's calls (Device.hpp) are Direct3D 11's in spirit: set a state, bind, draw. Here they
// are recorded into one command list, submitted at present() with two frames in flight:
//
//   pipelines     one root signature for every program (four constant buffers b0-b3, a table of
//                 16 textures t0-t15, a table of 8 samplers s0-s7); a pipeline state object for
//                 each program, state and target combination drawn with, made on first use and kept
//   descriptors   each texture's views in CPU heaps; a draw's textures copied into the
//                 shader-visible heap as a table, the same set reused while it stays bound; sampler
//                 tables made once per combination; VanGUI's images in a fixed part of the heap
//   memory        static buffers and textures in the card's memory, filled through upload
//                 buffers; a per-frame upload ring for what the CPU rewrites (dynamic buffers keep a
//                 CPU copy, so one not rewritten this frame is copied into this frame's ring again)
//   states        every resource's state tracked, transitions batched before each draw
//   lifetimes     a resource let go of stays alive until the card is done with the frames that
//                 used it; the device's shared core outlives the device while resources remain
//
// DRED is on from the start: when the driver resets the card, the log says which GPU operation it
// died in and what address faulted, which is what a player's report needs.
#include "Engine/Render/Device.hpp"

#include "Engine/Core/Log.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vangui/vangui.h>
#include <vangui_impl_dx12.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace eng {

std::unique_ptr<Device> make_d3d12();
bool d3d12_available();

namespace {

using Microsoft::WRL::ComPtr;

constexpr u32 kFrames = 2;          // frames the CPU may record ahead of the card
constexpr u32 kBackBuffers = 3;
constexpr u32 kCbvSlots = 4;        // b0-b3
constexpr u32 kSrvSlots = 16;       // t0-t15
constexpr u32 kSamplerSlots = 8;    // s0-s7
constexpr u32 kUiSlots = 4096;      // shader-visible descriptors kept for VanGUI's images
constexpr u32 kTableDescriptors = 120000;   // per frame, for draws' texture tables
constexpr u32 kSamplerHeap = 2048;
constexpr u64 kRingBytes = 64ull << 20;     // per frame
constexpr u64 kUploadFlushBytes = 256ull << 20;   // pending uploads before a load waits for the card

const char* vendor_name(UINT id) {
    switch (id) {
        case 0x1002: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x5143: return "Qualcomm";
        default: return "unknown vendor";
    }
}

struct Formats {
    DXGI_FORMAT resource, srv, target;
};
Formats dxgi(Format f) {
    switch (f) {
        case Format::RGBA8: return {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
        case Format::R8: return {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM};
        case Format::R16F: return {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_FLOAT};
        case Format::RG16F: return {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT};
        case Format::RGBA16F: return {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
        case Format::R11G11B10F: return {DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT};
        case Format::R32F: return {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT};
        case Format::BC1: return {DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_UNKNOWN};
        case Format::BC2: return {DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_UNKNOWN};
        case Format::BC3: return {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_UNKNOWN};
        case Format::D24S8: return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT};
        case Format::D32F: return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT};
    }
    return {};
}

D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type;
    return h;
}

D3D12_RESOURCE_DESC buffer_desc(u64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = std::max<u64>(bytes, 1);
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

u64 align(u64 v, u64 a) { return (v + a - 1) & ~(a - 1); }

// A CPU-only descriptor heap handing out single descriptors (texture views, targets).
class DescriptorPool {
public:
    bool init(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 count) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = type;
        hd.NumDescriptors = count;
        if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)))) return false;
        step_ = d->GetDescriptorHandleIncrementSize(type);
        start_ = heap_->GetCPUDescriptorHandleForHeapStart();
        capacity_ = count;
        return true;
    }
    u32 alloc() {
        std::lock_guard lock(m_);
        if (!free_.empty()) {
            const u32 i = free_.back();
            free_.pop_back();
            return i;
        }
        if (next_ >= capacity_) {
            LOG_ERROR("D3D12: descriptor heap full (%u)", capacity_);
            return 0;
        }
        return next_++;
    }
    void free(u32 i) {
        std::lock_guard lock(m_);
        free_.push_back(i);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(u32 i) const { return {start_.ptr + SIZE_T(i) * step_}; }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE start_{};
    u32 step_ = 0, next_ = 0, capacity_ = 0;
    std::vector<u32> free_;
    std::mutex m_;
};

// What resources share with the device and may outlive it: the card, the queue's progress,
// the heaps their views live in, and the list of things let go of but still in the card's use.
struct Core {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Fence> fence;
    u64 recording = 1;   // the fence value the commands now being recorded will signal
    bool alive = true;
    DescriptorPool srv, rtv, dsv;
    // The shader-visible heaps: VanGUI's fixed slots first, then each frame's table ring.
    ComPtr<ID3D12DescriptorHeap> views, samplers;
    u32 view_step = 0, sampler_step = 0;
    std::vector<u32> ui_free;
    u32 ui_next = 0;
    std::mutex m;
    struct Dead {
        u64 fence;
        ComPtr<IUnknown> resource;
        u32 ui_slot;
    };
    std::vector<Dead> dead;

    D3D12_CPU_DESCRIPTOR_HANDLE view_cpu(u32 i) const { return {views->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(i) * view_step}; }
    D3D12_GPU_DESCRIPTOR_HANDLE view_gpu(u32 i) const { return {views->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(i) * view_step}; }
    u32 ui_alloc() {
        std::lock_guard lock(m);
        if (!ui_free.empty()) {
            const u32 i = ui_free.back();
            ui_free.pop_back();
            return i;
        }
        if (ui_next >= kUiSlots) {
            LOG_ERROR("D3D12: no descriptor left for another UI image");
            return 0;
        }
        return ui_next++;
    }
    // Let go of once the card is done with what is being recorded now.
    void bury(ComPtr<IUnknown> r, u32 ui_slot = ~0u) {
        std::lock_guard lock(m);
        if (!alive) {
            if (ui_slot != ~0u) ui_free.push_back(ui_slot);
            return;
        }
        dead.push_back({recording, std::move(r), ui_slot});
    }
    void collect(u64 completed) {
        std::lock_guard lock(m);
        size_t kept = 0;
        for (auto& d : dead) {
            if (d.fence <= completed) {
                if (d.ui_slot != ~0u) ui_free.push_back(d.ui_slot);
                d.resource.Reset();
            } else {
                dead[kept++] = std::move(d);
            }
        }
        dead.resize(kept);
    }
};

std::atomic<u32> g_serial{1};

class Texture12 final : public Texture {
public:
    Texture12(std::shared_ptr<Core> c, const TextureDesc& d) : core(std::move(c)) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    ~Texture12() override { release(); }
    void release() {
        if (!core) return;
        if (srv != ~0u) core->srv.free(srv);
        for (u32 i : rtv) core->rtv.free(i);
        for (u32 i : dsv) core->dsv.free(i);
        srv = ~0u;
        rtv.clear();
        dsv.clear();
        if (res || ui_slot != ~0u) core->bury(res, ui_slot);
        res.Reset();
        ui_slot = ~0u;
    }
    void describe(const TextureDesc& d) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    u64 ui_id() const override {
        if (ui_slot == ~0u && srv != ~0u) {
            ui_slot = core->ui_alloc();
            core->device->CopyDescriptorsSimple(1, core->view_cpu(ui_slot), core->srv.cpu(srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        return ui_slot == ~0u ? 0 : core->view_gpu(ui_slot).ptr;
    }

    std::shared_ptr<Core> core;
    ComPtr<ID3D12Resource> res;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    u32 serial = g_serial++;
    u32 srv = ~0u;
    std::vector<u32> rtv, dsv;   // one a layer
    DXGI_FORMAT target_format = DXGI_FORMAT_UNKNOWN;
    mutable u32 ui_slot = ~0u;
};

class Buffer12 final : public Buffer {
public:
    Buffer12(std::shared_ptr<Core> c, BufferKind k, size_t n, bool dyn) : core(std::move(c)), dynamic(dyn) {
        kind_ = k;
        bytes_ = n;
    }
    ~Buffer12() override {
        if (core && res) core->bury(res);
    }
    std::shared_ptr<Core> core;
    bool dynamic = false;
    ComPtr<ID3D12Resource> res;               // static: in the card's memory
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    std::vector<u8> shadow;                   // dynamic: what the CPU last wrote
    D3D12_GPU_VIRTUAL_ADDRESS va = 0;         // dynamic: this frame's copy in the ring
    u64 frame = ~0ull;                        // the frame `va` belongs to
};

class Program12 final : public Program {
public:
    std::vector<u8> vs, ps;
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    u32 serial = g_serial++;
};

class Blend12 final : public BlendState {
public:
    D3D12_BLEND_DESC d{};
    u32 serial = g_serial++;
};
class Raster12 final : public RasterState {
public:
    D3D12_RASTERIZER_DESC d{};
    u32 serial = g_serial++;
};
class Depth12 final : public DepthState {
public:
    D3D12_DEPTH_STENCIL_DESC d{};
    u32 serial = g_serial++;
};
class Sampler12 final : public Sampler {
public:
    D3D12_SAMPLER_DESC d{};
    u32 serial = g_serial++;
};

D3D12_BLEND factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return D3D12_BLEND_ZERO;
        case BlendFactor::One: return D3D12_BLEND_ONE;
        case BlendFactor::SrcColour: return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DestColour: return D3D12_BLEND_DEST_COLOR;
    }
    return D3D12_BLEND_ONE;
}
D3D12_BLEND_OP op(BlendOp o) { return o == BlendOp::RevSubtract ? D3D12_BLEND_OP_REV_SUBTRACT : D3D12_BLEND_OP_ADD; }

D3D12_PRIMITIVE_TOPOLOGY topology(Topology t) {
    switch (t) {
        case Topology::Triangles: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case Topology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case Topology::Lines: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

// A pipeline as drawn with: the program, its fixed-function states and what it draws into.
struct PsoKey {
    PsoKey() {
        std::memset(this, 0, sizeof(*this));   // hashed and compared as bytes: no stray padding
        samples = 1;
    }
    u32 program, blend, raster, depth;
    u8 lines, count, samples, depth_on;
    DXGI_FORMAT rtv[4];
    DXGI_FORMAT dsv;
    bool operator==(const PsoKey& o) const { return std::memcmp(this, &o, sizeof(PsoKey)) == 0; }
};
struct PsoHash {
    size_t operator()(const PsoKey& k) const {
        const auto* p = reinterpret_cast<const u8*>(&k);
        u64 h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(PsoKey); ++i) h = (h ^ p[i]) * 1099511628211ull;
        return size_t(h);
    }
};

class Device12;

class Timer12 final : public TimerFrame {
public:
    Timer12(Device12& d, int n);
    bool ok() const { return heap_ && readback_; }
    void begin() override { used_ = 0; }
    void stamp(int i) override;
    void end() override;
    bool poll(double* ms, int count, bool& valid) override;

private:
    Device12& dev_;
    ComPtr<ID3D12QueryHeap> heap_;
    ComPtr<ID3D12Resource> readback_;
    int count_ = 0, used_ = 0, resolved_ = 0;
    u64 fence_ = 0;
};

class Device12 final : public Device {
public:
    ~Device12() override { destroy(); }
    Api api() const override { return Api::D3D12; }

    bool create(void* hwnd, int width, int height, const Options& o) override;
    void destroy() override;
    bool resize(int width, int height) override;
    void begin_frame(const Color& clear) override;
    void present() override;
    bool capture(Image& out) override;
    Texture* back_buffer() override { return &back_; }
    Texture* back_depth() override { return back_depth_.get(); }

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

    using Device::set_targets;
    void set_targets(std::span<const Target> colour, Target depth) override;
    void set_viewport(const Viewport& v) override;
    void clear(Target target, const Color& colour) override;
    void clear_depth(Target target, float depth) override;
    void set_program(const Program* program) override { program_ = static_cast<const Program12*>(program); }
    void set_blend(const BlendState* state) override { blend_ = static_cast<const Blend12*>(state); }
    void set_raster(const RasterState* state) override { raster_ = static_cast<const Raster12*>(state); }
    void set_depth(const DepthState* state) override { depth_state_ = static_cast<const Depth12*>(state); }
    void set_uniforms(int slot, const Buffer* buffer) override {
        if (slot >= 0 && slot < int(kCbvSlots)) cbv_[slot] = static_cast<const Buffer12*>(buffer);
    }
    void set_texture(int slot, const Texture* texture) override;
    void set_sampler(int slot, const Sampler* sampler) override;
    void set_vertices(const Buffer* buffer, u32 stride) override {
        vb_ = static_cast<const Buffer12*>(buffer);
        vb_stride_ = stride;
    }
    void set_indices(const Buffer* buffer) override { ib_ = static_cast<const Buffer12*>(buffer); }
    void draw(Topology t, u32 count, u32 first) override;
    void draw_indexed(Topology t, u32 count, u32 first) override;
    void resolve(Texture& dst, const Texture& src) override;
    void shader_read(Texture& texture) override {
        transition(static_cast<Texture12&>(texture), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        flush_barriers();
    }

    bool ui_init() override;
    void ui_shutdown() override;
    void ui_new_frame() override { VanGui_ImplDX12_NewFrame(); }
    void ui_render() override;

    std::unique_ptr<TimerFrame> create_timer_frame(int stamps) override {
        auto t = std::make_unique<Timer12>(*this, stamps);
        return t->ok() ? std::move(t) : nullptr;
    }
    bool video_memory(size_t& used, size_t& budget) const override;

    // For the timers.
    ID3D12Device* device() const { return core_->device.Get(); }
    ID3D12GraphicsCommandList* list() const { return list_.Get(); }
    u64 recording() const { return core_->recording; }
    u64 completed() const { return core_->fence->GetCompletedValue(); }
    u64 timestamp_frequency() const { return ts_freq_; }

private:
    struct Ring {
        ComPtr<ID3D12Resource> buffer;
        u8* cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        u64 used = 0;
    };
    struct Slice {
        u8* cpu;
        D3D12_GPU_VIRTUAL_ADDRESS gpu;
        ID3D12Resource* resource;
        u64 offset;
    };
    Slice ring_alloc(u64 bytes, u64 alignment);
    bool make_window_targets();
    void release_window_targets();
    void transition(Texture12& t, D3D12_RESOURCE_STATES s);
    void transition(Buffer12& b, D3D12_RESOURCE_STATES s);
    void flush_barriers();
    void prepare(Topology t);
    void apply_targets();
    D3D12_GPU_VIRTUAL_ADDRESS address(const Buffer12& b);
    // Everything the card has not run yet, run, and waited for; recording starts over.
    void submit_and_wait();
    void begin_recording();
    void invalidate();
    void check(HRESULT hr, const char* what);
    void report_removal();
    void drain_messages();
    ID3D12PipelineState* pipeline(const PsoKey& key);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_of(const Target& t) const;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_of(const Target& t) const;

    std::shared_ptr<Core> core_;
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12CommandAllocator> allocators_[kFrames];
    u64 frame_fence_[kFrames]{};
    u32 frame_ = 0;
    u64 frame_serial_ = 1;   // counts frames: a dynamic buffer's copy belongs to one
    HANDLE event_ = nullptr;
    ComPtr<IDXGISwapChain3> swap_;
    ComPtr<ID3D12Resource> back_res_[kBackBuffers];
    u32 back_rtv_[kBackBuffers]{};
    Texture12 back_{nullptr, TextureDesc{}};
    std::shared_ptr<Texture12> back_depth_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12InfoQueue> info_;
    bool tearing_ = false, ui_ = false, recording_open_ = false;
    u64 ts_freq_ = 0;
    u64 pending_upload_ = 0;
    Ring ring_[kFrames];
    std::vector<ComPtr<ID3D12Resource>> ring_extra_[kFrames];   // overflow when a frame's ring runs out
    u32 table_used_ = 0;                                         // this frame's descriptors of the ring
    std::unordered_map<u64, u32> table_cache_;                  // this frame's texture tables
    std::map<std::array<u32, kSamplerSlots>, u32> sampler_tables_;
    u32 sampler_next_ = 0;
    u32 null_srv_ = ~0u;
    ComPtr<ID3D12Resource> null_cb_;
    std::vector<D3D12_RESOURCE_BARRIER> barriers_;
    std::unordered_map<PsoKey, ComPtr<ID3D12PipelineState>, PsoHash> psos_;

    std::vector<std::pair<BlendDesc, std::unique_ptr<Blend12>>> blends_;
    std::vector<std::pair<RasterDesc, std::unique_ptr<Raster12>>> rasters_;
    std::vector<std::pair<DepthDesc, std::unique_ptr<Depth12>>> depths_;
    std::vector<std::pair<SamplerDesc, std::unique_ptr<Sampler12>>> samplers_;

    // What is bound, and what the command list was last given.
    const Program12* program_ = nullptr;
    const Blend12* blend_ = nullptr;
    const Raster12* raster_ = nullptr;
    const Depth12* depth_state_ = nullptr;
    const Buffer12* cbv_[kCbvSlots]{};
    const Texture12* srv_[kSrvSlots]{};
    const Sampler12* samp_[kSamplerSlots]{};
    const Buffer12* vb_ = nullptr;
    u32 vb_stride_ = 0;
    const Buffer12* ib_ = nullptr;
    bool root_set_ = false, targets_set_ = false, viewport_set_ = false;
    ID3D12PipelineState* pso_set_ = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS cbv_set_[kCbvSlots]{};
    u64 table_set_ = ~0ull, samplers_set_ = ~0ull;
    D3D12_VERTEX_BUFFER_VIEW vb_set_{};
    D3D12_INDEX_BUFFER_VIEW ib_set_{};
    D3D12_PRIMITIVE_TOPOLOGY topo_set_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

// ── Timers ─────────────────────────────────────────────────────────────────────

Timer12::Timer12(Device12& d, int n) : dev_(d), count_(n) {
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = UINT(std::max(n, 1));
    if (FAILED(d.device()->CreateQueryHeap(&qd, IID_PPV_ARGS(&heap_)))) return;
    const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC rd = buffer_desc(u64(std::max(n, 1)) * 8);
    d.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_));
}

void Timer12::stamp(int i) {
    if (i < 0 || i >= count_) return;
    dev_.list()->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(i));
    used_ = std::max(used_, i + 1);
}

void Timer12::end() {
    if (used_ == 0) return;
    dev_.list()->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, UINT(used_), readback_.Get(), 0);
    resolved_ = used_;
    fence_ = dev_.recording();
}

bool Timer12::poll(double* ms, int count, bool& valid) {
    if (fence_ == 0 || dev_.completed() < fence_) return false;
    count = std::min(count, resolved_);
    const D3D12_RANGE r{0, SIZE_T(count) * 8};
    void* p = nullptr;
    if (FAILED(readback_->Map(0, &r, &p))) return false;
    const auto* t = static_cast<const UINT64*>(p);
    const double to_ms = dev_.timestamp_frequency() ? 1000.0 / double(dev_.timestamp_frequency()) : 0.0;
    for (int i = 0; i < count; ++i) ms[i] = double(t[i] - t[0]) * to_ms;
    const D3D12_RANGE none{0, 0};
    readback_->Unmap(0, &none);
    valid = to_ms > 0;
    return true;
}

// ── Device ─────────────────────────────────────────────────────────────────────

bool Device12::create(void* hwnd, int width, int height, const Options& o) {
    vsync_ = o.vsync;
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    bool debug_layer = o.debug;
#ifdef _DEBUG
    debug_layer = true;
#endif
    if (debug_layer) {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            LOG_INFO("D3D12 debug layer on: its warnings and errors go to this log");
        } else {
            LOG_WARN("The D3D12 debug layer is not installed (Windows' Graphics Tools); going on without it");
            debug_layer = false;
        }
    }
    // DRED: which GPU operation a reset card died in, and the address that faulted.
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }
    if (FAILED(CreateDXGIFactory2(debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory_))) &&
        FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)))) {
        LOG_ERROR("CreateDXGIFactory2 failed");
        return false;
    }
    auto core = std::make_shared<Core>();
    // The fastest card, asked for by name: on a laptop with two, the default may be the slower.
    if (o.warp) {
        factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_));
        LOG_INFO("Using the WARP software renderer (--warp)");
    } else {
        ComPtr<IDXGIFactory6> f6;
        if (SUCCEEDED(factory_.As(&f6)))
            for (UINT i = 0; !adapter_; ++i) {
                ComPtr<IDXGIAdapter1> a;
                if (FAILED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)))) break;
                DXGI_ADAPTER_DESC1 ad{};
                a->GetDesc1(&ad);
                if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) adapter_ = a;
            }
    }
    HRESULT hr = adapter_ ? D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&core->device)) : E_FAIL;
    if (FAILED(hr) && !o.warp) {
        adapter_.Reset();
        factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_));
        hr = adapter_ ? D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&core->device)) : E_FAIL;
        if (SUCCEEDED(hr)) LOG_WARN("No hardware Direct3D 12: using the WARP software renderer");
    }
    if (FAILED(hr)) {
        LOG_ERROR("D3D12CreateDevice failed (0x%08x)", unsigned(hr));
        return false;
    }
    ID3D12Device* dev = core->device.Get();
    if (debug_layer && SUCCEEDED(core->device.As(&info_))) {
        D3D12_MESSAGE_SEVERITY deny[] = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
        // Clearing to a colour other than the one a target was made for, and the scene shader's
        // motion and surface outputs with nothing bound (temporal AA and reflections off): both
        // as intended.
        D3D12_MESSAGE_ID ids[] = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                  D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                                  D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_RENDERTARGETVIEW_NOT_SET};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumSeverities = UINT(std::size(deny));
        filter.DenyList.pSeverityList = deny;
        filter.DenyList.NumIDs = UINT(std::size(ids));
        filter.DenyList.pIDList = ids;
        info_->AddStorageFilterEntries(&filter);
    }

    DXGI_ADAPTER_DESC1 ad{};
    adapter_->GetDesc1(&ad);
    char name[128];
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name), nullptr, nullptr);
    adapter_name_ = name;
    adapter_memory_ = ad.DedicatedVideoMemory;
    vendor_ = ad.VendorId;
    LARGE_INTEGER umd{};
    char driver[48] = "unknown";
    if (SUCCEEDED(adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
        std::snprintf(driver, sizeof(driver), "%u.%u.%u.%u", unsigned(HIWORD(umd.HighPart)), unsigned(LOWORD(umd.HighPart)),
                      unsigned(HIWORD(umd.LowPart)), unsigned(LOWORD(umd.LowPart)));
    D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl{UINT(std::size(want)), want, D3D_FEATURE_LEVEL_11_0};
    dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    if (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) sm.HighestShaderModel = D3D_SHADER_MODEL_6_0;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
    LOG_INFO("GPU: %s (%s, vendor %04x device %04x), %llu MB, driver %s, Direct3D 12 feature level %x, shader model %x, raytracing tier %d",
             name, vendor_name(ad.VendorId), unsigned(ad.VendorId), unsigned(ad.DeviceId),
             (unsigned long long)(ad.DedicatedVideoMemory / (1024 * 1024)), driver, unsigned(fl.MaxSupportedFeatureLevel),
             unsigned(sm.HighestShaderModel), int(o5.RaytracingTier));

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)))) return false;
    queue_->GetTimestampFrequency(&ts_freq_);
    for (auto& a : allocators_)
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)))) return false;
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&list_)))) return false;
    recording_open_ = true;
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&core->fence)))) return false;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Heaps.
    if (!core->srv.init(dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536) || !core->rtv.init(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4096) ||
        !core->dsv.init(dev, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1024))
        return false;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kUiSlots + kTableDescriptors * kFrames;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&core->views)))) return false;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hd.NumDescriptors = kSamplerHeap;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&core->samplers)))) return false;
    core->view_step = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    core->sampler_step = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    core_ = core;
    back_.core = core_;

    // An empty texture for slots with nothing bound, and an empty constant buffer.
    null_srv_ = core_->srv.alloc();
    D3D12_SHADER_RESOURCE_VIEW_DESC nsv{};
    nsv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nsv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nsv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nsv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(nullptr, &nsv, core_->srv.cpu(null_srv_));
    {
        const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_UPLOAD);
        // The most a constant buffer can span, so an unbound slot reads zeros wherever it looks.
        const D3D12_RESOURCE_DESC rd = buffer_desc(65536);
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&null_cb_))))
            return false;
        void* p = nullptr;
        null_cb_->Map(0, nullptr, &p);
        std::memset(p, 0, 65536);
        null_cb_->Unmap(0, nullptr);
    }
    for (auto& r : ring_) {
        const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC rd = buffer_desc(kRingBytes);
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r.buffer))))
            return false;
        r.buffer->Map(0, nullptr, reinterpret_cast<void**>(&r.cpu));
        r.gpu = r.buffer->GetGPUVirtualAddress();
    }

    // One root signature: b0-b3 as root constant buffers, t0-t15 and s0-s7 as tables.
    {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = kSrvSlots;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[1].NumDescriptors = kSamplerSlots;
        D3D12_ROOT_PARAMETER params[kCbvSlots + 2]{};
        for (u32 i = 0; i < kCbvSlots; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (u32 i = 0; i < 2; ++i) {
            auto& p = params[kCbvSlots + i];
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges = &ranges[i];
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC rs{};
        rs.NumParameters = UINT(std::size(params));
        rs.pParameters = params;
        rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
            LOG_ERROR("D3D12: root signature: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            return false;
        }
        if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)))) return false;
    }

    ComPtr<IDXGIFactory5> f5;
    if (SUCCEEDED(factory_.As(&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) tearing_ = allow != FALSE;
    }
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = UINT(width_);
    sd.Height = UINT(height_);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> sc;
    hr = factory_->CreateSwapChainForHwnd(queue_.Get(), HWND(hwnd), &sd, nullptr, nullptr, &sc);
    if (FAILED(hr) || FAILED(sc.As(&swap_))) {
        LOG_ERROR("CreateSwapChainForHwnd failed (0x%08x)", unsigned(hr));
        return false;
    }
    factory_->MakeWindowAssociation(HWND(hwnd), DXGI_MWA_NO_ALT_ENTER);
    for (u32& i : back_rtv_) i = core_->rtv.alloc();
    return make_window_targets();
}

bool Device12::make_window_targets() {
    for (u32 i = 0; i < kBackBuffers; ++i) {
        if (FAILED(swap_->GetBuffer(i, IID_PPV_ARGS(&back_res_[i])))) return false;
        core_->device->CreateRenderTargetView(back_res_[i].Get(), nullptr, core_->rtv.cpu(back_rtv_[i]));
    }
    TextureDesc bd;
    bd.width = width_;
    bd.height = height_;
    bd.target = true;
    back_.describe(bd);
    back_.target_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const u32 i = swap_->GetCurrentBackBufferIndex();
    back_.res = back_res_[i];
    back_.rtv.assign(1, back_rtv_[i]);
    back_.state = D3D12_RESOURCE_STATE_PRESENT;
    TextureDesc dd = bd;
    dd.format = Format::D24S8;
    back_depth_ = std::static_pointer_cast<Texture12>(create_texture(dd, {}));
    return back_depth_ != nullptr;
}

void Device12::release_window_targets() {
    targets_ = {};
    targets_set_ = false;
    back_.res.Reset();
    back_.rtv.clear();
    for (auto& r : back_res_) r.Reset();
    back_depth_.reset();
}

void Device12::check(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr) || lost_) return;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        lost_ = true;
        const HRESULT why = core_->device->GetDeviceRemovedReason();
        LOG_ERROR("The graphics device was lost (%s 0x%08x, reason 0x%08x)", what, unsigned(hr), unsigned(why));
        report_removal();
    } else {
        LOG_ERROR("D3D12: %s failed (0x%08x)", what, unsigned(hr));
    }
}

void Device12::report_removal() {
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(core_->device.As(&dred))) return;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc)))
        for (const D3D12_AUTO_BREADCRUMB_NODE* n = bc.pHeadAutoBreadcrumbNode; n; n = n->pNext) {
            const UINT done = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
            if (done >= n->BreadcrumbCount) continue;
            LOG_ERROR("DRED: command list stopped after %u of %u operations; next: op %d", done, n->BreadcrumbCount,
                      done < n->BreadcrumbCount ? int(n->pCommandHistory[done]) : -1);
        }
    D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA)
        LOG_ERROR("DRED: page fault at GPU address 0x%llx", (unsigned long long)pf.PageFaultVA);
}

void Device12::drain_messages() {
    if (!info_) return;
    static std::vector<std::pair<int, int>> seen;
    const UINT64 n = info_->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T size = 0;
        if (FAILED(info_->GetMessage(i, nullptr, &size)) || size == 0) continue;
        std::vector<char> buf(size);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(info_->GetMessage(i, m, &size))) continue;
        auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& s) { return s.first == int(m->ID); });
        if (it == seen.end()) it = seen.insert(seen.end(), {int(m->ID), 0});
        if (++it->second > 3) continue;
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) LOG_ERROR("D3D12 debug: %.*s", int(m->DescriptionByteLength), m->pDescription);
        else LOG_WARN("D3D12 debug: %.*s", int(m->DescriptionByteLength), m->pDescription);
    }
    info_->ClearStoredMessages();
}

void Device12::invalidate() {
    root_set_ = targets_set_ = viewport_set_ = false;
    pso_set_ = nullptr;
    std::fill(std::begin(cbv_set_), std::end(cbv_set_), 0);
    table_set_ = samplers_set_ = ~0ull;
    vb_set_ = {};
    ib_set_ = {};
    topo_set_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void Device12::begin_recording() {
    allocators_[frame_]->Reset();
    list_->Reset(allocators_[frame_].Get(), nullptr);
    recording_open_ = true;
    ring_[frame_].used = 0;
    ring_extra_[frame_].clear();
    table_used_ = 0;
    table_cache_.clear();
    invalidate();
}

void Device12::submit_and_wait() {
    flush_barriers();
    check(list_->Close(), "Close");
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const u64 v = core_->recording++;
    check(queue_->Signal(core_->fence.Get(), v), "Signal");
    if (core_->fence->GetCompletedValue() < v) {
        core_->fence->SetEventOnCompletion(v, event_);
        WaitForSingleObject(event_, 10000);
    }
    core_->collect(core_->fence->GetCompletedValue());
    pending_upload_ = 0;
    begin_recording();
}

void Device12::destroy() {
    if (core_ && queue_ && list_) {
        if (recording_open_) {
            list_->Close();
            recording_open_ = false;
        }
        const u64 v = core_->recording++;
        queue_->Signal(core_->fence.Get(), v);
        if (core_->fence->GetCompletedValue() < v) {
            core_->fence->SetEventOnCompletion(v, event_);
            WaitForSingleObject(event_, 10000);
        }
    }
    release_window_targets();
    psos_.clear();
    blends_.clear();
    rasters_.clear();
    depths_.clear();
    samplers_.clear();
    if (core_) {
        core_->collect(~0ull);
        core_->alive = false;
    }
    for (auto& r : ring_) r = {};
    for (auto& e : ring_extra_) e.clear();
    null_cb_.Reset();
    root_.Reset();
    swap_.Reset();
    list_.Reset();
    for (auto& a : allocators_) a.Reset();
    queue_.Reset();
    info_.Reset();
    adapter_.Reset();
    factory_.Reset();
    back_.core.reset();
    core_.reset();
    if (event_) CloseHandle(event_);
    event_ = nullptr;
}

bool Device12::resize(int width, int height) {
    if (!swap_ || width <= 0 || height <= 0) return false;
    if (width == width_ && height == height_) return true;
    submit_and_wait();
    release_window_targets();
    core_->collect(core_->fence->GetCompletedValue());
    width_ = width;
    height_ = height;
    const HRESULT hr = swap_->ResizeBuffers(kBackBuffers, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        LOG_ERROR("ResizeBuffers failed (0x%08x)", unsigned(hr));
        return false;
    }
    return make_window_targets();
}

void Device12::begin_frame(const Color& clear_colour) {
    const u32 i = swap_->GetCurrentBackBufferIndex();
    back_.res = back_res_[i];
    back_.rtv.assign(1, back_rtv_[i]);
    back_.state = D3D12_RESOURCE_STATE_PRESENT;
    const Target colour{&back_};
    set_targets(std::span<const Target>(&colour, 1), Target{back_depth_.get()});
    clear(colour, clear_colour);
    clear_depth(Target{back_depth_.get()}, 1.0f);
    set_viewport({0, 0, float(width_), float(height_)});
}

void Device12::present() {
    if (!swap_ || lost_) return;
    transition(back_, D3D12_RESOURCE_STATE_PRESENT);
    flush_barriers();
    check(list_->Close(), "Close");
    recording_open_ = false;
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const UINT flags = (!vsync_ && tearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    check(swap_->Present(vsync_ ? 1 : 0, flags), "Present");
    const u64 v = core_->recording++;
    check(queue_->Signal(core_->fence.Get(), v), "Signal");
    frame_fence_[frame_] = v;
    drain_messages();
    // The next frame's allocator and ring, once the card has finished the frame that last used them.
    frame_ = (frame_ + 1) % kFrames;
    if (core_->fence->GetCompletedValue() < frame_fence_[frame_]) {
        core_->fence->SetEventOnCompletion(frame_fence_[frame_], event_);
        WaitForSingleObject(event_, 10000);
    }
    core_->collect(core_->fence->GetCompletedValue());
    ++frame_serial_;
    pending_upload_ = 0;
    // Nothing bound is carried into the next frame: a texture bound last frame may be gone by the
    // next (a match world freed between frames), and its slot would be transitioned on the next draw.
    std::fill(std::begin(srv_), std::end(srv_), nullptr);
    table_set_ = ~0ull;
    // The window's colour target is now the swap chain's next buffer (a frame need not begin with
    // begin_frame: the post passes draw into the back buffer themselves, as on Direct3D 11).
    const u32 i = swap_->GetCurrentBackBufferIndex();
    back_.res = back_res_[i];
    back_.rtv.assign(1, back_rtv_[i]);
    back_.state = D3D12_RESOURCE_STATE_PRESENT;
    begin_recording();
}

bool Device12::capture(Image& out) {
    if (!back_.res) return false;
    const D3D12_RESOURCE_DESC rd = back_.res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    core_->device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &row_bytes, &total);
    ComPtr<ID3D12Resource> readback;
    const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC bd = buffer_desc(total);
    if (FAILED(core_->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                       IID_PPV_ARGS(&readback))))
        return false;
    const D3D12_RESOURCE_STATES was = back_.state;
    transition(back_, D3D12_RESOURCE_STATE_COPY_SOURCE);
    flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION dst{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION src{back_.res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    transition(back_, was == D3D12_RESOURCE_STATE_PRESENT ? D3D12_RESOURCE_STATE_RENDER_TARGET : was);
    const Targets keep = targets_;
    const Viewport vp = viewport_;
    submit_and_wait();
    set_targets(keep);
    set_viewport(vp);
    u8* p = nullptr;
    const D3D12_RANGE r{0, SIZE_T(total)};
    if (FAILED(readback->Map(0, &r, reinterpret_cast<void**>(&p)))) return false;
    out.width = int(rd.Width);
    out.height = int(rd.Height);
    out.rgba.resize(size_t(rd.Width) * rd.Height * 4);
    for (UINT y = 0; y < rd.Height; ++y) {
        u8* d = out.rgba.data() + size_t(y) * rd.Width * 4;
        std::memcpy(d, p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, size_t(rd.Width) * 4);
        for (UINT x = 0; x < rd.Width; ++x) d[x * 4 + 3] = 255;
    }
    const D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    return true;
}

Device12::Slice Device12::ring_alloc(u64 bytes, u64 alignment) {
    Ring& r = ring_[frame_];
    const u64 at = align(r.used, alignment);
    if (at + bytes <= kRingBytes) {
        r.used = at + bytes;
        return {r.cpu + at, r.gpu + at, r.buffer.Get(), at};
    }
    // This frame asked for more than the ring holds (a level loading): a buffer of its own,
    // let go of with the frame.
    ComPtr<ID3D12Resource> extra;
    const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC rd = buffer_desc(align(bytes, 65536));
    core_->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&extra));
    u8* cpu = nullptr;
    extra->Map(0, nullptr, reinterpret_cast<void**>(&cpu));
    core_->bury(extra);
    ring_extra_[frame_].push_back(extra);
    return {cpu, extra->GetGPUVirtualAddress(), extra.Get(), 0};
}

void Device12::transition(Texture12& t, D3D12_RESOURCE_STATES s) {
    if (!t.res || t.state == s) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t.state;
    b.Transition.StateAfter = s;
    barriers_.push_back(b);
    t.state = s;
}

void Device12::transition(Buffer12& buf, D3D12_RESOURCE_STATES s) {
    if (!buf.res || buf.state == s) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = buf.res.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = buf.state;
    b.Transition.StateAfter = s;
    barriers_.push_back(b);
    buf.state = s;
}

void Device12::flush_barriers() {
    if (barriers_.empty()) return;
    list_->ResourceBarrier(UINT(barriers_.size()), barriers_.data());
    barriers_.clear();
}

TextureRef Device12::create_texture(const TextureDesc& d, std::span<const TextureData> init) {
    auto t = std::make_shared<Texture12>(core_, d);
    const Formats f = dxgi(d.format);
    const bool depth = is_depth(d.format);
    const UINT16 layers = UINT16(d.type == TextureType::Cube ? 6 : std::max(1, d.layers));
    const UINT16 mips = UINT16(std::max(1, d.mips));
    const UINT samples = UINT(std::max(1, d.samples));
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = UINT64(std::max(1, d.width));
    rd.Height = UINT(std::max(1, d.height));
    rd.DepthOrArraySize = layers;
    rd.MipLevels = mips;
    rd.Format = f.resource;
    rd.SampleDesc.Count = samples;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (d.target) rd.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const u32 subs = u32(mips) * layers;
    if (!init.empty() && init.size() != subs) return nullptr;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = f.target;
    if (depth) cv.DepthStencil.Depth = 1;
    D3D12_RESOURCE_STATES first = D3D12_RESOURCE_STATE_COPY_DEST;
    if (init.empty()) first = d.target ? (depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET)
                                       : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT hr = core_->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, first, d.target ? &cv : nullptr, IID_PPV_ARGS(&t->res));
    if (FAILED(hr)) {
        check(hr, "CreateCommittedResource (texture)");
        return nullptr;
    }
    t->state = first;
    t->target_format = f.target;

    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = f.srv;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (d.type == TextureType::Cube) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = mips;
    } else if (d.type == TextureType::Array) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Texture2DArray.MipLevels = mips;
        sv.Texture2DArray.ArraySize = layers;
    } else if (samples > 1) {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    } else {
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = mips;
    }
    t->srv = core_->srv.alloc();
    core_->device->CreateShaderResourceView(t->res.Get(), &sv, core_->srv.cpu(t->srv));

    if (d.target) {
        const bool layered = d.type != TextureType::Plain;
        for (UINT16 layer = 0; layer < layers; ++layer) {
            if (depth) {
                D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
                dv.Format = f.target;
                if (layered) {
                    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                    dv.Texture2DArray.FirstArraySlice = layer;
                    dv.Texture2DArray.ArraySize = 1;
                } else {
                    dv.ViewDimension = samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
                }
                const u32 i = core_->dsv.alloc();
                core_->device->CreateDepthStencilView(t->res.Get(), &dv, core_->dsv.cpu(i));
                t->dsv.push_back(i);
            } else {
                D3D12_RENDER_TARGET_VIEW_DESC rv{};
                rv.Format = f.target;
                if (layered) {
                    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rv.Texture2DArray.FirstArraySlice = layer;
                    rv.Texture2DArray.ArraySize = 1;
                } else {
                    rv.ViewDimension = samples > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
                }
                const u32 i = core_->rtv.alloc();
                core_->device->CreateRenderTargetView(t->res.Get(), &rv, core_->rtv.cpu(i));
                t->rtv.push_back(i);
            }
        }
    }

    if (!init.empty()) {
        // Every level of every layer into one upload buffer, laid out as the card copies it.
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(subs);
        std::vector<UINT> rows(subs);
        std::vector<UINT64> row_bytes(subs);
        UINT64 total = 0;
        core_->device->GetCopyableFootprints(&rd, 0, subs, 0, fp.data(), rows.data(), row_bytes.data(), &total);
        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES up = heap(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC ud = buffer_desc(total);
        if (FAILED(core_->device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                           IID_PPV_ARGS(&upload))))
            return nullptr;
        u8* p = nullptr;
        upload->Map(0, nullptr, reinterpret_cast<void**>(&p));
        for (u32 s = 0; s < subs; ++s) {
            const u8* src = static_cast<const u8*>(init[s].data);
            const size_t n = size_t(std::min<UINT64>(row_bytes[s], init[s].row_pitch ? init[s].row_pitch : row_bytes[s]));
            for (UINT y = 0; y < rows[s]; ++y)
                std::memcpy(p + fp[s].Offset + size_t(y) * fp[s].Footprint.RowPitch, src + size_t(y) * init[s].row_pitch, n);
        }
        upload->Unmap(0, nullptr);
        for (u32 s = 0; s < subs; ++s) {
            D3D12_TEXTURE_COPY_LOCATION dst{t->res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            dst.SubresourceIndex = s;
            D3D12_TEXTURE_COPY_LOCATION src{upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
            src.PlacedFootprint = fp[s];
            list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
        transition(*t, d.target ? (depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET)
                                : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        core_->bury(upload);
        pending_upload_ += total;
        if (pending_upload_ > kUploadFlushBytes) submit_and_wait();
    }
    return t;
}

void Device12::update_texture(Texture& texture, const void* data, u32 row_pitch) {
    auto& t = static_cast<Texture12&>(texture);
    if (!t.res || !t.desc().dynamic) return;
    const D3D12_RESOURCE_DESC rd = t.res->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    core_->device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &row_bytes, &total);
    const Slice s = ring_alloc(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    const size_t n = size_t(std::min<UINT64>(row_bytes, row_pitch));
    for (UINT y = 0; y < rows; ++y) std::memcpy(s.cpu + size_t(y) * fp.Footprint.RowPitch, static_cast<const u8*>(data) + size_t(y) * row_pitch, n);
    fp.Offset = s.offset;
    transition(t, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION dst{t.res.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{s.resource, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint = fp;
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    transition(t, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

BufferRef Device12::create_buffer(BufferKind kind, const void* data, size_t bytes, bool dynamic) {
    auto b = std::make_shared<Buffer12>(core_, kind, bytes, dynamic);
    if (dynamic) {
        b->shadow.assign(bytes, 0);
        if (data) std::memcpy(b->shadow.data(), data, bytes);
        return b;
    }
    const u64 size = align(bytes, 256);
    const D3D12_HEAP_PROPERTIES hp = heap(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC rd = buffer_desc(size);
    const HRESULT hr = core_->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&b->res));
    if (FAILED(hr)) {
        check(hr, "CreateCommittedResource (buffer)");
        return nullptr;
    }
    b->state = D3D12_RESOURCE_STATE_COMMON;
    if (data) {
        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES up = heap(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC ud = buffer_desc(size);
        if (FAILED(core_->device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                           IID_PPV_ARGS(&upload))))
            return nullptr;
        void* p = nullptr;
        upload->Map(0, nullptr, &p);
        std::memcpy(p, data, bytes);
        upload->Unmap(0, nullptr);
        transition(*b, D3D12_RESOURCE_STATE_COPY_DEST);
        flush_barriers();
        list_->CopyBufferRegion(b->res.Get(), 0, upload.Get(), 0, bytes);
        core_->bury(upload);
        pending_upload_ += size;
    }
    transition(*b, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (pending_upload_ > kUploadFlushBytes) submit_and_wait();
    return b;
}

void Device12::update_buffer(Buffer& buffer, const void* data, size_t bytes) {
    auto& b = static_cast<Buffer12&>(buffer);
    bytes = std::min(bytes, b.bytes());
    if (b.dynamic) {
        std::memcpy(b.shadow.data(), data, bytes);
        const Slice s = ring_alloc(align(b.bytes(), 256), 256);
        std::memcpy(s.cpu, b.shadow.data(), b.bytes());
        b.va = s.gpu;
        b.frame = frame_serial_;
        return;
    }
    // A static buffer rewritten: copied in order with the draws, as UpdateSubresource did.
    const Slice s = ring_alloc(align(bytes, 256), 256);
    std::memcpy(s.cpu, data, bytes);
    transition(b, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    list_->CopyBufferRegion(b.res.Get(), 0, s.resource, s.offset, bytes);
    transition(b, D3D12_RESOURCE_STATE_GENERIC_READ);
}

D3D12_GPU_VIRTUAL_ADDRESS Device12::address(const Buffer12& cb) {
    auto& b = const_cast<Buffer12&>(cb);
    if (!b.dynamic) return b.res ? b.res->GetGPUVirtualAddress() : 0;
    if (b.frame != frame_serial_) {
        // Not rewritten this frame: last frame's copy may be overwritten, so this frame gets one.
        const Slice s = ring_alloc(align(b.bytes(), 256), 256);
        std::memcpy(s.cpu, b.shadow.data(), b.bytes());
        b.va = s.gpu;
        b.frame = frame_serial_;
    }
    return b.va;
}

ProgramRef Device12::create_program(const ShaderSource& source, const char* vs_entry, const char* ps_entry, std::span<const VertexAttr> layout,
                                    std::string* error) {
    auto p = std::make_shared<Program12>();
    auto find = [&](const char* entry, std::vector<u8>& out) {
        for (size_t i = 0; i < source.dxil_count; ++i)
            if (std::strcmp(source.dxil[i].entry, entry) == 0) {
                out.assign(source.dxil[i].code, source.dxil[i].code + source.dxil[i].size);
                return true;
            }
        return false;
    };
    if (!find(vs_entry, p->vs) || !find(ps_entry, p->ps)) {
        const std::string msg = std::string("no DXIL for ") + source.name + " " + vs_entry + "/" + ps_entry + " (Tools/shaders_gen.py)";
        LOG_ERROR("Shader %s", msg.c_str());
        if (error) *error = msg;
        return nullptr;
    }
    for (const VertexAttr& a : layout) {
        static const char* names[] = {"POSITION", "NORMAL", "TEXCOORD", "COLOR", "BLENDINDICES", "BLENDWEIGHT"};
        DXGI_FORMAT f = DXGI_FORMAT_R32G32B32_FLOAT;
        switch (a.format) {
            case AttrFormat::Float2: f = DXGI_FORMAT_R32G32_FLOAT; break;
            case AttrFormat::Float3: f = DXGI_FORMAT_R32G32B32_FLOAT; break;
            case AttrFormat::Float4: f = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
            case AttrFormat::UNorm8x4: f = DXGI_FORMAT_R8G8B8A8_UNORM; break;
            case AttrFormat::UInt8x4: f = DXGI_FORMAT_R8G8B8A8_UINT; break;
        }
        p->layout.push_back({names[int(a.semantic)], UINT(a.index), f, 0, a.offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    }
    return p;
}

int Device12::samples_for(Format format, int want) {
    int n = std::max(1, want);
    while (n > 1) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
        q.Format = dxgi(format).resource == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_D24_UNORM_S8_UINT
                   : dxgi(format).resource == DXGI_FORMAT_R32_TYPELESS  ? DXGI_FORMAT_D32_FLOAT
                                                                        : dxgi(format).resource;
        q.SampleCount = UINT(n);
        if (SUCCEEDED(core_->device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) && q.NumQualityLevels > 0) break;
        n /= 2;
    }
    return n;
}

const BlendState* Device12::blend(const BlendDesc& d) {
    for (auto& [k, v] : blends_)
        if (k == d) return v.get();
    auto s = std::make_unique<Blend12>();
    s->d.AlphaToCoverageEnable = d.alpha_to_coverage;
    s->d.IndependentBlendEnable = TRUE;
    for (int k = 0; k < 8; ++k) {
        auto& t = s->d.RenderTarget[k];
        t.SrcBlend = t.SrcBlendAlpha = D3D12_BLEND_ONE;
        t.DestBlend = t.DestBlendAlpha = D3D12_BLEND_ZERO;
        t.BlendOp = t.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        t.LogicOp = D3D12_LOGIC_OP_NOOP;
        t.RenderTargetWriteMask = k < 4 && ((d.write >> k) & 1) ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
        if (k > 0) continue;
        t.BlendEnable = d.enable;
        t.SrcBlend = factor(d.src);
        t.DestBlend = factor(d.dst);
        t.BlendOp = op(d.op);
        t.SrcBlendAlpha = factor(d.src_alpha);
        t.DestBlendAlpha = factor(d.dst_alpha);
        t.BlendOpAlpha = op(d.op_alpha);
    }
    blends_.emplace_back(d, std::move(s));
    return blends_.back().second.get();
}

const RasterState* Device12::raster(const RasterDesc& d) {
    for (auto& [k, v] : rasters_)
        if (k == d) return v.get();
    auto s = std::make_unique<Raster12>();
    s->d.FillMode = d.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    s->d.CullMode = d.cull == Cull::None ? D3D12_CULL_MODE_NONE : d.cull == Cull::Front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK;
    s->d.FrontCounterClockwise = FALSE;   // left-handed data, clockwise front faces
    s->d.SlopeScaledDepthBias = d.slope_bias;
    s->d.DepthBiasClamp = d.bias_clamp;
    s->d.DepthClipEnable = TRUE;
    rasters_.emplace_back(d, std::move(s));
    return rasters_.back().second.get();
}

const DepthState* Device12::depth(const DepthDesc& d) {
    for (auto& [k, v] : depths_)
        if (k == d) return v.get();
    auto s = std::make_unique<Depth12>();
    s->d.DepthEnable = d.test;
    s->d.DepthWriteMask = d.write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    s->d.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depths_.emplace_back(d, std::move(s));
    return depths_.back().second.get();
}

const Sampler* Device12::sampler(const SamplerDesc& d) {
    for (auto& [k, v] : samplers_)
        if (k == d) return v.get();
    auto s = std::make_unique<Sampler12>();
    auto& sd = s->d;
    if (d.compare) sd.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    else if (d.filter == Filter::Anisotropic) sd.Filter = D3D12_FILTER_ANISOTROPIC;
    else if (d.filter == Filter::Point) sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    else sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxAnisotropy = std::clamp<UINT>(d.anisotropy, 1, 16);
    const D3D12_TEXTURE_ADDRESS_MODE a = d.address == Address::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                         : d.address == Address::Clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP
                                                                       : D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sd.AddressU = sd.AddressV = sd.AddressW = a;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1;
    sd.ComparisonFunc = d.compare ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
    sd.MaxLOD = D3D12_FLOAT32_MAX;
    samplers_.emplace_back(d, std::move(s));
    return samplers_.back().second.get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Device12::rtv_of(const Target& t) const {
    const auto* tex = static_cast<const Texture12*>(t.texture);
    if (!tex || size_t(t.layer) >= tex->rtv.size()) return {};
    return core_->rtv.cpu(tex->rtv[size_t(t.layer)]);
}

D3D12_CPU_DESCRIPTOR_HANDLE Device12::dsv_of(const Target& t) const {
    const auto* tex = static_cast<const Texture12*>(t.texture);
    if (!tex || size_t(t.layer) >= tex->dsv.size()) return {};
    return core_->dsv.cpu(tex->dsv[size_t(t.layer)]);
}

void Device12::set_targets(std::span<const Target> colour, Target depth) {
    targets_ = {};
    const int n = std::min<int>(int(colour.size()), 4);
    for (int i = 0; i < n; ++i) targets_.colour[i] = colour[size_t(i)];
    targets_.count = n;
    targets_.depth = depth;
    targets_set_ = false;
    // A texture about to be drawn into is let go of as an input first, as Direct3D 11 does.
    for (auto& s : srv_) {
        if (!s) continue;
        bool hit = depth.texture == s;
        for (int i = 0; i < n; ++i) hit |= colour[size_t(i)].texture == s;
        if (hit) {
            s = nullptr;
            table_set_ = ~0ull;
        }
    }
}

void Device12::set_texture(int slot, const Texture* texture) {
    if (slot < 0 || slot >= int(kSrvSlots)) return;
    // A texture bound as a target cannot be read (Direct3D 11 refuses it too).
    if (texture) {
        bool target = targets_.depth.texture == texture;
        for (int i = 0; i < targets_.count; ++i) target |= targets_.colour[i].texture == texture;
        if (target) texture = nullptr;
    }
    srv_[slot] = static_cast<const Texture12*>(texture);
}

void Device12::set_sampler(int slot, const Sampler* sampler) {
    if (slot >= 0 && slot < int(kSamplerSlots)) samp_[slot] = static_cast<const Sampler12*>(sampler);
}

void Device12::set_viewport(const Viewport& v) {
    viewport_ = v;
    viewport_set_ = false;
}

void Device12::apply_targets() {
    for (int i = 0; i < targets_.count; ++i)
        if (auto* t = static_cast<Texture12*>(targets_.colour[i].texture)) transition(*t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (auto* t = static_cast<Texture12*>(targets_.depth.texture)) transition(*t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    if (targets_set_) return;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[4]{};
    UINT n = 0;
    for (int i = 0; i < targets_.count; ++i) {
        const D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_of(targets_.colour[i]);
        if (h.ptr) rtv[n++] = h;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_of(targets_.depth);
    flush_barriers();
    list_->OMSetRenderTargets(n, n ? rtv : nullptr, FALSE, dsv.ptr ? &dsv : nullptr);
    targets_set_ = true;
}

void Device12::clear(Target target, const Color& c) {
    auto* t = static_cast<Texture12*>(target.texture);
    const D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_of(target);
    if (!t || !h.ptr) return;
    transition(*t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    flush_barriers();
    const float f[4] = {c.r, c.g, c.b, c.a};
    list_->ClearRenderTargetView(h, f, 0, nullptr);
}

void Device12::clear_depth(Target target, float value) {
    auto* t = static_cast<Texture12*>(target.texture);
    const D3D12_CPU_DESCRIPTOR_HANDLE h = dsv_of(target);
    if (!t || !h.ptr) return;
    transition(*t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    flush_barriers();
    const D3D12_CLEAR_FLAGS flags = t->desc().format == Format::D24S8 ? D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL : D3D12_CLEAR_FLAG_DEPTH;
    list_->ClearDepthStencilView(h, flags, value, 0, 0, nullptr);
}

ID3D12PipelineState* Device12::pipeline(const PsoKey& key) {
    if (auto it = psos_.find(key); it != psos_.end()) return it->second.Get();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_.Get();
    pd.VS = {program_->vs.data(), program_->vs.size()};
    pd.PS = {program_->ps.data(), program_->ps.size()};
    pd.BlendState = blend_->d;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState = raster_->d;
    pd.DepthStencilState = depth_state_->d;
    if (!key.depth_on) {
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    }
    pd.InputLayout = {program_->layout.data(), UINT(program_->layout.size())};
    pd.PrimitiveTopologyType = key.lines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = key.count;
    for (u32 i = 0; i < key.count; ++i) pd.RTVFormats[i] = key.rtv[i];
    pd.DSVFormat = key.dsv;
    pd.SampleDesc.Count = key.samples;
    ComPtr<ID3D12PipelineState> pso;
    const HRESULT hr = core_->device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        static int warned = 0;
        if (warned++ < 8) LOG_ERROR("D3D12: a pipeline state could not be made (0x%08x)", unsigned(hr));
        check(hr, "CreateGraphicsPipelineState");
    }
    return (psos_[key] = pso).Get();
}

void Device12::prepare(Topology t) {
    // What the draw reads and writes, in the states it needs.
    for (const Texture12* s : srv_)
        if (s) transition(const_cast<Texture12&>(*s), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    apply_targets();
    flush_barriers();
    if (!root_set_) {
        ID3D12DescriptorHeap* heaps[] = {core_->views.Get(), core_->samplers.Get()};
        list_->SetDescriptorHeaps(2, heaps);
        list_->SetGraphicsRootSignature(root_.Get());
        root_set_ = true;
    }
    if (!viewport_set_) {
        const D3D12_VIEWPORT vp{viewport_.x, viewport_.y, viewport_.w, viewport_.h, 0, 1};
        const D3D12_RECT sc{LONG(std::max(0.0f, viewport_.x)), LONG(std::max(0.0f, viewport_.y)), LONG(viewport_.x + viewport_.w),
                            LONG(viewport_.y + viewport_.h)};
        list_->RSSetViewports(1, &vp);
        list_->RSSetScissorRects(1, &sc);
        viewport_set_ = true;
    }

    // The pipeline.
    PsoKey key;
    key.program = program_->serial;
    key.blend = blend_->serial;
    key.raster = raster_->serial;
    key.depth = depth_state_->serial;
    key.lines = t == Topology::Lines;
    u8 n = 0;
    for (int i = 0; i < targets_.count; ++i) {
        const auto* tex = static_cast<const Texture12*>(targets_.colour[i].texture);
        if (!tex || rtv_of(targets_.colour[i]).ptr == 0) continue;
        key.rtv[n++] = tex->target_format;
        key.samples = u8(std::max(1, tex->desc().samples));
    }
    key.count = n;
    if (const auto* d = static_cast<const Texture12*>(targets_.depth.texture); d && dsv_of(targets_.depth).ptr) {
        key.dsv = d->target_format;
        key.samples = u8(std::max(1, d->desc().samples));
        key.depth_on = 1;
    }
    if (ID3D12PipelineState* pso = pipeline(key); pso != pso_set_) {
        if (pso) list_->SetPipelineState(pso);
        pso_set_ = pso;
    }

    // Constant buffers.
    for (u32 i = 0; i < kCbvSlots; ++i) {
        const D3D12_GPU_VIRTUAL_ADDRESS va = cbv_[i] ? address(*cbv_[i]) : null_cb_->GetGPUVirtualAddress();
        if (va != cbv_set_[i]) {
            list_->SetGraphicsRootConstantBufferView(i, va);
            cbv_set_[i] = va;
        }
    }

    // Textures: one table a combination, made the first time this frame it is drawn with.
    u64 h = 1469598103934665603ull;
    for (const Texture12* s : srv_) h = (h ^ (s ? s->serial : 0)) * 1099511628211ull;
    if (h != table_set_) {
        u32 at;
        if (auto it = table_cache_.find(h); it != table_cache_.end()) {
            at = it->second;
        } else {
            if (table_used_ + kSrvSlots > kTableDescriptors) {
                LOG_WARN("D3D12: a frame used every texture table; waiting for the card to start over");
                submit_and_wait();
                prepare(t);
                return;
            }
            at = kUiSlots + frame_ * kTableDescriptors + table_used_;
            table_used_ += kSrvSlots;
            D3D12_CPU_DESCRIPTOR_HANDLE src[kSrvSlots];
            UINT ones[kSrvSlots];
            for (u32 i = 0; i < kSrvSlots; ++i) {
                src[i] = core_->srv.cpu(srv_[i] && srv_[i]->srv != ~0u ? srv_[i]->srv : null_srv_);
                ones[i] = 1;
            }
            const D3D12_CPU_DESCRIPTOR_HANDLE dst = core_->view_cpu(at);
            const UINT count = kSrvSlots;
            core_->device->CopyDescriptors(1, &dst, &count, kSrvSlots, src, ones, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            table_cache_[h] = at;
        }
        list_->SetGraphicsRootDescriptorTable(kCbvSlots, core_->view_gpu(at));
        table_set_ = h;
    }

    // Samplers: a table a combination, kept for good.
    std::array<u32, kSamplerSlots> skey{};
    u64 sh = 1469598103934665603ull;
    for (u32 i = 0; i < kSamplerSlots; ++i) {
        skey[i] = samp_[i] ? samp_[i]->serial : 0;
        sh = (sh ^ skey[i]) * 1099511628211ull;
    }
    if (sh != samplers_set_) {
        u32 at;
        if (auto it = sampler_tables_.find(skey); it != sampler_tables_.end()) {
            at = it->second;
        } else {
            at = sampler_next_;
            sampler_next_ = std::min<u32>(sampler_next_ + kSamplerSlots, kSamplerHeap - kSamplerSlots);
            const SIZE_T base = core_->samplers->GetCPUDescriptorHandleForHeapStart().ptr;
            for (u32 i = 0; i < kSamplerSlots; ++i) {
                D3D12_SAMPLER_DESC sd{};
                if (samp_[i]) sd = samp_[i]->d;
                else {
                    sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    sd.MaxLOD = D3D12_FLOAT32_MAX;
                    sd.MaxAnisotropy = 1;
                }
                core_->device->CreateSampler(&sd, {base + SIZE_T(at + i) * core_->sampler_step});
            }
            sampler_tables_[skey] = at;
        }
        list_->SetGraphicsRootDescriptorTable(kCbvSlots + 1,
                                              {core_->samplers->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(at) * core_->sampler_step});
        samplers_set_ = sh;
    }

    // Vertices and indices.
    D3D12_VERTEX_BUFFER_VIEW vb{};
    if (vb_) {
        vb.BufferLocation = address(*vb_);
        vb.SizeInBytes = UINT(vb_->bytes());
        vb.StrideInBytes = vb_stride_;
    }
    if (std::memcmp(&vb, &vb_set_, sizeof(vb)) != 0) {
        list_->IASetVertexBuffers(0, vb_ ? 1 : 0, vb_ ? &vb : nullptr);
        vb_set_ = vb;
    }
    const D3D12_PRIMITIVE_TOPOLOGY topo = topology(t);
    if (topo != topo_set_) {
        list_->IASetPrimitiveTopology(topo);
        topo_set_ = topo;
    }
}

void Device12::draw(Topology t, u32 count, u32 first) {
    if (!program_ || !blend_ || !raster_ || !depth_state_ || lost_) return;
    prepare(t);
    if (!pso_set_) return;
    list_->DrawInstanced(count, 1, first, 0);
}

void Device12::draw_indexed(Topology t, u32 count, u32 first) {
    if (!program_ || !blend_ || !raster_ || !depth_state_ || !ib_ || lost_) return;
    prepare(t);
    if (!pso_set_) return;
    D3D12_INDEX_BUFFER_VIEW ib{address(*ib_), UINT(ib_->bytes()), DXGI_FORMAT_R32_UINT};
    if (std::memcmp(&ib, &ib_set_, sizeof(ib)) != 0) {
        list_->IASetIndexBuffer(&ib);
        ib_set_ = ib;
    }
    list_->DrawIndexedInstanced(count, 1, first, 0, 0);
}

void Device12::resolve(Texture& dst, const Texture& src) {
    auto& d = static_cast<Texture12&>(dst);
    auto& s = const_cast<Texture12&>(static_cast<const Texture12&>(src));
    transition(s, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition(d, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    flush_barriers();
    list_->ResolveSubresource(d.res.Get(), 0, s.res.Get(), 0, dxgi(src.desc().format).srv);
}

bool Device12::ui_init() {
    VanGui_ImplDX12_InitInfo info;
    info.Device = core_->device.Get();
    info.NumFramesInFlight = int(kFrames);
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    info.SrvDescriptorHeap = core_->views.Get();
    info.UserData = core_.get();
    info.SrvDescriptorAllocFn = [](VanGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* c = static_cast<Core*>(i->UserData);
        const u32 slot = c->ui_alloc();
        *cpu = c->view_cpu(slot);
        *gpu = c->view_gpu(slot);
    };
    info.SrvDescriptorFreeFn = [](VanGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto* c = static_cast<Core*>(i->UserData);
        const u32 slot = u32((cpu.ptr - c->views->GetCPUDescriptorHandleForHeapStart().ptr) / c->view_step);
        c->bury(nullptr, slot);
    };
    ui_ = VanGui_ImplDX12_Init(&info);
    return ui_;
}

void Device12::ui_shutdown() {
    if (!ui_) return;
    submit_and_wait();
    VanGui_ImplDX12_Shutdown();
    ui_ = false;
}

void Device12::ui_render() {
    apply_targets();
    flush_barriers();
    ID3D12DescriptorHeap* heaps[] = {core_->views.Get(), core_->samplers.Get()};
    list_->SetDescriptorHeaps(2, heaps);
    VanGui_ImplDX12_RenderDrawData(VanGui::GetDrawData(), list_.Get());
    // VanGUI set its own pipeline, root signature and buffers.
    invalidate();
    targets_set_ = true;
}

bool Device12::video_memory(size_t& used, size_t& budget) const {
    ComPtr<IDXGIAdapter3> a3;
    if (!adapter_ || FAILED(adapter_.As(&a3))) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;
    used = size_t(info.CurrentUsage);
    budget = size_t(info.Budget);
    return true;
}

}  // namespace

std::unique_ptr<Device> make_d3d12() { return std::make_unique<Device12>(); }

bool d3d12_available() {
    // A card and driver that can make a Direct3D 12 device (feature level 11.0 or better).
    ComPtr<IDXGIFactory4> f;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; SUCCEEDED(f->EnumAdapters1(i, &a)); ++i) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) return true;
    }
    return false;
}

}  // namespace eng
