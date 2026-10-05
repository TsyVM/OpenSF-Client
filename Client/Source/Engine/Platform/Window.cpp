#include "Engine/Platform/Window.hpp"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>

namespace eng {

namespace {

const wchar_t* kClassName = L"SpringfieldEngineWindow";

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return LRESULT(Window::dispatch(hwnd, msg, u64(wp), i64(lp)));
}

}  // namespace

Window::~Window() { destroy(); }

bool Window::create(const WindowDesc& desc) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r{0, 0, desc.width, desc.height};
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(0, kClassName, desc.title.c_str(), style, std::max(0, x), std::max(0, y), w, h,
                                nullptr, nullptr, instance, this);
    if (!hwnd) return false;
    hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));

    background_ = desc.background;
    if (!background_) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;   // mouse
        rid.hwndTarget = hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }

    RECT client;
    GetClientRect(hwnd, &client);
    width_ = client.right - client.left;
    height_ = client.bottom - client.top;
    ShowWindow(hwnd, background_ ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(hwnd);
    if (desc.mode != DisplayMode::Windowed) set_display_mode(desc.mode, desc.width, desc.height);
    return true;
}

void Window::destroy() {
    if (hwnd_) {
        set_mouse_captured(false);
        hold_accessibility_shortcuts(false);
        show_os_cursor(true);   // leave the desktop's pointer as we found it
        DestroyWindow(HWND(hwnd_));
        hwnd_ = nullptr;
    }
}

void Window::hide() {
    if (!hwnd_) return;
    set_mouse_captured(false);
    ClipCursor(nullptr);
    ShowWindow(HWND(hwnd_), SW_HIDE);
}

bool Window::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) closed_ = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !closed_;
}

bool Window::consume_resize() {
    bool r = resized_;
    resized_ = false;
    return r;
}

void Window::set_title(const std::wstring& title) { SetWindowTextW(HWND(hwnd_), title.c_str()); }

void Window::set_display_mode(DisplayMode mode, int width, int height) {
    HWND hwnd = HWND(hwnd_);
    if (mode == DisplayMode::Borderless) {
        if (mode_ == DisplayMode::Windowed) {
            RECT r;
            GetWindowRect(hwnd, &r);
            windowed_rect_[0] = r.left;
            windowed_rect_[1] = r.top;
            windowed_rect_[2] = r.right;
            windowed_rect_[3] = r.bottom;
        }
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        RECT r{0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;
        int x = windowed_rect_[2] ? windowed_rect_[0] : (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        int y = windowed_rect_[2] ? windowed_rect_[1] : (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        SetWindowPos(hwnd, HWND_NOTOPMOST, std::max(0, x), std::max(0, y), w, h, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    mode_ = mode;
    RECT client;
    GetClientRect(hwnd, &client);
    width_ = client.right - client.left;
    height_ = client.bottom - client.top;
    resized_ = true;
    apply_clip();
}

void Window::use_real_pixels() {
    // Per-monitor awareness (Windows 10 1703 on); the older call for anything before it.
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(HANDLE);
        if (auto set = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext")))) {
            if (set(reinterpret_cast<HANDLE>(LONG_PTR(-4)))) return;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            if (set(reinterpret_cast<HANDLE>(LONG_PTR(-3)))) return;   // ..._PER_MONITOR_AWARE
        }
    }
    SetProcessDPIAware();
}

namespace {

bool monitor_info(void* hwnd, MONITORINFOEXW& mi) {
    HMONITOR mon = hwnd ? MonitorFromWindow(HWND(hwnd), MONITOR_DEFAULTTONEAREST) : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    mi = {};
    mi.cbSize = sizeof(mi);
    return mon && GetMonitorInfoW(mon, &mi);
}

}  // namespace

ScreenSize Window::screen() const {
    MONITORINFOEXW mi;
    if (!monitor_info(hwnd_, mi)) return {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return {int(mi.rcMonitor.right - mi.rcMonitor.left), int(mi.rcMonitor.bottom - mi.rcMonitor.top)};
}

ScreenSize Window::screen_room() const {
    MONITORINFOEXW mi;
    if (!monitor_info(hwnd_, mi)) return screen();
    // The work area, less what a window's own frame and title bar take.
    RECT frame{0, 0, 0, 0};
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    return {int(mi.rcWork.right - mi.rcWork.left) - int(frame.right - frame.left), int(mi.rcWork.bottom - mi.rcWork.top) - int(frame.bottom - frame.top)};
}

std::vector<ScreenSize> Window::screen_modes() const {
    std::vector<ScreenSize> out;
    MONITORINFOEXW mi;
    if (!monitor_info(hwnd_, mi)) return out;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); ++i) {
        const ScreenSize s{int(dm.dmPelsWidth), int(dm.dmPelsHeight)};
        if (s.width < s.height) continue;   // a mode for the screen stood on end
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const ScreenSize& a, const ScreenSize& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });
    return out;
}

bool Window::pointer(float& x, float& y) const {
    POINT p;
    if (!hwnd_ || !GetCursorPos(&p) || !ScreenToClient(HWND(hwnd_), &p)) return false;
    x = float(p.x), y = float(p.y);
    return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
}

void Window::set_pointer(float x, float y) {
    if (!hwnd_ || background_ || !focused_) return;
    POINT p{LONG(std::clamp(x, 0.0f, float(std::max(1, width_) - 1))), LONG(std::clamp(y, 0.0f, float(std::max(1, height_) - 1)))};
    if (ClientToScreen(HWND(hwnd_), &p)) SetCursorPos(p.x, p.y);
}

void Window::set_mouse_captured(bool captured) {
    if (captured == captured_) return;
    captured_ = captured;
    apply_clip();
}

void Window::show_os_cursor(bool show) {
    if (show == os_cursor_) return;
    os_cursor_ = show;
    apply_clip();
}

namespace {

// The shortcuts as they were when the game started (Microsoft's "Disabling Shortcut Keys in Games").
STICKYKEYS g_sticky{sizeof(STICKYKEYS), 0};
TOGGLEKEYS g_toggle{sizeof(TOGGLEKEYS), 0};
FILTERKEYS g_filter{sizeof(FILTERKEYS), 0};
bool g_shortcuts_saved = false;

}  // namespace

void Window::hold_accessibility_shortcuts(bool hold) {
    if (hold == shortcuts_held_) return;
    if (!g_shortcuts_saved) {
        SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(g_sticky), &g_sticky, 0);
        SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof(g_toggle), &g_toggle, 0);
        SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof(g_filter), &g_filter, 0);
        g_shortcuts_saved = true;
    }
    shortcuts_held_ = hold;
    // Only the shortcut is touched, and only for a feature that is off: someone who uses Sticky
    // Keys keeps them. Nothing is written to the user's profile (no SPIF_UPDATEINIFILE).
    if (hold) {
        STICKYKEYS sk = g_sticky;
        if (!(sk.dwFlags & SKF_STICKYKEYSON)) {
            sk.dwFlags &= ~(SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY);
            SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(sk), &sk, 0);
        }
        TOGGLEKEYS tk = g_toggle;
        if (!(tk.dwFlags & TKF_TOGGLEKEYSON)) {
            tk.dwFlags &= ~(TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY);
            SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(tk), &tk, 0);
        }
        FILTERKEYS fk = g_filter;
        if (!(fk.dwFlags & FKF_FILTERKEYSON)) {
            fk.dwFlags &= ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
            SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(fk), &fk, 0);
        }
    } else {
        SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(g_sticky), &g_sticky, 0);
        SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof(g_toggle), &g_toggle, 0);
        SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof(g_filter), &g_filter, 0);
    }
}

void Window::apply_clip() {
    const bool hold = captured_ && focused_ && hwnd_ && !background_;
    hold_accessibility_shortcuts(hold);
    if (hold) {
        RECT r;
        GetClientRect(HWND(hwnd_), &r);
        POINT tl{r.left, r.top}, br{r.right, r.bottom};
        ClientToScreen(HWND(hwnd_), &tl);
        ClientToScreen(HWND(hwnd_), &br);
        // Confine to a small box in the middle so the cursor never reaches an edge.
        LONG cx = (tl.x + br.x) / 2, cy = (tl.y + br.y) / 2;
        RECT clip{cx - 4, cy - 4, cx + 4, cy + 4};
        ClipCursor(&clip);
        SetCursorPos(cx, cy);
    } else {
        ClipCursor(nullptr);
    }
    // Hidden by ShowCursor's counter, not SetCursor(nullptr): VanGUI's hook answers WM_SETCURSOR
    // before we see it, and sets its arrow again whenever the shape it wants changes, which left
    // the arrow sitting in the middle of a match. The counter is a counter, so this is the only
    // place that calls it and it only calls it on a change.
    const bool hide = hold || (!os_cursor_ && focused_ && hwnd_ && !background_);
    if (hide != cursor_hidden_) {
        cursor_hidden_ = hide;
        ShowCursor(hide ? FALSE : TRUE);
    }
}

i64 Window::dispatch(void* hwnd, unsigned msg, u64 wp, i64 lp) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(HWND(hwnd), GWLP_USERDATA));
    if (!self) return i64(DefWindowProcW(HWND(hwnd), msg, WPARAM(wp), LPARAM(lp)));
    return self->handle(msg, wp, lp);
}

i64 Window::handle(unsigned msg, u64 wp, i64 lp) {
    HWND hwnd = HWND(hwnd_);
    if (background_) {
        // Automated run: the real keyboard and mouse never reach the game.
        switch (msg) {
            case WM_INPUT: case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_MOUSEWHEEL: case WM_MOUSEMOVE:
                return i64(DefWindowProcW(HWND(hwnd_), msg, WPARAM(wp), LPARAM(lp)));
            case WM_ACTIVATE: case WM_KILLFOCUS: case WM_SETFOCUS:
                return i64(DefWindowProcW(HWND(hwnd_), msg, WPARAM(wp), LPARAM(lp)));
            default: break;
        }
    }
    if (message_hook && message_hook(hwnd_, msg, wp, lp)) return 1;

    switch (msg) {
        case WM_CLOSE:
            closed_ = true;
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SIZE: {
            minimized_ = wp == SIZE_MINIMIZED;
            int w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != width_ || h != height_)) {
                width_ = w;
                height_ = h;
                resized_ = true;
            }
            apply_clip();
            return 0;
        }
        case WM_ACTIVATE:
            focused_ = LOWORD(wp) != WA_INACTIVE;
            if (!focused_) input_.on_focus_lost();
            apply_clip();
            return 0;
        case WM_KILLFOCUS:
            focused_ = false;
            input_.on_focus_lost();
            apply_clip();
            return 0;
        case WM_SETFOCUS:
            focused_ = true;
            apply_clip();
            return 0;
        case WM_INPUT: {
            UINT size = 0;
            GetRawInputData(HRAWINPUT(lp), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            if (size > 0 && size <= 256) {
                BYTE buffer[256];
                if (GetRawInputData(HRAWINPUT(lp), RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == size) {
                    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
                    if (raw->header.dwType == RIM_TYPEMOUSE && !(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
                        input_.on_raw_mouse(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                }
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            u32 vk = u32(wp);
            input_.on_key(vk, down);
            bool extended = (lp >> 24) & 1;
            if (vk == VK_CONTROL) input_.on_key(extended ? VK_RCONTROL : VK_LCONTROL, down);
            if (vk == VK_MENU) input_.on_key(extended ? VK_RMENU : VK_LMENU, down);
            if (vk == VK_SHIFT) {
                UINT scan = (UINT(lp) >> 16) & 0xFF;
                u32 sided = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
                input_.on_key(sided ? sided : VK_LSHIFT, down);
            }
            if (msg == WM_SYSKEYDOWN && vk == VK_F4) closed_ = true;
            if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return 0;   // no menu beep on Alt
            return 0;
        }
        case WM_LBUTTONDOWN: input_.on_mouse_button(kMouseLeft, true); SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: input_.on_mouse_button(kMouseLeft, false); ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: input_.on_mouse_button(kMouseRight, true); return 0;
        case WM_RBUTTONUP: input_.on_mouse_button(kMouseRight, false); return 0;
        case WM_MBUTTONDOWN: input_.on_mouse_button(kMouseMiddle, true); return 0;
        case WM_MBUTTONUP: input_.on_mouse_button(kMouseMiddle, false); return 0;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            input_.on_mouse_button(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? kMouse4 : kMouse5, msg == WM_XBUTTONDOWN);
            return TRUE;
        case WM_MOUSEWHEEL:
            input_.on_wheel(float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA));
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 640;
            mmi->ptMinTrackSize.y = 480;
            return 0;
        }
        default:
            break;
    }
    return i64(DefWindowProcW(hwnd, msg, WPARAM(wp), LPARAM(lp)));
}

}  // namespace eng
