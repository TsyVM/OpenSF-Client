// VanGUI: Renderer Backend for DirectX10
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D10ShaderResourceView*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-23: DirectX10: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState, DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest. Obsoleting samplers from VanGui_ImplDX10_RenderState. (#9378)
//  2026-01-19: DirectX10: Added 'SamplerNearest' in VanGui_ImplDX10_RenderState. Renamed 'SamplerDefault' to 'SamplerLinear'. 
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-06-11: DirectX10: Added support for VanGuiBackendFlags_RendererHasTextures, for dynamic font atlas.
//  2025-05-07: DirectX10: Honor draw_data->FramebufferScale to allow for custom backends and experiment using it (consistently with other renderer backends, even though in normal condition it is not set under Windows).
//  2025-01-06: DirectX10: Expose selected render state in VanGui_ImplDX10_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2024-10-07: DirectX10: Changed default texture sampler to Clamp instead of Repeat/Wrap.
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: DirectX10: Replaced direct access to VanDrawCmd::TextureId with a call to VanDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: DirectX10: Change blending equation to preserve alpha in output buffer.
//  2019-07-21: DirectX10: Backup, clear and restore Geometry Shader is any is bound when calling VanGui_ImplDX10_RenderDrawData().
//  2019-05-29: DirectX10: Added support for large mesh (64K+ vertices), enable VanGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: DirectX10: Added support for special VanDrawCallback_ResetRenderState callback to reset render state.
//  2018-12-03: Misc: Added #pragma comment statement to automatically link with d3dcompiler.lib when using D3DCompile().
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-07-13: DirectX10: Fixed unreleased resources in Init and Shutdown functions.
//  2018-06-08: Misc: Extracted vangui_impl_dx10.cpp/.h away from the old combined DX10+Win32 example.
//  2018-06-08: DirectX10: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-04-09: Misc: Fixed erroneous call to io.Fonts->ClearInputData() + ClearTexData() that was left in DX10 example but removed in 1.47 (Nov 2015) on other backends.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed VanGui_ImplDX10_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to VanGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2016-05-07: DirectX10: Disabling depth-write.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_dx10.h"

// DirectX
#include <cstdio>
#include <d3d10_1.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#endif

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wold-style-cast"         // warning: use of old-style cast                            // yes, they are more terse.
#pragma clang diagnostic ignored "-Wsign-conversion"        // warning: implicit conversion changes signedness
#endif

// DirectX10 data
struct VanGui_ImplDX10_Texture
{
    ID3D10Texture2D*            pTexture;
    ID3D10ShaderResourceView*   pTextureView;
};

struct VanGui_ImplDX10_Data
{
    ID3D10Device*               pd3dDevice              = nullptr;
    IDXGIFactory*               pFactory                = nullptr;
    ID3D10Buffer*               pVB                     = nullptr;
    ID3D10Buffer*               pIB                     = nullptr;
    ID3D10VertexShader*         pVertexShader           = nullptr;
    ID3D10InputLayout*          pInputLayout            = nullptr;
    ID3D10Buffer*               pVertexConstantBuffer   = nullptr;
    ID3D10PixelShader*          pPixelShader            = nullptr;
    ID3D10SamplerState*         pTexSamplerLinear       = nullptr;
    ID3D10SamplerState*         pTexSamplerNearest      = nullptr;
    ID3D10RasterizerState*      pRasterizerState        = nullptr;
    ID3D10BlendState*           pBlendState             = nullptr;
    ID3D10DepthStencilState*    pDepthStencilState      = nullptr;
    int                         VertexBufferSize        = 5000;
    int                         IndexBufferSize         = 10000;
};

struct VERTEX_CONSTANT_BUFFER_DX10
{
    float   mvp[4][4];
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
static VanGui_ImplDX10_Data* VanGui_ImplDX10_GetBackendData()
{
    return VanGui::GetCurrentContext() ? (VanGui_ImplDX10_Data*)VanGui::GetIO().BackendRendererUserData : nullptr;
}

// Functions
static void VanGui_ImplDX10_SetupRenderState(VanDrawData* draw_data, ID3D10Device* device)
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();

    // Setup viewport
    D3D10_VIEWPORT vp = {};
    vp.Width = (UINT)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    vp.Height = (UINT)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    device->RSSetViewports(1, &vp);

    // Setup orthographic projection matrix into our constant buffer
    // Our visible vangui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    void* mapped_resource;
    if (bd->pVertexConstantBuffer->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped_resource) == S_OK)
    {
        VERTEX_CONSTANT_BUFFER_DX10* constant_buffer = (VERTEX_CONSTANT_BUFFER_DX10*)mapped_resource;
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
        bd->pVertexConstantBuffer->Unmap();
    }

    // Setup shader and vertex buffers
    unsigned int stride = sizeof(VanDrawVert);
    unsigned int offset = 0;
    device->IASetInputLayout(bd->pInputLayout);
    device->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    device->IASetIndexBuffer(bd->pIB, sizeof(VanDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->VSSetShader(bd->pVertexShader);
    device->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    device->PSSetShader(bd->pPixelShader);
    device->PSSetSamplers(0, 1, &bd->pTexSamplerLinear);
    device->GSSetShader(nullptr);

    // Setup render state
    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    device->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    device->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    device->RSSetState(bd->pRasterizerState);
}

// Draw callbacks
static void VanGui_ImplDX10_DrawCallback_ResetRenderState(const VanDrawList*, const VanDrawCmd*)       {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.
static void VanGui_ImplDX10_DrawCallback_SetSamplerLinear(const VanDrawList*, const VanDrawCmd*)       { VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData(); bd->pd3dDevice->PSSetSamplers(0, 1, &bd->pTexSamplerLinear); }
static void VanGui_ImplDX10_DrawCallback_SetSamplerNearest(const VanDrawList*, const VanDrawCmd*)      { VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData(); bd->pd3dDevice->PSSetSamplers(0, 1, &bd->pTexSamplerNearest); }

// Render function
void VanGui_ImplDX10_RenderDrawData(VanDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    ID3D10Device* device = bd->pd3dDevice;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to VanGui::GetPlatformIO().Textures[] but is part of VanDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (VanTextureData* tex : *draw_data->Textures)
            if (tex->Status != VanTextureStatus_OK)
                VanGui_ImplDX10_UpdateTexture(tex);

    // Create and grow vertex/index buffers if needed
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D10_BUFFER_DESC desc = {};
        desc.Usage = D3D10_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)bd->VertexBufferSize * sizeof(VanDrawVert);
        desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        HRESULT hr = device->CreateBuffer(&desc, nullptr, &bd->pVB);
        VAN_ASSERT(SUCCEEDED(hr) && "CreateBuffer (VB) failed");
        if (!SUCCEEDED(hr))
            return;
    }

    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D10_BUFFER_DESC desc = {};
        desc.Usage = D3D10_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)bd->IndexBufferSize * sizeof(VanDrawIdx);
        desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
        HRESULT hr = device->CreateBuffer(&desc, nullptr, &bd->pIB);
        VAN_ASSERT(SUCCEEDED(hr) && "CreateBuffer (IB) failed");
        if (!SUCCEEDED(hr))
            return;
    }

    // Copy and convert all vertices into a single contiguous buffer
    VanDrawVert* vtx_dst = nullptr;
    VanDrawIdx* idx_dst = nullptr;
    bd->pVB->Map(D3D10_MAP_WRITE_DISCARD, 0, (void**)&vtx_dst);
    bd->pIB->Map(D3D10_MAP_WRITE_DISCARD, 0, (void**)&idx_dst);
    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(VanDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(VanDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }
    bd->pVB->Unmap();
    bd->pIB->Unmap();

    // Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
    struct BACKUP_DX10_STATE
    {
        UINT                        ScissorRectsCount, ViewportsCount;
        D3D10_RECT                  ScissorRects[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D10_VIEWPORT              Viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D10RasterizerState*      RS;
        ID3D10BlendState*           BlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        UINT                        StencilRef;
        ID3D10DepthStencilState*    DepthStencilState;
        ID3D10ShaderResourceView*   PSShaderResource;
        ID3D10SamplerState*         PSSampler;
        ID3D10PixelShader*          PS;
        ID3D10VertexShader*         VS;
        ID3D10GeometryShader*       GS;
        D3D10_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
        ID3D10Buffer*               IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                 IndexBufferFormat;
        ID3D10InputLayout*          InputLayout;
    };
    BACKUP_DX10_STATE old = {};
    old.ScissorRectsCount = old.ViewportsCount = D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    device->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
    device->RSGetViewports(&old.ViewportsCount, old.Viewports);
    device->RSGetState(&old.RS);
    device->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
    device->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
    device->PSGetShaderResources(0, 1, &old.PSShaderResource);
    device->PSGetSamplers(0, 1, &old.PSSampler);
    device->PSGetShader(&old.PS);
    device->VSGetShader(&old.VS);
    device->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
    device->GSGetShader(&old.GS);
    device->IAGetPrimitiveTopology(&old.PrimitiveTopology);
    device->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
    device->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
    device->IAGetInputLayout(&old.InputLayout);

    // Setup desired DX state
    VanGui_ImplDX10_SetupRenderState(draw_data, device);
    // Setup render state structure (for callbacks and custom texture bindings)
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    VanGui_ImplDX10_RenderState render_state;
    render_state.Device = bd->pd3dDevice;
    render_state.VertexConstantBuffer = bd->pVertexConstantBuffer;
    platform_io.Renderer_RenderState = &render_state;

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    VanVec2 clip_off = draw_data->DisplayPos;
    VanVec2 clip_scale = draw_data->FramebufferScale;
    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const VanDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via VanDrawList::AddCallback()
                if (pcmd->UserCallback == VanGui_ImplDX10_DrawCallback_ResetRenderState)
                    VanGui_ImplDX10_SetupRenderState(draw_data, device);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                VanVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                VanVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                const D3D10_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                device->RSSetScissorRects(1, &r);

                // Bind texture, Draw
                ID3D10ShaderResourceView* texture_srv = (ID3D10ShaderResourceView*)pcmd->GetTexID();
                device->PSSetShaderResources(0, 1, &texture_srv);
                device->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }
    platform_io.Renderer_RenderState = nullptr;

    // Restore modified DX state
    device->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
    device->RSSetViewports(old.ViewportsCount, old.Viewports);
    device->RSSetState(old.RS); if (old.RS) old.RS->Release();
    device->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
    device->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
    device->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
    device->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
    device->PSSetShader(old.PS); if (old.PS) old.PS->Release();
    device->VSSetShader(old.VS); if (old.VS) old.VS->Release();
    device->GSSetShader(old.GS); if (old.GS) old.GS->Release();
    device->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
    device->IASetPrimitiveTopology(old.PrimitiveTopology);
    device->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
    device->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
    device->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}

static void VanGui_ImplDX10_DestroyTexture(VanTextureData* tex)
{
    if (VanGui_ImplDX10_Texture* backend_tex = (VanGui_ImplDX10_Texture*)tex->BackendUserData)
    {
        VAN_ASSERT(backend_tex->pTextureView == (ID3D10ShaderResourceView*)(intptr_t)tex->TexID);
        backend_tex->pTextureView->Release();
        backend_tex->pTexture->Release();
        VAN_DELETE(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(VanTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(VanTextureStatus_Destroyed);
}

void VanGui_ImplDX10_UpdateTexture(VanTextureData* tex)
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    if (tex->Status == VanTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //VANGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        VAN_ASSERT(tex->TexID == VanTextureID_Invalid && tex->BackendUserData == nullptr);
        VAN_ASSERT(tex->Format == VanTextureFormat_RGBA32);
        unsigned int* pixels = (unsigned int*)tex->GetPixels();
        VanGui_ImplDX10_Texture* backend_tex = VAN_NEW(VanGui_ImplDX10_Texture)();

        // Create texture
        D3D10_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = (UINT)tex->Width;
        desc.Height = (UINT)tex->Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D10_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = pixels;
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        {
            HRESULT hr = bd->pd3dDevice->CreateTexture2D(&desc, &subResource, &backend_tex->pTexture);
            VAN_ASSERT(SUCCEEDED(hr) && "Backend failed to create texture!");
        }

        // Create texture view
        D3D10_SHADER_RESOURCE_VIEW_DESC srv_desc;
        ZeroMemory(&srv_desc, sizeof(srv_desc));
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        srv_desc.Texture2D.MostDetailedMip = 0;
        {
            HRESULT hr = bd->pd3dDevice->CreateShaderResourceView(backend_tex->pTexture, &srv_desc, &backend_tex->pTextureView);
            VAN_ASSERT(SUCCEEDED(hr) && "Backend failed to create texture view!");
        }

        // Store identifiers
        tex->SetTexID((VanTextureID)(intptr_t)backend_tex->pTextureView);
        tex->SetStatus(VanTextureStatus_OK);
        tex->BackendUserData = backend_tex;
    }
    else if (tex->Status == VanTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        VanGui_ImplDX10_Texture* backend_tex = (VanGui_ImplDX10_Texture*)tex->BackendUserData;
        VAN_ASSERT(backend_tex->pTextureView == (ID3D10ShaderResourceView*)(intptr_t)tex->TexID);
        for (VanTextureRect& r : tex->Updates)
        {
            D3D10_BOX box = { (UINT)r.x, (UINT)r.y, (UINT)0, (UINT)(r.x + r.w), (UINT)(r.y + r.h), (UINT)1 };
            bd->pd3dDevice->UpdateSubresource(backend_tex->pTexture, 0, &box, tex->GetPixelsAt(r.x, r.y), (UINT)tex->GetPitch(), 0);
        }
        tex->SetStatus(VanTextureStatus_OK);
    }
    if (tex->Status == VanTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        VanGui_ImplDX10_DestroyTexture(tex);
}

// Forward declarations for platform interface
static void VanGui_ImplDX10_InitPlatformInterface();
static void VanGui_ImplDX10_ShutdownPlatformInterface();

bool    VanGui_ImplDX10_CreateDeviceObjects()
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    if (!bd->pd3dDevice) [[unlikely]]
        return false;
    VanGui_ImplDX10_InvalidateDeviceObjects();

    // By using D3DCompile() from <d3dcompiler.h> / d3dcompiler.lib, we introduce a dependency to a given version of d3dcompiler_XX.dll (see D3DCOMPILER_DLL_A)
    // If you would like to use this DX10 sample code but remove this dependency you can:
    //  1) compile once, save the compiled shader blobs into a file or source code and pass them to CreateVertexShader()/CreatePixelShader() [preferred solution]
    //  2) use code to detect any version of the DLL and grab a pointer to D3DCompile from the DLL.

    // Create the vertex shader
    {
        static const char* vertexShader =
            "cbuffer vertexBuffer : register(b0) \
            {\
              float4x4 ProjectionMatrix; \
            };\
            struct VS_INPUT\
            {\
              float2 pos : POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
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

        ID3DBlob* vertexShaderBlob;
        if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr)))
            return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
        if (bd->pd3dDevice->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &bd->pVertexShader) != S_OK)
        {
            vertexShaderBlob->Release();
            return false;
        }

        // Create the input layout
        D3D10_INPUT_ELEMENT_DESC local_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(VanDrawVert, pos), D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(VanDrawVert, uv),  D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(VanDrawVert, col), D3D10_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (bd->pd3dDevice->CreateInputLayout(local_layout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &bd->pInputLayout) != S_OK)
        {
            vertexShaderBlob->Release();
            return false;
        }
        vertexShaderBlob->Release();

        // Create the constant buffer
        {
            D3D10_BUFFER_DESC desc = {};
            desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX10);
            desc.Usage = D3D10_USAGE_DYNAMIC;
            desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
            desc.MiscFlags = 0;
            HRESULT hr = bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVertexConstantBuffer);
            VAN_ASSERT(SUCCEEDED(hr) && "CreateBuffer (constant buffer) failed");
        }
    }

    // Create the pixel shader
    {
        static const char* pixelShader =
            "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
            return out_col; \
            }";

        ID3DBlob* pixelShaderBlob;
        if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
            return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
        if (bd->pd3dDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), &bd->pPixelShader) != S_OK)
        {
            pixelShaderBlob->Release();
            return false;
        }
        pixelShaderBlob->Release();
    }

    // Create the blending setup
    {
        D3D10_BLEND_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.AlphaToCoverageEnable = false;
        desc.BlendEnable[0] = true;
        desc.SrcBlend = D3D10_BLEND_SRC_ALPHA;
        desc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
        desc.BlendOp = D3D10_BLEND_OP_ADD;
        desc.SrcBlendAlpha = D3D10_BLEND_ONE;
        desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
        desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
        desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
        {
            HRESULT hr = bd->pd3dDevice->CreateBlendState(&desc, &bd->pBlendState);
            VAN_ASSERT(SUCCEEDED(hr) && "CreateBlendState failed");
        }
    }

    // Create the rasterizer state
    {
        D3D10_RASTERIZER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.FillMode = D3D10_FILL_SOLID;
        desc.CullMode = D3D10_CULL_NONE;
        desc.ScissorEnable = true;
        desc.DepthClipEnable = true;
        HRESULT hr = bd->pd3dDevice->CreateRasterizerState(&desc, &bd->pRasterizerState);
        VAN_ASSERT(SUCCEEDED(hr) && "CreateRasterizerState failed");
    }

    // Create depth-stencil State
    {
        D3D10_DEPTH_STENCIL_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.DepthEnable = false;
        desc.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D10_COMPARISON_ALWAYS;
        desc.StencilEnable = false;
        desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D10_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D10_COMPARISON_ALWAYS;
        desc.BackFace = desc.FrontFace;
        HRESULT hr = bd->pd3dDevice->CreateDepthStencilState(&desc, &bd->pDepthStencilState);
        VAN_ASSERT(SUCCEEDED(hr) && "CreateDepthStencilState failed");
    }

    // Create texture sampler
    // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= VanFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
    {
        D3D10_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
        desc.MipLODBias = 0.f;
        desc.ComparisonFunc = D3D10_COMPARISON_ALWAYS;
        desc.MinLOD = 0.f;
        desc.MaxLOD = 0.f;
        {
            HRESULT hr = bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerLinear);
            VAN_ASSERT(SUCCEEDED(hr) && "CreateSamplerState (linear) failed");
        }
        desc.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
        {
            HRESULT hr = bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerNearest);
            VAN_ASSERT(SUCCEEDED(hr) && "CreateSamplerState (nearest) failed");
        }
    }

    return true;
}

void    VanGui_ImplDX10_InvalidateDeviceObjects()
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    if (!bd->pd3dDevice) [[unlikely]]
        return;

    // Destroy all textures
    for (VanTextureData* tex : VanGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            VanGui_ImplDX10_DestroyTexture(tex);
    if (bd->pTexSamplerLinear)      { bd->pTexSamplerLinear->Release(); bd->pTexSamplerLinear = nullptr; }
    if (bd->pTexSamplerNearest)     { bd->pTexSamplerNearest->Release(); bd->pTexSamplerNearest = nullptr; }
    if (bd->pIB)                    { bd->pIB->Release(); bd->pIB = nullptr; }
    if (bd->pVB)                    { bd->pVB->Release(); bd->pVB = nullptr; }
    if (bd->pBlendState)            { bd->pBlendState->Release(); bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)     { bd->pDepthStencilState->Release(); bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)       { bd->pRasterizerState->Release(); bd->pRasterizerState = nullptr; }
    if (bd->pPixelShader)           { bd->pPixelShader->Release(); bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer)  { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)           { bd->pInputLayout->Release(); bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)          { bd->pVertexShader->Release(); bd->pVertexShader = nullptr; }
}

bool    VanGui_ImplDX10_Init(ID3D10Device* device)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    VanGui_ImplDX10_Data* bd = VAN_NEW(VanGui_ImplDX10_Data)();
    io.BackendRendererUserData = static_cast<void*>(bd);
    io.BackendRendererName = "vangui_impl_dx10";
    io.BackendFlags |= VanGuiBackendFlags_RendererHasVtxOffset;  // We can honor the VanDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= VanGuiBackendFlags_RendererHasTextures;   // We can honor VanGuiPlatformIO::Textures[] requests during render.

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    platform_io.DrawCallback_ResetRenderState = VanGui_ImplDX10_DrawCallback_ResetRenderState;
    platform_io.DrawCallback_SetSamplerLinear = VanGui_ImplDX10_DrawCallback_SetSamplerLinear;
    platform_io.DrawCallback_SetSamplerNearest = VanGui_ImplDX10_DrawCallback_SetSamplerNearest;

    // Get factory from device
    IDXGIDevice* pDXGIDevice = nullptr;
    IDXGIAdapter* pDXGIAdapter = nullptr;
    IDXGIFactory* pFactory = nullptr;
    if (device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK)
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK)
            if (pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK)
            {
                bd->pd3dDevice = device;
                bd->pFactory = pFactory;
            }
    if (pDXGIDevice) pDXGIDevice->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();
    bd->pd3dDevice->AddRef();

    VanGui_ImplDX10_InitPlatformInterface();
    return true;
}

void VanGui_ImplDX10_Shutdown()
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    VanGui_ImplDX10_ShutdownPlatformInterface();
    VanGui_ImplDX10_InvalidateDeviceObjects();
    if (bd->pFactory) { bd->pFactory->Release(); }
    if (bd->pd3dDevice) { bd->pd3dDevice->Release(); }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_RendererHasVtxOffset | VanGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    VAN_DELETE(bd);
}

void VanGui_ImplDX10_NewFrame()
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplDX10_Init()?");

    if (!bd->pVertexShader)
        if (!VanGui_ImplDX10_CreateDeviceObjects())
            VAN_ASSERT(0 && "VanGui_ImplDX10_CreateDeviceObjects() failed!");
}

//-----------------------------------------------------------------------------

// [SECTION] Multi-viewport / Renderer Interface
//-----------------------------------------------------------------------------
// - VanGui_ImplDX10_ViewportData
// - VanGui_ImplDX10_CreateWindow()
// - VanGui_ImplDX10_DestroyWindow()
// - VanGui_ImplDX10_SetWindowSize()
// - VanGui_ImplDX10_RenderWindow()
// - VanGui_ImplDX10_SwapBuffers()
// - VanGui_ImplDX10_InitPlatformInterface()
// - VanGui_ImplDX10_ShutdownPlatformInterface()
//-----------------------------------------------------------------------------

struct VanGui_ImplDX10_ViewportData
{
    IDXGISwapChain*             SwapChain   = nullptr;
    ID3D10RenderTargetView*     RTView      = nullptr;

    VanGui_ImplDX10_ViewportData()  = default;
    ~VanGui_ImplDX10_ViewportData() { VAN_ASSERT(SwapChain == nullptr && RTView == nullptr); }
};

static void VanGui_ImplDX10_CreateWindow(VanGuiViewport* viewport)
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    auto* vd = VAN_NEW(VanGui_ImplDX10_ViewportData)();
    viewport->RendererUserData = vd;

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.Width                              = static_cast<UINT>(viewport->Size.x);
    sd.Height                             = static_cast<UINT>(viewport->Size.y);
    sd.Format                             = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Windowed                           = TRUE;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = static_cast<HWND>(viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle);

    IDXGIDevice*  pDXGIDevice  = nullptr;
    IDXGIAdapter* pDXGIAdapter = nullptr;
    IDXGIFactory* pFactory     = nullptr;

    if (bd->pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK)
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK)
            if (pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK)
            {
                HRESULT hr = pFactory->CreateSwapChain(bd->pd3dDevice, &sd, &vd->SwapChain);
                VAN_ASSERT(SUCCEEDED(hr) && "CreateSwapChain failed for secondary viewport!");
            }

    if (pFactory)     pFactory->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();
    if (pDXGIDevice)  pDXGIDevice->Release();

    // Create render target view for the swap chain back buffer
    if (vd->SwapChain != nullptr)
    {
        ID3D10Texture2D* pBackBuffer = nullptr;
        vd->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        bd->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &vd->RTView);
        pBackBuffer->Release();
    }
}

static void VanGui_ImplDX10_DestroyWindow(VanGuiViewport* viewport)
{
    auto* vd = static_cast<VanGui_ImplDX10_ViewportData*>(viewport->RendererUserData);
    if (vd == nullptr)
        return;

    if (vd->SwapChain) { vd->SwapChain->Release(); vd->SwapChain = nullptr; }
    if (vd->RTView)    { vd->RTView->Release();    vd->RTView    = nullptr; }

    VAN_DELETE(vd);
    viewport->RendererUserData = nullptr;
}

static void VanGui_ImplDX10_SetWindowSize(VanGuiViewport* viewport, VanVec2 size)
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    auto* vd = static_cast<VanGui_ImplDX10_ViewportData*>(viewport->RendererUserData);

    if (vd->RTView) { vd->RTView->Release(); vd->RTView = nullptr; }

    vd->SwapChain->ResizeBuffers(0, static_cast<UINT>(size.x), static_cast<UINT>(size.y), DXGI_FORMAT_UNKNOWN, 0);

    ID3D10Texture2D* pBackBuffer = nullptr;
    vd->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    bd->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &vd->RTView);
    pBackBuffer->Release();
}

static void VanGui_ImplDX10_RenderWindow(VanGuiViewport* viewport, void*)
{
    VanGui_ImplDX10_Data* bd = VanGui_ImplDX10_GetBackendData();
    auto* vd = static_cast<VanGui_ImplDX10_ViewportData*>(viewport->RendererUserData);

    VanVec4 clear_color(0.f, 0.f, 0.f, 1.f);
    bd->pd3dDevice->OMSetRenderTargets(1, &vd->RTView, nullptr);
    if (!(viewport->Flags & VanGuiViewportFlags_NoRendererClear))
        bd->pd3dDevice->ClearRenderTargetView(vd->RTView, reinterpret_cast<const float*>(&clear_color));
    VanGui_ImplDX10_RenderDrawData(viewport->DrawData);
}

static void VanGui_ImplDX10_SwapBuffers(VanGuiViewport* viewport, void*)
{
    auto* vd = static_cast<VanGui_ImplDX10_ViewportData*>(viewport->RendererUserData);
    vd->SwapChain->Present(0, 0);
}

static void VanGui_ImplDX10_InitPlatformInterface()
{
    VanGuiPlatformIO& platform_io          = VanGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow      = VanGui_ImplDX10_CreateWindow;
    platform_io.Renderer_DestroyWindow     = VanGui_ImplDX10_DestroyWindow;
    platform_io.Renderer_SetWindowSize     = VanGui_ImplDX10_SetWindowSize;
    platform_io.Renderer_RenderWindow      = VanGui_ImplDX10_RenderWindow;
    platform_io.Renderer_SwapBuffers       = VanGui_ImplDX10_SwapBuffers;
}

static void VanGui_ImplDX10_ShutdownPlatformInterface()
{
    VanGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
