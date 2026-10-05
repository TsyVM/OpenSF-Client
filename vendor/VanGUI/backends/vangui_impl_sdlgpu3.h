// VanGUI: Renderer Backend for SDL_GPU
// This needs to be used along with the SDL3 Platform Backend

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_GPUTexture*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef! **IMPORTANT** Before 2025/08/08, VanTextureID was a reference to a SDL_GPUTextureSamplerBinding struct.
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).

// The aim of vangui_impl_sdlgpu3.h/.cpp is to be usable in your engine without any modification.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// Important note to the reader who wish to integrate vangui_impl_sdlgpu3.cpp/.h in their own engine/app.
// - Unlike other backends, the user must call the function VanGui_ImplSDLGPU_PrepareDrawData BEFORE issuing a SDL_GPURenderPass containing VanGui_ImplSDLGPU_RenderDrawData.
//   Calling the function is MANDATORY, otherwise the VanGui will not upload neither the vertex nor the index buffer for the GPU. See vangui_impl_sdlgpu3.cpp for more info.

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE
#include <SDL3/SDL_gpu.h>

// Initialization data, for VanGui_ImplSDLGPU_Init()
// - Remember to set ColorTargetFormat to the correct format. If you're rendering to the swapchain, call SDL_GetGPUSwapchainTextureFormat() to query the right value
struct VanGui_ImplSDLGPU3_InitInfo
{
    SDL_GPUDevice*              Device                  = nullptr;
    SDL_GPUTextureFormat        ColorTargetFormat       = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampleCount          MSAASamples             = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUSwapchainComposition SwapchainComposition    = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;     // Only used in multi-viewports mode.
    SDL_GPUPresentMode          PresentMode             = SDL_GPU_PRESENTMODE_VSYNC;            // Only used in multi-viewports mode.
};

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplSDLGPU3_Init(VanGui_ImplSDLGPU3_InitInfo* info);
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_PrepareDrawData(VanDrawData* draw_data, SDL_GPUCommandBuffer* command_buffer);
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_RenderDrawData(VanDrawData* draw_data, SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass, SDL_GPUGraphicsPipeline* pipeline = nullptr);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplSDLGPU3_UpdateTexture(VanTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplSDLGPU3_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplSDLGPU3_RenderState
{
    SDL_GPUDevice*      Device;
};

#endif // #ifndef VANGUI_DISABLE
