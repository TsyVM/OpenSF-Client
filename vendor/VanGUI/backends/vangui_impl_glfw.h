// VanGUI: Platform Backend for GLFW
// This needs to be used along with a Renderer (e.g. OpenGL3, Vulkan, WebGPU..)
// (Info: GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan graphics context creation, etc.)
// (Requires: GLFW 3.0+. Prefer GLFW 3.3+/3.4+ for full feature support.)

// Implemented features:
//  [X] Platform: Clipboard support.
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen/Pen (Windows only).
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy GLFW_KEY_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Gamepad support. Enable with 'io.ConfigFlags |= VanGuiConfigFlags_NavEnableGamepad'.
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors) with GLFW 3.1+. Resizing cursors requires GLFW 3.4+! Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
//  [X] Multiple VanGUI contexts support.
// Missing features or Issues:
//  [ ] Platform: Touch events are only correctly identified as Touch on Windows. This create issues with some interactions. GLFW doesn't provide a way to identify touch inputs from mouse inputs, we cannot call io.AddMouseSourceEvent() to identify the source. We provide a Windows-specific workaround.
//  [ ] Platform: Missing VanGuiMouseCursor_Wait and VanGuiMouseCursor_Progress cursors.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#pragma once
#include <vangui/vangui.h>      // VANGUI_IMPL_API
#ifndef VANGUI_DISABLE

struct GLFWwindow;
struct GLFWmonitor;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool     VanGui_ImplGlfw_InitForOpenGL(GLFWwindow* window, bool install_callbacks);
VANGUI_IMPL_API bool     VanGui_ImplGlfw_InitForVulkan(GLFWwindow* window, bool install_callbacks);
VANGUI_IMPL_API bool     VanGui_ImplGlfw_InitForOther(GLFWwindow* window, bool install_callbacks);
VANGUI_IMPL_API void     VanGui_ImplGlfw_Shutdown();
VANGUI_IMPL_API void     VanGui_ImplGlfw_NewFrame();

// Emscripten related initialization phase methods (call after VanGui_ImplGlfw_InitForOpenGL)
#ifdef __EMSCRIPTEN__
VANGUI_IMPL_API void     VanGui_ImplGlfw_InstallEmscriptenCallbacks(GLFWwindow* window, const char* canvas_selector);
//static inline void    VanGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback(const char* canvas_selector) { VanGui_ImplGlfw_InstallEmscriptenCallbacks(nullptr, canvas_selector); } } // Renamed in 1.91.0
#endif

// GLFW callbacks install
// - When calling Init with 'install_callbacks=true': VanGui_ImplGlfw_InstallCallbacks() is called. GLFW callbacks will be installed for you. They will chain-call user's previously installed callbacks, if any.
// - When calling Init with 'install_callbacks=false': GLFW callbacks won't be installed. You will need to call individual function yourself from your own GLFW callbacks.
VANGUI_IMPL_API void     VanGui_ImplGlfw_InstallCallbacks(GLFWwindow* window);
VANGUI_IMPL_API void     VanGui_ImplGlfw_RestoreCallbacks(GLFWwindow* window);

// GLFW callbacks options:
// - Set 'chain_for_all_windows=true' to enable chaining callbacks for all windows (including secondary viewports created by backends or by user)
VANGUI_IMPL_API void     VanGui_ImplGlfw_SetCallbacksChainForAllWindows(bool chain_for_all_windows);

// GLFW callbacks (individual callbacks to call yourself if you didn't install callbacks)
VANGUI_IMPL_API void     VanGui_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused);        // Since 1.84
VANGUI_IMPL_API void     VanGui_ImplGlfw_CursorEnterCallback(GLFWwindow* window, int entered);        // Since 1.84
VANGUI_IMPL_API void     VanGui_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y);   // Since 1.87
VANGUI_IMPL_API void     VanGui_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
VANGUI_IMPL_API void     VanGui_ImplGlfw_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
VANGUI_IMPL_API void     VanGui_ImplGlfw_KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
VANGUI_IMPL_API void     VanGui_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int c);
VANGUI_IMPL_API void     VanGui_ImplGlfw_MonitorCallback(GLFWmonitor* monitor, int event);

// GLFW helpers
VANGUI_IMPL_API void     VanGui_ImplGlfw_Sleep(int milliseconds);
VANGUI_IMPL_API float    VanGui_ImplGlfw_GetContentScaleForWindow(GLFWwindow* window);
VANGUI_IMPL_API float    VanGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor* monitor);


#endif // #ifndef VANGUI_DISABLE
