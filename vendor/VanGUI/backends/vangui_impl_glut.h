// VanGUI: Platform Backend for GLUT/FreeGLUT
// This needs to be used along with a Renderer (e.g. OpenGL2)

// !!! GLUT/FreeGLUT IS OBSOLETE PREHISTORIC SOFTWARE. Using GLUT is not recommended unless you really miss the 90's. !!!
// !!! If someone or something is teaching you GLUT today, you are being abused. Please show some resistance. !!!
// !!! Nowadays, prefer using GLFW or SDL instead!

// Implemented features:
//  [X] Platform: Partial keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy GLUT values are obsolete since 1.87 and not supported since 1.91.5]
// Missing features or Issues:
//  [ ] Platform: GLUT is unable to distinguish e.g. Backspace from CTRL+H or TAB from CTRL+I
//  [ ] Platform: Missing horizontal mouse wheel support.
//  [ ] Platform: Missing mouse cursor shape/visibility support.
//  [ ] Platform: Missing clipboard support (not supported by Glut).
//  [ ] Platform: Missing gamepad support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#ifndef VANGUI_DISABLE
#include <vangui/vangui.h>      // VANGUI_IMPL_API

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplGLUT_Init();
VANGUI_IMPL_API void     VanGui_ImplGLUT_InstallFuncs();
VANGUI_IMPL_API void     VanGui_ImplGLUT_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplGLUT_NewFrame();

// You can call VanGui_ImplGLUT_InstallFuncs() to get all those functions installed automatically,
// or call them yourself from your own GLUT handlers. We are using the same weird names as GLUT for consistency..
//------------------------------------ GLUT name ---------------------------------------------- Decent Name ---------
VANGUI_IMPL_API void     VanGui_ImplGLUT_ReshapeFunc(int w, int h);                           // ~ ResizeFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_MotionFunc(int x, int y);                            // ~ MouseMoveFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_MouseFunc(int button, int state, int x, int y);      // ~ MouseButtonFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_MouseWheelFunc(int button, int dir, int x, int y);   // ~ MouseWheelFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_KeyboardFunc(unsigned char c, int x, int y);         // ~ CharPressedFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_KeyboardUpFunc(unsigned char c, int x, int y);       // ~ CharReleasedFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_SpecialFunc(int key, int x, int y);                  // ~ KeyPressedFunc
VANGUI_IMPL_API void     VanGui_ImplGLUT_SpecialUpFunc(int key, int x, int y);                // ~ KeyReleasedFunc

#endif // #ifndef VANGUI_DISABLE
