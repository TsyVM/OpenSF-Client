// Keyboard and mouse state for gameplay. Text entry and menu interaction go through
// VanGUI; this is for movement keys, buttons and raw mouse motion.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <string>

namespace eng {

enum MouseButton : int { kMouseLeft = 0, kMouseRight = 1, kMouseMiddle = 2, kMouse4 = 3, kMouse5 = 4 };

class Input {
public:
    // Called by the window procedure.
    void on_key(u32 vk, bool down);
    void on_mouse_button(int button, bool down);
    void on_wheel(float notches) { wheel_ += notches; }
    void on_raw_mouse(long dx, long dy) {
        raw_dx_ += float(dx);
        raw_dy_ += float(dy);
    }
    void on_focus_lost();

    // Frame boundary: clears the per-frame edges and accumulators.
    void end_frame();

    bool key_down(u32 vk) const { return vk < 256 && keys_[vk]; }
    // Pressed this frame, even if already let go (a tap quicker than a frame).
    bool key_pressed(u32 vk) const { return vk < 256 && ((keys_[vk] && !prev_keys_[vk]) || key_hit_[vk]); }
    bool mouse_down(int b) const { return b >= 0 && b < 5 && mouse_[b]; }
    bool mouse_pressed(int b) const { return b >= 0 && b < 5 && ((mouse_[b] && !prev_mouse_[b]) || mouse_hit_[b]); }
    float wheel() const { return wheel_; }
    float mouse_dx() const { return raw_dx_; }
    float mouse_dy() const { return raw_dy_; }

    // A binding is a virtual-key code or a mouse button encoded as 0x100 + button.
    bool binding_down(u32 code) const { return code >= 0x100 ? mouse_down(int(code - 0x100)) : key_down(code); }
    bool binding_pressed(u32 code) const {
        return code >= 0x100 ? mouse_pressed(int(code - 0x100)) : key_pressed(code);
    }

    // The first key or mouse button pressed this frame, as a binding code (0: none).
    u32 first_pressed() const;
    // Any key or mouse button pressed, or the mouse moved, this frame.
    bool any_activity() const;

    static std::string binding_name(u32 code);
    static u32 binding_from_name(const std::string& name);

private:
    std::array<bool, 256> keys_{};
    std::array<bool, 256> prev_keys_{};
    std::array<bool, 5> mouse_{};
    std::array<bool, 5> prev_mouse_{};
    std::array<bool, 256> key_hit_{};    // went down during this frame
    std::array<bool, 5> mouse_hit_{};
    float wheel_ = 0;
    float raw_dx_ = 0;
    float raw_dy_ = 0;
};

}  // namespace eng
