// The game pad: Xbox pads (and anything else speaking XInput) and Sony's pads (DualShock 4 and
// DualSense, by USB or Bluetooth, read as HID devices), polled once per frame.
//
// Every pad is read in the Xbox layout: A is Cross, B Circle, X Square, Y Triangle, LB/RB are
// L1/R1, Start is Options and Back is Share/Create (the touch pad's click too). The triggers are
// also buttons (pressed past halfway), so they can be bound like any other. With several pads
// plugged in, the one last used is the one read; `kind` says which family it is, for prompts.
#pragma once

#include "Engine/Core/Types.hpp"

#include <memory>
#include <string>

namespace eng {

enum PadButton : u32 {
    kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008,
    kPadStart = 0x0010, kPadBack = 0x0020, kPadLThumb = 0x0040, kPadRThumb = 0x0080,
    kPadLB = 0x0100, kPadRB = 0x0200, kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000,
    kPadLT = 0x10000, kPadRT = 0x20000,   // the triggers, past halfway
};

enum class PadKind { None, Xbox, DualShock4, DualSense };

// Android: what the system's pad events say (fed by the window, read by Gamepad::poll).
namespace android_pad {
void key(int keycode, bool down);
void axes(float lx, float ly, float rx, float ry, float lt, float rt, float hat_x, float hat_y);
void connected(const char* name, int vendor);
}  // namespace android_pad

// Linux and macOS: the first pad the system has, read by Gamepad::poll (Linux: the kernel's
// gamepad events, in Gamepad.cpp; macOS: the GameController framework, in Gamepad_mac.mm).
namespace desktop_pad {
struct State {
    bool connected = false;
    u32 buttons = 0;                  // PadButton
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;   // sticks -1..1 (up positive), triggers 0..1
    std::string name;
    int vendor = 0;                   // USB vendor: 0x054C Sony
};
void poll(State& out);
}  // namespace desktop_pad

class Gamepad {
public:
    Gamepad();
    ~Gamepad();
    void poll();
    bool connected() const { return connected_; }
    PadKind kind() const { return kind_; }
    bool playstation() const { return kind_ == PadKind::DualShock4 || kind_ == PadKind::DualSense; }
    const std::string& name() const { return name_; }
    bool down(u32 b) const { return (buttons_ & b) != 0; }
    bool pressed(u32 b) const { return (buttons_ & b) && !(prev_ & b); }
    // The first button pressed this frame (0: none), for binding one to an action.
    u32 any_pressed() const;
    // Sticks in -1..1 (y up), dead zone removed; triggers in 0..1.
    float lx() const { return lx_; }
    float ly() const { return ly_; }
    float rx() const { return rx_; }
    float ry() const { return ry_; }
    float lt() const { return lt_; }
    float rt() const { return rt_; }
    // Anything moved or pressed this frame (the player picked the pad up).
    bool active() const { return active_; }

    void rumble(float low, float high);

    // Tests: a pad that is not plugged in, held in this state until fed another (null: the real
    // pads again). Sticks and triggers as the game reads them (dead zones already out).
    struct Feed {
        u32 buttons = 0;
        float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        PadKind kind = PadKind::Xbox;
    };
    void feed(const Feed* state);
    bool fed() const { return fed_; }

    // A button's name in the pad's own family (Cross, L1, Options / A, LB, Start) and its
    // settings name (A, LB, RT ...), which is the same whatever the pad.
    static std::string button_label(u32 b, PadKind kind);
    static std::string button_key(u32 b);
    static u32 button_from_key(const std::string& key);

private:
    void poll_platform();
    struct Sony;
    std::unique_ptr<Sony> sony_;
    bool connected_ = false;
    PadKind kind_ = PadKind::None;
    std::string name_;
    int xinput_ = -1;
    u32 buttons_ = 0, prev_ = 0;
    float lx_ = 0, ly_ = 0, rx_ = 0, ry_ = 0, lt_ = 0, rt_ = 0;
    bool active_ = false;
    int retry_ = 0;
    float rumble_low_ = 0, rumble_high_ = 0;
    bool fed_ = false;
    Feed feed_;
};

// Tests: the Sony report layouts fed through the parser (logged; false on a mismatch).
bool gamepad_self_test();

}  // namespace eng
