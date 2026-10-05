// The game window on Android: the activity's surface (android_native_app_glue), its lifecycle
// (the surface goes away in the background and a new one comes back), the touch screen, the
// keyboard, the Back key (Escape: pause, or back out of a menu) and game pads.
#include "Engine/Platform/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Platform/Android.hpp"
#include "Engine/Platform/Gamepad.hpp"
#include "Engine/Platform/Keys.hpp"
#include "Engine/Platform/System.hpp"

#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <vangui/vangui.h>
#include <vangui_impl_android.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>

namespace eng {

// ── The app ────────────────────────────────────────────────────────────────────

namespace android {

namespace {
android_app* g_app = nullptr;
JNIEnv* g_env = nullptr;
}  // namespace

void set_app(android_app* a) { g_app = a; }
android_app* app() { return g_app; }

_JNIEnv* jni() {
    if (!g_env && g_app) g_app->activity->vm->AttachCurrentThread(&g_env, nullptr);
    return g_env;
}

void call_activity(const char* method) {
    JNIEnv* env = jni();
    if (!env) return;
    jobject activity = g_app->activity->clazz;
    jclass c = env->GetObjectClass(activity);
    if (jmethodID m = env->GetMethodID(c, method, "()V")) env->CallVoidMethod(activity, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
}

}  // namespace android

// ── Typing (the phone's keyboard: GameActivity.startTyping) ─────────────────────

namespace {
std::mutex g_text_lock;
bool g_text_changed = false, g_text_ready = false, g_text_ok = false;
std::string g_text;
bool g_typing = false;    // a line is open to the keyboard (the native thread's view of it)

// GameActivity.typeKeys: a real keyboard's key, into the line (0 letters, 1 rub out, 2 Done).
void type_keys(const std::string& text, int action) {
    JNIEnv* env = android::jni();
    if (!env || !android::app()) return;
    jobject activity = android::app()->activity->clazz;
    jclass c = env->GetObjectClass(activity);
    if (jmethodID m = env->GetMethodID(c, "typeKeys", "(Ljava/lang/String;I)V")) {
        jstring t = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(activity, m, t, jint(action));
        env->DeleteLocalRef(t);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
}

std::string from_java(JNIEnv* env, jstring s) {
    std::string out;
    if (!s) return out;
    const char* c = env->GetStringUTFChars(s, nullptr);
    if (c) {
        out = c;
        env->ReleaseStringUTFChars(s, c);
    }
    return out;
}

// Called by Java on its UI thread.
void JNICALL text_changed(JNIEnv* env, jclass, jstring text) {
    std::string typed = from_java(env, text);
    std::lock_guard<std::mutex> lock(g_text_lock);
    g_text = std::move(typed);
    g_text_changed = true;
}

void JNICALL text_entered(JNIEnv* env, jclass, jstring text, jboolean ok) {
    std::string typed = from_java(env, text);
    std::lock_guard<std::mutex> lock(g_text_lock);
    g_text = std::move(typed);
    g_text_changed = true;
    g_text_ok = ok != JNI_FALSE;
    g_text_ready = true;
}

// GameActivity's native methods, registered by hand: NativeActivity loads this library itself, so
// Java's own lookup (System.loadLibrary's) never sees it.
bool register_text_natives(JNIEnv* env, jobject activity) {
    static bool done = false;
    if (done) return true;
    jclass c = env->GetObjectClass(activity);
    static const JNINativeMethod methods[] = {
        {"nativeTextChanged", "(Ljava/lang/String;)V", reinterpret_cast<void*>(&text_changed)},
        {"nativeTextEntered", "(Ljava/lang/String;Z)V", reinterpret_cast<void*>(&text_entered)},
    };
    done = env->RegisterNatives(c, methods, 2) == JNI_OK;
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
    if (!done) LOG_ERROR("Android: the typing callbacks could not be registered");
    return done;
}
}  // namespace

namespace platform {

bool text_input_native() { return true; }

void start_typing(const std::string& initial, int max_length, bool capitals) {
    JNIEnv* env = android::jni();
    if (!env || !android::app()) return;
    jobject activity = android::app()->activity->clazz;
    if (!register_text_natives(env, activity)) return;
    {
        std::lock_guard<std::mutex> lock(g_text_lock);
        g_text = initial;
        g_text_changed = g_text_ready = false;
    }
    g_typing = true;
    jclass c = env->GetObjectClass(activity);
    if (jmethodID m = env->GetMethodID(c, "startTyping", "(Ljava/lang/String;IZ)V")) {
        jstring i = env->NewStringUTF(initial.c_str());
        env->CallVoidMethod(activity, m, i, jint(max_length), jboolean(capitals ? JNI_TRUE : JNI_FALSE));
        env->DeleteLocalRef(i);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
}

void stop_typing() {
    g_typing = false;
    {
        std::lock_guard<std::mutex> lock(g_text_lock);
        g_text_changed = g_text_ready = false;
    }
    android::call_activity("stopTyping");
}

bool typed_text(std::string& text) {
    std::lock_guard<std::mutex> lock(g_text_lock);
    if (!g_text_changed) return false;
    g_text_changed = false;
    text = g_text;
    return true;
}

bool text_answer(std::string& text, bool& confirmed) {
    std::lock_guard<std::mutex> lock(g_text_lock);
    if (!g_text_ready) return false;
    g_text_ready = false;
    g_typing = false;
    text = g_text;
    confirmed = g_text_ok;
    return true;
}

}  // namespace platform

namespace {

Window* g_window = nullptr;

void on_command(android_app*, int32_t cmd) {
    if (g_window) g_window->handle_command(cmd);
}

int32_t on_input(android_app*, AInputEvent* e) { return g_window ? g_window->handle_input(e) : 0; }

// Handles the looper's events: all that are waiting (timeout 0), or waits for one (-1).
void process(android_app* app, int timeout_ms) {
    for (;;) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int r = ALooper_pollOnce(timeout_ms, nullptr, &events, reinterpret_cast<void**>(&source));
        if (r < 0) return;   // timed out, woken, or an error
        if (source) source->process(app, source);
        if (app->destroyRequested) return;
        timeout_ms = 0;
    }
}

// Android's key codes as the game's (Windows') virtual keys.
u32 vk_of(int32_t k) {
    if (k >= AKEYCODE_A && k <= AKEYCODE_Z) return u32('A' + (k - AKEYCODE_A));
    if (k >= AKEYCODE_0 && k <= AKEYCODE_9) return u32('0' + (k - AKEYCODE_0));
    if (k >= AKEYCODE_F1 && k <= AKEYCODE_F12) return u32(VK_F1 + (k - AKEYCODE_F1));
    if (k >= AKEYCODE_NUMPAD_0 && k <= AKEYCODE_NUMPAD_9) return u32(VK_NUMPAD0 + (k - AKEYCODE_NUMPAD_0));
    switch (k) {
        case AKEYCODE_DPAD_UP: return VK_UP;
        case AKEYCODE_DPAD_DOWN: return VK_DOWN;
        case AKEYCODE_DPAD_LEFT: return VK_LEFT;
        case AKEYCODE_DPAD_RIGHT: return VK_RIGHT;
        case AKEYCODE_DPAD_CENTER:
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: return VK_RETURN;
        case AKEYCODE_SPACE: return VK_SPACE;
        case AKEYCODE_ESCAPE: return VK_ESCAPE;
        case AKEYCODE_DEL: return VK_BACK;
        case AKEYCODE_FORWARD_DEL: return VK_DELETE;
        case AKEYCODE_TAB: return VK_TAB;
        case AKEYCODE_SHIFT_LEFT: return VK_LSHIFT;
        case AKEYCODE_SHIFT_RIGHT: return VK_RSHIFT;
        case AKEYCODE_CTRL_LEFT: return VK_LCONTROL;
        case AKEYCODE_CTRL_RIGHT: return VK_RCONTROL;
        case AKEYCODE_ALT_LEFT: return VK_LMENU;
        case AKEYCODE_ALT_RIGHT: return VK_RMENU;
        case AKEYCODE_PAGE_UP: return VK_PRIOR;
        case AKEYCODE_PAGE_DOWN: return VK_NEXT;
        case AKEYCODE_MOVE_HOME: return VK_HOME;
        case AKEYCODE_MOVE_END: return VK_END;
        case AKEYCODE_INSERT: return VK_INSERT;
        case AKEYCODE_CAPS_LOCK: return VK_CAPITAL;
        case AKEYCODE_GRAVE: return VK_OEM_3;
        case AKEYCODE_MINUS: return VK_OEM_MINUS;
        case AKEYCODE_EQUALS: return VK_OEM_PLUS;
        case AKEYCODE_COMMA: return VK_OEM_COMMA;
        case AKEYCODE_PERIOD: return VK_OEM_PERIOD;
        case AKEYCODE_SLASH: return VK_OEM_2;
        case AKEYCODE_SEMICOLON: return VK_OEM_1;
        case AKEYCODE_LEFT_BRACKET: return VK_OEM_4;
        case AKEYCODE_RIGHT_BRACKET: return VK_OEM_6;
        case AKEYCODE_BACKSLASH: return VK_OEM_5;
        case AKEYCODE_APOSTROPHE: return VK_OEM_7;
        default: return 0;
    }
}

// A pad's name and maker, asked of Java once per device (InputDevice).
void describe_device(int32_t id) {
    static std::map<int32_t, bool> seen;
    if (seen.count(id)) return;
    seen[id] = true;
    JNIEnv* env = android::jni();
    if (!env) return;
    jclass cls = env->FindClass("android/view/InputDevice");
    jmethodID get = cls ? env->GetStaticMethodID(cls, "getDevice", "(I)Landroid/view/InputDevice;") : nullptr;
    jobject dev = get ? env->CallStaticObjectMethod(cls, get, id) : nullptr;
    std::string name = "Controller";
    int vendor = 0;
    if (dev) {
        if (jmethodID gn = env->GetMethodID(cls, "getName", "()Ljava/lang/String;")) {
            if (auto s = static_cast<jstring>(env->CallObjectMethod(dev, gn))) {
                const char* c = env->GetStringUTFChars(s, nullptr);
                name = c;
                env->ReleaseStringUTFChars(s, c);
                env->DeleteLocalRef(s);
            }
        }
        if (jmethodID gv = env->GetMethodID(cls, "getVendorId", "()I")) vendor = env->CallIntMethod(dev, gv);
        env->DeleteLocalRef(dev);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (cls) env->DeleteLocalRef(cls);
    LOG_INFO("Pad: %s (vendor %04x)", name.c_str(), unsigned(vendor));
    android_pad::connected(name.c_str(), vendor);
}

// The surface at most this tall (the landscape screen's short side): a phone's own resolution
// is far past what the eye takes in at arm's length, and the compositor scales it up for free.
constexpr int kMaxHeight = 1080;
// Touches come in screen pixels: these take them to the surface's.
float g_scale_x = 1, g_scale_y = 1;

void fit_surface(ANativeWindow* w, int& width, int& height) {
    // The display's own size first (the geometry set before would answer otherwise).
    ANativeWindow_setBuffersGeometry(w, 0, 0, 0);
    const int pw = ANativeWindow_getWidth(w), ph = ANativeWindow_getHeight(w);
    width = pw;
    height = ph;
    const int short_side = std::min(pw, ph);
    if (short_side > kMaxHeight) {
        const float s = float(kMaxHeight) / float(short_side);
        width = int(std::lround(float(pw) * s)) & ~1;
        height = int(std::lround(float(ph) * s)) & ~1;
        ANativeWindow_setBuffersGeometry(w, width, height, 0);
    }
    g_scale_x = float(width) / float(std::max(1, pw));
    g_scale_y = float(height) / float(std::max(1, ph));
    LOG_INFO("Window: screen %dx%d, drawn at %dx%d", pw, ph, width, height);
}

}  // namespace

Window::~Window() { destroy(); }

bool Window::create(const WindowDesc& desc) {
    android_app* app = android::app();
    if (!app) return false;
    g_window = this;
    app->onAppCmd = on_command;
    app->onInputEvent = on_input;
    background_ = desc.background;
    // The system gives the surface when the activity is shown.
    while (!app->window && !app->destroyRequested) process(app, -1);
    if (!app->window) return false;
    hwnd_ = app->window;
    fit_surface(app->window, width_, height_);
    focused_ = true;
    minimized_ = false;
    return true;
}

void Window::destroy() {
    if (android::app() && g_window == this) {
        android::app()->onAppCmd = nullptr;
        android::app()->onInputEvent = nullptr;
    }
    g_window = nullptr;
    hwnd_ = nullptr;
}

bool Window::pump() {
    android_app* app = android::app();
    if (!app) return false;
    // Last frame's lifts and fresh presses are done with; this frame's events bring new ones.
    lifted_.clear();
    for (auto& t : touches_) t.pressed = false;
    // The game took the fingers for its own controls (a match): the one working the menus' pointer
    // lets go of the button now, not when it is lifted (or it would fire, Fire being the left button).
    if (!touch_is_mouse && mouse_finger_ >= 0) {
        mouse_finger_ = -1;
        input_.on_mouse_button(kMouseLeft, false);
        if (ui_ready_) VanGui::GetIO().AddMouseButtonEvent(0, false);
    }
    // In the background (or with no surface) nothing is drawn: wait for the app to come back.
    process(app, 0);
    while (!app->destroyRequested && (minimized_ || !hwnd_)) process(app, -1);
    return !app->destroyRequested && !closed_;
}

void Window::handle_command(int cmd) {
    android_app* app = android::app();
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (!app->window) break;
            hwnd_ = app->window;
            fit_surface(app->window, width_, height_);
            resized_ = true;
            if (ui_ready_) {
                VanGui_ImplAndroid_Shutdown();
                VanGui_ImplAndroid_Init(app->window);
            }
            if (on_native_window) on_native_window(hwnd_);
            break;
        case APP_CMD_TERM_WINDOW:
            if (on_native_window) on_native_window(nullptr);
            hwnd_ = nullptr;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            if (app->window) {
                const int pw = width_, ph = height_;
                fit_surface(app->window, width_, height_);
                if (pw != width_ || ph != height_) resized_ = true;
            }
            safe_inset[0] = app->contentRect.left;
            safe_inset[1] = app->contentRect.top;
            break;
        case APP_CMD_GAINED_FOCUS:
            focused_ = true;
            break;
        case APP_CMD_LOST_FOCUS:
            focused_ = false;
            input_.on_focus_lost();
            touches_.clear();
            mouse_finger_ = -1;
            break;
        case APP_CMD_PAUSE:
            minimized_ = true;
            if (on_background) on_background(true);
            break;
        case APP_CMD_RESUME:
            minimized_ = false;
            if (on_background) on_background(false);
            break;
        case APP_CMD_LOW_MEMORY:
            LOG_WARN("Android: the system is low on memory");
            break;
        case APP_CMD_DESTROY:
            closed_ = true;
            break;
        default:
            break;
    }
}

int Window::handle_input(const void* event) {
    const auto* e = static_cast<const AInputEvent*>(event);
    const int32_t type = AInputEvent_getType(e);
    const int32_t source = AInputEvent_getSource(e);
    const bool pad_source = (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t code = AKeyEvent_getKeyCode(e);
        const int32_t action = AKeyEvent_getAction(e);
        if (action == AKEY_EVENT_ACTION_MULTIPLE) return 0;
        // The system keeps its own keys.
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_VOLUME_MUTE || code == AKEYCODE_POWER ||
            code == AKEYCODE_HOME || code == AKEYCODE_APP_SWITCH)
            return 0;
        const bool down = action == AKEY_EVENT_ACTION_DOWN;
        if (code == AKEYCODE_BACK) {
            input_.on_key(VK_ESCAPE, down);
            return 1;
        }
        if (pad_source || (code >= AKEYCODE_BUTTON_A && code <= AKEYCODE_BUTTON_MODE)) {
            describe_device(AInputEvent_getDeviceId(e));
            android_pad::key(code, down);
            return 1;
        }
        // A line open to the keyboard: a real keyboard's letters go into it (the phone's own
        // keyboard types into it on the Java side).
        if (g_typing) {
            const int32_t meta = AKeyEvent_getMetaState(e);
            const bool shift = (meta & AMETA_SHIFT_ON) != 0, caps = (meta & AMETA_CAPS_LOCK_ON) != 0;
            char ch = 0;
            int action_kind = 0;
            if (code >= AKEYCODE_A && code <= AKEYCODE_Z) ch = char((shift != caps ? 'A' : 'a') + (code - AKEYCODE_A));
            else if (code >= AKEYCODE_0 && code <= AKEYCODE_9 && !shift) ch = char('0' + (code - AKEYCODE_0));
            else if (code == AKEYCODE_SPACE) ch = ' ';
            else if (code == AKEYCODE_MINUS) ch = shift ? '_' : '-';
            else if (code == AKEYCODE_PERIOD) ch = '.';
            else if (code == AKEYCODE_APOSTROPHE) ch = '\'';
            else if (code == AKEYCODE_DEL) action_kind = 1;
            else if (code == AKEYCODE_ENTER || code == AKEYCODE_NUMPAD_ENTER) action_kind = 2;
            if (ch || action_kind) {
                if (down) type_keys(ch ? std::string(1, ch) : std::string(), action_kind);
                return 1;
            }
        }
        if (ui_ready_) VanGui_ImplAndroid_HandleInputEvent(e);
        if (const u32 vk = vk_of(code)) {
            input_.on_key(vk, down);
            if (vk == VK_LSHIFT || vk == VK_RSHIFT) input_.on_key(VK_SHIFT, down);
            if (vk == VK_LCONTROL || vk == VK_RCONTROL) input_.on_key(VK_CONTROL, down);
            if (vk == VK_LMENU || vk == VK_RMENU) input_.on_key(VK_MENU, down);
        }
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
        describe_device(AInputEvent_getDeviceId(e));
        auto ax = [&](int32_t a) { return AMotionEvent_getAxisValue(e, a, 0); };
        float rx = ax(AMOTION_EVENT_AXIS_Z), ry = ax(AMOTION_EVENT_AXIS_RZ);
        if (rx == 0 && ry == 0) {
            rx = ax(AMOTION_EVENT_AXIS_RX);
            ry = ax(AMOTION_EVENT_AXIS_RY);
        }
        const float lt = std::max(ax(AMOTION_EVENT_AXIS_LTRIGGER), ax(AMOTION_EVENT_AXIS_BRAKE));
        const float rt = std::max(ax(AMOTION_EVENT_AXIS_RTRIGGER), ax(AMOTION_EVENT_AXIS_GAS));
        android_pad::axes(ax(AMOTION_EVENT_AXIS_X), ax(AMOTION_EVENT_AXIS_Y), rx, ry, lt, rt, ax(AMOTION_EVENT_AXIS_HAT_X),
                          ax(AMOTION_EVENT_AXIS_HAT_Y));
        return 1;
    }

    // The touch screen (and a mouse or stylus, which work it the same way).
    const int32_t action = AMotionEvent_getAction(e);
    const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    const size_t index = size_t((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const size_t count = AMotionEvent_getPointerCount(e);
    auto find = [&](int id) { return std::find_if(touches_.begin(), touches_.end(), [&](const Touch& t) { return t.id == id; }); };
    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            Touch t;
            t.id = AMotionEvent_getPointerId(e, index);
            t.x = t.x0 = AMotionEvent_getX(e, index) * g_scale_x;
            t.y = t.y0 = AMotionEvent_getY(e, index) * g_scale_y;
            t.start = time::now();
            t.pressed = true;
            if (auto it = find(t.id); it != touches_.end()) *it = t;
            else touches_.push_back(t);
            if (touch_is_mouse && mouse_finger_ < 0) {
                mouse_finger_ = t.id;
                input_.on_mouse_button(kMouseLeft, true);
            }
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < count; ++i)
                if (auto it = find(AMotionEvent_getPointerId(e, i)); it != touches_.end()) {
                    it->x = AMotionEvent_getX(e, i) * g_scale_x;
                    it->y = AMotionEvent_getY(e, i) * g_scale_y;
                }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP: {
            const int id = AMotionEvent_getPointerId(e, index);
            if (auto it = find(id); it != touches_.end()) {
                it->x = AMotionEvent_getX(e, index) * g_scale_x;
                it->y = AMotionEvent_getY(e, index) * g_scale_y;
                lifted_.push_back(*it);
                touches_.erase(it);
            }
            if (id == mouse_finger_) {
                mouse_finger_ = -1;
                input_.on_mouse_button(kMouseLeft, false);
            }
            break;
        }
        case AMOTION_EVENT_ACTION_CANCEL:
            touches_.clear();
            if (mouse_finger_ >= 0) input_.on_mouse_button(kMouseLeft, false);
            mouse_finger_ = -1;
            break;
        default:
            break;
    }
    // VanGUI's pointer (the menus' pointer) is the finger working the mouse, in surface pixels.
    if (ui_ready_) {
        VanGuiIO& io = VanGui::GetIO();
        io.AddMouseSourceEvent(VanGuiMouseSource_TouchScreen);
        if (auto it = find(mouse_finger_); mouse_finger_ >= 0 && it != touches_.end()) {
            io.AddMousePosEvent(it->x, it->y);
            if (it->pressed) io.AddMouseButtonEvent(0, true);
        } else if (!lifted_.empty() && touch_is_mouse) {
            io.AddMousePosEvent(lifted_.back().x, lifted_.back().y);
            io.AddMouseButtonEvent(0, false);
        }
    }
    return 1;
}

bool Window::ui_init() {
    if (!hwnd_) return false;
    ui_ready_ = VanGui_ImplAndroid_Init(static_cast<ANativeWindow*>(hwnd_));
    return ui_ready_;
}

void Window::ui_shutdown() {
    if (ui_ready_) VanGui_ImplAndroid_Shutdown();
    ui_ready_ = false;
}

void Window::ui_new_frame() {
    if (ui_ready_ && hwnd_) VanGui_ImplAndroid_NewFrame();
}

bool Window::consume_resize() {
    const bool r = resized_;
    resized_ = false;
    return r;
}

void Window::set_display_mode(DisplayMode mode, int, int) { mode_ = mode; }
void Window::set_title(const std::wstring&) {}
// A phone's screen is the window, whole and in its own pixels; a finger is the pointer.
ScreenSize Window::screen() const { return {width_, height_}; }
ScreenSize Window::screen_room() const { return {width_, height_}; }
std::vector<ScreenSize> Window::screen_modes() const { return {}; }
void Window::use_real_pixels() {}
bool Window::pointer(float&, float&) const { return false; }
void Window::set_pointer(float, float) {}
void Window::set_mouse_captured(bool captured) { captured_ = captured; }
// The activity is the system's to close: nothing is hidden by hand.
void Window::hide() {}
void Window::show_os_cursor(bool show) { os_cursor_ = show; }
i64 Window::dispatch(void*, unsigned, u64, i64) { return 0; }
i64 Window::handle(unsigned, u64, i64) { return 0; }
void Window::apply_clip() {}

}  // namespace eng
