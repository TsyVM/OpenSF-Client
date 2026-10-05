// VanGUI: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(VanGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11Buffer;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context);
VANGUI_IMPL_API void     VanGui_ImplDX11_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplDX11_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplDX11_RenderDrawData(VanDrawData* draw_data);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API bool     VanGui_ImplDX11_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplDX11_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplDX11_UpdateTexture(VanTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplDX11_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplDX11_RenderState
{
    ID3D11Device*           Device;
    ID3D11DeviceContext*    DeviceContext;
    ID3D11Buffer*           VertexConstantBuffer;
    //ID3D11SamplerState*   SamplerLinear;          // Use VanDrawList::AddCallback(VanGui::GetPlatform().DrawCallback_SetSamplerLinear)
    //ID3D11SamplerState*   SamplerNearest;         // Use VanDrawList::AddCallback(VanGui::GetPlatform().DrawCallback_SetSamplerNearest)
};

#endif // #ifndef VANGUI_DISABLE
