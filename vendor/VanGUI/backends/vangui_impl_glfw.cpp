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

// About Emscripten support:
// - Emscripten provides its own GLFW (3.2.1) implementation (syntax: "-sUSE_GLFW=3"), but Joystick is broken and several features are not supported (multiple windows, clipboard, timer, etc.)
// - A third-party Emscripten GLFW (3.4.0) implementation (syntax: "--use-port=contrib.glfw3") fixes the Joystick issue and implements all relevant features for the browser.
// See https://github.com/pongasoft/emscripten-glfw/blob/master/docs/Comparison.md for details.

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-21: Added a Win32-specific implementation of VanGui_ImplGlfw_GetContentScaleXXXX functions for legacy GLFW 3.2.
//  2026-03-25: Mouse cursor is properly restored if changed by user app/code while using glfwSetInputMode(..., GLFW_CURSOR_DISABLED) or VanGuiConfigFlags_NoMouseCursorChange. Amend change from 2025-12-10.
//  2026-02-10: Try to set VANGUI_IMPL_GLFW_DISABLE_X11 / VANGUI_IMPL_GLFW_DISABLE_WAYLAND automatically if corresponding headers are not accessible. (#9225)
//  2025-12-12: Added VANGUI_IMPL_GLFW_DISABLE_X11 / VANGUI_IMPL_GLFW_DISABLE_WAYLAND to forcefully disable either.
//  2025-12-10: Avoid repeated glfwSetCursor()/glfwSetInputMode() calls when unnecessary. Lowers overhead for very high framerates (e.g. 10k+ FPS).
//  2025-11-06: Lower minimum requirement to GLFW 3.0. Though a recent version e.g GLFW 3.4 is highly recommended.
//  2025-09-18: Call platform_io.ClearPlatformHandlers() on shutdown.
//  2025-09-15: Content Scales are always reported as 1.0 on Wayland. FramebufferScale are always reported as 1.0 on X11. (#8920, #8921)
//  2025-07-08: Made VanGui_ImplGlfw_GetContentScaleForWindow(), VanGui_ImplGlfw_GetContentScaleForMonitor() helpers return 1.0f on Emscripten and Android platforms, matching macOS logic. (#8742, #8733)
//  2025-06-18: Added support for multiple VanGUI contexts. (#8676, #8239, #8069)
//  2025-06-11: Added VanGui_ImplGlfw_GetContentScaleForWindow(GLFWwindow* window) and VanGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor* monitor) helper to facilitate making DPI-aware apps.
//  2025-03-10: Map GLFW_KEY_WORLD_1 and GLFW_KEY_WORLD_2 into VanGuiKey_Oem102.
//  2025-03-03: Fixed clipboard handler assertion when using GLFW <= 3.2.1 compiled with asserts enabled.
//  2024-08-22: Moved some OS/backend related function pointers from VanGuiIO to VanGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//               - io.PlatformOpenInShellFn -> platform_io.Platform_OpenInShellFn
//  2024-07-31: Added VanGui_ImplGlfw_Sleep() helper function for usage by our examples app, since GLFW doesn't provide one.
//  2024-07-08: *BREAKING* Renamed VanGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback to VanGui_ImplGlfw_InstallEmscriptenCallbacks(), added GLFWWindow* parameter.
//  2024-07-08: Emscripten: Added support for GLFW3 contrib port (GLFW 3.4.0 features + bug fixes): to enable, replace -sUSE_GLFW=3 with --use-port=contrib.glfw3 (requires emscripten 3.1.59+) (https://github.com/pongasoft/emscripten-glfw)
//  2024-07-02: Emscripten: Added io.PlatformOpenInShellFn() handler for Emscripten versions.
//  2023-12-19: Emscripten: Added VanGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback() to register canvas selector and auto-resize GLFW window.
//  2023-10-05: Inputs: Added support for extra VanGuiKey values: F13 to F24 function keys.
//  2023-07-18: Inputs: Revert ignoring mouse data on GLFW_CURSOR_DISABLED as it can be used differently. User may set VanGuiConfigFLags_NoMouse if desired. (#5625, #6609)
//  2023-06-12: Accept glfwGetTime() not returning a monotonically increasing value. This seems to happens on some Windows setup when peripherals disconnect, and is likely to also happen on browser + Emscripten. (#6491)
//  2023-04-04: Inputs: Added support for io.AddMouseSourceEvent() to discriminate VanGuiMouseSource_Mouse/VanGuiMouseSource_TouchScreen/VanGuiMouseSource_Pen on Windows ONLY, using a custom WndProc hook. (#2702)
//  2023-03-16: Inputs: Fixed key modifiers handling on secondary viewports (docking branch). Broken on 2023/01/04. (#6248, #6034)
//  2023-03-14: Emscripten: Avoid using glfwGetError() and glfwGetGamepadState() which are not correctly implemented in Emscripten emulation. (#6240)
//  2023-02-03: Emscripten: Registering custom low-level mouse wheel handler to get more accurate scrolling impulses on Emscripten. (#4019, #6096)
//  2023-01-04: Inputs: Fixed mods state on Linux when using Alt-GR text input (e.g. German keyboard layout), could lead to broken text input. Revert a 2022/01/17 change were we resumed using mods provided by GLFW, turns out they were faulty.
//  2022-11-22: Perform a dummy glfwGetError() read to cancel missing names with glfwGetKeyName(). (#5908)
//  2022-10-18: Perform a dummy glfwGetError() read to cancel missing mouse cursors errors. Using GLFW_VERSION_COMBINED directly. (#5785)
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-09-01: Inputs: Honor GLFW_CURSOR_DISABLED by not setting mouse position *EDIT* Reverted 2023-07-18.
//  2022-04-30: Inputs: Fixed VanGui_ImplGlfw_TranslateUntranslatedKey() for lower case letters on OSX.
//  2022-03-23: Inputs: Fixed a regression in 1.87 which resulted in keyboard modifiers events being reported incorrectly on Linux/X11.
//  2022-02-07: Added VanGui_ImplGlfw_InstallCallbacks()/VanGui_ImplGlfw_RestoreCallbacks() helpers to facilitate user installing callbacks after initializing backend.
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2021-01-20: Inputs: calling new io.AddKeyAnalogEvent() for gamepad support, instead of writing directly to io.NavInputs[].
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always update key mods next and before key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-12: *BREAKING CHANGE*: Now using glfwSetCursorPosCallback(). If you called VanGui_ImplGlfw_InitXXX() with install_callbacks = false, you MUST install glfwSetCursorPosCallback() and forward it to the backend via VanGui_ImplGlfw_CursorPosCallback().
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2022-01-05: Inputs: Converting GLFW untranslated keycodes back to translated keycodes (in the VanGui_ImplGlfw_KeyCallback() function) in order to match the behavior of every other backend, and facilitate the use of GLFW with lettered-shortcuts API.
//  2021-08-17: *BREAKING CHANGE*: Now using glfwSetWindowFocusCallback() to calling io.AddFocusEvent(). If you called VanGui_ImplGlfw_InitXXX() with install_callbacks = false, you MUST install glfwSetWindowFocusCallback() and forward it to the backend via VanGui_ImplGlfw_WindowFocusCallback().
//  2021-07-29: *BREAKING CHANGE*: Now using glfwSetCursorEnterCallback(). MousePos is correctly reported when the host platform window is hovered but not focused. If you called VanGui_ImplGlfw_InitXXX() with install_callbacks = false, you MUST install glfwSetWindowFocusCallback() callback and forward it to the backend via VanGui_ImplGlfw_CursorEnterCallback().
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2020-01-17: Inputs: Disable error callback while assigning mouse cursors because some X11 setup don't have them and it generates errors.
//  2019-12-05: Inputs: Added support for new mouse cursors added in GLFW 3.4+ (resizing cursors, not allowed cursor).
//  2019-10-18: Misc: Previously installed user callbacks are now restored on shutdown.
//  2019-07-21: Inputs: Added mapping for VanGuiKey_KeyPadEnter.
//  2019-05-11: Inputs: Don't filter value from character callback before calling AddInputCharacter().
//  2019-03-12: Misc: Preserve DisplayFramebufferScale when main window is minimized.
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-11-07: Inputs: When installing our GLFW callbacks, we save user's previously installed ones - if any - and chain call them.
//  2018-08-01: Inputs: Workaround for Emscripten which doesn't seem to handle focus related calls.
//  2018-06-29: Inputs: Added support for the VanGuiMouseCursor_Hand cursor.
//  2018-06-08: Misc: Extracted vangui_impl_glfw.cpp/.h away from the old combined GLFW+OpenGL/Vulkan examples.
//  2018-03-20: Misc: Setup io.BackendFlags VanGuiBackendFlags_HasMouseCursors flag + honor VanGuiConfigFlags_NoMouseCursorChange flag.
//  2018-02-20: Inputs: Added support for mouse cursors (VanGui::GetMouseCursor() value, passed to glfwSetCursor()).
//  2018-02-06: Misc: Removed call to VanGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-02-06: Inputs: Added mapping for VanGuiKey_Space.
//  2018-01-25: Inputs: Added gamepad support if VanGuiConfigFlags_NavEnableGamepad is set.
//  2018-01-25: Inputs: Honoring the io.WantSetMousePos by repositioning the mouse (when using navigation and VanGuiConfigFlags_NavMoveMouse is set).
//  2018-01-20: Inputs: Added Horizontal Mouse Wheel support.
//  2018-01-18: Inputs: Added mapping for VanGuiKey_Insert.
//  2017-08-25: Inputs: MousePos set to -FLT_MAX,-FLT_MAX when mouse is unavailable/missing (instead of -1,-1).
//  2016-10-15: Misc: Added a void* user_data parameter to Clipboard function handlers.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_glfw.h"

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"         // warning: use of old-style cast
#pragma clang diagnostic ignored "-Wsign-conversion"        // warning: implicit conversion changes signedness
#pragma clang diagnostic ignored "-Wexit-time-destructors"  // warning: declaration requires an exit-time destructor     // exit-time destruction order is undefined. if MemFree() leads to users code that has been disabled before exit it might cause problems. VanGui coding style welcomes static/globals.
#pragma clang diagnostic ignored "-Wglobal-constructors"    // warning: declaration requires a global destructor         // similar to above, not sure what the exact difference is.
#endif

#if defined(__has_include)
#if !__has_include(<X11/Xlib.h>) || !__has_include(<X11/extensions/Xrandr.h>)
#define VANGUI_IMPL_GLFW_DISABLE_X11
#endif
#if !__has_include(<wayland-client.h>)
#define VANGUI_IMPL_GLFW_DISABLE_WAYLAND
#endif
#endif

// GLFW
#if !defined(VANGUI_IMPL_GLFW_DISABLE_X11) && (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
#define GLFW_HAS_X11        1
#else
#define GLFW_HAS_X11        0
#endif
#if !defined(VANGUI_IMPL_GLFW_DISABLE_WAYLAND) && (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__))
#define GLFW_HAS_WAYLAND    1
#else
#define GLFW_HAS_WAYLAND    0
#endif
#include <GLFW/glfw3.h>
#ifdef _WIN32
#undef APIENTRY
#ifndef GLFW_EXPOSE_NATIVE_WIN32    // for glfwGetWin32Window()
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
#ifndef GLFW_EXPOSE_NATIVE_COCOA    // for glfwGetCocoaWindow()
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#elif GLFW_HAS_X11
#ifndef GLFW_EXPOSE_NATIVE_X11      // for glfwGetX11Display(), glfwGetX11Window() on Freedesktop (Linux, BSD, etc.)
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>
#endif
#undef Status                   // X11 headers are leaking this.
#ifndef _WIN32
#include <unistd.h>             // for usleep()
#endif
#include <cstdio>              // for snprintf()

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#ifdef EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3
#include <GLFW/emscripten_glfw3.h>
#else
#define EMSCRIPTEN_USE_EMBEDDED_GLFW3
#endif
#endif

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#define GLFW_HAS_CREATECURSOR           (GLFW_VERSION_COMBINED >= 3100) // 3.1+ glfwCreateCursor()
#define GLFW_HAS_PER_MONITOR_DPI        (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetMonitorContentScale
#ifdef GLFW_RESIZE_NESW_CURSOR          // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
#define GLFW_HAS_NEW_CURSORS            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
#define GLFW_HAS_NEW_CURSORS            (0)
#endif
#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()
#define GLFW_HAS_GETERROR               (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetError()
#define GLFW_HAS_GETPLATFORM            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ glfwGetPlatform()

// Map GLFWWindow* to VanGuiContext*.
// - Would be simpler if we could use glfwSetWindowUserPointer()/glfwGetWindowUserPointer(), but this is a single and shared resource.
// - Would be simpler if we could use e.g. std::map<> as well. But we don't.
// - This is not particularly optimized as we expect size to be small and queries to be rare.
struct VanGui_ImplGlfw_WindowToContext { GLFWwindow* Window; VanGuiContext* Context; };
static VanVector<VanGui_ImplGlfw_WindowToContext> g_ContextMap;
static void VanGui_ImplGlfw_ContextMap_Add(GLFWwindow* window, VanGuiContext* ctx) { g_ContextMap.push_back(VanGui_ImplGlfw_WindowToContext{ window, ctx }); }
static void VanGui_ImplGlfw_ContextMap_Remove(GLFWwindow* window)                 { for (VanGui_ImplGlfw_WindowToContext& entry : g_ContextMap) if (entry.Window == window) { g_ContextMap.erase_unsorted(&entry); if (g_ContextMap.empty()) g_ContextMap.clear(); return; } }
static VanGuiContext* VanGui_ImplGlfw_ContextMap_Get(GLFWwindow* window)           { for (VanGui_ImplGlfw_WindowToContext& entry : g_ContextMap) if (entry.Window == window) return entry.Context; return nullptr; }

enum GlfwClientApi
{
    GlfwClientApi_OpenGL,
    GlfwClientApi_Vulkan,
    GlfwClientApi_Unknown,  // Anything else fits here.
};

// GLFW data
struct VanGui_ImplGlfw_Data
{
    VanGuiContext*           Context                         = nullptr;
    GLFWwindow*             Window                          = nullptr;
    GlfwClientApi           ClientApi                       = GlfwClientApi_Unknown;
    double                  Time                            = 0.0;
    GLFWwindow*             MouseWindow                     = nullptr;
#if GLFW_HAS_CREATECURSOR
    GLFWcursor*             MouseCursors[VanGuiMouseCursor_COUNT] = {};
    GLFWcursor*             LastMouseCursor                 = nullptr;
#endif
    VanVec2                  LastValidMousePos               = {};
    bool                    IsWayland                       = false;
    bool                    InstalledCallbacks               = false;
    bool                    CallbacksChainForAllWindows      = false;
    bool                    WantUpdateMonitors               = true;
    char                    BackendPlatformName[32]          = {};
#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
    const char*             CanvasSelector                   = nullptr;
#endif

    // Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
    GLFWwindowfocusfun      PrevUserCallbackWindowFocus      = nullptr;
    GLFWcursorposfun        PrevUserCallbackCursorPos        = nullptr;
    GLFWcursorenterfun      PrevUserCallbackCursorEnter      = nullptr;
    GLFWmousebuttonfun      PrevUserCallbackMousebutton      = nullptr;
    GLFWscrollfun           PrevUserCallbackScroll           = nullptr;
    GLFWkeyfun              PrevUserCallbackKey              = nullptr;
    GLFWcharfun             PrevUserCallbackChar             = nullptr;
    GLFWmonitorfun          PrevUserCallbackMonitor          = nullptr;
#ifdef _WIN32
    WNDPROC                 PrevWndProc                      = nullptr;
#endif
};

struct VanGui_ImplGlfw_ViewportData
{
    GLFWwindow* Window      = nullptr;
    bool        WindowOwned = false;
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// - Because glfwPollEvents() process all windows and some events may be called outside of it, you will need to register your own callbacks
//   (passing install_callbacks=false in VanGui_ImplGlfw_InitXXX functions), set the current VanGUI context and then call our callbacks.
// - Otherwise we may need to store a GLFWWindow* -> VanGuiContext* map and handle this in the backend, adding a little bit of extra complexity to it.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
namespace VanGui { extern VanGuiIO& GetIO(VanGuiContext*); }
static VanGui_ImplGlfw_Data* VanGui_ImplGlfw_GetBackendData()
{
    // Get data for current context
    return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplGlfw_Data*>(VanGui::GetIO().BackendPlatformUserData) : nullptr;
}
static VanGui_ImplGlfw_Data* VanGui_ImplGlfw_GetBackendData(GLFWwindow* window)
{
    // Get data for a given GLFW window, regardless of current context (since GLFW events are sent together)
    VanGuiContext* ctx = VanGui_ImplGlfw_ContextMap_Get(window);
    return static_cast<VanGui_ImplGlfw_Data*>(VanGui::GetIO(ctx).BackendPlatformUserData);
}

// Functions
static bool VanGui_ImplGlfw_IsWayland()
{
#if !GLFW_HAS_WAYLAND
    return false;
#elif GLFW_HAS_GETPLATFORM
    return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
    const char* version = glfwGetVersionString();
    if (strstr(version, "Wayland") == nullptr) // e.g. Ubuntu 22.04 ships with GLFW 3.3.6 compiled without Wayland
        return false;
#ifdef GLFW_EXPOSE_NATIVE_X11
    if (glfwGetX11Display() != nullptr)
        return false;
#endif
    return true;
#endif
}

// Not static to allow third-party code to use that if they want to (but undocumented)
VanGuiKey VanGui_ImplGlfw_KeyToVanGuiKey(int keycode, int scancode);
VanGuiKey VanGui_ImplGlfw_KeyToVanGuiKey(int keycode, int scancode)
{
    VAN_UNUSED(scancode);
    switch (keycode)
    {
        case GLFW_KEY_TAB: return VanGuiKey_Tab;
        case GLFW_KEY_LEFT: return VanGuiKey_LeftArrow;
        case GLFW_KEY_RIGHT: return VanGuiKey_RightArrow;
        case GLFW_KEY_UP: return VanGuiKey_UpArrow;
        case GLFW_KEY_DOWN: return VanGuiKey_DownArrow;
        case GLFW_KEY_PAGE_UP: return VanGuiKey_PageUp;
        case GLFW_KEY_PAGE_DOWN: return VanGuiKey_PageDown;
        case GLFW_KEY_HOME: return VanGuiKey_Home;
        case GLFW_KEY_END: return VanGuiKey_End;
        case GLFW_KEY_INSERT: return VanGuiKey_Insert;
        case GLFW_KEY_DELETE: return VanGuiKey_Delete;
        case GLFW_KEY_BACKSPACE: return VanGuiKey_Backspace;
        case GLFW_KEY_SPACE: return VanGuiKey_Space;
        case GLFW_KEY_ENTER: return VanGuiKey_Enter;
        case GLFW_KEY_ESCAPE: return VanGuiKey_Escape;
        case GLFW_KEY_APOSTROPHE: return VanGuiKey_Apostrophe;
        case GLFW_KEY_COMMA: return VanGuiKey_Comma;
        case GLFW_KEY_MINUS: return VanGuiKey_Minus;
        case GLFW_KEY_PERIOD: return VanGuiKey_Period;
        case GLFW_KEY_SLASH: return VanGuiKey_Slash;
        case GLFW_KEY_SEMICOLON: return VanGuiKey_Semicolon;
        case GLFW_KEY_EQUAL: return VanGuiKey_Equal;
        case GLFW_KEY_LEFT_BRACKET: return VanGuiKey_LeftBracket;
        case GLFW_KEY_BACKSLASH: return VanGuiKey_Backslash;
        case GLFW_KEY_WORLD_1: return VanGuiKey_Oem102;
        case GLFW_KEY_WORLD_2: return VanGuiKey_Oem102;
        case GLFW_KEY_RIGHT_BRACKET: return VanGuiKey_RightBracket;
        case GLFW_KEY_GRAVE_ACCENT: return VanGuiKey_GraveAccent;
        case GLFW_KEY_CAPS_LOCK: return VanGuiKey_CapsLock;
        case GLFW_KEY_SCROLL_LOCK: return VanGuiKey_ScrollLock;
        case GLFW_KEY_NUM_LOCK: return VanGuiKey_NumLock;
        case GLFW_KEY_PRINT_SCREEN: return VanGuiKey_PrintScreen;
        case GLFW_KEY_PAUSE: return VanGuiKey_Pause;
        case GLFW_KEY_KP_0: return VanGuiKey_Keypad0;
        case GLFW_KEY_KP_1: return VanGuiKey_Keypad1;
        case GLFW_KEY_KP_2: return VanGuiKey_Keypad2;
        case GLFW_KEY_KP_3: return VanGuiKey_Keypad3;
        case GLFW_KEY_KP_4: return VanGuiKey_Keypad4;
        case GLFW_KEY_KP_5: return VanGuiKey_Keypad5;
        case GLFW_KEY_KP_6: return VanGuiKey_Keypad6;
        case GLFW_KEY_KP_7: return VanGuiKey_Keypad7;
        case GLFW_KEY_KP_8: return VanGuiKey_Keypad8;
        case GLFW_KEY_KP_9: return VanGuiKey_Keypad9;
        case GLFW_KEY_KP_DECIMAL: return VanGuiKey_KeypadDecimal;
        case GLFW_KEY_KP_DIVIDE: return VanGuiKey_KeypadDivide;
        case GLFW_KEY_KP_MULTIPLY: return VanGuiKey_KeypadMultiply;
        case GLFW_KEY_KP_SUBTRACT: return VanGuiKey_KeypadSubtract;
        case GLFW_KEY_KP_ADD: return VanGuiKey_KeypadAdd;
        case GLFW_KEY_KP_ENTER: return VanGuiKey_KeypadEnter;
        case GLFW_KEY_KP_EQUAL: return VanGuiKey_KeypadEqual;
        case GLFW_KEY_LEFT_SHIFT: return VanGuiKey_LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return VanGuiKey_LeftCtrl;
        case GLFW_KEY_LEFT_ALT: return VanGuiKey_LeftAlt;
        case GLFW_KEY_LEFT_SUPER: return VanGuiKey_LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT: return VanGuiKey_RightShift;
        case GLFW_KEY_RIGHT_CONTROL: return VanGuiKey_RightCtrl;
        case GLFW_KEY_RIGHT_ALT: return VanGuiKey_RightAlt;
        case GLFW_KEY_RIGHT_SUPER: return VanGuiKey_RightSuper;
        case GLFW_KEY_MENU: return VanGuiKey_Menu;
        case GLFW_KEY_0: return VanGuiKey_0;
        case GLFW_KEY_1: return VanGuiKey_1;
        case GLFW_KEY_2: return VanGuiKey_2;
        case GLFW_KEY_3: return VanGuiKey_3;
        case GLFW_KEY_4: return VanGuiKey_4;
        case GLFW_KEY_5: return VanGuiKey_5;
        case GLFW_KEY_6: return VanGuiKey_6;
        case GLFW_KEY_7: return VanGuiKey_7;
        case GLFW_KEY_8: return VanGuiKey_8;
        case GLFW_KEY_9: return VanGuiKey_9;
        case GLFW_KEY_A: return VanGuiKey_A;
        case GLFW_KEY_B: return VanGuiKey_B;
        case GLFW_KEY_C: return VanGuiKey_C;
        case GLFW_KEY_D: return VanGuiKey_D;
        case GLFW_KEY_E: return VanGuiKey_E;
        case GLFW_KEY_F: return VanGuiKey_F;
        case GLFW_KEY_G: return VanGuiKey_G;
        case GLFW_KEY_H: return VanGuiKey_H;
        case GLFW_KEY_I: return VanGuiKey_I;
        case GLFW_KEY_J: return VanGuiKey_J;
        case GLFW_KEY_K: return VanGuiKey_K;
        case GLFW_KEY_L: return VanGuiKey_L;
        case GLFW_KEY_M: return VanGuiKey_M;
        case GLFW_KEY_N: return VanGuiKey_N;
        case GLFW_KEY_O: return VanGuiKey_O;
        case GLFW_KEY_P: return VanGuiKey_P;
        case GLFW_KEY_Q: return VanGuiKey_Q;
        case GLFW_KEY_R: return VanGuiKey_R;
        case GLFW_KEY_S: return VanGuiKey_S;
        case GLFW_KEY_T: return VanGuiKey_T;
        case GLFW_KEY_U: return VanGuiKey_U;
        case GLFW_KEY_V: return VanGuiKey_V;
        case GLFW_KEY_W: return VanGuiKey_W;
        case GLFW_KEY_X: return VanGuiKey_X;
        case GLFW_KEY_Y: return VanGuiKey_Y;
        case GLFW_KEY_Z: return VanGuiKey_Z;
        case GLFW_KEY_F1: return VanGuiKey_F1;
        case GLFW_KEY_F2: return VanGuiKey_F2;
        case GLFW_KEY_F3: return VanGuiKey_F3;
        case GLFW_KEY_F4: return VanGuiKey_F4;
        case GLFW_KEY_F5: return VanGuiKey_F5;
        case GLFW_KEY_F6: return VanGuiKey_F6;
        case GLFW_KEY_F7: return VanGuiKey_F7;
        case GLFW_KEY_F8: return VanGuiKey_F8;
        case GLFW_KEY_F9: return VanGuiKey_F9;
        case GLFW_KEY_F10: return VanGuiKey_F10;
        case GLFW_KEY_F11: return VanGuiKey_F11;
        case GLFW_KEY_F12: return VanGuiKey_F12;
        case GLFW_KEY_F13: return VanGuiKey_F13;
        case GLFW_KEY_F14: return VanGuiKey_F14;
        case GLFW_KEY_F15: return VanGuiKey_F15;
        case GLFW_KEY_F16: return VanGuiKey_F16;
        case GLFW_KEY_F17: return VanGuiKey_F17;
        case GLFW_KEY_F18: return VanGuiKey_F18;
        case GLFW_KEY_F19: return VanGuiKey_F19;
        case GLFW_KEY_F20: return VanGuiKey_F20;
        case GLFW_KEY_F21: return VanGuiKey_F21;
        case GLFW_KEY_F22: return VanGuiKey_F22;
        case GLFW_KEY_F23: return VanGuiKey_F23;
        case GLFW_KEY_F24: return VanGuiKey_F24;
        default: return VanGuiKey_None;
    }
}

// X11 does not include current pressed/released modifier key in 'mods' flags submitted by GLFW
// See https://github.com/glfw/glfw/issues/1630
static void VanGui_ImplGlfw_UpdateKeyModifiers(VanGuiIO& io, GLFWwindow* window)
{
    io.AddKeyEvent(VanGuiMod_Ctrl,  (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
    io.AddKeyEvent(VanGuiMod_Shift, (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)   == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT)   == GLFW_PRESS));
    io.AddKeyEvent(VanGuiMod_Alt,   (glfwGetKey(window, GLFW_KEY_LEFT_ALT)     == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_ALT)     == GLFW_PRESS));
    io.AddKeyEvent(VanGuiMod_Super, (glfwGetKey(window, GLFW_KEY_LEFT_SUPER)   == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER)   == GLFW_PRESS));
}

static bool VanGui_ImplGlfw_ShouldChainCallback(VanGui_ImplGlfw_Data* bd, GLFWwindow* window)
{
    return bd->CallbacksChainForAllWindows ? true : (window == bd->Window);
}

void VanGui_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackMousebutton != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackMousebutton(window, button, action, mods);

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    VanGui_ImplGlfw_UpdateKeyModifiers(io, window);
    if (button >= 0 && button < VanGuiMouseButton_COUNT)
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);
}

void VanGui_ImplGlfw_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackScroll != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackScroll(window, xoffset, yoffset);

#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
    // Ignore GLFW events: will be processed in VanGui_ImplEmscripten_WheelCallback().
    return;
#endif

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    io.AddMouseWheelEvent(static_cast<float>(xoffset), static_cast<float>(yoffset));
}

// FIXME: should this be baked into VanGui_ImplGlfw_KeyToVanGuiKey()? then what about the values passed to io.SetKeyEventNativeData()?
static int VanGui_ImplGlfw_TranslateUntranslatedKey(int key, int scancode)
{
#if GLFW_HAS_GETKEYNAME && !defined(EMSCRIPTEN_USE_EMBEDDED_GLFW3)
    // GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
    // (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
    // See https://github.com/glfw/glfw/issues/1502 for details.
    // Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
    // This won't cover edge cases but this is at least going to cover common cases.
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
        return key;
    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
    const char* key_name = glfwGetKeyName(key, scancode);
    glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(EMSCRIPTEN_USE_EMBEDDED_GLFW3) // Eat errors (see #5908)
    (void)glfwGetError(nullptr);
#endif
    if (key_name && key_name[0] != 0 && key_name[1] == 0)
    {
        const char char_names[] = "`-=[]\\,;\'./";
        const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
        VAN_ASSERT(VAN_COUNTOF(char_names) == VAN_COUNTOF(char_keys));
        if (key_name[0] >= '0' && key_name[0] <= '9')               { key = GLFW_KEY_0 + (key_name[0] - '0'); }
        else if (key_name[0] >= 'A' && key_name[0] <= 'Z')          { key = GLFW_KEY_A + (key_name[0] - 'A'); }
        else if (key_name[0] >= 'a' && key_name[0] <= 'z')          { key = GLFW_KEY_A + (key_name[0] - 'a'); }
        else if (const char* p = strchr(char_names, key_name[0]))   { key = char_keys[p - char_names]; }
    }
    // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
    VAN_UNUSED(scancode);
#endif
    return key;
}

void VanGui_ImplGlfw_KeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackKey != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackKey(window, keycode, scancode, action, mods);

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    VanGui_ImplGlfw_UpdateKeyModifiers(io, window);

    keycode = VanGui_ImplGlfw_TranslateUntranslatedKey(keycode, scancode);

    VanGuiKey vangui_key = VanGui_ImplGlfw_KeyToVanGuiKey(keycode, scancode);
    io.AddKeyEvent(vangui_key, (action == GLFW_PRESS));
    io.SetKeyEventNativeData(vangui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)
}

void VanGui_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackWindowFocus != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackWindowFocus(window, focused);

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    io.AddFocusEvent(focused != 0);
}

void VanGui_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackCursorPos != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackCursorPos(window, x, y);

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    bd->LastValidMousePos = VanVec2(static_cast<float>(x), static_cast<float>(y));
}

// Workaround: X11 seems to send spurious Leave/Enter events which would make us lose our position,
// so we back it up and restore on Leave/Enter
void VanGui_ImplGlfw_CursorEnterCallback(GLFWwindow* window, int entered)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackCursorEnter != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackCursorEnter(window, entered);

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    if (entered)
    {
        bd->MouseWindow = window;
        io.AddMousePosEvent(bd->LastValidMousePos.x, bd->LastValidMousePos.y);
    }
    else if (!entered && bd->MouseWindow == window)
    {
        bd->LastValidMousePos = io.MousePos;
        bd->MouseWindow = nullptr;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

void VanGui_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int c)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (bd == nullptr) [[unlikely]] return;
    if (bd->PrevUserCallbackChar != nullptr && VanGui_ImplGlfw_ShouldChainCallback(bd, window))
        bd->PrevUserCallbackChar(window, c);

    VanGuiIO& io = VanGui::GetIO(bd->Context);
    io.AddInputCharacter(c);
}

static void VanGui_ImplGlfw_UpdateMonitors()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Monitors.resize(0);
    int monitors_count = 0;
    GLFWmonitor** glfw_monitors = glfwGetMonitors(&monitors_count);
    for (int i = 0; i < monitors_count; i++)
    {
        VanGuiPlatformMonitor monitor = {};
        int x, y;
        glfwGetMonitorPos(glfw_monitors[i], &x, &y);
        const GLFWvidmode* vid_mode = glfwGetVideoMode(glfw_monitors[i]);
        monitor.MainPos  = VanVec2(static_cast<float>(x), static_cast<float>(y));
        monitor.MainSize = VanVec2(static_cast<float>(vid_mode->width), static_cast<float>(vid_mode->height));
        int wx, wy, ww, wh;
        glfwGetMonitorWorkarea(glfw_monitors[i], &wx, &wy, &ww, &wh);
        monitor.WorkPos  = VanVec2(static_cast<float>(wx), static_cast<float>(wy));
        monitor.WorkSize = VanVec2(static_cast<float>(ww), static_cast<float>(wh));
        monitor.DpiScale = VanGui_ImplGlfw_GetContentScaleForMonitor(glfw_monitors[i]);
        monitor.PlatformHandle = static_cast<void*>(glfw_monitors[i]);
        platform_io.Monitors.push_back(monitor);
    }
}

void VanGui_ImplGlfw_MonitorCallback(GLFWmonitor*, int)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    if (bd != nullptr)
        bd->WantUpdateMonitors = true;
}

#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
static EM_BOOL VanGui_ImplEmscripten_WheelCallback(int, const EmscriptenWheelEvent* ev, void* user_data)
{
    // Mimic Emscripten_HandleWheel() in SDL.
    // Corresponding equivalent in GLFW JS emulation layer has incorrect quantizing preventing small values. See #6096
    VanGui_ImplGlfw_Data* bd = static_cast<VanGui_ImplGlfw_Data*>(user_data);
    float multiplier = 0.0f;
    if (ev->deltaMode == DOM_DELTA_PIXEL)       { multiplier = 1.0f / 100.0f; } // 100 pixels make up a step.
    else if (ev->deltaMode == DOM_DELTA_LINE)   { multiplier = 1.0f / 3.0f; }   // 3 lines make up a step.
    else if (ev->deltaMode == DOM_DELTA_PAGE)   { multiplier = 80.0f; }         // A page makes up 80 steps.
    float wheel_x = ev->deltaX * -multiplier;
    float wheel_y = ev->deltaY * -multiplier;
    VanGuiIO& io = VanGui::GetIO(bd->Context);
    io.AddMouseWheelEvent(wheel_x, wheel_y);
    //VANGUI_DEBUG_LOG("[Emsc] mode %d dx: %.2f, dy: %.2f, dz: %.2f --> feed %.2f %.2f\n", (int)ev->deltaMode, ev->deltaX, ev->deltaY, ev->deltaZ, wheel_x, wheel_y);
    return EM_TRUE;
}
#endif

#ifdef _WIN32
// GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
// Add support for Win32 (based on vangui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
static VanGuiMouseSource GetMouseSourceFromMessageExtraInfo()
{
    LPARAM extra_info = ::GetMessageExtraInfo();
    if ((extra_info & 0xFFFFFF80) == 0xFF515700)
        return VanGuiMouseSource_Pen;
    if ((extra_info & 0xFFFFFF80) == 0xFF515780)
        return VanGuiMouseSource_TouchScreen;
    return VanGuiMouseSource_Mouse;
}
static LRESULT CALLBACK VanGui_ImplGlfw_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VanGui_ImplGlfw_Data* bd = static_cast<VanGui_ImplGlfw_Data*>(::GetPropA(hWnd, "VANGUI_BACKEND_DATA"));
    VanGuiIO& io = VanGui::GetIO(bd->Context);

    switch (msg)
    {
    case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        io.AddMouseSourceEvent(GetMouseSourceFromMessageExtraInfo());
        break;
    default: break;
    }
    return ::CallWindowProcW(bd->PrevWndProc, hWnd, msg, wParam, lParam);
}
#endif

void VanGui_ImplGlfw_InstallCallbacks(GLFWwindow* window)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    VAN_ASSERT(bd->InstalledCallbacks == false && "Callbacks already installed!");
    VAN_ASSERT(bd->Window == window);

    bd->PrevUserCallbackWindowFocus = glfwSetWindowFocusCallback(window, VanGui_ImplGlfw_WindowFocusCallback);
    bd->PrevUserCallbackCursorEnter = glfwSetCursorEnterCallback(window, VanGui_ImplGlfw_CursorEnterCallback);
    bd->PrevUserCallbackCursorPos = glfwSetCursorPosCallback(window, VanGui_ImplGlfw_CursorPosCallback);
    bd->PrevUserCallbackMousebutton = glfwSetMouseButtonCallback(window, VanGui_ImplGlfw_MouseButtonCallback);
    bd->PrevUserCallbackScroll = glfwSetScrollCallback(window, VanGui_ImplGlfw_ScrollCallback);
    bd->PrevUserCallbackKey = glfwSetKeyCallback(window, VanGui_ImplGlfw_KeyCallback);
    bd->PrevUserCallbackChar = glfwSetCharCallback(window, VanGui_ImplGlfw_CharCallback);
    bd->PrevUserCallbackMonitor = glfwSetMonitorCallback(VanGui_ImplGlfw_MonitorCallback);
    bd->InstalledCallbacks = true;
}

void VanGui_ImplGlfw_RestoreCallbacks(GLFWwindow* window)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    VAN_ASSERT(bd->InstalledCallbacks == true && "Callbacks not installed!");
    VAN_ASSERT(bd->Window == window);

    glfwSetWindowFocusCallback(window, bd->PrevUserCallbackWindowFocus);
    glfwSetCursorEnterCallback(window, bd->PrevUserCallbackCursorEnter);
    glfwSetCursorPosCallback(window, bd->PrevUserCallbackCursorPos);
    glfwSetMouseButtonCallback(window, bd->PrevUserCallbackMousebutton);
    glfwSetScrollCallback(window, bd->PrevUserCallbackScroll);
    glfwSetKeyCallback(window, bd->PrevUserCallbackKey);
    glfwSetCharCallback(window, bd->PrevUserCallbackChar);
    glfwSetMonitorCallback(bd->PrevUserCallbackMonitor);
    bd->InstalledCallbacks = false;
    bd->PrevUserCallbackWindowFocus = nullptr;
    bd->PrevUserCallbackCursorEnter = nullptr;
    bd->PrevUserCallbackCursorPos = nullptr;
    bd->PrevUserCallbackMousebutton = nullptr;
    bd->PrevUserCallbackScroll = nullptr;
    bd->PrevUserCallbackKey = nullptr;
    bd->PrevUserCallbackChar = nullptr;
    bd->PrevUserCallbackMonitor = nullptr;
}

// Set to 'true' to enable chaining installed callbacks for all windows (including secondary viewports created by backends or by user).
// This is 'false' by default meaning we only chain callbacks for the main viewport.
// We cannot set this to 'true' by default because user callbacks code may be not testing the 'window' parameter of their callback.
// If you set this to 'true' your user callback code will need to make sure you are testing the 'window' parameter.
void VanGui_ImplGlfw_SetCallbacksChainForAllWindows(bool chain_for_all_windows)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    bd->CallbacksChainForAllWindows = chain_for_all_windows;
}

#ifdef __EMSCRIPTEN__
#if EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3 >= 34020240817
void VanGui_ImplGlfw_EmscriptenOpenURL(const char* url) { if (url) emscripten::glfw3::OpenURL(url); }
#else
EM_JS(void, VanGui_ImplGlfw_EmscriptenOpenURL, (const char* url), { url = url ? UTF8ToString(url) : null; if (url) window.open(url, '_blank'); });
#endif
#endif

static void VanGui_ImplGlfw_InitPlatformInterface();
static void VanGui_ImplGlfw_ShutdownPlatformInterface();
float VanGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor* monitor);

static bool VanGui_ImplGlfw_Init(GLFWwindow* window, bool install_callbacks, GlfwClientApi client_api)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");
    //printf("GLFW_VERSION: %d.%d.%d (%d)", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION, GLFW_VERSION_COMBINED);

    // Setup backend capabilities flags
    VanGui_ImplGlfw_Data* bd = VAN_NEW(VanGui_ImplGlfw_Data)();
    snprintf(bd->BackendPlatformName, sizeof(bd->BackendPlatformName), "vangui_impl_glfw (%d)", GLFW_VERSION_COMBINED);
    io.BackendPlatformUserData = static_cast<void*>(bd);
    io.BackendPlatformName = bd->BackendPlatformName;
#if GLFW_HAS_CREATECURSOR
    io.BackendFlags |= VanGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
#endif
    io.BackendFlags |= VanGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    bd->Context = VanGui::GetCurrentContext();
    bd->Window = window;
    bd->Time = 0.0;
    bd->IsWayland = VanGui_ImplGlfw_IsWayland();
    VanGui_ImplGlfw_ContextMap_Add(window, bd->Context);

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
#if GLFW_VERSION_COMBINED < 3300
    platform_io.Platform_SetClipboardTextFn = [](VanGuiContext*, const char* text) { glfwSetClipboardString(VanGui_ImplGlfw_GetBackendData()->Window, text); };
    platform_io.Platform_GetClipboardTextFn = [](VanGuiContext*) { return glfwGetClipboardString(VanGui_ImplGlfw_GetBackendData()->Window); };
#else
    platform_io.Platform_SetClipboardTextFn = [](VanGuiContext*, const char* text) { glfwSetClipboardString(nullptr, text); };
    platform_io.Platform_GetClipboardTextFn = [](VanGuiContext*) { return glfwGetClipboardString(nullptr); };
#endif

#ifdef __EMSCRIPTEN__
    platform_io.Platform_OpenInShellFn = [](VanGuiContext*, const char* url) { VanGui_ImplGlfw_EmscriptenOpenURL(url); return true; };
#endif

    // Create mouse cursors
    // (By design, on X11 cursors are user configurable and some cursors may be missing. When a cursor doesn't exist,
    // GLFW will emit an error which will often be printed by the app, so we temporarily disable error reporting.
    // Missing cursors will return nullptr and our _UpdateMouseCursor() function will use the Arrow cursor instead.)
#if GLFW_HAS_CREATECURSOR
    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
    bd->MouseCursors[VanGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    bd->MouseCursors[VanGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
    bd->MouseCursors[VanGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    bd->MouseCursors[VanGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
    glfwSetErrorCallback(prev_error_callback);
#endif
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__) // Eat errors (see #5908)
    (void)glfwGetError(nullptr);
#endif

    // Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
    if (install_callbacks)
        VanGui_ImplGlfw_InstallCallbacks(window);
    else
        bd->PrevUserCallbackMonitor = glfwSetMonitorCallback(VanGui_ImplGlfw_MonitorCallback);

    // bd->WantUpdateMonitors is already initialized to true via the struct default, so monitors
    // will be populated on the first call to VanGui_ImplGlfw_NewFrame().

    // Set platform dependent data in viewport
    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    main_viewport->PlatformHandle = static_cast<void*>(bd->Window);
#ifdef _WIN32
    main_viewport->PlatformHandleRaw = glfwGetWin32Window(bd->Window);
#elif defined(__APPLE__)
    main_viewport->PlatformHandleRaw = reinterpret_cast<void*>(glfwGetCocoaWindow(bd->Window));
#else
    VAN_UNUSED(main_viewport);
#endif

    // Windows: register a WndProc hook so we can intercept some messages.
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(main_viewport->PlatformHandleRaw);
    ::SetPropA(hwnd, "VANGUI_BACKEND_DATA", bd);
    bd->PrevWndProc = reinterpret_cast<WNDPROC>(::GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    VAN_ASSERT(bd->PrevWndProc != nullptr);
    ::SetWindowLongPtrW(static_cast<HWND>(main_viewport->PlatformHandleRaw), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(VanGui_ImplGlfw_WndProc));
#endif

    // Emscripten: the same application can run on various platforms, so we detect the Apple platform at runtime
    // to override io.ConfigMacOSXBehaviors from its default (which is always false in Emscripten).
#ifdef __EMSCRIPTEN__
#if EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3 >= 34020240817
    if (emscripten::glfw3::IsRuntimePlatformApple())
    {
        io.ConfigMacOSXBehaviors = true;

        // Due to how the browser (poorly) handles the Meta Key, this line essentially disables repeats when used.
        // This means that Meta + V only registers a single key-press, even if the keys are held.
        // This is a compromise for dealing with this issue in VanGui since VanGui implements key repeat itself.
        // See https://github.com/pongasoft/emscripten-glfw/blob/v3.4.0.20240817/docs/Usage.md#the-problem-of-the-super-key
        emscripten::glfw3::SetSuperPlusKeyTimeouts(10, 10);
    }
#endif
#endif

    bd->ClientApi = client_api;
    VanGui_ImplGlfw_InitPlatformInterface();
    return true;
}

bool VanGui_ImplGlfw_InitForOpenGL(GLFWwindow* window, bool install_callbacks)
{
    return VanGui_ImplGlfw_Init(window, install_callbacks, GlfwClientApi_OpenGL);
}

bool VanGui_ImplGlfw_InitForVulkan(GLFWwindow* window, bool install_callbacks)
{
    return VanGui_ImplGlfw_Init(window, install_callbacks, GlfwClientApi_Vulkan);
}

bool VanGui_ImplGlfw_InitForOther(GLFWwindow* window, bool install_callbacks)
{
    return VanGui_ImplGlfw_Init(window, install_callbacks, GlfwClientApi_Unknown);
}

void VanGui_ImplGlfw_Shutdown()
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    VanGui_ImplGlfw_ShutdownPlatformInterface();

    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    if (bd->InstalledCallbacks)
        VanGui_ImplGlfw_RestoreCallbacks(bd->Window);
    else
        glfwSetMonitorCallback(bd->PrevUserCallbackMonitor);
#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
    if (bd->CanvasSelector)
        emscripten_set_wheel_callback(bd->CanvasSelector, nullptr, false, nullptr);
#endif
#if GLFW_HAS_CREATECURSOR
    for (VanGuiMouseCursor cursor_n = 0; cursor_n < VanGuiMouseCursor_COUNT; cursor_n++)
        glfwDestroyCursor(bd->MouseCursors[cursor_n]);
#endif
    // Windows: restore our WndProc hook
#ifdef _WIN32
    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    ::SetPropA(static_cast<HWND>(main_viewport->PlatformHandleRaw), "VANGUI_BACKEND_DATA", nullptr);
    ::SetWindowLongPtrW(static_cast<HWND>(main_viewport->PlatformHandleRaw), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(bd->PrevWndProc));
    bd->PrevWndProc = nullptr;
#endif

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_HasMouseCursors | VanGuiBackendFlags_HasSetMousePos | VanGuiBackendFlags_HasGamepad);
    platform_io.ClearPlatformHandlers();
    VanGui_ImplGlfw_ContextMap_Remove(bd->Window);
    VAN_DELETE(bd);
}

static void VanGui_ImplGlfw_UpdateMouseData()
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();

    // (those braces are here to reduce diff with multi-viewports support in 'docking' branch)
    {
        GLFWwindow* window = bd->Window;
#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
        const bool is_window_focused = true;
#else
        const bool is_window_focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
#endif
        if (is_window_focused)
        {
            // (Optional) Set OS mouse position from VanGUI if requested (rarely used, only when io.ConfigNavMoveSetMousePos is enabled by user)
            if (io.WantSetMousePos)
                glfwSetCursorPos(window, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y));

            // (Optional) Fallback to provide mouse position when focused (VanGui_ImplGlfw_CursorPosCallback already provides this when hovered or captured)
            if (bd->MouseWindow == nullptr)
            {
                double mouse_x, mouse_y;
                glfwGetCursorPos(window, &mouse_x, &mouse_y);
                bd->LastValidMousePos = VanVec2(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
                io.AddMousePosEvent(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
            }
        }
    }
}

static void VanGui_ImplGlfw_UpdateMouseCursor()
{
    VanGuiIO& io = VanGui::GetIO();
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    if ((io.ConfigFlags & VanGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(bd->Window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        bd->LastMouseCursor = nullptr;  // Invalidate so that if user changes underlying cursor we will update it next time we can.
        return;
    }

    VanGuiMouseCursor vangui_cursor = VanGui::GetMouseCursor();
    // (those braces are here to reduce diff with multi-viewports support in 'docking' branch)
    {
        GLFWwindow* window = bd->Window;
        if (vangui_cursor == VanGuiMouseCursor_None || io.MouseDrawCursor)
        {
            if (bd->LastMouseCursor != nullptr)
            {
                // Hide OS mouse cursor if vangui is drawing it or if it wants no cursor
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                bd->LastMouseCursor = nullptr;
            }
        }
        else
        {
            // Show OS mouse cursor
            // FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
#if GLFW_HAS_CREATECURSOR
            GLFWcursor* cursor = bd->MouseCursors[vangui_cursor] ? bd->MouseCursors[vangui_cursor] : bd->MouseCursors[VanGuiMouseCursor_Arrow];
            if (bd->LastMouseCursor != cursor)
            {
                glfwSetCursor(window, cursor);
                bd->LastMouseCursor = cursor;
            }
#endif
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

// Update gamepad inputs
static inline float Saturate(float v) { return v < 0.0f ? 0.0f : v  > 1.0f ? 1.0f : v; }
static void VanGui_ImplGlfw_UpdateGamepads()
{
    VanGuiIO& io = VanGui::GetIO();
    if ((io.ConfigFlags & VanGuiConfigFlags_NavEnableGamepad) == 0) // FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs, but see #8075
        return;

    io.BackendFlags &= ~VanGuiBackendFlags_HasGamepad;
#if GLFW_HAS_GAMEPAD_API && !defined(EMSCRIPTEN_USE_EMBEDDED_GLFW3)
    GLFWgamepadstate gamepad;
    if (!glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad))
        return;
    #define MAP_BUTTON(KEY_NO, BUTTON_NO, _UNUSED)          do { io.AddKeyEvent(KEY_NO, gamepad.buttons[BUTTON_NO] != 0); } while (0)
    #define MAP_ANALOG(KEY_NO, AXIS_NO, _UNUSED, V0, V1)    do { float v = gamepad.axes[AXIS_NO]; v = (v - V0) / (V1 - V0); io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, Saturate(v)); } while (0)
#else
    int axes_count = 0, buttons_count = 0;
    const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axes_count);
    const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &buttons_count);
    if (axes_count == 0 || buttons_count == 0)
        return;
    #define MAP_BUTTON(KEY_NO, _UNUSED, BUTTON_NO)          do { io.AddKeyEvent(KEY_NO, (buttons_count > BUTTON_NO && buttons[BUTTON_NO] == GLFW_PRESS)); } while (0)
    #define MAP_ANALOG(KEY_NO, _UNUSED, AXIS_NO, V0, V1)    do { float v = (axes_count > AXIS_NO) ? axes[AXIS_NO] : V0; v = (v - V0) / (V1 - V0); io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, Saturate(v)); } while (0)
#endif
    io.BackendFlags |= VanGuiBackendFlags_HasGamepad;
    MAP_BUTTON(VanGuiKey_GamepadStart,       GLFW_GAMEPAD_BUTTON_START,          7);
    MAP_BUTTON(VanGuiKey_GamepadBack,        GLFW_GAMEPAD_BUTTON_BACK,           6);
    MAP_BUTTON(VanGuiKey_GamepadFaceLeft,    GLFW_GAMEPAD_BUTTON_X,              2);     // Xbox X, PS Square
    MAP_BUTTON(VanGuiKey_GamepadFaceRight,   GLFW_GAMEPAD_BUTTON_B,              1);     // Xbox B, PS Circle
    MAP_BUTTON(VanGuiKey_GamepadFaceUp,      GLFW_GAMEPAD_BUTTON_Y,              3);     // Xbox Y, PS Triangle
    MAP_BUTTON(VanGuiKey_GamepadFaceDown,    GLFW_GAMEPAD_BUTTON_A,              0);     // Xbox A, PS Cross
    MAP_BUTTON(VanGuiKey_GamepadDpadLeft,    GLFW_GAMEPAD_BUTTON_DPAD_LEFT,      13);
    MAP_BUTTON(VanGuiKey_GamepadDpadRight,   GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,     11);
    MAP_BUTTON(VanGuiKey_GamepadDpadUp,      GLFW_GAMEPAD_BUTTON_DPAD_UP,        10);
    MAP_BUTTON(VanGuiKey_GamepadDpadDown,    GLFW_GAMEPAD_BUTTON_DPAD_DOWN,      12);
    MAP_BUTTON(VanGuiKey_GamepadL1,          GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,    4);
    MAP_BUTTON(VanGuiKey_GamepadR1,          GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,   5);
    MAP_ANALOG(VanGuiKey_GamepadL2,          GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,     4,      -0.75f,  +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadR2,          GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER,    5,      -0.75f,  +1.0f);
    MAP_BUTTON(VanGuiKey_GamepadL3,          GLFW_GAMEPAD_BUTTON_LEFT_THUMB,     8);
    MAP_BUTTON(VanGuiKey_GamepadR3,          GLFW_GAMEPAD_BUTTON_RIGHT_THUMB,    9);
    MAP_ANALOG(VanGuiKey_GamepadLStickLeft,  GLFW_GAMEPAD_AXIS_LEFT_X,           0,      -0.25f,  -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickRight, GLFW_GAMEPAD_AXIS_LEFT_X,           0,      +0.25f,  +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickUp,    GLFW_GAMEPAD_AXIS_LEFT_Y,           1,      -0.25f,  -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickDown,  GLFW_GAMEPAD_AXIS_LEFT_Y,           1,      +0.25f,  +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickLeft,  GLFW_GAMEPAD_AXIS_RIGHT_X,          2,      -0.25f,  -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickRight, GLFW_GAMEPAD_AXIS_RIGHT_X,          2,      +0.25f,  +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickUp,    GLFW_GAMEPAD_AXIS_RIGHT_Y,          3,      -0.25f,  -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickDown,  GLFW_GAMEPAD_AXIS_RIGHT_Y,          3,      +0.25f,  +1.0f);
    #undef MAP_BUTTON
    #undef MAP_ANALOG
}

// For GFLW 3.2 + Windows: include a simplified non-monitor aware version of VanGui_ImplWin32_GetDpiScaleForMonitor().
// This is merely a band-aid to make using GLFW 3.2 a little bit nicer, but prefer to use GLFW 3.3+ or the full correct functions from the Win32 backend.
#if !GLFW_HAS_PER_MONITOR_DPI && defined(_WIN32) && !defined(NOGDI)
static float   VanGui_ImplWin32_GetLegacyDpiScale()   { const HDC dc = ::GetDC(nullptr); UINT xdpi = ::GetDeviceCaps(dc, LOGPIXELSX); ::ReleaseDC(nullptr, dc); return static_cast<float>(xdpi) / 96.0f; }
static void    glfwGetWindowContentScale(GLFWwindow*, float* x_scale, float* y_scale)   { *x_scale = *y_scale = VanGui_ImplWin32_GetLegacyDpiScale(); }
static void    glfwGetMonitorContentScale(GLFWmonitor*, float* x_scale, float* y_scale) { *x_scale = *y_scale = VanGui_ImplWin32_GetLegacyDpiScale(); }
#undef GLFW_HAS_PER_MONITOR_DPI
#define GLFW_HAS_PER_MONITOR_DPI 1
#endif

// - On Windows the process needs to be marked DPI-aware!! SDL2 doesn't do it by default. You can call ::SetProcessDPIAware() or call VanGui_ImplWin32_EnableDpiAwareness() from Win32 backend.
// - Apple platforms use FramebufferScale so we always return 1.0f.
// - Some accessibility applications are declaring virtual monitors with a DPI of 0.0f, see #7902. We preserve this value for caller to handle.
float VanGui_ImplGlfw_GetContentScaleForWindow(GLFWwindow* window)
{
#if GLFW_HAS_WAYLAND
    if (VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window))
        if (bd->IsWayland)
            return 1.0f;
#endif
#if GLFW_HAS_PER_MONITOR_DPI && !(defined(__APPLE__) || defined(__EMSCRIPTEN__) || defined(__ANDROID__))
    float x_scale, y_scale;
    glfwGetWindowContentScale(window, &x_scale, &y_scale);
    return x_scale;
#else
    VAN_UNUSED(window);
    return 1.0f;
#endif
}

float VanGui_ImplGlfw_GetContentScaleForMonitor(GLFWmonitor* monitor)
{
#if GLFW_HAS_WAYLAND
    if (VanGui_ImplGlfw_IsWayland()) // We can't access our bd->IsWayland cache for a monitor.
        return 1.0f;
#endif
#if GLFW_HAS_PER_MONITOR_DPI && !(defined(__APPLE__) || defined(__EMSCRIPTEN__) || defined(__ANDROID__))
    float x_scale, y_scale;
    glfwGetMonitorContentScale(monitor, &x_scale, &y_scale);
    return x_scale;
#else
    VAN_UNUSED(monitor);
    return 1.0f;
#endif
}

static void VanGui_ImplGlfw_GetWindowSizeAndFramebufferScale(GLFWwindow* window, VanVec2* out_size, VanVec2* out_framebuffer_scale)
{
    int w, h;
    int display_w, display_h;
    glfwGetWindowSize(window, &w, &h);
    glfwGetFramebufferSize(window, &display_w, &display_h);
    float fb_scale_x = (w > 0) ? static_cast<float>(display_w) / static_cast<float>(w) : 1.0f;
    float fb_scale_y = (h > 0) ? static_cast<float>(display_h) / static_cast<float>(h) : 1.0f;
#if GLFW_HAS_WAYLAND
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData(window);
    if (!bd->IsWayland)
        fb_scale_x = fb_scale_y = 1.0f;
#endif
    if (out_size != nullptr)
        *out_size = VanVec2(static_cast<float>(w), static_cast<float>(h));
    if (out_framebuffer_scale != nullptr)
        *out_framebuffer_scale = VanVec2(fb_scale_x, fb_scale_y);
}

void VanGui_ImplGlfw_NewFrame()
{
    VanGuiIO& io = VanGui::GetIO();
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplGlfw_InitForXXX()?");

    // Setup main viewport size (every frame to accommodate for window resizing)
    VanGui_ImplGlfw_GetWindowSizeAndFramebufferScale(bd->Window, &io.DisplaySize, &io.DisplayFramebufferScale);

    // Setup time step
    // (Accept glfwGetTime() not returning a monotonically increasing value. Seems to happens on disconnecting peripherals and probably on VMs and Emscripten, see #6491, #6189, #6114, #3644)
    double current_time = glfwGetTime();
    if (current_time <= bd->Time)
        current_time = bd->Time + 0.00001;
    io.DeltaTime = bd->Time > 0.0 ? static_cast<float>(current_time - bd->Time) : static_cast<float>(1.0f / 60.0f);
    bd->Time = current_time;

    VanGui_ImplGlfw_UpdateMouseData();
    VanGui_ImplGlfw_UpdateMouseCursor();

    // Update monitor list (on first frame and whenever the monitor configuration changes)
    if (bd->WantUpdateMonitors)
    {
        VanGui_ImplGlfw_UpdateMonitors();
        bd->WantUpdateMonitors = false;
    }

    // Update game controllers (if enabled and available)
    VanGui_ImplGlfw_UpdateGamepads();
}

// GLFW doesn't provide a portable sleep function
void VanGui_ImplGlfw_Sleep(int milliseconds)
{
#ifdef _WIN32
    ::Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

#ifdef EMSCRIPTEN_USE_EMBEDDED_GLFW3
static EM_BOOL VanGui_ImplGlfw_OnCanvasSizeChange(int event_type, const EmscriptenUiEvent* event, void* user_data)
{
    VanGui_ImplGlfw_Data* bd = static_cast<VanGui_ImplGlfw_Data*>(user_data);
    double canvas_width, canvas_height;
    emscripten_get_element_css_size(bd->CanvasSelector, &canvas_width, &canvas_height);
    glfwSetWindowSize(bd->Window, static_cast<int>(canvas_width), static_cast<int>(canvas_height));
    return true;
}

static EM_BOOL VanGui_ImplEmscripten_FullscreenChangeCallback(int event_type, const EmscriptenFullscreenChangeEvent* event, void* user_data)
{
    VanGui_ImplGlfw_Data* bd = static_cast<VanGui_ImplGlfw_Data*>(user_data);
    double canvas_width, canvas_height;
    emscripten_get_element_css_size(bd->CanvasSelector, &canvas_width, &canvas_height);
    glfwSetWindowSize(bd->Window, static_cast<int>(canvas_width), static_cast<int>(canvas_height));
    return true;
}

// 'canvas_selector' is a CSS selector. The event listener is applied to the first element that matches the query.
// STRING MUST PERSIST FOR THE APPLICATION DURATION. PLEASE USE A STRING LITERAL OR ENSURE POINTER WILL STAY VALID.
void VanGui_ImplGlfw_InstallEmscriptenCallbacks(GLFWwindow*, const char* canvas_selector)
{
    VAN_ASSERT(canvas_selector != nullptr);
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplGlfw_InitForXXX()?");

    bd->CanvasSelector = canvas_selector;
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, bd, false, VanGui_ImplGlfw_OnCanvasSizeChange);
    emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, bd, false, VanGui_ImplEmscripten_FullscreenChangeCallback);

    // Change the size of the GLFW window according to the size of the canvas
    VanGui_ImplGlfw_OnCanvasSizeChange(EMSCRIPTEN_EVENT_RESIZE, {}, bd);

    // Register Emscripten Wheel callback to workaround issue in Emscripten GLFW Emulation (#6096)
    // We intentionally do not check 'if (install_callbacks)' here, as some users may set it to false and call GLFW callback themselves.
    // FIXME: May break chaining in case user registered their own Emscripten callback?
    emscripten_set_wheel_callback(bd->CanvasSelector, bd, false, VanGui_ImplEmscripten_WheelCallback);
}
#elif defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
// When using --use-port=contrib.glfw3 for the GLFW implementation, you can override the behavior of this call
// by invoking emscripten_glfw_make_canvas_resizable afterward.
// See https://github.com/pongasoft/emscripten-glfw/blob/master/docs/Usage.md#how-to-make-the-canvas-resizable-by-the-user for an explanation
void VanGui_ImplGlfw_InstallEmscriptenCallbacks(GLFWwindow* window, const char* canvas_selector)
{
  GLFWwindow* w = reinterpret_cast<GLFWwindow*>(EM_ASM_INT({ return Module.glfwGetWindow(UTF8ToString($0)); }, canvas_selector));
  VAN_ASSERT(window == w); // Sanity check
  VAN_UNUSED(w);
  emscripten_glfw_make_canvas_resizable(window, "window", nullptr);
}
#endif // #ifdef EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3

//-----------------------------------------------------------------------------
// [SECTION] Multi-viewport / Platform Interface
//-----------------------------------------------------------------------------

static void VanGui_ImplGlfw_WindowCloseCallback(GLFWwindow* window)
{
    if (VanGuiViewport* viewport = VanGui::FindViewportByPlatformHandle(static_cast<void*>(window)))
        viewport->PlatformRequestClose = true;
}

static void VanGui_ImplGlfw_WindowPosCallback(GLFWwindow* window, int x, int y)
{
    if (VanGuiViewport* viewport = VanGui::FindViewportByPlatformHandle(static_cast<void*>(window)))
        if (!(viewport->Flags & VanGuiViewportFlags_NoFocusOnClick))
            viewport->PlatformRequestMove = true;
}

static void VanGui_ImplGlfw_WindowSizeCallback(GLFWwindow* window, int w, int h)
{
    if (VanGuiViewport* viewport = VanGui::FindViewportByPlatformHandle(static_cast<void*>(window)))
        viewport->PlatformRequestResize = true;
}

static void VanGui_ImplGlfw_CreateWindow(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_Data* bd = VanGui_ImplGlfw_GetBackendData();
    VanGui_ImplGlfw_ViewportData* vd = VAN_NEW(VanGui_ImplGlfw_ViewportData)();
    viewport->PlatformUserData = static_cast<void*>(vd);

    glfwWindowHint(GLFW_VISIBLE, false);
    glfwWindowHint(GLFW_FOCUSED, false);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
    glfwWindowHint(GLFW_DECORATED, (viewport->Flags & VanGuiViewportFlags_NoDecoration) ? false : true);
    glfwWindowHint(GLFW_FLOATING, (viewport->Flags & VanGuiViewportFlags_TopMost) ? true : false);

    GLFWwindow* share_window = bd->Window;
    vd->Window = glfwCreateWindow(static_cast<int>(viewport->Size.x), static_cast<int>(viewport->Size.y), "No Title Yet", nullptr, share_window);
    vd->WindowOwned = true;
    viewport->PlatformHandle = static_cast<void*>(vd->Window);
#ifdef _WIN32
    viewport->PlatformHandleRaw = reinterpret_cast<void*>(glfwGetWin32Window(vd->Window));
#endif
    glfwSetWindowPos(vd->Window, static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y));

    // Register callbacks
    glfwSetWindowCloseCallback(vd->Window, VanGui_ImplGlfw_WindowCloseCallback);
    glfwSetWindowPosCallback(vd->Window, VanGui_ImplGlfw_WindowPosCallback);
    glfwSetWindowSizeCallback(vd->Window, VanGui_ImplGlfw_WindowSizeCallback);
}

static void VanGui_ImplGlfw_DestroyWindow(VanGuiViewport* viewport)
{
    if (VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData))
    {
        if (vd->WindowOwned)
        {
#ifndef __EMSCRIPTEN__
            glfwDestroyWindow(vd->Window);
#endif
        }
        vd->Window = nullptr;
        VAN_DELETE(vd);
    }
    viewport->PlatformUserData = nullptr;
    viewport->PlatformHandle = nullptr;
}

static void VanGui_ImplGlfw_ShowWindow(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(viewport->PlatformHandleRaw);
    if (viewport->Flags & VanGuiViewportFlags_NoTaskBarIcon)
    {
        LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        ex_style &= ~WS_EX_APPWINDOW;
        ex_style |= WS_EX_TOOLWINDOW;
        ::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
    }
    if (viewport->Flags & VanGuiViewportFlags_TopMost)
        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
    glfwShowWindow(vd->Window);
}

static VanVec2 VanGui_ImplGlfw_GetWindowPos(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    int x = 0, y = 0;
    glfwGetWindowPos(vd->Window, &x, &y);
    return VanVec2(static_cast<float>(x), static_cast<float>(y));
}

static void VanGui_ImplGlfw_SetWindowPos(VanGuiViewport* viewport, VanVec2 pos)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    glfwSetWindowPos(vd->Window, static_cast<int>(pos.x), static_cast<int>(pos.y));
}

static VanVec2 VanGui_ImplGlfw_GetWindowSize(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    int w = 0, h = 0;
    glfwGetWindowSize(vd->Window, &w, &h);
    return VanVec2(static_cast<float>(w), static_cast<float>(h));
}

static void VanGui_ImplGlfw_SetWindowSize(VanGuiViewport* viewport, VanVec2 size)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    glfwSetWindowSize(vd->Window, static_cast<int>(size.x), static_cast<int>(size.y));
}

static void VanGui_ImplGlfw_SetWindowTitle(VanGuiViewport* viewport, const char* title)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    glfwSetWindowTitle(vd->Window, title);
}

static void VanGui_ImplGlfw_SetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    glfwFocusWindow(vd->Window);
}

static bool VanGui_ImplGlfw_GetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    return glfwGetWindowAttrib(vd->Window, GLFW_FOCUSED) != 0;
}

static bool VanGui_ImplGlfw_GetWindowMinimized(VanGuiViewport* viewport)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    return glfwGetWindowAttrib(vd->Window, GLFW_ICONIFIED) != 0;
}

static void VanGui_ImplGlfw_SetWindowAlpha(VanGuiViewport* viewport, float alpha)
{
    VanGui_ImplGlfw_ViewportData* vd = static_cast<VanGui_ImplGlfw_ViewportData*>(viewport->PlatformUserData);
    glfwSetWindowOpacity(vd->Window, alpha);
}

static void VanGui_ImplGlfw_InitPlatformInterface()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Platform_CreateWindow       = VanGui_ImplGlfw_CreateWindow;
    platform_io.Platform_DestroyWindow      = VanGui_ImplGlfw_DestroyWindow;
    platform_io.Platform_ShowWindow         = VanGui_ImplGlfw_ShowWindow;
    platform_io.Platform_SetWindowPos       = VanGui_ImplGlfw_SetWindowPos;
    platform_io.Platform_GetWindowPos       = VanGui_ImplGlfw_GetWindowPos;
    platform_io.Platform_SetWindowSize      = VanGui_ImplGlfw_SetWindowSize;
    platform_io.Platform_GetWindowSize      = VanGui_ImplGlfw_GetWindowSize;
    platform_io.Platform_SetWindowFocus     = VanGui_ImplGlfw_SetWindowFocus;
    platform_io.Platform_GetWindowFocus     = VanGui_ImplGlfw_GetWindowFocus;
    platform_io.Platform_GetWindowMinimized = VanGui_ImplGlfw_GetWindowMinimized;
    platform_io.Platform_SetWindowTitle     = VanGui_ImplGlfw_SetWindowTitle;
    platform_io.Platform_SetWindowAlpha     = VanGui_ImplGlfw_SetWindowAlpha;
}

static void VanGui_ImplGlfw_ShutdownPlatformInterface()
{
    VanGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
