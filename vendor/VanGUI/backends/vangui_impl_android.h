// VanGUI: Platform Binding for Android native app
// This needs to be used along with the OpenGL 3 Renderer (vangui_impl_opengl3)

// Implemented features:
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy AKEYCODE_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen/Pen.
// Missing features or Issues:
//  [ ] Platform: Clipboard support.
//  [ ] Platform: Gamepad support.
//  [ ] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'. FIXME: Check if this is even possible with Android.
// Important:
//  - Consider using SDL or GLFW backend on Android, which will be more full-featured than this.
//  - FIXME: On-screen keyboard currently needs to be enabled by the application (see examples/ and issue #3446)
//  - FIXME: Unicode character inputs needs to be passed by VanGUI by the application (see examples/ and issue #3446)

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct ANativeWindow;
struct AInputEvent;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplAndroid_Init(ANativeWindow* window);
VANGUI_IMPL_API int32_t  VanGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event);
VANGUI_IMPL_API void     VanGui_ImplAndroid_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplAndroid_NewFrame();

#endif // #ifndef VANGUI_DISABLE
