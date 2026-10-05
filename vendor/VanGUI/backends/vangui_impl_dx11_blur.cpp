// VanGUI: backdrop blur for the DirectX 11 renderer backend. See vangui_impl_dx11_blur.h.
//
// Runs inside VanGui_ImplDX11_RenderDrawData(), at the point in the draw list where
// DrawBackdropBlur() queued it: everything drawn before it is in the render target, so
// that is what gets blurred. It leaves its own pipeline bound; the ResetRenderState
// callback queued right after it restores the backend's, and the render target is put
// back here before returning.

#include "vangui_impl_dx11_blur.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_dx11.h"

#if __has_include("misc/vangui_effects.h")
#include "misc/vangui_effects.h"
#else
#include "vangui_effects.h"
#endif

#ifdef VANGUI_ENABLE_EFFECTS

#include "vangui_impl_blur_common.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler")
#endif

namespace {

struct Params
{
    float Step[4];      // xy: one texel along the pass (uv); zw: down-pass tap offset (uv)
    float Scale[4];     // xy: output uv -> source uv; zw: largest source uv to sample
    float Rect[4];      // panel, framebuffer px (x0, y0, x1, y1)
    float Region[4];    // copied region's origin (framebuffer px); zw: framebuffer px -> blurred uv
    float Misc[4];      // x: rounding px, y: saturation, z: taps
    float Taps[VanBlur::kMaxTaps][4];   // x: offset (texels), y: weight
};

struct Target
{
    ID3D11Texture2D*          Tex = nullptr;
    ID3D11ShaderResourceView* Srv = nullptr;
    ID3D11RenderTargetView*   Rtv = nullptr;
    int                       W = 0, H = 0;
    DXGI_FORMAT               Format = DXGI_FORMAT_UNKNOWN;

    void Release()
    {
        if (Rtv) { Rtv->Release(); Rtv = nullptr; }
        if (Srv) { Srv->Release(); Srv = nullptr; }
        if (Tex) { Tex->Release(); Tex = nullptr; }
        W = H = 0;
        Format = DXGI_FORMAT_UNKNOWN;
    }
};

struct BlurData
{
    ID3D11Device*            Device = nullptr;
    ID3D11VertexShader*      VS = nullptr;
    ID3D11PixelShader*       PSDown = nullptr;
    ID3D11PixelShader*       PSBlur = nullptr;
    ID3D11PixelShader*       PSComposite = nullptr;
    ID3D11Buffer*            CB = nullptr;
    ID3D11SamplerState*      Sampler = nullptr;
    ID3D11BlendState*        BlendOpaque = nullptr;
    ID3D11BlendState*        BlendOver = nullptr;
    ID3D11RasterizerState*   Raster = nullptr;
    ID3D11DepthStencilState* NoDepth = nullptr;
    Target                   Source;    // the copied region, render-target format
    Target                   Resolve;   // whole multisampled target, resolved
    Target                   A, B;      // downsampled ping-pong, RGBA16F
};
BlurData* g_Blur = nullptr;

const char* kShaders = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
Texture2D    tex0  : register(t0);
SamplerState samp0 : register(s0);
cbuffer Params : register(b0)
{
    float4 g_Step;
    float4 g_Scale;
    float4 g_Rect;
    float4 g_Region;
    float4 g_Misc;
    float4 g_Taps[12];
};
float4 Fetch(float2 uv) { return tex0.Sample(samp0, min(uv, g_Scale.zw)); }
float4 PSDown(VSOut i) : SV_TARGET
{
    float2 uv = i.uv * g_Scale.xy, d = g_Step.zw;
    return 0.25 * (Fetch(uv + float2(-d.x, -d.y)) + Fetch(uv + float2(d.x, -d.y)) +
                   Fetch(uv + float2(-d.x,  d.y)) + Fetch(uv + float2(d.x,  d.y)));
}
float4 PSBlur(VSOut i) : SV_TARGET
{
    float2 uv = i.uv * g_Scale.xy;
    float4 c = Fetch(uv) * g_Taps[0].y;
    int n = (int)g_Misc.z;
    [unroll] for (int k = 1; k < 12; ++k)
    {
        if (k < n)
        {
            float2 o = g_Step.xy * g_Taps[k].x;
            c += (Fetch(uv + o) + Fetch(uv - o)) * g_Taps[k].y;
        }
    }
    return c;
}
float4 PSComposite(VSOut i) : SV_TARGET
{
    float2 p  = i.pos.xy;
    float2 uv = (p - g_Region.xy) * g_Region.zw;   // framebuffer px -> blurred texture uv
    float3 c  = Fetch(uv).rgb;
    float  l  = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(float3(l, l, l), c, g_Misc.y);
    float2 halfsz = (g_Rect.zw - g_Rect.xy) * 0.5;
    float2 centre = (g_Rect.xy + g_Rect.zw) * 0.5;
    float  r = min(g_Misc.x, min(halfsz.x, halfsz.y));
    float2 q = abs(p - centre) - (halfsz - r);
    float  d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    return float4(c, saturate(0.5 - d));
}
)";

template <typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool Compile(const char* entry, const char* profile, ID3DBlob** out)
{
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kShaders, strlen(kShaders), "vangui_blur", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (errors) errors->Release();
    return SUCCEEDED(hr);
}

bool PixelShader(const char* entry, ID3D11PixelShader** out)
{
    ID3DBlob* blob = nullptr;
    if (!Compile(entry, "ps_4_0", &blob)) return false;
    const HRESULT hr = g_Blur->Device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out);
    blob->Release();
    return SUCCEEDED(hr);
}

// Grow `t` to at least w x h (never shrinks, so a steady UI stops allocating).
bool Ensure(Target& t, int w, int h, DXGI_FORMAT resource_format, DXGI_FORMAT view_format, bool render_target)
{
    if (t.Tex && t.W >= w && t.H >= h && t.Format == resource_format) return true;
    const int nw = (t.Format == resource_format && t.W > w) ? t.W : w;
    const int nh = (t.Format == resource_format && t.H > h) ? t.H : h;
    t.Release();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)nw;
    desc.Height = (UINT)nh;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = resource_format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (render_target ? D3D11_BIND_RENDER_TARGET : 0);
    if (FAILED(g_Blur->Device->CreateTexture2D(&desc, nullptr, &t.Tex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = view_format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    if (FAILED(g_Blur->Device->CreateShaderResourceView(t.Tex, &srv, &t.Srv))) { t.Release(); return false; }
    if (render_target && FAILED(g_Blur->Device->CreateRenderTargetView(t.Tex, nullptr, &t.Rtv))) { t.Release(); return false; }
    t.W = nw;
    t.H = nh;
    t.Format = resource_format;
    return true;
}

DXGI_FORMAT TypedFormat(DXGI_FORMAT resource, DXGI_FORMAT view)
{
    return view != DXGI_FORMAT_UNKNOWN ? view : resource;
}

void Upload(ID3D11DeviceContext* ctx, const Params& p)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx->Map(g_Blur->CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        memcpy(m.pData, &p, sizeof(p));
        ctx->Unmap(g_Blur->CB, 0);
    }
}

void Pass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* src, ID3D11PixelShader* ps,
          float vw, float vh, const D3D11_RECT& scissor)
{
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);   // the source may have been this pass's target a moment ago
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width = vw;
    vp.Height = vh;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetScissorRects(1, &scissor);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &src);
    ctx->Draw(3, 0);
}

void BlurHandler(const VanDrawList*, const VanDrawCmd*, const VanGui::VanBlurRequest& req, void*)
{
    BlurData* bd = g_Blur;
    const auto* rs = static_cast<const VanGui_ImplDX11_RenderState*>(VanGui::GetPlatformIO().Renderer_RenderState);
    const VanDrawData* dd = VanGui::GetDrawData();
    if (!bd || !rs || !dd) return;
    ID3D11DeviceContext* ctx = rs->DeviceContext;

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    if (!rtv) { if (dsv) dsv->Release(); return; }
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    ID3D11Texture2D* target = nullptr;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
    rtv->GetDesc(&rtv_desc);
    if (res) res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&target);
    if (res) res->Release();

    // Framebuffer pixels of the panel, its clip and the region to copy.
    const float sx = dd->FramebufferScale.x, sy = dd->FramebufferScale.y;
    const float px0 = (req.Rect.x - dd->DisplayPos.x) * sx, py0 = (req.Rect.y - dd->DisplayPos.y) * sy;
    const float px1 = (req.Rect.z - dd->DisplayPos.x) * sx, py1 = (req.Rect.w - dd->DisplayPos.y) * sy;
    const float sigma = VanBlur::SigmaFor(req.Radius * sx);
    D3D11_TEXTURE2D_DESC td = {};
    VanBlur::Region rg;
    bool ok = target != nullptr;
    if (ok) target->GetDesc(&td);
    ok = ok && VanBlur::RegionFor(px0, py0, px1, py1, VanBlur::Margin(sigma), (int)td.Width, (int)td.Height, &rg);
    D3D11_RECT clip = {};
    if (ok)
    {
        clip.left   = (LONG)((req.ClipRect.x - dd->DisplayPos.x) * sx);
        clip.top    = (LONG)((req.ClipRect.y - dd->DisplayPos.y) * sy);
        clip.right  = (LONG)((req.ClipRect.z - dd->DisplayPos.x) * sx);
        clip.bottom = (LONG)((req.ClipRect.w - dd->DisplayPos.y) * sy);
        if (clip.left < (LONG)px0) clip.left = (LONG)px0;
        if (clip.top < (LONG)py0) clip.top = (LONG)py0;
        if (clip.right > (LONG)(px1 + 1.0f)) clip.right = (LONG)(px1 + 1.0f);
        if (clip.bottom > (LONG)(py1 + 1.0f)) clip.bottom = (LONG)(py1 + 1.0f);
        ok = clip.right > clip.left && clip.bottom > clip.top;
    }

    const DXGI_FORMAT view_format = TypedFormat(td.Format, rtv_desc.Format);
    const int rw = ok ? rg.X1 - rg.X0 : 0, rh = ok ? rg.Y1 - rg.Y0 : 0;
    const int f = VanBlur::Downsample(sigma);
    const int dw = (rw + f - 1) / f, dh = (rh + f - 1) / f;
    ok = ok && Ensure(bd->Source, rw, rh, td.Format, view_format, false)
            && Ensure(bd->A, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, true)
            && Ensure(bd->B, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if (ok && td.SampleDesc.Count > 1)
        ok = Ensure(bd->Resolve, (int)td.Width, (int)td.Height, td.Format, view_format, false);

    if (ok)
    {
        // Copy the region out (resolving a multisampled target first).
        ID3D11Resource* from = target;
        if (td.SampleDesc.Count > 1)
        {
            ctx->ResolveSubresource(bd->Resolve.Tex, 0, target, 0, view_format);
            from = bd->Resolve.Tex;
        }
        D3D11_BOX box = { (UINT)rg.X0, (UINT)rg.Y0, 0, (UINT)rg.X1, (UINT)rg.Y1, 1 };
        ctx->CopySubresourceRegion(bd->Source.Tex, 0, 0, 0, 0, from, 0, &box);

        // Our pipeline. The backend's comes back with the ResetRenderState that follows.
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(bd->VS, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &bd->CB);
        ctx->PSSetSamplers(0, 1, &bd->Sampler);
        ctx->RSSetState(bd->Raster);
        ctx->OMSetDepthStencilState(bd->NoDepth, 0);
        const float blend_factor[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(bd->BlendOpaque, blend_factor, 0xffffffff);

        const VanBlur::Kernel k = VanBlur::MakeKernel(sigma / float(f));
        Params p = {};
        p.Misc[0] = req.Rounding * sx;
        p.Misc[1] = req.Saturation;
        p.Misc[2] = float(k.Taps);
        for (int i = 0; i < k.Taps; ++i) { p.Taps[i][0] = k.Offsets[i]; p.Taps[i][1] = k.Weights[i]; }
        const D3D11_RECT low = {0, 0, dw, dh};

        // Down: region -> A at 1/f. Each A texel averages its f x f block (four bilinear taps).
        p.Step[2] = f > 1 ? float(f) * 0.25f / float(bd->Source.W) : 0.0f;
        p.Step[3] = f > 1 ? float(f) * 0.25f / float(bd->Source.H) : 0.0f;
        p.Scale[0] = float(f * dw) / float(bd->Source.W);
        p.Scale[1] = float(f * dh) / float(bd->Source.H);
        p.Scale[2] = (float(rw) - 0.5f) / float(bd->Source.W);
        p.Scale[3] = (float(rh) - 0.5f) / float(bd->Source.H);
        Upload(ctx, p);
        Pass(ctx, bd->A.Rtv, bd->Source.Srv, bd->PSDown, float(dw), float(dh), low);

        // Blur: A -> B across, B -> A down.
        p.Step[2] = p.Step[3] = 0.0f;
        p.Scale[0] = float(dw) / float(bd->A.W);
        p.Scale[1] = float(dh) / float(bd->A.H);
        p.Scale[2] = (float(dw) - 0.5f) / float(bd->A.W);
        p.Scale[3] = (float(dh) - 0.5f) / float(bd->A.H);
        p.Step[0] = 1.0f / float(bd->A.W);
        p.Step[1] = 0.0f;
        Upload(ctx, p);
        Pass(ctx, bd->B.Rtv, bd->A.Srv, bd->PSBlur, float(dw), float(dh), low);
        p.Step[0] = 0.0f;
        p.Step[1] = 1.0f / float(bd->A.H);
        Upload(ctx, p);
        Pass(ctx, bd->A.Rtv, bd->B.Srv, bd->PSBlur, float(dw), float(dh), low);

        // Composite A inside the panel's rounded rectangle, on the original target.
        p.Rect[0] = px0; p.Rect[1] = py0; p.Rect[2] = px1; p.Rect[3] = py1;
        p.Region[0] = float(rg.X0); p.Region[1] = float(rg.Y0);
        p.Region[2] = 1.0f / (float(f) * float(bd->A.W)); p.Region[3] = 1.0f / (float(f) * float(bd->A.H));
        Upload(ctx, p);
        ctx->OMSetBlendState(bd->BlendOver, blend_factor, 0xffffffff);
        Pass(ctx, rtv, bd->A.Srv, bd->PSComposite, float(td.Width), float(td.Height), clip);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);
    }

    ctx->OMSetRenderTargets(1, &rtv, dsv);
    rtv->Release();
    if (dsv) dsv->Release();
    if (target) target->Release();
}

} // namespace

bool VanGui_ImplDX11_InitBlur(ID3D11Device* device)
{
    if (!device) return false;
    VanGui_ImplDX11_ShutdownBlur();
    g_Blur = new BlurData();
    g_Blur->Device = device;
    device->AddRef();

    ID3DBlob* vs = nullptr;
    bool ok = Compile("VS", "vs_4_0", &vs) && SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_Blur->VS));
    if (vs) vs->Release();
    ok = ok && PixelShader("PSDown", &g_Blur->PSDown) && PixelShader("PSBlur", &g_Blur->PSBlur) && PixelShader("PSComposite", &g_Blur->PSComposite);

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = (sizeof(Params) + 15) & ~15u;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(device->CreateBuffer(&cb, nullptr, &g_Blur->CB));

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(device->CreateSamplerState(&sd, &g_Blur->Sampler));

    D3D11_BLEND_DESC bdesc = {};
    bdesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bdesc, &g_Blur->BlendOpaque));
    bdesc.RenderTarget[0].BlendEnable = TRUE;
    bdesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bdesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bdesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bdesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bdesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bdesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bdesc, &g_Blur->BlendOver));

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(device->CreateRasterizerState(&rd, &g_Blur->Raster));

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(device->CreateDepthStencilState(&dsd, &g_Blur->NoDepth));

    if (!ok) { VanGui_ImplDX11_ShutdownBlur(); return false; }
    VanGui::SetBlurHandler(BlurHandler, nullptr);
    return true;
}

void VanGui_ImplDX11_ShutdownBlur()
{
    if (!g_Blur) return;
    VanGui::SetBlurHandler(nullptr, nullptr);
    g_Blur->Source.Release();
    g_Blur->Resolve.Release();
    g_Blur->A.Release();
    g_Blur->B.Release();
    SafeRelease(g_Blur->VS);
    SafeRelease(g_Blur->PSDown);
    SafeRelease(g_Blur->PSBlur);
    SafeRelease(g_Blur->PSComposite);
    SafeRelease(g_Blur->CB);
    SafeRelease(g_Blur->Sampler);
    SafeRelease(g_Blur->BlendOpaque);
    SafeRelease(g_Blur->BlendOver);
    SafeRelease(g_Blur->Raster);
    SafeRelease(g_Blur->NoDepth);
    SafeRelease(g_Blur->Device);
    delete g_Blur;
    g_Blur = nullptr;
}

#else // VANGUI_ENABLE_EFFECTS

bool VanGui_ImplDX11_InitBlur(ID3D11Device*) { return false; }
void VanGui_ImplDX11_ShutdownBlur() {}

#endif // VANGUI_ENABLE_EFFECTS
#endif // #ifndef VANGUI_DISABLE
