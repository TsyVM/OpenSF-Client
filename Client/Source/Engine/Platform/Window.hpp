// The game window: Win32 message pump, resize tracking, borderless fullscreen, raw
// mouse input and cursor capture for mouse-look. On Android (Window_android.cpp) it is the
// activity's full-screen surface: the app's events, the touch screen, keys and pads. On Linux
// (Window_linux.cpp) an X11 window, and on macOS (Window_mac.mm) a Cocoa one, each feeding VanGUI
// itself (ui_init / ui_new_frame) as Android does.
#pragma once

#include "Engine/Platform/Input.hpp"

#include <functional>
#include <string>
#include <vector>

namespace eng {

enum class DisplayMode { Windowed = 0, Borderless = 1 };

struct ScreenSize {
    int width = 0, height = 0;
    bool operator==(const ScreenSize&) const = default;
};

struct WindowDesc {
    std::wstring title = L"Game";
    int width = 1600;
    int height = 900;
    DisplayMode mode = DisplayMode::Windowed;
    // Automated runs: open without taking focus and ignore the real keyboard and mouse, so a
    // test never grabs the user's pointer and the user's typing never reaches the test.
    bool background = false;
};

class Window {
public:
    ~Window();

    bool create(const WindowDesc& desc);
    void destroy();

    // Processes pending messages. Returns false once the window was closed.
    bool pump();
    // Off the screen and the taskbar at once, the cursor released: what the player sees of
    // quitting, while the game still saves and closes behind it.
    void hide();

    void set_display_mode(DisplayMode mode, int width, int height);
    DisplayMode display_mode() const { return mode_; }
    void set_title(const std::wstring& title);

    // The screen the window is on, in real pixels: its size, the room a window has on it (less
    // the taskbar and a title bar), and the sizes its modes offer (smallest first, each once).
    ScreenSize screen() const;
    ScreenSize screen_room() const;
    std::vector<ScreenSize> screen_modes() const;
    // Windows only: the process sees every screen's real pixels instead of ones Windows scales
    // for it (a 2880x1800 panel at 125% is otherwise 2304x1440, drawn soft). Before any window.
    static void use_real_pixels();
    // The mouse pointer, in the window's pixels (false: not over it, or no pointer).
    bool pointer(float& x, float& y) const;
    // Moves it (a controller's stick working the menus). Never in an automated run.
    void set_pointer(float x, float y);

    // Mouse-look: confines the cursor and ignores absolute motion.
    void set_mouse_captured(bool captured);
    bool mouse_captured() const { return captured_; }

    // Whether Windows draws its own pointer over this window. One owner of ShowCursor's
    // counter, because it is a counter: the client hides it whenever the window has focus and
    // draws its own arrow instead, and a match hides it by capturing anyway.
    void show_os_cursor(bool show);

    void* hwnd() const { return hwnd_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool focused() const { return focused_; }
    bool minimized() const { return minimized_; }
    // An automated run's window: the real keyboard and mouse are not its.
    bool background() const { return background_; }
    // True once after the client area changed size.
    bool consume_resize();

    Input& input() { return input_; }

    // Given every message first (VanGUI). Return true to swallow the message.
    std::function<bool(void* hwnd, unsigned msg, u64 wparam, i64 lparam)> message_hook;

    // The touch screen (Android; none on Windows): each finger down, in pixels from the top left.
    struct Touch {
        int id = 0;
        float x = 0, y = 0;     // now
        float x0 = 0, y0 = 0;   // where it came down
        double start = 0;       // when (eng::time::now())
        bool pressed = false;   // came down this frame
    };
    const std::vector<Touch>& touches() const { return touches_; }
    // Fingers lifted this frame, where they were last.
    const std::vector<Touch>& lifted() const { return lifted_; }

#ifndef _WIN32
    // The display the window is on, for EGL (Linux: the X11 Display); null elsewhere.
    void* native_display() const { return display_; }
    // VanGUI's platform side (the first finger as its mouse, keys for text).
    bool ui_init();
    void ui_shutdown();
    void ui_new_frame();
    // The first finger works the mouse (the menus) unless the game's touch controls own the screen.
    bool touch_is_mouse = true;
    // The system's window came (non-null) or went (null): Android takes it away in the background.
    std::function<void(void* native_window)> on_native_window;
    // The app went into the background (true) or came back (false).
    std::function<void(bool)> on_background;
    // The system's own bars and the camera's cut-out, in pixels (left, top, right, bottom).
    int safe_inset[4] = {0, 0, 0, 0};

    // The platform's event handlers (the app glue's callbacks).
    void handle_command(int cmd);
    int handle_input(const void* event);
#endif

    static i64 dispatch(void* hwnd, unsigned msg, u64 wparam, i64 lparam);

private:
    i64 handle(unsigned msg, u64 wparam, i64 lparam);
    void apply_clip();
    // While the game has the mouse, Windows' Sticky / Toggle / Filter Keys shortcuts are off (Shift
    // five times is a crouch-jump, not a dialog that takes the focus); put back as they were after.
    void hold_accessibility_shortcuts(bool hold);
    bool shortcuts_held_ = false;

    void* hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool focused_ = true;
    bool minimized_ = false;
    bool resized_ = false;
    bool closed_ = false;
    bool captured_ = false;
    bool os_cursor_ = true;
    bool cursor_hidden_ = false;   // Windows: what ShowCursor's counter was last told
    bool background_ = false;
    DisplayMode mode_ = DisplayMode::Windowed;
    long windowed_rect_[4] = {0, 0, 0, 0};
    Input input_;
    std::vector<Touch> touches_, lifted_;
#ifndef _WIN32
    int mouse_finger_ = -1;   // the finger working the mouse
    bool ui_ready_ = false;
    void* display_ = nullptr;    // Linux: the X11 Display
    void* platform_ = nullptr;   // the platform file's own state
#endif
};

}  // namespace eng
