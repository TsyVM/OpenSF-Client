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

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2023-04-17: BREAKING: Removed call to VanGui::NewFrame() from VanGui_ImplGLUT_NewFrame(). Needs to be called from the main application loop, like with every other backends.
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2019-04-03: Misc: Renamed vangui_impl_freeglut.cpp/.h to vangui_impl_glut.cpp/.h.
//  2019-03-25: Misc: Made io.DeltaTime always above zero.
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-03-22: Added GLUT Platform binding.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_glut.h"
#define GL_SILENCE_DEPRECATION
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/freeglut.h>
#endif

#ifdef _MSC_VER
#pragma warning (disable: 4505) // unreferenced local function has been removed (stb stuff)
#endif

static int g_Time = 0;          // Current time, in milliseconds

// Glut has one function for characters and one for "special keys". We map the characters in the 0..255 range and the keys above.
static VanGuiKey VanGui_ImplGLUT_KeyToVanGuiKey(int key)
{
    switch (key)
    {
        case '\t':                      return VanGuiKey_Tab;
        case 256 + GLUT_KEY_LEFT:       return VanGuiKey_LeftArrow;
        case 256 + GLUT_KEY_RIGHT:      return VanGuiKey_RightArrow;
        case 256 + GLUT_KEY_UP:         return VanGuiKey_UpArrow;
        case 256 + GLUT_KEY_DOWN:       return VanGuiKey_DownArrow;
        case 256 + GLUT_KEY_PAGE_UP:    return VanGuiKey_PageUp;
        case 256 + GLUT_KEY_PAGE_DOWN:  return VanGuiKey_PageDown;
        case 256 + GLUT_KEY_HOME:       return VanGuiKey_Home;
        case 256 + GLUT_KEY_END:        return VanGuiKey_End;
        case 256 + GLUT_KEY_INSERT:     return VanGuiKey_Insert;
        case 127:                       return VanGuiKey_Delete;
        case 8:                         return VanGuiKey_Backspace;
        case ' ':                       return VanGuiKey_Space;
        case 13:                        return VanGuiKey_Enter;
        case 27:                        return VanGuiKey_Escape;
        case 39:                        return VanGuiKey_Apostrophe;
        case 44:                        return VanGuiKey_Comma;
        case 45:                        return VanGuiKey_Minus;
        case 46:                        return VanGuiKey_Period;
        case 47:                        return VanGuiKey_Slash;
        case 59:                        return VanGuiKey_Semicolon;
        case 61:                        return VanGuiKey_Equal;
        case 91:                        return VanGuiKey_LeftBracket;
        case 92:                        return VanGuiKey_Backslash;
        case 93:                        return VanGuiKey_RightBracket;
        case 96:                        return VanGuiKey_GraveAccent;
        //case 0:                         return VanGuiKey_CapsLock;
        //case 0:                         return VanGuiKey_ScrollLock;
        case 256 + 0x006D:              return VanGuiKey_NumLock;
        //case 0:                         return VanGuiKey_PrintScreen;
        //case 0:                         return VanGuiKey_Pause;
        //case '0':                       return VanGuiKey_Keypad0;
        //case '1':                       return VanGuiKey_Keypad1;
        //case '2':                       return VanGuiKey_Keypad2;
        //case '3':                       return VanGuiKey_Keypad3;
        //case '4':                       return VanGuiKey_Keypad4;
        //case '5':                       return VanGuiKey_Keypad5;
        //case '6':                       return VanGuiKey_Keypad6;
        //case '7':                       return VanGuiKey_Keypad7;
        //case '8':                       return VanGuiKey_Keypad8;
        //case '9':                       return VanGuiKey_Keypad9;
        //case 46:                        return VanGuiKey_KeypadDecimal;
        //case 47:                        return VanGuiKey_KeypadDivide;
        case 42:                        return VanGuiKey_KeypadMultiply;
        //case 45:                        return VanGuiKey_KeypadSubtract;
        case 43:                        return VanGuiKey_KeypadAdd;
        //case 13:                        return VanGuiKey_KeypadEnter;
        //case 0:                         return VanGuiKey_KeypadEqual;
        case 256 + 0x0072:              return VanGuiKey_LeftCtrl;
        case 256 + 0x0070:              return VanGuiKey_LeftShift;
        case 256 + 0x0074:              return VanGuiKey_LeftAlt;
        //case 0:                         return VanGuiKey_LeftSuper;
        case 256 + 0x0073:              return VanGuiKey_RightCtrl;
        case 256 + 0x0071:              return VanGuiKey_RightShift;
        case 256 + 0x0075:              return VanGuiKey_RightAlt;
        //case 0:                         return VanGuiKey_RightSuper;
        //case 0:                         return VanGuiKey_Menu;
        case '0':                       return VanGuiKey_0;
        case '1':                       return VanGuiKey_1;
        case '2':                       return VanGuiKey_2;
        case '3':                       return VanGuiKey_3;
        case '4':                       return VanGuiKey_4;
        case '5':                       return VanGuiKey_5;
        case '6':                       return VanGuiKey_6;
        case '7':                       return VanGuiKey_7;
        case '8':                       return VanGuiKey_8;
        case '9':                       return VanGuiKey_9;
        case 'A': case 'a':             return VanGuiKey_A;
        case 'B': case 'b':             return VanGuiKey_B;
        case 'C': case 'c':             return VanGuiKey_C;
        case 'D': case 'd':             return VanGuiKey_D;
        case 'E': case 'e':             return VanGuiKey_E;
        case 'F': case 'f':             return VanGuiKey_F;
        case 'G': case 'g':             return VanGuiKey_G;
        case 'H': case 'h':             return VanGuiKey_H;
        case 'I': case 'i':             return VanGuiKey_I;
        case 'J': case 'j':             return VanGuiKey_J;
        case 'K': case 'k':             return VanGuiKey_K;
        case 'L': case 'l':             return VanGuiKey_L;
        case 'M': case 'm':             return VanGuiKey_M;
        case 'N': case 'n':             return VanGuiKey_N;
        case 'O': case 'o':             return VanGuiKey_O;
        case 'P': case 'p':             return VanGuiKey_P;
        case 'Q': case 'q':             return VanGuiKey_Q;
        case 'R': case 'r':             return VanGuiKey_R;
        case 'S': case 's':             return VanGuiKey_S;
        case 'T': case 't':             return VanGuiKey_T;
        case 'U': case 'u':             return VanGuiKey_U;
        case 'V': case 'v':             return VanGuiKey_V;
        case 'W': case 'w':             return VanGuiKey_W;
        case 'X': case 'x':             return VanGuiKey_X;
        case 'Y': case 'y':             return VanGuiKey_Y;
        case 'Z': case 'z':             return VanGuiKey_Z;
        case 256 + GLUT_KEY_F1:         return VanGuiKey_F1;
        case 256 + GLUT_KEY_F2:         return VanGuiKey_F2;
        case 256 + GLUT_KEY_F3:         return VanGuiKey_F3;
        case 256 + GLUT_KEY_F4:         return VanGuiKey_F4;
        case 256 + GLUT_KEY_F5:         return VanGuiKey_F5;
        case 256 + GLUT_KEY_F6:         return VanGuiKey_F6;
        case 256 + GLUT_KEY_F7:         return VanGuiKey_F7;
        case 256 + GLUT_KEY_F8:         return VanGuiKey_F8;
        case 256 + GLUT_KEY_F9:         return VanGuiKey_F9;
        case 256 + GLUT_KEY_F10:        return VanGuiKey_F10;
        case 256 + GLUT_KEY_F11:        return VanGuiKey_F11;
        case 256 + GLUT_KEY_F12:        return VanGuiKey_F12;
        default:                        return VanGuiKey_None;
    }
}

bool VanGui_ImplGLUT_Init()
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();

#ifdef FREEGLUT
    io.BackendPlatformName = "vangui_impl_glut (freeglut)";
#else
    io.BackendPlatformName = "vangui_impl_glut";
#endif
    g_Time = 0;

    return true;
}

void VanGui_ImplGLUT_InstallFuncs()
{
    glutReshapeFunc(VanGui_ImplGLUT_ReshapeFunc);
    glutMotionFunc(VanGui_ImplGLUT_MotionFunc);
    glutPassiveMotionFunc(VanGui_ImplGLUT_MotionFunc);
    glutMouseFunc(VanGui_ImplGLUT_MouseFunc);
#ifdef __FREEGLUT_EXT_H__
    glutMouseWheelFunc(VanGui_ImplGLUT_MouseWheelFunc);
#endif
    glutKeyboardFunc(VanGui_ImplGLUT_KeyboardFunc);
    glutKeyboardUpFunc(VanGui_ImplGLUT_KeyboardUpFunc);
    glutSpecialFunc(VanGui_ImplGLUT_SpecialFunc);
    glutSpecialUpFunc(VanGui_ImplGLUT_SpecialUpFunc);
}

void VanGui_ImplGLUT_Shutdown()
{
    VanGuiIO& io = VanGui::GetIO();
    io.BackendPlatformName = nullptr;
}

void VanGui_ImplGLUT_NewFrame()
{
    // Setup time step
    VanGuiIO& io = VanGui::GetIO();
    int current_time = glutGet(GLUT_ELAPSED_TIME);
    int delta_time_ms = (current_time - g_Time);
    if (delta_time_ms <= 0)
        delta_time_ms = 1;
    io.DeltaTime = delta_time_ms / 1000.0f;
    g_Time = current_time;
}

static void VanGui_ImplGLUT_UpdateKeyModifiers()
{
    VanGuiIO& io = VanGui::GetIO();
    int glut_key_mods = glutGetModifiers();
    io.AddKeyEvent(VanGuiMod_Ctrl, (glut_key_mods & GLUT_ACTIVE_CTRL) != 0);
    io.AddKeyEvent(VanGuiMod_Shift, (glut_key_mods & GLUT_ACTIVE_SHIFT) != 0);
    io.AddKeyEvent(VanGuiMod_Alt, (glut_key_mods & GLUT_ACTIVE_ALT) != 0);
}

static void VanGui_ImplGLUT_AddKeyEvent(VanGuiKey key, bool down, int native_keycode)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, -1); // To support legacy indexing (<1.87 user code)
}

void VanGui_ImplGLUT_KeyboardFunc(unsigned char c, int x, int y)
{
    // Send character to vangui
    //printf("char_down_func %d '%c'\n", c, c);
    VanGuiIO& io = VanGui::GetIO();
    if (c >= 32)
        io.AddInputCharacter(static_cast<unsigned int>(c));

    VanGuiKey key = VanGui_ImplGLUT_KeyToVanGuiKey(c);
    VanGui_ImplGLUT_AddKeyEvent(key, true, c);
    VanGui_ImplGLUT_UpdateKeyModifiers();
    (void)x; (void)y; // Unused
}

void VanGui_ImplGLUT_KeyboardUpFunc(unsigned char c, int x, int y)
{
    //printf("char_up_func %d '%c'\n", c, c);
    VanGuiKey key = VanGui_ImplGLUT_KeyToVanGuiKey(c);
    VanGui_ImplGLUT_AddKeyEvent(key, false, c);
    VanGui_ImplGLUT_UpdateKeyModifiers();
    (void)x; (void)y; // Unused
}

void VanGui_ImplGLUT_SpecialFunc(int key, int x, int y)
{
    //printf("key_down_func %d\n", key);
    VanGuiKey vangui_key = VanGui_ImplGLUT_KeyToVanGuiKey(key + 256);
    VanGui_ImplGLUT_AddKeyEvent(vangui_key, true, key + 256);
    VanGui_ImplGLUT_UpdateKeyModifiers();
    (void)x; (void)y; // Unused
}

void VanGui_ImplGLUT_SpecialUpFunc(int key, int x, int y)
{
    //printf("key_up_func %d\n", key);
    VanGuiKey vangui_key = VanGui_ImplGLUT_KeyToVanGuiKey(key + 256);
    VanGui_ImplGLUT_AddKeyEvent(vangui_key, false, key + 256);
    VanGui_ImplGLUT_UpdateKeyModifiers();
    (void)x; (void)y; // Unused
}

void VanGui_ImplGLUT_MouseFunc(int glut_button, int state, int x, int y)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    int button = -1;
    if (glut_button == GLUT_LEFT_BUTTON) button = 0;
    if (glut_button == GLUT_RIGHT_BUTTON) button = 1;
    if (glut_button == GLUT_MIDDLE_BUTTON) button = 2;
    if (button != -1 && (state == GLUT_DOWN || state == GLUT_UP))
        io.AddMouseButtonEvent(button, state == GLUT_DOWN);
}

#ifdef __FREEGLUT_EXT_H__
void VanGui_ImplGLUT_MouseWheelFunc(int button, int dir, int x, int y)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    if (dir != 0)
        io.AddMouseWheelEvent(0.0f, dir > 0 ? 1.0f : -1.0f);
    (void)button; // Unused
}
#endif

void VanGui_ImplGLUT_ReshapeFunc(int w, int h)
{
    VanGuiIO& io = VanGui::GetIO();
    io.DisplaySize = VanVec2(static_cast<float>(w), static_cast<float>(h));
}

void VanGui_ImplGLUT_MotionFunc(int x, int y)
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
