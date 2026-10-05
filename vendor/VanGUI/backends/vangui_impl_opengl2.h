// VanGUI: Renderer Backend for OpenGL2 (legacy OpenGL, fixed pipeline)
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GLuint' OpenGL texture as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
// Missing features or Issues:
//  [ ] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [ ] Renderer: Use of DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest is emulated by poking to glTexParameter(), as legacy OpenGL doesn't have glBindSampler().

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// **DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL (SHADERS, VBO, VAO, etc.)**
// **Prefer using the code in vangui_impl_opengl3.cpp**
// This code is mostly provided as a reference to learn how VanGui integration works, because it is shorter to read.
// If your code is using GL3+ context or any semi modern OpenGL calls, using this is likely to make everything more
// complicated, will require your code to reset every single OpenGL attributes to their initial state, and might
// confuse your GPU driver.
// The GL2 code is unable to reset attributes or even call e.g. "glUseProgram(0)" because they don't exist in that API.

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplOpenGL2_Init();
VANGUI_IMPL_API void     VanGui_ImplOpenGL2_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplOpenGL2_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplOpenGL2_RenderDrawData(VanDrawData* draw_data);

// Called by Init/NewFrame/Shutdown
VANGUI_IMPL_API bool     VanGui_ImplOpenGL2_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplOpenGL2_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplOpenGL2_UpdateTexture(VanTextureData* tex);

#endif // #ifndef VANGUI_DISABLE
