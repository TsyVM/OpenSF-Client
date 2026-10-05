// VanGUI: Renderer Backend for SDL_Renderer for SDL3
// (Requires: SDL 3.1.8+)

// Note that SDL_Renderer is an _optional_ component of SDL3, which IMHO is now largely obsolete.
// For a multi-platform app consider using other technologies:
// - SDL3+SDL_GPU: SDL_GPU is SDL3 new graphics abstraction API.
// - SDL3+DirectX, SDL3+OpenGL, SDL3+Vulkan: combine SDL with dedicated renderers.
// If your application wants to render any non trivial amount of graphics other than UI,
// please be aware that SDL_Renderer currently offers a limited graphic API to the end-user
// and it might be difficult to step out of those boundaries.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
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

struct SDL_Renderer;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer);
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_RenderDrawData(VanDrawData* draw_data, SDL_Renderer* renderer);

// Called by Init/NewFrame/Shutdown
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplSDLRenderer3_UpdateTexture(VanTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplSDLRenderer3_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplSDLRenderer3_RenderState
{
    SDL_Renderer*       Renderer;
};

#endif // #ifndef VANGUI_DISABLE
