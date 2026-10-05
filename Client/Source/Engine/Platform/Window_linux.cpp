// The game window on a Linux PC: an X11 window (Wayland desktops run it through XWayland), its
// keyboard and mouse, borderless full screen (_NET_WM_STATE_FULLSCREEN), mouse-look (the pointer
// grabbed, hidden, and warped back to the middle after each move), and VanGUI's platform side.
//
// libX11 is opened at run time (dlopen) with the few declarations it needs written here, so the
// game builds on any machine (the release is cross-compiled) and says plainly what is missing on
// one without X.
#include "Engine/Platform/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Platform/DesktopKeys.hpp"
#include "Engine/Platform/Keys.hpp"
#include "Engine/Platform/System.hpp"

#include <vangui/vangui.h>

#include <dlfcn.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>
#include <type_traits>

namespace eng {

namespace {

// ── Xlib, as much as is used ───────────────────────────────────────────────────

using XID = unsigned long;
using XWindow = XID;
using XCursor = XID;
using XPixmap = XID;
using XAtom = unsigned long;
using XTime = unsigned long;
using XKeySym = XID;
struct XDisplay;

struct XKeyEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    XWindow window, root, subwindow;
    XTime time;
    int x, y, x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
};
struct XButtonEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    XWindow window, root, subwindow;
    XTime time;
    int x, y, x_root, y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
};
struct XMotionEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    XWindow window, root, subwindow;
    XTime time;
    int x, y, x_root, y_root;
    unsigned int state;
    char is_hint;
    int same_screen;
};
struct XConfigureEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    XWindow event, window;
    int x, y, width, height, border_width;
    XWindow above;
    int override_redirect;
};
struct XClientMessageEvent {
    int type;
    unsigned long serial;
    int send_event;
    XDisplay* display;
    XWindow window;
    XAtom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
};
union XEvent {
    int type;
    XKeyEvent key;
    XButtonEvent button;
    XMotionEvent motion;
    XConfigureEvent configure;
    XClientMessageEvent client;
    long pad[24];
};
struct XColor {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags, pad;
};

enum : int { KeyPress = 2, KeyRelease = 3, ButtonPress = 4, ButtonRelease = 5, MotionNotify = 6, FocusIn = 9, FocusOut = 10,
             UnmapNotify = 18, MapNotify = 19, ConfigureNotify = 22, ClientMessage = 33 };
enum : long { KeyPressMask = 1L << 0, KeyReleaseMask = 1L << 1, ButtonPressMask = 1L << 2, ButtonReleaseMask = 1L << 3,
              PointerMotionMask = 1L << 6, ExposureMask = 1L << 15, StructureNotifyMask = 1L << 17, SubstructureNotifyMask = 1L << 19,
              SubstructureRedirectMask = 1L << 20, FocusChangeMask = 1L << 21 };
enum : unsigned { ShiftMask = 1u << 0, LockMask = 1u << 1, ControlMask = 1u << 2, Mod1Mask = 1u << 3, Mod4Mask = 1u << 6 };
constexpr int GrabModeAsync = 1;
constexpr XTime CurrentTime = 0;

struct Xlib {
    void* so = nullptr;
    XDisplay* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(XDisplay*) = nullptr;
    int (*DefaultScreen)(XDisplay*) = nullptr;
    XWindow (*RootWindow)(XDisplay*, int) = nullptr;
    int (*DisplayWidth)(XDisplay*, int) = nullptr;
    int (*DisplayHeight)(XDisplay*, int) = nullptr;
    unsigned long (*BlackPixel)(XDisplay*, int) = nullptr;
    XWindow (*CreateSimpleWindow)(XDisplay*, XWindow, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long) = nullptr;
    int (*DestroyWindow)(XDisplay*, XWindow) = nullptr;
    int (*SelectInput)(XDisplay*, XWindow, long) = nullptr;
    int (*MapRaised)(XDisplay*, XWindow) = nullptr;
    int (*UnmapWindow)(XDisplay*, XWindow) = nullptr;
    int (*StoreName)(XDisplay*, XWindow, const char*) = nullptr;
    XAtom (*InternAtom)(XDisplay*, const char*, int) = nullptr;
    int (*SetWMProtocols)(XDisplay*, XWindow, XAtom*, int) = nullptr;
    int (*ChangeProperty)(XDisplay*, XWindow, XAtom, XAtom, int, int, const unsigned char*, int) = nullptr;
    int (*Pending)(XDisplay*) = nullptr;
    int (*NextEvent)(XDisplay*, XEvent*) = nullptr;
    int (*LookupString)(XKeyEvent*, char*, int, XKeySym*, void*) = nullptr;
    XKeySym (*LookupKeysym)(XKeyEvent*, int) = nullptr;
    int (*SendEvent)(XDisplay*, XWindow, int, long, XEvent*) = nullptr;
    int (*GrabPointer)(XDisplay*, XWindow, int, unsigned, int, int, XWindow, XCursor, XTime) = nullptr;
    int (*UngrabPointer)(XDisplay*, XTime) = nullptr;
    int (*WarpPointer)(XDisplay*, XWindow, XWindow, int, int, unsigned, unsigned, int, int) = nullptr;
    XPixmap (*CreateBitmapFromData)(XDisplay*, XID, const char*, unsigned, unsigned) = nullptr;
    XCursor (*CreatePixmapCursor)(XDisplay*, XPixmap, XPixmap, XColor*, XColor*, unsigned, unsigned) = nullptr;
    int (*FreePixmap)(XDisplay*, XPixmap) = nullptr;
    int (*FreeCursor)(XDisplay*, XCursor) = nullptr;
    int (*DefineCursor)(XDisplay*, XWindow, XCursor) = nullptr;
    int (*UndefineCursor)(XDisplay*, XWindow) = nullptr;
    int (*Flush)(XDisplay*) = nullptr;
    int (*ResizeWindow)(XDisplay*, XWindow, unsigned, unsigned) = nullptr;
    int (*XkbSetDetectableAutoRepeat)(XDisplay*, int, int*) = nullptr;
    int (*QueryPointer)(XDisplay*, XWindow, XWindow*, XWindow*, int*, int*, int*, int*, unsigned*) = nullptr;

    bool load() {
        if (so) return true;
        so = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!so) so = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
        if (!so) return false;
        bool ok = true;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(so, name));
            if (!fn) {
                LOG_ERROR("X11: %s is missing from libX11", name);
                ok = false;
            }
        };
        get(OpenDisplay, "XOpenDisplay");
        get(CloseDisplay, "XCloseDisplay");
        get(DefaultScreen, "XDefaultScreen");
        get(RootWindow, "XRootWindow");
        get(DisplayWidth, "XDisplayWidth");
        get(DisplayHeight, "XDisplayHeight");
        get(BlackPixel, "XBlackPixel");
        get(CreateSimpleWindow, "XCreateSimpleWindow");
        get(DestroyWindow, "XDestroyWindow");
        get(SelectInput, "XSelectInput");
        get(MapRaised, "XMapRaised");
        get(UnmapWindow, "XUnmapWindow");
        get(StoreName, "XStoreName");
        get(InternAtom, "XInternAtom");
        get(SetWMProtocols, "XSetWMProtocols");
        get(ChangeProperty, "XChangeProperty");
        get(Pending, "XPending");
        get(NextEvent, "XNextEvent");
        get(LookupString, "XLookupString");
        get(LookupKeysym, "XLookupKeysym");
        get(SendEvent, "XSendEvent");
        get(GrabPointer, "XGrabPointer");
        get(UngrabPointer, "XUngrabPointer");
        get(WarpPointer, "XWarpPointer");
        get(CreateBitmapFromData, "XCreateBitmapFromData");
        get(CreatePixmapCursor, "XCreatePixmapCursor");
        get(FreePixmap, "XFreePixmap");
        get(FreeCursor, "XFreeCursor");
        get(DefineCursor, "XDefineCursor");
        get(UndefineCursor, "XUndefineCursor");
        get(Flush, "XFlush");
        get(ResizeWindow, "XResizeWindow");
        get(QueryPointer, "XQueryPointer");
        // Optional: without it a held key repeats as release-press pairs, which the game tolerates.
        XkbSetDetectableAutoRepeat = reinterpret_cast<decltype(XkbSetDetectableAutoRepeat)>(dlsym(so, "XkbSetDetectableAutoRepeat"));
        return ok;
    }
} g_x;

// The window's own X state.
struct LinuxState {
    XWindow window = 0;
    XWindow root = 0;
    int screen = 0;
    XAtom wm_delete = 0, wm_protocols = 0, net_wm_state = 0, net_wm_fullscreen = 0, net_wm_name = 0, utf8 = 0;
    XCursor blank = 0;
    bool grabbed = false;
    bool cursor_hidden = false;
    int mouse_x = -1, mouse_y = -1;   // the pointer, in the window's pixels
    bool mouse_in = false;
};

LinuxState& state_of(void* p) { return *static_cast<LinuxState*>(p); }
XDisplay* display_of(void* d) { return static_cast<XDisplay*>(d); }

// X keysyms as the game's (Windows') virtual keys.
u32 vk_of(XKeySym k) {
    if (k >= 'a' && k <= 'z') return u32('A' + (k - 'a'));
    if (k >= 'A' && k <= 'Z') return u32(k);
    if (k >= '0' && k <= '9') return u32(k);
    if (k >= 0xFFBE && k <= 0xFFC9) return u32(VK_F1 + (k - 0xFFBE));          // F1..F12
    if (k >= 0xFFB0 && k <= 0xFFB9) return u32(VK_NUMPAD0 + (k - 0xFFB0));      // KP_0..KP_9
    switch (k) {
        case 0xFF08: return VK_BACK;
        case 0xFF09: case 0xFE20: return VK_TAB;                                // Tab, ISO_Left_Tab (Shift+Tab)
        case 0xFF0D: case 0xFF8D: return VK_RETURN;                             // Return, KP_Enter
        case 0xFF13: return kVkPause;
        case 0xFF14: return kVkScroll;
        case 0xFF1B: return VK_ESCAPE;
        case 0xFF50: case 0xFF95: return VK_HOME;
        case 0xFF51: case 0xFF96: return VK_LEFT;
        case 0xFF52: case 0xFF97: return VK_UP;
        case 0xFF53: case 0xFF98: return VK_RIGHT;
        case 0xFF54: case 0xFF99: return VK_DOWN;
        case 0xFF55: case 0xFF9A: return VK_PRIOR;
        case 0xFF56: case 0xFF9B: return VK_NEXT;
        case 0xFF57: case 0xFF9C: return VK_END;
        case 0xFF61: return kVkSnapshot;
        case 0xFF63: case 0xFF9E: return VK_INSERT;
        case 0xFF67: return kVkApps;
        case 0xFF7F: return kVkNumLock;
        case 0xFFFF: case 0xFF9F: return VK_DELETE;
        case 0xFFAA: return VK_MULTIPLY;
        case 0xFFAB: return VK_ADD;
        case 0xFFAD: return VK_SUBTRACT;
        case 0xFFAE: return VK_DECIMAL;
        case 0xFFAF: return VK_DIVIDE;
        case 0xFFE1: return VK_LSHIFT;
        case 0xFFE2: return VK_RSHIFT;
        case 0xFFE3: return VK_LCONTROL;
        case 0xFFE4: return VK_RCONTROL;
        case 0xFFE5: return VK_CAPITAL;
        case 0xFFE9: case 0xFFE7: return VK_LMENU;                              // Alt_L, Meta_L
        case 0xFFEA: case 0xFE03: case 0xFFE8: return VK_RMENU;                 // Alt_R, AltGr, Meta_R
        case 0xFFEB: return kVkLWin;
        case 0xFFEC: return kVkRWin;
        case 0x0020: return VK_SPACE;
        case 0x0027: return VK_OEM_7;
        case 0x002C: return VK_OEM_COMMA;
        case 0x002D: return VK_OEM_MINUS;
        case 0x002E: return VK_OEM_PERIOD;
        case 0x002F: return VK_OEM_2;
        case 0x003B: return VK_OEM_1;
        case 0x003D: return VK_OEM_PLUS;
        case 0x005B: return VK_OEM_4;
        case 0x005C: return VK_OEM_5;
        case 0x005D: return VK_OEM_6;
        case 0x0060: return VK_OEM_3;
        default: return 0;
    }
}

// A keysym's character as UTF-8 (Latin-1 keysyms are their code point; 0x01000000 + U is U).
std::string utf8_of(XKeySym k) {
    unsigned long u = 0;
    if ((k >= 0x20 && k <= 0x7E) || (k >= 0xA0 && k <= 0xFF)) u = k;
    else if ((k & 0xFF000000UL) == 0x01000000UL) u = k & 0x00FFFFFFUL;
    if (u < 0x20 || u == 0x7F || u > 0x10FFFF) return {};
    std::string out;
    if (u < 0x80) out += char(u);
    else if (u < 0x800) out += char(0xC0 | (u >> 6)), out += char(0x80 | (u & 0x3F));
    else if (u < 0x10000) out += char(0xE0 | (u >> 12)), out += char(0x80 | ((u >> 6) & 0x3F)), out += char(0x80 | (u & 0x3F));
    else out += char(0xF0 | (u >> 18)), out += char(0x80 | ((u >> 12) & 0x3F)), out += char(0x80 | ((u >> 6) & 0x3F)), out += char(0x80 | (u & 0x3F));
    return out;
}

void feed_modifiers(unsigned state) {
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(VanGuiMod_Ctrl, (state & ControlMask) != 0);
    io.AddKeyEvent(VanGuiMod_Shift, (state & ShiftMask) != 0);
    io.AddKeyEvent(VanGuiMod_Alt, (state & Mod1Mask) != 0);
    io.AddKeyEvent(VanGuiMod_Super, (state & Mod4Mask) != 0);
}

}  // namespace

// ── Typing: a desktop has a keyboard; the game's own fields take it ────────────

namespace platform {
bool text_input_native() { return false; }
void start_typing(const std::string&, int, bool) {}
void stop_typing() {}
bool typed_text(std::string&) { return false; }
bool text_answer(std::string&, bool&) { return false; }
}  // namespace platform

Window::~Window() { destroy(); }

bool Window::create(const WindowDesc& desc) {
    if (!g_x.load()) {
        platform::fatal_message("Soldier Front Legacy", "This system has no X11 library (libX11.so.6): the game needs an X11 or XWayland desktop.");
        return false;
    }
    XDisplay* d = g_x.OpenDisplay(nullptr);
    if (!d) {
        platform::fatal_message("Soldier Front Legacy", "The game could not open the display (is DISPLAY set? On Wayland, XWayland must be on).");
        return false;
    }
    auto* st = new LinuxState;
    platform_ = st;
    display_ = d;
    background_ = desc.background;
    st->screen = g_x.DefaultScreen(d);
    st->root = g_x.RootWindow(d, st->screen);
    width_ = std::max(320, desc.width);
    height_ = std::max(200, desc.height);
    st->window = g_x.CreateSimpleWindow(d, st->root, 0, 0, unsigned(width_), unsigned(height_), 0, g_x.BlackPixel(d, st->screen), g_x.BlackPixel(d, st->screen));
    if (!st->window) {
        LOG_ERROR("X11: the window could not be made");
        return false;
    }
    hwnd_ = reinterpret_cast<void*>(st->window);
    g_x.SelectInput(d, st->window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask |
                                       FocusChangeMask | ExposureMask);
    st->wm_protocols = g_x.InternAtom(d, "WM_PROTOCOLS", 0);
    st->wm_delete = g_x.InternAtom(d, "WM_DELETE_WINDOW", 0);
    st->net_wm_state = g_x.InternAtom(d, "_NET_WM_STATE", 0);
    st->net_wm_fullscreen = g_x.InternAtom(d, "_NET_WM_STATE_FULLSCREEN", 0);
    st->net_wm_name = g_x.InternAtom(d, "_NET_WM_NAME", 0);
    st->utf8 = g_x.InternAtom(d, "UTF8_STRING", 0);
    g_x.SetWMProtocols(d, st->window, &st->wm_delete, 1);
    if (g_x.XkbSetDetectableAutoRepeat) g_x.XkbSetDetectableAutoRepeat(d, 1, nullptr);
    // The pointer a match hides: a cursor with no pixels.
    static const char empty[8] = {0};
    if (XPixmap bits = g_x.CreateBitmapFromData(d, st->window, empty, 8, 8)) {
        XColor black{};
        st->blank = g_x.CreatePixmapCursor(d, bits, bits, &black, &black, 0, 0);
        g_x.FreePixmap(d, bits);
    }
    set_title(desc.title);
    g_x.MapRaised(d, st->window);
    mode_ = DisplayMode::Windowed;
    if (desc.mode == DisplayMode::Borderless) set_display_mode(DisplayMode::Borderless, 0, 0);
    g_x.Flush(d);
    focused_ = !background_;
    LOG_INFO("Window: X11, %dx%d", width_, height_);
    return true;
}

void Window::destroy() {
    if (!platform_) return;
    LinuxState& st = state_of(platform_);
    XDisplay* d = display_of(display_);
    if (d) {
        if (st.grabbed) g_x.UngrabPointer(d, CurrentTime);
        if (st.blank) g_x.FreeCursor(d, st.blank);
        if (st.window) g_x.DestroyWindow(d, st.window);
        g_x.CloseDisplay(d);
    }
    delete &st;
    platform_ = nullptr;
    display_ = nullptr;
    hwnd_ = nullptr;
}

bool Window::pump() {
    if (!platform_ || !display_) return false;
    LinuxState& st = state_of(platform_);
    XDisplay* d = display_of(display_);
    VanGuiIO* io = ui_ready_ ? &VanGui::GetIO() : nullptr;
    const int cx = width_ / 2, cy = height_ / 2;
    while (g_x.Pending(d) > 0) {
        XEvent e{};
        g_x.NextEvent(d, &e);
        if (background_ && (e.type == KeyPress || e.type == KeyRelease || e.type == ButtonPress || e.type == ButtonRelease || e.type == MotionNotify))
            continue;   // an automated run: the real keyboard and mouse never reach the game
        switch (e.type) {
            case ClientMessage:
                if (XAtom(e.client.data.l[0]) == st.wm_delete) closed_ = true;
                break;
            case ConfigureNotify:
                if (e.configure.width > 0 && e.configure.height > 0 && (e.configure.width != width_ || e.configure.height != height_)) {
                    width_ = e.configure.width;
                    height_ = e.configure.height;
                    resized_ = true;
                }
                break;
            case MapNotify: minimized_ = false; break;
            case UnmapNotify: minimized_ = true; break;
            case FocusIn:
                focused_ = true;
                if (io) io->AddFocusEvent(true);
                apply_clip();
                break;
            case FocusOut:
                focused_ = false;
                input_.on_focus_lost();
                if (io) io->AddFocusEvent(false);
                apply_clip();
                break;
            case KeyPress:
            case KeyRelease: {
                const bool down = e.type == KeyPress;
                char text[32] = {};
                XKeySym sym = 0;
                const int n = g_x.LookupString(&e.key, text, int(sizeof(text) - 1), &sym, nullptr);
                // The key by its place (unshifted), so Shift+1 is still the key "1".
                const XKeySym base = g_x.LookupKeysym(&e.key, 0);
                const u32 vk = vk_of(base ? base : sym);
                if (vk) {
                    input_.on_key(vk, down);
                    if (vk == VK_LSHIFT || vk == VK_RSHIFT) input_.on_key(VK_SHIFT, down);
                    if (vk == VK_LCONTROL || vk == VK_RCONTROL) input_.on_key(VK_CONTROL, down);
                    if (vk == VK_LMENU || vk == VK_RMENU) input_.on_key(VK_MENU, down);
                }
                if (io) {
                    feed_modifiers(e.key.state);
                    if (const VanGuiKey k = vangui_key_of(vk); k != VanGuiKey_None) io->AddKeyEvent(k, down);
                    if (down && !(e.key.state & ControlMask)) {
                        std::string typed = utf8_of(sym);
                        if (typed.empty() && n > 0 && u8(text[0]) >= 0x20 && text[0] != 0x7F) typed.assign(text, size_t(n));
                        if (!typed.empty()) io->AddInputCharactersUTF8(typed.c_str());
                    }
                }
                break;
            }
            case ButtonPress:
            case ButtonRelease: {
                const bool down = e.type == ButtonPress;
                const unsigned b = e.button.button;
                if (b == 4 || b == 5) {
                    if (down) {
                        input_.on_wheel(b == 4 ? 1.0f : -1.0f);
                        if (io) io->AddMouseWheelEvent(0.0f, b == 4 ? 1.0f : -1.0f);
                    }
                    break;
                }
                if (b == 6 || b == 7) {
                    if (down && io) io->AddMouseWheelEvent(b == 6 ? 1.0f : -1.0f, 0.0f);
                    break;
                }
                const int button = b == 1 ? kMouseLeft : b == 3 ? kMouseRight : b == 2 ? kMouseMiddle : b == 8 ? kMouse4 : b == 9 ? kMouse5 : -1;
                if (button < 0) break;
                input_.on_mouse_button(button, down);
                if (io && button <= kMouseMiddle) io->AddMouseButtonEvent(button == kMouseLeft ? 0 : button == kMouseRight ? 1 : 2, down);
                break;
            }
            case MotionNotify: {
                st.mouse_x = e.motion.x, st.mouse_y = e.motion.y;
                st.mouse_in = true;
                if (st.grabbed) {
                    // Mouse-look: each move from the middle is the mouse's own motion; the pointer
                    // is put back there (that move comes back too, from nowhere: it is skipped).
                    const int dx = e.motion.x - cx, dy = e.motion.y - cy;
                    if (dx != 0 || dy != 0) {
                        input_.on_raw_mouse(dx, dy);
                        g_x.WarpPointer(d, 0, st.window, 0, 0, 0, 0, cx, cy);
                    }
                } else if (io) {
                    io->AddMousePosEvent(float(e.motion.x), float(e.motion.y));
                }
                break;
            }
            default: break;
        }
    }
    return !closed_;
}

bool Window::ui_init() {
    VanGuiIO& io = VanGui::GetIO();
    io.BackendPlatformName = "legacysf_x11";
    ui_ready_ = true;
    return true;
}

void Window::ui_shutdown() { ui_ready_ = false; }

void Window::ui_new_frame() {
    if (!ui_ready_) return;
    VanGuiIO& io = VanGui::GetIO();
    io.DisplaySize = VanVec2(float(std::max(1, width_)), float(std::max(1, height_)));
    io.DisplayFramebufferScale = VanVec2(1, 1);
    static double last = time::now();
    const double now = time::now();
    io.DeltaTime = float(std::clamp(now - last, 1.0e-4, 0.25));
    last = now;
    if (platform_ && state_of(platform_).grabbed) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

bool Window::consume_resize() {
    const bool r = resized_;
    resized_ = false;
    return r;
}

void Window::set_display_mode(DisplayMode mode, int width, int height) {
    if (!platform_) return;
    LinuxState& st = state_of(platform_);
    XDisplay* d = display_of(display_);
    const bool full = mode == DisplayMode::Borderless;
    if (!full && width > 0 && height > 0) g_x.ResizeWindow(d, st.window, unsigned(width), unsigned(height));
    if (full != (mode_ == DisplayMode::Borderless)) {
        // _NET_WM_STATE: 1 add, 0 remove; the window manager makes it the whole screen.
        XEvent e{};
        e.client.type = ClientMessage;
        e.client.window = st.window;
        e.client.message_type = st.net_wm_state;
        e.client.format = 32;
        e.client.data.l[0] = full ? 1 : 0;
        e.client.data.l[1] = long(st.net_wm_fullscreen);
        e.client.data.l[2] = 0;
        e.client.data.l[3] = 1;
        g_x.SendEvent(d, st.root, 0, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    }
    mode_ = mode;
    g_x.Flush(d);
}

void Window::set_title(const std::wstring& title) {
    if (!platform_) return;
    LinuxState& st = state_of(platform_);
    XDisplay* d = display_of(display_);
    const std::string t = str::narrow(title);
    g_x.StoreName(d, st.window, t.c_str());
    g_x.ChangeProperty(d, st.window, st.net_wm_name, st.utf8, 8, 0, reinterpret_cast<const unsigned char*>(t.data()), int(t.size()));
}

ScreenSize Window::screen() const {
    if (!display_ || !platform_) return {width_, height_};
    const LinuxState& st = *static_cast<const LinuxState*>(platform_);
    return {g_x.DisplayWidth(display_of(display_), st.screen), g_x.DisplayHeight(display_of(display_), st.screen)};
}

ScreenSize Window::screen_room() const {
    const ScreenSize s = screen();
    return {s.width, std::max(200, s.height - 64)};   // a panel and a title bar, roughly
}

std::vector<ScreenSize> Window::screen_modes() const {
    // The common 16:9, 16:10 and 4:3 sizes that fit the screen (no mode is ever switched: the
    // picture is drawn at the size picked and the compositor fits it).
    const ScreenSize s = screen();
    static const ScreenSize all[] = {{1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1366, 768}, {1440, 900}, {1600, 900},
                                     {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160}};
    std::vector<ScreenSize> out;
    for (const ScreenSize& m : all)
        if (m.width <= s.width && m.height <= s.height) out.push_back(m);
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    return out;
}

void Window::use_real_pixels() {}

bool Window::pointer(float& x, float& y) const {
    if (!platform_) return false;
    const LinuxState& st = *static_cast<const LinuxState*>(platform_);
    if (!st.mouse_in || st.mouse_x < 0) return false;
    x = float(st.mouse_x), y = float(st.mouse_y);
    return st.mouse_x < width_ && st.mouse_y < height_;
}

void Window::set_pointer(float x, float y) {
    if (!platform_ || background_ || !focused_) return;
    LinuxState& st = state_of(platform_);
    g_x.WarpPointer(display_of(display_), 0, st.window, 0, 0, 0, 0, int(std::clamp(x, 0.0f, float(width_ - 1))), int(std::clamp(y, 0.0f, float(height_ - 1))));
}

void Window::set_mouse_captured(bool captured) {
    if (captured == captured_) return;
    captured_ = captured;
    apply_clip();
}

void Window::hide() {
    if (!platform_) return;
    set_mouse_captured(false);
    g_x.UnmapWindow(display_of(display_), state_of(platform_).window);
    g_x.Flush(display_of(display_));
}

void Window::show_os_cursor(bool show) {
    if (show == os_cursor_) return;
    os_cursor_ = show;
    apply_clip();
}

i64 Window::dispatch(void*, unsigned, u64, i64) { return 0; }
i64 Window::handle(unsigned, u64, i64) { return 0; }
void Window::hold_accessibility_shortcuts(bool) {}

// Mouse-look holds the pointer (grabbed into the window, hidden, kept in the middle) only while the
// window has the focus: another window taking it (a screenshot tool, the panel) gets it back.
void Window::apply_clip() {
    if (!platform_) return;
    LinuxState& st = state_of(platform_);
    XDisplay* d = display_of(display_);
    const bool hold = captured_ && focused_ && !background_;
    if (hold && !st.grabbed) {
        st.grabbed = g_x.GrabPointer(d, st.window, 1, unsigned(ButtonPressMask | ButtonReleaseMask | PointerMotionMask), GrabModeAsync, GrabModeAsync,
                                     st.window, st.blank, CurrentTime) == 0;
        if (st.grabbed) g_x.WarpPointer(d, 0, st.window, 0, 0, 0, 0, width_ / 2, height_ / 2);
    } else if (!hold && st.grabbed) {
        g_x.UngrabPointer(d, CurrentTime);
        st.grabbed = false;
    }
    // The game draws its own arrow over its window, so the system's is hidden there too.
    const bool hide_cursor = hold || (!os_cursor_ && focused_);
    if (hide_cursor != st.cursor_hidden && st.blank) {
        if (hide_cursor) g_x.DefineCursor(d, st.window, st.blank);
        else g_x.UndefineCursor(d, st.window);
        st.cursor_hidden = hide_cursor;
    }
    g_x.Flush(d);
}

}  // namespace eng
