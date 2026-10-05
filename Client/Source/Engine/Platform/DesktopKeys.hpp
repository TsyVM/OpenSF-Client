// The desktop platforms other than Windows (Linux, macOS): the game's keys are Windows virtual-key
// codes everywhere (Keys.hpp), and VanGUI wants its own. Each platform turns its key codes into
// virtual keys; this turns those into VanGUI's, so both systems share one table.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

namespace eng {

// Virtual keys these platforms send beyond Keys.hpp's (Windows' values).
inline constexpr u32 kVkLWin = 0x5B, kVkRWin = 0x5C, kVkApps = 0x5D, kVkPause = 0x13, kVkSnapshot = 0x2C;
inline constexpr u32 kVkNumLock = 0x90, kVkScroll = 0x91, kVkSeparator = 0x6C;

inline VanGuiKey vangui_key_of(u32 vk) {
    if (vk >= 'A' && vk <= 'Z') return VanGuiKey(VanGuiKey_A + int(vk - 'A'));
    if (vk >= '0' && vk <= '9') return VanGuiKey(VanGuiKey_0 + int(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return VanGuiKey(VanGuiKey_F1 + int(vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return VanGuiKey(VanGuiKey_Keypad0 + int(vk - VK_NUMPAD0));
    switch (vk) {
        case VK_TAB: return VanGuiKey_Tab;
        case VK_LEFT: return VanGuiKey_LeftArrow;
        case VK_RIGHT: return VanGuiKey_RightArrow;
        case VK_UP: return VanGuiKey_UpArrow;
        case VK_DOWN: return VanGuiKey_DownArrow;
        case VK_PRIOR: return VanGuiKey_PageUp;
        case VK_NEXT: return VanGuiKey_PageDown;
        case VK_HOME: return VanGuiKey_Home;
        case VK_END: return VanGuiKey_End;
        case VK_INSERT: return VanGuiKey_Insert;
        case VK_DELETE: return VanGuiKey_Delete;
        case VK_BACK: return VanGuiKey_Backspace;
        case VK_SPACE: return VanGuiKey_Space;
        case VK_RETURN: return VanGuiKey_Enter;
        case VK_ESCAPE: return VanGuiKey_Escape;
        case VK_LCONTROL: return VanGuiKey_LeftCtrl;
        case VK_RCONTROL: return VanGuiKey_RightCtrl;
        case VK_LSHIFT: return VanGuiKey_LeftShift;
        case VK_RSHIFT: return VanGuiKey_RightShift;
        case VK_LMENU: return VanGuiKey_LeftAlt;
        case VK_RMENU: return VanGuiKey_RightAlt;
        case kVkLWin: return VanGuiKey_LeftSuper;
        case kVkRWin: return VanGuiKey_RightSuper;
        case kVkApps: return VanGuiKey_Menu;
        case VK_OEM_7: return VanGuiKey_Apostrophe;
        case VK_OEM_COMMA: return VanGuiKey_Comma;
        case VK_OEM_MINUS: return VanGuiKey_Minus;
        case VK_OEM_PERIOD: return VanGuiKey_Period;
        case VK_OEM_2: return VanGuiKey_Slash;
        case VK_OEM_1: return VanGuiKey_Semicolon;
        case VK_OEM_PLUS: return VanGuiKey_Equal;
        case VK_OEM_4: return VanGuiKey_LeftBracket;
        case VK_OEM_5: return VanGuiKey_Backslash;
        case VK_OEM_6: return VanGuiKey_RightBracket;
        case VK_OEM_3: return VanGuiKey_GraveAccent;
        case VK_CAPITAL: return VanGuiKey_CapsLock;
        case kVkScroll: return VanGuiKey_ScrollLock;
        case kVkNumLock: return VanGuiKey_NumLock;
        case kVkSnapshot: return VanGuiKey_PrintScreen;
        case kVkPause: return VanGuiKey_Pause;
        case VK_DECIMAL: return VanGuiKey_KeypadDecimal;
        case VK_DIVIDE: return VanGuiKey_KeypadDivide;
        case VK_MULTIPLY: return VanGuiKey_KeypadMultiply;
        case VK_SUBTRACT: return VanGuiKey_KeypadSubtract;
        case VK_ADD: return VanGuiKey_KeypadAdd;
        default: return VanGuiKey_None;
    }
}

}  // namespace eng
