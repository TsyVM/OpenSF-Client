// VanGUI: Renderer Backend for Metal
// This needs to be used along with a Platform Backend (e.g. OSX)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'MTLTexture' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
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

//-----------------------------------------------------------------------------
// ObjC API
//-----------------------------------------------------------------------------

#ifdef __OBJC__

@class MTLRenderPassDescriptor;
@protocol MTLDevice, MTLCommandBuffer, MTLRenderCommandEncoder;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool VanGui_ImplMetal_Init(id<MTLDevice> device);
VANGUI_IMPL_API void VanGui_ImplMetal_Shutdown();
VANGUI_IMPL_API void VanGui_ImplMetal_NewFrame(MTLRenderPassDescriptor* renderPassDescriptor);
VANGUI_IMPL_API void VanGui_ImplMetal_RenderDrawData(VanDrawData* drawData,
                                                   id<MTLCommandBuffer> commandBuffer,
                                                   id<MTLRenderCommandEncoder> commandEncoder);

// Called by Init/NewFrame/Shutdown
VANGUI_IMPL_API bool VanGui_ImplMetal_CreateDeviceObjects(id<MTLDevice> device);
VANGUI_IMPL_API void VanGui_ImplMetal_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void VanGui_ImplMetal_UpdateTexture(VanTextureData* tex);

#endif

//-----------------------------------------------------------------------------
// C++ API
//-----------------------------------------------------------------------------

// Enable Metal C++ binding support with '#define VANGUI_IMPL_METAL_CPP' in your vanconfig.h file
// More info about using Metal from C++: https://developer.apple.com/metal/cpp/

#ifdef VANGUI_IMPL_METAL_CPP
#include <Metal/Metal.hpp>
#ifndef __OBJC__

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool VanGui_ImplMetal_Init(MTL::Device* device);
VANGUI_IMPL_API void VanGui_ImplMetal_Shutdown();
VANGUI_IMPL_API void VanGui_ImplMetal_NewFrame(MTL::RenderPassDescriptor* renderPassDescriptor);
VANGUI_IMPL_API void VanGui_ImplMetal_RenderDrawData(VanDrawData* draw_data,
                                                   MTL::CommandBuffer* commandBuffer,
                                                   MTL::RenderCommandEncoder* commandEncoder);

// Called by Init/NewFrame/Shutdown
VANGUI_IMPL_API bool VanGui_ImplMetal_CreateDeviceObjects(MTL::Device* device);
VANGUI_IMPL_API void VanGui_ImplMetal_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void VanGui_ImplMetal_UpdateTexture(VanTextureData* tex);

#endif
#endif

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
