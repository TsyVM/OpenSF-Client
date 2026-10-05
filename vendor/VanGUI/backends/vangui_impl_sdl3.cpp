// VanGUI: Platform Backend for SDL3
// This needs to be used along with a Renderer (e.g. SDL_GPU, DirectX11, OpenGL3, Vulkan..)
// (Info: SDL3 is a cross-platform general purpose library for handling windows, inputs, graphics context creation, etc.)

// Implemented features:
//  [X] Platform: Clipboard support.
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy SDL_SCANCODE_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Gamepad support.
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: IME support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-02-13: Inputs: systems other than X11 are back to starting mouse capture on mouse down (reverts 2025-02-26 change). Only X11 requires waiting for a drag by default (not ideal, but a better default for X11 users). Added VanGui_ImplSDL3_SetMouseCaptureMode() for X11 debugger users. (#3650, #6410, #9235)
//  2026-01-15: Changed GetClipboardText() handler to return nullptr on error aka clipboard contents is not text. Consistent with other backends. (#9168)
//  2025-11-05: Fixed an issue with missing characters events when an already active text field changes viewports. (#9054)
//  2025-10-22: Fixed Platform_OpenInShellFn() return value (unused in core).
//  2025-09-24: Skip using the SDL_GetGlobalMouseState() state when one of our window is hovered, as the SDL_EVENT_MOUSE_MOTION data is reliable. Fix macOS notch mouse coordinates issue in fullscreen mode + better perf on X11. (#7919, #7786)
//  2025-09-18: Call platform_io.ClearPlatformHandlers() on shutdown.
//  2025-09-15: Use SDL_GetWindowDisplayScale() on Mac to output DisplayFrameBufferScale. The function is more reliable during resolution changes e.g. going fullscreen. (#8703, #4414)
//  2025-06-27: IME: avoid calling SDL_StartTextInput() again if already active. (#8727)
//  2025-04-22: IME: honor VanGuiPlatformImeData->WantTextInput as an alternative way to call SDL_StartTextInput(), without IME being necessarily visible.
//  2025-04-09: Don't attempt to call SDL_CaptureMouse() on drivers where we don't call SDL_GetGlobalMouseState(). (#8561)
//  2025-03-30: Update for SDL3 api changes: Revert SDL_GetClipboardText() memory ownership change. (#8530, #7801)
//  2025-03-21: Fill gamepad inputs and set VanGuiBackendFlags_HasGamepad regardless of VanGuiConfigFlags_NavEnableGamepad being set.
//  2025-03-10: When dealing with OEM keys, use scancodes instead of translated keycodes to choose VanGuiKey values. (#7136, #7201, #7206, #7306, #7670, #7672, #8468)
//  2025-02-26: Only start SDL_CaptureMouse() when mouse is being dragged, to mitigate issues with e.g. Linux debuggers not claiming capture back. (#6410, #3650)
//  2025-02-24: Avoid calling SDL_GetGlobalMouseState() when mouse is in relative mode.
//  2025-02-18: Added VanGuiMouseCursor_Wait and VanGuiMouseCursor_Progress mouse cursor support.
//  2025-02-10: Using SDL_OpenURL() in platform_io.Platform_OpenInShellFn handler.
//  2025-01-20: Made VanGui_ImplSDL3_SetGamepadMode(VanGui_ImplSDL3_GamepadMode_Manual) accept an empty array.
//  2024-10-24: Emscripten: SDL_EVENT_MOUSE_WHEEL event doesn't require dividing by 100.0f on Emscripten.
//  2024-09-03: Update for SDL3 api changes: SDL_GetGamepads() memory ownership revert. (#7918, #7898, #7807)
//  2024-08-22: moved some OS/backend related function pointers from VanGuiIO to VanGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//               - io.PlatformSetImeDataFn  -> platform_io.Platform_SetImeDataFn
//  2024-08-19: Storing SDL_WindowID inside VanGuiViewport::PlatformHandle instead of SDL_Window*.
//  2024-08-19: VanGui_ImplSDL3_ProcessEvent() now ignores events intended for other SDL windows. (#7853)
//  2024-07-22: Update for SDL3 api changes: SDL_GetGamepads() memory ownership change. (#7807)
//  2024-07-18: Update for SDL3 api changes: SDL_GetClipboardText() memory ownership change. (#7801)
//  2024-07-15: Update for SDL3 api changes: SDL_GetProperty() change to SDL_GetPointerProperty(). (#7794)
//  2024-07-02: Update for SDL3 api changes: SDLK_x renames and SDLK_KP_x removals (#7761, #7762).
//  2024-07-01: Update for SDL3 api changes: SDL_SetTextInputRect() changed to SDL_SetTextInputArea().
//  2024-06-26: Update for SDL3 api changes: SDL_StartTextInput()/SDL_StopTextInput()/SDL_SetTextInputRect() functions signatures.
//  2024-06-24: Update for SDL3 api changes: SDL_EVENT_KEY_DOWN/SDL_EVENT_KEY_UP contents.
//  2024-06-03; Update for SDL3 api changes: SDL_SYSTEM_CURSOR_ renames.
//  2024-05-15: Update for SDL3 api changes: SDLK_ renames.
//  2024-04-15: Inputs: Re-enable calling SDL_StartTextInput()/SDL_StopTextInput() as SDL3 no longer enables it by default and should play nicer with IME.
//  2024-02-13: Inputs: Fixed gamepad support. Handle gamepad disconnection. Added VanGui_ImplSDL3_SetGamepadMode().
//  2023-11-13: Updated for recent SDL3 API changes.
//  2023-10-05: Inputs: Added support for extra VanGuiKey values: F13 to F24 function keys, app back/forward keys.
//  2023-05-04: Fixed build on Emscripten/iOS/Android. (#6391)
//  2023-04-06: Inputs: Avoid calling SDL_StartTextInput()/SDL_StopTextInput() as they don't only pertain to IME. It's unclear exactly what their relation is to IME. (#6306)
//  2023-04-04: Inputs: Added support for io.AddMouseSourceEvent() to discriminate VanGuiMouseSource_Mouse/VanGuiMouseSource_TouchScreen. (#2702)
//  2023-02-23: Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. (#6189, #6114, #3644)
//  2023-02-07: Forked "vangui_impl_sdl2" into "vangui_impl_sdl3". Removed version checks for old feature. Refer to vangui_impl_sdl2.cpp for older changelog.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_sdl3.h"

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"                 // warning: use of old-style cast
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"  // warning: implicit conversion from 'xxx' to 'float' may lose precision
#endif

// SDL
#include <SDL3/SDL.h>
#include <cstdio>              // for snprintf()
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS) && !defined(__amigaos4__)
#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    1
#else
#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    0
#endif

// FIXME-LEGACY: remove when SDL 3.1.3 preview is released.
#ifndef SDLK_APOSTROPHE
#define SDLK_APOSTROPHE SDLK_QUOTE
#endif
#ifndef SDLK_GRAVE
#define SDLK_GRAVE SDLK_BACKQUOTE
#endif

// SDL Data
struct VanGui_ImplSDL3_Data
{
    SDL_Window*             Window;
    SDL_WindowID            WindowID;
    SDL_Renderer*           Renderer;
    Uint64                  Time;
    char*                   ClipboardTextData;
    char                    BackendPlatformName[48];

    // IME handling
    SDL_Window*             ImeWindow;
    VanGuiPlatformImeData    ImeData;
    bool                    ImeDirty;

    // Mouse handling
    Uint32                  MouseWindowID;
    int                     MouseButtonsDown;
    SDL_Cursor*             MouseCursors[VanGuiMouseCursor_COUNT];
    SDL_Cursor*             MouseLastCursor;
    int                     MousePendingLeaveFrame;
    bool                    MouseCanUseGlobalState;
    VanGui_ImplSDL3_MouseCaptureMode MouseCaptureMode;

    // Gamepad handling
    VanVector<SDL_Gamepad*>      Gamepads;
    VanGui_ImplSDL3_GamepadMode  GamepadMode;
    bool                        WantUpdateGamepadsList;

    // Monitor handling
    bool                        WantUpdateMonitors;

    VanGui_ImplSDL3_Data()   { memset(static_cast<void*>(this), 0, sizeof(*this)); }
};

// Viewport data stored in VanGuiViewport::PlatformUserData for secondary viewports
struct VanGui_ImplSDL3_ViewportData
{
    SDL_Window*     Window      = nullptr;
    SDL_WindowID    WindowID    = 0;
    bool            WindowOwned = false;

    VanGui_ImplSDL3_ViewportData()  = default;
    ~VanGui_ImplSDL3_ViewportData() = default;
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static VanGui_ImplSDL3_Data* VanGui_ImplSDL3_GetBackendData()
{
    return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplSDL3_Data*>(VanGui::GetIO().BackendPlatformUserData) : nullptr;
}

// Forward Declarations
static void VanGui_ImplSDL3_UpdateIme();

// Functions
static const char* VanGui_ImplSDL3_GetClipboardText(VanGuiContext*)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    if (SDL_HasClipboardText())
        bd->ClipboardTextData = SDL_GetClipboardText();
    else
        bd->ClipboardTextData = nullptr;
    return bd->ClipboardTextData;
}

static void VanGui_ImplSDL3_SetClipboardText(VanGuiContext*, const char* text)
{
    SDL_SetClipboardText(text);
}

static VanGuiViewport* VanGui_ImplSDL3_GetViewportForWindowID(SDL_WindowID window_id)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    if (window_id == bd->WindowID)
        return VanGui::GetMainViewport();
    if (VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO(); true)
        for (VanGuiViewport* viewport : platform_io.Viewports)
            if (viewport->PlatformHandle != nullptr)
                if (static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle)) == window_id)
                    return viewport;
    return nullptr;
}

static void VanGui_ImplSDL3_PlatformSetImeData(VanGuiContext*, VanGuiViewport*, VanGuiPlatformImeData* data)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    bd->ImeData = *data;
    bd->ImeDirty = true;
    VanGui_ImplSDL3_UpdateIme();
}

// We discard viewport passed via VanGuiPlatformImeData and always call SDL_StartTextInput() on SDL_GetKeyboardFocus().
static void VanGui_ImplSDL3_UpdateIme()
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VanGuiPlatformImeData* data = &bd->ImeData;
    SDL_Window* window = SDL_GetKeyboardFocus();

    // Stop previous input
    if ((!(data->WantVisible || data->WantTextInput) || bd->ImeWindow != window) && bd->ImeWindow != nullptr)
    {
        SDL_StopTextInput(bd->ImeWindow);
        bd->ImeWindow = nullptr;
    }
    if ((!bd->ImeDirty && bd->ImeWindow == window) || (window == nullptr))
        return;

    // Start/update current input
    bd->ImeDirty = false;
    if (data->WantVisible)
    {
        SDL_Rect r;
        r.x = static_cast<int>(data->InputPos.x);
        r.y = static_cast<int>(data->InputPos.y);
        r.w = 1;
        r.h = static_cast<int>(data->InputLineHeight);
        SDL_SetTextInputArea(window, &r, 0);
        bd->ImeWindow = window;
    }
    if (!SDL_TextInputActive(window) && (data->WantVisible || data->WantTextInput))
        SDL_StartTextInput(window);
}

// Not static to allow third-party code to use that if they want to (but undocumented)
VanGuiKey VanGui_ImplSDL3_KeyEventToVanGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);
VanGuiKey VanGui_ImplSDL3_KeyEventToVanGuiKey(SDL_Keycode keycode, SDL_Scancode scancode)
{
    // Keypad doesn't have individual key values in SDL3
    switch (scancode)
    {
        case SDL_SCANCODE_KP_0: return VanGuiKey_Keypad0;
        case SDL_SCANCODE_KP_1: return VanGuiKey_Keypad1;
        case SDL_SCANCODE_KP_2: return VanGuiKey_Keypad2;
        case SDL_SCANCODE_KP_3: return VanGuiKey_Keypad3;
        case SDL_SCANCODE_KP_4: return VanGuiKey_Keypad4;
        case SDL_SCANCODE_KP_5: return VanGuiKey_Keypad5;
        case SDL_SCANCODE_KP_6: return VanGuiKey_Keypad6;
        case SDL_SCANCODE_KP_7: return VanGuiKey_Keypad7;
        case SDL_SCANCODE_KP_8: return VanGuiKey_Keypad8;
        case SDL_SCANCODE_KP_9: return VanGuiKey_Keypad9;
        case SDL_SCANCODE_KP_PERIOD: return VanGuiKey_KeypadDecimal;
        case SDL_SCANCODE_KP_DIVIDE: return VanGuiKey_KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY: return VanGuiKey_KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS: return VanGuiKey_KeypadSubtract;
        case SDL_SCANCODE_KP_PLUS: return VanGuiKey_KeypadAdd;
        case SDL_SCANCODE_KP_ENTER: return VanGuiKey_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS: return VanGuiKey_KeypadEqual;
        default: break;
    }
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
        //case SDLK_APOSTROPHE: return VanGuiKey_Apostrophe;
        case SDLK_COMMA: return VanGuiKey_Comma;
        //case SDLK_MINUS: return VanGuiKey_Minus;
        case SDLK_PERIOD: return VanGuiKey_Period;
        //case SDLK_SLASH: return VanGuiKey_Slash;
        case SDLK_SEMICOLON: return VanGuiKey_Semicolon;
        //case SDLK_EQUALS: return VanGuiKey_Equal;
        //case SDLK_LEFTBRACKET: return VanGuiKey_LeftBracket;
        //case SDLK_BACKSLASH: return VanGuiKey_Backslash;
        //case SDLK_RIGHTBRACKET: return VanGuiKey_RightBracket;
        //case SDLK_GRAVE: return VanGuiKey_GraveAccent;
        case SDLK_CAPSLOCK: return VanGuiKey_CapsLock;
        case SDLK_SCROLLLOCK: return VanGuiKey_ScrollLock;
        case SDLK_NUMLOCKCLEAR: return VanGuiKey_NumLock;
        case SDLK_PRINTSCREEN: return VanGuiKey_PrintScreen;
        case SDLK_PAUSE: return VanGuiKey_Pause;
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
        case SDLK_A: return VanGuiKey_A;
        case SDLK_B: return VanGuiKey_B;
        case SDLK_C: return VanGuiKey_C;
        case SDLK_D: return VanGuiKey_D;
        case SDLK_E: return VanGuiKey_E;
        case SDLK_F: return VanGuiKey_F;
        case SDLK_G: return VanGuiKey_G;
        case SDLK_H: return VanGuiKey_H;
        case SDLK_I: return VanGuiKey_I;
        case SDLK_J: return VanGuiKey_J;
        case SDLK_K: return VanGuiKey_K;
        case SDLK_L: return VanGuiKey_L;
        case SDLK_M: return VanGuiKey_M;
        case SDLK_N: return VanGuiKey_N;
        case SDLK_O: return VanGuiKey_O;
        case SDLK_P: return VanGuiKey_P;
        case SDLK_Q: return VanGuiKey_Q;
        case SDLK_R: return VanGuiKey_R;
        case SDLK_S: return VanGuiKey_S;
        case SDLK_T: return VanGuiKey_T;
        case SDLK_U: return VanGuiKey_U;
        case SDLK_V: return VanGuiKey_V;
        case SDLK_W: return VanGuiKey_W;
        case SDLK_X: return VanGuiKey_X;
        case SDLK_Y: return VanGuiKey_Y;
        case SDLK_Z: return VanGuiKey_Z;
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

static void VanGui_ImplSDL3_UpdateKeyModifiers(SDL_Keymod sdl_key_mods)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(VanGuiMod_Ctrl, (sdl_key_mods & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(VanGuiMod_Shift, (sdl_key_mods & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(VanGuiMod_Alt, (sdl_key_mods & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(VanGuiMod_Super, (sdl_key_mods & SDL_KMOD_GUI) != 0);
}

// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if VanGUI wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to VanGUI, and hide them from your application based on those two flags.
bool VanGui_ImplSDL3_ProcessEvent(const SDL_Event* event)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplSDL3_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->motion.windowID) == nullptr)
                return false;
            VanVec2 mouse_pos(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID ? VanGuiMouseSource_TouchScreen : VanGuiMouseSource_Mouse);
            io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->wheel.windowID) == nullptr)
                return false;
            //VANGUI_DEBUG_LOG("wheel %.2f %.2f, precise %.2f %.2f\n", (float)event->wheel.x, (float)event->wheel.y, event->wheel.preciseX, event->wheel.preciseY);
            float wheel_x = -event->wheel.x;
            float wheel_y = event->wheel.y;
            io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? VanGuiMouseSource_TouchScreen : VanGuiMouseSource_Mouse);
            io.AddMouseWheelEvent(wheel_x, wheel_y);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->button.windowID) == nullptr)
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
            io.AddMouseButtonEvent(mouse_button, (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN));
            bd->MouseButtonsDown = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? (bd->MouseButtonsDown | (1 << mouse_button)) : (bd->MouseButtonsDown & ~(1 << mouse_button));
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->text.windowID) == nullptr)
                return false;
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->key.windowID) == nullptr)
                return false;
            VanGui_ImplSDL3_UpdateKeyModifiers(static_cast<SDL_Keymod>(event->key.mod));
            //VANGUI_DEBUG_LOG("SDL_EVENT_KEY_%s : key=%d ('%s'), scancode=%d ('%s'), mod=%X\n",
            //    (event->type == SDL_EVENT_KEY_DOWN) ? "DOWN" : "UP  ", event->key.key, SDL_GetKeyName(event->key.key), event->key.scancode, SDL_GetScancodeName(event->key.scancode), event->key.mod);
            VanGuiKey key = VanGui_ImplSDL3_KeyEventToVanGuiKey(event->key.key, event->key.scancode);
            io.AddKeyEvent(key, (event->type == SDL_EVENT_KEY_DOWN));
            io.SetKeyEventNativeData(key, static_cast<int>(event->key.key), static_cast<int>(event->key.scancode), static_cast<int>(event->key.scancode)); // To support legacy indexing (<1.87 user code). Legacy backend uses SDLK_*** as indices to IsKeyXXX() functions.
            return true;
        }
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            bd->MouseWindowID = event->window.windowID;
            bd->MousePendingLeaveFrame = 0;
            return true;
        }
        // - In some cases, when detaching a window from main viewport SDL may send SDL_WINDOWEVENT_ENTER one frame too late,
        //   causing SDL_WINDOWEVENT_LEAVE on previous frame to interrupt drag operation by clear mouse position. This is why
        //   we delay process the SDL_WINDOWEVENT_LEAVE events by one frame. See issue #5012 for details.
        // FIXME: Unconfirmed whether this is still needed with SDL3.
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            bd->MousePendingLeaveFrame = VanGui::GetFrameCount() + 1;
            return true;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            if (VanGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            io.AddFocusEvent(event->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
            return true;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            bd->WantUpdateGamepadsList = true;
            return true;
        }
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_ORIENTATION:
        {
            bd->WantUpdateMonitors = true;
            return true;
        }
        default:
            break;
    }
    return false;
}

static void VanGui_ImplSDL3_SetupPlatformHandles(VanGuiViewport* viewport, SDL_Window* window)
{
    viewport->PlatformHandle = reinterpret_cast<void*>(static_cast<intptr_t>(SDL_GetWindowID(window)));
    viewport->PlatformHandleRaw = nullptr;
#if defined(_WIN32) && !defined(__WINRT__)
    viewport->PlatformHandleRaw = reinterpret_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#elif defined(__APPLE__)
    viewport->PlatformHandleRaw = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif
}

static bool VanGui_ImplSDL3_Init(SDL_Window* window, SDL_Renderer* renderer, void* sdl_gl_context)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");
    VAN_UNUSED(sdl_gl_context); // Unused in this branch
    //SDL_SetHint(SDL_HINT_EVENT_LOGGING, "2");

    const int ver_linked = SDL_GetVersion();

    // Setup backend capabilities flags
    VanGui_ImplSDL3_Data* bd = VAN_NEW(VanGui_ImplSDL3_Data)();
    snprintf(bd->BackendPlatformName, sizeof(bd->BackendPlatformName), "vangui_impl_sdl3 (%d.%d.%d; %d.%d.%d)",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_VERSIONNUM_MAJOR(ver_linked), SDL_VERSIONNUM_MINOR(ver_linked), SDL_VERSIONNUM_MICRO(ver_linked));
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
    bd->MouseCaptureMode = VanGui_ImplSDL3_MouseCaptureMode_Disabled;
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
    const char* sdl_backend = SDL_GetCurrentVideoDriver();
    const char* capture_and_global_state_whitelist[] = { "windows", "cocoa", "x11", "DIVE", "VMAN" };
    for (const char* item : capture_and_global_state_whitelist)
        if (strncmp(sdl_backend, item, strlen(item)) == 0)
        {
            bd->MouseCanUseGlobalState = true;
            bd->MouseCaptureMode = (strcmp(item, "x11") == 0) ? VanGui_ImplSDL3_MouseCaptureMode_EnabledAfterDrag : VanGui_ImplSDL3_MouseCaptureMode_Enabled;
        }
#endif

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = VanGui_ImplSDL3_SetClipboardText;
    platform_io.Platform_GetClipboardTextFn = VanGui_ImplSDL3_GetClipboardText;
    platform_io.Platform_SetImeDataFn = VanGui_ImplSDL3_PlatformSetImeData;
    platform_io.Platform_OpenInShellFn = [](VanGuiContext*, const char* url) { return SDL_OpenURL(url); };

    // Gamepad handling
    bd->GamepadMode = VanGui_ImplSDL3_GamepadMode_AutoFirst;
    bd->WantUpdateGamepadsList = true;

    // Monitor handling
    bd->WantUpdateMonitors = true;

    // Load mouse cursors
    bd->MouseCursors[VanGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    bd->MouseCursors[VanGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    bd->MouseCursors[VanGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    bd->MouseCursors[VanGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    bd->MouseCursors[VanGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    bd->MouseCursors[VanGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    bd->MouseCursors[VanGuiMouseCursor_Wait] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    bd->MouseCursors[VanGuiMouseCursor_Progress] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_PROGRESS);
    bd->MouseCursors[VanGuiMouseCursor_NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);

    // Set platform dependent data in viewport
    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    VanGui_ImplSDL3_SetupPlatformHandles(main_viewport, window);

    // From 2.0.5: Set SDL hint to receive mouse click events on window focus, otherwise SDL doesn't emit the event.
    // Without this, when clicking to gain focus, our widgets wouldn't activate even though they showed as hovered.
    // (This is unfortunately a global SDL setting, so enabling it might have a side-effect on your application.
    // It is unlikely to make a difference, but if your app absolutely needs to ignore the initial on-focus click:
    // you can ignore SDL_EVENT_MOUSE_BUTTON_DOWN events coming right after a SDL_EVENT_WINDOW_FOCUS_GAINED)
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // From 2.0.22: Disable auto-capture, this is preventing drag and drop across multiple windows (see #5710)
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");

    VanGui_ImplSDL3_InitPlatformInterface();
    return true;
}

// Should technically be a SDL_GLContext but due to typedef it is sane to keep it void* in public interface.
bool VanGui_ImplSDL3_InitForOpenGL(SDL_Window* window, void* sdl_gl_context)
{
    return VanGui_ImplSDL3_Init(window, nullptr, sdl_gl_context);
}

bool VanGui_ImplSDL3_InitForVulkan(SDL_Window* window)
{
    return VanGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL3_InitForD3D(SDL_Window* window)
{
#if !defined(_WIN32)
    VAN_ASSERT(0 && "Unsupported");
#endif
    return VanGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL3_InitForMetal(SDL_Window* window)
{
    return VanGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL3_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer)
{
    return VanGui_ImplSDL3_Init(window, renderer, nullptr);
}

bool VanGui_ImplSDL3_InitForSDLGPU(SDL_Window* window)
{
    return VanGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool VanGui_ImplSDL3_InitForOther(SDL_Window* window)
{
    return VanGui_ImplSDL3_Init(window, nullptr, nullptr);
}

static void VanGui_ImplSDL3_CloseGamepads();

void VanGui_ImplSDL3_Shutdown()
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    VanGui_ImplSDL3_ShutdownPlatformInterface();
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    for (VanGuiMouseCursor cursor_n = 0; cursor_n < VanGuiMouseCursor_COUNT; cursor_n++)
        SDL_DestroyCursor(bd->MouseCursors[cursor_n]);
    VanGui_ImplSDL3_CloseGamepads();

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_HasMouseCursors | VanGuiBackendFlags_HasSetMousePos | VanGuiBackendFlags_HasGamepad);
    platform_io.ClearPlatformHandlers();
    VAN_DELETE(bd);
}

void VanGui_ImplSDL3_SetMouseCaptureMode(VanGui_ImplSDL3_MouseCaptureMode mode)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    if (mode == VanGui_ImplSDL3_MouseCaptureMode_Disabled && bd->MouseCaptureMode != VanGui_ImplSDL3_MouseCaptureMode_Disabled)
        SDL_CaptureMouse(false);
    bd->MouseCaptureMode = mode;
}

static void VanGui_ImplSDL3_UpdateMouseData()
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();

    // We forward mouse input when hovered or captured (via SDL_EVENT_MOUSE_MOTION) or when focused (below)
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
    // - SDL_CaptureMouse() let the OS know e.g. that our drags can extend outside of parent boundaries (we want updated position) and shouldn't trigger other operations outside.
    // - Debuggers under Linux tends to leave captured mouse on break, which may be very inconvenient, so to mitigate the issue on X11 we we wait until mouse has moved to begin capture.
    if (bd->MouseCaptureMode == VanGui_ImplSDL3_MouseCaptureMode_Enabled)
    {
        SDL_CaptureMouse(bd->MouseButtonsDown != 0);
    }
    else if (bd->MouseCaptureMode == VanGui_ImplSDL3_MouseCaptureMode_EnabledAfterDrag)
    {
        bool want_capture = false;
        for (int button_n = 0; button_n < VanGuiMouseButton_COUNT && !want_capture; button_n++)
            if (VanGui::IsMouseDragging(button_n, 1.0f))
                want_capture = true;
        SDL_CaptureMouse(want_capture);
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
            SDL_WarpMouseInWindow(bd->Window, io.MousePos.x, io.MousePos.y);

        // (Optional) Fallback to provide unclamped mouse position when focused but not hovered (SDL_EVENT_MOUSE_MOTION already provides this when hovered or captured)
        // Note that SDL_GetGlobalMouseState() is in theory slow on X11, but this only runs on rather specific cases. If a problem we may provide a way to opt-out this feature.
        SDL_Window* hovered_window = SDL_GetMouseFocus();
        const bool is_relative_mouse_mode = SDL_GetWindowRelativeMouseMode(bd->Window);
        if (hovered_window == nullptr && bd->MouseCanUseGlobalState && bd->MouseButtonsDown == 0 && !is_relative_mouse_mode)
        {
            // Single-viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
            float mouse_x, mouse_y;
            int window_x, window_y;
            SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
            SDL_GetWindowPosition(focused_window, &window_x, &window_y);
            mouse_x -= static_cast<float>(window_x);
            mouse_y -= static_cast<float>(window_y);
            io.AddMousePosEvent(mouse_x, mouse_y);
        }
    }
}

static void VanGui_ImplSDL3_UpdateMouseCursor()
{
    VanGuiIO& io = VanGui::GetIO();
    if (io.ConfigFlags & VanGuiConfigFlags_NoMouseCursorChange)
        return;
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();

    VanGuiMouseCursor vangui_cursor = VanGui::GetMouseCursor();
    if (io.MouseDrawCursor || vangui_cursor == VanGuiMouseCursor_None)
    {
        // Hide OS mouse cursor if vangui is drawing it or if it wants no cursor
        SDL_HideCursor();
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
        SDL_ShowCursor();
    }
}

static void VanGui_ImplSDL3_CloseGamepads()
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    if (bd->GamepadMode != VanGui_ImplSDL3_GamepadMode_Manual)
        for (SDL_Gamepad* gamepad : bd->Gamepads)
            SDL_CloseGamepad(gamepad);
    bd->Gamepads.resize(0);
}

void VanGui_ImplSDL3_SetGamepadMode(VanGui_ImplSDL3_GamepadMode mode, SDL_Gamepad** manual_gamepads_array, int manual_gamepads_count)
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VanGui_ImplSDL3_CloseGamepads();
    if (mode == VanGui_ImplSDL3_GamepadMode_Manual)
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

static void VanGui_ImplSDL3_UpdateGamepadButton(VanGui_ImplSDL3_Data* bd, VanGuiIO& io, VanGuiKey key, SDL_GamepadButton button_no)
{
    bool merged_value = false;
    for (SDL_Gamepad* gamepad : bd->Gamepads)
        merged_value |= SDL_GetGamepadButton(gamepad, button_no) != 0;
    io.AddKeyEvent(key, merged_value);
}

static inline float Saturate(float v) { return v < 0.0f ? 0.0f : v  > 1.0f ? 1.0f : v; }
static void VanGui_ImplSDL3_UpdateGamepadAnalog(VanGui_ImplSDL3_Data* bd, VanGuiIO& io, VanGuiKey key, SDL_GamepadAxis axis_no, float v0, float v1)
{
    float merged_value = 0.0f;
    for (SDL_Gamepad* gamepad : bd->Gamepads)
    {
        float vn = Saturate(static_cast<float>(SDL_GetGamepadAxis(gamepad, axis_no) - v0) / static_cast<float>(v1 - v0));
        if (merged_value < vn)
            merged_value = vn;
    }
    io.AddKeyAnalogEvent(key, merged_value > 0.1f, merged_value);
}

static void VanGui_ImplSDL3_UpdateGamepads()
{
    VanGuiIO& io = VanGui::GetIO();
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();

    // Update list of gamepads to use
    if (bd->WantUpdateGamepadsList && bd->GamepadMode != VanGui_ImplSDL3_GamepadMode_Manual)
    {
        VanGui_ImplSDL3_CloseGamepads();
        int sdl_gamepads_count = 0;
        SDL_JoystickID* sdl_gamepads = SDL_GetGamepads(&sdl_gamepads_count);
        for (int n = 0; n < sdl_gamepads_count; n++)
            if (SDL_Gamepad* gamepad = SDL_OpenGamepad(sdl_gamepads[n]))
            {
                bd->Gamepads.push_back(gamepad);
                if (bd->GamepadMode == VanGui_ImplSDL3_GamepadMode_AutoFirst)
                    break;
            }
        bd->WantUpdateGamepadsList = false;
        SDL_free(sdl_gamepads);
    }

    io.BackendFlags &= ~VanGuiBackendFlags_HasGamepad;
    if (bd->Gamepads.Size == 0)
        return;
    io.BackendFlags |= VanGuiBackendFlags_HasGamepad;

    // Update gamepad inputs
    const int thumb_dead_zone = 8000;           // SDL_gamepad.h suggests using this value.
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadStart,       SDL_GAMEPAD_BUTTON_START);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadBack,        SDL_GAMEPAD_BUTTON_BACK);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceLeft,    SDL_GAMEPAD_BUTTON_WEST);           // Xbox X, PS Square
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceRight,   SDL_GAMEPAD_BUTTON_EAST);           // Xbox B, PS Circle
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceUp,      SDL_GAMEPAD_BUTTON_NORTH);          // Xbox Y, PS Triangle
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadFaceDown,    SDL_GAMEPAD_BUTTON_SOUTH);          // Xbox A, PS Cross
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadLeft,    SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadRight,   SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadUp,      SDL_GAMEPAD_BUTTON_DPAD_UP);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadDpadDown,    SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadL1,          SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadR1,          SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadL2,          SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  0.0f, 32767);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadR2,          SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f, 32767);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadL3,          SDL_GAMEPAD_BUTTON_LEFT_STICK);
    VanGui_ImplSDL3_UpdateGamepadButton(bd, io, VanGuiKey_GamepadR3,          SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickLeft,  SDL_GAMEPAD_AXIS_LEFTX,  -thumb_dead_zone, -32768);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX,  +thumb_dead_zone, +32767);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickUp,    SDL_GAMEPAD_AXIS_LEFTY,  -thumb_dead_zone, -32768);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadLStickDown,  SDL_GAMEPAD_AXIS_LEFTY,  +thumb_dead_zone, +32767);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickLeft,  SDL_GAMEPAD_AXIS_RIGHTX, -thumb_dead_zone, -32768);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickRight, SDL_GAMEPAD_AXIS_RIGHTX, +thumb_dead_zone, +32767);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickUp,    SDL_GAMEPAD_AXIS_RIGHTY, -thumb_dead_zone, -32768);
    VanGui_ImplSDL3_UpdateGamepadAnalog(bd, io, VanGuiKey_GamepadRStickDown,  SDL_GAMEPAD_AXIS_RIGHTY, +thumb_dead_zone, +32767);
}

static void VanGui_ImplSDL3_UpdateMonitors()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Monitors.resize(0);
    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    if (displays == nullptr)
        return;
    for (int n = 0; n < display_count; n++)
    {
        SDL_DisplayID display_id = displays[n];
        VanGuiPlatformMonitor monitor = {};
        SDL_Rect r;
        SDL_GetDisplayBounds(display_id, &r);
        monitor.MainPos  = VanVec2(static_cast<float>(r.x), static_cast<float>(r.y));
        monitor.MainSize = VanVec2(static_cast<float>(r.w), static_cast<float>(r.h));
        SDL_GetDisplayUsableBounds(display_id, &r);
        monitor.WorkPos  = VanVec2(static_cast<float>(r.x), static_cast<float>(r.y));
        monitor.WorkSize = VanVec2(static_cast<float>(r.w), static_cast<float>(r.h));
        float content_scale = SDL_GetDisplayContentScale(display_id);
        monitor.DpiScale = (content_scale > 0.0f) ? content_scale : 1.0f;
        monitor.PlatformHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(display_id));
        platform_io.Monitors.push_back(monitor);
    }
    SDL_free(displays);
}

static void VanGui_ImplSDL3_GetWindowSizeAndFramebufferScale(SDL_Window* window, VanVec2* out_size, VanVec2* out_framebuffer_scale)
{
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        w = h = 0;

#if defined(__APPLE__)
    float fb_scale_x = SDL_GetWindowDisplayScale(window); // Seems more reliable during resolution change (#8703)
    float fb_scale_y = fb_scale_x;
#else
    int display_w, display_h;
    SDL_GetWindowSizeInPixels(window, &display_w, &display_h);
    float fb_scale_x = (w > 0) ? static_cast<float>(display_w) / static_cast<float>(w) : 1.0f;
    float fb_scale_y = (h > 0) ? static_cast<float>(display_h) / static_cast<float>(h) : 1.0f;
#endif

    if (out_size != nullptr)
        *out_size = VanVec2(static_cast<float>(w), static_cast<float>(h));
    if (out_framebuffer_scale != nullptr)
        *out_framebuffer_scale = VanVec2(fb_scale_x, fb_scale_y);
}

void VanGui_ImplSDL3_NewFrame()
{
    VanGui_ImplSDL3_Data* bd = VanGui_ImplSDL3_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplSDL3_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    // Setup main viewport size (every frame to accommodate for window resizing)
    VanGui_ImplSDL3_GetWindowSizeAndFramebufferScale(bd->Window, &io.DisplaySize, &io.DisplayFramebufferScale);

    // Setup time step (we could also use SDL_GetTicksNS() available since SDL3)
    // (Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. Happens in VMs and Emscripten, see #6189, #6114, #3644)
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    if (current_time <= bd->Time)
        current_time = bd->Time + 1;
    io.DeltaTime = bd->Time > 0 ? static_cast<float>(static_cast<double>(current_time - bd->Time) / static_cast<double>(frequency)) : static_cast<float>(1.0f / 60.0f);
    bd->Time = current_time;

    if (bd->MousePendingLeaveFrame && bd->MousePendingLeaveFrame >= VanGui::GetFrameCount() && bd->MouseButtonsDown == 0)
    {
        bd->MouseWindowID = 0;
        bd->MousePendingLeaveFrame = 0;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // Update monitors (initially and when a display change event is received)
    if (bd->WantUpdateMonitors)
    {
        VanGui_ImplSDL3_UpdateMonitors();
        bd->WantUpdateMonitors = false;
    }

    VanGui_ImplSDL3_UpdateMouseData();
    VanGui_ImplSDL3_UpdateMouseCursor();
    VanGui_ImplSDL3_UpdateIme();

    // Update game controllers (if enabled and available)
    VanGui_ImplSDL3_UpdateGamepads();
}

//-----------------------------------------------------------------------------
// Platform Interface (multi-viewport support)
//-----------------------------------------------------------------------------

static void VanGui_ImplSDL3_Platform_CreateWindow(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = VAN_NEW(VanGui_ImplSDL3_ViewportData)();
    viewport->PlatformUserData = static_cast<void*>(vd);

    SDL_WindowFlags sdl_flags = SDL_WINDOW_HIDDEN;
    if (viewport->Flags & VanGuiViewportFlags_NoDecoration)
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    else
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    if (viewport->Flags & VanGuiViewportFlags_NoTaskBarIcon)
        sdl_flags |= SDL_WINDOW_UTILITY;
    if (viewport->Flags & VanGuiViewportFlags_TopMost)
        sdl_flags |= SDL_WINDOW_ALWAYS_ON_TOP;

    vd->Window = SDL_CreateWindow("No Title Yet",
        static_cast<int>(viewport->Size.x), static_cast<int>(viewport->Size.y),
        sdl_flags);
    if (vd->Window != nullptr)
        SDL_SetWindowPosition(vd->Window, static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y));
    vd->WindowID    = (vd->Window != nullptr) ? SDL_GetWindowID(vd->Window) : 0;
    vd->WindowOwned = true;

    VanGui_ImplSDL3_SetupPlatformHandles(viewport, vd->Window);
}

static void VanGui_ImplSDL3_Platform_DestroyWindow(VanGuiViewport* viewport)
{
    if (VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData))
    {
        if (vd->Window && vd->WindowOwned)
            SDL_DestroyWindow(vd->Window);
        vd->Window = nullptr;
        VAN_DELETE(vd);
    }
    viewport->PlatformUserData = nullptr;
}

static void VanGui_ImplSDL3_Platform_ShowWindow(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_ShowWindow(vd->Window);
}

static void VanGui_ImplSDL3_Platform_SetWindowPos(VanGuiViewport* viewport, VanVec2 pos)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowPosition(vd->Window, static_cast<int>(pos.x), static_cast<int>(pos.y));
}

static VanVec2 VanGui_ImplSDL3_Platform_GetWindowPos(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    int x = 0, y = 0;
    SDL_GetWindowPosition(vd->Window, &x, &y);
    return VanVec2(static_cast<float>(x), static_cast<float>(y));
}

static void VanGui_ImplSDL3_Platform_SetWindowSize(VanGuiViewport* viewport, VanVec2 size)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowSize(vd->Window, static_cast<int>(size.x), static_cast<int>(size.y));
}

static VanVec2 VanGui_ImplSDL3_Platform_GetWindowSize(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    int w = 0, h = 0;
    SDL_GetWindowSize(vd->Window, &w, &h);
    return VanVec2(static_cast<float>(w), static_cast<float>(h));
}

static void VanGui_ImplSDL3_Platform_SetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_RaiseWindow(vd->Window);
}

static bool VanGui_ImplSDL3_Platform_GetWindowFocus(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static bool VanGui_ImplSDL3_Platform_GetWindowMinimized(VanGuiViewport* viewport)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_MINIMIZED) != 0;
}

static void VanGui_ImplSDL3_Platform_SetWindowTitle(VanGuiViewport* viewport, const char* title)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowTitle(vd->Window, title);
}

static void VanGui_ImplSDL3_Platform_SetWindowAlpha(VanGuiViewport* viewport, float alpha)
{
    VanGui_ImplSDL3_ViewportData* vd = static_cast<VanGui_ImplSDL3_ViewportData*>(viewport->PlatformUserData);
    SDL_SetWindowOpacity(vd->Window, alpha);
}

static void VanGui_ImplSDL3_InitPlatformInterface()
{
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.Platform_CreateWindow       = VanGui_ImplSDL3_Platform_CreateWindow;
    platform_io.Platform_DestroyWindow      = VanGui_ImplSDL3_Platform_DestroyWindow;
    platform_io.Platform_ShowWindow         = VanGui_ImplSDL3_Platform_ShowWindow;
    platform_io.Platform_SetWindowPos       = VanGui_ImplSDL3_Platform_SetWindowPos;
    platform_io.Platform_GetWindowPos       = VanGui_ImplSDL3_Platform_GetWindowPos;
    platform_io.Platform_SetWindowSize      = VanGui_ImplSDL3_Platform_SetWindowSize;
    platform_io.Platform_GetWindowSize      = VanGui_ImplSDL3_Platform_GetWindowSize;
    platform_io.Platform_SetWindowFocus     = VanGui_ImplSDL3_Platform_SetWindowFocus;
    platform_io.Platform_GetWindowFocus     = VanGui_ImplSDL3_Platform_GetWindowFocus;
    platform_io.Platform_GetWindowMinimized = VanGui_ImplSDL3_Platform_GetWindowMinimized;
    platform_io.Platform_SetWindowTitle     = VanGui_ImplSDL3_Platform_SetWindowTitle;
    platform_io.Platform_SetWindowAlpha     = VanGui_ImplSDL3_Platform_SetWindowAlpha;
}

static void VanGui_ImplSDL3_ShutdownPlatformInterface()
{
    VanGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
