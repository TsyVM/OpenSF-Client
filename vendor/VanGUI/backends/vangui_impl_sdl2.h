// VanGUI: Platform Backend for SDL2
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)
// (Info: SDL2 is a cross-platform general purpose library for handling windows, inputs, graphics context creation, etc.)

// Implemented features:
//  [X] Platform: Clipboard support.
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy SDL_SCANCODE_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Gamepad support.
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Basic IME support. App needs to call 'SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");' before SDL_CreateWindow()!.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct SDL_Window;
struct SDL_Renderer;
struct _SDL_GameController;
typedef union SDL_Event SDL_Event;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context);
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForVulkan(SDL_Window* window);
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForD3D(SDL_Window* window);
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForMetal(SDL_Window* window);
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer);
VANGUI_IMPL_API bool     VanGui_ImplSDL2_InitForOther(SDL_Window* window);
VANGUI_IMPL_API void     VanGui_ImplSDL2_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplSDL2_NewFrame();
VANGUI_IMPL_API bool     VanGui_ImplSDL2_ProcessEvent(const SDL_Event* event);

// DPI-related helpers (optional)
VANGUI_IMPL_API float    VanGui_ImplSDL2_GetContentScaleForWindow(SDL_Window* window);
VANGUI_IMPL_API float    VanGui_ImplSDL2_GetContentScaleForDisplay(int display_index);

// Gamepad selection automatically starts in AutoFirst mode, picking first available SDL_Gamepad. You may override this.
// When using manual mode, caller is responsible for opening/closing gamepad.
enum VanGui_ImplSDL2_GamepadMode { VanGui_ImplSDL2_GamepadMode_AutoFirst, VanGui_ImplSDL2_GamepadMode_AutoAll, VanGui_ImplSDL2_GamepadMode_Manual };
VANGUI_IMPL_API void     VanGui_ImplSDL2_SetGamepadMode(VanGui_ImplSDL2_GamepadMode mode, struct _SDL_GameController** manual_gamepads_array = nullptr, int manual_gamepads_count = -1);

// (Advanced, for X11 users) Override Mouse Capture mode. Mouse capture allows receiving updated mouse position after clicking inside our window and dragging outside it.
// Having this 'Enabled' is in theory always better. But, on X11 if you crash/break to debugger while capture is active you may temporarily lose access to your mouse.
// The best solution is to setup your debugger to automatically release capture, e.g. 'setxkbmap -option grab:break_actions && xdotool key XF86Ungrab' or via a GDB script. See #3650.
// But you may independently decide on X11, when a debugger is attached, to set this value to VanGui_ImplSDL2_MouseCaptureMode_Disabled.
enum VanGui_ImplSDL2_MouseCaptureMode { VanGui_ImplSDL2_MouseCaptureMode_Enabled, VanGui_ImplSDL2_MouseCaptureMode_EnabledAfterDrag, VanGui_ImplSDL2_MouseCaptureMode_Disabled };
VANGUI_IMPL_API void     VanGui_ImplSDL2_SetMouseCaptureMode(VanGui_ImplSDL2_MouseCaptureMode mode);

#endif // #ifndef VANGUI_DISABLE
