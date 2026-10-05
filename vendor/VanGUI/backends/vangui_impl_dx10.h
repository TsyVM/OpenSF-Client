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

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct ID3D10Device;
struct ID3D10SamplerState;
struct ID3D10Buffer;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplDX10_Init(ID3D10Device* device);
VANGUI_IMPL_API void     VanGui_ImplDX10_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplDX10_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplDX10_RenderDrawData(VanDrawData* draw_data);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API bool     VanGui_ImplDX10_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplDX10_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplDX10_UpdateTexture(VanTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplDX10_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplDX10_RenderState
{
    ID3D10Device*           Device;
    ID3D10Buffer*           VertexConstantBuffer;
    //ID3D10SamplerState*   SamplerLinear;          // Use VanDrawList::AddCallback(VanGui::GetPlatform().DrawCallback_SetSamplerLinear)
    //ID3D10SamplerState*   SamplerNearest;         // Use VanDrawList::AddCallback(VanGui::GetPlatform().DrawCallback_SetSamplerNearest)
};

#endif // #ifndef VANGUI_DISABLE
