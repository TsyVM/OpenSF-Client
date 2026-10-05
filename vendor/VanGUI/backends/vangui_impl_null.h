// VanGUI: Null Platform+Renderer Backends
// This is designed if you need to use a blind VanGUI context with no input and no output.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

// Follow "Getting Started" link and check examples/ folder to learn about using backends!

// Null = NullPlatform + NullRender
VANGUI_IMPL_API bool     VanGui_ImplNull_Init();
VANGUI_IMPL_API void     VanGui_ImplNull_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplNull_NewFrame();

// Null platform only (single screen, fixed timestep, no inputs)
VANGUI_IMPL_API bool     VanGui_ImplNullPlatform_Init();
VANGUI_IMPL_API void     VanGui_ImplNullPlatform_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplNullPlatform_NewFrame();

// Null renderer only (no output)
VANGUI_IMPL_API bool     VanGui_ImplNullRender_Init();
VANGUI_IMPL_API void     VanGui_ImplNullRender_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplNullRender_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplNullRender_RenderDrawData(VanDrawData* draw_data);

#endif // #ifndef VANGUI_DISABLE
