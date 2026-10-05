// VanGUI: Renderer Backend for DirectX9
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'LPDIRECT3DTEXTURE9' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: VANGUI_USE_BGRA_PACKED_COLOR support, as this is the optimal color encoding for DirectX9.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct IDirect3DDevice9;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplDX9_Init(IDirect3DDevice9* device);
VANGUI_IMPL_API void     VanGui_ImplDX9_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplDX9_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplDX9_RenderDrawData(VanDrawData* draw_data);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API bool     VanGui_ImplDX9_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplDX9_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplDX9_UpdateTexture(VanTextureData* tex);

#endif // #ifndef VANGUI_DISABLE
