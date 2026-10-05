// VanGUI: Renderer Backend for modern OpenGL with shaders / programmatic pipeline
// - Desktop GL: 2.x 3.x 4.x
// - Embedded GL: ES 2.0 (WebGL 1.0), ES 3.0 (WebGL 2.0)
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GLuint' OpenGL texture as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [x] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset) [Desktop OpenGL only!]
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).

// About WebGL/ES:
// - You need to '#define VANGUI_IMPL_OPENGL_ES2' or '#define VANGUI_IMPL_OPENGL_ES3' to use WebGL or OpenGL ES.
// - This is done automatically on iOS, Android and Emscripten targets.
// - For other targets, the define needs to be visible from the vangui_impl_opengl3.cpp compilation unit. If unsure, define globally or in vanconfig.h.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// About GLSL version:
//  The 'glsl_version' initialization parameter should be nullptr (default) or a "#version XXX" string.
//  On computer platform the GLSL version default to "#version 130". On OpenGL ES 3 platform it defaults to "#version 300 es"
//  Only override if your GL version doesn't handle this GLSL version. See GLSL version table at the top of vangui_impl_opengl3.cpp.

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplOpenGL3_Init(const char* glsl_version = nullptr);
VANGUI_IMPL_API void     VanGui_ImplOpenGL3_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplOpenGL3_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplOpenGL3_RenderDrawData(VanDrawData* draw_data);

// (Optional) Called by Init/NewFrame/Shutdown
VANGUI_IMPL_API bool     VanGui_ImplOpenGL3_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplOpenGL3_DestroyDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplOpenGL3_UpdateTexture(VanTextureData* tex);

// Configuration flags to add in your vanconfig file:
//#define VANGUI_IMPL_OPENGL_ES2     // Enable ES 2 (Auto-detected on Emscripten)
//#define VANGUI_IMPL_OPENGL_ES3     // Enable ES 3 (Auto-detected on iOS/Android)

// You can explicitly select GLES2 or GLES3 API by using one of the '#define VANGUI_IMPL_OPENGL_LOADER_XXX' in vanconfig.h or compiler command-line.
#if !defined(VANGUI_IMPL_OPENGL_ES2) \
 && !defined(VANGUI_IMPL_OPENGL_ES3)

// Try to detect GLES on matching platforms
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if (defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)) || (defined(__ANDROID__))
#define VANGUI_IMPL_OPENGL_ES3               // iOS, Android  -> GL ES 3, "#version 300 es"
#elif defined(__EMSCRIPTEN__) || defined(__amigaos4__)
#define VANGUI_IMPL_OPENGL_ES2               // Emscripten    -> GL ES 2, "#version 100"
#else
// Otherwise vangui_impl_opengl3_loader.h will be used.
#endif

#endif

#endif // #ifndef VANGUI_DISABLE
