// VanGUI: Renderer + Platform Backend for Allegro 5
// (Info: Allegro 5 is a cross-platform general purpose library for handling windows, inputs, graphics, etc.)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ALLEGRO_BITMAP*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy ALLEGRO_KEY_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Clipboard support (from Allegro 5.1.12).
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
// Missing features or Issues:
//  [ ] Renderer: The renderer is suboptimal as we need to unindex our buffers and convert vertices manually.
//  [ ] Renderer: Missing support for DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest callbacks: Allegro5 cannot enable/disable LINEAR bitmap flags after creation.
//  [ ] Platform: Missing gamepad support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct ALLEGRO_DISPLAY;
union ALLEGRO_EVENT;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplAllegro5_Init(ALLEGRO_DISPLAY* display);
VANGUI_IMPL_API void     VanGui_ImplAllegro5_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplAllegro5_NewFrame();
VANGUI_IMPL_API void     VanGui_ImplAllegro5_RenderDrawData(VanDrawData* draw_data);
VANGUI_IMPL_API bool     VanGui_ImplAllegro5_ProcessEvent(ALLEGRO_EVENT* event);
VANGUI_IMPL_API void     VanGui_ImplAllegro5_SetDisplay(ALLEGRO_DISPLAY* display);

// Use if you want to reset your rendering device without losing VanGUI state.
VANGUI_IMPL_API bool     VanGui_ImplAllegro5_CreateDeviceObjects();
VANGUI_IMPL_API void     VanGui_ImplAllegro5_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void     VanGui_ImplAllegro5_UpdateTexture(VanTextureData* tex);

#endif // #ifndef VANGUI_DISABLE
