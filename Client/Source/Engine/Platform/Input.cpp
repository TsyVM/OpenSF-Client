#include "Engine/Platform/Input.hpp"

#include "Engine/Core/Strings.hpp"

#include "Engine/Platform/Keys.hpp"

namespace eng {

void Input::on_key(u32 vk, bool down) {
    if (vk >= 256) return;
    if (down && !keys_[vk]) key_hit_[vk] = true;
    keys_[vk] = down;
}

void Input::on_mouse_button(int button, bool down) {
    if (button < 0 || button >= 5) return;
    if (down && !mouse_[button]) mouse_hit_[button] = true;
    mouse_[button] = down;
}

void Input::on_focus_lost() {
    keys_.fill(false);
    mouse_.fill(false);
}

void Input::end_frame() {
    prev_keys_ = keys_;
    prev_mouse_ = mouse_;
    key_hit_.fill(false);
    mouse_hit_.fill(false);
    wheel_ = 0;
    raw_dx_ = 0;
    raw_dy_ = 0;
}

u32 Input::first_pressed() const {
    for (u32 k = 1; k < 256; ++k)
        if (key_pressed(k)) return k;
    for (int b = 0; b < 5; ++b)
        if (mouse_pressed(b)) return 0x100 + u32(b);
    return 0;
}

bool Input::any_activity() const { return first_pressed() != 0 || raw_dx_ != 0 || raw_dy_ != 0 || wheel_ != 0; }

namespace {

struct NamedKey {
    u32 code;
    const char* name;
};

const NamedKey kNames[] = {
    {0x100, "MOUSE1"}, {0x101, "MOUSE2"}, {0x102, "MOUSE3"}, {0x103, "MOUSE4"}, {0x104, "MOUSE5"},
    {VK_SPACE, "SPACE"}, {VK_LCONTROL, "LCTRL"}, {VK_CONTROL, "CTRL"}, {VK_LSHIFT, "LSHIFT"}, {VK_SHIFT, "SHIFT"},
    {VK_LMENU, "LALT"}, {VK_TAB, "TAB"}, {VK_RETURN, "ENTER"}, {VK_ESCAPE, "ESCAPE"}, {VK_BACK, "BACKSPACE"},
    {VK_UP, "UP"}, {VK_DOWN, "DOWN"}, {VK_LEFT, "LEFT"}, {VK_RIGHT, "RIGHT"}, {VK_F1, "F1"}, {VK_F2, "F2"},
    {VK_F3, "F3"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"}, {VK_F9, "F9"},
    {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"}, {VK_CAPITAL, "CAPSLOCK"}, {VK_OEM_3, "GRAVE"},
    {VK_RSHIFT, "RSHIFT"}, {VK_RCONTROL, "RCTRL"}, {VK_RMENU, "RALT"}, {VK_MENU, "ALT"}, {VK_INSERT, "INSERT"},
    {VK_DELETE, "DELETE"}, {VK_HOME, "HOME"}, {VK_END, "END"}, {VK_PRIOR, "PGUP"}, {VK_NEXT, "PGDN"},
    {VK_NUMPAD0, "NUM0"}, {VK_NUMPAD1, "NUM1"}, {VK_NUMPAD2, "NUM2"}, {VK_NUMPAD3, "NUM3"}, {VK_NUMPAD4, "NUM4"},
    {VK_NUMPAD5, "NUM5"}, {VK_NUMPAD6, "NUM6"}, {VK_NUMPAD7, "NUM7"}, {VK_NUMPAD8, "NUM8"}, {VK_NUMPAD9, "NUM9"},
    {VK_MULTIPLY, "NUMSTAR"}, {VK_ADD, "NUMPLUS"}, {VK_SUBTRACT, "NUMMINUS"}, {VK_DECIMAL, "NUMDOT"},
    {VK_DIVIDE, "NUMSLASH"}, {VK_OEM_1, "SEMICOLON"}, {VK_OEM_PLUS, "EQUALS"}, {VK_OEM_COMMA, "COMMA"},
    {VK_OEM_MINUS, "MINUS"}, {VK_OEM_PERIOD, "PERIOD"}, {VK_OEM_2, "SLASH"}, {VK_OEM_4, "LBRACKET"},
    {VK_OEM_5, "BACKSLASH"}, {VK_OEM_6, "RBRACKET"}, {VK_OEM_7, "APOSTROPHE"},
};

}  // namespace

std::string Input::binding_name(u32 code) {
    for (const auto& k : kNames)
        if (k.code == code) return k.name;
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, char(code));
    return str::format("KEY%u", code);
}

u32 Input::binding_from_name(const std::string& name) {
    std::string n = str::upper(name);
    for (const auto& k : kNames)
        if (n == k.name) return k.code;
    if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return u32(n[0]);
    if (n.starts_with("KEY")) return u32(std::atoi(n.c_str() + 3));
    return 0;
}

}  // namespace eng
