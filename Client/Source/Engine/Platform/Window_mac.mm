// The game window on a Mac (Apple Silicon): a Cocoa window whose view hosts a plain CALayer, which
// is what the renderer is given (ANGLE puts its Metal layer inside it), its keyboard and mouse,
// borderless full screen (the Dock and the menu bar hidden), mouse-look (the pointer hidden and
// held still, the mouse's own movement read from its events), Retina (the window's size counted in
// real pixels), Cmd+Q and the window's close button (both ask the game to quit, which saves first),
// and VanGUI's platform side.
//
// Built with ARC (Client/Mac/CMakeLists.txt): the Cocoa objects live in MacState and go with it.
#include "Engine/Platform/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Platform/DesktopKeys.hpp"
#include "Engine/Platform/Keys.hpp"
#include "Engine/Platform/System.hpp"

#include <vangui/vangui.h>

// Carbon brings AssertMacros.h, whose bare check()/require()/verify() macros C++ must not meet.
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#import <Carbon/Carbon.h>   // the kVK_ key codes (HIToolbox/Events.h): constants only
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace {

// What Cocoa's callbacks tell the window, read back by Window::pump.
struct MacEvents {
    bool close = false;
    int focus = -1;          // 1 came, 0 went, -1 no change
    bool resized = false;
    bool minimized = false;
};

}  // namespace

// The window's delegate: closing, focus, size and the screen's scale.
@interface LsfWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) MacEvents* events;
@end

@implementation LsfWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    _events->close = true;   // the game quits in its own time (it saves first)
    return NO;
}
- (void)windowDidBecomeKey:(NSNotification*)n { _events->focus = 1; }
- (void)windowDidResignKey:(NSNotification*)n { _events->focus = 0; }
- (void)windowDidResize:(NSNotification*)n { _events->resized = true; }
- (void)windowDidChangeBackingProperties:(NSNotification*)n { _events->resized = true; }
- (void)windowDidChangeScreen:(NSNotification*)n { _events->resized = true; }
- (void)windowDidMiniaturize:(NSNotification*)n { _events->minimized = true; }
- (void)windowDidDeminiaturize:(NSNotification*)n { _events->minimized = false; }
@end

// Cmd+Q and the app menu's Quit: asked of the game, never done behind its back.
@interface LsfAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic) MacEvents* events;
@end

@implementation LsfAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    if (_events) _events->close = true;
    return NSTerminateCancel;
}
@end

// The window: a borderless one (full screen) must still take the keyboard, which Cocoa's own refuses.
@interface LsfWindow : NSWindow
@end

@implementation LsfWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

// The view: takes the keyboard (so keys never beep) and the first click.
@interface LsfView : NSView
@end

@implementation LsfView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }
- (BOOL)isFlipped { return YES; }
- (void)keyDown:(NSEvent*)e {}
- (void)keyUp:(NSEvent*)e {}
- (void)flagsChanged:(NSEvent*)e {}
@end

namespace eng {

namespace {

struct MacState {
    LsfWindow* window = nil;
    LsfView* view = nil;
    CALayer* layer = nil;
    LsfWindowDelegate* delegate = nil;
    MacEvents events;
    double scale = 1;            // real pixels per point
    bool held = false;           // the pointer is held for mouse-look
    bool cursor_hidden = false;
    float mouse_x = -1, mouse_y = -1;   // the pointer, in the window's pixels
    double look_x = 0, look_y = 0;      // mouse-look's fractions of a count, carried over
    double wheel = 0;                   // a trackpad's scrolling, carried over to whole notches
    NSRect windowed = NSZeroRect;       // the window's frame before full screen
};

LsfAppDelegate* g_app_delegate = nil;

MacState& state_of(void* p) { return *static_cast<MacState*>(p); }
const MacState& state_of(const void* p) { return *static_cast<const MacState*>(p); }

// The app itself, once: a regular app (a Dock icon, a menu bar) with Quit (Cmd+Q) and Hide (Cmd+H).
void start_app(bool background) {
    static bool started = false;
    if (started) return;
    started = true;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_app_delegate = [[LsfAppDelegate alloc] init];
    [NSApp setDelegate:g_app_delegate];
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    NSMenu* app_menu = [[NSMenu alloc] init];
    [app_menu addItemWithTitle:@"Hide Soldier Front Legacy" action:@selector(hide:) keyEquivalent:@"h"];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Quit Soldier Front Legacy" action:@selector(terminate:) keyEquivalent:@"q"];
    [app_item setSubmenu:app_menu];
    [NSApp setMainMenu:bar];
    [NSApp finishLaunching];
    if (!background) [NSApp activateIgnoringOtherApps:YES];
}

// Mac key codes (where a key is, not what it types) as the game's (Windows') virtual keys.
u32 vk_of(unsigned short code) {
    switch (code) {
        case kVK_ANSI_A: return 'A';
        case kVK_ANSI_B: return 'B';
        case kVK_ANSI_C: return 'C';
        case kVK_ANSI_D: return 'D';
        case kVK_ANSI_E: return 'E';
        case kVK_ANSI_F: return 'F';
        case kVK_ANSI_G: return 'G';
        case kVK_ANSI_H: return 'H';
        case kVK_ANSI_I: return 'I';
        case kVK_ANSI_J: return 'J';
        case kVK_ANSI_K: return 'K';
        case kVK_ANSI_L: return 'L';
        case kVK_ANSI_M: return 'M';
        case kVK_ANSI_N: return 'N';
        case kVK_ANSI_O: return 'O';
        case kVK_ANSI_P: return 'P';
        case kVK_ANSI_Q: return 'Q';
        case kVK_ANSI_R: return 'R';
        case kVK_ANSI_S: return 'S';
        case kVK_ANSI_T: return 'T';
        case kVK_ANSI_U: return 'U';
        case kVK_ANSI_V: return 'V';
        case kVK_ANSI_W: return 'W';
        case kVK_ANSI_X: return 'X';
        case kVK_ANSI_Y: return 'Y';
        case kVK_ANSI_Z: return 'Z';
        case kVK_ANSI_0: return '0';
        case kVK_ANSI_1: return '1';
        case kVK_ANSI_2: return '2';
        case kVK_ANSI_3: return '3';
        case kVK_ANSI_4: return '4';
        case kVK_ANSI_5: return '5';
        case kVK_ANSI_6: return '6';
        case kVK_ANSI_7: return '7';
        case kVK_ANSI_8: return '8';
        case kVK_ANSI_9: return '9';
        case kVK_F1: return VK_F1;
        case kVK_F2: return VK_F1 + 1;
        case kVK_F3: return VK_F1 + 2;
        case kVK_F4: return VK_F1 + 3;
        case kVK_F5: return VK_F1 + 4;
        case kVK_F6: return VK_F1 + 5;
        case kVK_F7: return VK_F1 + 6;
        case kVK_F8: return VK_F1 + 7;
        case kVK_F9: return VK_F1 + 8;
        case kVK_F10: return VK_F1 + 9;
        case kVK_F11: return VK_F1 + 10;
        case kVK_F12: return VK_F12;
        case kVK_ANSI_Keypad0: return VK_NUMPAD0;
        case kVK_ANSI_Keypad1: return VK_NUMPAD0 + 1;
        case kVK_ANSI_Keypad2: return VK_NUMPAD0 + 2;
        case kVK_ANSI_Keypad3: return VK_NUMPAD0 + 3;
        case kVK_ANSI_Keypad4: return VK_NUMPAD0 + 4;
        case kVK_ANSI_Keypad5: return VK_NUMPAD0 + 5;
        case kVK_ANSI_Keypad6: return VK_NUMPAD0 + 6;
        case kVK_ANSI_Keypad7: return VK_NUMPAD0 + 7;
        case kVK_ANSI_Keypad8: return VK_NUMPAD0 + 8;
        case kVK_ANSI_Keypad9: return VK_NUMPAD0 + 9;
        case kVK_ANSI_KeypadDecimal: return VK_DECIMAL;
        case kVK_ANSI_KeypadMultiply: return VK_MULTIPLY;
        case kVK_ANSI_KeypadPlus: return VK_ADD;
        case kVK_ANSI_KeypadMinus: return VK_SUBTRACT;
        case kVK_ANSI_KeypadDivide: return VK_DIVIDE;
        case kVK_ANSI_KeypadEnter: return VK_RETURN;
        case kVK_ANSI_KeypadClear: return kVkNumLock;
        case kVK_Return: return VK_RETURN;
        case kVK_Tab: return VK_TAB;
        case kVK_Space: return VK_SPACE;
        case kVK_Delete: return VK_BACK;
        case kVK_ForwardDelete: return VK_DELETE;
        case kVK_Escape: return VK_ESCAPE;
        case kVK_Help: return VK_INSERT;   // where a PC keyboard's Insert is
        case kVK_Home: return VK_HOME;
        case kVK_End: return VK_END;
        case kVK_PageUp: return VK_PRIOR;
        case kVK_PageDown: return VK_NEXT;
        case kVK_LeftArrow: return VK_LEFT;
        case kVK_RightArrow: return VK_RIGHT;
        case kVK_UpArrow: return VK_UP;
        case kVK_DownArrow: return VK_DOWN;
        case kVK_Shift: return VK_LSHIFT;
        case kVK_RightShift: return VK_RSHIFT;
        case kVK_Control: return VK_LCONTROL;
        case kVK_RightControl: return VK_RCONTROL;
        case kVK_Option: return VK_LMENU;
        case kVK_RightOption: return VK_RMENU;
        case kVK_Command: return kVkLWin;
        case kVK_RightCommand: return kVkRWin;
        case kVK_CapsLock: return VK_CAPITAL;
        case kVK_ANSI_Quote: return VK_OEM_7;
        case kVK_ANSI_Comma: return VK_OEM_COMMA;
        case kVK_ANSI_Minus: return VK_OEM_MINUS;
        case kVK_ANSI_Period: return VK_OEM_PERIOD;
        case kVK_ANSI_Slash: return VK_OEM_2;
        case kVK_ANSI_Semicolon: return VK_OEM_1;
        case kVK_ANSI_Equal: return VK_OEM_PLUS;
        case kVK_ANSI_LeftBracket: return VK_OEM_4;
        case kVK_ANSI_Backslash: return VK_OEM_5;
        case kVK_ANSI_RightBracket: return VK_OEM_6;
        case kVK_ANSI_Grave: return VK_OEM_3;
        default: return 0;
    }
}

// Which side's modifier a flags-changed event is about, as the event's device flags say it (both
// Shifts held and one let go is then still told apart).
NSEventModifierFlags side_mask(unsigned short code) {
    switch (code) {
        case kVK_Shift: return 0x0002;          // NX_DEVICELSHIFTKEYMASK
        case kVK_RightShift: return 0x0004;     // NX_DEVICERSHIFTKEYMASK
        case kVK_Control: return 0x0001;        // NX_DEVICELCTLKEYMASK
        case kVK_RightControl: return 0x2000;   // NX_DEVICERCTLKEYMASK
        case kVK_Option: return 0x0020;         // NX_DEVICELALTKEYMASK
        case kVK_RightOption: return 0x0040;    // NX_DEVICERALTKEYMASK
        case kVK_Command: return 0x0008;        // NX_DEVICELCMDKEYMASK
        case kVK_RightCommand: return 0x0010;   // NX_DEVICERCMDKEYMASK
        case kVK_CapsLock: return NSEventModifierFlagCapsLock;
        default: return 0;
    }
}

void feed_modifiers(NSEventModifierFlags flags) {
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(VanGuiMod_Ctrl, (flags & NSEventModifierFlagControl) != 0);
    io.AddKeyEvent(VanGuiMod_Shift, (flags & NSEventModifierFlagShift) != 0);
    io.AddKeyEvent(VanGuiMod_Alt, (flags & NSEventModifierFlagOption) != 0);
    io.AddKeyEvent(VanGuiMod_Super, (flags & NSEventModifierFlagCommand) != 0);
}

// What a key typed, without the keys that type nothing (arrows and F keys are in Apple's
// private-use range).
std::string typed_of(NSEvent* e) {
    NSString* chars = [e characters];
    if (!chars || chars.length == 0) return {};
    std::string out;
    for (NSUInteger i = 0; i < chars.length; ++i) {
        const unichar c = [chars characterAtIndex:i];
        if (c < 0x20 || c == 0x7F || (c >= 0xF700 && c <= 0xF8FF)) return {};
    }
    if (const char* u = [chars UTF8String]) out = u;
    return out;
}

// The pointer, from a point in the window (bottom-left origin) to the view's real pixels.
NSPoint to_pixels(const MacState& st, NSPoint in_window) {
    const NSPoint p = [st.view convertPoint:in_window fromView:nil];   // the view is flipped: y down
    return NSMakePoint(p.x * st.scale, p.y * st.scale);
}

}  // namespace

// ── Typing: a Mac has a keyboard; the game's own fields take it ────────────────

namespace platform {
bool text_input_native() { return false; }
void start_typing(const std::string&, int, bool) {}
void stop_typing() {}
bool typed_text(std::string&) { return false; }
bool text_answer(std::string&, bool&) { return false; }
}  // namespace platform

Window::~Window() { destroy(); }

bool Window::create(const WindowDesc& desc) {
    @autoreleasepool {
        background_ = desc.background;
        start_app(background_);
        auto* st = new MacState;
        platform_ = st;
        g_app_delegate.events = &st->events;

        NSScreen* screen = [NSScreen mainScreen];
        st->scale = screen ? screen.backingScaleFactor : 1.0;
        // The size asked for is in real pixels, as on Windows; Cocoa counts points.
        const NSRect room = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
        const double w = std::min(std::max(320.0, desc.width / st->scale), room.size.width);
        const double h = std::min(std::max(200.0, desc.height / st->scale), room.size.height - 28);
        const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        st->window = [[LsfWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h) styleMask:style backing:NSBackingStoreBuffered defer:NO];
        if (!st->window) {
            LOG_ERROR("Cocoa: the window could not be made");
            return false;
        }
        st->window.releasedWhenClosed = NO;
        st->window.acceptsMouseMovedEvents = YES;
        st->window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        st->delegate = [[LsfWindowDelegate alloc] init];
        st->delegate.events = &st->events;
        st->window.delegate = st->delegate;

        // A layer-hosting view: the layer is ours, and ANGLE draws inside it with Metal.
        st->view = [[LsfView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
        st->layer = [CALayer layer];
        st->layer.contentsScale = st->scale;
        st->view.layer = st->layer;
        st->view.wantsLayer = YES;
        st->window.contentView = st->view;
        [st->window makeFirstResponder:st->view];
        [st->window center];
        hwnd_ = (__bridge void*)st->layer;

        const NSRect px = [st->view convertRectToBacking:st->view.bounds];
        width_ = std::max(1, int(px.size.width));
        height_ = std::max(1, int(px.size.height));
        set_title(desc.title);
        if (background_) [st->window orderFront:nil];
        else [st->window makeKeyAndOrderFront:nil];
        mode_ = DisplayMode::Windowed;
        if (desc.mode == DisplayMode::Borderless) set_display_mode(DisplayMode::Borderless, 0, 0);
        focused_ = !background_;
        LOG_INFO("Window: Cocoa, %dx%d (%.1fx)", width_, height_, st->scale);
        return true;
    }
}

void Window::destroy() {
    if (!platform_) return;
    @autoreleasepool {
        MacState& st = state_of(platform_);
        if (st.held) {
            CGAssociateMouseAndMouseCursorPosition(true);
            st.held = false;
        }
        if (st.cursor_hidden) [NSCursor unhide];
        if (g_app_delegate.events == &st.events) g_app_delegate.events = nullptr;
        st.window.delegate = nil;
        [st.window orderOut:nil];
        [st.window close];
        delete &st;
    }
    platform_ = nullptr;
    hwnd_ = nullptr;
}

bool Window::pump() {
    if (!platform_) return false;
    MacState& st = state_of(platform_);
    VanGuiIO* io = ui_ready_ ? &VanGui::GetIO() : nullptr;
    @autoreleasepool {
        for (;;) {
            NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!e) break;
            const NSEventType type = e.type;
            const bool ours = e.window == st.window;
            const bool input = type == NSEventTypeKeyDown || type == NSEventTypeKeyUp || type == NSEventTypeFlagsChanged ||
                               type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseUp || type == NSEventTypeRightMouseDown ||
                               type == NSEventTypeRightMouseUp || type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseUp ||
                               type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged || type == NSEventTypeRightMouseDragged ||
                               type == NSEventTypeOtherMouseDragged || type == NSEventTypeScrollWheel;
            if (background_ && input) {
                // An automated run: the real keyboard and mouse never reach the game (Cmd+Q still quits).
                if (type == NSEventTypeKeyDown && (e.modifierFlags & NSEventModifierFlagCommand)) [NSApp sendEvent:e];
                continue;
            }
            bool forward = true;
            switch (type) {
                case NSEventTypeKeyDown:
                case NSEventTypeKeyUp: {
                    const bool down = type == NSEventTypeKeyDown;
                    const NSEventModifierFlags flags = e.modifierFlags;
                    // Cmd with a key is the system's and the menu's (Cmd+Q, Cmd+H); the game never sees
                    // it pressed, but always sees a key let go (W held, then Cmd+Tab, must not stick).
                    const bool command = (flags & NSEventModifierFlagCommand) != 0;
                    if (command && down) break;
                    forward = command;
                    const u32 vk = vk_of(e.keyCode);
                    if (vk) input_.on_key(vk, down);
                    if (io) {
                        feed_modifiers(flags);
                        if (const VanGuiKey k = vangui_key_of(vk); k != VanGuiKey_None) io->AddKeyEvent(k, down);
                        if (down && !(flags & NSEventModifierFlagControl)) {
                            const std::string typed = typed_of(e);
                            if (!typed.empty()) io->AddInputCharactersUTF8(typed.c_str());
                        }
                    }
                    break;
                }
                case NSEventTypeFlagsChanged: {
                    forward = false;
                    const u32 vk = vk_of(e.keyCode);
                    const NSEventModifierFlags mask = side_mask(e.keyCode);
                    if (!vk || !mask) break;
                    const bool down = (e.modifierFlags & mask) != 0;
                    input_.on_key(vk, down);
                    if (vk == VK_LSHIFT || vk == VK_RSHIFT) input_.on_key(VK_SHIFT, (e.modifierFlags & NSEventModifierFlagShift) != 0);
                    if (vk == VK_LCONTROL || vk == VK_RCONTROL) input_.on_key(VK_CONTROL, (e.modifierFlags & NSEventModifierFlagControl) != 0);
                    if (vk == VK_LMENU || vk == VK_RMENU) input_.on_key(VK_MENU, (e.modifierFlags & NSEventModifierFlagOption) != 0);
                    if (io) {
                        feed_modifiers(e.modifierFlags);
                        if (const VanGuiKey k = vangui_key_of(vk); k != VanGuiKey_None) io->AddKeyEvent(k, down);
                    }
                    break;
                }
                case NSEventTypeLeftMouseDown:
                case NSEventTypeLeftMouseUp:
                case NSEventTypeRightMouseDown:
                case NSEventTypeRightMouseUp:
                case NSEventTypeOtherMouseDown:
                case NSEventTypeOtherMouseUp: {
                    const bool down = type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown || type == NSEventTypeOtherMouseDown;
                    // A click on the title bar (dragging the window) is the window's, not the game's; a
                    // button let go anywhere is the game's (none stays held).
                    if (down) {
                        if (!ours) break;
                        const NSPoint p = to_pixels(st, e.locationInWindow);
                        if (!st.held && (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_)) break;
                    }
                    const NSInteger n = e.buttonNumber;
                    const int button = n == 0 ? kMouseLeft : n == 1 ? kMouseRight : n == 2 ? kMouseMiddle : n == 3 ? kMouse4 : n == 4 ? kMouse5 : -1;
                    if (button < 0) break;
                    input_.on_mouse_button(button, down);
                    if (io && button <= kMouseMiddle) io->AddMouseButtonEvent(button == kMouseLeft ? 0 : button == kMouseRight ? 1 : 2, down);
                    break;
                }
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged: {
                    if (st.held) {
                        // Mouse-look: the pointer is held still; the event's deltas are the mouse's own
                        // movement (in points: counted in the screen's pixels, as on Windows).
                        st.look_x += e.deltaX * st.scale;
                        st.look_y += e.deltaY * st.scale;
                        const long dx = long(st.look_x), dy = long(st.look_y);
                        st.look_x -= double(dx), st.look_y -= double(dy);
                        if (dx != 0 || dy != 0) input_.on_raw_mouse(dx, dy);
                        forward = false;
                        break;
                    }
                    if (!ours) break;
                    const NSPoint p = to_pixels(st, e.locationInWindow);
                    st.mouse_x = float(p.x), st.mouse_y = float(p.y);
                    if (io) io->AddMousePosEvent(st.mouse_x, st.mouse_y);
                    break;
                }
                case NSEventTypeScrollWheel: {
                    if (!ours && !st.held) break;
                    // A wheel's lines, or a trackpad's points (twenty to a notch). The menus scroll as the
                    // Mac is set to; the game (the next weapon) goes by which way the wheel turned.
                    const double dy = e.hasPreciseScrollingDeltas ? e.scrollingDeltaY / 20.0 : e.scrollingDeltaY;
                    const double dx = e.hasPreciseScrollingDeltas ? e.scrollingDeltaX / 20.0 : e.scrollingDeltaX;
                    st.wheel += e.isDirectionInvertedFromDevice ? -dy : dy;
                    if (std::fabs(st.wheel) >= 1.0) {
                        const double notches = std::trunc(st.wheel);
                        input_.on_wheel(float(notches));
                        st.wheel -= notches;
                    }
                    if (io) io->AddMouseWheelEvent(float(dx), float(dy));
                    forward = false;
                    break;
                }
                default: break;
            }
            if (forward) [NSApp sendEvent:e];
        }
        [NSApp updateWindows];
    }

    // What the delegates said while the events went through.
    if (st.events.focus >= 0) {
        focused_ = st.events.focus == 1;
        st.events.focus = -1;
        if (!focused_) input_.on_focus_lost();
        if (io) io->AddFocusEvent(focused_);
        apply_clip();
    }
    minimized_ = st.events.minimized;
    if (st.events.resized) {
        st.events.resized = false;
        st.scale = st.window.backingScaleFactor;
        st.layer.contentsScale = st.scale;
        const NSRect px = [st.view convertRectToBacking:st.view.bounds];
        const int w = std::max(1, int(px.size.width)), h = std::max(1, int(px.size.height));
        if (w != width_ || h != height_) {
            width_ = w, height_ = h;
            resized_ = true;
        }
        if (st.held) apply_clip();   // the middle moved
    }
    if (st.events.close) closed_ = true;
    return !closed_;
}

bool Window::ui_init() {
    VanGuiIO& io = VanGui::GetIO();
    io.BackendPlatformName = "legacysf_cocoa";
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
    if (platform_ && state_of(platform_).held) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

bool Window::consume_resize() {
    const bool r = resized_;
    resized_ = false;
    return r;
}

void Window::set_display_mode(DisplayMode mode, int width, int height) {
    if (!platform_) return;
    @autoreleasepool {
        MacState& st = state_of(platform_);
        const bool full = mode == DisplayMode::Borderless;
        const bool was_full = mode_ == DisplayMode::Borderless;
        if (full && !was_full) {
            // The whole screen, the Dock and the menu bar out of the way while the game is in front.
            st.windowed = st.window.frame;
            NSScreen* screen = st.window.screen ? st.window.screen : [NSScreen mainScreen];
            st.window.styleMask = NSWindowStyleMaskBorderless;
            [st.window setFrame:screen.frame display:YES];
            st.window.level = NSMainMenuWindowLevel + 1;
            [NSApp setPresentationOptions:NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar];
        } else if (!full) {
            if (was_full) {
                [NSApp setPresentationOptions:NSApplicationPresentationDefault];
                st.window.level = NSNormalWindowLevel;
                st.window.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
                if (!NSIsEmptyRect(st.windowed)) [st.window setFrame:st.windowed display:YES];
            }
            if (width > 0 && height > 0) {
                [st.window setContentSize:NSMakeSize(width / st.scale, height / st.scale)];
                if (!was_full) [st.window center];
            }
        }
        [st.window makeFirstResponder:st.view];
        if (!background_) [st.window makeKeyAndOrderFront:nil];
        st.events.resized = true;
    }
    mode_ = mode;
}

void Window::set_title(const std::wstring& title) {
    if (!platform_) return;
    @autoreleasepool {
        const std::string t = str::narrow(title);
        state_of(platform_).window.title = [NSString stringWithUTF8String:t.c_str()] ?: @"Soldier Front Legacy";
    }
}

ScreenSize Window::screen() const {
    if (!platform_) return {width_, height_};
    const MacState& st = state_of(platform_);
    NSScreen* s = st.window.screen ? st.window.screen : [NSScreen mainScreen];
    if (!s) return {width_, height_};
    return {int(s.frame.size.width * s.backingScaleFactor), int(s.frame.size.height * s.backingScaleFactor)};
}

ScreenSize Window::screen_room() const {
    if (!platform_) return {width_, height_};
    const MacState& st = state_of(platform_);
    NSScreen* s = st.window.screen ? st.window.screen : [NSScreen mainScreen];
    if (!s) return {width_, height_};
    // Less the menu bar and the Dock (visibleFrame), and the window's own title bar.
    const NSRect room = s.visibleFrame;
    return {int(room.size.width * s.backingScaleFactor), std::max(200, int((room.size.height - 28) * s.backingScaleFactor))};
}

std::vector<ScreenSize> Window::screen_modes() const {
    // The common sizes that fit the screen, and the screen's own (no mode is ever switched: the
    // picture is drawn at the size picked and the window fits it).
    const ScreenSize s = screen();
    static const ScreenSize all[] = {{1024, 768}, {1280, 720}, {1280, 800}, {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080},
                                     {1920, 1200}, {2560, 1440}, {2560, 1600}, {2880, 1800}, {3024, 1964}, {3456, 2234}, {3840, 2160}};
    std::vector<ScreenSize> out;
    for (const ScreenSize& m : all)
        if (m.width <= s.width && m.height <= s.height) out.push_back(m);
    if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    return out;
}

void Window::use_real_pixels() {}

bool Window::pointer(float& x, float& y) const {
    if (!platform_) return false;
    const MacState& st = state_of(platform_);
    if (st.held) return false;
    const NSPoint p = to_pixels(st, st.window.mouseLocationOutsideOfEventStream);
    if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) return false;
    x = float(p.x), y = float(p.y);
    return true;
}

void Window::set_pointer(float x, float y) {
    if (!platform_ || background_ || !focused_) return;
    MacState& st = state_of(platform_);
    if (st.held) return;
    // From the view's pixels to the screen's points, top-left origin (the display's own space).
    const NSPoint in_view = NSMakePoint(std::clamp(double(x), 0.0, double(width_ - 1)) / st.scale, std::clamp(double(y), 0.0, double(height_ - 1)) / st.scale);
    const NSPoint in_window = [st.view convertPoint:in_view toView:nil];
    const NSPoint on_screen = [st.window convertPointToScreen:in_window];
    const CGFloat top = NSMaxY([NSScreen screens].firstObject.frame);
    CGWarpMouseCursorPosition(CGPointMake(on_screen.x, top - on_screen.y));
    CGAssociateMouseAndMouseCursorPosition(true);   // no quarter-second freeze after the jump
}

void Window::set_mouse_captured(bool captured) {
    if (captured == captured_) return;
    captured_ = captured;
    apply_clip();
}

void Window::hide() {
    if (!platform_) return;
    set_mouse_captured(false);
    @autoreleasepool {
        MacState& st = state_of(platform_);
        if (mode_ == DisplayMode::Borderless) [NSApp setPresentationOptions:NSApplicationPresentationDefault];
        [st.window orderOut:nil];
    }
}

void Window::show_os_cursor(bool show) {
    if (show == os_cursor_) return;
    os_cursor_ = show;
    apply_clip();
}

i64 Window::dispatch(void*, unsigned, u64, i64) { return 0; }
i64 Window::handle(unsigned, u64, i64) { return 0; }
void Window::hold_accessibility_shortcuts(bool) {}

// Mouse-look holds the pointer (hidden, held still in the window's middle, the mouse's movement read
// from its events) only while the window has the focus: Cmd+Tab, a screenshot (Cmd+Shift+4) or the
// menu bar taking it gets the pointer back at once.
void Window::apply_clip() {
    if (!platform_) return;
    MacState& st = state_of(platform_);
    const bool hold = captured_ && focused_ && !background_;
    if (hold) {
        // Into the middle first, so a click lands in the window, then held there.
        const NSRect frame = [st.window convertRectToScreen:[st.view convertRect:st.view.bounds toView:nil]];
        const CGFloat top = NSMaxY([NSScreen screens].firstObject.frame);
        CGWarpMouseCursorPosition(CGPointMake(NSMidX(frame), top - NSMidY(frame)));
        CGAssociateMouseAndMouseCursorPosition(false);
        st.look_x = st.look_y = 0;
        st.held = true;
    } else if (st.held) {
        CGAssociateMouseAndMouseCursorPosition(true);
        st.held = false;
    }
    // The game draws its own arrow over its window, so the system's is hidden there too.
    const bool hide_cursor = hold || (!os_cursor_ && focused_);
    if (hide_cursor != st.cursor_hidden) {
        if (hide_cursor) [NSCursor hide];
        else [NSCursor unhide];
        st.cursor_hidden = hide_cursor;
    }
}

}  // namespace eng
