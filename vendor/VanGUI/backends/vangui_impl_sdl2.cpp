// VanGUI: Platform Backend for SDL2
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)
// (Info: SDL2 is a cross-platform general purpose library for handling windows, inputs, graphics context creation, etc.)
// (Prefer SDL 2.0.5+ for full feature support.)

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

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-16: Made VanGui_ImplSDL2_GetContentScaleForWindow(), VanGui_ImplSDL2_GetContentScaleForDisplay() helpers return a minimum of 1.0f, as some Linux setup seems to report <1.0f value and this breaks scaling border size. (#9369)
//  2026-02-13: Inputs: systems other than X11 are back to starting mouse capture on mouse down (reverts 2025-02-26 change). Only X11 requires waiting for a drag by default (not ideal, but a better default for X11 users). Added VanGui_ImplSDL2_SetMouseCaptureMode() for X11 debugger users. (#3650, #6410, #9235)
//  2026-01-15: Changed GetClipboardText() handler to return nullptr on error aka clipboard contents is not text. Consistent with other backends. (#9168)
//  2025-09-24: Skip using the SDL_GetGlobalMouseState() state when one of our window is hovered, as the SDL_MOUSEMOTION data is reliable. Fix macOS notch mouse coordinates issue in fullscreen mode + better perf on X11. (#7919, #7786)
//  2025-09-18: Call platform_io.ClearPlatformHandlers() on shutdown.
//  2025-09-15: Content Scales are always reported as 1.0 on Wayland. (#8921)
//  2025-07-08: Made VanGui_ImplSDL2_GetContentScaleForWindow(), VanGui_ImplSDL2_GetContentScaleForDisplay() helpers return 1.0f on Emscripten and Android platforms, matching macOS logic. (#8742, #8733)
//  2025-06-11: Added VanGui_ImplSDL2_GetContentScaleForWindow(SDL_Window* window) and VanGui_ImplSDL2_GetContentScaleForDisplay(int display_index) helper to facilitate making DPI-aware apps.
//  2025-04-09: Don't attempt to call SDL_CaptureMouse() on drivers where we don't call SDL_GetGlobalMouseState(). (#8561)
//  2025-03-21: Fill gamepad inputs and set VanGuiBackendFlags_HasGamepad regardless of VanGuiConfigFlags_NavEnableGamepad being set.
//  2025-03-10: When dealing with OEM keys, use scancodes instead of translated keycodes to choose VanGuiKey values. (#7136, #7201, #7206, #7306, #7670, #7672, #8468)
//  2025-02-26: Only start SDL_CaptureMouse() when mouse is being dragged, to mitigate issues with e.g. Linux debuggers not claiming capture back. (#6410, #3650)
//  2025-02-24: Avoid calling SDL_GetGlobalMouseState() when mouse is in relative mode.
//  2025-02-18: Added VanGuiMouseCursor_Wait and VanGuiMouseCursor_Progress mouse cursor support.
//  2025-02-10: Using SDL_OpenURL() in platform_io.Platform_OpenInShellFn handler.
//  2025-01-20: Made VanGui_ImplSDL2_SetGamepadMode(VanGui_ImplSDL2_GamepadMode_Manual) accept an empty array.
//  2024-10-24: Emscripten: from SDL 2.30.9, SDL_EVENT_MOUSE_WHEEL event doesn't require dividing by 100.0f.
//  2024-09-09: use SDL_Vulkan_GetDrawableSize() when available. (#7967, #3190)
//  2024-08-22: moved some OS/backend related function pointers from VanGuiIO to VanGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//               - io.PlatformOpenInShellFn -> platform_io.Platform_OpenInShellFn
//               - io.PlatformSetImeDataFn  -> platform_io.Platform_SetImeDataFn
//  2024-08-19: Storing SDL's Uint32 WindowID inside VanGuiViewport::PlatformHandle instead of SDL_Window*.
//  2024-08-19: VanGui_ImplSDL2_ProcessEvent() now ignores events intended for other SDL windows. (#7853)
//  2024-07-02: Emscripten: Added io.PlatformOpenInShellFn() handler for Emscripten versions.
//  2024-07-02: Update for io.SetPlatformImeDataFn() -> io.PlatformSetImeDataFn() renaming in main library.
//  2024-02-14: Inputs: Handle gamepad disconnection. Added VanGui_ImplSDL2_SetGamepadMode().
//  2023-10-05: Inputs: Added support for extra VanGuiKey values: F13 to F24 function keys, app back/forward keys.
//  2023-04-06: Inputs: Avoid calling SDL_StartTextInput()/SDL_StopTextInput() as they don't only pertain to IME. It's unclear exactly what their relation is to IME. (#6306)
//  2023-04-04: Inputs: Added support for io.AddMouseSourceEvent() to discriminate VanGuiMouseSource_Mouse/VanGuiMouseSource_TouchScreen. (#2702)
//  2023-02-23: Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. (#6189, #6114, #3644)
//  2023-02-07: Implement IME handler (io.SetPlatformImeDataFn will call SDL_SetTextInputRect()/SDL_StartTextInput()).
//  2023-02-07: *BREAKING CHANGE* Renamed this backend file from vangui_impl_sdl.cpp/.h to vangui_impl_sdl2.cpp/.h in prevision for the future release of SDL3.
//  2023-02-02: Avoid calling SDL_SetCursor() when cursor has not changed, as the function is surprisingly costly on Mac with latest SDL (may be fixed in next SDL version).
//  2023-02-02: Added support for SDL 2.0.18+ preciseX/preciseY mouse wheel data for smooth scrolling + Scaling X value on Emscripten (bug?). (#4019, #6096)
//  2023-02-02: Removed SDL_MOUSEWHEEL value clamping, as values seem correct in latest Emscripten. (#4019)
//  2023-02-01: Flipping SDL_MOUSEWHEEL 'wheel.x' value to match other backends and offer consistent horizontal scrolling direction. (#4019, #6096, #1463)
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2022-09-26: Inputs: Disable SDL 2.0.22 new "auto capture" (SDL_HINT_MOUSE_AUTO_CAPTURE) which prevents drag and drop across windows for multi-viewport support + don't capture when drag and dropping. (#5710)
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-03-22: Inputs: Fix mouse position issues when dragging outside of boundaries. SDL_CaptureMouse() erroneously still gives out LEAVE events when hovering OS decorations.
//  2022-03-22: Inputs: Added support for extra mouse buttons (SDL_BUTTON_X1/SDL_BUTTON_X2).
//  2022-02-04: Added SDL_Renderer* parameter to VanGui_ImplSDL2_InitForSDLRenderer(), so we can use SDL_GetRendererOutputSize() instead of SDL_GL_GetDrawableSize() when bound to a SDL_Renderer.
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2021-01-20: Inputs: calling new io.AddKeyAnalogEvent() for gamepad support, instead of writing directly to io.NavInputs[].
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always update key mods next and before key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-12: Update mouse inputs using SDL_MOUSEMOTION/SDL_WINDOWEVENT_LEAVE + fallback to provide it when focused but not hovered/captured. More standard and will allow us to pass it to future input queue API.
//  2022-01-12: Maintain our own copy of MouseButtonsDown mask instead of using VanGui::IsAnyMouseDown() which will be obsoleted.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2021-08-17: Calling io.AddFocusEvent() on SDL_WINDOWEVENT_FOCUS_GAINED/SDL_WINDOWEVENT_FOCUS_LOST.
//  2021-07-29: Inputs: MousePos is correctly reported when the host platform window is hovered but not focused (using SDL_GetMouseFocus() + SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, requires SDL 2.0.5+)
//  2021-06-29: *BREAKING CHANGE* Removed 'SDL_Window* window' parameter to VanGui_ImplSDL2_NewFrame() which was unnecessary.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-03-22: Rework global mouse pos availability check listing supported platforms explicitly, effectively fixing mouse access on Raspberry Pi. (#2837, #3950)
//  2020-05-25: Misc: Report a zero display-size when window is minimized, to be consistent with other backends.
//  2020-02-20: Inputs: Fixed mapping for VanGuiKey_KeyPadEnter (using SDL_SCANCODE_KP_ENTER instead of SDL_SCANCODE_RETURN2).
//  2019-12-17: Inputs: On Wayland, use SDL_GetMouseState (because there is no global mouse state).
//  2019-12-05: Inputs: Added support for VanGuiMouseCursor_NotAllowed mouse cursor.
//  2019-07-21: Inputs: Added mapping for VanGuiKey_KeyPadEnter.
//  2019-04-23: Inputs: Added support for SDL_GameController (if VanGuiConfigFlags_NavEnableGamepad is set by user application).
//  2019-03-12: Misc: Preserve DisplayFramebufferScale when main window is minimized.
//  2018-12-21: Inputs: Workaround for Android/iOS which don't seem to handle focus related calls.
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-11-14: Changed the signature of VanGui_ImplSDL2_ProcessEvent() to take a 'const SDL_Event*'.
//  2018-08-01: Inputs: Workaround for Emscripten which doesn't seem to handle focus related calls.
//  2018-06-29: Inputs: Added support for the VanGuiMouseCursor_Hand cursor.
//  2018-06-08: Misc: Extracted vangui_impl_sdl.cpp/.h away from the old combined SDL2+OpenGL/Vulkan examples.
//  2018-06-08: Misc: VanGui_ImplSDL2_InitForOpenGL() now takes a SDL_GLContext parameter.
//  2018-05-09: Misc: Fixed clipboard paste memory leak (we didn't call SDL_FreeMemory on the data returned by SDL_GetClipboardText).
//  2018-03-20: Misc: Setup io.BackendFlags VanGuiBackendFlags_HasMouseCursors flag + honor VanGuiConfigFlags_NoMouseCursorChange flag.
//  2018-02-16: Inputs: Added support for mouse cursors, honoring VanGui::GetMouseCursor() value.
//  2018-02-06: Misc: Removed call to VanGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-02-06: Inputs: Added mapping for VanGuiKey_Space.
//  2018-02-05: Misc: Using SDL_GetPerformanceCounter() instead of SDL_GetTicks() to be able to handle very high framerate (1000+ FPS).
//  2018-02-05: Inputs: Keyboard mapping is using scancodes everywhere instead of a confusing mixture of keycodes and scancodes.
//  2018-01-20: Inputs: Added Horizontal Mouse Wheel support.
//  2018-01-19: Inputs: When available (SDL 2.0.4+) using SDL_CaptureMouse() to retrieve coordinates outside of client area when dragging. Otherwise (SDL 2.0.3 and before) testing for SDL_WINDOW_INPUT_FOCUS instead of SDL_WINDOW_MOUSE_FOCUS.
//  2018-01-18: Inputs: Added mapping for VanGuiKey_Insert.
//  2017-08-25: Inputs: MousePos set to -FLT_MAX,-FLT_MAX when mouse is unavailable/missing (instead of -1,-1).
//  2016-10-15: Misc: Added a void* user_data parameter to Clipboard function handlers.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_sdl2.h"

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"                 // warning: use of old-style cast
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"  // warning: implicit conversion from 'xxx' to 'float' may lose precision
#endif

// SDL
#include <SDL.h>
#include <SDL_syswm.h>
#include <cstdio>              // for snprintf()
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#endif
#undef Status // X11 headers are leaking this.

#if SDL_VERSION_ATLEAST(2,0,4) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS) && !defined(__amigaos4__)
#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    1
#else
#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    0
#endif
#define SDL_HAS_PER_MONITOR_DPI             SDL_VERSION_ATLEAST(2,0,4)
#define SDL_HAS_VULKAN                      SDL_VERSION_ATLEAST(2,0,6)
#define SDL_HAS_OPEN_URL                    SDL_VERSION_ATLEAST(2,0,14)
#if SDL_HAS_VULKAN
#include <SDL_vulkan.h>
#endif

// SDL Data
struct VanGui_ImplSDL2_Data
{
    SDL_Window*             Window;
    Uint32                  WindowID;       // Stored in VanGuiViewport::PlatformHandle. Use SDL_GetWindowFromID() to get SDL_Window* from Uint32 WindowID.
    SDL_Renderer*           Renderer;
    Uint64                  Time;
    char*                   ClipboardTextData;
    char                    BackendPlatformName[48];

    // Mouse handling
    Uint32                  MouseWindowID;
    int                     MouseButtonsDown;
    SDL_Cursor*             MouseCursors[VanGuiMouseCursor_COUNT];
    SDL_Cursor*             MouseLastCursor;
    int                     MouseLastLeaveFrame;
    bool                    MouseCanUseGlobalState;
    VanGui_ImplSDL2_MouseCaptureMode MouseCaptureMode;

    // Gamepad handling
    VanVector<SDL_GameController*> Gamepads;
    VanGui_ImplSDL2_GamepadMode    GamepadMode;
    bool                          WantUpdateGamepadsList;

    // Monitor handling
    bool                          WantUpdateMonitors;

    VanGui_ImplSDL2_Data()   { memset(static_cast<void*>(this), 0, sizeof(*this)); }
};

// Viewport data stored in VanGuiViewport::PlatformUserData for secondary viewports
struct VanGui_ImplSDL2_ViewportData
{
    SDL_Window*     Window      = nullptr;
    Uint32          WindowID    = 0;
    bool            WindowOwned = false;

    VanGui_ImplSDL2_ViewportData()  = default;
    ~VanGui_ImplSDL2_ViewportData() = default;
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static VanGui_ImplSDL2_Data* VanGui_ImplSDL2_GetBackendData()
{
    return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplSDL2_Data*>(VanGui::GetIO().BackendPlatformUserData) : nullptr;
}

// Functions
static const char* VanGui_ImplSDL2_GetClipboardText(VanGuiContext*)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    if (SDL_HasClipboardText())
        bd->ClipboardTextData = SDL_GetClipboardText();
    else
        bd->ClipboardTextData = nullptr;
    return bd->ClipboardTextData;
}

static void VanGui_ImplSDL2_SetClipboardText(VanGuiContext*, const char* text)
{
    SDL_SetClipboardText(text);
}

// Note: native IME will only display if user calls SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1") _before_ SDL_CreateWindow().
static void VanGui_ImplSDL2_PlatformSetImeData(VanGuiContext*, VanGuiViewport*, VanGuiPlatformImeData* data)
{
    if (data->WantVisible)
    {
        SDL_Rect r;
        r.x = static_cast<int>(data->InputPos.x);
        r.y = static_cast<int>(data->InputPos.y);
        r.w = 1;
        r.h = static_cast<int>(data->InputLineHeight);
        SDL_SetTextInputRect(&r);
    }
}

// Not static to allow third-party code to use that if they want to (but undocumented)
VanGuiKey VanGui_ImplSDL2_KeyEventToVanGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);
VanGuiKey VanGui_ImplSDL2_KeyEventToVanGuiKey(SDL_Keycode keycode, SDL_Scancode scancode)
{
    switch (keycode)
    {
        case SDLK_TAB: return VanGuiKey_Tab;
        case SDLK_LEFT: return VanGuiKey_LeftArrow;
        case SDLK_RIGHT: return VanGuiKey_RightArrow;
        case SDLK_UP: return VanGuiKey_UpArrow;
        case SDLK_DOWN: return VanGuiKey_DownArrow;
        case SDLK_PAGEUP: return VanGuiKey_PageUp;
        case SDLK_PAGEDOWN: return VanGuiKey_PageDown;
        case SDLK_HOME: return VanGuiKey_Home;
        case SDLK_END: return VanGuiKey_End;
        case SDLK_INSERT: return VanGuiKey_Insert;
        case SDLK_DELETE: return VanGuiKey_Delete;
        case SDLK_BACKSPACE: return VanGuiKey_Backspace;
        case SDLK_SPACE: return VanGuiKey_Space;
        case SDLK_RETURN: return VanGuiKey_Enter;
        case SDLK_ESCAPE: return VanGuiKey_Escape;
        //case SDLK_QUOTE: return VanGuiKey_Apostrophe;
        case SDLK_COMMA: return VanGuiKey_Comma;
        //case SDLK_MINUS: return VanGuiKey_Minus;
        case SDLK_PERIOD: return VanGuiKey_Period;
        //case SDLK_SLASH: return VanGuiKey_Slash;
        case SDLK_SEMICOLON: return VanGuiKey_Semicolon;
        //case SDLK_EQUALS: return VanGuiKey_Equal;
        //case SDLK_LEFTBRACKET: return VanGuiKey_LeftBracket;
        //case SDLK_BACKSLASH: return VanGuiKey_Backslash;
        //case SDLK_RIGHTBRACKET: return VanGuiKey_RightBracket;
        //case SDLK_BACKQUOTE: return VanGuiKey_GraveAccent;
        case SDLK_CAPSLOCK: return VanGuiKey_CapsLock;
        case SDLK_SCROLLLOCK: return VanGuiKey_ScrollLock;
        case SDLK_NUMLOCKCLEAR: return VanGuiKey_NumLock;
        case SDLK_PRINTSCREEN: return VanGuiKey_PrintScreen;
        case SDLK_PAUSE: return VanGuiKey_Pause;
        case SDLK_KP_0: return VanGuiKey_Keypad0;
        case SDLK_KP_1: return VanGuiKey_Keypad1;
        case SDLK_KP_2: return VanGuiKey_Keypad2;
        case SDLK_KP_3: return VanGuiKey_Keypad3;
        case SDLK_KP_4: return VanGuiKey_Keypad4;
        case SDLK_KP_5: return VanGuiKey_Keypad5;
        case SDLK_KP_6: return VanGuiKey_Keypad6;
        case SDLK_KP_7: return VanGuiKey_Keypad7;
        case SDLK_KP_8: return VanGuiKey_Keypad8;
        case SDLK_KP_9: return VanGuiKey_Keypad9;
        case SDLK_KP_PERIOD: return VanGuiKey_KeypadDecimal;
        case SDLK_KP_DIVIDE: return VanGuiKey_KeypadDivide;
        case SDLK_KP_MULTIPLY: return VanGuiKey_KeypadMultiply;
        case SDLK_KP_MINUS: return VanGuiKey_KeypadSubtract;
        case SDLK_KP_PLUS: return VanGuiKey_KeypadAdd;
        case SDLK_KP_ENTER: return VanGuiKey_KeypadEnter;
        case SDLK_KP_EQUALS: return VanGuiKey_KeypadEqual;
        case SDLK_LCTRL: return VanGuiKey_LeftCtrl;
        case SDLK_LSHIFT: return VanGuiKey_LeftShift;
        case SDLK_LALT: return VanGuiKey_LeftAlt;
        case SDLK_LGUI: return VanGuiKey_LeftSuper;
        case SDLK_RCTRL: return VanGuiKey_RightCtrl;
        case SDLK_RSHIFT: return VanGuiKey_RightShift;
        case SDLK_RALT: return VanGuiKey_RightAlt;
        case SDLK_RGUI: return VanGuiKey_RightSuper;
        case SDLK_APPLICATION: return VanGuiKey_Menu;
        case SDLK_0: return VanGuiKey_0;
        case SDLK_1: return VanGuiKey_1;
        case SDLK_2: return VanGuiKey_2;
        case SDLK_3: return VanGuiKey_3;
        case SDLK_4: return VanGuiKey_4;
        case SDLK_5: return VanGuiKey_5;
        case SDLK_6: return VanGuiKey_6;
        case SDLK_7: return VanGuiKey_7;
        case SDLK_8: return VanGuiKey_8;
        case SDLK_9: return VanGuiKey_9;
        case SDLK_a: return VanGuiKey_A;
        case SDLK_b: return VanGuiKey_B;
        case SDLK_c: return VanGuiKey_C;
        case SDLK_d: return VanGuiKey_D;
        case SDLK_e: return VanGuiKey_E;
        case SDLK_f: return VanGuiKey_F;
        case SDLK_g: return VanGuiKey_G;
        case SDLK_h: return VanGuiKey_H;
        case SDLK_i: return VanGuiKey_I;
        case SDLK_j: return VanGuiKey_J;
        case SDLK_k: return VanGuiKey_K;
        case SDLK_l: return VanGuiKey_L;
        case SDLK_m: return VanGuiKey_M;
        case SDLK_n: return VanGuiKey_N;
        case SDLK_o: return VanGuiKey_O;
        case SDLK_p: return VanGuiKey_P;
        case SDLK_q: return VanGuiKey_Q;
        case SDLK_r: return VanGuiKey_R;
        case SDLK_s: return VanGuiKey_S;
        case SDLK_t: return VanGuiKey_T;
        case SDLK_u: return VanGuiKey_U;
        case SDLK_v: return VanGuiKey_V;
        case SDLK_w: return VanGuiKey_W;
        case SDLK_x: return VanGuiKey_X;
        case SDLK_y: return VanGuiKey_Y;
        case SDLK_z: return VanGuiKey_Z;
        case SDLK_F1: return VanGuiKey_F1;
        case SDLK_F2: return VanGuiKey_F2;
        case SDLK_F3: return VanGuiKey_F3;
        case SDLK_F4: return VanGuiKey_F4;
        case SDLK_F5: return VanGuiKey_F5;
        case SDLK_F6: return VanGuiKey_F6;
        case SDLK_F7: return VanGuiKey_F7;
        case SDLK_F8: return VanGuiKey_F8;
        case SDLK_F9: return VanGuiKey_F9;
        case SDLK_F10: return VanGuiKey_F10;
        case SDLK_F11: return VanGuiKey_F11;
        case SDLK_F12: return VanGuiKey_F12;
        case SDLK_F13: return VanGuiKey_F13;
        case SDLK_F14: return VanGuiKey_F14;
        case SDLK_F15: return VanGuiKey_F15;
        case SDLK_F16: return VanGuiKey_F16;
        case SDLK_F17: return VanGuiKey_F17;
        case SDLK_F18: return VanGuiKey_F18;
        case SDLK_F19: return VanGuiKey_F19;
        case SDLK_F20: return VanGuiKey_F20;
        case SDLK_F21: return VanGuiKey_F21;
        case SDLK_F22: return VanGuiKey_F22;
        case SDLK_F23: return VanGuiKey_F23;
        case SDLK_F24: return VanGuiKey_F24;
        case SDLK_AC_BACK: return VanGuiKey_AppBack;
        case SDLK_AC_FORWARD: return VanGuiKey_AppForward;
        default: break;
    }

    // Fallback to scancode
    switch (scancode)
    {
    case SDL_SCANCODE_GRAVE: return VanGuiKey_GraveAccent;
    case SDL_SCANCODE_MINUS: return VanGuiKey_Minus;
    case SDL_SCANCODE_EQUALS: return VanGuiKey_Equal;
    case SDL_SCANCODE_LEFTBRACKET: return VanGuiKey_LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET: return VanGuiKey_RightBracket;
    case SDL_SCANCODE_NONUSBACKSLASH: return VanGuiKey_Oem102;
    case SDL_SCANCODE_BACKSLASH: return VanGuiKey_Backslash;
    case SDL_SCANCODE_SEMICOLON: return VanGuiKey_Semicolon;
    case SDL_SCANCODE_APOSTROPHE: return VanGuiKey_Apostrophe;
    case SDL_SCANCODE_COMMA: return VanGuiKey_Comma;
    case SDL_SCANCODE_PERIOD: return VanGuiKey_Period;
    case SDL_SCANCODE_SLASH: return VanGuiKey_Slash;
    default: break;
    }
    return VanGuiKey_None;
}

static void VanGui_ImplSDL2_UpdateKeyModifiers(SDL_Keymod sdl_key_mods)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(VanGuiMod_Ctrl, (sdl_key_mods & KMOD_CTRL) != 0);
    io.AddKeyEvent(VanGuiMod_Shift, (sdl_key_mods & KMOD_SHIFT) != 0);
    io.AddKeyEvent(VanGuiMod_Alt, (sdl_key_mods & KMOD_ALT) != 0);
    io.AddKeyEvent(VanGuiMod_Super, (sdl_key_mods & KMOD_GUI) != 0);
}

static VanGuiViewport* VanGui_ImplSDL2_GetViewportForWindowID(Uint32 window_id)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    if (window_id == bd->WindowID)
        return VanGui::GetMainViewport();
    if (VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO(); true)
        for (VanGuiViewport* viewport : platform_io.Viewports)
            if (viewport->PlatformHandle != nullptr)
                if (static_cast<Uint32>(reinterpret_cast<intptr_t>(viewport->PlatformHandle)) == window_id)
                    return viewport;
    return nullptr;
}

// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if VanGUI wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to VanGUI, and hide them from your application based on those two flags.
bool VanGui_ImplSDL2_ProcessEvent(const SDL_Event* event)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplSDL2_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    switch (event->type)
    {
        case SDL_MOUSEMOTION:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->motion.windowID) == nullptr)
                return false;
            VanVec2 mouse_pos(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID ? VanGuiMouseSource_TouchScreen : VanGuiMouseSource_Mouse);
            io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
            return true;
        }
        case SDL_MOUSEWHEEL:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->wheel.windowID) == nullptr)
                return false;
            //VANGUI_DEBUG_LOG("wheel %.2f %.2f, precise %.2f %.2f\n", (float)event->wheel.x, (float)event->wheel.y, event->wheel.preciseX, event->wheel.preciseY);
#if SDL_VERSION_ATLEAST(2,0,18) // If this fails to compile on Emscripten: update to latest Emscripten!
            float wheel_x = -event->wheel.preciseX;
            float wheel_y = event->wheel.preciseY;
#else
            float wheel_x = -static_cast<float>(event->wheel.x);
            float wheel_y = static_cast<float>(event->wheel.y);
#endif
#if defined(__EMSCRIPTEN__) && !SDL_VERSION_ATLEAST(2,31,0)
            wheel_x /= 100.0f;
#endif
            io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? VanGuiMouseSource_TouchScreen : VanGuiMouseSource_Mouse);
            io.AddMouseWheelEvent(wheel_x, wheel_y);
            return true;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->button.windowID) == nullptr)
                return false;
            int mouse_button = -1;
            if (event->button.button == SDL_BUTTON_LEFT) { mouse_button = 0; }
            if (event->button.button == SDL_BUTTON_RIGHT) { mouse_button = 1; }
            if (event->button.button == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
            if (event->button.button == SDL_BUTTON_X1) { mouse_button = 3; }
            if (event->button.button == SDL_BUTTON_X2) { mouse_button = 4; }
            if (mouse_button == -1)
                break;
            io.AddMouseSourceEvent(event->button.which == SDL_TOUCH_MOUSEID ? VanGuiMouseSource_TouchScreen : VanGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(mouse_button, (event->type == SDL_MOUSEBUTTONDOWN));
            bd->MouseButtonsDown = (event->type == SDL_MOUSEBUTTONDOWN) ? (bd->MouseButtonsDown | (1 << mouse_button)) : (bd->MouseButtonsDown & ~(1 << mouse_button));
            return true;
        }
        case SDL_TEXTINPUT:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->text.windowID) == nullptr)
                return false;
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->key.windowID) == nullptr)
                return false;
            VanGui_ImplSDL2_UpdateKeyModifiers(static_cast<SDL_Keymod>(event->key.keysym.mod));
            //VANGUI_DEBUG_LOG("SDL_KEY_%s : key=%d ('%s'), scancode=%d ('%s'), mod=%X\n",
            //    (event->type == SDL_KEYDOWN) ? "DOWN" : "UP  ", event->key.keysym.sym, SDL_GetKeyName(event->key.keysym.sym), event->key.keysym.scancode, SDL_GetScancodeName(event->key.keysym.scancode), event->key.keysym.mod);
            VanGuiKey key = VanGui_ImplSDL2_KeyEventToVanGuiKey(event->key.keysym.sym, event->key.keysym.scancode);
            io.AddKeyEvent(key, (event->type == SDL_KEYDOWN));
            io.SetKeyEventNativeData(key, static_cast<int>(event->key.keysym.sym), static_cast<int>(event->key.keysym.scancode), static_cast<int>(event->key.keysym.scancode)); // To support legacy indexing (<1.87 user code). Legacy backend uses SDLK_*** as indices to IsKeyXXX() functions.
            return true;
        }
        case SDL_WINDOWEVENT:
        {
            if (VanGui_ImplSDL2_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            // - When capturing mouse, SDL will send a bunch of conflicting LEAVE/ENTER event on every mouse move, but the final ENTER tends to be right.
            // - However we won't get a correct LEAVE event for a captured window.
            // - In some cases, when detaching a window from main viewport SDL may send SDL_WINDOWEVENT_ENTER one frame too late,
            //   causing SDL_WINDOWEVENT_LEAVE on previous frame to interrupt drag operation by clear mouse position. This is why
            //   we delay process the SDL_WINDOWEVENT_LEAVE events by one frame. See issue #5012 for details.
            Uint8 window_event = event->window.event;
            if (window_event == SDL_WINDOWEVENT_ENTER)
            {
                bd->MouseWindowID = event->window.windowID;
                bd->MouseLastLeaveFrame = 0;
            }
            if (window_event == SDL_WINDOWEVENT_LEAVE)
                bd->MouseLastLeaveFrame = VanGui::GetFrameCount() + 1;
            if (window_event == SDL_WINDOWEVENT_FOCUS_GAINED)
                io.AddFocusEvent(true);
            if (window_event == SDL_WINDOWEVENT_FOCUS_LOST)
                io.AddFocusEvent(false);
            return true;
        }
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        {
            bd->WantUpdateGamepadsList = true;
            return true;
        }
#if SDL_VERSION_ATLEAST(2, 0, 9)
        case SDL_DISPLAYEVENT:
        {
            bd->WantUpdateMonitors = true;
            return true;
        }
#endif
        default:
            break;
    }
    return false;
}

#ifdef __EMSCRIPTEN__
EM_JS(void, VanGui_ImplSDL2_EmscriptenOpenURL, (char const* url), { url = url ? UTF8ToString(url) : null; if (url) window.open(url, '_blank'); });
#endif

static bool VanGui_ImplSDL2_Init(SDL_Window* window, SDL_Renderer* renderer, void* sdl_gl_context)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");
    //SDL_SetHint(SDL_HINT_EVENT_LOGGING, "2");

    // Obtain compiled and runtime versions
    SDL_version ver_compiled;
    SDL_version ver_runtime;
    SDL_VERSION(&ver_compiled);
    SDL_GetVersion(&ver_runtime);

    // Setup backend capabilities flags
    VanGui_ImplSDL2_Data* bd = VAN_NEW(VanGui_ImplSDL2_Data)();
    snprintf(bd->BackendPlatformName, sizeof(bd->BackendPlatformName), "vangui_impl_sdl2 (%u.%u.%u, %u.%u.%u)",
        ver_compiled.major, ver_compiled.minor, ver_compiled.patch, ver_runtime.major, ver_runtime.minor, ver_runtime.patch);
    io.BackendPlatformUserData = static_cast<void*>(bd);
    io.BackendPlatformName = bd->BackendPlatformName;
    io.BackendFlags |= VanGuiBackendFlags_HasMouseCursors;           // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= VanGuiBackendFlags_HasSetMousePos;            // We can honor io.WantSetMousePos requests (optional, rarely used)

    bd->Window = window;
    bd->WindowID = SDL_GetWindowID(window);
    bd->Renderer = renderer;

    // Check and store if we are on a SDL backend that supports SDL_GetGlobalMouseState() and SDL_CaptureMouse()
    // ("wayland" and "rpi" don't support it, but we chose to use a white-list instead of a black-list)
    bd->MouseCanUseGlobalState = false;
    bd->MouseCaptureMode = VanGui_ImplSDL2_MouseCaptureMode_Disabled;
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
    const char* sdl_backend = SDL_GetCurrentVideoDriver();
    const char* capture_and_global_state_whitelist[] = { "windows", "cocoa", "x11", "DIVE", "VMAN" };
    for (const char* item : capture_and_global_state_whitelist)
        if (strncmp(sdl_backend, item, strlen(item)) == 0)
        {
            bd->MouseCanUseGlobalState = true;
            bd->MouseCaptureMode = (strcmp(item, "x11") == 0) ? VanGui_ImplSDL2_MouseCaptureMode_EnabledAfterDrag : VanGui_ImplSDL2_MouseCaptureMode_Enabled;
        }
#endif

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = VanGui_ImplSDL2_SetClipboardText;
    platform_io.Platform_GetClipboardTextFn = VanGui_ImplSDL2_GetClipboardText;
    platform_io.Platform_ClipboardUserData = nullptr;
    platform_io.Platform_SetImeDataFn = VanGui_ImplSDL2_PlatformSetImeData;
#ifdef __EMSCRIPTEN__
    platform_io.Platform_OpenInShellFn = [](VanGuiContext*, const char* url) { VanGui_ImplSDL2_EmscriptenOpenURL(url); return true; };
#elif SDL_HAS_OPEN_URL
    platform_io.Platform_OpenInShellFn = [](VanGuiContext*, const char* url) { return SDL_OpenURL(url) == 0; };
#endif

    // Gamepad handling
    bd->GamepadMode = VanGui_ImplSDL2_GamepadMode_AutoFirst;
    bd->WantUpdateGamepadsList = true;

    // Monitor handling
    bd->WantUpdateMonitors = true;

    // Load mouse cursors
    bd->MouseCursors[VanGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    bd->MouseCursors[VanGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    bd->MouseCursors[VanGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    bd->MouseCursors[VanGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
    bd->MouseCursors[VanGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    bd->MouseCursors[VanGuiMouseCursor_Wait] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    bd->MouseCursors[VanGuiMouseCursor_Progress] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAITARROW);
    bd->MouseCursors[VanGuiMouseCursor_NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);

    // Set platform dependent data in viewport
    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    main_viewport->PlatformHandle = reinterpret_cast<void*>(static_cast<intptr_t>(bd->WindowID));
    main_viewport->PlatformHandleRaw = nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info))
    {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        main_viewport->PlatformHandleRaw = static_cast<void*>(info.info.win.window);
#elif defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_COCOA)
        main_viewport->PlatformHandleRaw = static_cast<void*>(info.info.cocoa.window);
#endif
    }

    // From 2.0.5: Set SDL hint to receive mouse click events on window focus, otherwise SDL doesn't emit the event.
    // Without this, when clicking to gain focus, our widgets wouldn't activate even though they showed as hovered.
    // (This is unfortunately a global SDL setting, so enabling it might have a side-effect on your application.
    // It is unlikely to make a difference, but if your app absolutely needs to ignore the initial on-focus click:
    // you can ignore SDL_MOUSEBUTTONDOWN events coming right after a SDL_WINDOWEVENT_FOCUS_GAINED)
#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

    // From 2.0.18: Enable native IME.
    // IMPORTANT: This is used at the time of SDL_CreateWindow() so this will only affects secondary windows, if any.
    // For the main window to be affected, your application needs to call this manually before calling SDL_CreateWindow().
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // From 2.0.22: Disable auto-capture, this is preventing drag and drop across multiple windows (see #5710)
#ifdef SDL_HINT_MOUSE_AUTO_CAPTURE
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
#endif

    (void)sdl_gl_context; // Unused in 'master' branch.
    VanGui_ImplSDL2_InitPlatformInterface();
    return true;
}

bool VanGui_ImplSDL2_InitForOpenGL(SDL_Window* window, void* sdl_gl_context)
{
    return VanGui_ImplSDL2_Init(window, nullptr, sdl_gl_context);
}

bool VanGui_ImplSDL2_InitForVulkan(SDL_Window* window)
{
#if !SDL_HAS_VULKAN
    VAN_ASSERT(0 && "Unsupported");
#endif
    return VanGui_ImplSDL2_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL2_InitForD3D(SDL_Window* window)
{
#if !defined(_WIN32)
    VAN_ASSERT(0 && "Unsupported");
#endif
    return VanGui_ImplSDL2_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL2_InitForMetal(SDL_Window* window)
{
    return VanGui_ImplSDL2_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL2_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer)
{
    return VanGui_ImplSDL2_Init(window, renderer, nullptr);
}

bool VanGui_ImplSDL2_InitForOther(SDL_Window* window)
{
    return VanGui_ImplSDL2_Init(window, nullptr, nullptr);
}

static void VanGui_ImplSDL2_CloseGamepads();

void VanGui_ImplSDL2_Shutdown()
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    VanGui_ImplSDL2_ShutdownPlatformInterface();
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    for (VanGuiMouseCursor cursor_n = 0; cursor_n < VanGuiMouseCursor_COUNT; cursor_n++)
        SDL_FreeCursor(bd->MouseCursors[cursor_n]);
    VanGui_ImplSDL2_CloseGamepads();

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_HasMouseCursors | VanGuiBackendFlags_HasSetMousePos | VanGuiBackendFlags_HasGamepad);
    platform_io.ClearPlatformHandlers();
    VAN_DELETE(bd);
}

void VanGui_ImplSDL2_SetMouseCaptureMode(VanGui_ImplSDL2_MouseCaptureMode mode)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    if (mode == VanGui_ImplSDL2_MouseCaptureMode_Disabled && bd->MouseCaptureMode != VanGui_ImplSDL2_MouseCaptureMode_Disabled)
        SDL_CaptureMouse(SDL_FALSE);
    bd->MouseCaptureMode = mode;
}

static void VanGui_ImplSDL2_UpdateMouseData()
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();

    // We forward mouse input when hovered or captured (via SDL_MOUSEMOTION) or when focused (below)
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
    // - SDL_CaptureMouse() let the OS know e.g. that our drags can extend outside of parent boundaries (we want updated position) and shouldn't trigger other operations outside.
    // - Debuggers under Linux tends to leave captured mouse on break, which may be very inconvenient, so to mitigate the issue on X11 we we wait until mouse has moved to begin capture.
    if (bd->MouseCaptureMode == VanGui_ImplSDL2_MouseCaptureMode_Enabled)
    {
        SDL_CaptureMouse((bd->MouseButtonsDown != 0) ? SDL_TRUE : SDL_FALSE);
    }
    else if (bd->MouseCaptureMode == VanGui_ImplSDL2_MouseCaptureMode_EnabledAfterDrag)
    {
        bool want_capture = false;
        for (int button_n = 0; button_n < VanGuiMouseButton_COUNT && !want_capture; button_n++)
            if (VanGui::IsMouseDragging(button_n, 1.0f))
                want_capture = true;
        SDL_CaptureMouse(want_capture ? SDL_TRUE : SDL_FALSE);
    }

    SDL_Window* focused_window = SDL_GetKeyboardFocus();
    const bool is_app_focused = (bd->Window == focused_window);
#else
    SDL_Window* focused_window = bd->Window;
    const bool is_app_focused = (SDL_GetWindowFlags(bd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0; // SDL 2.0.3 and non-windowed systems: single-viewport only
#endif
    if (is_app_focused)
    {
        // (Optional) Set OS mouse position from VanGUI if requested (rarely used, only when io.ConfigNavMoveSetMousePos is enabled by user)
        if (io.WantSetMousePos)
            SDL_WarpMouseInWindow(bd->Window, static_cast<int>(io.MousePos.x), static_cast<int>(io.MousePos.y));

        // (Optional) Fallback to provide unclamped mouse position when focused but not hovered (SDL_MOUSEMOTION already provides this when hovered or captured)
        // Note that SDL_GetGlobalMouseState() is in theory slow on X11, but this only runs on rather specific cases. If a problem we may provide a way to opt-out this feature.
        SDL_Window* hovered_window = SDL_GetMouseFocus();
        const bool is_relative_mouse_mode = SDL_GetRelativeMouseMode() != 0;
        if (hovered_window == nullptr && bd->MouseCanUseGlobalState && bd->MouseButtonsDown == 0 && !is_relative_mouse_mode)
        {
            // Single-viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
            int mouse_x, mouse_y;
            int window_x, window_y;
            SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
            SDL_GetWindowPosition(focused_window, &window_x, &window_y);
            mouse_x -= window_x;
            mouse_y -= window_y;
            io.AddMousePosEvent(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
        }
    }
}

static void VanGui_ImplSDL2_UpdateMouseCursor()
{
    VanGuiIO& io = VanGui::GetIO();
    if (io.ConfigFlags & VanGuiConfigFlags_NoMouseCursorChange)
        return;
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();

    VanGuiMouseCursor vangui_cursor = VanGui::GetMouseCursor();
    if (io.MouseDrawCursor || vangui_cursor == VanGuiMouseCursor_None)
    {
        // Hide OS mouse cursor if vangui is drawing it or if it wants no cursor
        SDL_ShowCursor(SDL_FALSE);
    }
    else
    {
        // Show OS mouse cursor
        SDL_Cursor* expected_cursor = bd->MouseCursors[vangui_cursor] ? bd->MouseCursors[vangui_cursor] : bd->MouseCursors[VanGuiMouseCursor_Arrow];
        if (bd->MouseLastCursor != expected_cursor)
        {
            SDL_SetCursor(expected_cursor); // SDL function doesn't have an early out (see #6113)
            bd->MouseLastCursor = expected_cursor;
        }
        SDL_ShowCursor(SDL_TRUE);
    }
}

// - On Windows the process needs to be marked DPI-aware!! SDL2 doesn't do it by default. You can call ::SetProcessDPIAware() or call VanGui_ImplWin32_EnableDpiAwareness() from Win32 backend.
// - Apple platforms use FramebufferScale so we always return 1.0f.
// - Some accessibility applications are declaring virtual monitors with a DPI of 0.0f, see #7902. We preserve this value for caller to handle.
float VanGui_ImplSDL2_GetContentScaleForWindow(SDL_Window* window)
{
    return VanGui_ImplSDL2_GetContentScaleForDisplay(SDL_GetWindowDisplayIndex(window));
}

// SDL_GetDisplayDPI() seems rather unreliable on Linux.
float VanGui_ImplSDL2_GetContentScaleForDisplay(int display_index)
{
    const char* sdl_driver = SDL_GetCurrentVideoDriver();
    if (sdl_driver && strcmp(sdl_driver, "wayland") == 0)
        return 1.0f;
#if SDL_HAS_PER_MONITOR_DPI
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    float dpi = 0.0f;
    if (SDL_GetDisplayDPI(display_index, &dpi, nullptr, nullptr) == 0)
    {
        if (dpi < 96.0f)
            dpi = 96.0f;
        return dpi / 96.0f;
    }
#endif
#endif
    VAN_UNUSED(display_index);
    return 1.0f;
}

static void VanGui_ImplSDL2_CloseGamepads()
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    if (bd->GamepadMode != VanGui_ImplSDL2_GamepadMode_Manual)
        for (SDL_GameController* gamepad : bd->Gamepads)
            SDL_GameControllerClose(gamepad);
    bd->Gamepads.resize(0);
}

void VanGui_ImplSDL2_SetGamepadMode(VanGui_ImplSDL2_GamepadMode mode, struct _SDL_GameController** manual_gamepads_array, int manual_gamepads_count)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VanGui_ImplSDL2_CloseGamepads();
    if (mode == VanGui_ImplSDL2_GamepadMode_Manual)
    {
        VAN_ASSERT(manual_gamepads_array != nullptr || manual_gamepads_count <= 0);
        for (int n = 0; n < manual_gamepads_count; n++)
            bd->Gamepads.push_back(manual_gamepads_array[n]);
    }
    else
    {
        VAN_ASSERT(manual_gamepads_array == nullptr && manual_gamepads_count <= 0);
        bd->WantUpdateGamepadsList = true;
    }
    bd->GamepadMode = mode;
}

static void VanGui_ImplSDL2_UpdateGamepadButton(VanGui_ImplSDL2_Data* bd, VanGuiIO& io, VanGuiKey key, SDL_GameControllerButton button_no)
{
    bool merged_value = false;
    for (SDL_GameController* gamepad : bd->Gamepads)
        merged_value |= SDL_GameControllerGetButton(gamepad, button_no) != 0;
    io.AddKeyEvent(key, merged_value);
}

static inline float Saturate(float v) { return v < 0.0f ? 0.0f : v  > 1.0f ? 1.0f : v; }
static void VanGui_ImplSDL2_UpdateGamepadAnalog(VanGui_ImplSDL2_Data* bd, VanGuiIO& io, VanGuiKey key, SDL_GameControllerAxis axis_no, float v0, float v1)
{
    float merged_value = 0.0f;
    for (SDL_GameController* gamepad : bd->Gamepads)
    {
        float vn = Saturate(static_cast<float>(SDL_GameControllerGetAxis(gamepad, axis_no) - v0) / static_cast<float>(v1 - v0));
        if (merged_value < vn)
            merged_value = vn;
    }
    io.AddKeyAnalogEvent(key, merged_value > 0.1f, merged_value);
}

static void VanGui_ImplSDL2_UpdateGamepads()
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();

    // Update list of controller(s) to use
    if (bd->WantUpdateGamepadsList && bd->GamepadMode != VanGui_ImplSDL2_GamepadMode_Manual)
    {
        VanGui_ImplSDL2_CloseGamepads();
        int joystick_count = SDL_NumJoysticks();
        for (int n = 0; n < joystick_count; n++)
            if (SDL_IsGameController(n))
                if (SDL_GameController* gamepad = SDL_GameControllerOpen(n))
                {
                    bd->Gamepads.push_back(gamepad);
                    if (bd->GamepadMode == VanGui_ImplSDL2_GamepadMode_AutoFirst)
                        break;
                }
        bd->WantUpdateGamepadsList = false;
    }

    io.BackendFlags &= ~VanGuiBackendFlags_HasGamepad;
    if (bd->Gamepads.Size == 0)
        return;
    io.BackendFlags |= VanGuiBackendFlags_HasGamepad;

    // Update gamepad inputs
    const int thumb_dead_zone = 8000; // SDL_gamecontroller.h suggests using this value.
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadStart,       SDL_CONTROLLER_BUTTON_START);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadBack,        SDL_CONTROLLER_BUTTON_BACK);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceLeft,    SDL_CONTROLLER_BUTTON_X);              // Xbox X, PS Square
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceRight,   SDL_CONTROLLER_BUTTON_B);              // Xbox B, PS Circle
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceUp,      SDL_CONTROLLER_BUTTON_Y);              // Xbox Y, PS Triangle
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceDown,    SDL_CONTROLLER_BUTTON_A);              // Xbox A, PS Cross
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadLeft,    SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadRight,   SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadUp,      SDL_CONTROLLER_BUTTON_DPAD_UP);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadDown,    SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadL1,          SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadR1,          SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadL2,          SDL_CONTROLLER_AXIS_TRIGGERLEFT,  0.0f, 32767);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadR2,          SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.0f, 32767);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadL3,          SDL_CONTROLLER_BUTTON_LEFTSTICK);
    VanGui_ImplSDL2_UpdateGamepadButton(bd, io, VanGuiKey_GamepadR3,          SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickLeft,  SDL_CONTROLLER_AXIS_LEFTX,  -thumb_dead_zone, -32768);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickRight, SDL_CONTROLLER_AXIS_LEFTX,  +thumb_dead_zone, +32767);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickUp,    SDL_CONTROLLER_AXIS_LEFTY,  -thumb_dead_zone, -32768);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickDown,  SDL_CONTROLLER_AXIS_LEFTY,  +thumb_dead_zone, +32767);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickLeft,  SDL_CONTROLLER_AXIS_RIGHTX, -thumb_dead_zone, -32768);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickRight, SDL_CONTROLLER_AXIS_RIGHTX, +thumb_dead_zone, +32767);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickUp,    SDL_CONTROLLER_AXIS_RIGHTY, -thumb_dead_zone, -32768);
    VanGui_ImplSDL2_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickDown,  SDL_CONTROLLER_AXIS_RIGHTY, +thumb_dead_zone, +32767);
}

static void VanGui_ImplSDL2_UpdateMonitors()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Monitors.resize(0);
    const int display_count = SDL_GetNumVideoDisplays();
    for (int n = 0; n < display_count; n++)
    {
        VanGuiPlatformMonitor monitor = {};
        SDL_Rect r;
        SDL_GetDisplayBounds(n, &r);
        monitor.MainPos  = VanVec2(static_cast<float>(r.x), static_cast<float>(r.y));
        monitor.MainSize = VanVec2(static_cast<float>(r.w), static_cast<float>(r.h));
        SDL_GetDisplayUsableBounds(n, &r);
        monitor.WorkPos  = VanVec2(static_cast<float>(r.x), static_cast<float>(r.y));
        monitor.WorkSize = VanVec2(static_cast<float>(r.w), static_cast<float>(r.h));
        float dpi = 0.0f;
        if (SDL_GetDisplayDPI(n, &dpi, nullptr, nullptr) == 0)
            monitor.DpiScale = dpi / 96.0f;
        else
            monitor.DpiScale = 1.0f;
        monitor.PlatformHandle = nullptr; // SDL2 doesn't expose an opaque monitor handle
        platform_io.Monitors.push_back(monitor);
    }
}

static void VanGui_ImplSDL2_GetWindowSizeAndFramebufferScale(SDL_Window* window, SDL_Renderer* renderer, VanVec2* out_size, VanVec2* out_framebuffer_scale)
{
    int w, h;
    int display_w, display_h;
    SDL_GetWindowSize(window, &w, &h);
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        w = h = 0;
    if (renderer != nullptr)
        SDL_GetRendererOutputSize(renderer, &display_w, &display_h);
#if SDL_HAS_VULKAN
    else if (SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)
        SDL_Vulkan_GetDrawableSize(window, &display_w, &display_h);
#endif
    else
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
    if (out_size != nullptr)
        *out_size = VanVec2(static_cast<float>(w), static_cast<float>(h));
    if (out_framebuffer_scale != nullptr)
        *out_framebuffer_scale = (w > 0 && h > 0) ? VanVec2(static_cast<float>(display_w) / static_cast<float>(w), static_cast<float>(display_h) / static_cast<float>(h)) : VanVec2(1.0f, 1.0f);
}

void VanGui_ImplSDL2_NewFrame()
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplSDL2_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    // Setup main viewport size (every frame to accommodate for window resizing)
    VanGui_ImplSDL2_GetWindowSizeAndFramebufferScale(bd->Window, bd->Renderer, &io.DisplaySize, &io.DisplayFramebufferScale);

    // Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
    // (Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. Happens in VMs and Emscripten, see #6189, #6114, #3644)
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    if (current_time <= bd->Time)
        current_time = bd->Time + 1;
    io.DeltaTime = bd->Time > 0 ? static_cast<float>(static_cast<double>(current_time - bd->Time) / static_cast<double>(frequency)) : static_cast<float>(1.0f / 60.0f);
    bd->Time = current_time;

    if (bd->MouseLastLeaveFrame && bd->MouseLastLeaveFrame >= VanGui::GetFrameCount() && bd->MouseButtonsDown == 0)
    {
        bd->MouseWindowID = 0;
        bd->MouseLastLeaveFrame = 0;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // Update monitors (initially and when a display change event is received)
    if (bd->WantUpdateMonitors)
    {
        VanGui_ImplSDL2_UpdateMonitors();
        bd->WantUpdateMonitors = false;
    }

    VanGui_ImplSDL2_UpdateMouseData();
    VanGui_ImplSDL2_UpdateMouseCursor();

    // Update game controllers (if enabled and available)
    VanGui_ImplSDL2_UpdateGamepads();
}

//-----------------------------------------------------------------------------
// Platform Interface (multi-viewport support)
//-----------------------------------------------------------------------------

static void VanGui_ImplSDL2_Platform_CreateWindow(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_Data* bd = VanGui_ImplSDL2_GetBackendData();
    VanGui_ImplSDL2_ViewportData* vd = VAN_NEW(VanGui_ImplSDL2_ViewportData)();
    viewport->PlatformUserData = static_cast<void*>(vd);

    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    SDL_Window* parent_window = (viewport->ParentViewportId != 0 && viewport->ParentViewportId != main_viewport->ID)
        ? nullptr // child-of-child not supported
        : static_cast<SDL_Window*>(SDL_GetWindowFromID(bd->WindowID));

    Uint32 sdl_flags = SDL_WINDOW_HIDDEN;
    if (viewport->Flags & VanGuiViewportFlags_NoDecoration)
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    else
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    if (viewport->Flags & VanGuiViewportFlags_NoTaskBarIcon)
        sdl_flags |= SDL_WINDOW_SKIP_TASKBAR;
    if (viewport->Flags & VanGuiViewportFlags_TopMost)
        sdl_flags |= SDL_WINDOW_ALWAYS_ON_TOP;

    vd->Window = SDL_CreateWindow("No Title Yet",
        static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y),
        static_cast<int>(viewport->Size.x), static_cast<int>(viewport->Size.y),
        sdl_flags);
    vd->WindowID    = SDL_GetWindowID(vd->Window);
    vd->WindowOwned = true;

    viewport->PlatformHandle    = reinterpret_cast<void*>(static_cast<intptr_t>(vd->WindowID));
    viewport->PlatformHandleRaw = nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(vd->Window, &info))
    {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        viewport->PlatformHandleRaw = static_cast<void*>(info.info.win.window);
#elif defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_COCOA)
        viewport->PlatformHandleRaw = static_cast<void*>(info.info.cocoa.window);
#endif
    }
    VAN_UNUSED(parent_window);
}

static void VanGui_ImplSDL2_Platform_DestroyWindow(VanGuiViewport* viewport)
{
    if (VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData))
    {
        if (vd->Window && vd->WindowOwned)
            SDL_DestroyWindow(vd->Window);
        vd->Window = nullptr;
        VAN_DELETE(vd);
    }
    viewport->PlatformUserData = nullptr;
}

static void VanGui_ImplSDL2_Platform_ShowWindow(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_ShowWindow(vd->Window);
}

static void VanGui_ImplSDL2_Platform_SetWindowPos(VanGuiViewport* viewport, VanVec2 pos)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowPosition(vd->Window, static_cast<int>(pos.x), static_cast<int>(pos.y));
}

static VanVec2 VanGui_ImplSDL2_Platform_GetWindowPos(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    int x = 0, y = 0;
    SDL_GetWindowPosition(vd->Window, &x, &y);
    return VanVec2(static_cast<float>(x), static_cast<float>(y));
}

static void VanGui_ImplSDL2_Platform_SetWindowSize(VanGuiViewport* viewport, VanVec2 size)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowSize(vd->Window, static_cast<int>(size.x), static_cast<int>(size.y));
}

static VanVec2 VanGui_ImplSDL2_Platform_GetWindowSize(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    int w = 0, h = 0;
    SDL_GetWindowSize(vd->Window, &w, &h);
    return VanVec2(static_cast<float>(w), static_cast<float>(h));
}

static void VanGui_ImplSDL2_Platform_SetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_RaiseWindow(vd->Window);
}

static bool VanGui_ImplSDL2_Platform_GetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static bool VanGui_ImplSDL2_Platform_GetWindowMinimized(VanGuiViewport* viewport)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_MINIMIZED) != 0;
}

static void VanGui_ImplSDL2_Platform_SetWindowTitle(VanGuiViewport* viewport, const char* title)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowTitle(vd->Window, title);
}

static void VanGui_ImplSDL2_Platform_SetWindowAlpha(VanGuiViewport* viewport, float alpha)
{
    VanGui_ImplSDL2_ViewportData* vd = static_cast<VanGui_ImplSDL2_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowOpacity(vd->Window, alpha);
}

static void VanGui_ImplSDL2_InitPlatformInterface()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Platform_CreateWindow       = VanGui_ImplSDL2_Platform_CreateWindow;
    platform_io.Platform_DestroyWindow      = VanGui_ImplSDL2_Platform_DestroyWindow;
    platform_io.Platform_ShowWindow         = VanGui_ImplSDL2_Platform_ShowWindow;
    platform_io.Platform_SetWindowPos       = VanGui_ImplSDL2_Platform_SetWindowPos;
    platform_io.Platform_GetWindowPos       = VanGui_ImplSDL2_Platform_GetWindowPos;
    platform_io.Platform_SetWindowSize      = VanGui_ImplSDL2_Platform_SetWindowSize;
    platform_io.Platform_GetWindowSize      = VanGui_ImplSDL2_Platform_GetWindowSize;
    platform_io.Platform_SetWindowFocus     = VanGui_ImplSDL2_Platform_SetWindowFocus;
    platform_io.Platform_GetWindowFocus     = VanGui_ImplSDL2_Platform_GetWindowFocus;
    platform_io.Platform_GetWindowMinimized = VanGui_ImplSDL2_Platform_GetWindowMinimized;
    platform_io.Platform_SetWindowTitle     = VanGui_ImplSDL2_Platform_SetWindowTitle;
    platform_io.Platform_SetWindowAlpha     = VanGui_ImplSDL2_Platform_SetWindowAlpha;
}

static void VanGui_ImplSDL2_ShutdownPlatformInterface()
{
    VanGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
