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

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2021-03-04: Initial version.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_android.h"
#include <time.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>

// Android data
static double                                   g_Time = 0.0;
static ANativeWindow*                           g_Window;
static char                                     g_LogTag[] = "VanGuiExample";

static VanGuiKey VanGui_ImplAndroid_KeyCodeToVanGuiKey(int32_t key_code)
{
    switch (key_code)
    {
        case AKEYCODE_TAB:                  return VanGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:            return VanGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT:           return VanGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:              return VanGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:            return VanGuiKey_DownArrow;
        case AKEYCODE_PAGE_UP:              return VanGuiKey_PageUp;
        case AKEYCODE_PAGE_DOWN:            return VanGuiKey_PageDown;
        case AKEYCODE_MOVE_HOME:            return VanGuiKey_Home;
        case AKEYCODE_MOVE_END:             return VanGuiKey_End;
        case AKEYCODE_INSERT:               return VanGuiKey_Insert;
        case AKEYCODE_FORWARD_DEL:          return VanGuiKey_Delete;
        case AKEYCODE_DEL:                  return VanGuiKey_Backspace;
        case AKEYCODE_SPACE:                return VanGuiKey_Space;
        case AKEYCODE_ENTER:                return VanGuiKey_Enter;
        case AKEYCODE_ESCAPE:               return VanGuiKey_Escape;
        case AKEYCODE_APOSTROPHE:           return VanGuiKey_Apostrophe;
        case AKEYCODE_COMMA:                return VanGuiKey_Comma;
        case AKEYCODE_MINUS:                return VanGuiKey_Minus;
        case AKEYCODE_PERIOD:               return VanGuiKey_Period;
        case AKEYCODE_SLASH:                return VanGuiKey_Slash;
        case AKEYCODE_SEMICOLON:            return VanGuiKey_Semicolon;
        case AKEYCODE_EQUALS:               return VanGuiKey_Equal;
        case AKEYCODE_LEFT_BRACKET:         return VanGuiKey_LeftBracket;
        case AKEYCODE_BACKSLASH:            return VanGuiKey_Backslash;
        case AKEYCODE_RIGHT_BRACKET:        return VanGuiKey_RightBracket;
        case AKEYCODE_GRAVE:                return VanGuiKey_GraveAccent;
        case AKEYCODE_CAPS_LOCK:            return VanGuiKey_CapsLock;
        case AKEYCODE_SCROLL_LOCK:          return VanGuiKey_ScrollLock;
        case AKEYCODE_NUM_LOCK:             return VanGuiKey_NumLock;
        case AKEYCODE_SYSRQ:                return VanGuiKey_PrintScreen;
        case AKEYCODE_BREAK:                return VanGuiKey_Pause;
        case AKEYCODE_NUMPAD_0:             return VanGuiKey_Keypad0;
        case AKEYCODE_NUMPAD_1:             return VanGuiKey_Keypad1;
        case AKEYCODE_NUMPAD_2:             return VanGuiKey_Keypad2;
        case AKEYCODE_NUMPAD_3:             return VanGuiKey_Keypad3;
        case AKEYCODE_NUMPAD_4:             return VanGuiKey_Keypad4;
        case AKEYCODE_NUMPAD_5:             return VanGuiKey_Keypad5;
        case AKEYCODE_NUMPAD_6:             return VanGuiKey_Keypad6;
        case AKEYCODE_NUMPAD_7:             return VanGuiKey_Keypad7;
        case AKEYCODE_NUMPAD_8:             return VanGuiKey_Keypad8;
        case AKEYCODE_NUMPAD_9:             return VanGuiKey_Keypad9;
        case AKEYCODE_NUMPAD_DOT:           return VanGuiKey_KeypadDecimal;
        case AKEYCODE_NUMPAD_DIVIDE:        return VanGuiKey_KeypadDivide;
        case AKEYCODE_NUMPAD_MULTIPLY:      return VanGuiKey_KeypadMultiply;
        case AKEYCODE_NUMPAD_SUBTRACT:      return VanGuiKey_KeypadSubtract;
        case AKEYCODE_NUMPAD_ADD:           return VanGuiKey_KeypadAdd;
        case AKEYCODE_NUMPAD_ENTER:         return VanGuiKey_KeypadEnter;
        case AKEYCODE_NUMPAD_EQUALS:        return VanGuiKey_KeypadEqual;
        case AKEYCODE_CTRL_LEFT:            return VanGuiKey_LeftCtrl;
        case AKEYCODE_SHIFT_LEFT:           return VanGuiKey_LeftShift;
        case AKEYCODE_ALT_LEFT:             return VanGuiKey_LeftAlt;
        case AKEYCODE_META_LEFT:            return VanGuiKey_LeftSuper;
        case AKEYCODE_CTRL_RIGHT:           return VanGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_RIGHT:          return VanGuiKey_RightShift;
        case AKEYCODE_ALT_RIGHT:            return VanGuiKey_RightAlt;
        case AKEYCODE_META_RIGHT:           return VanGuiKey_RightSuper;
        case AKEYCODE_MENU:                 return VanGuiKey_Menu;
        case AKEYCODE_0:                    return VanGuiKey_0;
        case AKEYCODE_1:                    return VanGuiKey_1;
        case AKEYCODE_2:                    return VanGuiKey_2;
        case AKEYCODE_3:                    return VanGuiKey_3;
        case AKEYCODE_4:                    return VanGuiKey_4;
        case AKEYCODE_5:                    return VanGuiKey_5;
        case AKEYCODE_6:                    return VanGuiKey_6;
        case AKEYCODE_7:                    return VanGuiKey_7;
        case AKEYCODE_8:                    return VanGuiKey_8;
        case AKEYCODE_9:                    return VanGuiKey_9;
        case AKEYCODE_A:                    return VanGuiKey_A;
        case AKEYCODE_B:                    return VanGuiKey_B;
        case AKEYCODE_C:                    return VanGuiKey_C;
        case AKEYCODE_D:                    return VanGuiKey_D;
        case AKEYCODE_E:                    return VanGuiKey_E;
        case AKEYCODE_F:                    return VanGuiKey_F;
        case AKEYCODE_G:                    return VanGuiKey_G;
        case AKEYCODE_H:                    return VanGuiKey_H;
        case AKEYCODE_I:                    return VanGuiKey_I;
        case AKEYCODE_J:                    return VanGuiKey_J;
        case AKEYCODE_K:                    return VanGuiKey_K;
        case AKEYCODE_L:                    return VanGuiKey_L;
        case AKEYCODE_M:                    return VanGuiKey_M;
        case AKEYCODE_N:                    return VanGuiKey_N;
        case AKEYCODE_O:                    return VanGuiKey_O;
        case AKEYCODE_P:                    return VanGuiKey_P;
        case AKEYCODE_Q:                    return VanGuiKey_Q;
        case AKEYCODE_R:                    return VanGuiKey_R;
        case AKEYCODE_S:                    return VanGuiKey_S;
        case AKEYCODE_T:                    return VanGuiKey_T;
        case AKEYCODE_U:                    return VanGuiKey_U;
        case AKEYCODE_V:                    return VanGuiKey_V;
        case AKEYCODE_W:                    return VanGuiKey_W;
        case AKEYCODE_X:                    return VanGuiKey_X;
        case AKEYCODE_Y:                    return VanGuiKey_Y;
        case AKEYCODE_Z:                    return VanGuiKey_Z;
        case AKEYCODE_F1:                   return VanGuiKey_F1;
        case AKEYCODE_F2:                   return VanGuiKey_F2;
        case AKEYCODE_F3:                   return VanGuiKey_F3;
        case AKEYCODE_F4:                   return VanGuiKey_F4;
        case AKEYCODE_F5:                   return VanGuiKey_F5;
        case AKEYCODE_F6:                   return VanGuiKey_F6;
        case AKEYCODE_F7:                   return VanGuiKey_F7;
        case AKEYCODE_F8:                   return VanGuiKey_F8;
        case AKEYCODE_F9:                   return VanGuiKey_F9;
        case AKEYCODE_F10:                  return VanGuiKey_F10;
        case AKEYCODE_F11:                  return VanGuiKey_F11;
        case AKEYCODE_F12:                  return VanGuiKey_F12;
        default:                            return VanGuiKey_None;
    }
}

int32_t VanGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event)
{
    VanGuiIO& io = VanGui::GetIO();
    int32_t event_type = AInputEvent_getType(input_event);
    switch (event_type)
    {
    case AINPUT_EVENT_TYPE_KEY:
    {
        int32_t event_key_code = AKeyEvent_getKeyCode(input_event);
        int32_t event_scan_code = AKeyEvent_getScanCode(input_event);
        int32_t event_action = AKeyEvent_getAction(input_event);
        int32_t event_meta_state = AKeyEvent_getMetaState(input_event);

        io.AddKeyEvent(VanGuiMod_Ctrl,  (event_meta_state & AMETA_CTRL_ON)  != 0);
        io.AddKeyEvent(VanGuiMod_Shift, (event_meta_state & AMETA_SHIFT_ON) != 0);
        io.AddKeyEvent(VanGuiMod_Alt,   (event_meta_state & AMETA_ALT_ON)   != 0);
        io.AddKeyEvent(VanGuiMod_Super, (event_meta_state & AMETA_META_ON)  != 0);

        switch (event_action)
        {
        // FIXME: AKEY_EVENT_ACTION_DOWN and AKEY_EVENT_ACTION_UP occur at once as soon as a touch pointer
        // goes up from a key. We use a simple key event queue/ and process one event per key per frame in
        // VanGui_ImplAndroid_NewFrame()...or consider using IO queue, if suitable
        case AKEY_EVENT_ACTION_DOWN:
        case AKEY_EVENT_ACTION_UP:
        {
            VanGuiKey key = VanGui_ImplAndroid_KeyCodeToVanGuiKey(event_key_code);
            if (key != VanGuiKey_None)
            {
                io.AddKeyEvent(key, event_action == AKEY_EVENT_ACTION_DOWN);
                io.SetKeyEventNativeData(key, event_key_code, event_scan_code);
            }

            break;
        }
        default:
            break;
        }
        break;
    }
    case AINPUT_EVENT_TYPE_MOTION:
    {
        int32_t event_action = AMotionEvent_getAction(input_event);
        int32_t event_pointer_index = (event_action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        event_action &= AMOTION_EVENT_ACTION_MASK;

        switch (AMotionEvent_getToolType(input_event, event_pointer_index))
        {
        case AMOTION_EVENT_TOOL_TYPE_MOUSE:
            io.AddMouseSourceEvent(VanGuiMouseSource_Mouse);
            break;
        case AMOTION_EVENT_TOOL_TYPE_STYLUS:
        case AMOTION_EVENT_TOOL_TYPE_ERASER:
            io.AddMouseSourceEvent(VanGuiMouseSource_Pen);
            break;
        case AMOTION_EVENT_TOOL_TYPE_FINGER:
        default:
            io.AddMouseSourceEvent(VanGuiMouseSource_TouchScreen);
            break;
        }

        switch (event_action)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_UP:
        {
            // Physical mouse buttons (and probably other physical devices) also invoke the actions AMOTION_EVENT_ACTION_DOWN/_UP,
            // but we have to process them separately to identify the actual button pressed. This is done below via
            // AMOTION_EVENT_ACTION_BUTTON_PRESS/_RELEASE. Here, we only process "FINGER" input (and "UNKNOWN", as a fallback).
            int tool_type = AMotionEvent_getToolType(input_event, event_pointer_index);
            if (tool_type == AMOTION_EVENT_TOOL_TYPE_FINGER || tool_type == AMOTION_EVENT_TOOL_TYPE_UNKNOWN)
            {
                io.AddMousePosEvent(AMotionEvent_getX(input_event, event_pointer_index), AMotionEvent_getY(input_event, event_pointer_index));
                io.AddMouseButtonEvent(0, event_action == AMOTION_EVENT_ACTION_DOWN);
            }
            break;
        }
        case AMOTION_EVENT_ACTION_BUTTON_PRESS:
        case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
        {
            int32_t button_state = AMotionEvent_getButtonState(input_event);
            io.AddMouseButtonEvent(0, (button_state & AMOTION_EVENT_BUTTON_PRIMARY) != 0);
            io.AddMouseButtonEvent(1, (button_state & AMOTION_EVENT_BUTTON_SECONDARY) != 0);
            io.AddMouseButtonEvent(2, (button_state & AMOTION_EVENT_BUTTON_TERTIARY) != 0);
            break;
        }
        case AMOTION_EVENT_ACTION_HOVER_MOVE: // Hovering: Tool moves while NOT pressed (such as a physical mouse)
        case AMOTION_EVENT_ACTION_MOVE:       // Touch pointer moves while DOWN
            io.AddMousePosEvent(AMotionEvent_getX(input_event, event_pointer_index), AMotionEvent_getY(input_event, event_pointer_index));
            break;
        case AMOTION_EVENT_ACTION_SCROLL:
            io.AddMouseWheelEvent(AMotionEvent_getAxisValue(input_event, AMOTION_EVENT_AXIS_HSCROLL, event_pointer_index), AMotionEvent_getAxisValue(input_event, AMOTION_EVENT_AXIS_VSCROLL, event_pointer_index));
            break;
        default:
            break;
        }
    }
        return 1;
    default:
        break;
    }

    return 0;
}

bool VanGui_ImplAndroid_Init(ANativeWindow* window)
{
    VANGUI_CHECKVERSION();

    g_Window = window;
    g_Time = 0.0;

    // Setup backend capabilities flags
    VanGuiIO& io = VanGui::GetIO();
    io.BackendPlatformName = "vangui_impl_android";

    return true;
}

void VanGui_ImplAndroid_Shutdown()
{
    VanGuiIO& io = VanGui::GetIO();
    io.BackendPlatformName = nullptr;
}

void VanGui_ImplAndroid_NewFrame()
{
    VanGuiIO& io = VanGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    int32_t window_width = ANativeWindow_getWidth(g_Window);
    int32_t window_height = ANativeWindow_getHeight(g_Window);
    int display_width = window_width;
    int display_height = window_height;

    io.DisplaySize = VanVec2(static_cast<float>(window_width), static_cast<float>(window_height));
    if (window_width > 0 && window_height > 0)
        io.DisplayFramebufferScale = VanVec2(static_cast<float>(display_width) / window_width, static_cast<float>(display_height) / window_height);

    // Setup time step
    struct timespec current_timespec;
    clock_gettime(CLOCK_MONOTONIC, &current_timespec);
    double current_time = static_cast<double>(current_timespec.tv_sec) + (current_timespec.tv_nsec / 1000000000.0);
    io.DeltaTime = g_Time > 0.0 ? static_cast<float>(current_time - g_Time) : static_cast<float>(1.0f / 60.0f);
    g_Time = current_time;
}

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
