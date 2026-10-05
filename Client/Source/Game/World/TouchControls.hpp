// The touch screen in a match: what a phone has instead of keys and a mouse. A stick under the left
// thumb, set down wherever the thumb lands (pushed part way it walks, all the way it runs); Fire,
// Aim, Jump, Crouch, Reload, Swap and Use under the right thumb (Fire also looks while it is held
// and dragged); Menu, Chat and Score along the top; and the rest of the screen to look by swiping.
// The player may move and size each of them (arrange(), Options, Controls, Touch, Arrange).
//
// Each finger takes the part it came down on and keeps it until it is lifted, so a thumb sliding
// off Fire keeps firing and a look never turns into a button. What the fingers do comes out as the
// game's own actions (GameWorld::binding_down / binding_pressed), a move like a controller's stick
// and a look in degrees, so the match plays a touch the same as a key or a pad. No aim assist.
//
// A desktop never sees a finger, so none of it shows there; on a phone it shows from the start of a
// match, steps aside when a controller or the keyboard is used, and comes back at the next touch.
#pragma once

#include "Game/Settings.hpp"

#include <array>
#include <vector>

struct VanDrawList;

namespace eng {
class Window;
}

namespace lsf {

class TouchControls {
public:
    TouchControls();

    struct Context {
        bool control = false;     // the match has the controls (no menu, chat or dialog up)
        bool alive = false;
        bool scope = false;       // the gun in hand has a scope: Aim shows
        bool other_input = false; // a controller or a key was used this frame: the controls step aside
    };
    // Once a frame, before the match reads its actions.
    void update(const eng::Window& window, const TouchSettings& s, const Context& c);

    // On the screen and in use (the fingers are the controls, not the menus' pointer).
    bool shown() const { return shown_; }
    bool down(Action a) const;
    bool pressed(Action a) const;
    // The stick: a direction (forward, to the right) at full length, and whether it walks.
    bool moving() const { return moving_; }
    float forward() const { return forward_; }
    float side() const { return side_; }
    bool walking() const { return walking_; }
    // The look this frame, in degrees (right and down positive, as the mouse's).
    float look_x() const { return look_x_; }
    float look_y() const { return look_y_; }
    bool menu_pressed() const { return menu_pressed_; }
    bool chat_pressed() const { return chat_pressed_; }

    void draw(VanDrawList* dl, const TouchSettings& s) const;

    // ── Where the controls are ──
    struct Screen {
        float w = 0, h = 0;
        float inset[4] = {0, 0, 0, 0};   // left, top, right, bottom: the camera's cut-out and the system's bars
    };
    struct Place {
        float x = 0, y = 0, r = 0;   // the centre and the radius, in pixels
        bool visible = false;
    };
    using Layout = std::array<Place, kTouchParts>;
    // Each control's place on a screen: out of the box, or where the player put it. `arranging`:
    // every control shows (Aim with any gun), for the arranging screen.
    static Layout lay_out(const Screen& screen, const TouchSettings& s, bool scope, bool arranging = false);

    // The arranging screen, drawn in the current (full-screen) VanGUI window: every control where it
    // is, dragged by the pointer (a finger, or a mouse) to wherever it should be; the one picked
    // sized by Smaller and Bigger; Reset puts it back, Reset all puts them all back. Writes into
    // `s.places`. Returns false once Done (or Back) is pressed.
    static bool arrange(TouchSettings& s, const Screen& screen, int& picked);

private:
    struct Finger {
        enum class Part { Stick, Look, Button } part = Part::Look;
        int id = 0;
        int button = -1;
        float x = 0, y = 0;       // where it was last frame
        float bx = 0, by = 0;     // the stick's centre
    };

    bool stick_zone(float x, float y, float W, float H) const;
    int button_at(float x, float y) const;
    void let_go();

    Layout place_{};
    std::array<bool, kTouchParts> held_{}, hit_{}, on_{};   // a finger on it; came down this frame; a toggle's state
    std::vector<Finger> fingers_;
    bool shown_ = false;
    bool moving_ = false, walking_ = false;
    float forward_ = 0, side_ = 0;
    float look_x_ = 0, look_y_ = 0;
    bool menu_pressed_ = false, chat_pressed_ = false;
    // The stick, for drawing: where its centre is (where the thumb landed), and the knob.
    float stick_x_ = 0, stick_y_ = 0, knob_x_ = 0, knob_y_ = 0;
    bool stick_live_ = false;
    float unit_ = 1;   // pixels per point of a 1080-tall screen, times the size setting
};

}  // namespace lsf
