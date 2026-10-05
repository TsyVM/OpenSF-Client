// VanGUI: Platform Backend for OSX / Cocoa
// This needs to be used along with a Renderer (e.g. OpenGL2, OpenGL3, Vulkan, Metal..)
// - Not well tested. If you want a portable application, prefer using the GLFW or SDL platform Backends on Mac.
// - Requires linking with the GameController framework ("-framework GameController").

// Implemented features:
//  [X] Platform: Clipboard support is part of core VanGUI (no specific code in this backend).
//  [X] Platform: Mouse support. Can discriminate Mouse/Pen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy kVK_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Gamepad support.
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: IME support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

#import "vangui.h"
#ifndef VANGUI_DISABLE
#import "vangui_impl_osx.h"
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <GameController/GameController.h>
#import <time.h>

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2025-09-18: Call platform_io.ClearPlatformHandlers() on shutdown.
//  2025-06-27: Added VanGuiMouseCursor_Wait and VanGuiMouseCursor_Progress mouse cursor support.
//  2025-06-12: VanGui_ImplOSX_HandleEvent() only process event for window containing our view. (#8644)
//  2025-03-21: Fill gamepad inputs and set VanGuiBackendFlags_HasGamepad regardless of VanGuiConfigFlags_NavEnableGamepad being set.
//  2025-01-20: Removed notification observer when shutting down. (#8331)
//  2024-08-22: moved some OS/backend related function pointers from VanGuiIO to VanGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//               - io.PlatformSetImeDataFn  -> platform_io.Platform_SetImeDataFn
//  2024-07-02: Update for io.SetPlatformImeDataFn() -> io.PlatformSetImeDataFn() renaming in main library.
//  2023-10-05: Inputs: Added support for extra VanGuiKey values: F13 to F20 function keys. Stopped mapping F13 into PrintScreen.
//  2023-04-09: Inputs: Added support for io.AddMouseSourceEvent() to discriminate VanGuiMouseSource_Mouse/VanGuiMouseSource_Pen.
//  2023-02-01: Fixed scroll wheel scaling for devices emitting events with hasPreciseScrollingDeltas==false (e.g. non-Apple mice).
//  2022-11-02: Fixed mouse coordinates before clicking the host window.
//  2022-10-06: Fixed mouse inputs on flipped views.
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-05-03: Inputs: Removed VanGui_ImplOSX_HandleEvent() from backend API in favor of backend automatically handling event capture.
//  2022-04-27: Misc: Store backend data in a per-context struct, allowing to use this backend with multiple contexts.
//  2022-03-22: Inputs: Monitor NSKeyUp events to catch missing keyUp for key when user press Cmd + key
//  2022-02-07: Inputs: Forward keyDown/keyUp events to OS when unused by VanGUI.
//  2022-01-31: Fixed building with old Xcode versions that are missing gamepad features.
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2021-01-20: Inputs: calling new io.AddKeyAnalogEvent() for gamepad support, instead of writing directly to io.NavInputs[].
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-12: Inputs: Added basic Platform IME support, hooking the io.SetPlatformImeDataFn() function.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2021-12-13: *BREAKING CHANGE* Add NSView parameter to VanGui_ImplOSX_Init(). Generally fix keyboard support. Using kVK_* codes for keyboard keys.
//  2021-12-13: Add game controller support.
//  2021-09-21: Use mach_absolute_time as CFAbsoluteTimeGetCurrent can jump backwards.
//  2021-08-17: Calling io.AddFocusEvent() on NSApplicationDidBecomeActiveNotification/NSApplicationDidResignActiveNotification events.
//  2021-06-23: Inputs: Added a fix for shortcuts using CTRL key instead of CMD key.
//  2021-04-19: Inputs: Added a fix for keys remaining stuck in pressed state when CMD-tabbing into different application.
//  2021-01-27: Inputs: Added a fix for mouse position not being reported when mouse buttons other than left one are down.
//  2020-10-28: Inputs: Added a fix for handling keypad-enter key.
//  2020-05-25: Inputs: Added a fix for missing trackpad clicks when done with "soft tap".
//  2019-12-05: Inputs: Added support for VanGuiMouseCursor_NotAllowed mouse cursor.
//  2019-10-11: Inputs:  Fix using Backspace key.
//  2019-07-21: Re-added clipboard handlers as they are not enabled by default in core vangui.cpp (reverted 2019-05-18 change).
//  2019-05-28: Inputs: Added mouse cursor shape and visibility support.
//  2019-05-18: Misc: Removed clipboard handlers as they are now supported by core vangui.cpp.
//  2019-05-11: Inputs: Don't filter character values before calling AddInputCharacter() apart from 0xF700..0xFFFF range.
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-07-07: Initial version.

#define APPLE_HAS_BUTTON_OPTIONS (__IPHONE_OS_VERSION_MIN_REQUIRED >= 130000 || __MAC_OS_X_VERSION_MIN_REQUIRED >= 101500 || __TV_OS_VERSION_MIN_REQUIRED >= 130000)
#define APPLE_HAS_CONTROLLER     (__IPHONE_OS_VERSION_MIN_REQUIRED >= 140000 || __MAC_OS_X_VERSION_MIN_REQUIRED >= 110000 || __TV_OS_VERSION_MIN_REQUIRED >= 140000)
#define APPLE_HAS_THUMBSTICKS    (__IPHONE_OS_VERSION_MIN_REQUIRED >= 120100 || __MAC_OS_X_VERSION_MIN_REQUIRED >= 101401 || __TV_OS_VERSION_MIN_REQUIRED >= 120100)

@class VanGuiObserver;
@class KeyEventResponder;

// Data
struct VanGui_ImplOSX_Data
{
    CFTimeInterval              Time;
    NSCursor*                   MouseCursors[VanGuiMouseCursor_COUNT];
    bool                        MouseCursorHidden;
    VanGuiObserver*              Observer;
    KeyEventResponder*          KeyEventResponder;
    NSTextInputContext*         InputContext;
    id                          Monitor;
    NSWindow*                   Window;

    VanGui_ImplOSX_Data()        { memset((void*)this, 0, sizeof(*this)); }
};

static VanGui_ImplOSX_Data*      VanGui_ImplOSX_GetBackendData()      { return (VanGui_ImplOSX_Data*)VanGui::GetIO().BackendPlatformUserData; }
static void                     VanGui_ImplOSX_DestroyBackendData()  { IM_DELETE(VanGui_ImplOSX_GetBackendData()); }

static inline CFTimeInterval    GetMachAbsoluteTimeInSeconds()      { return (CFTimeInterval)(double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e9); }

// Forward Declarations
static void VanGui_ImplOSX_AddTrackingArea(NSView* _Nonnull view);
static bool VanGui_ImplOSX_HandleEvent(NSEvent* event, NSView* view);

// Undocumented methods for creating cursors.
@interface NSCursor()
+ (id)_windowResizeNorthWestSouthEastCursor;
+ (id)_windowResizeNorthEastSouthWestCursor;
+ (id)_windowResizeNorthSouthCursor;
+ (id)_windowResizeEastWestCursor;
+ (id)busyButClickableCursor;
@end

/**
 KeyEventResponder implements the NSTextInputClient protocol as is required by the macOS text input manager.

 The macOS text input manager is invoked by calling the interpretKeyEvents method from the keyDown method.
 Keyboard events are then evaluated by the macOS input manager and valid text input is passed back via the
 insertText:replacementRange method.

 This is the same approach employed by other cross-platform libraries such as SDL2:
  https://github.com/spurious/SDL-mirror/blob/e17aacbd09e65a4fd1e166621e011e581fb017a8/src/video/cocoa/SDL_cocoakeyboard.m#L53
 and GLFW:
  https://github.com/glfw/glfw/blob/b55a517ae0c7b5127dffa79a64f5406021bf9076/src/cocoa_window.m#L722-L723
 */
@interface KeyEventResponder: NSView<NSTextInputClient>
@end

@implementation KeyEventResponder
{
    float _posX;
    float _posY;
    NSRect _imeRect;
}

#pragma mark - Public

- (void)setImePosX:(float)posX imePosY:(float)posY
{
    _posX = posX;
    _posY = posY;
}

- (void)updateImePosWithView:(NSView *)view
{
    NSWindow* window = view.window;
    if (!window)
        return;
    {
        NSRect contentRect = [window contentRectForFrameRect:window.frame];
        NSRect rect = NSMakeRect(_posX, contentRect.size.height - _posY, 0, 0);
        _imeRect = [window convertRectToScreen:rect];
    }
}

- (void)viewDidMoveToWindow
{
    // Ensure self is a first responder to receive the input events.
    [self.window makeFirstResponder:self];
}

- (void)keyDown:(NSEvent*)event
{
    if (!VanGui_ImplOSX_HandleEvent(event, self))
        [super keyDown:event];

    // Call to the macOS input manager system.
    [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent*)event
{
    if (!VanGui_ImplOSX_HandleEvent(event, self))
        [super keyUp:event];
}

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange
{
    VanGuiIO& io = VanGui::GetIO();

    NSString* characters;
    if ([aString isKindOfClass:[NSAttributedString class]])
        characters = [aString string];
    else
        characters = (NSString*)aString;

    io.AddInputCharactersUTF8(characters.UTF8String);
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)doCommandBySelector:(SEL)myselector
{
}

- (nullable NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange
{
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange
{
    return _imeRect;
}

- (BOOL)hasMarkedText
{
    return NO;
}

- (NSRange)markedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
    return NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(nonnull id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
}

- (void)unmarkText
{
}

- (nonnull NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{
    return @[];
}

@end

@interface VanGuiObserver : NSObject

- (void)onApplicationBecomeActive:(NSNotification*)aNotification;
- (void)onApplicationBecomeInactive:(NSNotification*)aNotification;

@end

@implementation VanGuiObserver

- (void)onApplicationBecomeActive:(NSNotification*)aNotification
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddFocusEvent(true);
}

- (void)onApplicationBecomeInactive:(NSNotification*)aNotification
{
    VanGuiIO& io = VanGui::GetIO();
    io.AddFocusEvent(false);
}

@end

// Functions

// Not static to allow third-party code to use that if they want to (but undocumented)
VanGuiKey VanGui_ImplOSX_KeyCodeToVanGuiKey(int key_code);
VanGuiKey VanGui_ImplOSX_KeyCodeToVanGuiKey(int key_code)
{
    switch (key_code)
    {
        case kVK_ANSI_A: return VanGuiKey_A;
        case kVK_ANSI_S: return VanGuiKey_S;
        case kVK_ANSI_D: return VanGuiKey_D;
        case kVK_ANSI_F: return VanGuiKey_F;
        case kVK_ANSI_H: return VanGuiKey_H;
        case kVK_ANSI_G: return VanGuiKey_G;
        case kVK_ANSI_Z: return VanGuiKey_Z;
        case kVK_ANSI_X: return VanGuiKey_X;
        case kVK_ANSI_C: return VanGuiKey_C;
        case kVK_ANSI_V: return VanGuiKey_V;
        case kVK_ANSI_B: return VanGuiKey_B;
        case kVK_ANSI_Q: return VanGuiKey_Q;
        case kVK_ANSI_W: return VanGuiKey_W;
        case kVK_ANSI_E: return VanGuiKey_E;
        case kVK_ANSI_R: return VanGuiKey_R;
        case kVK_ANSI_Y: return VanGuiKey_Y;
        case kVK_ANSI_T: return VanGuiKey_T;
        case kVK_ANSI_1: return VanGuiKey_1;
        case kVK_ANSI_2: return VanGuiKey_2;
        case kVK_ANSI_3: return VanGuiKey_3;
        case kVK_ANSI_4: return VanGuiKey_4;
        case kVK_ANSI_6: return VanGuiKey_6;
        case kVK_ANSI_5: return VanGuiKey_5;
        case kVK_ANSI_Equal: return VanGuiKey_Equal;
        case kVK_ANSI_9: return VanGuiKey_9;
        case kVK_ANSI_7: return VanGuiKey_7;
        case kVK_ANSI_Minus: return VanGuiKey_Minus;
        case kVK_ANSI_8: return VanGuiKey_8;
        case kVK_ANSI_0: return VanGuiKey_0;
        case kVK_ANSI_RightBracket: return VanGuiKey_RightBracket;
        case kVK_ANSI_O: return VanGuiKey_O;
        case kVK_ANSI_U: return VanGuiKey_U;
        case kVK_ANSI_LeftBracket: return VanGuiKey_LeftBracket;
        case kVK_ANSI_I: return VanGuiKey_I;
        case kVK_ANSI_P: return VanGuiKey_P;
        case kVK_ANSI_L: return VanGuiKey_L;
        case kVK_ANSI_J: return VanGuiKey_J;
        case kVK_ANSI_Quote: return VanGuiKey_Apostrophe;
        case kVK_ANSI_K: return VanGuiKey_K;
        case kVK_ANSI_Semicolon: return VanGuiKey_Semicolon;
        case kVK_ANSI_Backslash: return VanGuiKey_Backslash;
        case kVK_ANSI_Comma: return VanGuiKey_Comma;
        case kVK_ANSI_Slash: return VanGuiKey_Slash;
        case kVK_ANSI_N: return VanGuiKey_N;
        case kVK_ANSI_M: return VanGuiKey_M;
        case kVK_ANSI_Period: return VanGuiKey_Period;
        case kVK_ANSI_Grave: return VanGuiKey_GraveAccent;
        case kVK_ANSI_KeypadDecimal: return VanGuiKey_KeypadDecimal;
        case kVK_ANSI_KeypadMultiply: return VanGuiKey_KeypadMultiply;
        case kVK_ANSI_KeypadPlus: return VanGuiKey_KeypadAdd;
        case kVK_ANSI_KeypadClear: return VanGuiKey_NumLock;
        case kVK_ANSI_KeypadDivide: return VanGuiKey_KeypadDivide;
        case kVK_ANSI_KeypadEnter: return VanGuiKey_KeypadEnter;
        case kVK_ANSI_KeypadMinus: return VanGuiKey_KeypadSubtract;
        case kVK_ANSI_KeypadEquals: return VanGuiKey_KeypadEqual;
        case kVK_ANSI_Keypad0: return VanGuiKey_Keypad0;
        case kVK_ANSI_Keypad1: return VanGuiKey_Keypad1;
        case kVK_ANSI_Keypad2: return VanGuiKey_Keypad2;
        case kVK_ANSI_Keypad3: return VanGuiKey_Keypad3;
        case kVK_ANSI_Keypad4: return VanGuiKey_Keypad4;
        case kVK_ANSI_Keypad5: return VanGuiKey_Keypad5;
        case kVK_ANSI_Keypad6: return VanGuiKey_Keypad6;
        case kVK_ANSI_Keypad7: return VanGuiKey_Keypad7;
        case kVK_ANSI_Keypad8: return VanGuiKey_Keypad8;
        case kVK_ANSI_Keypad9: return VanGuiKey_Keypad9;
        case kVK_Return: return VanGuiKey_Enter;
        case kVK_Tab: return VanGuiKey_Tab;
        case kVK_Space: return VanGuiKey_Space;
        case kVK_Delete: return VanGuiKey_Backspace;
        case kVK_Escape: return VanGuiKey_Escape;
        case kVK_CapsLock: return VanGuiKey_CapsLock;
        case kVK_Control: return VanGuiKey_LeftCtrl;
        case kVK_Shift: return VanGuiKey_LeftShift;
        case kVK_Option: return VanGuiKey_LeftAlt;
        case kVK_Command: return VanGuiKey_LeftSuper;
        case kVK_RightControl: return VanGuiKey_RightCtrl;
        case kVK_RightShift: return VanGuiKey_RightShift;
        case kVK_RightOption: return VanGuiKey_RightAlt;
        case kVK_RightCommand: return VanGuiKey_RightSuper;
//      case kVK_Function: return VanGuiKey_;
//      case kVK_VolumeUp: return VanGuiKey_;
//      case kVK_VolumeDown: return VanGuiKey_;
//      case kVK_Mute: return VanGuiKey_;
        case kVK_F1: return VanGuiKey_F1;
        case kVK_F2: return VanGuiKey_F2;
        case kVK_F3: return VanGuiKey_F3;
        case kVK_F4: return VanGuiKey_F4;
        case kVK_F5: return VanGuiKey_F5;
        case kVK_F6: return VanGuiKey_F6;
        case kVK_F7: return VanGuiKey_F7;
        case kVK_F8: return VanGuiKey_F8;
        case kVK_F9: return VanGuiKey_F9;
        case kVK_F10: return VanGuiKey_F10;
        case kVK_F11: return VanGuiKey_F11;
        case kVK_F12: return VanGuiKey_F12;
        case kVK_F13: return VanGuiKey_F13;
        case kVK_F14: return VanGuiKey_F14;
        case kVK_F15: return VanGuiKey_F15;
        case kVK_F16: return VanGuiKey_F16;
        case kVK_F17: return VanGuiKey_F17;
        case kVK_F18: return VanGuiKey_F18;
        case kVK_F19: return VanGuiKey_F19;
        case kVK_F20: return VanGuiKey_F20;
        case 0x6E: return VanGuiKey_Menu;
        case kVK_Help: return VanGuiKey_Insert;
        case kVK_Home: return VanGuiKey_Home;
        case kVK_PageUp: return VanGuiKey_PageUp;
        case kVK_ForwardDelete: return VanGuiKey_Delete;
        case kVK_End: return VanGuiKey_End;
        case kVK_PageDown: return VanGuiKey_PageDown;
        case kVK_LeftArrow: return VanGuiKey_LeftArrow;
        case kVK_RightArrow: return VanGuiKey_RightArrow;
        case kVK_DownArrow: return VanGuiKey_DownArrow;
        case kVK_UpArrow: return VanGuiKey_UpArrow;
        default: return VanGuiKey_None;
    }
}

#ifdef VANGUI_IMPL_METAL_CPP_EXTENSIONS

VANGUI_IMPL_API bool VanGui_ImplOSX_Init(void* _Nonnull view) {
    return VanGui_ImplOSX_Init((__bridge NSView*)(view));
}

VANGUI_IMPL_API void VanGui_ImplOSX_NewFrame(void* _Nullable view) {
    return VanGui_ImplOSX_NewFrame((__bridge NSView*)(view));
}

#endif


bool VanGui_ImplOSX_Init(NSView* view)
{
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    VANGUI_CHECKVERSION();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    // Setup backend capabilities flags
    VanGui_ImplOSX_Data* bd = IM_NEW(VanGui_ImplOSX_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "vangui_impl_osx";
    io.BackendFlags |= VanGuiBackendFlags_HasMouseCursors;           // We can honor GetMouseCursor() values (optional)
    //io.BackendFlags |= VanGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    bd->Observer = [VanGuiObserver new];
    bd->Window = view.window ?: NSApp.orderedWindows.firstObject;
    VanGuiViewport* main_viewport = VanGui::GetMainViewport();
    main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (__bridge_retained void*)bd->Window;

    // Load cursors. Some of them are undocumented.
    bd->MouseCursorHidden = false;
    bd->MouseCursors[VanGuiMouseCursor_Arrow] = [NSCursor arrowCursor];
    bd->MouseCursors[VanGuiMouseCursor_TextInput] = [NSCursor IBeamCursor];
    bd->MouseCursors[VanGuiMouseCursor_ResizeAll] = [NSCursor closedHandCursor];
    bd->MouseCursors[VanGuiMouseCursor_ResizeNS] = [NSCursor respondsToSelector:@selector(_windowResizeNorthSouthCursor)] ? [NSCursor _windowResizeNorthSouthCursor] : [NSCursor resizeUpDownCursor];
    bd->MouseCursors[VanGuiMouseCursor_ResizeEW] = [NSCursor respondsToSelector:@selector(_windowResizeEastWestCursor)] ? [NSCursor _windowResizeEastWestCursor] : [NSCursor resizeLeftRightCursor];
    bd->MouseCursors[VanGuiMouseCursor_ResizeNESW] = [NSCursor respondsToSelector:@selector(_windowResizeNorthEastSouthWestCursor)] ? [NSCursor _windowResizeNorthEastSouthWestCursor] : [NSCursor closedHandCursor];
    bd->MouseCursors[VanGuiMouseCursor_ResizeNWSE] = [NSCursor respondsToSelector:@selector(_windowResizeNorthWestSouthEastCursor)] ? [NSCursor _windowResizeNorthWestSouthEastCursor] : [NSCursor closedHandCursor];
    bd->MouseCursors[VanGuiMouseCursor_Hand] = [NSCursor pointingHandCursor];
    bd->MouseCursors[VanGuiMouseCursor_Wait] = bd->MouseCursors[VanGuiMouseCursor_Progress] = [NSCursor respondsToSelector:@selector(busyButClickableCursor)] ? [NSCursor busyButClickableCursor] : [NSCursor arrowCursor];
    bd->MouseCursors[VanGuiMouseCursor_NotAllowed] = [NSCursor operationNotAllowedCursor];

    // Note that vangui.cpp also include default OSX clipboard handlers which can be enabled
    // by adding '#define VANGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS' in imconfig.h and adding '-framework ApplicationServices' to your linker command-line.
    // Since we are already in ObjC land here, it is easy for us to add a clipboard handler using the NSPasteboard api.
    platform_io.Platform_SetClipboardTextFn = [](VanGuiContext*, const char* str) -> void
    {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard declareTypes:[NSArray arrayWithObject:NSPasteboardTypeString] owner:nil];
        [pasteboard setString:[NSString stringWithUTF8String:str] forType:NSPasteboardTypeString];
    };

    platform_io.Platform_GetClipboardTextFn = [](VanGuiContext*) -> const char*
    {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSString* available = [pasteboard availableTypeFromArray: [NSArray arrayWithObject:NSPasteboardTypeString]];
        if (![available isEqualToString:NSPasteboardTypeString])
            return nullptr;

        NSString* string = [pasteboard stringForType:NSPasteboardTypeString];
        if (string == nil)
            return nullptr;

        const char* string_c = (const char*)[string UTF8String];
        size_t string_len = strlen(string_c);
        static ImVector<char> s_clipboard;
        s_clipboard.resize((int)string_len + 1);
        strcpy(s_clipboard.Data, string_c);
        return s_clipboard.Data;
    };

    [[NSNotificationCenter defaultCenter] addObserver:bd->Observer
                                             selector:@selector(onApplicationBecomeActive:)
                                                 name:NSApplicationDidBecomeActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:bd->Observer
                                             selector:@selector(onApplicationBecomeInactive:)
                                                 name:NSApplicationDidResignActiveNotification
                                               object:nil];

    // Add the NSTextInputClient to the view hierarchy,
    // to receive keyboard events and translate them to input text.
    bd->KeyEventResponder = [[KeyEventResponder alloc] initWithFrame:NSZeroRect];
    bd->InputContext = [[NSTextInputContext alloc] initWithClient:bd->KeyEventResponder];
    [view addSubview:bd->KeyEventResponder];
    VanGui_ImplOSX_AddTrackingArea(view);

    platform_io.Platform_SetImeDataFn = [](VanGuiContext*, VanGuiViewport*, VanGuiPlatformImeData* data) -> void
    {
        VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
        if (data->WantVisible)
        {
            [bd->InputContext activate];
        }
        else
        {
            [bd->InputContext discardMarkedText];
            [bd->InputContext invalidateCharacterCoordinates];
            [bd->InputContext deactivate];
        }
        [bd->KeyEventResponder setImePosX:data->InputPos.x imePosY:data->InputPos.y + data->InputLineHeight];
    };

    return true;
}

void VanGui_ImplOSX_Shutdown()
{
    VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");

    [[NSNotificationCenter defaultCenter] removeObserver:bd->Observer];
    bd->Observer = nullptr;
    if (bd->Monitor != nullptr)
    {
        [NSEvent removeMonitor:bd->Monitor];
        bd->Monitor = nullptr;
    }

    VanGui_ImplOSX_DestroyBackendData();

    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_HasMouseCursors | VanGuiBackendFlags_HasGamepad);
    platform_io.ClearPlatformHandlers();
}

static void VanGui_ImplOSX_UpdateMouseCursor()
{
    VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();
    if (io.ConfigFlags & VanGuiConfigFlags_NoMouseCursorChange)
        return;

    VanGuiMouseCursor vangui_cursor = VanGui::GetMouseCursor();
    if (io.MouseDrawCursor || vangui_cursor == VanGuiMouseCursor_None)
    {
        // Hide OS mouse cursor if vangui is drawing it or if it wants no cursor
        if (!bd->MouseCursorHidden)
        {
            bd->MouseCursorHidden = true;
            [NSCursor hide];
        }
    }
    else
    {
        NSCursor* desired = bd->MouseCursors[vangui_cursor] ?: bd->MouseCursors[VanGuiMouseCursor_Arrow];
        // -[NSCursor set] generates measurable overhead if called unconditionally.
        if (desired != NSCursor.currentCursor)
        {
            [desired set];
        }
        if (bd->MouseCursorHidden)
        {
            bd->MouseCursorHidden = false;
            [NSCursor unhide];
        }
    }
}

static void VanGui_ImplOSX_UpdateGamepads()
{
    VanGuiIO& io = VanGui::GetIO();

#if APPLE_HAS_CONTROLLER
    GCController* controller = GCController.current;
#else
    GCController* controller = GCController.controllers.firstObject;
#endif
    if (controller == nil || controller.extendedGamepad == nil)
    {
        io.BackendFlags &= ~VanGuiBackendFlags_HasGamepad;
        return;
    }

    GCExtendedGamepad* gp = controller.extendedGamepad;

    // Update gamepad inputs
    #define IM_SATURATE(V)                        (V < 0.0f ? 0.0f : V > 1.0f ? 1.0f : V)
    #define MAP_BUTTON(KEY_NO, BUTTON_NAME)       { io.AddKeyEvent(KEY_NO, gp.BUTTON_NAME.isPressed); }
    #define MAP_ANALOG(KEY_NO, AXIS_NAME, V0, V1) { float vn = (float)(gp.AXIS_NAME.value - V0) / (float)(V1 - V0); vn = IM_SATURATE(vn); io.AddKeyAnalogEvent(KEY_NO, vn > 0.1f, vn); }
    const float thumb_dead_zone = 0.0f;

#if APPLE_HAS_BUTTON_OPTIONS
    MAP_BUTTON(VanGuiKey_GamepadBack,            buttonOptions);
#endif
    MAP_BUTTON(VanGuiKey_GamepadFaceLeft,        buttonX);              // Xbox X, PS Square
    MAP_BUTTON(VanGuiKey_GamepadFaceRight,       buttonB);              // Xbox B, PS Circle
    MAP_BUTTON(VanGuiKey_GamepadFaceUp,          buttonY);              // Xbox Y, PS Triangle
    MAP_BUTTON(VanGuiKey_GamepadFaceDown,        buttonA);              // Xbox A, PS Cross
    MAP_BUTTON(VanGuiKey_GamepadDpadLeft,        dpad.left);
    MAP_BUTTON(VanGuiKey_GamepadDpadRight,       dpad.right);
    MAP_BUTTON(VanGuiKey_GamepadDpadUp,          dpad.up);
    MAP_BUTTON(VanGuiKey_GamepadDpadDown,        dpad.down);
    MAP_ANALOG(VanGuiKey_GamepadL1,              leftShoulder, 0.0f, 1.0f);
    MAP_ANALOG(VanGuiKey_GamepadR1,              rightShoulder, 0.0f, 1.0f);
    MAP_ANALOG(VanGuiKey_GamepadL2,              leftTrigger,  0.0f, 1.0f);
    MAP_ANALOG(VanGuiKey_GamepadR2,              rightTrigger, 0.0f, 1.0f);
#if APPLE_HAS_THUMBSTICKS
    MAP_BUTTON(VanGuiKey_GamepadL3,              leftThumbstickButton);
    MAP_BUTTON(VanGuiKey_GamepadR3,              rightThumbstickButton);
#endif
    MAP_ANALOG(VanGuiKey_GamepadLStickLeft,      leftThumbstick.xAxis,  -thumb_dead_zone, -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickRight,     leftThumbstick.xAxis,  +thumb_dead_zone, +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickUp,        leftThumbstick.yAxis,  +thumb_dead_zone, +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadLStickDown,      leftThumbstick.yAxis,  -thumb_dead_zone, -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickLeft,      rightThumbstick.xAxis, -thumb_dead_zone, -1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickRight,     rightThumbstick.xAxis, +thumb_dead_zone, +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickUp,        rightThumbstick.yAxis, +thumb_dead_zone, +1.0f);
    MAP_ANALOG(VanGuiKey_GamepadRStickDown,      rightThumbstick.yAxis, -thumb_dead_zone, -1.0f);
    #undef MAP_BUTTON
    #undef MAP_ANALOG

    io.BackendFlags |= VanGuiBackendFlags_HasGamepad;
}

static void VanGui_ImplOSX_UpdateImePosWithView(NSView* view)
{
    VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
    VanGuiIO& io = VanGui::GetIO();
    if (io.WantTextInput)
        [bd->KeyEventResponder updateImePosWithView:view];
}

void VanGui_ImplOSX_NewFrame(NSView* view)
{
    VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplOSX_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    // Setup display size
    if (view)
    {
        const float fb_scale = (float)[view.window backingScaleFactor];
        io.DisplaySize = ImVec2((float)view.bounds.size.width, (float)view.bounds.size.height);
        io.DisplayFramebufferScale = ImVec2(fb_scale, fb_scale);
    }

    // Setup time step
    if (bd->Time == 0.0)
        bd->Time = GetMachAbsoluteTimeInSeconds();

    double current_time = GetMachAbsoluteTimeInSeconds();
    io.DeltaTime = (float)(current_time - bd->Time);
    bd->Time = current_time;

    VanGui_ImplOSX_UpdateMouseCursor();
    VanGui_ImplOSX_UpdateGamepads();
    VanGui_ImplOSX_UpdateImePosWithView(view);
}

// Must only be called for a mouse event, otherwise an exception occurs
// (Note that NSEventTypeScrollWheel is considered "other input". Oddly enough an exception does not occur with it, but the value will sometimes be wrong!)
static VanGuiMouseSource GetMouseSource(NSEvent* event)
{
    switch (event.subtype)
    {
        case NSEventSubtypeTabletPoint:
            return VanGuiMouseSource_Pen;
        // macOS considers input from relative touch devices (like the trackpad or Apple Magic Mouse) to be touch input.
        // This doesn't really make sense for VanGUI, which expects absolute touch devices only.
        // There does not seem to be a simple way to disambiguate things here so we consider NSEventSubtypeTouch events to always come from mice.
        // See https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/HandlingTouchEvents/HandlingTouchEvents.html#//apple_ref/doc/uid/10000060i-CH13-SW24
        //case NSEventSubtypeTouch:
        //    return VanGuiMouseSource_TouchScreen;
        case NSEventSubtypeMouseEvent:
        default:
            return VanGuiMouseSource_Mouse;
    }
}

static bool VanGui_ImplOSX_HandleEvent(NSEvent* event, NSView* view)
{
    // Only process events from the window containing VanGui view
    if (event.window != view.window)
        return false;
    VanGuiIO& io = VanGui::GetIO();

    if (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown)
    {
        int button = (int)[event buttonNumber];
        if (button >= 0 && button < VanGuiMouseButton_COUNT)
        {
            io.AddMouseSourceEvent(GetMouseSource(event));
            io.AddMouseButtonEvent(button, true);
        }
        return io.WantCaptureMouse;
    }

    if (event.type == NSEventTypeLeftMouseUp || event.type == NSEventTypeRightMouseUp || event.type == NSEventTypeOtherMouseUp)
    {
        int button = (int)[event buttonNumber];
        if (button >= 0 && button < VanGuiMouseButton_COUNT)
        {
            io.AddMouseSourceEvent(GetMouseSource(event));
            io.AddMouseButtonEvent(button, false);
        }
        return io.WantCaptureMouse;
    }

    if (event.type == NSEventTypeMouseMoved || event.type == NSEventTypeLeftMouseDragged || event.type == NSEventTypeRightMouseDragged || event.type == NSEventTypeOtherMouseDragged)
    {
        NSPoint mousePoint = event.locationInWindow;
        if (event.window == nil)
            mousePoint = [[view window] convertPointFromScreen:mousePoint];
        mousePoint = [view convertPoint:mousePoint fromView:nil];
        if ([view isFlipped])
            mousePoint = NSMakePoint(mousePoint.x, mousePoint.y);
        else
            mousePoint = NSMakePoint(mousePoint.x, view.bounds.size.height - mousePoint.y);
        io.AddMouseSourceEvent(GetMouseSource(event));
        io.AddMousePosEvent((float)mousePoint.x, (float)mousePoint.y);
        return io.WantCaptureMouse;
    }

    if (event.type == NSEventTypeScrollWheel)
    {
        // Ignore canceled events.
        //
        // From macOS 12.1, scrolling with two fingers and then decelerating
        // by tapping two fingers results in two events appearing:
        //
        // 1. A scroll wheel NSEvent, with a phase == NSEventPhaseMayBegin, when the user taps
        // two fingers to decelerate or stop the scroll events.
        //
        // 2. A scroll wheel NSEvent, with a phase == NSEventPhaseCancelled, when the user releases the
        // two-finger tap. It is this event that sometimes contains large values for scrollingDeltaX and
        // scrollingDeltaY. When these are added to the current x and y positions of the scrolling view,
        // it appears to jump up or down. It can be observed in Preview, various JetBrains IDEs and here.
        if (event.phase == NSEventPhaseCancelled)
            return false;

        double wheel_dx = 0.0;
        double wheel_dy = 0.0;

        #if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
        if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_6)
        {
            wheel_dx = [event scrollingDeltaX];
            wheel_dy = [event scrollingDeltaY];
            if ([event hasPreciseScrollingDeltas])
            {
                wheel_dx *= 0.01;
                wheel_dy *= 0.01;
            }
        }
        else
        #endif // MAC_OS_X_VERSION_MAX_ALLOWED
        {
            wheel_dx = [event deltaX] * 0.1;
            wheel_dy = [event deltaY] * 0.1;
        }
        if (wheel_dx != 0.0 || wheel_dy != 0.0)
            io.AddMouseWheelEvent((float)wheel_dx, (float)wheel_dy);

        return io.WantCaptureMouse;
    }

    if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp)
    {
        if ([event isARepeat])
            return io.WantCaptureKeyboard;

        int key_code = (int)[event keyCode];
        VanGuiKey key = VanGui_ImplOSX_KeyCodeToVanGuiKey(key_code);
        io.AddKeyEvent(key, event.type == NSEventTypeKeyDown);
        io.SetKeyEventNativeData(key, key_code, -1); // To support legacy indexing (<1.87 user code)

        return io.WantCaptureKeyboard;
    }

    if (event.type == NSEventTypeFlagsChanged)
    {
        unsigned short key_code = [event keyCode];
        NSEventModifierFlags modifier_flags = [event modifierFlags];

        io.AddKeyEvent(VanGuiMod_Shift, (modifier_flags & NSEventModifierFlagShift)   != 0);
        io.AddKeyEvent(VanGuiMod_Ctrl,  (modifier_flags & NSEventModifierFlagControl) != 0);
        io.AddKeyEvent(VanGuiMod_Alt,   (modifier_flags & NSEventModifierFlagOption)  != 0);
        io.AddKeyEvent(VanGuiMod_Super, (modifier_flags & NSEventModifierFlagCommand) != 0);

        VanGuiKey key = VanGui_ImplOSX_KeyCodeToVanGuiKey(key_code);
        if (key != VanGuiKey_None)
        {
            // macOS does not generate down/up event for modifiers. We're trying
            // to use hardware dependent masks to extract that information.
            // 'vangui_mask' is left as a fallback.
            NSEventModifierFlags mask = 0;
            switch (key)
            {
                case VanGuiKey_LeftCtrl:   mask = 0x0001; break;
                case VanGuiKey_RightCtrl:  mask = 0x2000; break;
                case VanGuiKey_LeftShift:  mask = 0x0002; break;
                case VanGuiKey_RightShift: mask = 0x0004; break;
                case VanGuiKey_LeftSuper:  mask = 0x0008; break;
                case VanGuiKey_RightSuper: mask = 0x0010; break;
                case VanGuiKey_LeftAlt:    mask = 0x0020; break;
                case VanGuiKey_RightAlt:   mask = 0x0040; break;
                default:
                    return io.WantCaptureKeyboard;
            }
            io.AddKeyEvent(key, (modifier_flags & mask) != 0);
            io.SetKeyEventNativeData(key, key_code, -1); // To support legacy indexing (<1.87 user code)
        }

        return io.WantCaptureKeyboard;
    }

    return false;
}

static void VanGui_ImplOSX_AddTrackingArea(NSView* _Nonnull view)
{
    // If we want to receive key events, we either need to be in the responder chain of the key view,
    // or else we can install a local monitor. The consequence of this heavy-handed approach is that
    // we receive events for all controls, not just VanGUI widgets. If we had native controls in our
    // window, we'd want to be much more careful than just ingesting the complete event stream.
    // To match the behavior of other backends, we pass every event down to the OS.
    VanGui_ImplOSX_Data* bd = VanGui_ImplOSX_GetBackendData();
    if (bd->Monitor)
        return;
    NSEventMask eventMask = 0;
    eventMask |= NSEventMaskMouseMoved | NSEventMaskScrollWheel;
    eventMask |= NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged;
    eventMask |= NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged;
    eventMask |= NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskOtherMouseDragged;
    eventMask |= NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged;
    bd->Monitor = [NSEvent addLocalMonitorForEventsMatchingMask:eventMask
                                                        handler:^NSEvent* _Nullable(NSEvent* event)
    {
        VanGui_ImplOSX_HandleEvent(event, view);
        return event;
    }];
}

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
