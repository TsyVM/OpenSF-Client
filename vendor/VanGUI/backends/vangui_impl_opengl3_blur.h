// VanGUI: backdrop blur for the OpenGL 3 renderer backend.
//
// Makes DrawBackdropBlur() (misc/vangui_effects) blur for real: the region behind the
// panel is copied out of the framebuffer being drawn to (glBlitFramebuffer, which also
// resolves a multisampled one), blurred on the GPU (downsampled, separable Gaussian)
// and drawn back inside the panel's rounded rectangle, then the panel's tint goes on
// top. Without this, DrawBackdropBlur() falls back to a denser tint.
//
//   VanGui_ImplOpenGL3_Init(glsl_version);
//   VanGui_ImplOpenGL3_InitBlur(glsl_version);   // same GLSL version string; installs the handler
//   ...
//   VanGui_ImplOpenGL3_ShutdownBlur();           // before VanGui_ImplOpenGL3_Shutdown()
//
// Needs OpenGL 3.0 / GLES 3.0 (framebuffer objects, gl_VertexID) and the effects module
// (VANGUI_ENABLE_EFFECTS); otherwise InitBlur returns false and nothing changes.

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

VANGUI_IMPL_API bool VanGui_ImplOpenGL3_InitBlur(const char* glsl_version = nullptr);
VANGUI_IMPL_API void VanGui_ImplOpenGL3_ShutdownBlur();

#endif // #ifndef VANGUI_DISABLE
