// The graphics device on Direct3D 11 (Windows).
#include "Engine/Render/Device.hpp"

#include "Engine/Core/Log.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vangui/vangui.h>
#include <vangui_impl_dx11.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace eng {

namespace {

using Microsoft::WRL::ComPtr;

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

// What the debug layer said since the last frame, into the log (each message a few times at most).
void drain(ID3D11InfoQueue* q) {
    static std::vector<std::pair<int, int>> seen;   // message id, times logged
    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T size = 0;
        if (FAILED(q->GetMessage(i, nullptr, &size)) || size == 0) continue;
        std::vector<char> buf(size);
        auto* m = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (FAILED(q->GetMessage(i, m, &size))) continue;
        auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& s) { return s.first == int(m->ID); });
        if (it == seen.end()) it = seen.insert(seen.end(), {int(m->ID), 0});
        if (++it->second > 3) continue;
        if (m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) LOG_ERROR("D3D11 debug: %.*s", int(m->DescriptionByteLength), m->pDescription);
        else LOG_WARN("D3D11 debug: %.*s", int(m->DescriptionByteLength), m->pDescription);
    }
    q->ClearStoredMessages();
}

// The texture's own format, and the formats its views read and write it as.
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

class Texture11 final : public Texture {
public:
    Texture11(const TextureDesc& d) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    u64 ui_id() const override { return u64(reinterpret_cast<uintptr_t>(srv.Get())); }
    void describe(const TextureDesc& d) {
        desc_ = d;
        bytes_ = texture_bytes(d);
    }
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    std::vector<ComPtr<ID3D11RenderTargetView>> rtv;   // one a layer
    std::vector<ComPtr<ID3D11DepthStencilView>> dsv;   // one a layer
};

class Buffer11 final : public Buffer {
public:
    Buffer11(BufferKind k, size_t n) {
        kind_ = k;
        bytes_ = n;
    }
    ComPtr<ID3D11Buffer> buf;
};

class Program11 final : public Program {
public:
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
};

class Blend11 final : public BlendState {
public:
    ComPtr<ID3D11BlendState> s;
};
class Raster11 final : public RasterState {
public:
    ComPtr<ID3D11RasterizerState> s;
};
class Depth11 final : public DepthState {
public:
    ComPtr<ID3D11DepthStencilState> s;
};
class Sampler11 final : public Sampler {
public:
    ComPtr<ID3D11SamplerState> s;
};

class Timer11 final : public TimerFrame {
public:
    Timer11(ID3D11Device* d, ID3D11DeviceContext* c, int n) : ctx_(c) {
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        d->CreateQuery(&qd, &disjoint_);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        stamps_.resize(size_t(n));
        for (auto& s : stamps_) d->CreateQuery(&qd, &s);
    }
    bool ok() const {
        if (!disjoint_) return false;
        for (const auto& s : stamps_)
            if (!s) return false;
        return true;
    }
    void begin() override {
        ctx_->Begin(disjoint_.Get());
        used_ = 0;
    }
    void stamp(int i) override {
        if (i < 0 || i >= int(stamps_.size())) return;
        ctx_->End(stamps_[size_t(i)].Get());
        used_ = std::max(used_, i + 1);
    }
    void end() override { ctx_->End(disjoint_.Get()); }
    bool poll(double* ms, int count, bool& valid) override {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx_->GetData(disjoint_.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return false;
        count = std::min(count, used_);
        std::vector<UINT64> t(size_t(std::max(count, 1)), 0);
        for (int i = 0; i < count; ++i)
            if (ctx_->GetData(stamps_[size_t(i)].Get(), &t[size_t(i)], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return false;
        valid = !dj.Disjoint && dj.Frequency != 0;
        const double to_ms = dj.Frequency ? 1000.0 / double(dj.Frequency) : 0.0;
        for (int i = 0; i < count; ++i) ms[i] = double(t[size_t(i)] - t[0]) * to_ms;
        return true;
    }

private:
    ID3D11DeviceContext* ctx_;
    ComPtr<ID3D11Query> disjoint_;
    std::vector<ComPtr<ID3D11Query>> stamps_;
    int used_ = 0;
};

D3D11_BLEND factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return D3D11_BLEND_ZERO;
        case BlendFactor::One: return D3D11_BLEND_ONE;
        case BlendFactor::SrcColour: return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DestColour: return D3D11_BLEND_DEST_COLOR;
    }
    return D3D11_BLEND_ONE;
}
D3D11_BLEND_OP op(BlendOp o) { return o == BlendOp::RevSubtract ? D3D11_BLEND_OP_REV_SUBTRACT : D3D11_BLEND_OP_ADD; }

D3D11_PRIMITIVE_TOPOLOGY topology(Topology t) {
    switch (t) {
        case Topology::Triangles: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case Topology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case Topology::Lines: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    }
    return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

class Device11 final : public Device {
public:
    ~Device11() override { destroy(); }
    Api api() const override { return Api::D3D11; }

    bool create(void* hwnd, int width, int height, const Options& o) override;
    void destroy() override;
    bool resize(int width, int height) override;
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
    void set_program(const Program* program) override;
    void set_blend(const BlendState* state) override;
    void set_raster(const RasterState* state) override;
    void set_depth(const DepthState* state) override;
    void set_uniforms(int slot, const Buffer* buffer) override;
    void set_texture(int slot, const Texture* texture) override;
    void set_sampler(int slot, const Sampler* sampler) override;
    void set_vertices(const Buffer* buffer, u32 stride) override;
    void set_indices(const Buffer* buffer) override;
    void draw(Topology t, u32 count, u32 first) override;
    void draw_indexed(Topology t, u32 count, u32 first) override;
    void resolve(Texture& dst, const Texture& src) override;

    bool ui_init() override { return ui_ = VanGui_ImplDX11_Init(device_.Get(), context_.Get()); }
    void ui_shutdown() override {
        if (ui_) VanGui_ImplDX11_Shutdown();
        ui_ = false;
    }
    void ui_new_frame() override { VanGui_ImplDX11_NewFrame(); }
    void ui_render() override {
        VanGui_ImplDX11_RenderDrawData(VanGui::GetDrawData());
        forget();
    }

    std::unique_ptr<TimerFrame> create_timer_frame(int stamps) override {
        auto t = std::make_unique<Timer11>(device_.Get(), context_.Get(), stamps);
        return t->ok() ? std::move(t) : nullptr;
    }
    bool video_memory(size_t& used, size_t& budget) const override;

private:
    bool create_window_targets();
    void release_window_targets();
    // Forget what the pipeline was known to hold (something else set it).
    void forget() {
        program_ = nullptr;
        blend_ = nullptr;
        raster_ = nullptr;
        depth_state_ = nullptr;
        std::fill(std::begin(srv_), std::end(srv_), nullptr);
    }
    bool compile(const ShaderSource& src, const char* entry, const char* target, ComPtr<ID3DBlob>& out, std::string* error);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<IDXGIAdapter> adapter_;
    ComPtr<ID3D11InfoQueue> info_queue_;
    Texture11 back_{TextureDesc{}}, back_depth_{TextureDesc{TextureType::Plain, Format::D24S8}};
    bool tearing_ = false;
    bool ui_ = false;

    // The states made, by description.
    std::vector<std::pair<BlendDesc, std::unique_ptr<Blend11>>> blends_;
    std::vector<std::pair<RasterDesc, std::unique_ptr<Raster11>>> rasters_;
    std::vector<std::pair<DepthDesc, std::unique_ptr<Depth11>>> depths_;
    std::vector<std::pair<SamplerDesc, std::unique_ptr<Sampler11>>> samplers_;
    // Compiled shader stages, by source, entry and target (a pass's shared vertex shader once).
    std::map<std::tuple<const char*, std::string, std::string>, ComPtr<ID3DBlob>> blobs_;

    // What the pipeline holds.
    const Program* program_ = nullptr;
    const BlendState* blend_ = nullptr;
    const RasterState* raster_ = nullptr;
    const DepthState* depth_state_ = nullptr;
    const Texture* srv_[16]{};
};

bool Device11::create(void* hwnd, int width, int height, const Options& o) {
    vsync_ = o.vsync;
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    bool debug_layer = o.debug;
    const bool warp = o.warp;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    debug_layer = true;
#endif
    if (debug_layer) flags |= D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    // The fastest card, asked for by name: on a laptop with two, the default may be the slower
    // (the integrated one beside an AMD or NVIDIA card).
    ComPtr<IDXGIAdapter1> chosen;
    if (!warp) {
        ComPtr<IDXGIFactory6> f6;
        if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&f6))))
            f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&chosen));
    }
    auto make = [&](UINT f) {
        if (warp)
            return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, f, levels, UINT(std::size(levels)), D3D11_SDK_VERSION, &device_,
                                     nullptr, &context_);
        HRESULT r = E_FAIL;
        if (chosen)
            r = D3D11CreateDevice(chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, f, levels, UINT(std::size(levels)), D3D11_SDK_VERSION,
                                  &device_, nullptr, &context_);
        if (FAILED(r))
            r = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, f, levels, UINT(std::size(levels)), D3D11_SDK_VERSION, &device_,
                                  nullptr, &context_);
        return r;
    };
    HRESULT hr = make(flags);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        LOG_WARN("The D3D11 debug layer is not installed (Windows' Graphics Tools); going on without it");
        flags &= ~UINT(D3D11_CREATE_DEVICE_DEBUG);
        hr = make(flags);
    }
    if (FAILED(hr) && !warp) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, UINT(std::size(levels)), D3D11_SDK_VERSION, &device_,
                               nullptr, &context_);
        if (SUCCEEDED(hr)) LOG_WARN("No hardware Direct3D 11: using the WARP software renderer");
    }
    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDevice failed (0x%08x)", unsigned(hr));
        return false;
    }
    if (warp) LOG_INFO("Using the WARP software renderer (--warp)");
    if ((flags & D3D11_CREATE_DEVICE_DEBUG) && SUCCEEDED(device_.As(&info_queue_))) {
        // Only what matters: warnings and errors.
        D3D11_MESSAGE_SEVERITY deny[] = {D3D11_MESSAGE_SEVERITY_INFO, D3D11_MESSAGE_SEVERITY_MESSAGE};
        D3D11_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumSeverities = UINT(std::size(deny));
        filter.DenyList.pSeverityList = deny;
        // The scene shader's motion and surface outputs with nothing bound (temporal AA and
        // reflections off): writes to an unbound target are discarded, as intended.
        D3D11_MESSAGE_ID ids[] = {D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET};
        filter.DenyList.NumIDs = UINT(std::size(ids));
        filter.DenyList.pIDList = ids;
        info_queue_->AddStorageFilterEntries(&filter);
        LOG_INFO("D3D11 debug layer on: its warnings and errors go to this log");
    }

    ComPtr<IDXGIDevice> dxgi_device;
    device_.As(&dxgi_device);
    dxgi_device->GetAdapter(&adapter_);
    ComPtr<IDXGIFactory2> factory;
    adapter_->GetParent(IID_PPV_ARGS(&factory));
    DXGI_ADAPTER_DESC ad{};
    adapter_->GetDesc(&ad);
    char name[128];
    WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name), nullptr, nullptr);
    adapter_name_ = name;
    adapter_memory_ = ad.DedicatedVideoMemory;
    // The card, its maker and its driver: what a report from a player's machine needs first.
    LARGE_INTEGER umd{};
    char driver[48] = "unknown";
    if (SUCCEEDED(adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
        std::snprintf(driver, sizeof(driver), "%u.%u.%u.%u", unsigned(HIWORD(umd.HighPart)), unsigned(LOWORD(umd.HighPart)),
                      unsigned(HIWORD(umd.LowPart)), unsigned(LOWORD(umd.LowPart)));
    vendor_ = ad.VendorId;
    LOG_INFO("GPU: %s (%s, vendor %04x device %04x), %llu MB, driver %s, feature level %x", name, vendor_name(ad.VendorId), unsigned(ad.VendorId),
             unsigned(ad.DeviceId), (unsigned long long)(ad.DedicatedVideoMemory / (1024 * 1024)), driver, unsigned(device_->GetFeatureLevel()));

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) tearing_ = allow != FALSE;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = UINT(width_);
    sd.Height = UINT(height_);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    hr = factory->CreateSwapChainForHwnd(device_.Get(), HWND(hwnd), &sd, nullptr, nullptr, &swap_);
    if (FAILED(hr)) {
        LOG_ERROR("CreateSwapChainForHwnd failed (0x%08x)", unsigned(hr));
        return false;
    }
    factory->MakeWindowAssociation(HWND(hwnd), DXGI_MWA_NO_ALT_ENTER);
    return create_window_targets();
}

bool Device11::create_window_targets() {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    back_.rtv.assign(1, {});
    if (FAILED(device_->CreateRenderTargetView(back.Get(), nullptr, &back_.rtv[0]))) return false;
    // The window's targets are described by its size, for any pass that asks.
    TextureDesc bd;
    bd.width = width_;
    bd.height = height_;
    bd.target = true;
    back_.describe(bd);

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = UINT(width_);
    dd.Height = UINT(height_);
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, &back_depth_.tex))) return false;
    back_depth_.dsv.assign(1, {});
    bd.format = Format::D24S8;
    back_depth_.describe(bd);
    return SUCCEEDED(device_->CreateDepthStencilView(back_depth_.tex.Get(), nullptr, &back_depth_.dsv[0]));
}

void Device11::release_window_targets() {
    if (context_) context_->OMSetRenderTargets(0, nullptr, nullptr);
    targets_ = {};
    back_.rtv.clear();
    back_depth_.dsv.clear();
    back_depth_.tex.Reset();
}

bool Device11::resize(int width, int height) {
    if (!swap_ || width <= 0 || height <= 0) return false;
    if (width == width_ && height == height_) return true;
    release_window_targets();
    width_ = width;
    height_ = height;
    const HRESULT hr = swap_->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        LOG_ERROR("ResizeBuffers failed (0x%08x)", unsigned(hr));
        return false;
    }
    return create_window_targets();
}

void Device11::destroy() {
    release_window_targets();
    swap_.Reset();
    blends_.clear();
    rasters_.clear();
    depths_.clear();
    samplers_.clear();
    blobs_.clear();
    forget();
    if (context_) context_->ClearState();
    info_queue_.Reset();
    adapter_.Reset();
    context_.Reset();
    device_.Reset();
}

void Device11::begin_frame(const Color& clear_colour) {
    forget();
    const Target colour{&back_};
    set_targets(std::span<const Target>(&colour, 1), Target{&back_depth_});
    clear(colour, clear_colour);
    clear_depth(Target{&back_depth_}, 1.0f);
    set_viewport({0, 0, float(width_), float(height_)});
}

void Device11::present() {
    if (!swap_ || lost_) return;
    const UINT flags = (!vsync_ && tearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = swap_->Present(vsync_ ? 1 : 0, flags);
    if (info_queue_) drain(info_queue_.Get());
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        // The driver reset the card (a hang, a driver crash, a driver update): nothing more
        // draws. Why, as the driver tells it, for the report.
        lost_ = true;
        const HRESULT why = device_ ? device_->GetDeviceRemovedReason() : hr;
        LOG_ERROR("The graphics device was lost (Present 0x%08x, reason 0x%08x)", unsigned(hr), unsigned(why));
    }
}

bool Device11::capture(Image& out) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context_->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    out.width = desc.Width;
    out.height = desc.Height;
    out.rgba.resize(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const u8* src = static_cast<const u8*>(m.pData) + size_t(y) * m.RowPitch;
        u8* dst = out.rgba.data() + size_t(y) * desc.Width * 4;
        std::memcpy(dst, src, size_t(desc.Width) * 4);
        for (UINT x = 0; x < desc.Width; ++x) dst[x * 4 + 3] = 255;
    }
    context_->Unmap(staging.Get(), 0);
    return true;
}

TextureRef Device11::create_texture(const TextureDesc& d, std::span<const TextureData> init) {
    auto t = std::make_shared<Texture11>(d);
    const Formats f = dxgi(d.format);
    const bool depth = is_depth(d.format);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(std::max(1, d.width));
    td.Height = UINT(std::max(1, d.height));
    td.MipLevels = UINT(std::max(1, d.mips));
    td.ArraySize = UINT(d.type == TextureType::Cube ? 6 : std::max(1, d.layers));
    td.Format = f.resource;
    td.SampleDesc.Count = UINT(std::max(1, d.samples));
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (d.target) td.BindFlags |= depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
    if (d.type == TextureType::Cube) td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (d.dynamic) {
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    }
    std::vector<D3D11_SUBRESOURCE_DATA> subs;
    for (const TextureData& s : init) subs.push_back({s.data, s.row_pitch, 0});
    if (!subs.empty() && subs.size() != size_t(td.MipLevels) * td.ArraySize) return nullptr;
    if (FAILED(device_->CreateTexture2D(&td, subs.empty() ? nullptr : subs.data(), &t->tex))) return nullptr;

    const bool ms = td.SampleDesc.Count > 1;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = f.srv;
    if (d.type == TextureType::Cube) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sv.TextureCube.MipLevels = td.MipLevels;
    } else if (d.type == TextureType::Array) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Texture2DArray.MipLevels = td.MipLevels;
        sv.Texture2DArray.ArraySize = td.ArraySize;
    } else if (ms) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    } else {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MipLevels = td.MipLevels;
    }
    if (FAILED(device_->CreateShaderResourceView(t->tex.Get(), &sv, &t->srv))) return nullptr;

    if (d.target) {
        const bool layered = d.type != TextureType::Plain;
        for (UINT layer = 0; layer < td.ArraySize; ++layer) {
            if (depth) {
                D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
                dv.Format = f.target;
                if (layered) {
                    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                    dv.Texture2DArray.FirstArraySlice = layer;
                    dv.Texture2DArray.ArraySize = 1;
                } else {
                    dv.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
                }
                ComPtr<ID3D11DepthStencilView> v;
                if (FAILED(device_->CreateDepthStencilView(t->tex.Get(), &dv, &v))) return nullptr;
                t->dsv.push_back(v);
            } else {
                D3D11_RENDER_TARGET_VIEW_DESC rv{};
                rv.Format = f.target;
                if (layered) {
                    rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                    rv.Texture2DArray.FirstArraySlice = layer;
                    rv.Texture2DArray.ArraySize = 1;
                } else {
                    rv.ViewDimension = ms ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
                }
                ComPtr<ID3D11RenderTargetView> v;
                if (FAILED(device_->CreateRenderTargetView(t->tex.Get(), &rv, &v))) return nullptr;
                t->rtv.push_back(v);
            }
        }
    }
    return t;
}

void Device11::update_texture(Texture& texture, const void* data, u32 row_pitch) {
    auto& t = static_cast<Texture11&>(texture);
    if (!t.tex || !t.desc().dynamic) return;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context_->Map(t.tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    const size_t row = size_t(t.width()) * texel_bytes(t.desc().format);
    for (int y = 0; y < t.height(); ++y)
        std::memcpy(static_cast<u8*>(m.pData) + size_t(y) * m.RowPitch, static_cast<const u8*>(data) + size_t(y) * row_pitch, row);
    context_->Unmap(t.tex.Get(), 0);
}

BufferRef Device11::create_buffer(BufferKind kind, const void* data, size_t bytes, bool dynamic) {
    auto b = std::make_shared<Buffer11>(kind, bytes);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = UINT(bytes);
    bd.BindFlags = kind == BufferKind::Vertex ? D3D11_BIND_VERTEX_BUFFER : kind == BufferKind::Index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA init{data, 0, 0};
    if (FAILED(device_->CreateBuffer(&bd, data ? &init : nullptr, &b->buf))) return nullptr;
    return b;
}

void Device11::update_buffer(Buffer& buffer, const void* data, size_t bytes) {
    auto& b = static_cast<Buffer11&>(buffer);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(context_->Map(b.buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, data, std::min(bytes, b.bytes()));
        context_->Unmap(b.buf.Get(), 0);
    }
}

bool Device11::compile(const ShaderSource& src, const char* entry, const char* target, ComPtr<ID3DBlob>& out, std::string* error) {
    const auto key = std::make_tuple(src.hlsl, std::string(entry), std::string(target));
    if (auto it = blobs_.find(key); it != blobs_.end()) {
        out = it->second;
        return true;
    }
    ComPtr<ID3DBlob> errors;
    // The compiler recurses deeply on a big shader (its optimiser overflowed the main thread's
    // stack on the scene shader once): it runs on a thread of its own with room.
    struct Job {
        const char *source, *entry, *target;
        ComPtr<ID3DBlob>* blob;
        ComPtr<ID3DBlob>* errors;
        HRESULT hr = E_FAIL;
    } job{src.hlsl, entry, target, &out, &errors};
    auto run = [](void* p) -> DWORD {
        auto& j = *static_cast<Job*>(p);
        j.hr = D3DCompile(j.source, std::strlen(j.source), j.entry, nullptr, nullptr, j.entry, j.target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                          j.blob->ReleaseAndGetAddressOf(), j.errors->ReleaseAndGetAddressOf());
        return 0;
    };
    if (HANDLE t = CreateThread(nullptr, 64u << 20, run, &job, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    } else {
        run(&job);
    }
    if (FAILED(job.hr)) {
        const std::string msg =
            errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "D3DCompile failed";
        LOG_ERROR("Shader %s %s: %s", src.name, entry, msg.c_str());
        if (error) *error = msg;
        return false;
    }
    blobs_[key] = out;
    return true;
}

ProgramRef Device11::create_program(const ShaderSource& source, const char* vs_entry, const char* ps_entry, std::span<const VertexAttr> layout,
                                    std::string* error) {
    ComPtr<ID3DBlob> vs, ps;
    if (!compile(source, vs_entry, "vs_5_0", vs, error) || !compile(source, ps_entry, "ps_5_0", ps, error)) return nullptr;
    auto p = std::make_shared<Program11>();
    if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &p->vs))) return nullptr;
    if (FAILED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &p->ps))) return nullptr;
    if (!layout.empty()) {
        std::vector<D3D11_INPUT_ELEMENT_DESC> el;
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
            el.push_back({names[int(a.semantic)], UINT(a.index), f, 0, a.offset, D3D11_INPUT_PER_VERTEX_DATA, 0});
        }
        if (FAILED(device_->CreateInputLayout(el.data(), UINT(el.size()), vs->GetBufferPointer(), vs->GetBufferSize(), &p->layout))) return nullptr;
    }
    return p;
}

int Device11::samples_for(Format format, int want) {
    const DXGI_FORMAT f = dxgi(format).resource;
    int n = std::max(1, want);
    UINT quality = 0;
    while (n > 1 && (FAILED(device_->CheckMultisampleQualityLevels(f, UINT(n), &quality)) || quality == 0)) n /= 2;
    return n;
}

const BlendState* Device11::blend(const BlendDesc& d) {
    for (auto& [k, v] : blends_)
        if (k == d) return v.get();
    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = d.alpha_to_coverage;
    bd.IndependentBlendEnable = TRUE;
    for (int k = 0; k < 4; ++k) {
        auto& t = bd.RenderTarget[k];
        t.RenderTargetWriteMask = (d.write >> k) & 1 ? D3D11_COLOR_WRITE_ENABLE_ALL : 0;
        if (k > 0) continue;
        t.BlendEnable = d.enable;
        t.SrcBlend = factor(d.src);
        t.DestBlend = factor(d.dst);
        t.BlendOp = op(d.op);
        t.SrcBlendAlpha = factor(d.src_alpha);
        t.DestBlendAlpha = factor(d.dst_alpha);
        t.BlendOpAlpha = op(d.op_alpha);
    }
    auto s = std::make_unique<Blend11>();
    device_->CreateBlendState(&bd, &s->s);
    blends_.emplace_back(d, std::move(s));
    return blends_.back().second.get();
}

const RasterState* Device11::raster(const RasterDesc& d) {
    for (auto& [k, v] : rasters_)
        if (k == d) return v.get();
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = d.wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    rs.CullMode = d.cull == Cull::None ? D3D11_CULL_NONE : d.cull == Cull::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
    rs.FrontCounterClockwise = FALSE;   // left-handed data, clockwise front faces
    rs.SlopeScaledDepthBias = d.slope_bias;
    rs.DepthBiasClamp = d.bias_clamp;
    rs.DepthClipEnable = TRUE;
    auto s = std::make_unique<Raster11>();
    device_->CreateRasterizerState(&rs, &s->s);
    rasters_.emplace_back(d, std::move(s));
    return rasters_.back().second.get();
}

const DepthState* Device11::depth(const DepthDesc& d) {
    for (auto& [k, v] : depths_)
        if (k == d) return v.get();
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = d.test;
    ds.DepthWriteMask = d.write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    auto s = std::make_unique<Depth11>();
    device_->CreateDepthStencilState(&ds, &s->s);
    depths_.emplace_back(d, std::move(s));
    return depths_.back().second.get();
}

const Sampler* Device11::sampler(const SamplerDesc& d) {
    for (auto& [k, v] : samplers_)
        if (k == d) return v.get();
    D3D11_SAMPLER_DESC sd{};
    if (d.compare) sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    else if (d.filter == Filter::Anisotropic) sd.Filter = D3D11_FILTER_ANISOTROPIC;
    else if (d.filter == Filter::Point) sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    else sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.MaxAnisotropy = std::clamp<UINT>(d.anisotropy, 1, 16);
    const D3D11_TEXTURE_ADDRESS_MODE a = d.address == Address::Wrap ? D3D11_TEXTURE_ADDRESS_WRAP
                                         : d.address == Address::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP
                                                                       : D3D11_TEXTURE_ADDRESS_BORDER;
    sd.AddressU = sd.AddressV = sd.AddressW = a;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1;
    sd.ComparisonFunc = d.compare ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    auto s = std::make_unique<Sampler11>();
    device_->CreateSamplerState(&sd, &s->s);
    samplers_.emplace_back(d, std::move(s));
    return samplers_.back().second.get();
}

void Device11::set_targets(std::span<const Target> colour, Target depth) {
    ID3D11RenderTargetView* rtv[4]{};
    targets_ = {};
    const int n = std::min<int>(int(colour.size()), 4);
    for (int i = 0; i < n; ++i) {
        targets_.colour[i] = colour[size_t(i)];
        auto* t = static_cast<Texture11*>(colour[size_t(i)].texture);
        if (t && size_t(colour[size_t(i)].layer) < t->rtv.size()) rtv[i] = t->rtv[size_t(colour[size_t(i)].layer)].Get();
    }
    targets_.count = n;
    targets_.depth = depth;
    ID3D11DepthStencilView* dsv = nullptr;
    if (auto* t = static_cast<Texture11*>(depth.texture); t && size_t(depth.layer) < t->dsv.size()) dsv = t->dsv[size_t(depth.layer)].Get();
    // A texture about to be drawn into is let go of as an input first (Direct3D would refuse it).
    for (int s = 0; s < 16; ++s) {
        if (!srv_[s]) continue;
        bool hit = depth.texture == srv_[s];
        for (int i = 0; i < n; ++i) hit |= colour[size_t(i)].texture == srv_[s];
        if (hit) {
            ID3D11ShaderResourceView* none = nullptr;
            context_->PSSetShaderResources(UINT(s), 1, &none);
            srv_[s] = nullptr;
        }
    }
    context_->OMSetRenderTargets(UINT(n), rtv, dsv);
}

void Device11::set_viewport(const Viewport& v) {
    viewport_ = v;
    const D3D11_VIEWPORT vp{v.x, v.y, v.w, v.h, 0, 1};
    context_->RSSetViewports(1, &vp);
}

void Device11::clear(Target target, const Color& c) {
    auto* t = static_cast<Texture11*>(target.texture);
    if (!t || size_t(target.layer) >= t->rtv.size()) return;
    const float f[4] = {c.r, c.g, c.b, c.a};
    context_->ClearRenderTargetView(t->rtv[size_t(target.layer)].Get(), f);
}

void Device11::clear_depth(Target target, float value) {
    auto* t = static_cast<Texture11*>(target.texture);
    if (!t || size_t(target.layer) >= t->dsv.size()) return;
    const UINT flags = t->desc().format == Format::D24S8 ? D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL : D3D11_CLEAR_DEPTH;
    context_->ClearDepthStencilView(t->dsv[size_t(target.layer)].Get(), flags, value, 0);
}

void Device11::set_program(const Program* program) {
    if (program == program_) return;
    program_ = program;
    const auto* p = static_cast<const Program11*>(program);
    context_->IASetInputLayout(p ? p->layout.Get() : nullptr);
    context_->VSSetShader(p ? p->vs.Get() : nullptr, nullptr, 0);
    context_->PSSetShader(p ? p->ps.Get() : nullptr, nullptr, 0);
}

void Device11::set_blend(const BlendState* state) {
    if (state == blend_) return;
    blend_ = state;
    const float f[4] = {1, 1, 1, 1};
    context_->OMSetBlendState(state ? static_cast<const Blend11*>(state)->s.Get() : nullptr, f, 0xFFFFFFFF);
}

void Device11::set_raster(const RasterState* state) {
    if (state == raster_) return;
    raster_ = state;
    context_->RSSetState(state ? static_cast<const Raster11*>(state)->s.Get() : nullptr);
}

void Device11::set_depth(const DepthState* state) {
    if (state == depth_state_) return;
    depth_state_ = state;
    context_->OMSetDepthStencilState(state ? static_cast<const Depth11*>(state)->s.Get() : nullptr, 0);
}

void Device11::set_uniforms(int slot, const Buffer* buffer) {
    ID3D11Buffer* b = buffer ? static_cast<const Buffer11*>(buffer)->buf.Get() : nullptr;
    context_->VSSetConstantBuffers(UINT(slot), 1, &b);
    context_->PSSetConstantBuffers(UINT(slot), 1, &b);
}

void Device11::set_texture(int slot, const Texture* texture) {
    ID3D11ShaderResourceView* v = texture ? static_cast<const Texture11*>(texture)->srv.Get() : nullptr;
    context_->PSSetShaderResources(UINT(slot), 1, &v);
    if (slot >= 0 && slot < 16) srv_[slot] = texture;
}

void Device11::set_sampler(int slot, const Sampler* sampler) {
    ID3D11SamplerState* s = sampler ? static_cast<const Sampler11*>(sampler)->s.Get() : nullptr;
    context_->PSSetSamplers(UINT(slot), 1, &s);
}

void Device11::set_vertices(const Buffer* buffer, u32 stride) {
    ID3D11Buffer* b = buffer ? static_cast<const Buffer11*>(buffer)->buf.Get() : nullptr;
    const UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, &b, &stride, &offset);
}

void Device11::set_indices(const Buffer* buffer) {
    context_->IASetIndexBuffer(buffer ? static_cast<const Buffer11*>(buffer)->buf.Get() : nullptr, DXGI_FORMAT_R32_UINT, 0);
}

void Device11::draw(Topology t, u32 count, u32 first) {
    context_->IASetPrimitiveTopology(topology(t));
    context_->Draw(count, first);
}

void Device11::draw_indexed(Topology t, u32 count, u32 first) {
    context_->IASetPrimitiveTopology(topology(t));
    context_->DrawIndexed(count, first, 0);
}

void Device11::resolve(Texture& dst, const Texture& src) {
    auto& d = static_cast<Texture11&>(dst);
    const auto& s = static_cast<const Texture11&>(src);
    context_->ResolveSubresource(d.tex.Get(), 0, s.tex.Get(), 0, dxgi(src.desc().format).srv);
}

bool Device11::video_memory(size_t& used, size_t& budget) const {
    ComPtr<IDXGIAdapter3> a3;
    if (!adapter_ || FAILED(adapter_.As(&a3))) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;
    used = size_t(info.CurrentUsage);
    budget = size_t(info.Budget);
    return true;
}

}  // namespace

std::unique_ptr<Device> make_gles();
std::unique_ptr<Device> make_d3d12();
bool d3d12_available();

std::unique_ptr<Device> Device::make(Api api) {
#ifdef ENG_GLES_ON_WINDOWS
    if (api == Api::GLES) return make_gles();
#endif
    if (api == Api::D3D11) return std::make_unique<Device11>();
    if (api == Api::D3D12 || d3d12_available()) return make_d3d12();
    LOG_WARN("Direct3D 12 is not available on this card or driver: drawing with Direct3D 11");
    return std::make_unique<Device11>();
}

}  // namespace eng
