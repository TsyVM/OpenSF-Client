// VanGUI: Renderer Backend for DirectX12

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_dx12.h"

// DirectX
#include <stdio.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib
#endif

// The backdrop blur is compiled in only alongside the effects module that queues it.
#if __has_include("misc/vangui_effects.h")
#include "misc/vangui_effects.h"
#else
#include "vangui_effects.h"
#endif
#ifdef VANGUI_ENABLE_EFFECTS
#include "vangui_impl_blur_common.h"
#endif

// VanGUI progress (this is a DX12 VanGUI backend)
struct VanGui_ImplDX12_RenderBuffers;
struct VanGui_ImplDX12_Data;
struct VanGui_ImplDX12_Blur;
static void VanGui_ImplDX12_DestroyTexture(VanTextureData* tex);
static bool VanGui_ImplDX12_CreateBlurObjects(VanGui_ImplDX12_Data* bd);
static void VanGui_ImplDX12_ReleaseBlurObjects(VanGui_ImplDX12_Data* bd, bool deferred);

// Per-frame resources (one set per frame-in-flight)
struct VanGui_ImplDX12_RenderBuffers
{
    ID3D12Resource*     IndexBuffer         = nullptr;
    ID3D12Resource*     VertexBuffer        = nullptr;
    int                 IndexBufferSize     = 0;
    int                 VertexBufferSize    = 0;
};

// Deferred-release entry for upload buffers used during texture creation
struct VanGui_ImplDX12_Texture
{
    ID3D12Resource*     pTexture            = nullptr;    // GPU texture (DEFAULT heap)
    ID3D12Resource*     pUploadBuffer       = nullptr;    // temporary UPLOAD heap buffer
    int                 UploadFrameIndex    = -1;         // frame when upload was submitted

    // The SRV slot this texture's view lives in. Owned when it came from
    // SrvDescriptorAllocFn, and handed back on destroy; borrowed (and not
    // freed) when the single-slot legacy path supplied the font's handles.
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle = {};
    bool                OwnsDescriptor      = false;
};

// One thing waiting for the GPU to be done with it. `resource` is released and,
// when `ownsDescriptor` is set, the SRV slot is handed back to the allocator --
// both only after a full lap of the frame ring, by which time the command list
// that referenced them has retired. A device child rather than a resource so the
// blur's pipeline states and root signature can wait here too.
struct VanGui_ImplDX12_Retired
{
    ID3D12DeviceChild*          resource        = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle  = {};
    D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle  = {};
    bool                        ownsDescriptor  = false;
};

// Main backend data
struct VanGui_ImplDX12_Data
{
    ID3D12Device*                   pd3dDevice              = nullptr;
    ID3D12RootSignature*            pRootSignature          = nullptr;
    ID3D12PipelineState*            pPipelineState          = nullptr;
    DXGI_FORMAT                     RTVFormat               = (DXGI_FORMAT)0;
    ID3D12DescriptorHeap*           pd3dSrvDescHeap         = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE     hFontSrvCpuDescHandle   = {};
    D3D12_GPU_DESCRIPTOR_HANDLE     hFontSrvGpuDescHandle   = {};

    // A copy, so the caller's struct need not outlive Init. The callbacks are
    // handed a pointer to this copy, which is why UserData must still be the
    // caller's own pointer and must outlive the backend.
    VanGui_ImplDX12_InitInfo        InitInfo                = {};
    int                             numFramesInFlight       = 0;
    int                             frameIndex              = INT_MAX; // starts before first frame
    VanGui_ImplDX12_RenderBuffers*  pFrameResources         = nullptr;

    // Things that cannot be released until the GPU has finished with them: the
    // staging buffers a texture upload copies from, and textures themselves
    // once VanGUI has asked for them to go.
    //
    // A LIST per frame slot, not one pointer. It used to be one, on the theory
    // that a frame uploads at most one texture -- true for as long as the only
    // texture in the process was a font atlas built once. It stopped being true
    // the moment anything else existed: a second upload in the same frame
    // released the first one's staging buffer while the open command list was
    // still going to read it, and a GPU reading freed upload memory is a
    // device-removed crash on some drivers and nothing at all on others, which
    // is exactly how it survived testing.
    //
    // Textures are deferred the same way, with their descriptor, because an
    // in-flight draw still names both.
    VanVector<VanGui_ImplDX12_Retired>* pRetired = nullptr; // array[numFramesInFlight]

    // Valid only inside RenderDrawData, for draw callbacks (the blur) that need
    // to record commands of their own.
    ID3D12GraphicsCommandList*      pCmdList                = nullptr;
    VanDrawData*                    pDrawData               = nullptr;

    // The render target named by SetRenderTarget for this frame; forgotten when
    // RenderDrawData returns.
    ID3D12Resource*                 pTarget                 = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE     hTargetRtv              = {};
    D3D12_CPU_DESCRIPTOR_HANDLE     hTargetDsv              = {};
    bool                            HasTargetDsv            = false;

    VanGui_ImplDX12_Blur*           pBlur                   = nullptr; // set by InitBlur
};

static VanGui_ImplDX12_Data* VanGui_ImplDX12_GetBackendData()
{
    return VanGui::GetCurrentContext() ? (VanGui_ImplDX12_Data*)VanGui::GetIO().BackendRendererUserData : nullptr;
}

// Hands back everything parked in one frame slot. Called when that slot comes
// round again -- a full lap later, so the command list that used any of it has
// long since retired -- and on shutdown, where the caller has already idled the
// device.
static void VanGui_ImplDX12_DrainRetired(VanGui_ImplDX12_Data* bd, int slot)
{
    if (!bd || !bd->pRetired) return;

    VanVector<VanGui_ImplDX12_Retired>& list = bd->pRetired[slot];
    for (VanGui_ImplDX12_Retired& r : list)
    {
        if (r.resource)
            r.resource->Release();

        // The descriptor goes back only now. Freeing it with the texture would
        // let the slot be reissued and overwritten while an in-flight draw was
        // still naming that GPU handle -- which shows the wrong texture at
        // best, and reads a descriptor for a released resource at worst.
        if (r.ownsDescriptor && bd->InitInfo.SrvDescriptorFreeFn)
            bd->InitInfo.SrvDescriptorFreeFn(&bd->InitInfo,
                                             r.hCpuDescHandle, r.hGpuDescHandle);
    }
    list.clear();
}

// Parks a resource until the GPU is done with it.
static void VanGui_ImplDX12_Retire(VanGui_ImplDX12_Data* bd, ID3D12DeviceChild* res)
{
    if (!bd || !res || !bd->pRetired) { if (res) res->Release(); return; }

    const int slot = (bd->frameIndex == INT_MAX)
                   ? 0
                   : (bd->frameIndex % bd->numFramesInFlight);

    VanGui_ImplDX12_Retired r;
    r.resource = res;
    bd->pRetired[slot].push_back(r);
}

// Intentionally empty: its address is what the draw loop looks for.
static void VanGui_ImplDX12_DrawCallback_ResetRenderState(const VanDrawList*, const VanDrawCmd*) {}

static const char* vertexShaderHlsl =
    "cbuffer vertexBuffer : register(b0) \
    {\
      float4x4 ProjectionMatrix; \
    };\
    struct VS_INPUT\
    {\
      float2 pos : POSITION;\
      float2 uv  : TEXCOORD0;\
      float4 col : COLOR0;\
    };\
    \
    struct PS_INPUT\
    {\
      float4 pos : SV_POSITION;\
      float4 col : COLOR0;\
      float2 uv  : TEXCOORD0;\
    };\
    \
    PS_INPUT main(VS_INPUT input)\
    {\
      PS_INPUT output;\
      output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
      output.col = input.col;\
      output.uv  = input.uv;\
      return output;\
    }";

static const char* pixelShaderHlsl =
    "struct PS_INPUT\
    {\
      float4 pos : SV_POSITION;\
      float4 col : COLOR0;\
      float2 uv  : TEXCOORD0;\
    };\
    SamplerState sampler0 : register(s0);\
    Texture2D texture0 : register(t0);\
    \
    float4 main(PS_INPUT input) : SV_Target\
    {\
      float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
      return out_col; \
    }";

bool VanGui_ImplDX12_CreateDeviceObjects()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd || !bd->pd3dDevice)
        return false;
    if (bd->pPipelineState)
        VanGui_ImplDX12_InvalidateDeviceObjects();

    // Compile vertex shader
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    if (FAILED(D3DCompile(vertexShaderHlsl, strlen(vertexShaderHlsl), nullptr, nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &vs_blob, &error_blob)))
    {
        if (error_blob)
        {
            OutputDebugStringA((const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        return false;
    }
    if (error_blob) { error_blob->Release(); error_blob = nullptr; }

    // Compile pixel shader
    ID3DBlob* ps_blob = nullptr;
    if (FAILED(D3DCompile(pixelShaderHlsl, strlen(pixelShaderHlsl), nullptr, nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &ps_blob, &error_blob)))
    {
        if (error_blob)
        {
            OutputDebugStringA((const char*)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        vs_blob->Release();
        return false;
    }
    if (error_blob) { error_blob->Release(); error_blob = nullptr; }

    // Create root signature
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType                          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                     = 1;
        srvRange.BaseShaderRegister                 = 0;
        srvRange.RegisterSpace                      = 0;
        srvRange.OffsetInDescriptorsFromTableStart  = 0;

        D3D12_ROOT_PARAMETER rootParams[2] = {};

        // Param[0]: 16 root 32-bit constants (ProjectionMatrix) at b0, VERTEX visibility
        rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister  = 0;
        rootParams[0].Constants.RegisterSpace   = 0;
        rootParams[0].Constants.Num32BitValues  = 16;
        rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

        // Param[1]: Descriptor table with 1 SRV range (t0), PIXEL visibility
        rootParams[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        rootParams[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        // Static sampler: LINEAR filter, CLAMP addressing
        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.MipLODBias       = 0.0f;
        staticSampler.MaxAnisotropy    = 0;
        staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        staticSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSampler.MinLOD           = 0.0f;
        staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
        staticSampler.ShaderRegister   = 0;
        staticSampler.RegisterSpace    = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters        = 2;
        rsDesc.pParameters          = rootParams;
        rsDesc.NumStaticSamplers    = 1;
        rsDesc.pStaticSamplers      = &staticSampler;
        rsDesc.Flags                =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS       |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS     |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ID3DBlob* rs_blob = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &error_blob)))
        {
            if (error_blob)
            {
                OutputDebugStringA((const char*)error_blob->GetBufferPointer());
                error_blob->Release();
            }
            vs_blob->Release();
            ps_blob->Release();
            return false;
        }
        if (error_blob) { error_blob->Release(); error_blob = nullptr; }

        HRESULT hr = bd->pd3dDevice->CreateRootSignature(0,
            rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
            IID_PPV_ARGS(&bd->pRootSignature));
        rs_blob->Release();
        if (FAILED(hr))
        {
            vs_blob->Release();
            ps_blob->Release();
            return false;
        }
    }

    // Create PSO
    {
        D3D12_INPUT_ELEMENT_DESC inputLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, (UINT)offsetof(VanDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, (UINT)offsetof(VanDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, (UINT)offsetof(VanDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature          = bd->pRootSignature;
        psoDesc.VS                      = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
        psoDesc.PS                      = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };
        psoDesc.InputLayout             = { inputLayout, 3 };
        psoDesc.PrimitiveTopologyType   = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets        = 1;
        psoDesc.RTVFormats[0]           = bd->RTVFormat;
        psoDesc.SampleDesc.Count        = 1;

        // Every sample enabled. This is not a default: psoDesc is zero
        // initialised, and a SampleMask of 0 masks out every sample. The
        // pipeline state still creates, the draws are still recorded, and not
        // one pixel is ever written -- so the UI builds its geometry, submits
        // it, and is invisible, with nothing anywhere reporting an error.
        psoDesc.SampleMask              = 0xFFFFFFFFu;

        // Rasterizer
        // Note: D3D12_RASTERIZER_DESC has no ScissorEnable — scissor testing in D3D12 is
        // always active when RSSetScissorRects has been called. No field to set here.
        psoDesc.RasterizerState.FillMode                = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode                = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise   = TRUE;
        psoDesc.RasterizerState.DepthBias               = D3D12_DEFAULT_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthBiasClamp          = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        psoDesc.RasterizerState.SlopeScaledDepthBias    = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthClipEnable         = TRUE;
        psoDesc.RasterizerState.MultisampleEnable       = FALSE;
        psoDesc.RasterizerState.AntialiasedLineEnable   = FALSE;
        psoDesc.RasterizerState.ForcedSampleCount       = 0;
        psoDesc.RasterizerState.ConservativeRaster      = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // Blend state for RenderTarget[0]
        D3D12_RENDER_TARGET_BLEND_DESC& rtBlend = psoDesc.BlendState.RenderTarget[0];
        rtBlend.BlendEnable             = TRUE;
        rtBlend.LogicOpEnable           = FALSE;
        rtBlend.SrcBlend                = D3D12_BLEND_SRC_ALPHA;
        rtBlend.DestBlend               = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOp                 = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha           = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha          = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOpAlpha            = D3D12_BLEND_OP_ADD;
        rtBlend.RenderTargetWriteMask   = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Depth stencil
        psoDesc.DepthStencilState.DepthEnable       = FALSE;
        psoDesc.DepthStencilState.DepthWriteMask    = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc         = D3D12_COMPARISON_FUNC_ALWAYS;
        psoDesc.DepthStencilState.StencilEnable     = FALSE;

        HRESULT hr = bd->pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&bd->pPipelineState));
        vs_blob->Release();
        ps_blob->Release();
        if (FAILED(hr))
            return false;
    }

    // After a device reset the blur comes back with everything else. A failure
    // here costs only the blur (its handler checks), not the UI.
    if (bd->pBlur)
        VanGui_ImplDX12_CreateBlurObjects(bd);

    return true;
}

void VanGui_ImplDX12_InvalidateDeviceObjects()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd)
        return;

    if (bd->pRootSignature) { bd->pRootSignature->Release(); bd->pRootSignature = nullptr; }
    if (bd->pPipelineState) { bd->pPipelineState->Release(); bd->pPipelineState = nullptr; }

    // Release per-frame vertex/index buffers
    if (bd->pFrameResources)
    {
        for (int i = 0; i < bd->numFramesInFlight; i++)
        {
            VanGui_ImplDX12_RenderBuffers* fr = &bd->pFrameResources[i];
            if (fr->VertexBuffer) { fr->VertexBuffer->Release(); fr->VertexBuffer = nullptr; }
            if (fr->IndexBuffer)  { fr->IndexBuffer->Release();  fr->IndexBuffer  = nullptr; }
            fr->VertexBufferSize = 0;
            fr->IndexBufferSize  = 0;
        }
    }

    // Every texture the backend made, as the other backends do. Without this the
    // font atlas and its SRV slot outlived Shutdown, and after a device reset
    // VanGUI kept drawing with a texture from the old device. They go through
    // the retire ring, which is drained just below.
    for (VanTextureData* tex : VanGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            VanGui_ImplDX12_DestroyTexture(tex);

    // The device is idle here, so the blur's objects can go at once.
    if (bd->pBlur)
        VanGui_ImplDX12_ReleaseBlurObjects(bd, false);

    // Everything still waiting on the GPU. The caller has already made the
    // device idle by the time this runs, so there is nothing left to wait for.
    if (bd->pRetired)
        for (int i = 0; i < bd->numFramesInFlight; i++)
            VanGui_ImplDX12_DrainRetired(bd, i);
}

bool VanGui_ImplDX12_Init(VanGui_ImplDX12_InitInfo* info)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    VAN_ASSERT(info != nullptr);
    VAN_ASSERT(info->Device != nullptr);
    VAN_ASSERT(info->NumFramesInFlight >= 1);
    VAN_ASSERT(info->SrvDescriptorHeap != nullptr);

    // An allocator without a matching free would leak a descriptor per texture
    // for the life of the process. Catch it here rather than as a heap that
    // slowly fills up months later.
    VAN_ASSERT((info->SrvDescriptorAllocFn == nullptr) == (info->SrvDescriptorFreeFn == nullptr)
               && "supply both SrvDescriptorAllocFn and SrvDescriptorFreeFn, or neither");

    VanGui_ImplDX12_Data* bd = VAN_NEW(VanGui_ImplDX12_Data)();
    io.BackendRendererUserData = bd;
    io.BackendRendererName = "vangui_impl_dx12";
    io.BackendFlags |= VanGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= VanGuiBackendFlags_RendererHasTextures;

    bd->InitInfo = *info;

    bd->pd3dDevice = info->Device;
    bd->RTVFormat = info->RTVFormat;
    bd->pd3dSrvDescHeap = info->SrvDescriptorHeap;
    bd->numFramesInFlight = info->NumFramesInFlight;
    bd->frameIndex = INT_MAX;

    bd->pFrameResources = new VanGui_ImplDX12_RenderBuffers[info->NumFramesInFlight];
    bd->pRetired        = new VanVector<VanGui_ImplDX12_Retired>[info->NumFramesInFlight];

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    platform_io.DrawCallback_ResetRenderState = VanGui_ImplDX12_DrawCallback_ResetRenderState;

    return true;
}

bool VanGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format,
    ID3D12DescriptorHeap* cbv_srv_heap,
    D3D12_CPU_DESCRIPTOR_HANDLE font_srv_cpu_desc_handle,
    D3D12_GPU_DESCRIPTOR_HANDLE font_srv_gpu_desc_handle)
{
    // The single-slot path: no allocator, so every texture is given the one
    // descriptor supplied here. Correct for a caller that only ever shows the
    // font, which is what this signature was written for.
    VanGui_ImplDX12_InitInfo info = {};
    info.Device            = device;
    info.NumFramesInFlight = num_frames_in_flight;
    info.RTVFormat         = rtv_format;
    info.SrvDescriptorHeap = cbv_srv_heap;

    if (!VanGui_ImplDX12_Init(&info))
        return false;

    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    bd->hFontSrvCpuDescHandle = font_srv_cpu_desc_handle;
    bd->hFontSrvGpuDescHandle = font_srv_gpu_desc_handle;
    return true;
}

void VanGui_ImplDX12_Shutdown()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    VanGuiIO& io = VanGui::GetIO();

    VanGui_ImplDX12_ShutdownBlur();
    VanGui_ImplDX12_InvalidateDeviceObjects();

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.DrawCallback_ResetRenderState = nullptr;

    delete[] bd->pFrameResources;
    bd->pFrameResources = nullptr;

    delete[] bd->pRetired;
    bd->pRetired = nullptr;

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_RendererHasVtxOffset | VanGuiBackendFlags_RendererHasTextures);

    VAN_DELETE(bd);
}

void VanGui_ImplDX12_NewFrame()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplDX12_Init()?");

    if (!bd->pPipelineState)
        VanGui_ImplDX12_CreateDeviceObjects();
}

static void VanGui_ImplDX12_CreateTexture(VanTextureData* tex, ID3D12GraphicsCommandList* cmd_list)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    VAN_ASSERT(tex->Format == VanTextureFormat_RGBA32);
    VAN_ASSERT(tex->BackendUserData == nullptr);

    VanGui_ImplDX12_Texture* backend_tex = new VanGui_ImplDX12_Texture();

    // Create GPU texture (DEFAULT heap)
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width               = (UINT)tex->Width;
    texDesc.Height              = (UINT)tex->Height;
    texDesc.DepthOrArraySize    = 1;
    texDesc.MipLevels           = 1;
    texDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count    = 1;
    texDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = bd->pd3dDevice->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&backend_tex->pTexture));
    if (!SUCCEEDED(hr)) { delete backend_tex; return; }

    // Create upload buffer
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    bd->pd3dDevice->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width                = uploadSize;
    uploadDesc.Height               = 1;
    uploadDesc.DepthOrArraySize     = 1;
    uploadDesc.MipLevels            = 1;
    uploadDesc.SampleDesc.Count     = 1;
    uploadDesc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = bd->pd3dDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&backend_tex->pUploadBuffer));
    if (!SUCCEEDED(hr)) { backend_tex->pTexture->Release(); delete backend_tex; return; }

    // Copy pixel data into upload buffer
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    backend_tex->pUploadBuffer->Map(0, &readRange, &mapped);
    const unsigned char* src = (const unsigned char*)tex->GetPixels();
    unsigned char* dst = (unsigned char*)mapped + footprint.Offset;
    for (UINT y = 0; y < (UINT)tex->Height; y++)
        memcpy(dst + y * footprint.Footprint.RowPitch,
               src + y * tex->Width * 4,
               tex->Width * 4);
    backend_tex->pUploadBuffer->Unmap(0, nullptr);

    // Issue copy command
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource        = backend_tex->pUploadBuffer;
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint  = footprint;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource        = backend_tex->pTexture;
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    cmd_list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition to SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                        = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource        = backend_tex->pTexture;
    barrier.Transition.Subresource      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore      = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter       = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd_list->ResourceBarrier(1, &barrier);

    // A descriptor of this texture's own when the caller gave us an allocator,
    // and the one shared font slot when it did not. The GPU handle becomes the
    // texture's id, which is what VanDrawCmd carries and what SetGraphicsRoot-
    // DescriptorTable is given at draw time.
    if (bd->InitInfo.SrvDescriptorAllocFn)
    {
        bd->InitInfo.SrvDescriptorAllocFn(&bd->InitInfo,
                                          &backend_tex->hCpuDescHandle,
                                          &backend_tex->hGpuDescHandle);
        backend_tex->OwnsDescriptor = true;
    }
    else
    {
        backend_tex->hCpuDescHandle = bd->hFontSrvCpuDescHandle;
        backend_tex->hGpuDescHandle = bd->hFontSrvGpuDescHandle;
        backend_tex->OwnsDescriptor = false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                      = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension               = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels         = 1;
    srvDesc.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    bd->pd3dDevice->CreateShaderResourceView(backend_tex->pTexture, &srvDesc,
                                             backend_tex->hCpuDescHandle);

    tex->SetTexID((VanTextureID)(intptr_t)backend_tex->hGpuDescHandle.ptr);
    tex->SetStatus(VanTextureStatus_OK);
    tex->BackendUserData = backend_tex;

    // The staging buffer is parked until this frame slot comes round again --
    // a full lap, by which time the copy has certainly happened.
    VanGui_ImplDX12_Retire(bd, backend_tex->pUploadBuffer);
    backend_tex->pUploadBuffer = nullptr;   // ownership transferred
}

// Re-uploads the part of a texture that changed, without recreating it.
//
// This backend advertised "[X] Renderer: Texture updates (WantCreate /
// WantUpdates / WantDestroy)" in its header and implemented two of the three.
// Nothing noticed while the only texture was a static bitmap font atlas built
// once at one size. VanGUI 1.92's font atlas is *dynamic*: it rasterises glyphs
// on demand and asks for an incremental upload through WantUpdates, and with
// that request dropped on the floor every glyph after the first batch stayed
// blank. The symptom is a UI with most of its text missing.
//
// The queued rectangles are covered by tex->UpdateRect, and one copy of that
// bounding box is both simpler and, for a glyph atlas, cheaper than a copy per
// rect -- the rects are typically adjacent cells of the same row.
static void VanGui_ImplDX12_UpdateTexture(VanTextureData* tex,
                                          ID3D12GraphicsCommandList* cmd_list)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    VanGui_ImplDX12_Texture* backend_tex = (VanGui_ImplDX12_Texture*)tex->BackendUserData;
    if (!bd || !backend_tex || !backend_tex->pTexture) return;

    const int x = tex->UpdateRect.x;
    const int y = tex->UpdateRect.y;
    const int w = tex->UpdateRect.w;
    const int h = tex->UpdateRect.h;
    if (w <= 0 || h <= 0) { tex->SetStatus(VanTextureStatus_OK); return; }

    // A staging buffer sized to the dirty box, with rows padded to D3D12's
    // 256-byte alignment.
    const UINT bpp      = (UINT)tex->BytesPerPixel;
    const UINT rowBytes = (UINT)w * bpp;
    const UINT rowPitch = (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                        & ~(UINT)(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 uploadSize = (UINT64)rowPitch * (UINT64)h;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width            = uploadSize;
    uploadDesc.Height           = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels        = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* upload = nullptr;
    if (FAILED(bd->pd3dDevice->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
        return;

    void* mapped = nullptr;
    D3D12_RANGE noRead = { 0, 0 };
    if (FAILED(upload->Map(0, &noRead, &mapped))) { upload->Release(); return; }

    for (int row = 0; row < h; ++row)
        memcpy((unsigned char*)mapped + (size_t)row * rowPitch,
               tex->GetPixelsAt(x, y + row), rowBytes);

    upload->Unmap(0, nullptr);

    D3D12_RESOURCE_BARRIER toCopy = {};
    toCopy.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource   = backend_tex->pTexture;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toCopy.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd_list->ResourceBarrier(1, &toCopy);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource                          = upload;
    src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset             = 0;
    src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width    = (UINT)w;
    src.PlacedFootprint.Footprint.Height   = (UINT)h;
    src.PlacedFootprint.Footprint.Depth    = 1;
    src.PlacedFootprint.Footprint.RowPitch = rowPitch;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = backend_tex->pTexture;
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    cmd_list->CopyTextureRegion(&dst, (UINT)x, (UINT)y, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER toRead = toCopy;
    toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toRead.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd_list->ResourceBarrier(1, &toRead);

    // Same deferred release the creation path uses.
    VanGui_ImplDX12_Retire(bd, upload);

    tex->SetStatus(VanTextureStatus_OK);
}

static void VanGui_ImplDX12_DestroyTexture(VanTextureData* tex)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();

    if (VanGui_ImplDX12_Texture* backend_tex = (VanGui_ImplDX12_Texture*)tex->BackendUserData)
    {
        // The texture and its descriptor go into the deferred ring rather than
        // being released here.
        //
        // VanGUI asks for a texture to be destroyed on the frame it stopped
        // using it, which is a frame the GPU has not finished -- and often has
        // not started. Releasing the resource there is a use-after-free the
        // moment the atlas grows, which a dynamic font atlas does whenever a
        // new glyph appears. Some drivers survive it; others remove the device.
        //
        // Only a descriptor this texture allocated is handed back. The legacy
        // path borrows the font's slot, and returning that to an allocator that
        // never issued it is how a heap ends up double-freeing one entry.
        if (bd && bd->pRetired)
        {
            const int slot = (bd->frameIndex == INT_MAX)
                           ? 0
                           : (bd->frameIndex % bd->numFramesInFlight);

            VanGui_ImplDX12_Retired r;
            r.resource       = backend_tex->pTexture;
            r.hCpuDescHandle = backend_tex->hCpuDescHandle;
            r.hGpuDescHandle = backend_tex->hGpuDescHandle;
            r.ownsDescriptor = backend_tex->OwnsDescriptor;
            bd->pRetired[slot].push_back(r);

            backend_tex->pTexture = nullptr;
        }
        else if (backend_tex->pTexture)
        {
            backend_tex->pTexture->Release();
        }

        if (backend_tex->pUploadBuffer) VanGui_ImplDX12_Retire(bd, backend_tex->pUploadBuffer);
        delete backend_tex;
        tex->SetTexID(VanTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(VanTextureStatus_Destroyed);
}

static void VanGui_ImplDX12_SetupRenderState(VanDrawData* draw_data, ID3D12GraphicsCommandList* cmd_list,
    VanGui_ImplDX12_RenderBuffers* fr, VanGui_ImplDX12_Data* bd)
{
    // Setup viewport
    D3D12_VIEWPORT vp = {};
    vp.Width    = draw_data->DisplaySize.x;
    vp.Height   = draw_data->DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    cmd_list->RSSetViewports(1, &vp);

    // Setup orthographic projection matrix
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    float mvp[4][4] =
    {
        { 2.0f/(R-L),    0.0f,         0.0f,  0.0f },
        { 0.0f,          2.0f/(T-B),   0.0f,  0.0f },
        { 0.0f,          0.0f,         0.5f,  0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.5f,  1.0f },
    };

    // Setup vertex buffers
    UINT stride = sizeof(VanDrawVert);
    UINT offset = 0;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation  = fr->VertexBuffer->GetGPUVirtualAddress();
    vbv.SizeInBytes     = (UINT)(fr->VertexBufferSize * sizeof(VanDrawVert));
    vbv.StrideInBytes   = stride;
    cmd_list->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation  = fr->IndexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes     = (UINT)(fr->IndexBufferSize * sizeof(VanDrawIdx));
    ibv.Format          = sizeof(VanDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    cmd_list->IASetIndexBuffer(&ibv);

    cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd_list->SetPipelineState(bd->pPipelineState);
    cmd_list->SetGraphicsRootSignature(bd->pRootSignature);
    cmd_list->SetDescriptorHeaps(1, &bd->pd3dSrvDescHeap);
    cmd_list->SetGraphicsRoot32BitConstants(0, 16, &mvp[0][0], 0);
}

void VanGui_ImplDX12_RenderDrawData(VanDrawData* draw_data, ID3D12GraphicsCommandList* cmd_list)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();

    // The target named for this frame, and the command list callbacks record
    // into, hold only until this returns -- by any path.
    struct FrameScope
    {
        VanGui_ImplDX12_Data* bd;
        ~FrameScope() { bd->pCmdList = nullptr; bd->pDrawData = nullptr; bd->pTarget = nullptr; bd->HasTargetDsv = false; }
    } frame_scope = { bd };

    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    // Advance frame index
    bd->frameIndex = (bd->frameIndex == INT_MAX) ? 0 : (bd->frameIndex + 1) % bd->numFramesInFlight;
    VanGui_ImplDX12_RenderBuffers* fr = &bd->pFrameResources[bd->frameIndex];

    // This slot has come round again, so everything parked in it a full lap
    // ago is finished with.
    VanGui_ImplDX12_DrainRetired(bd, bd->frameIndex);

    // Process pending textures
    if (draw_data->Textures != nullptr)
    {
        for (VanTextureData* tex : *draw_data->Textures)
        {
            if (tex->Status == VanTextureStatus_WantCreate)
                VanGui_ImplDX12_CreateTexture(tex, cmd_list);
            else if (tex->Status == VanTextureStatus_WantUpdates)
                VanGui_ImplDX12_UpdateTexture(tex, cmd_list);
            else if (tex->Status == VanTextureStatus_WantDestroy)
                VanGui_ImplDX12_DestroyTexture(tex);
        }
    }

    // Create and grow vertex buffer if needed
    if (!fr->VertexBuffer || fr->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (fr->VertexBuffer) { fr->VertexBuffer->Release(); fr->VertexBuffer = nullptr; }
        fr->VertexBufferSize = draw_data->TotalVtxCount + 5000;

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width            = (UINT64)(fr->VertexBufferSize * sizeof(VanDrawVert));
        vbDesc.Height           = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels        = 1;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(bd->pd3dDevice->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&fr->VertexBuffer))))
            return;
    }

    // Create and grow index buffer if needed
    if (!fr->IndexBuffer || fr->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (fr->IndexBuffer) { fr->IndexBuffer->Release(); fr->IndexBuffer = nullptr; }
        fr->IndexBufferSize = draw_data->TotalIdxCount + 10000;

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC ibDesc = {};
        ibDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        ibDesc.Width            = (UINT64)(fr->IndexBufferSize * sizeof(VanDrawIdx));
        ibDesc.Height           = 1;
        ibDesc.DepthOrArraySize = 1;
        ibDesc.MipLevels        = 1;
        ibDesc.SampleDesc.Count = 1;
        ibDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(bd->pd3dDevice->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&fr->IndexBuffer))))
            return;
    }

    // Map and copy vertex/index data
    {
        void* vtx_mapped = nullptr;
        void* idx_mapped = nullptr;
        D3D12_RANGE readRange = { 0, 0 };

        if (FAILED(fr->VertexBuffer->Map(0, &readRange, &vtx_mapped)))
            return;
        if (FAILED(fr->IndexBuffer->Map(0, &readRange, &idx_mapped)))
        {
            fr->VertexBuffer->Unmap(0, nullptr);
            return;
        }

        VanDrawVert* vtx_dst = (VanDrawVert*)vtx_mapped;
        VanDrawIdx*  idx_dst = (VanDrawIdx*)idx_mapped;
        for (const VanDrawList* draw_list : draw_data->CmdLists)
        {
            memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(VanDrawVert));
            memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(VanDrawIdx));
            vtx_dst += draw_list->VtxBuffer.Size;
            idx_dst += draw_list->IdxBuffer.Size;
        }

        D3D12_RANGE vtxWriteRange = { 0, (SIZE_T)(draw_data->TotalVtxCount * sizeof(VanDrawVert)) };
        D3D12_RANGE idxWriteRange = { 0, (SIZE_T)(draw_data->TotalIdxCount * sizeof(VanDrawIdx)) };
        fr->VertexBuffer->Unmap(0, &vtxWriteRange);
        fr->IndexBuffer->Unmap(0, &idxWriteRange);
    }

    // Setup render state
    bd->pCmdList = cmd_list;
    bd->pDrawData = draw_data;
    VanGui_ImplDX12_SetupRenderState(draw_data, cmd_list, fr, bd);

    // Draw loop
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    VanVec2 clip_off = draw_data->DisplayPos;

    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const VanDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // The standard callback, and the old sentinel value for code
                // written before it existed.
                if (pcmd->UserCallback == VanGui_ImplDX12_DrawCallback_ResetRenderState
                    || pcmd->UserCallback == (VanDrawCallback)(-8))
                    VanGui_ImplDX12_SetupRenderState(draw_data, cmd_list, fr, bd);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Apply scissor/clipping rectangle
                float clip_min_x = pcmd->ClipRect.x - clip_off.x;
                float clip_min_y = pcmd->ClipRect.y - clip_off.y;
                float clip_max_x = pcmd->ClipRect.z - clip_off.x;
                float clip_max_y = pcmd->ClipRect.w - clip_off.y;
                if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y)
                    continue;

                D3D12_RECT scissor = {
                    (LONG)clip_min_x, (LONG)clip_min_y,
                    (LONG)clip_max_x, (LONG)clip_max_y
                };
                cmd_list->RSSetScissorRects(1, &scissor);

                // Bind texture
                D3D12_GPU_DESCRIPTOR_HANDLE texture_handle = {};
                texture_handle.ptr = (UINT64)(intptr_t)pcmd->GetTexID();
                cmd_list->SetGraphicsRootDescriptorTable(1, texture_handle);

                // Draw
                cmd_list->DrawIndexedInstanced(
                    pcmd->ElemCount, 1,
                    pcmd->IdxOffset + global_idx_offset,
                    pcmd->VtxOffset + global_vtx_offset,
                    0);
            }
        }
        global_vtx_offset += draw_list->VtxBuffer.Size;
        global_idx_offset += draw_list->IdxBuffer.Size;
    }
}

//-----------------------------------------------------------------------------
// Backdrop blur
//-----------------------------------------------------------------------------
//
// The DX11 and OpenGL handlers live in files of their own because those APIs
// will say what render target is bound. D3D12 will not, and the blur needs the
// backend's command list, SRV allocator and retire ring besides, so it lives
// here. It runs as a draw callback at the point DrawBackdropBlur() queued it:
//
//   target --copy--> Source --down--> A --across--> B --down--> A --composite--> target
//
// Source is the panel's region (grown by 3 sigma) in the target's format; A and
// B are that region reduced by a power of two, RGBA16F. The composite draws A
// inside the panel's rounded rectangle; the tint follows as ordinary geometry,
// after the ResetRenderState callback DrawBackdropBlur() queues puts the
// backend's pipeline back.

void VanGui_ImplDX12_SetRenderTarget(ID3D12Resource* target, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd) return;
    bd->pTarget      = target;
    bd->hTargetRtv   = rtv;
    bd->HasTargetDsv = dsv != nullptr;
    bd->hTargetDsv   = dsv ? *dsv : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

#ifdef VANGUI_ENABLE_EFFECTS

// A texture the blur renders into or samples, grown as panels get bigger and
// never shrunk, so a steady UI stops allocating.
struct VanGui_ImplDX12_BlurTex
{
    ID3D12Resource*             Res     = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu  = {};
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu  = {};
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv     = {};   // fixed slot in the blur's RTV heap (A and B)
    D3D12_RESOURCE_STATES       State   = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT                 Format  = DXGI_FORMAT_UNKNOWN;
    int                         W = 0, H = 0;
};

struct VanGui_ImplDX12_Blur
{
    ID3D12RootSignature*        pRootSignature  = nullptr;
    ID3D12PipelineState*        pDown           = nullptr;
    ID3D12PipelineState*        pBlur           = nullptr;
    ID3D12PipelineState*        pComposite      = nullptr;
    ID3D12DescriptorHeap*       pRtvHeap        = nullptr;  // A and B; RTVs are read at record time
    VanGui_ImplDX12_BlurTex     Source, A, B;
};

// Root constants, not a constant buffer: nothing to allocate per pass, and they
// are captured when recorded, so each pass keeps its own. 44 of the root
// signature's 64 DWORDs; the taps are packed two to a float4 to fit.
struct VanGui_ImplDX12_BlurParams
{
    float Step[4];      // xy: one texel along the pass (uv); zw: down-pass tap offset (uv)
    float Scale[4];     // xy: output uv -> source uv; zw: largest source uv to sample
    float Rect[4];      // panel, framebuffer px (x0, y0, x1, y1)
    float Region[4];    // copied region's origin (framebuffer px); zw: framebuffer px -> blurred uv
    float Misc[4];      // x: rounding px, y: saturation, z: taps
    float Taps[VanBlur::kMaxTaps / 2][4];   // tap 2i in xy, tap 2i+1 in zw: (offset texels, weight)
};
static_assert(sizeof(VanGui_ImplDX12_BlurParams) == 44 * 4, "blur root constants");

static const char* blurShadersHlsl = R"(
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
    float4 g_Taps[6];
};
float4 Fetch(float2 uv) { return tex0.Sample(samp0, min(uv, g_Scale.zw)); }
float2 Tap(int k) { float4 t = g_Taps[k >> 1]; return (k & 1) ? t.zw : t.xy; }
float4 PSDown(VSOut i) : SV_TARGET
{
    float2 uv = i.uv * g_Scale.xy, d = g_Step.zw;
    return 0.25 * (Fetch(uv + float2(-d.x, -d.y)) + Fetch(uv + float2(d.x, -d.y)) +
                   Fetch(uv + float2(-d.x,  d.y)) + Fetch(uv + float2(d.x,  d.y)));
}
float4 PSBlur(VSOut i) : SV_TARGET
{
    float2 uv = i.uv * g_Scale.xy;
    float4 c = Fetch(uv) * Tap(0).y;
    int n = (int)g_Misc.z;
    [unroll] for (int k = 1; k < 12; ++k)
    {
        if (k < n)
        {
            float2 t = Tap(k);
            float2 o = g_Step.xy * t.x;
            c += (Fetch(uv + o) + Fetch(uv - o)) * t.y;
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

static int VanGui_ImplDX12_CurrentSlot(VanGui_ImplDX12_Data* bd)
{
    return (bd->frameIndex == INT_MAX) ? 0 : (bd->frameIndex % bd->numFramesInFlight);
}

// Lets go of a blur texture and its SRV slot: through the retire ring when a
// command list may still name them, at once when the device is idle. The RTV
// slot is the heap's and stays.
static void VanGui_ImplDX12_DropBlurTex(VanGui_ImplDX12_Data* bd, VanGui_ImplDX12_BlurTex& t, bool deferred)
{
    if (t.Res)
    {
        if (deferred && bd->pRetired)
        {
            VanGui_ImplDX12_Retired r;
            r.resource       = t.Res;
            r.hCpuDescHandle = t.SrvCpu;
            r.hGpuDescHandle = t.SrvGpu;
            r.ownsDescriptor = true;
            bd->pRetired[VanGui_ImplDX12_CurrentSlot(bd)].push_back(r);
        }
        else
        {
            t.Res->Release();
            if (bd->InitInfo.SrvDescriptorFreeFn)
                bd->InitInfo.SrvDescriptorFreeFn(&bd->InitInfo, t.SrvCpu, t.SrvGpu);
        }
    }
    t.Res    = nullptr;
    t.SrvCpu = {};
    t.SrvGpu = {};
    t.State  = D3D12_RESOURCE_STATE_COMMON;
    t.Format = DXGI_FORMAT_UNKNOWN;
    t.W = t.H = 0;
}

// Grows `t` to at least w x h. A grown texture gets a new SRV slot as well: the
// old slot may be named by a command list still in flight, and a shader-visible
// descriptor is read when the GPU runs, not when the list was recorded.
static bool VanGui_ImplDX12_EnsureBlurTex(VanGui_ImplDX12_Data* bd, VanGui_ImplDX12_BlurTex& t, int w, int h,
                                          DXGI_FORMAT resource_format, DXGI_FORMAT view_format, bool render_target)
{
    if (t.Res && t.W >= w && t.H >= h && t.Format == resource_format) return true;
    const int nw = (t.Format == resource_format && t.W > w) ? t.W : w;
    const int nh = (t.Format == resource_format && t.H > h) ? t.H : h;
    VanGui_ImplDX12_DropBlurTex(bd, t, true);

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = (UINT64)nw;
    desc.Height           = (UINT)nh;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = resource_format;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags            = render_target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    const D3D12_RESOURCE_STATES initial = render_target ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                        : D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(bd->pd3dDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr,
                                                       IID_PPV_ARGS(&t.Res))))
    {
        t.Res = nullptr;
        return false;
    }

    bd->InitInfo.SrvDescriptorAllocFn(&bd->InitInfo, &t.SrvCpu, &t.SrvGpu);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format                  = view_format;
    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels     = 1;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    bd->pd3dDevice->CreateShaderResourceView(t.Res, &srv, t.SrvCpu);
    if (render_target)
        bd->pd3dDevice->CreateRenderTargetView(t.Res, nullptr, t.Rtv);

    t.State  = initial;
    t.Format = resource_format;
    t.W = nw;
    t.H = nh;
    return true;
}

static void VanGui_ImplDX12_Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    cmd->ResourceBarrier(1, &b);
}

static void VanGui_ImplDX12_BlurTransition(ID3D12GraphicsCommandList* cmd, VanGui_ImplDX12_BlurTex& t, D3D12_RESOURCE_STATES to)
{
    if (t.State == to) return;
    VanGui_ImplDX12_Barrier(cmd, t.Res, t.State, to);
    t.State = to;
}

// The format to read a copy of the target through. A swap chain buffer is often
// created typeless and viewed through the RTV's format; a copy of it keeps the
// typeless format, so its SRV needs the same typed one.
static DXGI_FORMAT VanGui_ImplDX12_BlurViewFormat(DXGI_FORMAT resource, DXGI_FORMAT rtv)
{
    switch (resource)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return rtv;
    default:
        return resource;
    }
}

static void VanGui_ImplDX12_BlurPass(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                     D3D12_GPU_DESCRIPTOR_HANDLE src, float vw, float vh, const D3D12_RECT& scissor,
                                     const VanGui_ImplDX12_BlurParams& p)
{
    cmd->SetPipelineState(pso);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = {};
    vp.Width    = vw;
    vp.Height   = vh;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRoot32BitConstants(0, sizeof(p) / 4, &p, 0);
    cmd->SetGraphicsRootDescriptorTable(1, src);
    cmd->DrawInstanced(3, 1, 0, 0);
}

static void VanGui_ImplDX12_BlurHandler(const VanDrawList*, const VanDrawCmd*, const VanGui::VanBlurRequest& req, void*)
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd || !bd->pBlur || !bd->pBlur->pComposite || !bd->pCmdList || !bd->pDrawData || !bd->pTarget) return;
    VanGui_ImplDX12_Blur* blur = bd->pBlur;
    ID3D12GraphicsCommandList* cmd = bd->pCmdList;
    const VanDrawData* dd = bd->pDrawData;

    const D3D12_RESOURCE_DESC td = bd->pTarget->GetDesc();
    if (td.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || td.SampleDesc.Count != 1) return;

    // Framebuffer pixels of the panel, its clip and the region to copy.
    const float sx = dd->FramebufferScale.x, sy = dd->FramebufferScale.y;
    const float px0 = (req.Rect.x - dd->DisplayPos.x) * sx, py0 = (req.Rect.y - dd->DisplayPos.y) * sy;
    const float px1 = (req.Rect.z - dd->DisplayPos.x) * sx, py1 = (req.Rect.w - dd->DisplayPos.y) * sy;
    const float sigma = VanBlur::SigmaFor(req.Radius * sx);
    VanBlur::Region rg;
    if (!VanBlur::RegionFor(px0, py0, px1, py1, VanBlur::Margin(sigma), (int)td.Width, (int)td.Height, &rg)) return;
    D3D12_RECT clip = {};
    clip.left   = (LONG)((req.ClipRect.x - dd->DisplayPos.x) * sx);
    clip.top    = (LONG)((req.ClipRect.y - dd->DisplayPos.y) * sy);
    clip.right  = (LONG)((req.ClipRect.z - dd->DisplayPos.x) * sx);
    clip.bottom = (LONG)((req.ClipRect.w - dd->DisplayPos.y) * sy);
    if (clip.left < (LONG)px0) clip.left = (LONG)px0;
    if (clip.top < (LONG)py0) clip.top = (LONG)py0;
    if (clip.right > (LONG)(px1 + 1.0f)) clip.right = (LONG)(px1 + 1.0f);
    if (clip.bottom > (LONG)(py1 + 1.0f)) clip.bottom = (LONG)(py1 + 1.0f);
    if (clip.right <= clip.left || clip.bottom <= clip.top) return;

    const DXGI_FORMAT view_format = VanGui_ImplDX12_BlurViewFormat(td.Format, bd->RTVFormat);
    const int rw = rg.X1 - rg.X0, rh = rg.Y1 - rg.Y0;
    const int f = VanBlur::Downsample(sigma);
    const int dw = (rw + f - 1) / f, dh = (rh + f - 1) / f;
    if (!VanGui_ImplDX12_EnsureBlurTex(bd, blur->Source, rw, rh, td.Format, view_format, false)
        || !VanGui_ImplDX12_EnsureBlurTex(bd, blur->A, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, true)
        || !VanGui_ImplDX12_EnsureBlurTex(bd, blur->B, dw, dh, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, true))
        return;   // nothing recorded yet: the target is as the backend left it

    // Copy the region out.
    VanGui_ImplDX12_Barrier(cmd, bd->pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    VanGui_ImplDX12_BlurTransition(cmd, blur->Source, D3D12_RESOURCE_STATE_COPY_DEST);
    {
        D3D12_TEXTURE_COPY_LOCATION from = {};
        from.pResource        = bd->pTarget;
        from.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION to = {};
        to.pResource          = blur->Source.Res;
        to.Type               = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex   = 0;
        const D3D12_BOX box = { (UINT)rg.X0, (UINT)rg.Y0, 0, (UINT)rg.X1, (UINT)rg.Y1, 1 };
        cmd->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
    }
    VanGui_ImplDX12_Barrier(cmd, bd->pTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    VanGui_ImplDX12_BlurTransition(cmd, blur->Source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Our pipeline. The backend's comes back with the ResetRenderState that follows.
    cmd->SetGraphicsRootSignature(blur->pRootSignature);
    cmd->SetDescriptorHeaps(1, &bd->pd3dSrvDescHeap);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const VanBlur::Kernel k = VanBlur::MakeKernel(sigma / float(f));
    VanGui_ImplDX12_BlurParams p = {};
    p.Misc[0] = req.Rounding * sx;
    p.Misc[1] = req.Saturation;
    p.Misc[2] = float(k.Taps);
    for (int i = 0; i < k.Taps; ++i)
    {
        p.Taps[i >> 1][(i & 1) * 2 + 0] = k.Offsets[i];
        p.Taps[i >> 1][(i & 1) * 2 + 1] = k.Weights[i];
    }
    const D3D12_RECT low = { 0, 0, dw, dh };

    // Down: region -> A at 1/f. Each A texel averages its f x f block (four bilinear taps).
    p.Step[2] = f > 1 ? float(f) * 0.25f / float(blur->Source.W) : 0.0f;
    p.Step[3] = f > 1 ? float(f) * 0.25f / float(blur->Source.H) : 0.0f;
    p.Scale[0] = float(f * dw) / float(blur->Source.W);
    p.Scale[1] = float(f * dh) / float(blur->Source.H);
    p.Scale[2] = (float(rw) - 0.5f) / float(blur->Source.W);
    p.Scale[3] = (float(rh) - 0.5f) / float(blur->Source.H);
    VanGui_ImplDX12_BlurTransition(cmd, blur->A, D3D12_RESOURCE_STATE_RENDER_TARGET);
    VanGui_ImplDX12_BlurPass(cmd, blur->pDown, blur->A.Rtv, blur->Source.SrvGpu, float(dw), float(dh), low, p);

    // Blur: A -> B across, B -> A down.
    p.Step[2] = p.Step[3] = 0.0f;
    p.Scale[0] = float(dw) / float(blur->A.W);
    p.Scale[1] = float(dh) / float(blur->A.H);
    p.Scale[2] = (float(dw) - 0.5f) / float(blur->A.W);
    p.Scale[3] = (float(dh) - 0.5f) / float(blur->A.H);
    p.Step[0] = 1.0f / float(blur->A.W);
    p.Step[1] = 0.0f;
    VanGui_ImplDX12_BlurTransition(cmd, blur->A, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    VanGui_ImplDX12_BlurTransition(cmd, blur->B, D3D12_RESOURCE_STATE_RENDER_TARGET);
    VanGui_ImplDX12_BlurPass(cmd, blur->pBlur, blur->B.Rtv, blur->A.SrvGpu, float(dw), float(dh), low, p);
    p.Step[0] = 0.0f;
    p.Step[1] = 1.0f / float(blur->A.H);
    VanGui_ImplDX12_BlurTransition(cmd, blur->B, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    VanGui_ImplDX12_BlurTransition(cmd, blur->A, D3D12_RESOURCE_STATE_RENDER_TARGET);
    VanGui_ImplDX12_BlurPass(cmd, blur->pBlur, blur->A.Rtv, blur->B.SrvGpu, float(dw), float(dh), low, p);
    VanGui_ImplDX12_BlurTransition(cmd, blur->A, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Composite A inside the panel's rounded rectangle, on the original target.
    p.Rect[0] = px0; p.Rect[1] = py0; p.Rect[2] = px1; p.Rect[3] = py1;
    p.Region[0] = float(rg.X0); p.Region[1] = float(rg.Y0);
    p.Region[2] = 1.0f / (float(f) * float(blur->A.W)); p.Region[3] = 1.0f / (float(f) * float(blur->A.H));
    VanGui_ImplDX12_BlurPass(cmd, blur->pComposite, bd->hTargetRtv, blur->A.SrvGpu, float(td.Width), float(td.Height), clip, p);

    // Put the application's binding back, depth included when it named one.
    cmd->OMSetRenderTargets(1, &bd->hTargetRtv, FALSE, bd->HasTargetDsv ? &bd->hTargetDsv : nullptr);
}

static bool VanGui_ImplDX12_CreateBlurObjects(VanGui_ImplDX12_Data* bd)
{
    VanGui_ImplDX12_Blur* blur = bd->pBlur;
    if (!blur || !bd->pd3dDevice) return false;
    if (blur->pComposite) return true;
    VanGui_ImplDX12_ReleaseBlurObjects(bd, false);   // a half-built set from an earlier failure

    ID3DBlob* blobs[4] = {};
    static const char* const entries[4]  = { "VS", "PSDown", "PSBlur", "PSComposite" };
    static const char* const profiles[4] = { "vs_5_0", "ps_5_0", "ps_5_0", "ps_5_0" };
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++)
    {
        ID3DBlob* errors = nullptr;
        ok = SUCCEEDED(D3DCompile(blurShadersHlsl, strlen(blurShadersHlsl), "vangui_blur", nullptr, nullptr,
                                  entries[i], profiles[i], D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blobs[i], &errors));
        if (errors)
        {
            OutputDebugStringA((const char*)errors->GetBufferPointer());
            errors->Release();
        }
    }

    // Root signature: the params as constants at b0, one SRV at t0, a linear clamp sampler.
    if (ok)
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = sizeof(VanGui_ImplDX12_BlurParams) / 4;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs = {};
        rs.NumParameters     = 2;
        rs.pParameters       = params;
        rs.NumStaticSamplers = 1;
        rs.pStaticSamplers   = &sampler;
        rs.Flags             = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                             | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                             | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        ID3DBlob* rs_blob = nullptr;
        ID3DBlob* errors = nullptr;
        ok = SUCCEEDED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &errors));
        if (errors) errors->Release();
        ok = ok && SUCCEEDED(bd->pd3dDevice->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                                                 IID_PPV_ARGS(&blur->pRootSignature)));
        if (rs_blob) rs_blob->Release();
    }

    // Three pipelines on one fullscreen triangle: the two reductions write the
    // RGBA16F intermediates opaquely; the composite blends onto the target.
    if (ok)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature        = blur->pRootSignature;
        pso.VS                    = { blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize() };
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets      = 1;
        pso.SampleDesc.Count      = 1;
        pso.SampleMask            = 0xFFFFFFFFu;   // zero would mask out every sample; see CreateDeviceObjects
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable   = FALSE;
        pso.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.PS = { blobs[1]->GetBufferPointer(), blobs[1]->GetBufferSize() };
        ok = SUCCEEDED(bd->pd3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&blur->pDown)));
        pso.PS = { blobs[2]->GetBufferPointer(), blobs[2]->GetBufferSize() };
        ok = ok && SUCCEEDED(bd->pd3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&blur->pBlur)));

        D3D12_RENDER_TARGET_BLEND_DESC& over = pso.BlendState.RenderTarget[0];
        over.BlendEnable    = TRUE;
        over.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        over.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        over.BlendOp        = D3D12_BLEND_OP_ADD;
        over.SrcBlendAlpha  = D3D12_BLEND_ONE;
        over.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        over.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        pso.RTVFormats[0] = bd->RTVFormat;
        pso.PS = { blobs[3]->GetBufferPointer(), blobs[3]->GetBufferSize() };
        ok = ok && SUCCEEDED(bd->pd3dDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&blur->pComposite)));
    }
    for (ID3DBlob* b : blobs)
        if (b) b->Release();

    if (ok)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 2;
        ok = SUCCEEDED(bd->pd3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&blur->pRtvHeap)));
    }
    if (ok)
    {
        const UINT step = bd->pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        blur->A.Rtv = blur->pRtvHeap->GetCPUDescriptorHandleForHeapStart();
        blur->B.Rtv = blur->A.Rtv;
        blur->B.Rtv.ptr += step;
    }

    if (!ok)
    {
        VanGui_ImplDX12_ReleaseBlurObjects(bd, false);   // nothing of it was ever recorded
        return false;
    }
    return true;
}

static void VanGui_ImplDX12_ReleaseBlurObjects(VanGui_ImplDX12_Data* bd, bool deferred)
{
    VanGui_ImplDX12_Blur* blur = bd ? bd->pBlur : nullptr;
    if (!blur) return;
    VanGui_ImplDX12_DropBlurTex(bd, blur->Source, deferred);
    VanGui_ImplDX12_DropBlurTex(bd, blur->A, deferred);
    VanGui_ImplDX12_DropBlurTex(bd, blur->B, deferred);
    ID3D12DeviceChild* objects[5] = { blur->pRootSignature, blur->pDown, blur->pBlur, blur->pComposite, blur->pRtvHeap };
    for (ID3D12DeviceChild* o : objects)
    {
        if (!o) continue;
        if (deferred) VanGui_ImplDX12_Retire(bd, o);
        else o->Release();
    }
    blur->pRootSignature = nullptr;
    blur->pDown = blur->pBlur = blur->pComposite = nullptr;
    blur->pRtvHeap = nullptr;
    blur->A.Rtv = blur->B.Rtv = {};
}

bool VanGui_ImplDX12_InitBlur()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd || !bd->pd3dDevice || !bd->InitInfo.SrvDescriptorAllocFn)
        return false;   // the single-slot Init has nowhere to put the blur's views
    VanGui_ImplDX12_ShutdownBlur();
    bd->pBlur = new VanGui_ImplDX12_Blur();
    if (!VanGui_ImplDX12_CreateBlurObjects(bd))
    {
        VanGui_ImplDX12_ShutdownBlur();
        return false;
    }
    VanGui::SetBlurHandler(VanGui_ImplDX12_BlurHandler, nullptr);
    return true;
}

void VanGui_ImplDX12_ShutdownBlur()
{
    VanGui_ImplDX12_Data* bd = VanGui_ImplDX12_GetBackendData();
    if (!bd || !bd->pBlur) return;
    VanGui::SetBlurHandler(nullptr, nullptr);
    VanGui_ImplDX12_ReleaseBlurObjects(bd, true);
    delete bd->pBlur;
    bd->pBlur = nullptr;
}

#else // VANGUI_ENABLE_EFFECTS

struct VanGui_ImplDX12_Blur {};
static bool VanGui_ImplDX12_CreateBlurObjects(VanGui_ImplDX12_Data*) { return false; }
static void VanGui_ImplDX12_ReleaseBlurObjects(VanGui_ImplDX12_Data*, bool) {}
bool VanGui_ImplDX12_InitBlur() { return false; }
void VanGui_ImplDX12_ShutdownBlur() {}

#endif // VANGUI_ENABLE_EFFECTS

#endif // #ifndef VANGUI_DISABLE
