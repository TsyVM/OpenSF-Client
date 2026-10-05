// VanGUI integration: context, the platform and graphics backends, fonts and resolution scaling.
#pragma once

#include "Engine/Platform/Window.hpp"
#include "Engine/Render/Device.hpp"

struct VanFont;
struct VanGuiStyle;

namespace eng {

struct UiFonts {
    VanFont* body = nullptr;      // regular text
    VanFont* bold = nullptr;      // emphasis, buttons
    VanFont* heading = nullptr;   // condensed display face for titles and HUD numbers
    VanFont* mono = nullptr;      // console, debug
    VanFont* display = nullptr;   // the heaviest weight: page titles, the main menu
    VanFont* page = nullptr;      // the original client's own face (Tahoma), for its lobby pages
    VanFont* page_bold = nullptr;
    VanFont* kit = nullptr;       // the lettering of the lobby kit's plates (the nearest Windows has to it)
};

class UiLayer {
public:
    // `apply_theme` rebuilds the style whenever the UI scale changes.
    bool init(Window& window, Device& device, void (*apply_theme)(VanGuiStyle&));
    void shutdown();

    // `scale` multiplies every style size and the font size.
    void begin_frame(float scale);
    void end_frame();
    // Pixels at the top of the window the toasts keep clear of this frame (a screen's top bar).
    void set_overlay_top(float pixels) { overlay_top_ = pixels; }
    // An automated run's pointer: where the test put it (nowhere until it does). The real mouse
    // never points for a test, even when its window ends up in front with the mouse over it.
    void set_test_pointer(float x, float y) { test_x_ = x, test_y_ = y; }

    float scale() const { return scale_; }
    const UiFonts& fonts() const { return fonts_; }
    bool wants_keyboard() const;
    bool wants_mouse() const;

private:
    Window* window_ = nullptr;
    Device* device_ = nullptr;
    void (*apply_theme_)(VanGuiStyle&) = nullptr;
    UiFonts fonts_;
    float scale_ = 0;
    float overlay_top_ = 0;
    float test_x_ = -3.0e38f, test_y_ = -3.0e38f;
    bool ready_ = false;
};

}  // namespace eng
