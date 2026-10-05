// VanGUI: Renderer for WebGPU
// This needs to be used along with a Platform Binding (e.g. GLFW, SDL2, SDL3)
// (Please note that WebGPU is a recent API, may not be supported by all browser, and its ecosystem is generally a mess)

// When targeting native platforms:
//  - One of VANGUI_IMPL_WEBGPU_BACKEND_DAWN, VANGUI_IMPL_WEBGPU_BACKEND_WGPU or VANGUI_IMPL_WEBGPU_BACKEND_WGVK *must* be provided.
// When targeting Emscripten:
//  - We now defaults to VANGUI_IMPL_WEBGPU_BACKEND_DAWN and requires Emscripten 4.0.10+, which correspond to using Emscripten '--use-port=emdawnwebgpu'.
//  - Emscripten < 4.0.10 is not supported anymore (old '-sUSE_WEBGPU=1' option).
//  - We can still define VANGUI_IMPL_WEBGPU_BACKEND_WGPU to use Emscripten '-s USE_WEBGPU=1' which is marked as obsolete by Emscripten.
// Add #define to your vanconfig.h file, or as a compilation flag in your build system.
// This requirement may be removed once WebGPU stabilizes and backends converge on a unified interface.
//#define VANGUI_IMPL_WEBGPU_BACKEND_DAWN
//#define VANGUI_IMPL_WEBGPU_BACKEND_WGPU
//#define VANGUI_IMPL_WEBGPU_BACKEND_WGVK

// Implemented features:
//  [X] Renderer: User texture binding. Use 'WGPUTextureView' as VanTextureID. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(VanGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.
//  [X] Renderer: Texture updates support for dynamic font system (VanGuiBackendFlags_RendererHasTextures).

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>          // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

// Setup Emscripten default if not specified.
#if defined(__EMSCRIPTEN__) && !defined(VANGUI_IMPL_WEBGPU_BACKEND_DAWN) && !defined(VANGUI_IMPL_WEBGPU_BACKEND_WGPU)
#include <emscripten/version.h>
#define VANGUI_IMPL_WEBGPU_BACKEND_DAWN
#endif

#include <webgpu/webgpu.h>
#if defined(VANGUI_IMPL_WEBGPU_BACKEND_WGPU) && !defined(__EMSCRIPTEN__)
#include <webgpu/wgpu.h>        // WGPULogLevel
#endif

// Initialization data, for VanGui_ImplWGPU_Init()
struct VanGui_ImplWGPU_InitInfo
{
    WGPUDevice              Device = nullptr;
    int                     NumFramesInFlight = 3;
    WGPUTextureFormat       RenderTargetFormat = WGPUTextureFormat_Undefined;
    WGPUTextureFormat       DepthStencilFormat = WGPUTextureFormat_Undefined;
    WGPUMultisampleState    PipelineMultisampleState = {};

    VanGui_ImplWGPU_InitInfo()
    {
        PipelineMultisampleState.count = 1;
        PipelineMultisampleState.mask = UINT32_MAX;
        PipelineMultisampleState.alphaToCoverageEnabled = false;
    }
};

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool VanGui_ImplWGPU_Init(VanGui_ImplWGPU_InitInfo* init_info);
VANGUI_IMPL_API void VanGui_ImplWGPU_Shutdown();
VANGUI_IMPL_API void VanGui_ImplWGPU_NewFrame();
VANGUI_IMPL_API void VanGui_ImplWGPU_RenderDrawData(VanDrawData* draw_data, WGPURenderPassEncoder pass_encoder);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API bool VanGui_ImplWGPU_CreateDeviceObjects();
VANGUI_IMPL_API void VanGui_ImplWGPU_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void VanGui_ImplWGPU_UpdateTexture(VanTextureData* tex);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplWGPU_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplWGPU_RenderState
{
    WGPUDevice                  Device;
    WGPURenderPassEncoder       RenderPassEncoder;
};

//-------------------------------------------------------------------------
// Internal Helpers
// Those are currently used by our example applications.
//-------------------------------------------------------------------------

// (Optional) Helper to wrap some of the Dawn/WGPU/Emscripten quirks
bool        VanGui_ImplWGPU_IsSurfaceStatusError(WGPUSurfaceGetCurrentTextureStatus status);
bool        VanGui_ImplWGPU_IsSurfaceStatusSubOptimal(WGPUSurfaceGetCurrentTextureStatus status);    // Return whether the texture is suboptimal and may need to be recreated.

// (Optional) Helper for debugging/logging
void        VanGui_ImplWGPU_DebugPrintAdapterInfo(const WGPUAdapter& adapter);
const char* VanGui_ImplWGPU_GetBackendTypeName(WGPUBackendType type);
const char* VanGui_ImplWGPU_GetAdapterTypeName(WGPUAdapterType type);
#if defined(VANGUI_IMPL_WEBGPU_BACKEND_DAWN)
const char* VanGui_ImplWGPU_GetDeviceLostReasonName(WGPUDeviceLostReason type);
const char* VanGui_ImplWGPU_GetErrorTypeName(WGPUErrorType type);
#elif defined(VANGUI_IMPL_WEBGPU_BACKEND_WGPU)
const char* VanGui_ImplWGPU_GetLogLevelName(WGPULogLevel level);
#endif

// (Optional) Helper to create a surface on macOS/Wayland/X11/Window
#ifndef __EMSCRIPTEN__
struct VanGui_ImplWGPU_CreateSurfaceInfo
{
    WGPUInstance    Instance;
    const char*     System;      // "cocoa"   | "wayland"   | "x11"     | "win32"
    void*           RawWindow;   // NSWindow* | 0           | Window    | HWND
    void*           RawDisplay;  // 0         | wl_display* | Display*  | 0
    void*           RawSurface;  //           | wl_surface* | 0         | 0
    void*           RawInstance; // 0         | 0           | 0         | HINSTANCE
};
WGPUSurface VanGui_ImplWGPU_CreateWGPUSurfaceHelper(VanGui_ImplWGPU_CreateSurfaceInfo* info);
#endif // #ifndef __EMSCRIPTEN__

#endif // #ifndef VANGUI_DISABLE
