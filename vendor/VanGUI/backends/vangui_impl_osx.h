// VanGUI: Platform Backend for OSX / Cocoa
// This needs to be used along with a Renderer (e.g. OpenGL2, OpenGL3, Vulkan, Metal..)
// - Not well tested. If you want a portable application, prefer using the GLFW or SDL platform Backends on Mac.
// - Requires linking with the GameController framework ("-framework GameController").

// Implemented features:
//  [X] Platform: Clipboard support is part of core VanGUI (no specific code in this backend).
//  [X] Platform: Mouse support. Can discriminate Mouse/Pen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy kVK_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Gamepad support.
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: IME support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

#ifdef __OBJC__

@class NSEvent;
@class NSView;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplOSX_Init(NSView* _Nonnull view);
VANGUI_IMPL_API void     VanGui_ImplOSX_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplOSX_NewFrame(NSView* _Nullable view);

#endif

//-----------------------------------------------------------------------------
// C++ API
//-----------------------------------------------------------------------------

#ifdef VANGUI_IMPL_METAL_CPP_EXTENSIONS
// #include <AppKit/AppKit.hpp>
#ifndef __OBJC__

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplOSX_Init(void* _Nonnull view);
VANGUI_IMPL_API void     VanGui_ImplOSX_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplOSX_NewFrame(void* _Nullable view);

#endif
#endif

#endif // #ifndef VANGUI_DISABLE
