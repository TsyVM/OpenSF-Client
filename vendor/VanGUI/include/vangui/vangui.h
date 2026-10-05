// VanGUI, v1.92.9 WIP
// (headers)

// Help:
// - Call and read VanGui::ShowDemoWindow() in vangui_demo.cpp. All applications in examples/ are doing that.
// - Read top of vangui.cpp for more details, links and comments.
// - Add '#define VANGUI_DEFINE_MATH_OPERATORS' before including vangui.h (or in vanconfig.h) to access courtesy maths operators for VanVec2 and VanVec4.

// Resources:
// - FAQ ........................ docs/FAQ.md

// EVERYTHING ELSE should be asked in 'Issues'! We are building a database of cross-linked knowledge there.
// Since 1.92, we encourage font loading questions to also be posted in 'Issues'.

// Library Version
//
// Two spellings of the same thing, and which one you need depends on where you
// are asking. VANGUI_VERSION / VANGUI_VERSION_NUM are constexpr, so they are
// typed and usable anywhere a C++ value is -- but a constexpr is invisible to
// the preprocessor. `#if VANGUI_VERSION_NUM >= 19263` does not fail: it sees an
// undefined identifier, evaluates it as 0, and silently takes the false branch.
// That is how vangui_demo.cpp lost its demo markers.
//
// So the _PP forms exist for #if, and are defined here beside the constexpr
// ones so the two cannot drift.
inline constexpr const char* VANGUI_VERSION     = "1.92.9 WIP";
inline constexpr int         VANGUI_VERSION_NUM = 19281;

#define VANGUI_VERSION_PP       "1.92.9 WIP"
#define VANGUI_VERSION_NUM_PP   19281
#define VANGUI_HAS_TABLE             // Added BeginTable() - from VANGUI_VERSION_NUM >= 18000
#define VANGUI_HAS_TEXTURES          // Added VanGuiBackendFlags_RendererHasTextures - from VANGUI_VERSION_NUM >= 19198

/*

Index of this file:
// [SECTION] Header mess
// [SECTION] Forward declarations and basic types
// [SECTION] Texture identifiers (VanTextureID, VanTextureRef)
// [SECTION] VanGUI end-user API functions
// [SECTION] Flags & Enumerations
// [SECTION] Tables API flags and structures (VanGuiTableFlags, VanGuiTableColumnFlags, VanGuiTableRowFlags, VanGuiTableBgTarget, VanGuiTableSortSpecs, VanGuiTableColumnSortSpecs)
// [SECTION] Helpers: Debug log, Memory allocations macros, VanVector<>
// [SECTION] VanGuiStyle
// [SECTION] VanGuiIO
// [SECTION] Misc data structures (VanGuiInputTextCallbackData, VanGuiSizeCallbackData, VanGuiPayload)
// [SECTION] Helpers (VanGuiOnceUponAFrame, VanGuiTextFilter, VanGuiTextBuffer, VanGuiStorage, VanGuiListClipper, Math Operators, VanColor)
// [SECTION] Multi-Select API flags and structures (VanGuiMultiSelectFlags, VanGuiMultiSelectIO, VanGuiSelectionRequest, VanGuiSelectionBasicStorage, VanGuiSelectionExternalStorage)
// [SECTION] Drawing API (VanDrawCallback, VanDrawCmd, VanDrawIdx, VanDrawVert, VanDrawChannel, VanDrawListSplitter, VanDrawFlags, VanDrawListFlags, VanDrawList, VanDrawData)
// [SECTION] Texture API (VanTextureFormat, VanTextureStatus, VanTextureRect, VanTextureData)
// [SECTION] Font API (VanFontConfig, VanFontGlyph, VanFontGlyphRangesBuilder, VanFontAtlasFlags, VanFontAtlas, VanFontBaked, VanFont)
// [SECTION] Viewports (VanGuiViewportFlags, VanGuiViewport)
// [SECTION] VanGuiPlatformIO + other Platform Dependent Interfaces (VanGuiPlatformImeData)
// [SECTION] Obsolete functions and types

*/

#pragma once

// Configuration file with compile-time options
// (edit vanconfig.h or '#define VANGUI_USER_CONFIG "myfilename.h" from your build system)
#ifdef VANGUI_USER_CONFIG
#include VANGUI_USER_CONFIG
#endif
#include "vanconfig.h"

#ifndef VANGUI_DISABLE

//-----------------------------------------------------------------------------
// [SECTION] Header mess
//-----------------------------------------------------------------------------

// Includes
#include <algorithm>               // std::min, std::max, std::clamp, std::swap
#include <bit>                     // std::has_single_bit, std::popcount
#include <cfloat>                  // FLT_MIN, FLT_MAX
#include <cstdarg>                 // va_list, va_start, va_end
#include <cstddef>                 // ptrdiff_t
#include <cstdint>                 // uint8_t, int32_t, etc.
#include <cstring>                 // memset, memmove, memcpy, strlen, strchr, strcpy, strcmp
#include <numbers>                 // std::numbers::pi_v
#include <span>                    // std::span
#include <string_view>             // std::string_view

// Define attributes of all API symbols declarations (e.g. for DLL under Windows)
// VANGUI_API is used for core vangui functions, VANGUI_IMPL_API is used for the default backends files (vangui_impl_xxx.h)
// Using VanGUI via a shared library is not recommended: we don't guarantee backward nor forward ABI compatibility + this is a call-heavy library and function call overhead adds up.
#ifndef VANGUI_API
#define VANGUI_API
#endif
#ifndef VANGUI_IMPL_API
#define VANGUI_IMPL_API              VANGUI_API
#endif

// Helper Macros
// (note: compiling with NDEBUG will usually strip out assert() to nothing, which is NOT recommended because we use asserts to notify of programmer mistakes.)
#ifndef VAN_ASSERT
#include <cassert>
#define VAN_ASSERT(_EXPR)            assert(_EXPR)                               // You can override the default assert handler by editing vanconfig.h
#endif
#define VAN_COUNTOF(_ARR)            ((int)(sizeof(_ARR) / sizeof(*(_ARR))))     // Size of a static C-style array. Don't use on pointers!
#define VAN_UNUSED(_VAR)             ((void)(_VAR))                              // Used to silence "unused variable warnings". Often useful as asserts may be stripped out from final builds.
#define VAN_STRINGIFY_HELPER(_EXPR)  #_EXPR
#define VAN_STRINGIFY(_EXPR)         VAN_STRINGIFY_HELPER(_EXPR)                  // Preprocessor idiom to stringify e.g. an integer or a macro.

// Check that version and structures layouts are matching between compiled vangui code and caller. Read comments above DebugCheckVersionAndDataLayout() for details.
#define VANGUI_CHECKVERSION()        (void)VanGui::DebugCheckVersionAndDataLayout(VANGUI_VERSION, sizeof(VanGuiIO), sizeof(VanGuiStyle), sizeof(VanVec2), sizeof(VanVec4), sizeof(VanDrawVert), sizeof(VanDrawIdx))   // the check asserts on a mismatch; its result is informational

// Helper Macros - VAN_FMTARGS, VAN_FMTLIST: Apply printf-style warnings to our formatting functions.
// (MSVC provides an equivalent mechanism via SAL Annotations but it requires the macros in a different
//  location. e.g. #include <sal.h> + void myprintf(_Printf_format_string_ const char* format, ...),
//  and only works when using Code Analysis, rather than just normal compiling).
#if !defined(VANGUI_USE_STB_SPRINTF) && defined(__MINGW32__) && !defined(__clang__)
#define VAN_FMTARGS(FMT)             __attribute__((format(gnu_printf, FMT, FMT+1)))
#define VAN_FMTLIST(FMT)             __attribute__((format(gnu_printf, FMT, 0)))
#elif !defined(VANGUI_USE_STB_SPRINTF) && (defined(__clang__) || defined(__GNUC__))
#define VAN_FMTARGS(FMT)             __attribute__((format(printf, FMT, FMT+1)))
#define VAN_FMTLIST(FMT)             __attribute__((format(printf, FMT, 0)))
#else
#define VAN_FMTARGS(FMT)
#define VAN_FMTLIST(FMT)
#endif

// Disable some of MSVC most aggressive Debug runtime checks in function header/footer (used in some simple/low-level functions)
#if defined(_MSC_VER) && !defined(__clang__)  && !defined(__INTEL_COMPILER) && !defined(VANGUI_DEBUG_PARANOID)
#define VAN_MSVC_RUNTIME_CHECKS_OFF      __pragma(runtime_checks("",off))     __pragma(check_stack(off)) __pragma(strict_gs_check(push,off))
#define VAN_MSVC_RUNTIME_CHECKS_RESTORE  __pragma(runtime_checks("",restore)) __pragma(check_stack())    __pragma(strict_gs_check(pop))
#else
#define VAN_MSVC_RUNTIME_CHECKS_OFF
#define VAN_MSVC_RUNTIME_CHECKS_RESTORE
#endif

// Warnings
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 26495)    // [Static Analyzer] Variable 'XXX' is uninitialized. Always initialize a member variable (type.6).
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#if __has_warning("-Wunknown-warning-option")
#pragma clang diagnostic ignored "-Wunknown-warning-option"         // warning: unknown warning group 'xxx'
#endif
#pragma clang diagnostic ignored "-Wunknown-pragmas"                // warning: unknown warning group 'xxx'
#pragma clang diagnostic ignored "-Wold-style-cast"                 // warning: use of old-style cast
#pragma clang diagnostic ignored "-Wfloat-equal"                    // warning: comparing floating point with == or != is unsafe
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"  // warning: zero as null pointer constant
#pragma clang diagnostic ignored "-Wreserved-identifier"            // warning: identifier '_Xxx' is reserved because it starts with '_' followed by a capital letter
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"            // warning: 'xxx' is an unsafe pointer used for buffer access
#pragma clang diagnostic ignored "-Wnontrivial-memaccess"           // warning: first argument in call to 'memset' is a pointer to non-trivially copyable type
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"                          // warning: unknown option after '#pragma GCC diagnostic' kind
#pragma GCC diagnostic ignored "-Wfloat-equal"                      // warning: comparing floating-point with '==' or '!=' is unsafe
#pragma GCC diagnostic ignored "-Wclass-memaccess"                  // [__GNUC__ >= 8] warning: 'memset/memcpy' clearing/writing an object of type 'xxxx' with no trivial copy-assignment; use assignment or value-initialization instead
#endif

//-----------------------------------------------------------------------------
// [SECTION] Forward declarations and basic types
//-----------------------------------------------------------------------------

// Scalar data types
using VanGuiID = unsigned int;      // A unique ID used by widgets (typically the result of hashing a stack of string)
using VanS8    = int8_t;            // 8-bit signed integer
using VanU8    = uint8_t;           // 8-bit unsigned integer
using VanS16   = int16_t;           // 16-bit signed integer
using VanU16   = uint16_t;          // 16-bit unsigned integer
using VanS32   = int32_t;           // 32-bit signed integer
using VanU32   = uint32_t;          // 32-bit unsigned integer (often used to store packed colors)
using VanS64   = int64_t;           // 64-bit signed integer
using VanU64   = uint64_t;          // 64-bit unsigned integer

// Forward declarations: VanDrawList, VanFontAtlas layer
struct VanDrawChannel;               // Temporary storage to output draw commands out of order, used by VanDrawListSplitter and VanDrawList::ChannelsSplit()
struct VanDrawCmd;                   // A single draw command within a parent VanDrawList (generally maps to 1 GPU draw call, unless it is a callback)
struct VanDrawData;                  // All draw command lists required to render the frame + pos/size coordinates to use for the projection matrix.
struct VanDrawList;                  // A single draw command list (generally one per window, conceptually you may see this as a dynamic "mesh" builder)
struct VanDrawListSharedData;        // Data shared among multiple draw lists (typically owned by parent VanGui context, but you may create one yourself)
struct VanDrawListSplitter;          // Helper to split a draw list into different layers which can be drawn into out of order, then flattened back.
struct VanDrawVert;                  // A single vertex (pos + uv + col = 20 bytes by default. Override layout with VANGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT)
struct VanFont;                      // Runtime data for a single font within a parent VanFontAtlas
struct VanFontAtlas;                 // Runtime data for multiple fonts, bake multiple fonts into a single texture, TTF/OTF font loader
struct VanFontAtlasBuilder;          // Opaque storage for building a VanFontAtlas
struct VanFontAtlasRect;             // Output of VanFontAtlas::GetCustomRect() when using custom rectangles.
struct VanFontBaked;                 // Baked data for a VanFont at a given size.
struct VanFontConfig;                // Configuration data when adding a font or merging fonts
struct VanFontGlyph;                 // A single font glyph (code point + coordinates within in VanFontAtlas + offset)
struct VanFontGlyphRangesBuilder;    // Helper to build glyph ranges from text/string data
struct VanFontLoader;                // Opaque interface to a font loading backend (stb_truetype, FreeType etc.).
struct VanTextureData;               // Specs and pixel storage for a texture used by VanGUI.
struct VanTextureRect;               // Coordinates of a rectangle within a texture.
struct VanColor;                     // Helper functions to create a color that can be converted to either u32 or float4 (*OBSOLETE* please avoid using)

// Forward declarations: VanGui layer
struct VanGuiContext;                // VanGUI context (opaque structure, unless including vangui_internal.h)
struct VanGuiIO;                     // Main configuration and I/O between your application and VanGui (also see: VanGuiPlatformIO)
struct VanGuiInputTextCallbackData;  // Shared state of InputText() when using custom VanGuiInputTextCallback (rare/advanced use)
struct VanGuiKeyData;                // Storage for VanGuiIO and IsKeyDown(), IsKeyPressed() etc functions.
struct VanGuiListClipper;            // Helper to manually clip large list of items
struct VanGuiMultiSelectIO;          // Structure to interact with a BeginMultiSelect()/EndMultiSelect() block
struct VanGuiOnceUponAFrame;         // Helper for running a block of code not more than once a frame
struct VanGuiPayload;                // User data payload for drag and drop operations
struct VanGuiPlatformIO;             // Interface between platform/renderer backends and VanGui (e.g. Clipboard, IME hooks). Extends VanGuiIO. In docking branch, this gets extended to support multi-viewports.
struct VanGuiPlatformImeData;        // Platform IME data for io.PlatformSetImeDataFn() function.
struct VanGuiPlatformMonitor;        // Multi-viewport support: interface for a monitor/display.
struct VanGuiSelectionBasicStorage;  // Optional helper to store multi-selection state + apply multi-selection requests.
struct VanGuiSelectionExternalStorage;//Optional helper to apply multi-selection requests to existing randomly accessible storage.
struct VanGuiSelectionRequest;       // A selection request (stored in VanGuiMultiSelectIO)
struct VanGuiSizeCallbackData;       // Callback data when using SetNextWindowSizeConstraints() (rare/advanced use)
struct VanGuiStorage;                // Helper for key->value storage (container sorted by key)
struct VanGuiStoragePair;            // Helper for key->value storage (pair)
struct VanGuiStyle;                  // Runtime data for styling/colors
struct VanGuiTableSortSpecs;         // Sorting specifications for a table (often handling sort specs for a single column, occasionally more)
struct VanGuiTableColumnSortSpecs;   // Sorting specification for one column of a table
struct VanGuiTextBuffer;             // Helper to hold and append into a text buffer (~string builder)
struct VanGuiTextFilter;             // Helper to parse and apply text filters (e.g. "aaaaa[,bbbbb][,ccccc]")
struct VanGuiViewport;               // A Platform Window (always only one in 'master' branch), in the future may represent Platform Monitor
struct VanGuiWindowClass;            // Window class (rare/advanced uses: provide hints to the platform backend via altered viewport flags and parent/child info)

// Enumerations
// - We don't use strongly typed enums much because they add constraints (can't extend in private code, can't store typed in bit fields, extra casting on iteration)
// - Tip: Use your programming IDE navigation facilities on the names in the _central column_ below to find the actual flags/enum lists!
//   - In Visual Studio: Ctrl+Comma ("Edit.GoToAll") can follow symbols inside comments, whereas Ctrl+F12 ("Edit.GoToImplementation") cannot.
//   - In Visual Studio w/ Visual Assist installed: Alt+G ("VAssistX.GoToImplementation") can also follow symbols inside comments.
//   - In VS Code, CLion, etc.: Ctrl+Click can follow symbols inside comments.
enum VanGuiDir : int;                // -> enum VanGuiDir              // Enum: A cardinal direction (Left, Right, Up, Down)
enum VanGuiKey : int;                // -> enum VanGuiKey              // Enum: A key identifier (VanGuiKey_XXX or VanGuiMod_XXX value)
enum VanGuiMouseSource : int;        // -> enum VanGuiMouseSource      // Enum; A mouse input source identifier (Mouse, TouchScreen, Pen)
enum VanGuiSortDirection : VanU8;     // -> enum VanGuiSortDirection    // Enum: A sorting direction (ascending or descending)
using VanGuiCol          = int;     // -> enum VanGuiCol_             // Enum: A color identifier for styling
using VanGuiCond         = int;     // -> enum VanGuiCond_            // Enum: A condition for many Set*() functions
using VanGuiDataType     = int;     // -> enum VanGuiDataType_        // Enum: A primary data type
using VanGuiMouseButton  = int;     // -> enum VanGuiMouseButton_     // Enum: A mouse button identifier (0=left, 1=right, 2=middle)
using VanGuiMouseCursor  = int;     // -> enum VanGuiMouseCursor_     // Enum: A mouse cursor shape
using VanGuiStyleVar     = int;     // -> enum VanGuiStyleVar_        // Enum: A variable identifier for styling
using VanGuiTableBgTarget = int;    // -> enum VanGuiTableBgTarget_   // Enum: A color target for TableSetBgColor()

// Flags (declared as int to allow using as flags without overhead, and to not pollute the top of this file)
// - Tip: Use your programming IDE navigation facilities on the names in the _central column_ below to find the actual flags/enum lists!
//   - In Visual Studio: Ctrl+Comma ("Edit.GoToAll") can follow symbols inside comments, whereas Ctrl+F12 ("Edit.GoToImplementation") cannot.
//   - In Visual Studio w/ Visual Assist installed: Alt+G ("VAssistX.GoToImplementation") can also follow symbols inside comments.
//   - In VS Code, CLion, etc.: Ctrl+Click can follow symbols inside comments.
using VanDrawFlags           = int; // -> enum VanDrawFlags_          // Flags: for VanDrawList functions
using VanDrawListFlags       = int; // -> enum VanDrawListFlags_      // Flags: for VanDrawList instance
using VanDrawTextFlags       = int; // -> enum VanDrawTextFlags_      // Internal, do not use!
using VanFontFlags           = int; // -> enum VanFontFlags_          // Flags: for VanFont
using VanFontAtlasFlags      = int; // -> enum VanFontAtlasFlags_     // Flags: for VanFontAtlas
using VanGuiBackendFlags     = int; // -> enum VanGuiBackendFlags_    // Flags: for io.BackendFlags
using VanGuiButtonFlags      = int; // -> enum VanGuiButtonFlags_     // Flags: for InvisibleButton()
using VanGuiChildFlags       = int; // -> enum VanGuiChildFlags_      // Flags: for BeginChild()
using VanGuiColorEditFlags   = int; // -> enum VanGuiColorEditFlags_  // Flags: for ColorEdit4(), ColorPicker4() etc.
using VanGuiConfigFlags      = int; // -> enum VanGuiConfigFlags_     // Flags: for io.ConfigFlags
using VanGuiComboFlags       = int; // -> enum VanGuiComboFlags_      // Flags: for BeginCombo()
using VanGuiDragDropFlags    = int; // -> enum VanGuiDragDropFlags_   // Flags: for BeginDragDropSource(), AcceptDragDropPayload()
using VanGuiFocusedFlags     = int; // -> enum VanGuiFocusedFlags_    // Flags: for IsWindowFocused()
using VanGuiHoveredFlags     = int; // -> enum VanGuiHoveredFlags_    // Flags: for IsItemHovered(), IsWindowHovered() etc.
using VanGuiInputFlags       = int; // -> enum VanGuiInputFlags_      // Flags: for Shortcut(), SetNextItemShortcut()
using VanGuiInputTextFlags   = int; // -> enum VanGuiInputTextFlags_  // Flags: for InputText(), InputTextMultiline()
using VanGuiItemFlags        = int; // -> enum VanGuiItemFlags_       // Flags: for PushItemFlag(), shared by all items
using VanGuiKeyChord         = int; // -> VanGuiKey | VanGuiMod_XXX   // Flags: for IsKeyChordPressed(), Shortcut() etc. an VanGuiKey optionally OR-ed with one or more VanGuiMod_XXX values.
using VanGuiListClipperFlags = int; // -> enum VanGuiListClipperFlags_// Flags: for VanGuiListClipper
using VanGuiPopupFlags       = int; // -> enum VanGuiPopupFlags_      // Flags: for OpenPopup*(), BeginPopupContext*(), IsPopupOpen()
using VanGuiMultiSelectFlags = int; // -> enum VanGuiMultiSelectFlags_// Flags: for BeginMultiSelect()
using VanGuiSelectableFlags  = int; // -> enum VanGuiSelectableFlags_ // Flags: for Selectable()
using VanGuiSliderFlags      = int; // -> enum VanGuiSliderFlags_     // Flags: for DragFloat(), DragInt(), SliderFloat(), SliderInt() etc.
using VanGuiTabBarFlags      = int; // -> enum VanGuiTabBarFlags_     // Flags: for BeginTabBar()
using VanGuiTabItemFlags     = int; // -> enum VanGuiTabItemFlags_    // Flags: for BeginTabItem()
using VanGuiTableFlags       = int; // -> enum VanGuiTableFlags_      // Flags: For BeginTable()
using VanGuiTableColumnFlags = int; // -> enum VanGuiTableColumnFlags_// Flags: For TableSetupColumn()
using VanGuiTableRowFlags    = int; // -> enum VanGuiTableRowFlags_   // Flags: For TableNextRow()
using VanGuiTreeNodeFlags    = int; // -> enum VanGuiTreeNodeFlags_   // Flags: for TreeNode(), TreeNodeEx(), CollapsingHeader()
using VanGuiDockNodeFlags    = int; // -> enum VanGuiDockNodeFlags_   // Flags: for DockSpace(), DockSpaceOverViewport()
using VanGuiViewportFlags    = int; // -> enum VanGuiViewportFlags_   // Flags: for VanGuiViewport
using VanGuiWindowFlags      = int; // -> enum VanGuiWindowFlags_     // Flags: for Begin(), BeginChild()

// Character types
// (we generally use UTF-8 encoded string in the API. This is storage specifically for a decoded character used for keyboard input and display)
using VanWchar32 = uint32_t;        // A single decoded U32 character/code point. We encode them as multi bytes UTF-8 when used in strings.
using VanWchar16 = uint16_t;        // A single decoded U16 character/code point. We encode them as multi bytes UTF-8 when used in strings.
#ifdef VANGUI_USE_WCHAR32           // VanWchar [configurable type: override in vanconfig.h with '#define VANGUI_USE_WCHAR32' to support Unicode planes 1-16]
using VanWchar = VanWchar32;
#else
using VanWchar = VanWchar16;
#endif

// Multi-Selection item index or identifier when using BeginMultiSelect()
// - Used by SetNextItemSelectionUserData() + and inside VanGuiMultiSelectIO structure.
// - Most users are likely to use this store an item INDEX but this may be used to store a POINTER/ID as well. Read comments near VanGuiMultiSelectIO for details.
using VanGuiSelectionUserData = VanS64;

// Callback and function pointer types
using VanGuiInputTextCallback = int  (*)(VanGuiInputTextCallbackData* data);  // Callback function for VanGui::InputText()
using VanGuiSizeCallback      = void (*)(VanGuiSizeCallbackData* data);        // Callback function for VanGui::SetNextWindowSizeConstraints()
using VanGuiMemAllocFunc      = void*(*)(size_t sz, void* user_data);          // Function signature for VanGui::SetAllocatorFunctions()
using VanGuiMemFreeFunc       = void (*)(void* ptr, void* user_data);          // Function signature for VanGui::SetAllocatorFunctions()

// VanVec2: 2D vector used to store positions, sizes etc. [Compile-time configurable type]
// - This is a frequently used type in the API. Consider using VAN_VEC2_CLASS_EXTRA to create implicit cast from/to our preferred type.
// - Add '#define VANGUI_DEFINE_MATH_OPERATORS' before including this file (or in vanconfig.h) to access courtesy maths operators for VanVec2 and VanVec4.
VAN_MSVC_RUNTIME_CHECKS_OFF
struct VanVec2
{
    float x, y;
    constexpr VanVec2() noexcept                   : x(0.0f), y(0.0f) {}
    constexpr VanVec2(float _x, float _y) noexcept : x(_x), y(_y) {}
    [[nodiscard]] float& operator[](size_t idx) noexcept       { VAN_ASSERT(idx == 0 || idx == 1); return (&x)[idx]; }
    [[nodiscard]] float  operator[](size_t idx) const noexcept { VAN_ASSERT(idx == 0 || idx == 1); return (&x)[idx]; }
    [[nodiscard]] auto operator<=>(const VanVec2&) const noexcept = default;
#ifdef VAN_VEC2_CLASS_EXTRA
    VAN_VEC2_CLASS_EXTRA     // Define additional constructors and implicit cast operators in vanconfig.h to convert back and forth between your math types and VanVec2.
#endif
};

// VanVec4: 4D vector used to store clipping rectangles, colors etc. [Compile-time configurable type]
struct VanVec4
{
    float x, y, z, w;
    constexpr VanVec4() noexcept                                        : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    constexpr VanVec4(float _x, float _y, float _z, float _w) noexcept : x(_x), y(_y), z(_z), w(_w) {}
    [[nodiscard]] auto operator<=>(const VanVec4&) const noexcept = default;
#ifdef VAN_VEC4_CLASS_EXTRA
    VAN_VEC4_CLASS_EXTRA     // Define additional constructors and implicit cast operators in vanconfig.h to convert back and forth between your math types and VanVec4.
#endif
};
VAN_MSVC_RUNTIME_CHECKS_RESTORE

//-----------------------------------------------------------------------------
// [SECTION] Texture identifiers (VanTextureID, VanTextureRef)
//-----------------------------------------------------------------------------

// VanTextureID = backend specific, low-level identifier for a texture uploaded in GPU/graphics system.
// [Compile-time configurable type]
// - When a Rendered Backend creates a texture, it store its native identifier into a VanTextureID value.
//   (e.g. Used by DX11 backend to a `ID3D11ShaderResourceView*`; Used by OpenGL backends to store `GLuint`;
//         Used by SDLGPU backend to store a `SDL_GPUTextureSamplerBinding*`, etc.).
// - User may submit their own textures to e.g. VanGui::Image() function by passing this value.
// - During the rendering loop, the Renderer Backend retrieve the VanTextureID, which stored inside a
//   VanTextureRef, which is stored inside a VanDrawCmd.
// - Compile-time type configuration:
//   - To use something other than a 64-bit value: add '#define VanTextureID MyTextureType*' in your vanconfig.h file.
//   - This can be whatever to you want it to be! read the FAQ entry about textures for details.
//   - You may decide to store a higher-level structure containing texture, sampler, shader etc. with various
//     constructors if you like. You will need to implement ==/!= operators.
// History:
// - In v1.91.4 (2024/10/08): the default type for VanTextureID was changed from 'void*' to 'VanU64'. This allowed backends requiring 64-bit worth of data to build on 32-bit architectures. Use intermediary intptr_t cast and read FAQ if you have casting warnings.
// - In v1.92.0 (2025/06/11): added VanTextureRef which carry either a VanTextureID either a pointer to internal texture atlas. All user facing functions taking VanTextureID changed to VanTextureRef
#ifndef VanTextureID
using VanTextureID = VanU64;      // Default: store up to 64-bits (any pointer or integer). A majority of backends are ok with that.
#endif

// Define this if you need to change the invalid value for your backend.
// - If your backend is using VanTextureID to store an index/offset and you need 0 to be valid, You can add '#define VanTextureID_Invalid ((VanTextureID)-1)' in your vanconfig.h file.
// - From 2026/03/12 to 2026/03/19 we experimented with changing to default to -1, but I worried it would cause too many issues in third-party code so it was reverted.
#ifndef VanTextureID_Invalid
#define VanTextureID_Invalid     ((VanTextureID)0)
#endif

// VanTextureRef = higher-level identifier for a texture. Store a VanTextureID _or_ a VanTextureData*.
// The identifier is valid even before the texture has been uploaded to the GPU/graphics system.
// This is what gets passed to functions such as `VanGui::Image()`, `VanDrawList::AddImage()`.
// This is what gets stored in draw commands (`VanDrawCmd`) to identify a texture during rendering.
// - When a texture is created by user code (e.g. custom images), we directly store the low-level VanTextureID.
//   Because of this, when displaying your own texture you are likely to ever only manage VanTextureID values on your side.
// - When a texture is created by the backend, we stores a VanTextureData* which becomes an indirection
//   to extract the VanTextureID value during rendering, after texture upload has happened.
// - To create a VanTextureRef from a VanTextureData you can use VanTextureData::GetTexRef().
//   We intentionally do not provide an VanTextureRef constructor for this: we don't expect this
//   to be frequently useful to the end-user, and it would be erroneously called by many legacy code.
// - If you want to bind the current atlas when using custom rectangle, you can use io.Fonts->TexRef.
// - Binding generators for languages such as C (which don't have constructors), should provide a helper, e.g.
//      inline VanTextureRef VanTextureRefFromID(VanTextureID tex_id) { VanTextureRef tex_ref = { ._TexData = nullptr, .TexID = tex_id }; return tex_ref; }
// In 1.92 we changed most drawing functions using VanTextureID to use VanTextureRef.
// We intentionally do not provide an implicit VanTextureRef -> VanTextureID cast operator because it is technically lossy to convert VanTextureRef to VanTextureID before rendering.
VAN_MSVC_RUNTIME_CHECKS_OFF
struct VanTextureRef
{
    VanTextureRef()                          { _TexData = nullptr; _TexID = VanTextureID_Invalid; }
    VanTextureRef(VanTextureID tex_id)        { _TexData = nullptr; _TexID = tex_id; }
#if !defined(VANGUI_DISABLE_OBSOLETE_FUNCTIONS) && !defined(VanTextureID)
    VanTextureRef(void* tex_id)              { _TexData = nullptr; _TexID = static_cast<VanTextureID>(reinterpret_cast<size_t>(tex_id)); }  // For legacy backends casting to VanTextureID
#endif

    inline VanTextureID  GetTexID() const;   // == (_TexData ? _TexData->TexID : _TexID) // Implemented below in the file.

    // Members (either are set, never both!)
    VanTextureData*      _TexData;           //      A texture, generally owned by a VanFontAtlas. Will convert to VanTextureID during render loop, after texture has been uploaded.
    VanTextureID         _TexID;             // _OR_ Low-level backend texture identifier, if already uploaded or created by user/app. Generally provided to e.g. VanGui::Image() calls.
};
VAN_MSVC_RUNTIME_CHECKS_RESTORE

//-----------------------------------------------------------------------------
// [SECTION] VanGUI end-user API functions
// (Note that VanGui:: being a namespace, you can add extra VanGui:: functions in your own separate file. Please don't modify vangui source files!)
//-----------------------------------------------------------------------------

namespace VanGui
{
    // Context creation and access
    // - Each context create its own VanFontAtlas by default. You may instance one yourself and pass it to CreateContext() to share a font atlas between contexts.
    // - DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()
    //   for each static/DLL boundary you are calling from. Read "Context and Memory Allocators" section of vangui.cpp for details.
    [[nodiscard]] VANGUI_API VanGuiContext* CreateContext(VanFontAtlas* shared_font_atlas = nullptr);
    VANGUI_API void          DestroyContext(VanGuiContext* ctx = nullptr);   // nullptr = destroy current context
    [[nodiscard]] VANGUI_API VanGuiContext* GetCurrentContext();
    VANGUI_API void          SetCurrentContext(VanGuiContext* ctx);

    // Main
    VANGUI_API VanGuiIO&      GetIO();                                    // access the VanGuiIO structure (mouse/keyboard/gamepad inputs, time, various configuration options/flags)
    [[nodiscard]] VANGUI_API VanGuiPlatformIO& GetPlatformIO();           // access the VanGuiPlatformIO structure (mostly hooks/functions to connect to platform/renderer and OS Clipboard, IME etc.)
    VANGUI_API VanGuiStyle&   GetStyle();                                 // access the Style structure (colors, sizes). Always use PushStyleColor(), PushStyleVar() to modify style mid-frame!
    VANGUI_API void          NewFrame();                                 // start a new VanGUI frame, you can submit any command from this point until Render()/EndFrame().
    VANGUI_API void          EndFrame();                                 // ends the VanGUI frame. automatically called by Render(). If you don't need to render data (skipping rendering) you may call EndFrame() without Render()... but you'll have wasted CPU already! If you don't need to render, better to not create any windows and not call NewFrame() at all!
    VANGUI_API void          Render();                                   // ends the VanGUI frame, finalize the draw data. You can then get call GetDrawData().
    [[nodiscard]] VANGUI_API VanDrawData*   GetDrawData();                              // valid after Render() and until the next call to NewFrame(). Call VanGui_ImplXXXX_RenderDrawData() function in your Renderer Backend to render.

    // Demo, Debug, Information
    VANGUI_API void          ShowDemoWindow(bool* p_open = nullptr);        // create Demo window. demonstrate most VanGui features. call this to learn about the library! try to make it always available in your application!
    VANGUI_API void          ShowMetricsWindow(bool* p_open = nullptr);     // create Metrics/Debugger window. display VanGUI internals: windows, draw commands, various internal state, etc.
    VANGUI_API void          ShowDebugLogWindow(bool* p_open = nullptr);    // create Debug Log window. display a simplified log of important VanGUI events.
    VANGUI_API void          ShowIDStackToolWindow(bool* p_open = nullptr); // create Stack Tool window. hover items with mouse to query information about the source of their unique ID.
    VANGUI_API void          ShowAboutWindow(bool* p_open = nullptr);       // create About window. display VanGUI version, credits and build/system information.
    VANGUI_API void          ShowStyleEditor(VanGuiStyle* ref = nullptr);    // add style editor block (not a window). you can pass in a reference VanGuiStyle structure to compare to, revert to and save to (else it uses the default style)
    [[nodiscard]] VANGUI_API bool          ShowStyleSelector(const char* label);       // add style selector block (not a window), essentially a combo listing the default styles.
    VANGUI_API void          ShowFontSelector(const char* label);        // add font selector block (not a window), essentially a combo listing the loaded fonts.
    VANGUI_API void          ShowUserGuide();                            // add basic help/info block (not a window): how to manipulate VanGui as an end-user (mouse/keyboard controls).
    [[nodiscard]] VANGUI_API const char*   GetVersion();                               // get the compiled version string e.g. "1.80 WIP" (essentially the value for VANGUI_VERSION from the compiled version of vangui.cpp)

    // Styles
    VANGUI_API void          StyleColorsDark(VanGuiStyle* dst = nullptr);    // new, recommended style (default)
    VANGUI_API void          StyleColorsLight(VanGuiStyle* dst = nullptr);   // best used with borders and a custom, thicker font
    VANGUI_API void          StyleColorsClassic(VanGuiStyle* dst = nullptr); // classic vangui style

    // Windows
    // - Begin() = push window to the stack and start appending to it. End() = pop window from the stack.
    // - Passing 'bool* p_open != nullptr' shows a window-closing widget in the upper-right corner of the window,
    //   which clicking will set the boolean to false when clicked.
    // - You may append multiple times to the same window during the same frame by calling Begin()/End() pairs multiple times.
    //   Some information such as 'flags' or 'p_open' will only be considered by the first call to Begin().
    // - Begin() return false to indicate the window is collapsed or fully clipped, so you may early out and omit submitting
    //   anything to the window. Always call a matching End() for each Begin() call, regardless of its return value!
    //   [Important: due to legacy reason, Begin/End and BeginChild/EndChild are inconsistent with all other functions
    //    such as BeginMenu/EndMenu, BeginPopup/EndPopup, etc. where the EndXXX call should only be called if the corresponding
    //    BeginXXX function returned true. Begin and BeginChild are the only odd ones out. Will be fixed in a future update.]
    // - Note that the bottom of window stack always contains a window called "Debug".
    [[nodiscard]] VANGUI_API bool          Begin(const char* name, bool* p_open = nullptr, VanGuiWindowFlags flags = 0);
    VANGUI_API void          End();

    // Child Windows
    // - Use child windows to begin into a self-contained independent scrolling/clipping regions within a host window. Child windows can embed their own child.
    // - Before 1.90 (November 2023), the "VanGuiChildFlags child_flags = 0" parameter was "bool border = false".
    //   This API is backward compatible with old code, as we guarantee that VanGuiChildFlags_Borders == true.
    //   Consider updating your old code:
    //      BeginChild("Name", size, false)   -> Begin("Name", size, 0); or Begin("Name", size, VanGuiChildFlags_None);
    //      BeginChild("Name", size, true)    -> Begin("Name", size, VanGuiChildFlags_Borders);
    // - Manual sizing (each axis can use a different setting e.g. VanVec2(0.0f, 400.0f)):
    //     == 0.0f: use remaining parent window size for this axis.
    //      > 0.0f: use specified size for this axis.
    //      < 0.0f: right/bottom-align to specified distance from available content boundaries.
    // - Specifying VanGuiChildFlags_AutoResizeX or VanGuiChildFlags_AutoResizeY makes the sizing automatic based on child contents.
    //   Combining both VanGuiChildFlags_AutoResizeX _and_ VanGuiChildFlags_AutoResizeY defeats purpose of a scrolling region and is NOT recommended.
    // - BeginChild() returns false to indicate the window is collapsed or fully clipped, so you may early out and omit submitting
    //   anything to the window. Always call a matching EndChild() for each BeginChild() call, regardless of its return value.
    //   [Important: due to legacy reason, Begin/End and BeginChild/EndChild are inconsistent with all other functions
    //    such as BeginMenu/EndMenu, BeginPopup/EndPopup, etc. where the EndXXX call should only be called if the corresponding
    //    BeginXXX function returned true. Begin and BeginChild are the only odd ones out. Will be fixed in a future update.]
    [[nodiscard]] VANGUI_API bool          BeginChild(const char* str_id, const VanVec2& size = VanVec2(0, 0), VanGuiChildFlags child_flags = 0, VanGuiWindowFlags window_flags = 0);
    [[nodiscard]] VANGUI_API bool          BeginChild(VanGuiID id, const VanVec2& size = VanVec2(0, 0), VanGuiChildFlags child_flags = 0, VanGuiWindowFlags window_flags = 0);
    VANGUI_API void          EndChild();

    // Windows Utilities
    // - 'current window' = the window we are appending into while inside a Begin()/End() block. 'next window' = next window we will Begin() into.
    [[nodiscard]] VANGUI_API bool          IsWindowAppearing();
    [[nodiscard]] VANGUI_API bool          IsWindowCollapsed();
    [[nodiscard]] VANGUI_API bool          IsWindowFocused(VanGuiFocusedFlags flags=0); // is current window focused? or its root/child, depending on flags. see flags for options.
    [[nodiscard]] VANGUI_API bool          IsWindowHovered(VanGuiHoveredFlags flags=0); // is current window hovered and hoverable (e.g. not blocked by a popup/modal)? See VanGuiHoveredFlags_ for options. IMPORTANT: If you are trying to check whether your mouse should be dispatched to VanGUI or to your underlying app, you should not use this function! Use the 'io.WantCaptureMouse' boolean for that! Refer to FAQ entry "How can I tell whether to dispatch mouse/keyboard to VanGUI or my application?" for details.
    [[nodiscard]] VANGUI_API VanDrawList*   GetWindowDrawList();                        // get draw list associated to the current window, to append your own drawing primitives
    VANGUI_API VanVec2        GetWindowPos();                             // get current window position in screen space (IT IS UNLIKELY YOU EVER NEED TO USE THIS. Consider always using GetCursorScreenPos() and GetContentRegionAvail() instead)
    VANGUI_API VanVec2        GetWindowSize();                            // get current window size (IT IS UNLIKELY YOU EVER NEED TO USE THIS. Consider always using GetCursorScreenPos() and GetContentRegionAvail() instead)
    VANGUI_API float         GetWindowWidth();                           // get current window width (IT IS UNLIKELY YOU EVER NEED TO USE THIS). Shortcut for GetWindowSize().x.
    VANGUI_API float         GetWindowHeight();                          // get current window height (IT IS UNLIKELY YOU EVER NEED TO USE THIS). Shortcut for GetWindowSize().y.

    // Window manipulation
    // - Prefer using SetNextXXX functions (before Begin) rather that SetXXX functions (after Begin).
    VANGUI_API void          SetNextWindowPos(const VanVec2& pos, VanGuiCond cond = 0, const VanVec2& pivot = VanVec2(0, 0)); // set next window position. call before Begin(). use pivot=(0.5f,0.5f) to center on given point, etc.
    VANGUI_API void          SetNextWindowSize(const VanVec2& size, VanGuiCond cond = 0);                  // set next window size. set axis to 0.0f to force an auto-fit on this axis. call before Begin()
    VANGUI_API void          SetNextWindowSizeConstraints(const VanVec2& size_min, const VanVec2& size_max, VanGuiSizeCallback custom_callback = nullptr, void* custom_callback_data = nullptr); // set next window size limits. use 0.0f or FLT_MAX if you don't want limits. Use -1 for both min and max of same axis to preserve current size (which itself is a constraint). Use callback to apply non-trivial programmatic constraints.
    VANGUI_API void          SetNextWindowContentSize(const VanVec2& size);                               // set next window content size (~ scrollable client area, which enforce the range of scrollbars). Not including window decorations (title bar, menu bar, etc.) nor WindowPadding. set an axis to 0.0f to leave it automatic. call before Begin()
    VANGUI_API void          SetNextWindowCollapsed(bool collapsed, VanGuiCond cond = 0);                 // set next window collapsed state. call before Begin()
    VANGUI_API void          SetNextWindowFocus();                                                       // set next window to be focused / top-most. call before Begin()
    VANGUI_API void          SetNextWindowScroll(const VanVec2& scroll);                                  // set next window scrolling value (use < 0.0f to not affect a given axis).
    VANGUI_API void          SetNextWindowBgAlpha(float alpha);                                          // set next window background color alpha. helper to easily override the Alpha component of VanGuiCol_WindowBg/ChildBg/PopupBg. you may also use VanGuiWindowFlags_NoBackground.
    VANGUI_API void          SetNextWindowRefreshWhenIdle(float interval = 0.0f, bool refresh_on_hover = true, bool refresh_on_focus = true); // [VanGUI] keep the next window's last drawing while it is not hovered/focused: Begin() returns false then (skip the contents as usual). It still refreshes every `interval` seconds (0 = never), when it appears, and when SetNextWindowPos/Size change it.
    VANGUI_API void          SetWindowPos(const VanVec2& pos, VanGuiCond cond = 0);                        // (not recommended) set current window position - call within Begin()/End(). prefer using SetNextWindowPos(), as this may incur tearing and side-effects.
    VANGUI_API void          SetWindowSize(const VanVec2& size, VanGuiCond cond = 0);                      // (not recommended) set current window size - call within Begin()/End(). set to VanVec2(0, 0) to force an auto-fit. prefer using SetNextWindowSize(), as this may incur tearing and minor side-effects.
    VANGUI_API void          SetWindowCollapsed(bool collapsed, VanGuiCond cond = 0);                     // (not recommended) set current window collapsed state. prefer using SetNextWindowCollapsed().
    VANGUI_API void          SetWindowFocus();                                                           // (not recommended) set current window to be focused / top-most. prefer using SetNextWindowFocus().
    VANGUI_API void          SetWindowPos(const char* name, const VanVec2& pos, VanGuiCond cond = 0);      // set named window position.
    VANGUI_API void          SetWindowSize(const char* name, const VanVec2& size, VanGuiCond cond = 0);    // set named window size. set axis to 0.0f to force an auto-fit on this axis.
    VANGUI_API void          SetWindowCollapsed(const char* name, bool collapsed, VanGuiCond cond = 0);   // set named window collapsed state
    VANGUI_API void          SetWindowFocus(const char* name);                                           // set named window to be focused / top-most. use nullptr to remove focus.

    // Windows Scrolling
    // - Any change of Scroll will be applied at the beginning of next frame in the first call to Begin().
    // - You may instead use SetNextWindowScroll() prior to calling Begin() to avoid this delay, as an alternative to using SetScrollX()/SetScrollY().
    VANGUI_API float         GetScrollX();                                                   // get scrolling amount [0 .. GetScrollMaxX()]
    VANGUI_API float         GetScrollY();                                                   // get scrolling amount [0 .. GetScrollMaxY()]
    VANGUI_API void          SetScrollX(float scroll_x);                                     // set scrolling amount [0 .. GetScrollMaxX()]
    VANGUI_API void          SetScrollY(float scroll_y);                                     // set scrolling amount [0 .. GetScrollMaxY()]
    VANGUI_API float         GetScrollMaxX();                                                // get maximum scrolling amount ~~ ContentSize.x - WindowSize.x - DecorationsSize.x
    VANGUI_API float         GetScrollMaxY();                                                // get maximum scrolling amount ~~ ContentSize.y - WindowSize.y - DecorationsSize.y
    VANGUI_API void          SetScrollHereX(float center_x_ratio = 0.5f);                    // adjust scrolling amount to make current cursor position visible. center_x_ratio=0.0: left, 0.5: center, 1.0: right. When using to make a "default/current item" visible, consider using SetItemDefaultFocus() instead.
    VANGUI_API void          SetScrollHereY(float center_y_ratio = 0.5f);                    // adjust scrolling amount to make current cursor position visible. center_y_ratio=0.0: top, 0.5: center, 1.0: bottom. When using to make a "default/current item" visible, consider using SetItemDefaultFocus() instead.
    VANGUI_API void          SetScrollFromPosX(float local_x, float center_x_ratio = 0.5f);  // adjust scrolling amount to make given position visible. Generally GetCursorStartPos() + offset to compute a valid position.
    VANGUI_API void          SetScrollFromPosY(float local_y, float center_y_ratio = 0.5f);  // adjust scrolling amount to make given position visible. Generally GetCursorStartPos() + offset to compute a valid position.
    // [VanGUI] The same, easing there like a wheel scroll (instant without the animation substrate, or under ReduceMotion).
    // Anim::GetMotionConfig().SmoothScrollTo = true makes the plain calls above ease too.
    VANGUI_API void          SetScrollXSmooth(float scroll_x);
    VANGUI_API void          SetScrollYSmooth(float scroll_y);
    VANGUI_API void          SetScrollHereYSmooth(float center_y_ratio = 0.5f);
    VANGUI_API void          ScrollToItemSmooth();                                           // bring the last item into view, easing
    // [VanGUI] Touch: how far the current window's contents are pulled past its scroll limits (the rubber band; y < 0
    // = pulled down past the top), and a way for gestures to take over a press so releasing it doesn't click.
    VANGUI_API VanVec2       GetWindowOverscroll();
    VANGUI_API bool          IsAppFocused();                                                 // [VanGUI] false from the backend's io.AddFocusEvent(false) until it reports focus again
    VANGUI_API void          CancelItemPress();
    // [VanGUI] A hit area for something drawn with the draw list over what the window already holds (gizmo handles,
    // overlay controls): registers `id` over [p_min, p_max) without moving the cursor or growing the window, and takes
    // the mouse from an item submitted earlier underneath it (its hover, and a press it got this frame). Otherwise it is
    // a button that activates on mouse down: returns true then, and IsItemActive() stays true while it is held.
    VANGUI_API bool          OverlayHitArea(VanGuiID id, const VanVec2& p_min, const VanVec2& p_max, VanGuiButtonFlags flags = 0, bool* out_hovered = NULL, bool* out_held = NULL);
    // [VanGUI] Textures you fill yourself (VanTextureData::Create, then write GetPixels()): the renderer backend creates,
    // updates and destroys them alongside the font atlas. To free one, set WantDestroyNextFrame = true and unregister
    // (then delete) it once its Status reads VanTextureStatus_Destroyed.
    VANGUI_API void          RegisterUserTexture(VanTextureData* tex);
    VANGUI_API void          UnregisterUserTexture(VanTextureData* tex);

    // Parameters stacks (font)
    //  - PushFont(font, 0.0f)                       // Change font and keep current size
    //  - PushFont(nullptr, 20.0f)                      // Keep font and change current size
    //  - PushFont(font, 20.0f)                      // Change font and set size to 20.0f
    //  - PushFont(font, style.FontSizeBase * 2.0f)  // Change font and set size to be twice bigger than current size.
    //  - PushFont(font, font->LegacySize)           // Change font and set size to size passed to AddFontXXX() function. Same as pre-1.92 behavior.
    // *IMPORTANT* before 1.92, fonts had a single size. They can now be dynamically be adjusted.
    //  - In 1.92 we have REMOVED the single parameter version of PushFont() because it seems like the easiest way to provide an error-proof transition.
    //  - PushFont(font) before 1.92 = PushFont(font, font->LegacySize) after 1.92          // Use default font size as passed to AddFontXXX() function.
    // *IMPORTANT* global scale factors are applied over the provided size.
    //  - Global scale factors are: 'style.FontScaleMain', 'style.FontScaleDpi' and maybe more.
    // -  If you want to apply a factor to the _current_ font size:
    //  - CORRECT:   PushFont(nullptr, style.FontSizeBase)         // use current unscaled size    == does nothing
    //  - CORRECT:   PushFont(nullptr, style.FontSizeBase * 2.0f)  // use current unscaled size x2 == make text twice bigger
    //  - INCORRECT: PushFont(nullptr, GetFontSize())              // INCORRECT! using size after global factors already applied == GLOBAL SCALING FACTORS WILL APPLY TWICE!
    //  - INCORRECT: PushFont(nullptr, GetFontSize() * 2.0f)       // INCORRECT! using size after global factors already applied == GLOBAL SCALING FACTORS WILL APPLY TWICE!
    VANGUI_API void          PushFont(VanFont* font, float font_size_base_unscaled);          // Use nullptr as a shortcut to keep current font. Use 0.0f to keep current size.
    VANGUI_API void          PopFont();
    [[nodiscard]] VANGUI_API VanFont*       GetFont();                                                      // get current font
    VANGUI_API float         GetFontSize();                                                  // get current scaled font size (= height in pixels). AFTER global scale factors applied. *IMPORTANT* DO NOT PASS THIS VALUE TO PushFont()! Use VanGui::GetStyle().FontSizeBase to get value before global scale factors.
    [[nodiscard]] VANGUI_API VanFontBaked*  GetFontBaked();                                                 // get current font bound at current size // == GetFont()->GetFontBaked(GetFontSize())

    // Parameters stacks (shared)
    VANGUI_API void          PushStyleColor(VanGuiCol idx, VanU32 col);                        // modify a style color. always use this if you modify the style after NewFrame().
    VANGUI_API void          PushStyleColor(VanGuiCol idx, const VanVec4& col);
    VANGUI_API void          PopStyleColor(int count = 1);
    VANGUI_API void          PushStyleVar(VanGuiStyleVar idx, float val);                     // modify a style float variable. always use this if you modify the style after NewFrame()!
    VANGUI_API void          PushStyleVar(VanGuiStyleVar idx, const VanVec2& val);             // modify a style VanVec2 variable. "
    VANGUI_API void          PushStyleVarX(VanGuiStyleVar idx, float val_x);                  // modify X component of a style VanVec2 variable. "
    VANGUI_API void          PushStyleVarY(VanGuiStyleVar idx, float val_y);                  // modify Y component of a style VanVec2 variable. "
    VANGUI_API void          PopStyleVar(int count = 1);
    VANGUI_API void          PushItemFlag(VanGuiItemFlags option, bool enabled);              // modify specified shared item flag, e.g. PushItemFlag(VanGuiItemFlags_NoTabStop, true)
    VANGUI_API void          PopItemFlag();

    // Parameters stacks (current window)
    VANGUI_API void          PushItemWidth(float item_width);                                // push width of items for common large "item+label" widgets. >0.0f: width in pixels, <0.0f align xx pixels to the right of window (so -FLT_MIN always align width to the right side).
    VANGUI_API void          PopItemWidth();
    VANGUI_API void          SetNextItemWidth(float item_width);                             // set width of the _next_ common large "item+label" widget. >0.0f: width in pixels, <0.0f align xx pixels to the right of window (so -FLT_MIN always align width to the right side)
    VANGUI_API float         CalcItemWidth();                                                // width of item given pushed settings and current cursor position. NOT necessarily the width of last item unlike most 'Item' functions.
    VANGUI_API void          PushTextWrapPos(float wrap_local_pos_x = 0.0f);                 // push word-wrapping position for Text*() commands. < 0.0f: no wrapping; 0.0f: wrap to end of window (or column); > 0.0f: wrap at 'wrap_pos_x' position in window local space
    VANGUI_API void          PopTextWrapPos();

    // Style read access
    // - Use the ShowStyleEditor() function to interactively see/edit the colors.
    VANGUI_API VanVec2        GetFontTexUvWhitePixel();                                       // get UV coordinate for a white pixel, useful to draw custom shapes via the VanDrawList API
    VANGUI_API VanU32         GetColorU32(VanGuiCol idx, float alpha_mul = 1.0f);              // retrieve given style color with style alpha applied and optional extra alpha multiplier, packed as a 32-bit value suitable for VanDrawList
    VANGUI_API VanU32         GetColorU32(const VanVec4& col);                                 // retrieve given color with style alpha applied, packed as a 32-bit value suitable for VanDrawList
    VANGUI_API VanU32         GetColorU32(VanU32 col, float alpha_mul = 1.0f);                 // retrieve given color with style alpha applied, packed as a 32-bit value suitable for VanDrawList
    VANGUI_API const VanVec4& GetStyleColorVec4(VanGuiCol idx);                                // retrieve style color as stored in VanGuiStyle structure. use to feed back into PushStyleColor(), otherwise use GetColorU32() to get style color with style alpha baked in.

    // Layout cursor positioning
    // - By "cursor" we mean the current output position.
    // - The typical widget behavior is to output themselves at the current cursor position, then move the cursor one line down.
    // - You can call SameLine() between widgets to undo the last carriage return and output at the right of the preceding widget.
    // - YOU CAN DO 99% OF WHAT YOU NEED WITH ONLY GetCursorScreenPos() and GetContentRegionAvail().
    // - Attention! We currently have inconsistencies between window-local and absolute positions we will aim to fix with future API:
    //    - Absolute coordinate:        GetCursorScreenPos(), SetCursorScreenPos(), all VanDrawList:: functions. -> this is the preferred way forward.
    //    - Window-local coordinates:   SameLine(offset), GetCursorPos(), SetCursorPos(), GetCursorStartPos(), PushTextWrapPos()
    //    - Window-local coordinates:   GetContentRegionMax(), GetWindowContentRegionMin(), GetWindowContentRegionMax() --> all obsoleted. YOU DON'T NEED THEM.
    // - GetCursorScreenPos() = GetCursorPos() + GetWindowPos(). GetWindowPos() is almost only ever useful to convert from window-local to absolute coordinates. Try not to use it.
    VANGUI_API VanVec2        GetCursorScreenPos();                                           // cursor position, absolute coordinates. THIS IS YOUR BEST FRIEND (prefer using this rather than GetCursorPos(), also more useful to work with VanDrawList API).
    VANGUI_API void          SetCursorScreenPos(const VanVec2& pos);                          // cursor position, absolute coordinates. THIS IS YOUR BEST FRIEND.
    VANGUI_API VanVec2        GetContentRegionAvail();                                        // available space from current position. THIS IS YOUR BEST FRIEND.
    VANGUI_API VanVec2        GetCursorPos();                                                 // [window-local] cursor position in window-local coordinates. This is not your best friend.
    VANGUI_API float         GetCursorPosX();                                                // [window-local] "
    VANGUI_API float         GetCursorPosY();                                                // [window-local] "
    VANGUI_API void          SetCursorPos(const VanVec2& local_pos);                          // [window-local] "
    VANGUI_API void          SetCursorPosX(float local_x);                                   // [window-local] "
    VANGUI_API void          SetCursorPosY(float local_y);                                   // [window-local] "
    VANGUI_API VanVec2        GetCursorStartPos();                                            // [window-local] initial cursor position, in window-local coordinates. Call GetCursorScreenPos() after Begin() to get the absolute coordinates version.

    // Other layout functions
    VANGUI_API void          Separator();                                                    // separator, generally horizontal. inside a menu bar or in horizontal layout mode, this becomes a vertical separator.
    VANGUI_API void          SameLine(float offset_from_start_x=0.0f, float spacing=-1.0f);  // call between widgets or groups to layout them horizontally. X position given in window coordinates.
    VANGUI_API void          NewLine();                                                      // undo a SameLine() or force a new line when in a horizontal-layout context.
    VANGUI_API void          Spacing();                                                      // add vertical spacing.
    VANGUI_API void          Dummy(const VanVec2& size);                                      // add a dummy item of given size. unlike InvisibleButton(), Dummy() won't take the mouse click or be navigable into.
    VANGUI_API void          Indent(float indent_w = 0.0f);                                  // move content position toward the right, by indent_w, or style.IndentSpacing if indent_w <= 0
    VANGUI_API void          Unindent(float indent_w = 0.0f);                                // move content position back to the left, by indent_w, or style.IndentSpacing if indent_w <= 0
    VANGUI_API void          BeginGroup();                                                   // lock horizontal starting position
    VANGUI_API void          EndGroup();                                                     // unlock horizontal starting position + capture the whole group bounding box into one "item" (so you can use IsItemHovered() or layout primitives such as SameLine() on whole group, etc.)
    VANGUI_API void          AlignTextToFramePadding();                                      // vertically align upcoming text baseline to FramePadding.y so that it will align properly to regularly framed items (call if you have text on a line before a framed item)
    VANGUI_API float         GetTextLineHeight();                                            // ~ FontSize
    VANGUI_API float         GetTextLineHeightWithSpacing();                                 // ~ FontSize + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of text)
    VANGUI_API float         GetFrameHeight();                                               // ~ FontSize + style.FramePadding.y * 2
    VANGUI_API float         GetFrameHeightWithSpacing();                                    // ~ FontSize + style.FramePadding.y * 2 + style.ItemSpacing.y (distance in pixels between 2 consecutive lines of framed widgets)

    // ID stack/scopes
    // Read the FAQ (docs/FAQ.md) for more details about how ID are handled in VanGUI.
    // - Those questions are answered and impacted by understanding of the ID stack system:
    //   - "Q: Why is my widget not reacting when I click on it?"
    //   - "Q: How can I have widgets with an empty label?"
    //   - "Q: How can I have multiple widgets with the same label?"
    // - Short version: ID are hashes of the entire ID stack. If you are creating widgets in a loop you most likely
    //   want to push a unique identifier (e.g. object pointer, loop index) to uniquely differentiate them.
    // - You can also use the "Label##foobar" syntax within widget label to distinguish them from each others.
    // - In this header file we use the "label"/"name" terminology to denote a string that will be displayed + used as an ID,
    //   whereas "str_id" denote a string that is only used as an ID and not normally displayed.
    VANGUI_API void          PushID(const char* str_id);                                     // push string into the ID stack (will hash string).
    VANGUI_API void          PushID(const char* str_id_begin, const char* str_id_end);       // push string into the ID stack (will hash string).
    VANGUI_API void          PushID(const void* ptr_id);                                     // push pointer into the ID stack (will hash pointer).
    VANGUI_API void          PushID(int int_id);                                             // push integer into the ID stack (will hash integer).
    VANGUI_API void          PopID();                                                        // pop from the ID stack.
    VANGUI_API VanGuiID       GetID(const char* str_id);                                      // calculate unique ID (hash of whole ID stack + given parameter). e.g. if you want to query into VanGuiStorage yourself
    VANGUI_API VanGuiID       GetID(const char* str_id_begin, const char* str_id_end);
    VANGUI_API VanGuiID       GetID(const void* ptr_id);
    VANGUI_API VanGuiID       GetID(int int_id);

    // Widgets: Text
    // - Note that all functions taking format strings in the API may be passed ("%s", text) or ("%.*s", text_len, text): which will automatically bypass the formatter.
    VANGUI_API void          TextUnformatted(const char* text, const char* text_end = nullptr); // raw text without formatting. Practically equivalent to 'Text("%s", text)' but doesn't require null terminated string if 'text_end' is specified.
    VANGUI_API void          Text(const char* fmt, ...)                                      VAN_FMTARGS(1); // formatted text
    VANGUI_API void          TextV(const char* fmt, va_list args)                            VAN_FMTLIST(1);
    VANGUI_API void          TextColored(const VanVec4& col, const char* fmt, ...)            VAN_FMTARGS(2); // shortcut for PushStyleColor(VanGuiCol_Text, col); Text(fmt, ...); PopStyleColor();
    VANGUI_API void          TextColoredV(const VanVec4& col, const char* fmt, va_list args)  VAN_FMTLIST(2);
    VANGUI_API void          TextDisabled(const char* fmt, ...)                              VAN_FMTARGS(1); // shortcut for PushStyleColor(VanGuiCol_Text, style.Colors[VanGuiCol_TextDisabled]); Text(fmt, ...); PopStyleColor();
    VANGUI_API void          TextDisabledV(const char* fmt, va_list args)                    VAN_FMTLIST(1);
    VANGUI_API void          TextWrapped(const char* fmt, ...)                               VAN_FMTARGS(1); // shortcut for PushTextWrapPos(0.0f); Text(fmt, ...); PopTextWrapPos();. Note that this won't work on an auto-resizing window if there's no other widgets to extend the window width, yoy may need to set a size using SetNextWindowSize().
    VANGUI_API void          TextWrappedV(const char* fmt, va_list args)                     VAN_FMTLIST(1);
    VANGUI_API void          LabelText(const char* label, const char* fmt, ...)              VAN_FMTARGS(2); // display text+label aligned the same way as value+label widgets
    VANGUI_API void          LabelTextV(const char* label, const char* fmt, va_list args)    VAN_FMTLIST(2);
    VANGUI_API void          BulletText(const char* fmt, ...)                                VAN_FMTARGS(1); // shortcut for Bullet()+Text()
    VANGUI_API void          BulletTextV(const char* fmt, va_list args)                      VAN_FMTLIST(1);
    VANGUI_API void          SeparatorText(const char* label);                               // currently: formatted text with a horizontal line

    // Widgets: Main
    // - Most widgets return true when the value has been changed or when pressed/selected
    // - You may also use one of the many IsItemXXX functions (e.g. IsItemActive, IsItemHovered, etc.) to query widget state.
    [[nodiscard]] VANGUI_API bool          Button(const char* label, const VanVec2& size = VanVec2(0, 0));   // button
    [[nodiscard]] VANGUI_API bool          SmallButton(const char* label);                                 // button with (FramePadding.y == 0) to easily embed within text
    [[nodiscard]] VANGUI_API bool          InvisibleButton(const char* str_id, const VanVec2& size, VanGuiButtonFlags flags = 0); // flexible button behavior without the visuals, frequently useful to build custom behaviors using the public api (along with IsItemActive, IsItemHovered, etc.)
    [[nodiscard]] VANGUI_API bool          ArrowButton(const char* str_id, VanGuiDir dir);                  // square button with an arrow shape
    [[nodiscard]] VANGUI_API bool          Checkbox(const char* label, bool* v);
    [[nodiscard]] VANGUI_API bool          CheckboxFlags(const char* label, int* flags, int flags_value);
    [[nodiscard]] VANGUI_API bool          CheckboxFlags(const char* label, unsigned int* flags, unsigned int flags_value);
    [[nodiscard]] VANGUI_API bool          RadioButton(const char* label, bool active);                    // use with e.g. if (RadioButton("one", my_value==1)) { my_value = 1; }
    [[nodiscard]] VANGUI_API bool          RadioButton(const char* label, int* v, int v_button);           // shortcut to handle the above pattern when value is an integer
    VANGUI_API void          ProgressBar(float fraction, const VanVec2& size_arg = VanVec2(-FLT_MIN, 0), const char* overlay = nullptr);
    VANGUI_API void          Bullet();                                                       // draw a small circle + keep the cursor on the same line. advance cursor x position by GetTreeNodeToLabelSpacing(), same distance that TreeNode() uses
    [[nodiscard]] VANGUI_API bool          TextLink(const char* label);                                    // hyperlink text button, return true when clicked
    [[nodiscard]] VANGUI_API bool          TextLinkOpenURL(const char* label, const char* url = nullptr);     // hyperlink text button, automatically open file/url when clicked

    // Widgets: Images
    // - 'uv0' and 'uv1' are texture coordinates.
    // - Image() pads adds style.ImageBorderSize on each side, ImageButton() adds style.FramePadding on each side.
    // - ImageButton() draws a background based on regular Button() color + optionally an inner background if specified.
    // - An obsolete version of Image(), before 1.91.9 (March 2025), had a 'tint_col' parameter which is now supported by the ImageWithBg() function.
    VANGUI_API void          Image(VanTextureRef tex_ref, const VanVec2& image_size, const VanVec2& uv0 = VanVec2(0, 0), const VanVec2& uv1 = VanVec2(1, 1));
    VANGUI_API void          ImageWithBg(VanTextureRef tex_ref, const VanVec2& image_size, const VanVec2& uv0 = VanVec2(0, 0), const VanVec2& uv1 = VanVec2(1, 1), const VanVec4& bg_col = VanVec4(0, 0, 0, 0), const VanVec4& tint_col = VanVec4(1, 1, 1, 1));
    [[nodiscard]] VANGUI_API bool          ImageButton(const char* str_id, VanTextureRef tex_ref, const VanVec2& image_size, const VanVec2& uv0 = VanVec2(0, 0), const VanVec2& uv1 = VanVec2(1, 1), const VanVec4& bg_col = VanVec4(0, 0, 0, 0), const VanVec4& tint_col = VanVec4(1, 1, 1, 1));

    // Widgets: Combo Box (Dropdown)
    // - The BeginCombo()/EndCombo() api allows you to manage your contents and selection state however you want it, by creating e.g. Selectable() items.
    // - The old Combo() api are helpers over BeginCombo()/EndCombo() which are kept available for convenience purpose. This is analogous to how ListBox are created.
    [[nodiscard]] VANGUI_API bool          BeginCombo(const char* label, const char* preview_value, VanGuiComboFlags flags = 0);
    VANGUI_API void          EndCombo(); // only call EndCombo() if BeginCombo() returns true!
    [[nodiscard]] VANGUI_API bool          Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1);
    [[nodiscard]] VANGUI_API bool          Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);      // Separate items with \0 within a string, end item-list with \0\0. e.g. "One\0Two\0Three\0"
    [[nodiscard]] VANGUI_API bool          Combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items = -1);

    // Widgets: Drag Sliders
    // - Ctrl+Click on any drag box to turn them into an input box. Manually input values aren't clamped by default and can go off-bounds. Use VanGuiSliderFlags_AlwaysClamp to always clamp.
    // - For all the Float2/Float3/Float4/Int2/Int3/Int4 versions of every function, note that a 'float v[X]' function argument is the same as 'float* v',
    //   the array syntax is just a way to document the number of elements that are expected to be accessible. You can pass address of your first element out of a contiguous set, e.g. &myvector.x
    // - Adjust format string to decorate the value with a prefix, a suffix, or adapt the editing and display precision e.g. "%.3f" -> 1.234; "%5.2f secs" -> 01.23 secs; "Biscuit: %.0f" -> Biscuit: 1; etc.
    // - Format string may also be set to nullptr or use the default format ("%f" or "%d").
    // - Speed are per-pixel of mouse movement (v_speed=0.2f: mouse needs to move by 5 pixels to increase value by 1). For keyboard/gamepad navigation, minimum speed is Max(v_speed, minimum_step_at_given_precision).
    // - Use v_min < v_max to clamp edits to given limits. Note that Ctrl+Click manual input can override those limits if VanGuiSliderFlags_AlwaysClamp is not used.
    // - Use v_max = FLT_MAX / INT_MAX etc to avoid clamping to a maximum, same with v_min = -FLT_MAX / INT_MIN to avoid clamping to a minimum.
    // - We use the same sets of flags for DragXXX() and SliderXXX() functions as the features are the same and it makes it easier to swap them.
    // - Legacy: Pre-1.78 there are DragXXX() function signatures that take a final `float power=1.0f' argument instead of the `VanGuiSliderFlags flags=0' argument.
    [[nodiscard]] VANGUI_API bool          DragFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", VanGuiSliderFlags flags = 0);     // If v_min >= v_max we have no bound
    [[nodiscard]] VANGUI_API bool          DragFloat2(const char* label, float v[2], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragFloat3(const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragFloat4(const char* label, float v[4], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragFloatRange2(const char* label, float* v_current_min, float* v_current_max, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", const char* format_max = nullptr, VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", VanGuiSliderFlags flags = 0);  // If v_min >= v_max we have no bound
    [[nodiscard]] VANGUI_API bool          DragInt2(const char* label, int v[2], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragInt3(const char* label, int v[3], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragInt4(const char* label, int v[4], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", const char* format_max = nullptr, VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragScalar(const char* label, VanGuiDataType data_type, void* p_data, float v_speed = 1.0f, const void* p_min = nullptr, const void* p_max = nullptr, const char* format = nullptr, VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          DragScalarN(const char* label, VanGuiDataType data_type, void* p_data, int components, float v_speed = 1.0f, const void* p_min = nullptr, const void* p_max = nullptr, const char* format = nullptr, VanGuiSliderFlags flags = 0);

    // Widgets: Regular Sliders
    // - Ctrl+Click on any slider to turn them into an input box. Manually input values aren't clamped by default and can go off-bounds. Use VanGuiSliderFlags_AlwaysClamp to always clamp.
    // - Adjust format string to decorate the value with a prefix, a suffix, or adapt the editing and display precision e.g. "%.3f" -> 1.234; "%5.2f secs" -> 01.23 secs; "Biscuit: %.0f" -> Biscuit: 1; etc.
    // - Format string may also be set to nullptr or use the default format ("%f" or "%d").
    // - Legacy: Pre-1.78 there are SliderXXX() function signatures that take a final `float power=1.0f' argument instead of the `VanGuiSliderFlags flags=0' argument.
    [[nodiscard]] VANGUI_API bool          SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", VanGuiSliderFlags flags = 0);     // adjust format to decorate the value with a prefix or a suffix for in-slider labels or unit display.
    [[nodiscard]] VANGUI_API bool          SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderAngle(const char* label, float* v_rad, float v_degrees_min = -360.0f, float v_degrees_max = +360.0f, const char* format = "%.0f deg", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderScalar(const char* label, VanGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = nullptr, VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          SliderScalarN(const char* label, VanGuiDataType data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format = nullptr, VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          VSliderFloat(const char* label, const VanVec2& size, float* v, float v_min, float v_max, const char* format = "%.3f", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          VSliderInt(const char* label, const VanVec2& size, int* v, int v_min, int v_max, const char* format = "%d", VanGuiSliderFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          VSliderScalar(const char* label, const VanVec2& size, VanGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = nullptr, VanGuiSliderFlags flags = 0);

    // Widgets: Input with Keyboard
    // - If you want to use InputText() with std::string or any custom dynamic string type, use the wrapper in misc/cpp/vangui_stdlib.h/.cpp!
    // - Most of the VanGuiInputTextFlags flags are only useful for InputText() and not for InputFloatX, InputIntX, InputDouble etc.
    [[nodiscard]] VANGUI_API bool          InputText(const char* label, char* buf, size_t buf_size, VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
    [[nodiscard]] VANGUI_API bool          InputTextMultiline(const char* label, char* buf, size_t buf_size, const VanVec2& size = VanVec2(0, 0), VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
    [[nodiscard]] VANGUI_API bool          InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, VanGuiInputTextFlags flags = 0, VanGuiInputTextCallback callback = nullptr, void* user_data = nullptr);
    [[nodiscard]] VANGUI_API bool          InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputFloat2(const char* label, float v[2], const char* format = "%.3f", VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputFloat3(const char* label, float v[3], const char* format = "%.3f", VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputFloat4(const char* label, float v[4], const char* format = "%.3f", VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputInt(const char* label, int* v, int step = 1, int step_fast = 100, VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputInt2(const char* label, int v[2], VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputInt3(const char* label, int v[3], VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputInt4(const char* label, int v[4], VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputDouble(const char* label, double* v, double step = 0.0, double step_fast = 0.0, const char* format = "%.6f", VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputScalar(const char* label, VanGuiDataType data_type, void* p_data, const void* p_step = nullptr, const void* p_step_fast = nullptr, const char* format = nullptr, VanGuiInputTextFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          InputScalarN(const char* label, VanGuiDataType data_type, void* p_data, int components, const void* p_step = nullptr, const void* p_step_fast = nullptr, const char* format = nullptr, VanGuiInputTextFlags flags = 0);

    // Widgets: Color Editor/Picker (tip: the ColorEdit* functions have a little color square that can be left-clicked to open a picker, and right-clicked to open an option menu.)
    // - Note that in C++ a 'float v[X]' function argument is the _same_ as 'float* v', the array syntax is just a way to document the number of elements that are expected to be accessible.
    // - You can pass the address of a first float element out of a contiguous structure, e.g. &myvector.x
    [[nodiscard]] VANGUI_API bool          ColorEdit3(const char* label, float col[3], VanGuiColorEditFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          ColorEdit4(const char* label, float col[4], VanGuiColorEditFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          ColorPicker3(const char* label, float col[3], VanGuiColorEditFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          ColorPicker4(const char* label, float col[4], VanGuiColorEditFlags flags = 0, const float* ref_col = nullptr);
    [[nodiscard]] VANGUI_API bool          ColorButton(const char* desc_id, const VanVec4& col, VanGuiColorEditFlags flags = 0, const VanVec2& size = VanVec2(0, 0)); // display a color square/button, hover for details, return true when pressed.
    VANGUI_API void          SetColorEditOptions(VanGuiColorEditFlags flags);                     // initialize current options (generally on application startup) if you want to select a default format, picker type, etc. User will be able to change many settings, unless you pass the _NoOptions flag to your calls.

    // Widgets: Trees
    // - TreeNode functions return true when the node is open, in which case you need to also call TreePop() when you are finished displaying the tree node contents.
    [[nodiscard]] VANGUI_API bool          TreeNode(const char* label);
    [[nodiscard]] VANGUI_API bool          TreeNode(const char* str_id, const char* fmt, ...) VAN_FMTARGS(2);   // helper variation to easily decorrelate the id from the displayed string. Read the FAQ about why and how to use ID. to align arbitrary text at the same level as a TreeNode() you can use Bullet().
    [[nodiscard]] VANGUI_API bool          TreeNode(const void* ptr_id, const char* fmt, ...) VAN_FMTARGS(2);   // "
    [[nodiscard]] VANGUI_API bool          TreeNodeV(const char* str_id, const char* fmt, va_list args) VAN_FMTLIST(2);
    [[nodiscard]] VANGUI_API bool          TreeNodeV(const void* ptr_id, const char* fmt, va_list args) VAN_FMTLIST(2);
    [[nodiscard]] VANGUI_API bool          TreeNodeEx(const char* label, VanGuiTreeNodeFlags flags = 0);
    [[nodiscard]] VANGUI_API bool          TreeNodeEx(const char* str_id, VanGuiTreeNodeFlags flags, const char* fmt, ...) VAN_FMTARGS(3);
    [[nodiscard]] VANGUI_API bool          TreeNodeEx(const void* ptr_id, VanGuiTreeNodeFlags flags, const char* fmt, ...) VAN_FMTARGS(3);
    [[nodiscard]] VANGUI_API bool          TreeNodeExV(const char* str_id, VanGuiTreeNodeFlags flags, const char* fmt, va_list args) VAN_FMTLIST(3);
    [[nodiscard]] VANGUI_API bool          TreeNodeExV(const void* ptr_id, VanGuiTreeNodeFlags flags, const char* fmt, va_list args) VAN_FMTLIST(3);
    VANGUI_API void          TreePush(const char* str_id);                                       // ~ Indent()+PushID(). Already called by TreeNode() when returning true, but you can call TreePush/TreePop yourself if desired.
    VANGUI_API void          TreePush(const void* ptr_id);                                       // "
    VANGUI_API void          TreePop();                                                          // ~ Unindent()+PopID()
    VANGUI_API float         GetTreeNodeToLabelSpacing();                                        // horizontal distance preceding label when using TreeNode*() or Bullet() == (g.FontSize + style.FramePadding.x*2) for a regular unframed TreeNode
    [[nodiscard]] VANGUI_API bool          CollapsingHeader(const char* label, VanGuiTreeNodeFlags flags = 0);  // if returning 'true' the header is open. doesn't indent nor push on ID stack. user doesn't have to call TreePop().
    [[nodiscard]] VANGUI_API bool          CollapsingHeader(const char* label, bool* p_visible, VanGuiTreeNodeFlags flags = 0); // when 'p_visible != nullptr': if '*p_visible==true' display an additional small close button on upper right of the header which will set the bool to false when clicked, if '*p_visible==false' don't display the header.
    VANGUI_API void          SetNextItemOpen(bool is_open, VanGuiCond cond = 0);                  // set next TreeNode/CollapsingHeader open state.
    VANGUI_API void          SetNextItemStorageID(VanGuiID storage_id);                           // set id to use for open/close storage (default to same as item id).
    [[nodiscard]] VANGUI_API bool          TreeNodeGetOpen(VanGuiID storage_id);                                // retrieve tree node open/close state.
    VANGUI_API void          TreeNodeSetOpen(VanGuiID storage_id, bool open);                          // set tree node open/close state (same storage as SetNextItemStorageID() / TreeNodeGetOpen()).

    // Widgets: Selectables
    // - A selectable highlights when hovered, and can display another color when selected.
    // - Neighbors selectable extend their highlight bounds in order to leave no gap between them. This is so a series of selected Selectable appear contiguous.
    [[nodiscard]] VANGUI_API bool          Selectable(const char* label, bool selected = false, VanGuiSelectableFlags flags = 0, const VanVec2& size = VanVec2(0, 0)); // "bool selected" carry the selection state (read-only). Selectable() is clicked is returns true so you can modify your selection state. size.x==0.0: use remaining width, size.x>0.0: specify width. size.y==0.0: use label height, size.y>0.0: specify height
    [[nodiscard]] VANGUI_API bool          Selectable(const char* label, bool* p_selected, VanGuiSelectableFlags flags = 0, const VanVec2& size = VanVec2(0, 0));      // "bool* p_selected" point to the selection state (read-write), as a convenient helper.

    // Multi-selection system for Selectable(), Checkbox(), TreeNode() functions [BETA]
    // - This enables standard multi-selection/range-selection idioms (Ctrl+Mouse/Keyboard, Shift+Mouse/Keyboard, etc.) in a way that also allow a clipper to be used.
    // - VanGuiSelectionUserData is often used to store your item index within the current view (but may store something else).
    // - Read comments near VanGuiMultiSelectIO for instructions/details and see 'Demo->Widgets->Selection State & Multi-Select' for demo.
    // - TreeNode() is technically supported but... using this correctly is more complicated. You need some sort of linear/random access to your tree,
    //   which is suited to advanced trees setups already implementing filters and clipper. We will work simplifying the current demo.
    // - 'selection_size' and 'items_count' parameters are optional and used by a few features. If they are costly for you to compute, you may avoid them.
    [[nodiscard]] VANGUI_API VanGuiMultiSelectIO*   BeginMultiSelect(VanGuiMultiSelectFlags flags, int selection_size = -1, int items_count = -1);
    [[nodiscard]] VANGUI_API VanGuiMultiSelectIO*   EndMultiSelect();
    VANGUI_API void                  SetNextItemSelectionUserData(VanGuiSelectionUserData selection_user_data);
    [[nodiscard]] VANGUI_API bool                  IsItemToggledSelection();                                   // Was the last item selection state toggled? Useful if you need the per-item information _before_ reaching EndMultiSelect(). We only returns toggle _event_ in order to handle clipping correctly.

    // Widgets: List Boxes
    // - This is essentially a thin wrapper to using BeginChild/EndChild with the VanGuiChildFlags_FrameStyle flag for stylistic changes + displaying a label.
    // - If you don't need a label you can probably simply use BeginChild() with the VanGuiChildFlags_FrameStyle flag for the same result.
    // - You can submit contents and manage your selection state however you want it, by creating e.g. Selectable() or any other items.
    // - The simplified/old ListBox() api are helpers over BeginListBox()/EndListBox() which are kept available for convenience purpose. This is analogous to how Combos are created.
    // - Choose frame width:   size.x > 0.0f: custom  /  size.x < 0.0f or -FLT_MIN: right-align   /  size.x = 0.0f (default): use current ItemWidth
    // - Choose frame height:  size.y > 0.0f: custom  /  size.y < 0.0f or -FLT_MIN: bottom-align  /  size.y = 0.0f (default): arbitrary default height which can fit ~7 items
    [[nodiscard]] VANGUI_API bool          BeginListBox(const char* label, const VanVec2& size = VanVec2(0, 0)); // open a framed scrolling region
    VANGUI_API void          EndListBox();                                                       // only call EndListBox() if BeginListBox() returned true!
    [[nodiscard]] VANGUI_API bool          ListBox(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items = -1);
    [[nodiscard]] VANGUI_API bool          ListBox(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int height_in_items = -1);

    // Widgets: Data Plotting
    // - Consider using VanPlot (https://github.com/epezent/implot) which is much better!
    VANGUI_API void          PlotLines(const char* label, const float* values, int values_count, int values_offset = 0, const char* overlay_text = nullptr, float scale_min = FLT_MAX, float scale_max = FLT_MAX, VanVec2 graph_size = VanVec2(0, 0), int stride = sizeof(float));
    VANGUI_API void          PlotLines(const char* label, float(*values_getter)(void* data, int idx), void* data, int values_count, int values_offset = 0, const char* overlay_text = nullptr, float scale_min = FLT_MAX, float scale_max = FLT_MAX, VanVec2 graph_size = VanVec2(0, 0));
    VANGUI_API void          PlotHistogram(const char* label, const float* values, int values_count, int values_offset = 0, const char* overlay_text = nullptr, float scale_min = FLT_MAX, float scale_max = FLT_MAX, VanVec2 graph_size = VanVec2(0, 0), int stride = sizeof(float));
    VANGUI_API void          PlotHistogram(const char* label, float (*values_getter)(void* data, int idx), void* data, int values_count, int values_offset = 0, const char* overlay_text = nullptr, float scale_min = FLT_MAX, float scale_max = FLT_MAX, VanVec2 graph_size = VanVec2(0, 0));

    // Widgets: Value() Helpers.
    // - Those are merely shortcut to calling Text() with a format string. Output single value in "name: value" format (tip: freely declare more in your code to handle your types. you can add functions to the VanGui namespace)
    VANGUI_API void          Value(const char* prefix, bool b);
    VANGUI_API void          Value(const char* prefix, int v);
    VANGUI_API void          Value(const char* prefix, unsigned int v);
    VANGUI_API void          Value(const char* prefix, float v, const char* float_format = nullptr);

    // Widgets: Menus
    // - Use BeginMenuBar() on a window VanGuiWindowFlags_MenuBar to append to its menu bar.
    // - Use BeginMainMenuBar() to create a menu bar at the top of the screen and append to it.
    // - Use BeginMenu() to create a menu. You can call BeginMenu() multiple time with the same identifier to append more items to it.
    // - Not that MenuItem() keyboardshortcuts are displayed as a convenience but _not processed_ by VanGUI at the moment.
    [[nodiscard]] VANGUI_API bool          BeginMenuBar();                                                     // append to menu-bar of current window (requires VanGuiWindowFlags_MenuBar flag set on parent window).
    VANGUI_API void          EndMenuBar();                                                       // only call EndMenuBar() if BeginMenuBar() returns true!
    [[nodiscard]] VANGUI_API bool          BeginMainMenuBar();                                                 // create and append to a full screen menu-bar.
    VANGUI_API void          EndMainMenuBar();                                                   // only call EndMainMenuBar() if BeginMainMenuBar() returns true!
    [[nodiscard]] VANGUI_API bool          BeginMenu(const char* label, bool enabled = true);                  // create a sub-menu entry. only call EndMenu() if this returns true!
    VANGUI_API void          EndMenu();                                                          // only call EndMenu() if BeginMenu() returns true!
    [[nodiscard]] VANGUI_API bool          MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);  // return true when activated.
    [[nodiscard]] VANGUI_API bool          MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled = true);              // return true when activated + toggle (*p_selected) if p_selected != nullptr

    // Tooltips
    // - Tooltips are windows following the mouse. They do not take focus away.
    // - A tooltip window can contain items of any types.
    // - SetTooltip() is more or less a shortcut for the 'if (BeginTooltip()) { Text(...); EndTooltip(); }' idiom (with a subtlety that it discard any previously submitted tooltip)
    [[nodiscard]] VANGUI_API bool          BeginTooltip();                                                     // begin/append a tooltip window.
    VANGUI_API void          EndTooltip();                                                       // only call EndTooltip() if BeginTooltip()/BeginItemTooltip() returns true!
    VANGUI_API void          SetTooltip(const char* fmt, ...) VAN_FMTARGS(1);                     // set a text-only tooltip. Often used after a VanGui::IsItemHovered() check. Override any previous call to SetTooltip().
    VANGUI_API void          SetTooltipV(const char* fmt, va_list args) VAN_FMTLIST(1);

    // Tooltips: helpers for showing a tooltip when hovering an item
    // - BeginItemTooltip() is a shortcut for the 'if (IsItemHovered(VanGuiHoveredFlags_ForTooltip) && BeginTooltip())' idiom.
    // - SetItemTooltip() is a shortcut for the 'if (IsItemHovered(VanGuiHoveredFlags_ForTooltip)) { SetTooltip(...); }' idiom.
    // - Where 'VanGuiHoveredFlags_ForTooltip' itself is a shortcut to use 'style.HoverFlagsForTooltipMouse' or 'style.HoverFlagsForTooltipNav' depending on active input type. For mouse it defaults to 'VanGuiHoveredFlags_Stationary | VanGuiHoveredFlags_DelayShort'.
    [[nodiscard]] VANGUI_API bool          BeginItemTooltip();                                                 // begin/append a tooltip window if preceding item was hovered.
    VANGUI_API void          SetItemTooltip(const char* fmt, ...) VAN_FMTARGS(1);                 // set a text-only tooltip if preceding item was hovered. override any previous call to SetTooltip().
    VANGUI_API void          SetItemTooltipV(const char* fmt, va_list args) VAN_FMTLIST(1);

    // Popups, Modals
    //  - They block normal mouse hovering detection (and therefore most mouse interactions) behind them.
    //  - If not modal: they can be closed by clicking anywhere outside them, or by pressing Escape (call 'Shortcut(VanGuiKey_Escape)' to claim a higher-priority shortcut).
    //  - Their visibility state (~bool) is held internally instead of being held by the programmer as we are used to with regular Begin*() calls.
    //  - The 3 properties above are related: we need to retain popup visibility state in the library because popups may be closed as any time.
    //  - You can bypass the hovering restriction by using VanGuiHoveredFlags_AllowWhenBlockedByPopup when calling IsItemHovered() or IsWindowHovered().
    //  - IMPORTANT: Popup identifiers are relative to the current ID stack, so OpenPopup and BeginPopup generally needs to be at the same level of the stack.
    //    This is sometimes leading to confusing mistakes. May rework this in the future.
    //  - BeginPopup(): query popup state, if open start appending into the window. Call EndPopup() afterwards if returned true. VanGuiWindowFlags are forwarded to the window.
    //  - BeginPopupModal(): block every interaction behind the window, cannot be closed by user, add a dimming background, has a title bar.
    [[nodiscard]] VANGUI_API bool          BeginPopup(const char* str_id, VanGuiWindowFlags flags = 0);                         // return true if the popup is open, and you can start outputting to it.
    [[nodiscard]] VANGUI_API bool          BeginPopupModal(const char* name, bool* p_open = nullptr, VanGuiWindowFlags flags = 0); // return true if the modal is open, and you can start outputting to it.
    VANGUI_API void          EndPopup();                                                                         // only call EndPopup() if BeginPopupXXX() returns true!

    // Popups: open/close functions
    //  - OpenPopup(): set popup state to open. VanGuiPopupFlags are available for opening options.
    //  - If not modal: they can be closed by clicking anywhere outside them, or by pressing ESCAPE.
    //  - CloseCurrentPopup(): use inside the BeginPopup()/EndPopup() scope to close manually.
    //  - CloseCurrentPopup() is called by default by Selectable()/MenuItem() when activated (FIXME: need some options).
    //  - Use VanGuiPopupFlags_NoOpenOverExistingPopup to avoid opening a popup if there's already one at the same level. This is equivalent to e.g. testing for !IsAnyPopupOpen() prior to OpenPopup().
    //  - Use IsWindowAppearing() after BeginPopup() to tell if a window just opened.
    VANGUI_API void          OpenPopup(const char* str_id, VanGuiPopupFlags popup_flags = 0);                     // call to mark popup as open (don't call every frame!).
    VANGUI_API void          OpenPopup(VanGuiID id, VanGuiPopupFlags popup_flags = 0);                             // id overload to facilitate calling from nested stacks
    VANGUI_API void          OpenPopupOnItemClick(const char* str_id = nullptr, VanGuiPopupFlags popup_flags = 0);   // helper to open popup when clicked on last item. Default to VanGuiPopupFlags_MouseButtonRight == 1. (note: actually triggers on the mouse _released_ event to be consistent with popup behaviors)
    VANGUI_API void          CloseCurrentPopup();                                                                // manually close the popup we have begin-ed into.

    // Popups: Open+Begin popup combined functions helpers to create context menus.
    //  - Helpers to do OpenPopup+BeginPopup where the Open action is triggered by e.g. hovering an item and right-clicking.
    //  - IMPORTANT: Notice that BeginPopupContextXXX takes VanGuiPopupFlags just like OpenPopup() and unlike BeginPopup(). For full consistency, we may add VanGuiWindowFlags to the BeginPopupContextXXX functions in the future.
    //  - IMPORTANT: If you ever used the left mouse button with BeginPopupContextXXX() helpers before 1.92.6:
    //    - Before this version, OpenPopupOnItemClick(), BeginPopupContextItem(), BeginPopupContextWindow(), BeginPopupContextVoid() had 'a VanGuiPopupFlags popup_flags = 1' default value in their function signature.
    //    - Before: Explicitly passing a literal 0 meant VanGuiPopupFlags_MouseButtonLeft. The default = 1 meant VanGuiPopupFlags_MouseButtonRight.
    //    - After: The default = 0 means VanGuiPopupFlags_MouseButtonRight. Explicitly passing a literal 1 also means VanGuiPopupFlags_MouseButtonRight (if legacy behavior are enabled) or will assert (if legacy behavior are disabled).
    //    - TL;DR: if you don't want to use right mouse button for popups, always specify it explicitly using a named VanGuiPopupFlags_MouseButtonXXXX value.
    //    - Read "API BREAKING CHANGES" 2026/01/07 (1.92.6) entry in vangui.cpp or GitHub topic #9157 for all details.
    [[nodiscard]] VANGUI_API bool          BeginPopupContextItem(const char* str_id = nullptr, VanGuiPopupFlags popup_flags = 0);  // open+begin popup when clicked on last item. Use str_id==nullptr to associate the popup to previous item. If you want to use that on a non-interactive item such as Text() you need to pass in an explicit ID here. read comments in .cpp!
    [[nodiscard]] VANGUI_API bool          BeginPopupContextWindow(const char* str_id = nullptr, VanGuiPopupFlags popup_flags = 0);// open+begin popup when clicked on current window.
    [[nodiscard]] VANGUI_API bool          BeginPopupContextVoid(const char* str_id = nullptr, VanGuiPopupFlags popup_flags = 0);  // open+begin popup when clicked in void (where there are no windows).

    // Popups: query functions
    //  - IsPopupOpen(): return true if the popup is open at the current BeginPopup() level of the popup stack.
    //  - IsPopupOpen() with VanGuiPopupFlags_AnyPopupId: return true if any popup is open at the current BeginPopup() level of the popup stack.
    //  - IsPopupOpen() with VanGuiPopupFlags_AnyPopupId + VanGuiPopupFlags_AnyPopupLevel: return true if any popup is open.
    [[nodiscard]] VANGUI_API bool          IsPopupOpen(const char* str_id, VanGuiPopupFlags flags = 0);                         // return true if the popup is open.

    // Tables
    // - Full-featured replacement for old Columns API.
    // - See Demo->Tables for demo code. See top of vangui_tables.cpp for general commentary.
    // - See VanGuiTableFlags_ and VanGuiTableColumnFlags_ enums for a description of available flags.
    // The typical call flow is:
    // - 1. Call BeginTable(), early out if returning false.
    // - 2. Optionally call TableSetupColumn() to submit column name/flags/defaults.
    // - 3. Optionally call TableSetupScrollFreeze() to request scroll freezing of columns/rows.
    // - 4. Optionally call TableHeadersRow() to submit a header row. Names are pulled from TableSetupColumn() data.
    // - 5. Populate contents:
    //    - In most situations you can use TableNextRow() + TableSetColumnIndex(N) to start appending into a column.
    //    - If you are using tables as a sort of grid, where every column is holding the same type of contents,
    //      you may prefer using TableNextColumn() instead of TableNextRow() + TableSetColumnIndex().
    //      TableNextColumn() will automatically wrap-around into the next row if needed.
    //    - IMPORTANT: Comparatively to the old Columns() API, we need to call TableNextColumn() for the first column!
    //    - Summary of possible call flow:
    //        - TableNextRow() -> TableSetColumnIndex(0) -> Text("Hello 0") -> TableSetColumnIndex(1) -> Text("Hello 1")  // OK
    //        - TableNextRow() -> TableNextColumn()      -> Text("Hello 0") -> TableNextColumn()      -> Text("Hello 1")  // OK
    //        -                   TableNextColumn()      -> Text("Hello 0") -> TableNextColumn()      -> Text("Hello 1")  // OK: TableNextColumn() automatically gets to next row!
    //        - TableNextRow()                           -> Text("Hello 0")                                               // Not OK! Missing TableSetColumnIndex() or TableNextColumn()! Text will not appear!
    // - 5. Call EndTable()
    [[nodiscard]] VANGUI_API bool          BeginTable(const char* str_id, int columns, VanGuiTableFlags flags = 0, const VanVec2& outer_size = VanVec2(0.0f, 0.0f), float inner_width = 0.0f);
    VANGUI_API void          EndTable();                                         // only call EndTable() if BeginTable() returns true!
    VANGUI_API void          TableNextRow(VanGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f); // append into the first cell of a new row. 'min_row_height' include the minimum top and bottom padding aka CellPadding.y * 2.0f.
    [[nodiscard]] VANGUI_API bool          TableNextColumn();                                  // append into the next column (or first column of next row if currently in last column). Return true when column is visible.
    [[nodiscard]] VANGUI_API bool          TableSetColumnIndex(int column_n);                  // append into the specified column. Return true when column is visible.

    // Tables: Headers & Columns declaration
    // - Use TableSetupColumn() to specify label, resizing policy, default width/weight, id, various other flags etc.
    // - Use TableHeadersRow() to create a header row and automatically submit a TableHeader() for each column.
    //   Headers are required to perform: reordering, sorting, and opening the context menu.
    //   The context menu can also be made available in columns body using VanGuiTableFlags_ContextMenuInBody.
    // - You may manually submit headers using TableNextRow() + TableHeader() calls, but this is only useful in
    //   some advanced use cases (e.g. adding custom widgets in header row).
    // - Use TableSetupScrollFreeze() to lock columns/rows so they stay visible when scrolled. When freezing columns you would usually also use VanGuiTableColumnFlags_NoHide on them.
    VANGUI_API void          TableSetupColumn(const char* label, VanGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f, VanGuiID user_id = 0);
    VANGUI_API void          TableSetupScrollFreeze(int cols, int rows);         // lock columns/rows so they stay visible when scrolled.
    VANGUI_API void          TableHeader(const char* label);                     // submit one header cell manually (rarely used)
    VANGUI_API void          TableHeadersRow();                                  // submit a row with headers cells based on data provided to TableSetupColumn() + submit context menu
    VANGUI_API void          TableAngledHeadersRow();                            // submit a row with angled headers for every column with the VanGuiTableColumnFlags_AngledHeader flag. MUST BE FIRST ROW.

    // Tables: Sorting & Miscellaneous functions
    // - Sorting: call TableGetSortSpecs() to retrieve latest sort specs for the table. nullptr when not sorting.
    //   When 'sort_specs->SpecsDirty == true' you should sort your data. It will be true when sorting specs have
    //   changed since last call, or the first time. Make sure to set 'SpecsDirty = false' after sorting,
    //   else you may wastefully sort your data every frame!
    // - Functions args 'int column_n' treat the default value of -1 as the same as passing the current column index.
    [[nodiscard]] VANGUI_API VanGuiTableSortSpecs*  TableGetSortSpecs();                        // get latest sort specs for the table (nullptr if not sorting).  Lifetime: don't hold on this pointer over multiple frames or past any subsequent call to BeginTable().
    VANGUI_API int                   TableGetColumnCount();                      // return number of columns (value passed to BeginTable)
    VANGUI_API int                   TableGetColumnIndex();                      // return current column index.
    VANGUI_API int                   TableGetRowIndex();                         // return current row index (header rows are accounted for)
    [[nodiscard]] VANGUI_API const char*           TableGetColumnName(int column_n = -1);      // return "" if column didn't have a name declared by TableSetupColumn(). Pass -1 to use current column.
    VANGUI_API VanGuiTableColumnFlags TableGetColumnFlags(int column_n = -1);     // return column flags so you can query their Enabled/Visible/Sorted/Hovered status flags. Pass -1 to use current column.
    VANGUI_API void                  TableSetColumnEnabled(int column_n, bool v);// change user accessible enabled/disabled state of a column. Set to false to hide the column. User can use the context menu to change this themselves (right-click in headers, or right-click in columns body with VanGuiTableFlags_ContextMenuInBody)
    VANGUI_API int                   TableGetHoveredColumn();                    // return hovered column. return -1 when table is not hovered. return columns_count if the unused space at the right of visible columns is hovered. Can also use (TableGetColumnFlags() & VanGuiTableColumnFlags_IsHovered) instead.
    VANGUI_API void                  TableSetBgColor(VanGuiTableBgTarget target, VanU32 color, int column_n = -1);  // change the color of a cell, row, or column. See VanGuiTableBgTarget_ flags for details.

    // Legacy Columns API (prefer using Tables!)
    // - You can also use SameLine(pos_x) to mimic simplified columns.
    VANGUI_API void          Columns(int count = 1, const char* id = nullptr, bool borders = true);
    VANGUI_API void          NextColumn();                                                       // next column, defaults to current row or next row if the current row is finished
    VANGUI_API int           GetColumnIndex();                                                   // get current column index
    VANGUI_API float         GetColumnWidth(int column_index = -1);                              // get column width (in pixels). pass -1 to use current column
    VANGUI_API void          SetColumnWidth(int column_index, float width);                      // set column width (in pixels). pass -1 to use current column
    VANGUI_API float         GetColumnOffset(int column_index = -1);                             // get position of column line (in pixels, from the left side of the contents region). pass -1 to use current column, otherwise 0..GetColumnsCount() inclusive. column 0 is typically 0.0f
    VANGUI_API void          SetColumnOffset(int column_index, float offset_x);                  // set position of column line (in pixels, from the left side of the contents region). pass -1 to use current column
    VANGUI_API int           GetColumnsCount();

    // Tab Bars, Tabs
    // - Note: Tabs are automatically created by the docking system (when in 'docking' branch). Use this to create tab bars/tabs yourself.
    [[nodiscard]] VANGUI_API bool          BeginTabBar(const char* str_id, VanGuiTabBarFlags flags = 0);        // create and append into a TabBar
    VANGUI_API void          EndTabBar();                                                        // only call EndTabBar() if BeginTabBar() returns true!
    [[nodiscard]] VANGUI_API bool          BeginTabItem(const char* label, bool* p_open = nullptr, VanGuiTabItemFlags flags = 0); // create a Tab. Returns true if the Tab is selected.
    VANGUI_API void          EndTabItem();                                                       // only call EndTabItem() if BeginTabItem() returns true!
    [[nodiscard]] VANGUI_API bool          TabItemButton(const char* label, VanGuiTabItemFlags flags = 0);      // create a Tab behaving like a button. return true when clicked. cannot be selected in the tab bar.
    VANGUI_API void          SetTabItemClosed(const char* tab_or_docked_window_label);           // notify TabBar or Docking system of a closed tab/window ahead (useful to reduce visual flicker on reorderable tab bars). For tab-bar: call after BeginTabBar() and before Tab submissions. Otherwise call with a window name.

    // Logging/Capture
    // - All text output from the interface can be captured into tty/file/clipboard. By default, tree nodes are automatically opened during logging.
    VANGUI_API void          LogToTTY(int auto_open_depth = -1);                                 // start logging to tty (stdout)
    VANGUI_API void          LogToFile(int auto_open_depth = -1, const char* filename = nullptr);   // start logging to file
    VANGUI_API void          LogToClipboard(int auto_open_depth = -1);                           // start logging to OS clipboard
    VANGUI_API void          LogFinish();                                                        // stop logging (close file, etc.)
    VANGUI_API void          LogButtons();                                                       // helper to display buttons for logging to tty/file/clipboard
    VANGUI_API void          LogText(const char* fmt, ...) VAN_FMTARGS(1);                        // pass text data straight to log (without being displayed)
    VANGUI_API void          LogTextV(const char* fmt, va_list args) VAN_FMTLIST(1);

    // Drag and Drop
    // - On source items, call BeginDragDropSource(), if it returns true also call SetDragDropPayload() + EndDragDropSource().
    // - On target candidates, call BeginDragDropTarget(), if it returns true also call AcceptDragDropPayload() + EndDragDropTarget().
    // - If you stop calling BeginDragDropSource() the payload is preserved however it won't have a preview tooltip (we currently display a fallback "..." tooltip, see #1725)
    // - An item can be both drag source and drop target.
    [[nodiscard]] VANGUI_API bool          BeginDragDropSource(VanGuiDragDropFlags flags = 0);                                      // call after submitting an item which may be dragged. when this return true, you can call SetDragDropPayload() + EndDragDropSource()
    [[nodiscard]] VANGUI_API bool          SetDragDropPayload(const char* type, const void* data, size_t sz, VanGuiCond cond = 0);  // type is a user defined string of maximum 32 characters. Strings starting with '_' are reserved for VanGUI internal types. Data is copied and held by vangui. Return true when payload has been accepted.
    VANGUI_API void          EndDragDropSource();                                                                    // only call EndDragDropSource() if BeginDragDropSource() returns true!
    [[nodiscard]] VANGUI_API bool                  BeginDragDropTarget();                                                          // call after submitting an item that may receive a payload. If this returns true, you can call AcceptDragDropPayload() + EndDragDropTarget()
    [[nodiscard]] VANGUI_API const VanGuiPayload*   AcceptDragDropPayload(const char* type, VanGuiDragDropFlags flags = 0);          // accept contents of a given type. If VanGuiDragDropFlags_AcceptBeforeDelivery is set you can peek into the payload before the mouse button is released.
    VANGUI_API void                  EndDragDropTarget();                                                            // only call EndDragDropTarget() if BeginDragDropTarget() returns true!
    [[nodiscard]] VANGUI_API const VanGuiPayload*   GetDragDropPayload();                                                           // peek directly into the current payload from anywhere. returns nullptr when drag and drop is finished or inactive. use VanGuiPayload::IsDataType() to test for the payload type.

    // Disabling [BETA API]
    // - Disable all user interactions and dim items visuals (applying style.DisabledAlpha over current colors)
    // - Those can be nested but it cannot be used to enable an already disabled section (a single BeginDisabled(true) in the stack is enough to keep everything disabled)
    // - Tooltips windows are automatically opted out of disabling. Note that IsItemHovered() by default returns false on disabled items, unless using VanGuiHoveredFlags_AllowWhenDisabled.
    // - BeginDisabled(false)/EndDisabled() essentially does nothing but is provided to facilitate use of boolean expressions (as a micro-optimization: if you have tens of thousands of BeginDisabled(false)/EndDisabled() pairs, you might want to reformulate your code to avoid making those calls)
    VANGUI_API void          BeginDisabled(bool disabled = true);
    VANGUI_API void          EndDisabled();

    // Clipping
    // - Mouse hovering is affected by VanGui::PushClipRect() calls, unlike direct calls to VanDrawList::PushClipRect() which are render only.
    VANGUI_API void          PushClipRect(const VanVec2& clip_rect_min, const VanVec2& clip_rect_max, bool intersect_with_current_clip_rect);
    VANGUI_API void          PopClipRect();

    // Focus, Activation
    VANGUI_API void          SetItemDefaultFocus();                                              // make last item the default focused item of a newly appearing window.
    VANGUI_API void          SetKeyboardFocusHere(int offset = 0);                               // focus keyboard on the next widget. Use positive 'offset' to access sub components of a multiple component widget. Use -1 to access previous widget.

    // Keyboard/Gamepad Navigation
    VANGUI_API void          SetNavCursorVisible(bool visible);                                  // alter visibility of keyboard/gamepad cursor. by default: show when using an arrow key, hide when clicking with mouse.

    // Overlapping mode
    VANGUI_API void          SetNextItemAllowOverlap();                                          // allow next item to be overlapped by a subsequent item. Typically useful with InvisibleButton(), Selectable(), TreeNode() covering an area where subsequent items may need to be added. Note that both Selectable() and TreeNode() have dedicated flags doing this.

    // Item/Widgets Utilities and Query Functions
    // - Most of the functions are referring to the previous Item that has been submitted.
    // - See Demo Window under "Widgets->Querying Status" for an interactive visualization of most of those functions.
    [[nodiscard]] VANGUI_API bool          IsItemHovered(VanGuiHoveredFlags flags = 0);                         // is the last item hovered? (and usable, aka not blocked by a popup, etc.). See VanGuiHoveredFlags for more options.
    [[nodiscard]] VANGUI_API bool          IsItemActive();                                                     // is the last item active? (e.g. button being held, text field being edited. This will continuously return true while holding mouse button on an item. Items that don't interact will always return false)
    [[nodiscard]] VANGUI_API bool          IsItemFocused();                                                    // is the last item focused for keyboard/gamepad navigation?
    [[nodiscard]] VANGUI_API bool          IsItemClicked(VanGuiMouseButton mouse_button = 0);                   // is the last item hovered and mouse clicked on? (**)  == IsMouseClicked(mouse_button) && IsItemHovered()Important. (**) this is NOT equivalent to the behavior of e.g. Button(). Read comments in function definition.
    [[nodiscard]] VANGUI_API bool          IsItemVisible();                                                    // is the last item visible? (items may be out of sight because of clipping/scrolling)
    [[nodiscard]] VANGUI_API bool          IsItemEdited();                                                     // did the last item modify its underlying value this frame? or was pressed? This is generally the same as the "bool" return value of many widgets.
    [[nodiscard]] VANGUI_API bool          IsItemActivated();                                                  // was the last item just made active (item was previously inactive).
    [[nodiscard]] VANGUI_API bool          IsItemDeactivated();                                                // was the last item just made inactive (item was previously active). Useful for Undo/Redo patterns with widgets that require continuous editing.
    [[nodiscard]] VANGUI_API bool          IsItemDeactivatedAfterEdit();                                       // was the last item just made inactive and made a value change when it was active? (e.g. Slider/Drag moved). Useful for Undo/Redo patterns with widgets that require continuous editing. Note that you may get false positives (some widgets such as Combo()/ListBox()/Selectable() will return true even when clicking an already selected item).
    [[nodiscard]] VANGUI_API bool          IsItemToggledOpen();                                                // was the last item open state toggled? set by TreeNode().
    [[nodiscard]] VANGUI_API bool          IsAnyItemHovered();                                                 // is any item hovered?
    [[nodiscard]] VANGUI_API bool          IsAnyItemActive();                                                  // is any item active?
    [[nodiscard]] VANGUI_API bool          IsAnyItemFocused();                                                 // is any item focused?
    VANGUI_API VanGuiID       GetItemID();                                                        // get ID of last item (~~ often same VanGui::GetID(label) beforehand)
    VANGUI_API VanVec2        GetItemRectMin();                                                   // get upper-left bounding rectangle of the last item (screen space)
    VANGUI_API VanVec2        GetItemRectMax();                                                   // get lower-right bounding rectangle of the last item (screen space)
    VANGUI_API VanVec2        GetItemRectSize();                                                  // get size of last item
    VANGUI_API VanGuiItemFlags GetItemFlags();                                                    // get generic flags of last item

    // Docking
    // [BETA API] Enable with io.ConfigFlags |= VanGuiConfigFlags_DockingEnable.
    // Note: You can use most Docking facilities without calling any API. You DO NOT need to call DockSpace() to use Docking!
    // - To dock windows: if io.ConfigDockingWithShift == false (default) drag window from their title bar.
    // - To dock windows: if io.ConfigDockingWithShift == true: hold SHIFT anywhere while moving windows.
    // About DockSpace:
    // - Use DockSpace() to create an explicit dock node _within_ an existing window. See Docking demo for details.
    // - DockSpace() needs to be submitted _before_ any window they can host. If you use a dockspace, submit it early in your application.
    // - Important: Dockspaces need to be kept alive if hidden, otherwise windows docked into it will be undocked.
    //   e.g. if you have multiple tabs with a dockspace in each tab: submit all dockspaces every frame even if the hosting tab is not visible!
    VANGUI_API VanGuiID   DockSpace(VanGuiID dockspace_id, const VanVec2& size = VanVec2(0.0f, 0.0f), VanGuiDockNodeFlags flags = 0, const VanGuiWindowClass* window_class = nullptr);
    VANGUI_API VanGuiID   DockSpaceOverViewport(VanGuiID dockspace_id = 0, const VanGuiViewport* viewport = nullptr, VanGuiDockNodeFlags flags = 0, const VanGuiWindowClass* window_class = nullptr);
    VANGUI_API void       SetNextWindowDockID(VanGuiID dock_id, VanGuiCond cond = 0);           // set next window dock id
    VANGUI_API void       SetNextWindowClass(const VanGuiWindowClass* window_class);             // set next window class
    [[nodiscard]] VANGUI_API VanGuiID GetWindowDockID();
    [[nodiscard]] VANGUI_API bool     IsWindowDocked();                                          // is current window docked into another window?

    // Viewports
    // - Currently represents the Platform Window created by the application which is hosting our VanGUI windows.
    // - In 'docking' branch with multi-viewport enabled, we extend this concept to have multiple active viewports.
    // - In the future we will extend this concept further to also represent Platform Monitor and support a "no main platform window" operation mode.
    [[nodiscard]] VANGUI_API VanGuiViewport* GetMainViewport();                                  // return primary/default viewport. This can never be NULL.
    [[nodiscard]] VANGUI_API VanGuiViewport* GetWindowViewport();                                // get viewport currently associated to the current window.
    [[nodiscard]] VANGUI_API VanGuiViewport* FindViewportByID(VanGuiID id);                      // this is a helper for backends.
    [[nodiscard]] VANGUI_API VanGuiViewport* FindViewportByPlatformHandle(void* platform_handle); // this is a helper for backends. the type platform_handle is decided by the backend (e.g. HWND, MyWindow*, GLFWwindow* etc.)
    VANGUI_API void          SetNextWindowViewport(VanGuiID viewport_id);                        // set next window viewport

    // Platform/Renderer functions
    // - Called by user code if using custom VanGuiPlatformIO. Not needed if you are using a standard backend.
    VANGUI_API void UpdatePlatformWindows();                                                                                    // call in main loop. will call Platform_CreateWindow/Resize/etc. as needed.
    VANGUI_API void RenderPlatformWindowsDefault(void* platform_render_arg = nullptr, void* renderer_render_arg = nullptr);    // call in main loop. will call Renderer_RenderWindow/SwapBuffers platform functions for each secondary viewport which doesn't have the VanGuiViewportFlags_Minimized flag set. May be reimplemented by user for custom rendering needs.
    VANGUI_API void DestroyPlatformWindows();                                                                                   // call DestroyWindow platform functions for all viewports. call from backend Shutdown() if you need to close platform windows before VanGUI shutdown. otherwise will be called by DestroyContext().

    // Background/Foreground Draw Lists
    [[nodiscard]] VANGUI_API VanDrawList*   GetBackgroundDrawList();                                            // this draw list will be the first rendered one. Useful to quickly draw shapes/text behind VanGUI contents.
    [[nodiscard]] VANGUI_API VanDrawList*   GetForegroundDrawList();                                            // this draw list will be the last rendered one. Useful to quickly draw shapes/text over VanGUI contents.

    // Miscellaneous Utilities
    [[nodiscard]] VANGUI_API bool          IsRectVisible(const VanVec2& size);                                  // test if rectangle (of given size, starting from cursor position) is visible / not clipped.
    [[nodiscard]] VANGUI_API bool          IsRectVisible(const VanVec2& rect_min, const VanVec2& rect_max);      // test if rectangle (in screen space) is visible / not clipped. to perform coarse clipping on user's side.
    VANGUI_API double        GetTime();                                                          // get global vangui time. incremented by io.DeltaTime every frame.
    VANGUI_API int           GetFrameCount();                                                    // get global vangui frame count. incremented by 1 every frame.
    [[nodiscard]] VANGUI_API VanDrawListSharedData* GetDrawListSharedData();                                    // you may use this when creating your own VanDrawList instances.
    [[nodiscard]] VANGUI_API const char*   GetStyleColorName(VanGuiCol idx);                                    // get a string corresponding to the enum value (for display, saving, etc.).
    VANGUI_API void          SetStateStorage(VanGuiStorage* storage);                             // replace current window storage with our own (if you want to manipulate it yourself, typically clear subsection of it)
    [[nodiscard]] VANGUI_API VanGuiStorage* GetStateStorage();

    // Text Utilities
    VANGUI_API VanVec2        CalcTextSize(const char* text, const char* text_end = nullptr, bool hide_text_after_double_hash = false, float wrap_width = -1.0f);

    // Color Utilities
    VANGUI_API VanVec4        ColorConvertU32ToFloat4(VanU32 in);
    VANGUI_API VanU32         ColorConvertFloat4ToU32(const VanVec4& in);
    VANGUI_API void          ColorConvertRGBtoHSV(float r, float g, float b, float& out_h, float& out_s, float& out_v);
    VANGUI_API void          ColorConvertHSVtoRGB(float h, float s, float v, float& out_r, float& out_g, float& out_b);

    // Inputs Utilities: Raw Keyboard/Mouse/Gamepad Access
    // - Consider using the Shortcut() function instead of IsKeyPressed()/IsKeyChordPressed()! Shortcut() is easier to use and better featured (can do focus routing check).
    // - the VanGuiKey enum contains all possible keyboard, mouse and gamepad inputs (e.g. VanGuiKey_A, VanGuiKey_MouseLeft, VanGuiKey_GamepadDpadUp...).
    // - (legacy: before v1.87 (2022-02), we used VanGuiKey < 512 values to carry native/user indices as defined by each backends. This was obsoleted in 1.87 (2022-02) and completely removed in 1.91.5 (2024-11).)
    [[nodiscard]] VANGUI_API bool          IsKeyDown(VanGuiKey key);                                            // is key being held.
    [[nodiscard]] VANGUI_API bool          IsKeyPressed(VanGuiKey key, bool repeat = true);                     // was key pressed (went from !Down to Down)? Repeat rate uses io.KeyRepeatDelay / KeyRepeatRate.
    [[nodiscard]] VANGUI_API bool          IsKeyReleased(VanGuiKey key);                                        // was key released (went from Down to !Down)?
    [[nodiscard]] VANGUI_API bool          IsKeyChordPressed(VanGuiKeyChord key_chord);                         // was key chord (mods + key) pressed, e.g. you can pass 'VanGuiMod_Ctrl | VanGuiKey_S' as a key-chord. This doesn't do any routing or focus check, please consider using Shortcut() function instead.
    VANGUI_API int           GetKeyPressedAmount(VanGuiKey key, float repeat_delay, float rate);  // uses provided repeat rate/delay. return a count, most often 0 or 1 but might be >1 if RepeatRate is small enough that DeltaTime > RepeatRate
    [[nodiscard]] VANGUI_API const char*   GetKeyName(VanGuiKey key);                                           // [DEBUG] returns English name of the key. Those names are provided for debugging purpose and are not meant to be saved persistently nor compared.
    VANGUI_API void          SetNextFrameWantCaptureKeyboard(bool want_capture_keyboard);        // Override io.WantCaptureKeyboard flag next frame (said flag is left for your application to handle, typically when true it instructs your app to ignore inputs). e.g. force capture keyboard when your widget is being hovered. This is equivalent to setting "io.WantCaptureKeyboard = want_capture_keyboard"; after the next NewFrame() call.

    // Inputs Utilities: Shortcut Testing & Routing
    // - Typical use is e.g.: 'if (VanGui::Shortcut(VanGuiMod_Ctrl | VanGuiKey_S)) { ... }'.
    // - Flags: Default route use VanGuiInputFlags_RouteFocused, but see VanGuiInputFlags_RouteGlobal and other options in VanGuiInputFlags_!
    // - Flags: Use VanGuiInputFlags_Repeat to support repeat.
    // - VanGuiKeyChord = a VanGuiKey + optional VanGuiMod_Alt/VanGuiMod_Ctrl/VanGuiMod_Shift/VanGuiMod_Super.
    //       VanGuiKey_C                          // Accepted by functions taking VanGuiKey or VanGuiKeyChord arguments
    //       VanGuiMod_Ctrl | VanGuiKey_C          // Accepted by functions taking VanGuiKeyChord arguments
    //   only VanGuiMod_XXX values are legal to combine with an VanGuiKey. You CANNOT combine two VanGuiKey values.
    // - The general idea is that several callers may register interest in a shortcut, and only one owner gets it.
    //      Parent   -> call Shortcut(Ctrl+S)    // When Parent is focused, Parent gets the shortcut.
    //        Child1 -> call Shortcut(Ctrl+S)    // When Child1 is focused, Child1 gets the shortcut (Child1 overrides Parent shortcuts)
    //        Child2 -> no call                  // When Child2 is focused, Parent gets the shortcut.
    //   The whole system is order independent, so if Child1 makes its calls before Parent, results will be identical.
    //   This is an important property as it facilitate working with foreign code or larger codebase.
    // - To understand the difference:
    //   - IsKeyChordPressed() compares mods and call IsKeyPressed()
    //     -> the function has no side-effect.
    //   - Shortcut() submits a route, routes are resolved, if it currently can be routed it calls IsKeyChordPressed()
    //     -> the function has (desirable) side-effects as it can prevents another call from getting the route.
    // - Visualize registered routes in 'Metrics/Debugger->Inputs'.
    [[nodiscard]] VANGUI_API bool          Shortcut(VanGuiKeyChord key_chord, VanGuiInputFlags flags = 0);
    VANGUI_API void          SetNextItemShortcut(VanGuiKeyChord key_chord, VanGuiInputFlags flags = 0);

    // Inputs Utilities: Key/Input Ownership [BETA]
    // - One common use case would be to allow your items to disable standard inputs behaviors such
    //   as Tab or Alt key handling, Mouse Wheel scrolling, etc.
    //   e.g. `Button(...); if (SetItemKeyOwner(VanGuiKey_MouseWheelY)) { ... }` to make hovering/activating a button disable wheel for scrolling.
    // - Reminder VanGuiKey enum include access to mouse buttons and gamepad, so key ownership can apply to them.
    // - The return value of SetItemKeyOwner() says if ownership has been requested for the item, which is a shortcut to calling yet non-public TestKeyOwner() function.
    // - Many related features are still in vangui_internal.h. For instance, most IsKeyXXX()/IsMouseXXX() functions have an owner-id-aware version.
    [[nodiscard]] VANGUI_API bool          SetItemKeyOwner(VanGuiKey key);                                      // Set key owner to last item ID if it is hovered or active. Return true when ownership has been set. Roughly equivalent to 'if (TestKeyOwner(key, GetItemID()) && (IsItemHovered() || IsItemActive())) { SetKeyOwner(key, GetItemID());'.

    // Inputs Utilities: Mouse
    // - To refer to a mouse button, you may use named enums in your code e.g. VanGuiMouseButton_Left, VanGuiMouseButton_Right.
    // - You can also use regular integer: it is forever guaranteed that 0=Left, 1=Right, 2=Middle.
    // - Dragging operations are only reported after mouse has moved a certain distance away from the initial clicking position (see 'lock_threshold' and 'io.MouseDraggingThreshold')
    [[nodiscard]] VANGUI_API bool          IsMouseDown(VanGuiMouseButton button);                               // is mouse button held?
    [[nodiscard]] VANGUI_API bool          IsMouseClicked(VanGuiMouseButton button, bool repeat = false);       // did mouse button clicked? (went from !Down to Down). Same as GetMouseClickedCount() == 1.
    [[nodiscard]] VANGUI_API bool          IsMouseReleased(VanGuiMouseButton button);                           // did mouse button released? (went from Down to !Down)
    [[nodiscard]] VANGUI_API bool          IsMouseDoubleClicked(VanGuiMouseButton button);                      // did mouse button double-clicked? Same as GetMouseClickedCount() == 2. (note that a double-click will also report IsMouseClicked() == true)
    [[nodiscard]] VANGUI_API bool          IsMouseChordPressed(VanGuiKeyChord key_chord);                       // is a mouse chord (key-chord where one of the key is a mouse button) pressed? e.g. IsMouseChordPressed(VanGuiMod_Ctrl | VanGuiKey_MouseLeft).
    [[nodiscard]] VANGUI_API bool          IsMouseReleasedWithDelay(VanGuiMouseButton button, float delay);     // delayed mouse release (use very sparingly!). Generally used with 'delay >= io.MouseDoubleClickTime' + combined with a 'io.MouseClickedLastCount==1' test. This is a very rarely used UI idiom, but some apps use this: e.g. MS Explorer single click on an icon to rename.
    VANGUI_API int           GetMouseClickedCount(VanGuiMouseButton button);                      // return the number of successive mouse-clicks at the time where a click happen (otherwise 0).
    [[nodiscard]] VANGUI_API bool          IsMouseHoveringRect(const VanVec2& r_min, const VanVec2& r_max, bool clip = true);// is mouse hovering given bounding rect (in screen space). clipped by current clipping settings, but disregarding of other consideration of focus/window ordering/popup-block.
    [[nodiscard]] VANGUI_API bool          IsMousePosValid(const VanVec2* mouse_pos = nullptr);                    // by convention we use (-FLT_MAX,-FLT_MAX) to denote that there is no mouse available
    [[nodiscard]] VANGUI_API bool          IsAnyMouseDown();                                                   // [WILL OBSOLETE] is any mouse button held? This was designed for backends, but prefer having backend maintain a mask of held mouse buttons, because upcoming input queue system will make this invalid.
    VANGUI_API VanVec2        GetMousePos();                                                      // shortcut to VanGui::GetIO().MousePos provided by user, to be consistent with other calls
    VANGUI_API VanVec2        GetMousePosOnOpeningCurrentPopup();                                 // retrieve mouse position at the time of opening popup we have BeginPopup() into (helper to avoid user backing that value themselves)
    [[nodiscard]] VANGUI_API bool          IsMouseDragging(VanGuiMouseButton button, float lock_threshold = -1.0f);         // is mouse dragging? (uses io.MouseDraggingThreshold if lock_threshold < 0.0f)
    VANGUI_API VanVec2        GetMouseDragDelta(VanGuiMouseButton button = 0, float lock_threshold = -1.0f);   // return the delta from the initial clicking position while the mouse button is pressed or was just released. This is locked and return 0.0f until the mouse moves past a distance threshold at least once (uses io.MouseDraggingThreshold if lock_threshold < 0.0f)
    VANGUI_API void          ResetMouseDragDelta(VanGuiMouseButton button = 0);                   //
    VANGUI_API VanGuiMouseCursor GetMouseCursor();                                                // get desired mouse cursor shape. Important: reset in VanGui::NewFrame(), this is updated during the frame. valid before Render(). If you use software rendering by setting io.MouseDrawCursor VanGui will render those for you
    VANGUI_API void          SetMouseCursor(VanGuiMouseCursor cursor_type);                       // set desired mouse cursor shape
    VANGUI_API void          SetNextFrameWantCaptureMouse(bool want_capture_mouse);              // Override io.WantCaptureMouse flag next frame (said flag is left for your application to handle, typical when true it instructs your app to ignore inputs). This is equivalent to setting "io.WantCaptureMouse = want_capture_mouse;" after the next NewFrame() call.

    // Clipboard Utilities
    // - Also see the LogToClipboard() function to capture GUI into clipboard, or easily output text data to the clipboard.
    [[nodiscard]] VANGUI_API const char*   GetClipboardText();
    VANGUI_API void          SetClipboardText(const char* text);

    // Settings/.Ini Utilities
    // - The disk functions are automatically called if io.IniFilename != nullptr (default is "vangui.ini").
    // - Set io.IniFilename to nullptr to load/save manually. Read io.WantSaveIniSettings description about handling .ini saving manually.
    // - Important: default value "vangui.ini" is relative to current working dir! Most apps will want to lock this to an absolute path (e.g. same path as executables).
    VANGUI_API void          LoadIniSettingsFromDisk(const char* ini_filename);                  // call after CreateContext() and before the first call to NewFrame(). NewFrame() automatically calls LoadIniSettingsFromDisk(io.IniFilename).
    VANGUI_API void          LoadIniSettingsFromMemory(const char* ini_data, size_t ini_size=0); // call after CreateContext() and before the first call to NewFrame() to provide .ini data from your own data source.
    VANGUI_API void          SaveIniSettingsToDisk(const char* ini_filename);                    // this is automatically called (if io.IniFilename is not empty) a few seconds after any modification that should be reflected in the .ini file (and also by DestroyContext).
    [[nodiscard]] VANGUI_API const char*   SaveIniSettingsToMemory(size_t* out_ini_size = nullptr);               // return a zero-terminated string with the .ini data which you can save by your own mean. call when io.WantSaveIniSettings is set, then save data by your own mean and clear io.WantSaveIniSettings.

    // Debug Utilities
    // - Your main debugging friend is the ShowMetricsWindow() function.
    // - Interactive tools are all accessible from the 'VanGUI Demo->Tools' menu.
    VANGUI_API void          DebugTextEncoding(const char* text);
    VANGUI_API void          DebugFlashStyleColor(VanGuiCol idx);
    VANGUI_API void          DebugStartItemPicker();
    [[nodiscard]] VANGUI_API bool          DebugCheckVersionAndDataLayout(const char* version_str, size_t sz_io, size_t sz_style, size_t sz_vec2, size_t sz_vec4, size_t sz_drawvert, size_t sz_drawidx); // This is called by VANGUI_CHECKVERSION() macro.
#ifndef VANGUI_DISABLE_DEBUG_TOOLS
    VANGUI_API void          DebugLog(const char* fmt, ...)           VAN_FMTARGS(1); // Call via VANGUI_DEBUG_LOG() for maximum stripping in caller code!
    VANGUI_API void          DebugLogV(const char* fmt, va_list args) VAN_FMTLIST(1);
#endif

    // Memory Allocators
    // - Those functions are not reliant on the current context.
    // - DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()
    //   for each static/DLL boundary you are calling from. Read "Context and Memory Allocators" section of vangui.cpp for more details.
    VANGUI_API void          SetAllocatorFunctions(VanGuiMemAllocFunc alloc_func, VanGuiMemFreeFunc free_func, void* user_data = nullptr);
    VANGUI_API void          GetAllocatorFunctions(VanGuiMemAllocFunc* p_alloc_func, VanGuiMemFreeFunc* p_free_func, void** p_user_data);
    VANGUI_API void*         MemAlloc(size_t size);
    VANGUI_API void          MemFree(void* ptr);

} // namespace VanGui

//-----------------------------------------------------------------------------
// [SECTION] Flags & Enumerations
//-----------------------------------------------------------------------------

// Flags for VanGui::Begin()
// (Those are per-window flags. There are shared flags in VanGuiIO: io.ConfigWindowsResizeFromEdges and io.ConfigWindowsMoveFromTitleBarOnly)
enum VanGuiWindowFlags_
{
    VanGuiWindowFlags_None                   = 0,
    VanGuiWindowFlags_NoTitleBar             = 1 << 0,   // Disable title-bar
    VanGuiWindowFlags_NoResize               = 1 << 1,   // Disable user resizing with the lower-right grip
    VanGuiWindowFlags_NoMove                 = 1 << 2,   // Disable user moving the window
    VanGuiWindowFlags_NoScrollbar            = 1 << 3,   // Disable scrollbars (window can still scroll with mouse or programmatically)
    VanGuiWindowFlags_NoScrollWithMouse      = 1 << 4,   // Disable user vertically scrolling with mouse wheel. On child window, mouse wheel will be forwarded to the parent unless NoScrollbar is also set.
    VanGuiWindowFlags_NoCollapse             = 1 << 5,   // Disable user collapsing window by double-clicking on it. Also referred to as Window Menu Button (e.g. within a docking node).
    VanGuiWindowFlags_AlwaysAutoResize       = 1 << 6,   // Resize every window to its content every frame
    VanGuiWindowFlags_NoBackground           = 1 << 7,   // Disable drawing background color (WindowBg, etc.) and outside border. Similar as using SetNextWindowBgAlpha(0.0f).
    VanGuiWindowFlags_NoSavedSettings        = 1 << 8,   // Never load/save settings in .ini file
    VanGuiWindowFlags_NoMouseInputs          = 1 << 9,   // Disable catching mouse, hovering test with pass through.
    VanGuiWindowFlags_MenuBar                = 1 << 10,  // Has a menu-bar
    VanGuiWindowFlags_HorizontalScrollbar    = 1 << 11,  // Allow horizontal scrollbar to appear (off by default). You may use SetNextWindowContentSize(VanVec2(width,0.0f)); prior to calling Begin() to specify width. Read code in vangui_demo in the "Horizontal Scrolling" section.
    VanGuiWindowFlags_NoFocusOnAppearing     = 1 << 12,  // Disable taking focus when transitioning from hidden to visible state
    VanGuiWindowFlags_NoBringToFrontOnFocus  = 1 << 13,  // Disable bringing window to front when taking focus (e.g. clicking on it or programmatically giving it focus)
    VanGuiWindowFlags_AlwaysVerticalScrollbar= 1 << 14,  // Always show vertical scrollbar (even if ContentSize.y < Size.y)
    VanGuiWindowFlags_AlwaysHorizontalScrollbar=1<< 15,  // Always show horizontal scrollbar (even if ContentSize.x < Size.x)
    VanGuiWindowFlags_NoNavInputs            = 1 << 16,  // No keyboard/gamepad navigation within the window
    VanGuiWindowFlags_NoNavFocus             = 1 << 17,  // No focusing toward this window with keyboard/gamepad navigation (e.g. skipped by Ctrl+Tab)
    VanGuiWindowFlags_UnsavedDocument        = 1 << 18,  // Display a dot next to the title. When used in a tab/docking context, tab is selected when clicking the X + closure is not assumed (will wait for user to stop submitting the tab). Otherwise closure is assumed when pressing the X, so if you keep submitting the tab may reappear at end of tab bar.
    VanGuiWindowFlags_NoDocking              = 1 << 19,  // Disable any kind of docking of this window
    VanGuiWindowFlags_NoBringToDisplayOnFocus= 1 << 20,  // Disable bringing window to front when focused (SHIFT+click in title bar, clicking in title bar when already focused)
    VanGuiWindowFlags_NoNav                  = VanGuiWindowFlags_NoNavInputs | VanGuiWindowFlags_NoNavFocus,
    VanGuiWindowFlags_NoDecoration           = VanGuiWindowFlags_NoTitleBar | VanGuiWindowFlags_NoResize | VanGuiWindowFlags_NoScrollbar | VanGuiWindowFlags_NoCollapse,
    VanGuiWindowFlags_NoInputs               = VanGuiWindowFlags_NoMouseInputs | VanGuiWindowFlags_NoNavInputs | VanGuiWindowFlags_NoNavFocus,

    // [Internal]
    VanGuiWindowFlags_ChildWindow            = 1 << 24,  // Don't use! For internal use by BeginChild()
    VanGuiWindowFlags_Tooltip                = 1 << 25,  // Don't use! For internal use by BeginTooltip()
    VanGuiWindowFlags_Popup                  = 1 << 26,  // Don't use! For internal use by BeginPopup()
    VanGuiWindowFlags_Modal                  = 1 << 27,  // Don't use! For internal use by BeginPopupModal()
    VanGuiWindowFlags_ChildMenu              = 1 << 28,  // Don't use! For internal use by BeginMenu()

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    //VanGuiWindowFlags_NavFlattened           = 1 << 29,  // Obsoleted in 1.90.9: moved to VanGuiChildFlags. BeginChild(name, size, 0, VanGuiWindowFlags_NavFlattened)           --> BeginChild(name, size, VanGuiChildFlags_NavFlattened, 0)
    //VanGuiWindowFlags_AlwaysUseWindowPadding = 1 << 30,  // Obsoleted in 1.90.0: moved to VanGuiChildFlags. BeginChild(name, size, 0, VanGuiWindowFlags_AlwaysUseWindowPadding) --> BeginChild(name, size, VanGuiChildFlags_AlwaysUseWindowPadding, 0)
#endif
};

// Flags for VanGui::BeginChild()
// (Legacy: bit 0 must always correspond to VanGuiChildFlags_Borders to be backward compatible with old API using 'bool border = false'.)
// About using AutoResizeX/AutoResizeY flags:
// - May be combined with SetNextWindowSizeConstraints() to set a min/max size for each axis (see "Demo->Child->Auto-resize with Constraints").
// - Size measurement for a given axis is only performed when the child window is within visible boundaries, or is just appearing.
//   - This allows BeginChild() to return false when not within boundaries (e.g. when scrolling), which is more optimal. BUT it won't update its auto-size while clipped.
//     While not perfect, it is a better default behavior as the always-on performance gain is more valuable than the occasional "resizing after becoming visible again" glitch.
//   - You may also use VanGuiChildFlags_AlwaysAutoResize to force an update even when child window is not in view.
//     HOWEVER PLEASE UNDERSTAND THAT DOING SO WILL PREVENT BeginChild() FROM EVER RETURNING FALSE, disabling benefits of coarse clipping.
enum VanGuiChildFlags_
{
    VanGuiChildFlags_None                    = 0,
    VanGuiChildFlags_Borders                 = 1 << 0,   // Show an outer border and enable WindowPadding. (IMPORTANT: this is always == 1 == true for legacy reason)
    VanGuiChildFlags_AlwaysUseWindowPadding  = 1 << 1,   // Pad with style.WindowPadding even if no border are drawn (no padding by default for non-bordered child windows because it makes more sense)
    VanGuiChildFlags_ResizeX                 = 1 << 2,   // Allow resize from right border (layout direction). Enable .ini saving (unless VanGuiWindowFlags_NoSavedSettings passed to window flags)
    VanGuiChildFlags_ResizeY                 = 1 << 3,   // Allow resize from bottom border (layout direction). "
    VanGuiChildFlags_AutoResizeX             = 1 << 4,   // Enable auto-resizing width. Read "IMPORTANT: Size measurement" details above.
    VanGuiChildFlags_AutoResizeY             = 1 << 5,   // Enable auto-resizing height. Read "IMPORTANT: Size measurement" details above.
    VanGuiChildFlags_AlwaysAutoResize        = 1 << 6,   // Combined with AutoResizeX/AutoResizeY. Always measure size even when child is hidden, always return true, always disable clipping optimization! NOT RECOMMENDED.
    VanGuiChildFlags_FrameStyle              = 1 << 7,   // Style the child window like a framed item: use FrameBg, FrameRounding, FrameBorderSize, FramePadding instead of ChildBg, ChildRounding, ChildBorderSize, WindowPadding.
    VanGuiChildFlags_NavFlattened            = 1 << 8,   // [BETA] Share focus scope, allow keyboard/gamepad navigation to cross over parent border to this child or between sibling child windows.

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    //VanGuiChildFlags_Border                = VanGuiChildFlags_Borders,  // Renamed in 1.91.1 (August 2024) for consistency.
#endif
};

// Flags for VanGui::PushItemFlag()
// (Those are shared by all submitted items)
enum VanGuiItemFlags_
{
    VanGuiItemFlags_None                     = 0,        // (Default)
    VanGuiItemFlags_NoTabStop                = 1 << 0,   // false    // Disable keyboard tabbing. This is a "lighter" version of VanGuiItemFlags_NoNav.
    VanGuiItemFlags_NoNav                    = 1 << 1,   // false    // Disable any form of focusing (keyboard/gamepad directional navigation and SetKeyboardFocusHere() calls).
    VanGuiItemFlags_NoNavDefaultFocus        = 1 << 2,   // false    // Disable item being a candidate for default focus (e.g. used by title bar items).
    VanGuiItemFlags_ButtonRepeat             = 1 << 3,   // false    // Any button-like behavior will have repeat mode enabled (based on io.KeyRepeatDelay and io.KeyRepeatRate values). Note that you can also call IsItemActive() after any button to tell if it is being held.
    VanGuiItemFlags_AutoClosePopups          = 1 << 4,   // true     // MenuItem()/Selectable() automatically close their parent popup window.
    VanGuiItemFlags_AllowDuplicateId         = 1 << 5,   // false    // Allow submitting an item with the same identifier as an item already submitted this frame without triggering a warning tooltip if io.ConfigDebugHighlightIdConflicts is set.
    VanGuiItemFlags_Disabled                 = 1 << 6,   // false    // [Internal] Disable interactions. DOES NOT affect visuals. This is used by BeginDisabled()/EndDisabled() and only provided here so you can read back via GetItemFlags().
};

// Flags for VanGui::InputText()
// (Those are per-item flags. There are shared flags in VanGuiIO: io.ConfigInputTextCursorBlink and io.ConfigInputTextEnterKeepActive)
enum VanGuiInputTextFlags_
{
    // Basic filters (also see VanGuiInputTextFlags_CallbackCharFilter)
    VanGuiInputTextFlags_None                = 0,
    VanGuiInputTextFlags_CharsDecimal        = 1 << 0,   // Allow 0123456789.+-*/
    VanGuiInputTextFlags_CharsHexadecimal    = 1 << 1,   // Allow 0123456789ABCDEFabcdef
    VanGuiInputTextFlags_CharsScientific     = 1 << 2,   // Allow 0123456789.+-*/eE (Scientific notation input)
    VanGuiInputTextFlags_CharsUppercase      = 1 << 3,   // Turn a..z into A..Z
    VanGuiInputTextFlags_CharsNoBlank        = 1 << 4,   // Filter out spaces, tabs

    // Inputs
    VanGuiInputTextFlags_AllowTabInput       = 1 << 5,   // Pressing TAB input a '\t' character into the text field
    VanGuiInputTextFlags_EnterReturnsTrue    = 1 << 6,   // Return 'true' when Enter is pressed (as opposed to every time the value was modified). Consider using IsItemDeactivatedAfterEdit() instead!
    VanGuiInputTextFlags_EscapeClearsAll     = 1 << 7,   // Escape key clears content if not empty, and deactivate otherwise (contrast to default behavior of Escape to revert)
    VanGuiInputTextFlags_CtrlEnterForNewLine = 1 << 8,   // In multi-line mode: validate with Enter, add new line with Ctrl+Enter (default is opposite: validate with Ctrl+Enter, add line with Enter). Note that Shift+Enter always enter a new line either way.

    // Other options
    VanGuiInputTextFlags_ReadOnly            = 1 << 9,   // Read-only mode
    VanGuiInputTextFlags_Password            = 1 << 10,  // Password mode, display all characters as '*', disable copy
    VanGuiInputTextFlags_AlwaysOverwrite     = 1 << 11,  // Overwrite mode
    VanGuiInputTextFlags_AutoSelectAll       = 1 << 12,  // Select entire text when first taking mouse focus
    VanGuiInputTextFlags_ParseEmptyRefVal    = 1 << 13,  // InputFloat(), InputInt(), InputScalar() etc. only: parse empty string as zero value.
    VanGuiInputTextFlags_DisplayEmptyRefVal  = 1 << 14,  // InputFloat(), InputInt(), InputScalar() etc. only: when value is zero, do not display it. Generally used with VanGuiInputTextFlags_ParseEmptyRefVal.
    VanGuiInputTextFlags_NoHorizontalScroll  = 1 << 15,  // Disable following the cursor horizontally
    VanGuiInputTextFlags_NoUndoRedo          = 1 << 16,  // Disable undo/redo. Note that input text owns the text data while active, if you want to provide your own undo/redo stack you need e.g. to call ClearActiveID().

    // Elide display / Alignment
    VanGuiInputTextFlags_ElideLeft           = 1 << 17,  // When text doesn't fit, elide left side to ensure right side stays visible. Useful for path/filenames. Single-line only!

    // Callback features
    VanGuiInputTextFlags_CallbackCompletion  = 1 << 18,  // Callback on pressing TAB (for completion handling)
    VanGuiInputTextFlags_CallbackHistory     = 1 << 19,  // Callback on pressing Up/Down arrows (for history handling)
    VanGuiInputTextFlags_CallbackAlways      = 1 << 20,  // Callback on each iteration. User code may query cursor position, modify text buffer.
    VanGuiInputTextFlags_CallbackCharFilter  = 1 << 21,  // Callback on character inputs to replace or discard them. Modify 'EventChar' to replace or discard, or return 1 in callback to discard.
    VanGuiInputTextFlags_CallbackResize      = 1 << 22,  // Callback on buffer capacity changes request (beyond 'buf_size' parameter value), allowing the string to grow. Notify when the string wants to be resized (for string types which hold a cache of their Size). You will be provided a new BufSize in the callback and NEED to honor it. (see misc/cpp/vangui_stdlib.h for an example of using this)
    VanGuiInputTextFlags_CallbackEdit        = 1 << 23,  // Callback on any edit. Note that InputText() already returns true on edit + you can always use IsItemEdited(). The callback is useful to manipulate the underlying buffer while focus is active.

    // Multi-line Word-Wrapping [BETA]
    // - Not well tested yet.
    // - Wrapping style is not ideal. Wrapping of long words/sections (e.g. words larger than total available width) may be particularly unpleasing.
    // - Wrapping width needs to always account for the possibility of a vertical scrollbar.
    // - It is much slower than regular text fields.
    //   Ballpark estimate of cost on my 2019 desktop PC: for a 100 KB text buffer: +~0.3 ms (Optimized) / +~1.0 ms (Debug build).
    //   The CPU cost is very roughly proportional to text length, so a 10 KB buffer should cost about ten times less.
    VanGuiInputTextFlags_WordWrap            = 1 << 24,  // InputTextMultiline(): word-wrap lines that are too long.

    // Obsolete names
    //VanGuiInputTextFlags_AlwaysInsertMode  = VanGuiInputTextFlags_AlwaysOverwrite   // [renamed in 1.82] name was not matching behavior
};

// Flags for VanGui::TreeNodeEx(), VanGui::CollapsingHeader*()
enum VanGuiTreeNodeFlags_
{
    VanGuiTreeNodeFlags_None                 = 0,
    VanGuiTreeNodeFlags_Selected             = 1 << 0,   // Draw as selected
    VanGuiTreeNodeFlags_Framed               = 1 << 1,   // Draw frame with background (e.g. for CollapsingHeader)
    VanGuiTreeNodeFlags_AllowOverlap         = 1 << 2,   // Hit testing will allow subsequent widgets to overlap this one. Require previous frame HoveredId to match before being usable. Shortcut to calling SetNextItemAllowOverlap().
    VanGuiTreeNodeFlags_NoTreePushOnOpen     = 1 << 3,   // Don't do a TreePush() when open (e.g. for CollapsingHeader) = no extra indent nor pushing on ID stack
    VanGuiTreeNodeFlags_NoAutoOpenOnLog      = 1 << 4,   // Don't automatically and temporarily open node when Logging is active (by default logging will automatically open tree nodes)
    VanGuiTreeNodeFlags_DefaultOpen          = 1 << 5,   // Default node to be open
    VanGuiTreeNodeFlags_OpenOnDoubleClick    = 1 << 6,   // Open on double-click instead of simple click (default for multi-select unless any _OpenOnXXX behavior is set explicitly). Both behaviors may be combined.
    VanGuiTreeNodeFlags_OpenOnArrow          = 1 << 7,   // Open when clicking on the arrow part (default for multi-select unless any _OpenOnXXX behavior is set explicitly). Both behaviors may be combined.
    VanGuiTreeNodeFlags_Leaf                 = 1 << 8,   // No collapsing, no arrow (use as a convenience for leaf nodes). Note: will always open a tree/id scope and return true. If you never use that scope, add VanGuiTreeNodeFlags_NoTreePushOnOpen.
    VanGuiTreeNodeFlags_Bullet               = 1 << 9,   // Display a bullet instead of arrow. IMPORTANT: node can still be marked open/close if you don't set the _Leaf flag!
    VanGuiTreeNodeFlags_FramePadding         = 1 << 10,  // Use FramePadding (even for an unframed text node) to vertically align text baseline to regular widget height. Equivalent to calling AlignTextToFramePadding() before the node.
    VanGuiTreeNodeFlags_SpanAvailWidth       = 1 << 11,  // Extend hit box to the right-most edge, even if not framed. This is not the default in order to allow adding other items on the same line without using AllowOverlap mode.
    VanGuiTreeNodeFlags_SpanFullWidth        = 1 << 12,  // Extend hit box to the left-most and right-most edges (cover the indent area).
    VanGuiTreeNodeFlags_SpanLabelWidth       = 1 << 13,  // Narrow hit box + narrow hovering highlight, will only cover the label text.
    VanGuiTreeNodeFlags_SpanAllColumns       = 1 << 14,  // Frame will span all columns of its container table (label will still fit in current column)
    VanGuiTreeNodeFlags_LabelSpanAllColumns  = 1 << 15,  // Label will span all columns of its container table
    //VanGuiTreeNodeFlags_NoScrollOnOpen     = 1 << 16,  // FIXME: TODO: Disable automatic scroll on TreePop() if node got just open and contents is not visible
    VanGuiTreeNodeFlags_NavLeftJumpsToParent = 1 << 17,  // Nav: left arrow moves back to parent. This is processed in TreePop() when there's an unfulfilled Left nav request remaining.
    VanGuiTreeNodeFlags_CollapsingHeader     = VanGuiTreeNodeFlags_Framed | VanGuiTreeNodeFlags_NoTreePushOnOpen | VanGuiTreeNodeFlags_NoAutoOpenOnLog,

    // [EXPERIMENTAL] Draw lines connecting TreeNode hierarchy. Discuss in GitHub issue #2920.
    // Default value is pulled from style.TreeLinesFlags. May be overridden in TreeNode calls.
    VanGuiTreeNodeFlags_DrawLinesNone        = 1 << 18,  // No lines drawn
    VanGuiTreeNodeFlags_DrawLinesFull        = 1 << 19,  // Horizontal lines to child nodes. Vertical line drawn down to TreePop() position: cover full contents. Faster (for large trees).
    VanGuiTreeNodeFlags_DrawLinesToNodes     = 1 << 20,  // Horizontal lines to child nodes. Vertical line drawn down to bottom-most child node. Slower (for large trees).

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiTreeNodeFlags_NavLeftJumpsBackHere = VanGuiTreeNodeFlags_NavLeftJumpsToParent,  // Renamed in 1.92.0
    VanGuiTreeNodeFlags_SpanTextWidth        = VanGuiTreeNodeFlags_SpanLabelWidth,        // Renamed in 1.90.7
    //VanGuiTreeNodeFlags_AllowItemOverlap   = VanGuiTreeNodeFlags_AllowOverlap,          // Renamed in 1.89.7
#endif
};

// Flags for OpenPopup*(), BeginPopupContext*(), IsPopupOpen() functions.
// - IMPORTANT: If you ever used the left mouse button with BeginPopupContextXXX() helpers before 1.92.6: Read "API BREAKING CHANGES" 2026/01/07 (1.92.6) entry in vangui.cpp or GitHub topic #9157.
// - Multiple buttons currently cannot be combined/or-ed in those functions (we could allow it later).
enum VanGuiPopupFlags_
{
    VanGuiPopupFlags_None                    = 0,
    VanGuiPopupFlags_MouseButtonLeft         = 1 << 2,   // For BeginPopupContext*(): open on Left Mouse release. Only one button allowed!
    VanGuiPopupFlags_MouseButtonRight        = 2 << 2,   // For BeginPopupContext*(): open on Right Mouse release. Only one button allowed! (default)
    VanGuiPopupFlags_MouseButtonMiddle       = 3 << 2,   // For BeginPopupContext*(): open on Middle Mouse release. Only one button allowed!
    VanGuiPopupFlags_NoReopen                = 1 << 5,   // For OpenPopup*(), BeginPopupContext*(): don't reopen same popup if already open (won't reposition, won't reinitialize navigation)
    //VanGuiPopupFlags_NoReopenAlwaysNavInit = 1 << 6,   // For OpenPopup*(), BeginPopupContext*(): focus and initialize navigation even when not reopening.
    VanGuiPopupFlags_NoOpenOverExistingPopup = 1 << 7,   // For OpenPopup*(), BeginPopupContext*(): don't open if there's already a popup at the same level of the popup stack
    VanGuiPopupFlags_NoOpenOverItems         = 1 << 8,   // For BeginPopupContextWindow(): don't return true when hovering items, only when hovering empty space
    VanGuiPopupFlags_AnyPopupId              = 1 << 10,  // For IsPopupOpen(): ignore the VanGuiID parameter and test for any popup.
    VanGuiPopupFlags_AnyPopupLevel           = 1 << 11,  // For IsPopupOpen(): search/test at any level of the popup stack (default test in the current level)
    VanGuiPopupFlags_AnyPopup                = VanGuiPopupFlags_AnyPopupId | VanGuiPopupFlags_AnyPopupLevel,
    VanGuiPopupFlags_MouseButtonShift_       = 2,        // [Internal]
    VanGuiPopupFlags_MouseButtonMask_        = 0x0C,     // [Internal]
    VanGuiPopupFlags_InvalidMask_            = 0x03,     // [Internal] Reserve legacy bits 0-1 to detect incorrectly passing 1 or 2 to the function.
};

// Flags for VanGui::Selectable()
enum VanGuiSelectableFlags_
{
    VanGuiSelectableFlags_None               = 0,
    VanGuiSelectableFlags_NoAutoClosePopups  = 1 << 0,   // Clicking this doesn't close parent popup window (overrides VanGuiItemFlags_AutoClosePopups)
    VanGuiSelectableFlags_SpanAllColumns     = 1 << 1,   // Frame will span all columns of its container table (text will still fit in current column)
    VanGuiSelectableFlags_AllowDoubleClick   = 1 << 2,   // Generate press events on double clicks too
    VanGuiSelectableFlags_Disabled           = 1 << 3,   // Cannot be selected, display grayed out text
    VanGuiSelectableFlags_AllowOverlap       = 1 << 4,   // Hit testing will allow subsequent widgets to overlap this one. Require previous frame HoveredId to match before being usable. Shortcut to calling SetNextItemAllowOverlap().
    VanGuiSelectableFlags_Highlight          = 1 << 5,   // Make the item be displayed as if it is hovered
    VanGuiSelectableFlags_SelectOnNav        = 1 << 6,   // Auto-select when moved into, unless Ctrl is held. Automatic when in a BeginMultiSelect() block.

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiSelectableFlags_DontClosePopups    = VanGuiSelectableFlags_NoAutoClosePopups,   // Renamed in 1.91.0
    //VanGuiSelectableFlags_AllowItemOverlap = VanGuiSelectableFlags_AllowOverlap,        // Renamed in 1.89.7
#endif
};

// Flags for VanGui::BeginCombo()
enum VanGuiComboFlags_
{
    VanGuiComboFlags_None                    = 0,
    VanGuiComboFlags_PopupAlignLeft          = 1 << 0,   // Align the popup toward the left by default
    VanGuiComboFlags_HeightSmall             = 1 << 1,   // Max ~4 items visible. Tip: If you want your combo popup to be a specific size you can use SetNextWindowSizeConstraints() prior to calling BeginCombo()
    VanGuiComboFlags_HeightRegular           = 1 << 2,   // Max ~8 items visible (default)
    VanGuiComboFlags_HeightLarge             = 1 << 3,   // Max ~20 items visible
    VanGuiComboFlags_HeightLargest           = 1 << 4,   // As many fitting items as possible
    VanGuiComboFlags_NoArrowButton           = 1 << 5,   // Display on the preview box without the square arrow button
    VanGuiComboFlags_NoPreview               = 1 << 6,   // Display only a square arrow button
    VanGuiComboFlags_WidthFitPreview         = 1 << 7,   // Width dynamically calculated from preview contents
    VanGuiComboFlags_HeightMask_             = VanGuiComboFlags_HeightSmall | VanGuiComboFlags_HeightRegular | VanGuiComboFlags_HeightLarge | VanGuiComboFlags_HeightLargest,
};

// Flags for VanGui::BeginTabBar()
enum VanGuiTabBarFlags_
{
    VanGuiTabBarFlags_None                           = 0,
    VanGuiTabBarFlags_Reorderable                    = 1 << 0,   // Allow manually dragging tabs to re-order them + New tabs are appended at the end of list
    VanGuiTabBarFlags_AutoSelectNewTabs              = 1 << 1,   // Automatically select new tabs when they appear
    VanGuiTabBarFlags_TabListPopupButton             = 1 << 2,   // Disable buttons to open the tab list popup
    VanGuiTabBarFlags_NoCloseWithMiddleMouseButton   = 1 << 3,   // Disable behavior of closing tabs (that are submitted with p_open != nullptr) with middle mouse button. You may handle this behavior manually on user's side with if (IsItemHovered() && IsMouseClicked(2)) *p_open = false.
    VanGuiTabBarFlags_NoTabListScrollingButtons      = 1 << 4,   // Disable scrolling buttons (apply when fitting policy is VanGuiTabBarFlags_FittingPolicyScroll)
    VanGuiTabBarFlags_NoTooltip                      = 1 << 5,   // Disable tooltips when hovering a tab
    VanGuiTabBarFlags_DrawSelectedOverline           = 1 << 6,   // Draw selected overline markers over selected tab

    // Fitting/Resize policy
    VanGuiTabBarFlags_FittingPolicyMixed             = 1 << 7,   // Shrink down tabs when they don't fit, until width is style.TabMinWidthShrink, then enable scrolling. Setting TabMinWidthShrink to FLT_MAX makes this behave like VanGuiTabBarFlags_FittingPolicyScroll.
    VanGuiTabBarFlags_FittingPolicyShrink            = 1 << 8,   // Shrink down tabs when they don't fit
    VanGuiTabBarFlags_FittingPolicyScroll            = 1 << 9,   // Enable scrolling buttons when tabs don't fit
    VanGuiTabBarFlags_FittingPolicyMask_             = VanGuiTabBarFlags_FittingPolicyMixed | VanGuiTabBarFlags_FittingPolicyShrink | VanGuiTabBarFlags_FittingPolicyScroll,
    VanGuiTabBarFlags_FittingPolicyDefault_          = VanGuiTabBarFlags_FittingPolicyMixed,

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiTabBarFlags_FittingPolicyResizeDown        = VanGuiTabBarFlags_FittingPolicyShrink, // Renamed in 1.92.2
#endif
};

// Flags for VanGui::BeginTabItem()
enum VanGuiTabItemFlags_
{
    VanGuiTabItemFlags_None                          = 0,
    VanGuiTabItemFlags_UnsavedDocument               = 1 << 0,   // Display a dot next to the title + set VanGuiTabItemFlags_NoAssumedClosure.
    VanGuiTabItemFlags_SetSelected                   = 1 << 1,   // Trigger flag to programmatically make the tab selected when calling BeginTabItem()
    VanGuiTabItemFlags_NoCloseWithMiddleMouseButton  = 1 << 2,   // Disable behavior of closing tabs (that are submitted with p_open != nullptr) with middle mouse button. You may handle this behavior manually on user's side with if (IsItemHovered() && IsMouseClicked(2)) *p_open = false.
    VanGuiTabItemFlags_NoPushId                      = 1 << 3,   // Don't call PushID()/PopID() on BeginTabItem()/EndTabItem()
    VanGuiTabItemFlags_NoTooltip                     = 1 << 4,   // Disable tooltip for the given tab
    VanGuiTabItemFlags_NoReorder                     = 1 << 5,   // Disable reordering this tab or having another tab cross over this tab
    VanGuiTabItemFlags_Leading                       = 1 << 6,   // Enforce the tab position to the left of the tab bar (after the tab list popup button)
    VanGuiTabItemFlags_Trailing                      = 1 << 7,   // Enforce the tab position to the right of the tab bar (before the scrolling buttons)
    VanGuiTabItemFlags_NoAssumedClosure              = 1 << 8,   // Tab is selected when trying to close + closure is not immediately assumed (will wait for user to stop submitting the tab). Otherwise closure is assumed when pressing the X, so if you keep submitting the tab may reappear at end of tab bar.
};

// Flags for VanGui::DockSpace(), VanGui::DockSpaceOverViewport()
// [BETA] Enable with io.ConfigFlags |= VanGuiConfigFlags_DockingEnable.
enum VanGuiDockNodeFlags_
{
    VanGuiDockNodeFlags_None                         = 0,
    VanGuiDockNodeFlags_KeepAliveOnly                = 1 << 0,   // Don't display the dockspace node but keep it alive. Windows docked into this dockspace node won't be undocked.
    VanGuiDockNodeFlags_NoDockingOverCentralNode     = 1 << 2,   // Disable docking over the Central Node, which will be always kept empty.
    VanGuiDockNodeFlags_PassthruCentralNode          = 1 << 3,   // Enable passthrough on the central node. (FIXME: Loss of DockFlags_ from VanGuiDockNodeFlags_ when merged)
    VanGuiDockNodeFlags_NoDockingSplit               = 1 << 4,   // Disable other windows/nodes from splitting this node.
    VanGuiDockNodeFlags_NoResize                     = 1 << 5,   // Saved // Disable resizing node using the splitter/separator. Useful with programmatic docking.
    VanGuiDockNodeFlags_AutoHideTabBar               = 1 << 6,   // Tab bar will automatically hide when there is a single window in the dock node.
    VanGuiDockNodeFlags_NoUndocking                  = 1 << 7,   // Disable undocking of windows from this dock node.
};

// Flags for VanGui::IsWindowFocused()
enum VanGuiFocusedFlags_
{
    VanGuiFocusedFlags_None                          = 0,
    VanGuiFocusedFlags_ChildWindows                  = 1 << 0,   // Return true if any children of the window is focused
    VanGuiFocusedFlags_RootWindow                    = 1 << 1,   // Test from root window (top most parent of the current hierarchy)
    VanGuiFocusedFlags_AnyWindow                     = 1 << 2,   // Return true if any window is focused. Important: If you are trying to tell how to dispatch your low-level inputs, do NOT use this. Use 'io.WantCaptureMouse' instead! Please read the FAQ!
    VanGuiFocusedFlags_NoPopupHierarchy              = 1 << 3,   // Do not consider popup hierarchy (do not treat popup emitter as parent of popup) (when used with _ChildWindows or _RootWindow)
    //VanGuiFocusedFlags_DockHierarchy               = 1 << 4,   // Consider docking hierarchy (treat dockspace host as parent of docked window) (when used with _ChildWindows or _RootWindow)
    VanGuiFocusedFlags_RootAndChildWindows           = VanGuiFocusedFlags_RootWindow | VanGuiFocusedFlags_ChildWindows,
};

// Flags for VanGui::IsItemHovered(), VanGui::IsWindowHovered()
// Note: if you are trying to check whether your mouse should be dispatched to VanGUI or to your app, you should use 'io.WantCaptureMouse' instead! Please read the FAQ!
// Note: windows with the VanGuiWindowFlags_NoInputs flag are ignored by IsWindowHovered() calls.
enum VanGuiHoveredFlags_
{
    VanGuiHoveredFlags_None                          = 0,        // Return true if directly over the item/window, not obstructed by another window, not obstructed by an active popup or modal blocking inputs under them.
    VanGuiHoveredFlags_ChildWindows                  = 1 << 0,   // IsWindowHovered() only: Return true if any children of the window is hovered
    VanGuiHoveredFlags_RootWindow                    = 1 << 1,   // IsWindowHovered() only: Test from root window (top most parent of the current hierarchy)
    VanGuiHoveredFlags_AnyWindow                     = 1 << 2,   // IsWindowHovered() only: Return true if any window is hovered
    VanGuiHoveredFlags_NoPopupHierarchy              = 1 << 3,   // IsWindowHovered() only: Do not consider popup hierarchy (do not treat popup emitter as parent of popup) (when used with _ChildWindows or _RootWindow)
    //VanGuiHoveredFlags_DockHierarchy               = 1 << 4,   // IsWindowHovered() only: Consider docking hierarchy (treat dockspace host as parent of docked window) (when used with _ChildWindows or _RootWindow)
    VanGuiHoveredFlags_AllowWhenBlockedByPopup       = 1 << 5,   // Return true even if a popup window is normally blocking access to this item/window
    //VanGuiHoveredFlags_AllowWhenBlockedByModal     = 1 << 6,   // Return true even if a modal popup window is normally blocking access to this item/window. FIXME-TODO: Unavailable yet.
    VanGuiHoveredFlags_AllowWhenBlockedByActiveItem  = 1 << 7,   // Return true even if an active item is blocking access to this item/window. Useful for Drag and Drop patterns.
    VanGuiHoveredFlags_AllowWhenOverlappedByItem     = 1 << 8,   // IsItemHovered() only: Return true even if the item uses AllowOverlap mode and is overlapped by another hoverable item.
    VanGuiHoveredFlags_AllowWhenOverlappedByWindow   = 1 << 9,   // IsItemHovered() only: Return true even if the position is obstructed or overlapped by another window.
    VanGuiHoveredFlags_AllowWhenDisabled             = 1 << 10,  // IsItemHovered() only: Return true even if the item is disabled
    VanGuiHoveredFlags_NoNavOverride                 = 1 << 11,  // IsItemHovered() only: Disable using keyboard/gamepad navigation state when active, always query mouse
    VanGuiHoveredFlags_AllowWhenOverlapped           = VanGuiHoveredFlags_AllowWhenOverlappedByItem | VanGuiHoveredFlags_AllowWhenOverlappedByWindow,
    VanGuiHoveredFlags_RectOnly                      = VanGuiHoveredFlags_AllowWhenBlockedByPopup | VanGuiHoveredFlags_AllowWhenBlockedByActiveItem | VanGuiHoveredFlags_AllowWhenOverlapped,
    VanGuiHoveredFlags_RootAndChildWindows           = VanGuiHoveredFlags_RootWindow | VanGuiHoveredFlags_ChildWindows,

    // Tooltips mode
    // - typically used in IsItemHovered() + SetTooltip() sequence.
    // - this is a shortcut to pull flags from 'style.HoverFlagsForTooltipMouse' or 'style.HoverFlagsForTooltipNav' where you can reconfigure desired behavior.
    //   e.g. 'HoverFlagsForTooltipMouse' defaults to 'VanGuiHoveredFlags_Stationary | VanGuiHoveredFlags_DelayShort | VanGuiHoveredFlags_AllowWhenDisabled'.
    // - for frequently actioned or hovered items providing a tooltip, you want may to use VanGuiHoveredFlags_ForTooltip (stationary + delay) so the tooltip doesn't show too often.
    // - for items which main purpose is to be hovered, or items with low affordance, or in less consistent apps, prefer no delay or shorter delay.
    VanGuiHoveredFlags_ForTooltip                    = 1 << 12,  // Shortcut for standard flags when using IsItemHovered() + SetTooltip() sequence.

    // (Advanced) Mouse Hovering delays.
    // - generally you can use VanGuiHoveredFlags_ForTooltip to use application-standardized flags.
    // - use those if you need specific overrides.
    VanGuiHoveredFlags_Stationary                    = 1 << 13,  // Require mouse to be stationary for style.HoverStationaryDelay (~0.15 sec) _at least one time_. After this, can move on same item/window. Using the stationary test tends to reduces the need for a long delay.
    VanGuiHoveredFlags_DelayNone                     = 1 << 14,  // IsItemHovered() only: Return true immediately (default). As this is the default you generally ignore this.
    VanGuiHoveredFlags_DelayShort                    = 1 << 15,  // IsItemHovered() only: Return true after style.HoverDelayShort elapsed (~0.15 sec) (shared between items) + requires mouse to be stationary for style.HoverStationaryDelay (once per item).
    VanGuiHoveredFlags_DelayNormal                   = 1 << 16,  // IsItemHovered() only: Return true after style.HoverDelayNormal elapsed (~0.40 sec) (shared between items) + requires mouse to be stationary for style.HoverStationaryDelay (once per item).
    VanGuiHoveredFlags_NoSharedDelay                 = 1 << 17,  // IsItemHovered() only: Disable shared delay system where moving from one item to the next keeps the previous timer for a short time (standard for tooltips with long delays)
};

// Flags for VanGui::BeginDragDropSource(), VanGui::AcceptDragDropPayload()
enum VanGuiDragDropFlags_
{
    VanGuiDragDropFlags_None                         = 0,
    // BeginDragDropSource() flags
    VanGuiDragDropFlags_SourceNoPreviewTooltip       = 1 << 0,   // Disable preview tooltip. By default, a successful call to BeginDragDropSource opens a tooltip so you can display a preview or description of the source contents. This flag disables this behavior.
    VanGuiDragDropFlags_SourceNoDisableHover         = 1 << 1,   // By default, when dragging we clear data so that IsItemHovered() will return false, to avoid subsequent user code submitting tooltips. This flag disables this behavior so you can still call IsItemHovered() on the source item.
    VanGuiDragDropFlags_SourceNoHoldToOpenOthers     = 1 << 2,   // Disable the behavior that allows to open tree nodes and collapsing header by holding over them while dragging a source item.
    VanGuiDragDropFlags_SourceAllowNullID            = 1 << 3,   // Allow items such as Text(), Image() that have no unique identifier to be used as drag source, by manufacturing a temporary identifier based on their window-relative position. This is extremely unusual within the VanGUI ecosystem and so we made it explicit.
    VanGuiDragDropFlags_SourceExtern                 = 1 << 4,   // External source (from outside of VanGUI), won't attempt to read current item/window info. Will always return true. Only one Extern source can be active simultaneously.
    VanGuiDragDropFlags_PayloadAutoExpire            = 1 << 5,   // Automatically expire the payload if the source cease to be submitted (otherwise payloads are persisting while being dragged)
    VanGuiDragDropFlags_PayloadNoCrossContext        = 1 << 6,   // Hint to specify that the payload may not be copied outside current VanGUI context.
    VanGuiDragDropFlags_PayloadNoCrossProcess        = 1 << 7,   // Hint to specify that the payload may not be copied outside current process.
    // AcceptDragDropPayload() flags
    VanGuiDragDropFlags_AcceptBeforeDelivery         = 1 << 10,  // AcceptDragDropPayload() will returns true even before the mouse button is released. You can then call IsDelivery() to test if the payload needs to be delivered.
    VanGuiDragDropFlags_AcceptNoDrawDefaultRect      = 1 << 11,  // Do not draw the default highlight rectangle when hovering over target.
    VanGuiDragDropFlags_AcceptNoPreviewTooltip       = 1 << 12,  // Request hiding the BeginDragDropSource tooltip from the BeginDragDropTarget site.
    VanGuiDragDropFlags_AcceptDrawAsHovered          = 1 << 13,  // Accepting item will render as if hovered. Useful for e.g. a Button() used as a drop target.
    VanGuiDragDropFlags_AcceptPeekOnly               = VanGuiDragDropFlags_AcceptBeforeDelivery | VanGuiDragDropFlags_AcceptNoDrawDefaultRect, // For peeking ahead and inspecting the payload before delivery.

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiDragDropFlags_SourceAutoExpirePayload = VanGuiDragDropFlags_PayloadAutoExpire, // Renamed in 1.90.9
#endif
};

// Standard Drag and Drop payload types. You can define you own payload types using short strings. Types starting with '_' are defined by VanGUI.
#define VANGUI_PAYLOAD_TYPE_COLOR_3F     "_COL3F"    // float[3]: Standard type for colors, without alpha. User code may use this type.
#define VANGUI_PAYLOAD_TYPE_COLOR_4F     "_COL4F"    // float[4]: Standard type for colors. User code may use this type.

// A primary data type
enum VanGuiDataType_
{
    VanGuiDataType_S8,       // signed char / char (with sensible compilers)
    VanGuiDataType_U8,       // unsigned char
    VanGuiDataType_S16,      // short
    VanGuiDataType_U16,      // unsigned short
    VanGuiDataType_S32,      // int
    VanGuiDataType_U32,      // unsigned int
    VanGuiDataType_S64,      // long long / __int64
    VanGuiDataType_U64,      // unsigned long long / unsigned __int64
    VanGuiDataType_Float,    // float
    VanGuiDataType_Double,   // double
    VanGuiDataType_Bool,     // bool (provided for user convenience, not supported by scalar widgets)
    VanGuiDataType_String,   // char* (provided for user convenience, not supported by scalar widgets)
    VanGuiDataType_COUNT
};

// A cardinal direction
enum VanGuiDir : int
{
    VanGuiDir_None    = -1,
    VanGuiDir_Left    = 0,
    VanGuiDir_Right   = 1,
    VanGuiDir_Up      = 2,
    VanGuiDir_Down    = 3,
    VanGuiDir_COUNT
};

// A sorting direction
enum VanGuiSortDirection : VanU8
{
    VanGuiSortDirection_None         = 0,
    VanGuiSortDirection_Ascending    = 1,    // Ascending = 0->9, A->Z etc.
    VanGuiSortDirection_Descending   = 2     // Descending = 9->0, Z->A etc.
};

// A key identifier (VanGuiKey_XXX or VanGuiMod_XXX value): can represent Keyboard, Mouse and Gamepad values.
// All our named keys are >= 512. Keys value 0 to 511 are left unused and were legacy native/opaque key values (< 1.87).
// Support for legacy keys was completely removed in 1.91.5.
// Note that "Keys" related to physical keys and are not the same concept as input "Characters", the latter are submitted via io.AddInputCharacter().
// The keyboard key enum values are named after the keys on a standard US keyboard, and on other keyboard types the keys reported may not match the keycaps.
enum VanGuiKey : int
{
    // Keyboard
    VanGuiKey_None = 0,
    VanGuiKey_NamedKey_BEGIN = 512,  // First valid key value (other than 0)

    VanGuiKey_Tab = 512,             // == VanGuiKey_NamedKey_BEGIN
    VanGuiKey_LeftArrow,
    VanGuiKey_RightArrow,
    VanGuiKey_UpArrow,
    VanGuiKey_DownArrow,
    VanGuiKey_PageUp,
    VanGuiKey_PageDown,
    VanGuiKey_Home,
    VanGuiKey_End,
    VanGuiKey_Insert,
    VanGuiKey_Delete,
    VanGuiKey_Backspace,
    VanGuiKey_Space,
    VanGuiKey_Enter,
    VanGuiKey_Escape,
    VanGuiKey_LeftCtrl, VanGuiKey_LeftShift, VanGuiKey_LeftAlt, VanGuiKey_LeftSuper,     // Also see VanGuiMod_Ctrl, VanGuiMod_Shift, VanGuiMod_Alt, VanGuiMod_Super below!
    VanGuiKey_RightCtrl, VanGuiKey_RightShift, VanGuiKey_RightAlt, VanGuiKey_RightSuper,
    VanGuiKey_Menu,
    VanGuiKey_0, VanGuiKey_1, VanGuiKey_2, VanGuiKey_3, VanGuiKey_4, VanGuiKey_5, VanGuiKey_6, VanGuiKey_7, VanGuiKey_8, VanGuiKey_9,
    VanGuiKey_A, VanGuiKey_B, VanGuiKey_C, VanGuiKey_D, VanGuiKey_E, VanGuiKey_F, VanGuiKey_G, VanGuiKey_H, VanGuiKey_I, VanGuiKey_J,
    VanGuiKey_K, VanGuiKey_L, VanGuiKey_M, VanGuiKey_N, VanGuiKey_O, VanGuiKey_P, VanGuiKey_Q, VanGuiKey_R, VanGuiKey_S, VanGuiKey_T,
    VanGuiKey_U, VanGuiKey_V, VanGuiKey_W, VanGuiKey_X, VanGuiKey_Y, VanGuiKey_Z,
    VanGuiKey_F1, VanGuiKey_F2, VanGuiKey_F3, VanGuiKey_F4, VanGuiKey_F5, VanGuiKey_F6,
    VanGuiKey_F7, VanGuiKey_F8, VanGuiKey_F9, VanGuiKey_F10, VanGuiKey_F11, VanGuiKey_F12,
    VanGuiKey_F13, VanGuiKey_F14, VanGuiKey_F15, VanGuiKey_F16, VanGuiKey_F17, VanGuiKey_F18,
    VanGuiKey_F19, VanGuiKey_F20, VanGuiKey_F21, VanGuiKey_F22, VanGuiKey_F23, VanGuiKey_F24,
    VanGuiKey_Apostrophe,        // '
    VanGuiKey_Comma,             // ,
    VanGuiKey_Minus,             // -
    VanGuiKey_Period,            // .
    VanGuiKey_Slash,             // /
    VanGuiKey_Semicolon,         // ;
    VanGuiKey_Equal,             // =
    VanGuiKey_LeftBracket,       // [
    VanGuiKey_Backslash,         // \ (this text inhibit multiline comment caused by backslash)
    VanGuiKey_RightBracket,      // ]
    VanGuiKey_GraveAccent,       // `
    VanGuiKey_CapsLock,
    VanGuiKey_ScrollLock,
    VanGuiKey_NumLock,
    VanGuiKey_PrintScreen,
    VanGuiKey_Pause,
    VanGuiKey_Keypad0, VanGuiKey_Keypad1, VanGuiKey_Keypad2, VanGuiKey_Keypad3, VanGuiKey_Keypad4,
    VanGuiKey_Keypad5, VanGuiKey_Keypad6, VanGuiKey_Keypad7, VanGuiKey_Keypad8, VanGuiKey_Keypad9,
    VanGuiKey_KeypadDecimal,
    VanGuiKey_KeypadDivide,
    VanGuiKey_KeypadMultiply,
    VanGuiKey_KeypadSubtract,
    VanGuiKey_KeypadAdd,
    VanGuiKey_KeypadEnter,
    VanGuiKey_KeypadEqual,
    VanGuiKey_AppBack,               // Available on some keyboard/mouses. Often referred as "Browser Back"
    VanGuiKey_AppForward,
    VanGuiKey_Oem102,                // Non-US backslash.

    // Gamepad
    // (analog values are 0.0f to 1.0f)
    //                              // XBOX        | SWITCH  | PLAYSTA. | -> ACTION
    VanGuiKey_GamepadStart,          // Menu        | +       | Options  |
    VanGuiKey_GamepadBack,           // View        | -       | Share    |
    VanGuiKey_GamepadFaceLeft,       // X           | Y       | Square   | Toggle Menu. Hold for Windowing mode (Focus/Move/Resize windows)
    VanGuiKey_GamepadFaceRight,      // B           | A       | Circle   | Cancel / Close / Exit
    VanGuiKey_GamepadFaceUp,         // Y           | X       | Triangle | Open Context Menu
    VanGuiKey_GamepadFaceDown,       // A           | B       | Cross    | Activate / Open / Toggle. Hold for 0.60f to Activate in Text Input mode (e.g. wired to an on-screen keyboard).
    VanGuiKey_GamepadDpadLeft,       // D-pad Left  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    VanGuiKey_GamepadDpadRight,      // D-pad Right | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    VanGuiKey_GamepadDpadUp,         // D-pad Up    | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    VanGuiKey_GamepadDpadDown,       // D-pad Down  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    VanGuiKey_GamepadL1,             // L Bumper    | L       | L1       | Tweak Slower / Focus Previous (in Windowing mode)
    VanGuiKey_GamepadR1,             // R Bumper    | R       | R1       | Tweak Faster / Focus Next (in Windowing mode)
    VanGuiKey_GamepadL2,             // L Trigger   | ZL      | L2       | [Analog]
    VanGuiKey_GamepadR2,             // R Trigger   | ZR      | R2       | [Analog]
    VanGuiKey_GamepadL3,             // L Stick     | L3      | L3       |
    VanGuiKey_GamepadR3,             // R Stick     | R3      | R3       |
    VanGuiKey_GamepadLStickLeft,     //             |         |          | [Analog] Move Window (in Windowing mode)
    VanGuiKey_GamepadLStickRight,    //             |         |          | [Analog] Move Window (in Windowing mode)
    VanGuiKey_GamepadLStickUp,       //             |         |          | [Analog] Move Window (in Windowing mode)
    VanGuiKey_GamepadLStickDown,     //             |         |          | [Analog] Move Window (in Windowing mode)
    VanGuiKey_GamepadRStickLeft,     //             |         |          | [Analog]
    VanGuiKey_GamepadRStickRight,    //             |         |          | [Analog]
    VanGuiKey_GamepadRStickUp,       //             |         |          | [Analog]
    VanGuiKey_GamepadRStickDown,     //             |         |          | [Analog]

    // Aliases: Mouse Buttons (auto-submitted from AddMouseButtonEvent() calls)
    // - This is mirroring the data also written to io.MouseDown[], io.MouseWheel, in a format allowing them to be accessed via standard key API.
    VanGuiKey_MouseLeft, VanGuiKey_MouseRight, VanGuiKey_MouseMiddle, VanGuiKey_MouseX1, VanGuiKey_MouseX2, VanGuiKey_MouseWheelX, VanGuiKey_MouseWheelY,

    // [Internal] Reserved for mod storage
    VanGuiKey_ReservedForModCtrl, VanGuiKey_ReservedForModShift, VanGuiKey_ReservedForModAlt, VanGuiKey_ReservedForModSuper,

    // [Internal] If you need to iterate all keys (for e.g. an input mapper) you may use VanGuiKey_NamedKey_BEGIN..VanGuiKey_NamedKey_END.
    VanGuiKey_NamedKey_END,
    VanGuiKey_NamedKey_COUNT = VanGuiKey_NamedKey_END - VanGuiKey_NamedKey_BEGIN,

    // Keyboard Modifiers (explicitly submitted by backend via AddKeyEvent() calls)
    // - Any functions taking a VanGuiKeyChord parameter can binary-or those with regular keys, e.g. Shortcut(VanGuiMod_Ctrl | VanGuiKey_S).
    // - Those are written back into io.KeyCtrl, io.KeyShift, io.KeyAlt, io.KeySuper for convenience,
    //   but may be accessed via standard key API such as IsKeyPressed(), IsKeyReleased(), querying duration etc.
    // - Code polling every key (e.g. an interface to detect a key press for input mapping) might want to ignore those
    //   and prefer using the real keys (e.g. VanGuiKey_LeftCtrl, VanGuiKey_RightCtrl instead of VanGuiMod_Ctrl).
    // - In theory the value of keyboard modifiers should be roughly equivalent to a logical or of the equivalent left/right keys.
    //   In practice: it's complicated; mods are often provided from different sources. Keyboard layout, IME, sticky keys and
    //   backends tend to interfere and break that equivalence. The safer decision is to relay that ambiguity down to the end-user...
    // - On macOS, we swap Cmd(Super) and Ctrl keys at the time of the io.AddKeyEvent() call.
    VanGuiMod_None                   = 0,
    VanGuiMod_Ctrl                   = 1 << 12, // Ctrl (non-macOS), Cmd (macOS)
    VanGuiMod_Shift                  = 1 << 13, // Shift
    VanGuiMod_Alt                    = 1 << 14, // Option/Menu
    VanGuiMod_Super                  = 1 << 15, // Windows/Super (non-macOS), Ctrl (macOS)
    VanGuiMod_Mask_                  = 0xF000,  // 4-bits

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiKey_COUNT                  = VanGuiKey_NamedKey_END,    // Obsoleted in 1.91.5 because it was misleading (since named keys don't start at 0 anymore)
    VanGuiMod_Shortcut               = VanGuiMod_Ctrl,            // Removed in 1.90.7, you can now simply use VanGuiMod_Ctrl
    //VanGuiKey_ModCtrl = VanGuiMod_Ctrl, VanGuiKey_ModShift = VanGuiMod_Shift, VanGuiKey_ModAlt = VanGuiMod_Alt, VanGuiKey_ModSuper = VanGuiMod_Super, // Renamed in 1.89
    //VanGuiKey_KeyPadEnter = VanGuiKey_KeypadEnter,              // Renamed in 1.87
#endif
};

// Flags for Shortcut(), SetNextItemShortcut(),
// (and for upcoming extended versions of IsKeyPressed(), IsMouseClicked(), Shortcut(), SetKeyOwner(), SetItemKeyOwner() that are still in vangui_internal.h)
// Don't mistake with VanGuiInputTextFlags! (which is for VanGui::InputText() function)
enum VanGuiInputFlags_
{
    VanGuiInputFlags_None                    = 0,
    VanGuiInputFlags_Repeat                  = 1 << 0,   // Enable repeat. Return true on successive repeats. Default for legacy IsKeyPressed(). NOT Default for legacy IsMouseClicked(). MUST BE == 1.

    // Flags for Shortcut(), SetNextItemShortcut()
    // - Routing policies: RouteGlobal+OverActive >> RouteActive or RouteFocused (if owner is active item) >> RouteGlobal+OverFocused >> RouteFocused (if in focused window stack) >> RouteGlobal.
    // - Default policy is RouteFocused. Can select only 1 policy among all available.
    VanGuiInputFlags_RouteActive             = 1 << 10,  // Route to active item only.
    VanGuiInputFlags_RouteFocused            = 1 << 11,  // Route to windows in the focus stack (DEFAULT). Deep-most focused window takes inputs. Active item takes inputs over deep-most focused window.
    VanGuiInputFlags_RouteGlobal             = 1 << 12,  // Global route (unless a focused window or active item registered the route).
    VanGuiInputFlags_RouteAlways             = 1 << 13,  // Do not register route, poll keys directly.
    // - Routing options
    VanGuiInputFlags_RouteOverFocused        = 1 << 14,  // Option: global route: higher priority than focused route (unless active item in focused route).
    VanGuiInputFlags_RouteOverActive         = 1 << 15,  // Option: global route: higher priority than active item. Unlikely you need to use that: will interfere with every active items, e.g. Ctrl+A registered by InputText will be overridden by this. May not be fully honored as user/internal code is likely to always assume they can access keys when active.
    VanGuiInputFlags_RouteUnlessBgFocused    = 1 << 16,  // Option: global route: will not be applied if underlying background/void is focused (== no VanGUI windows are focused). Useful for overlay applications.
    VanGuiInputFlags_RouteFromRootWindow     = 1 << 17,  // Option: route evaluated from the point of view of root window rather than current window.

    // Flags for SetNextItemShortcut()
    VanGuiInputFlags_Tooltip                 = 1 << 18,  // Automatically display a tooltip when hovering item [BETA] Unsure of right api (opt-in/opt-out)
};

// Configuration flags stored in io.ConfigFlags. Set by user/application.
// Note that nowadays most of our configuration options are in other VanGuiIO fields, e.g. io.ConfigWindowsMoveFromTitleBarOnly.
enum VanGuiConfigFlags_
{
    VanGuiConfigFlags_None                   = 0,
    VanGuiConfigFlags_NavEnableKeyboard      = 1 << 0,   // Master keyboard navigation enable flag. Enable full Tabbing + directional arrows + Space/Enter to activate. Note: some features such as basic Tabbing and CtrL+Tab are enabled by regardless of this flag (and may be disabled via other means, see #4828, #9218).
    VanGuiConfigFlags_NavEnableGamepad       = 1 << 1,   // Master gamepad navigation enable flag. Backend also needs to set VanGuiBackendFlags_HasGamepad.
    VanGuiConfigFlags_NoMouse                = 1 << 4,   // Instruct VanGUI to disable mouse inputs and interactions.
    VanGuiConfigFlags_NoMouseCursorChange    = 1 << 5,   // Instruct backend to not alter mouse cursor shape and visibility. Use if the backend cursor changes are interfering with yours and you don't want to use SetMouseCursor() to change mouse cursor. You may want to honor requests from vangui by reading GetMouseCursor() yourself instead.
    VanGuiConfigFlags_NoKeyboard             = 1 << 6,   // Instruct VanGUI to disable keyboard inputs and interactions. This is done by ignoring keyboard events and clearing existing states.

    // [BETA] Docking and Viewport options
    VanGuiConfigFlags_DockingEnable          = 1 << 6,   // [BETA] Enable docking functionality. Use SHIFT to dock window (or disable io.ConfigDockingWithShift)
    VanGuiConfigFlags_ViewportsEnable        = 1 << 10,  // [BETA] Enable Multi-Viewport / Platform Windows. Not fully supported yet: gamepad support, viewports with no decorations, viewports not in task bar, IME support.

    // [Unused] User storage (to allow your backend/engine to communicate to code that may be shared between multiple projects. Those flags are NOT used by core VanGUI)
    VanGuiConfigFlags_IsSRGB                 = 1 << 20,  // Application is SRGB-aware.
    VanGuiConfigFlags_IsTouchScreen          = 1 << 21,  // Application is using a touch screen instead of a mouse.
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiConfigFlags_NavEnableSetMousePos   = 1 << 2,   // [moved/renamed in 1.91.4] -> use bool io.ConfigNavMoveSetMousePos
    VanGuiConfigFlags_NavNoCaptureKeyboard   = 1 << 3,   // [moved/renamed in 1.91.4] -> use bool io.ConfigNavCaptureKeyboard
#endif
};

// Backend capabilities flags stored in io.BackendFlags. Set by vangui_impl_xxx or custom backend.
enum VanGuiBackendFlags_
{
    VanGuiBackendFlags_None                  = 0,
    VanGuiBackendFlags_HasGamepad            = 1 << 0,   // Backend Platform supports gamepad and currently has one connected.
    VanGuiBackendFlags_HasMouseCursors       = 1 << 1,   // Backend Platform supports honoring GetMouseCursor() value to change the OS cursor shape.
    VanGuiBackendFlags_HasSetMousePos        = 1 << 2,   // Backend Platform supports io.WantSetMousePos requests to reposition the OS mouse position (only used if io.ConfigNavMoveSetMousePos is set).
    VanGuiBackendFlags_RendererHasVtxOffset  = 1 << 3,   // Backend Renderer supports VanDrawCmd::VtxOffset. This enables output of large meshes (64K+ vertices) while still using 16-bit indices.
    VanGuiBackendFlags_RendererHasTextures   = 1 << 4,   // Backend Renderer supports VanTextureData requests to create/update/destroy textures. This enables incremental texture updates and texture reloads.
};

// Enumeration for PushStyleColor() / PopStyleColor()
enum VanGuiCol_
{
    VanGuiCol_Text,
    VanGuiCol_TextDisabled,
    VanGuiCol_WindowBg,              // Background of normal windows
    VanGuiCol_ChildBg,               // Background of child windows
    VanGuiCol_PopupBg,               // Background of popups, menus, tooltips windows
    VanGuiCol_Border,
    VanGuiCol_BorderShadow,
    VanGuiCol_FrameBg,               // Background of checkbox, radio button, plot, slider, text input
    VanGuiCol_FrameBgHovered,
    VanGuiCol_FrameBgActive,
    VanGuiCol_TitleBg,               // Title bar
    VanGuiCol_TitleBgActive,         // Title bar when focused
    VanGuiCol_TitleBgCollapsed,      // Title bar when collapsed
    VanGuiCol_MenuBarBg,
    VanGuiCol_ScrollbarBg,
    VanGuiCol_ScrollbarGrab,
    VanGuiCol_ScrollbarGrabHovered,
    VanGuiCol_ScrollbarGrabActive,
    VanGuiCol_CheckMark,             // Checkbox tick and RadioButton circle
    VanGuiCol_CheckboxSelectedBg,    // Checkbox background when Selected, otherwise use FrameBg
    VanGuiCol_SliderGrab,
    VanGuiCol_SliderGrabActive,
    VanGuiCol_Button,
    VanGuiCol_ButtonHovered,
    VanGuiCol_ButtonActive,
    VanGuiCol_Header,                // Header* colors are used for CollapsingHeader, TreeNode, Selectable, MenuItem
    VanGuiCol_HeaderHovered,
    VanGuiCol_HeaderActive,
    VanGuiCol_Separator,
    VanGuiCol_SeparatorHovered,
    VanGuiCol_SeparatorActive,
    VanGuiCol_ResizeGrip,            // Resize grip in lower-right and lower-left corners of windows.
    VanGuiCol_ResizeGripHovered,
    VanGuiCol_ResizeGripActive,
    VanGuiCol_InputTextCursor,       // InputText cursor/caret
    VanGuiCol_TabHovered,            // Tab background, when hovered
    VanGuiCol_Tab,                   // Tab background, when tab-bar is focused & tab is unselected
    VanGuiCol_TabSelected,           // Tab background, when tab-bar is focused & tab is selected
    VanGuiCol_TabSelectedOverline,   // Tab horizontal overline, when tab-bar is focused & tab is selected
    VanGuiCol_TabDimmed,             // Tab background, when tab-bar is unfocused & tab is unselected
    VanGuiCol_TabDimmedSelected,     // Tab background, when tab-bar is unfocused & tab is selected
    VanGuiCol_TabDimmedSelectedOverline,//..horizontal overline, when tab-bar is unfocused & tab is selected
    VanGuiCol_PlotLines,
    VanGuiCol_PlotLinesHovered,
    VanGuiCol_PlotHistogram,
    VanGuiCol_PlotHistogramHovered,
    VanGuiCol_TableHeaderBg,         // Table header background
    VanGuiCol_TableBorderStrong,     // Table outer and header borders (prefer using Alpha=1.0 here)
    VanGuiCol_TableBorderLight,      // Table inner borders (prefer using Alpha=1.0 here)
    VanGuiCol_TableRowBg,            // Table row background (even rows)
    VanGuiCol_TableRowBgAlt,         // Table row background (odd rows)
    VanGuiCol_TextLink,              // Hyperlink color
    VanGuiCol_TextSelectedBg,        // Selected text inside an InputText
    VanGuiCol_TreeLines,             // Tree node hierarchy outlines when using VanGuiTreeNodeFlags_DrawLines
    VanGuiCol_DragDropTarget,        // Rectangle border highlighting a drop target
    VanGuiCol_DragDropTargetBg,      // Rectangle background highlighting a drop target
    VanGuiCol_UnsavedMarker,         // Unsaved Document marker (in window title and tabs)
    VanGuiCol_NavCursor,             // Color of keyboard/gamepad navigation cursor/rectangle, when visible
    VanGuiCol_NavWindowingHighlight, // Highlight window when using Ctrl+Tab
    VanGuiCol_NavWindowingDimBg,     // Darken/colorize entire screen behind the Ctrl+Tab window list, when active
    VanGuiCol_ModalWindowDimBg,      // Darken/colorize entire screen behind a modal window, when one is active
    VanGuiCol_DockingPreview,        // Preview overlay color when about to docking something
    VanGuiCol_DockingEmptyBg,        // Background color for empty node (e.g. CentralNode with no window docked into it)
    VanGuiCol_COUNT,

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiCol_TabActive = VanGuiCol_TabSelected,                  // [renamed in 1.90.9]
    VanGuiCol_TabUnfocused = VanGuiCol_TabDimmed,                 // [renamed in 1.90.9]
    VanGuiCol_TabUnfocusedActive = VanGuiCol_TabDimmedSelected,   // [renamed in 1.90.9]
    VanGuiCol_NavHighlight = VanGuiCol_NavCursor,                 // [renamed in 1.91.4]
#endif
};

// Enumeration for PushStyleVar() / PopStyleVar() to temporarily modify the VanGuiStyle structure.
// - The enum only refers to fields of VanGuiStyle which makes sense to be pushed/popped inside UI code.
//   During initialization or between frames, feel free to just poke into VanGuiStyle directly.
// - Tip: Use your programming IDE navigation facilities on the names in the _second column_ below to find the actual members and their description.
//   - In Visual Studio: Ctrl+Comma ("Edit.GoToAll") can follow symbols inside comments, whereas Ctrl+F12 ("Edit.GoToImplementation") cannot.
//   - In Visual Studio w/ Visual Assist installed: Alt+G ("VAssistX.GoToImplementation") can also follow symbols inside comments.
//   - In VS Code, CLion, etc.: Ctrl+Click can follow symbols inside comments.
// - When changing this enum, you need to update the associated internal table GStyleVarInfo[] accordingly. This is where we link enum values to members offset/type.
enum VanGuiStyleVar_
{
    // Enum name -------------------------- // Member in VanGuiStyle structure (see VanGuiStyle for descriptions)
    VanGuiStyleVar_Alpha,                    // float     Alpha
    VanGuiStyleVar_DisabledAlpha,            // float     DisabledAlpha
    VanGuiStyleVar_WindowPadding,            // VanVec2    WindowPadding
    VanGuiStyleVar_WindowRounding,           // float     WindowRounding
    VanGuiStyleVar_WindowBorderSize,         // float     WindowBorderSize
    VanGuiStyleVar_WindowMinSize,            // VanVec2    WindowMinSize
    VanGuiStyleVar_WindowTitleAlign,         // VanVec2    WindowTitleAlign
    VanGuiStyleVar_ChildRounding,            // float     ChildRounding
    VanGuiStyleVar_ChildBorderSize,          // float     ChildBorderSize
    VanGuiStyleVar_PopupRounding,            // float     PopupRounding
    VanGuiStyleVar_PopupBorderSize,          // float     PopupBorderSize
    VanGuiStyleVar_FramePadding,             // VanVec2    FramePadding
    VanGuiStyleVar_FrameRounding,            // float     FrameRounding
    VanGuiStyleVar_FrameBorderSize,          // float     FrameBorderSize
    VanGuiStyleVar_ItemSpacing,              // VanVec2    ItemSpacing
    VanGuiStyleVar_ItemInnerSpacing,         // VanVec2    ItemInnerSpacing
    VanGuiStyleVar_IndentSpacing,            // float     IndentSpacing
    VanGuiStyleVar_CellPadding,              // VanVec2    CellPadding
    VanGuiStyleVar_ScrollbarSize,            // float     ScrollbarSize
    VanGuiStyleVar_ScrollbarRounding,        // float     ScrollbarRounding
    VanGuiStyleVar_ScrollbarPadding,         // float     ScrollbarPadding
    VanGuiStyleVar_GrabMinSize,              // float     GrabMinSize
    VanGuiStyleVar_GrabRounding,             // float     GrabRounding
    VanGuiStyleVar_ImageRounding,            // float     ImageRounding
    VanGuiStyleVar_ImageBorderSize,          // float     ImageBorderSize
    VanGuiStyleVar_TabRounding,              // float     TabRounding
    VanGuiStyleVar_TabBorderSize,            // float     TabBorderSize
    VanGuiStyleVar_TabMinWidthBase,          // float     TabMinWidthBase
    VanGuiStyleVar_TabMinWidthShrink,        // float     TabMinWidthShrink
    VanGuiStyleVar_TabBarBorderSize,         // float     TabBarBorderSize
    VanGuiStyleVar_TabBarOverlineSize,       // float     TabBarOverlineSize
    VanGuiStyleVar_TableAngledHeadersAngle,  // float     TableAngledHeadersAngle
    VanGuiStyleVar_TableAngledHeadersTextAlign,// VanVec2  TableAngledHeadersTextAlign
    VanGuiStyleVar_TreeLinesSize,            // float     TreeLinesSize
    VanGuiStyleVar_TreeLinesRounding,        // float     TreeLinesRounding
    VanGuiStyleVar_DragDropTargetRounding,   // float     DragDropTargetRounding
    VanGuiStyleVar_ButtonTextAlign,          // VanVec2    ButtonTextAlign
    VanGuiStyleVar_SelectableTextAlign,      // VanVec2    SelectableTextAlign
    VanGuiStyleVar_SeparatorSize,            // float     SeparatorSize
    VanGuiStyleVar_SeparatorTextBorderSize,  // float     SeparatorTextBorderSize
    VanGuiStyleVar_SeparatorTextAlign,       // VanVec2    SeparatorTextAlign
    VanGuiStyleVar_SeparatorTextPadding,     // VanVec2    SeparatorTextPadding
    VanGuiStyleVar_COUNT
};

// Flags for InvisibleButton() [extended in vangui_internal.h]
enum VanGuiButtonFlags_
{
    VanGuiButtonFlags_None                   = 0,
    VanGuiButtonFlags_MouseButtonLeft        = 1 << 0,   // React on left mouse button (default)
    VanGuiButtonFlags_MouseButtonRight       = 1 << 1,   // React on right mouse button
    VanGuiButtonFlags_MouseButtonMiddle      = 1 << 2,   // React on center mouse button
    VanGuiButtonFlags_MouseButtonMask_       = VanGuiButtonFlags_MouseButtonLeft | VanGuiButtonFlags_MouseButtonRight | VanGuiButtonFlags_MouseButtonMiddle, // [Internal]
    VanGuiButtonFlags_EnableNav              = 1 << 3,   // InvisibleButton(): do not disable navigation/tabbing. Otherwise disabled by default.
    VanGuiButtonFlags_AllowOverlap           = 1 << 12,  // Hit testing will allow subsequent widgets to overlap this one. Require previous frame HoveredId to match before being usable. Shortcut to calling SetNextItemAllowOverlap().
};

// Flags for ColorEdit3() / ColorEdit4() / ColorPicker3() / ColorPicker4() / ColorButton()
enum VanGuiColorEditFlags_
{
    VanGuiColorEditFlags_None            = 0,
    VanGuiColorEditFlags_NoAlpha         = 1 << 1,   //              // ColorEdit, ColorPicker, ColorButton: ignore Alpha component (will only read 3 components from the input pointer).
    VanGuiColorEditFlags_NoPicker        = 1 << 2,   //              // ColorEdit: disable picker when clicking on color square.
    VanGuiColorEditFlags_NoOptions       = 1 << 3,   //              // ColorEdit: disable toggling options menu when right-clicking on inputs/small preview.
    VanGuiColorEditFlags_NoSmallPreview  = 1 << 4,   //              // ColorEdit, ColorPicker: disable color square preview next to the inputs. (e.g. to show only the inputs)
    VanGuiColorEditFlags_NoInputs        = 1 << 5,   //              // ColorEdit, ColorPicker: disable inputs sliders/text widgets (e.g. to show only the small preview color square).
    VanGuiColorEditFlags_NoTooltip       = 1 << 6,   //              // ColorEdit, ColorPicker, ColorButton: disable tooltip when hovering the preview.
    VanGuiColorEditFlags_NoLabel         = 1 << 7,   //              // ColorEdit, ColorPicker: disable display of inline text label (the label is still forwarded to the tooltip and picker).
    VanGuiColorEditFlags_NoSidePreview   = 1 << 8,   //              // ColorPicker: disable bigger color preview on right side of the picker, use small color square preview instead.
    VanGuiColorEditFlags_NoDragDrop      = 1 << 9,   //              // ColorEdit: disable drag and drop target/source. ColorButton: disable drag and drop source.
    VanGuiColorEditFlags_NoBorder        = 1 << 10,  //              // ColorButton: disable border (which is enforced by default)
    VanGuiColorEditFlags_NoColorMarkers  = 1 << 11,  //              // ColorEdit: disable rendering R/G/B/A color marker. May also be disabled globally by setting style.ColorMarkerSize = 0.

    // Alpha preview
    // - Prior to 1.91.8 (2025/01/21): alpha was made opaque in the preview by default using old name VanGuiColorEditFlags_AlphaPreview.
    // - We now display the preview as transparent by default. You can use VanGuiColorEditFlags_AlphaOpaque to use old behavior.
    // - The new flags may be combined better and allow finer controls.
    VanGuiColorEditFlags_AlphaOpaque     = 1 << 12,  //              // ColorEdit, ColorPicker, ColorButton: disable alpha in the preview,. Contrary to _NoAlpha it may still be edited when calling ColorEdit4()/ColorPicker4(). For ColorButton() this does the same as _NoAlpha.
    VanGuiColorEditFlags_AlphaNoBg       = 1 << 13,  //              // ColorEdit, ColorPicker, ColorButton: disable rendering a checkerboard background behind transparent color.
    VanGuiColorEditFlags_AlphaPreviewHalf= 1 << 14,  //              // ColorEdit, ColorPicker, ColorButton: display half opaque / half transparent preview.

    // User Options (right-click on widget to change some of them).
    VanGuiColorEditFlags_AlphaBar        = 1 << 18,  //              // ColorEdit, ColorPicker: show vertical alpha bar/gradient in picker.
    VanGuiColorEditFlags_HDR             = 1 << 19,  //              // (WIP) ColorEdit: Currently only disable 0.0f..1.0f limits in RGBA edition (note: you probably want to use VanGuiColorEditFlags_Float flag as well).
    VanGuiColorEditFlags_DisplayRGB      = 1 << 20,  // [Display]    // ColorEdit: override _display_ type among RGB/HSV/Hex. ColorPicker: select any combination using one or more of RGB/HSV/Hex.
    VanGuiColorEditFlags_DisplayHSV      = 1 << 21,  // [Display]    // "
    VanGuiColorEditFlags_DisplayHex      = 1 << 22,  // [Display]    // "
    VanGuiColorEditFlags_Uint8           = 1 << 23,  // [DataType]   // ColorEdit, ColorPicker, ColorButton: _display_ values formatted as 0..255.
    VanGuiColorEditFlags_Float           = 1 << 24,  // [DataType]   // ColorEdit, ColorPicker, ColorButton: _display_ values formatted as 0.0f..1.0f floats instead of 0..255 integers. No round-trip of value via integers.
    VanGuiColorEditFlags_PickerHueBar    = 1 << 25,  // [Picker]     // ColorPicker: bar for Hue, rectangle for Sat/Value.
    VanGuiColorEditFlags_PickerHueWheel  = 1 << 26,  // [Picker]     // ColorPicker: wheel for Hue, triangle for Sat/Value.
    VanGuiColorEditFlags_InputRGB        = 1 << 27,  // [Input]      // ColorEdit, ColorPicker: input and output data in RGB format.
    VanGuiColorEditFlags_InputHSV        = 1 << 28,  // [Input]      // ColorEdit, ColorPicker: input and output data in HSV format.

    // Defaults Options. You can set application defaults using SetColorEditOptions(). The intent is that you probably don't want to
    // override them in most of your calls. Let the user choose via the option menu and/or call SetColorEditOptions() once during startup.
    VanGuiColorEditFlags_DefaultOptions_ = VanGuiColorEditFlags_Uint8 | VanGuiColorEditFlags_DisplayRGB | VanGuiColorEditFlags_InputRGB | VanGuiColorEditFlags_PickerHueBar,

    // [Internal] Masks
    VanGuiColorEditFlags_AlphaMask_      = VanGuiColorEditFlags_NoAlpha | VanGuiColorEditFlags_AlphaOpaque | VanGuiColorEditFlags_AlphaNoBg | VanGuiColorEditFlags_AlphaPreviewHalf,
    VanGuiColorEditFlags_DisplayMask_    = VanGuiColorEditFlags_DisplayRGB | VanGuiColorEditFlags_DisplayHSV | VanGuiColorEditFlags_DisplayHex,
    VanGuiColorEditFlags_DataTypeMask_   = VanGuiColorEditFlags_Uint8 | VanGuiColorEditFlags_Float,
    VanGuiColorEditFlags_PickerMask_     = VanGuiColorEditFlags_PickerHueWheel | VanGuiColorEditFlags_PickerHueBar,
    VanGuiColorEditFlags_InputMask_      = VanGuiColorEditFlags_InputRGB | VanGuiColorEditFlags_InputHSV,

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiColorEditFlags_AlphaPreview = 0, // Removed in 1.91.8. This is the default now. Will display a checkerboard unless VanGuiColorEditFlags_AlphaNoBg is set.
#endif
    //VanGuiColorEditFlags_RGB = VanGuiColorEditFlags_DisplayRGB, VanGuiColorEditFlags_HSV = VanGuiColorEditFlags_DisplayHSV, VanGuiColorEditFlags_HEX = VanGuiColorEditFlags_DisplayHex  // [renamed in 1.69]
};

// Flags for DragFloat(), DragInt(), SliderFloat(), SliderInt() etc.
// We use the same sets of flags for DragXXX() and SliderXXX() functions as the features are the same and it makes it easier to swap them.
// (Those are per-item flags. There is shared behavior flag too: VanGuiIO: io.ConfigDragClickToInputText)
enum VanGuiSliderFlags_
{
    VanGuiSliderFlags_None               = 0,
    VanGuiSliderFlags_Logarithmic        = 1 << 5,       // Make the widget logarithmic (linear otherwise). Consider using VanGuiSliderFlags_NoRoundToFormat with this if using a format-string with small amount of digits.
    VanGuiSliderFlags_NoRoundToFormat    = 1 << 6,       // Disable rounding underlying value to match precision of the display format string (e.g. %.3f values are rounded to those 3 digits).
    VanGuiSliderFlags_NoInput            = 1 << 7,       // Disable Ctrl+Click or Enter key allowing to input text directly into the widget.
    VanGuiSliderFlags_WrapAround         = 1 << 8,       // Enable wrapping around from max to min and from min to max. Only supported by DragXXX() functions for now.
    VanGuiSliderFlags_ClampOnInput       = 1 << 9,       // Clamp value to min/max bounds when input manually with Ctrl+Click. By default Ctrl+Click allows going out of bounds.
    VanGuiSliderFlags_ClampZeroRange     = 1 << 10,      // Clamp even if min==max==0.0f. Otherwise due to legacy reason DragXXX functions don't clamp with those values. When your clamping limits are dynamic you almost always want to use it.
    VanGuiSliderFlags_NoSpeedTweaks      = 1 << 11,      // Disable keyboard modifiers altering tweak speed. Useful if you want to alter tweak speed yourself based on your own logic.
    VanGuiSliderFlags_ColorMarkers       = 1 << 12,      // DragScalarN(), SliderScalarN(): Draw R/G/B/A color markers on each component.
    VanGuiSliderFlags_AlwaysClamp        = VanGuiSliderFlags_ClampOnInput | VanGuiSliderFlags_ClampZeroRange,
    VanGuiSliderFlags_InvalidMask_       = 0x7000000F,   // [Internal] We treat using those bits as being potentially a 'float power' argument from legacy API (obsoleted 2020-08) that has got miscast to this enum, and will trigger an assert if needed.
};

// Identify a mouse button.
// Those values are guaranteed to be stable and we frequently use 0/1 directly. Named enums provided for convenience.
enum VanGuiMouseButton_
{
    VanGuiMouseButton_Left = 0,
    VanGuiMouseButton_Right = 1,
    VanGuiMouseButton_Middle = 2,
    VanGuiMouseButton_COUNT = 5
};

// Enumeration for GetMouseCursor()
// User code may request backend to display given cursor by calling SetMouseCursor(), which is why we have some cursors that are marked unused here
enum VanGuiMouseCursor_
{
    VanGuiMouseCursor_None = -1,
    VanGuiMouseCursor_Arrow = 0,
    VanGuiMouseCursor_TextInput,         // When hovering over InputText, etc.
    VanGuiMouseCursor_ResizeAll,         // (Unused by VanGUI functions)
    VanGuiMouseCursor_ResizeNS,          // When hovering over a horizontal border
    VanGuiMouseCursor_ResizeEW,          // When hovering over a vertical border or a column
    VanGuiMouseCursor_ResizeNESW,        // When hovering over the bottom-left corner of a window
    VanGuiMouseCursor_ResizeNWSE,        // When hovering over the bottom-right corner of a window
    VanGuiMouseCursor_Hand,              // (Unused by VanGUI functions. Use for e.g. hyperlinks)
    VanGuiMouseCursor_Wait,              // When waiting for something to process/load.
    VanGuiMouseCursor_Progress,          // When waiting for something to process/load, but application is still interactive.
    VanGuiMouseCursor_NotAllowed,        // When hovering something with disallowed interaction. Usually a crossed circle.
    VanGuiMouseCursor_COUNT
};

// Enumeration for AddMouseSourceEvent() actual source of Mouse Input data.
// Historically we use "Mouse" terminology everywhere to indicate pointer data, e.g. MousePos, IsMousePressed(), io.AddMousePosEvent()
// But that "Mouse" data can come from different source which occasionally may be useful for application to know about.
// You can submit a change of pointer type using io.AddMouseSourceEvent().
enum VanGuiMouseSource : int
{
    VanGuiMouseSource_Mouse = 0,         // Input is coming from an actual mouse.
    VanGuiMouseSource_TouchScreen,       // Input is coming from a touch screen (no hovering prior to initial press, less precise initial press aiming, dual-axis wheeling possible).
    VanGuiMouseSource_Pen,               // Input is coming from a pressure/magnetic pen (often used in conjunction with high-sampling rates).
    VanGuiMouseSource_COUNT
};

// Enumeration for VanGui::SetNextWindow***(), SetWindow***(), SetNextItem***() functions
// Represent a condition.
// Important: Treat as a regular enum! Do NOT combine multiple values using binary operators! All the functions above treat 0 as a shortcut to VanGuiCond_Always.
enum VanGuiCond_
{
    VanGuiCond_None          = 0,        // No condition (always set the variable), same as _Always
    VanGuiCond_Always        = 1 << 0,   // No condition (always set the variable), same as _None
    VanGuiCond_Once          = 1 << 1,   // Set the variable once per runtime session (only the first call will succeed)
    VanGuiCond_FirstUseEver  = 1 << 2,   // Set the variable if the object/window has no persistently saved data (no entry in .ini file)
    VanGuiCond_Appearing     = 1 << 3,   // Set the variable if the object/window is appearing after being hidden/inactive (or the first time)
};

//-----------------------------------------------------------------------------
// [SECTION] Tables API flags and structures (VanGuiTableFlags, VanGuiTableColumnFlags, VanGuiTableRowFlags, VanGuiTableBgTarget, VanGuiTableSortSpecs, VanGuiTableColumnSortSpecs)
//-----------------------------------------------------------------------------

// Flags for VanGui::BeginTable()
// - Important! Sizing policies have complex and subtle side effects, much more so than you would expect.
//   Read comments/demos carefully + experiment with live demos to get acquainted with them.
// - The DEFAULT sizing policies are:
//    - Default to VanGuiTableFlags_SizingFixedFit    if ScrollX is on, or if host window has VanGuiWindowFlags_AlwaysAutoResize.
//    - Default to VanGuiTableFlags_SizingStretchSame if ScrollX is off.
// - When ScrollX is off:
//    - Table defaults to VanGuiTableFlags_SizingStretchSame -> all Columns defaults to VanGuiTableColumnFlags_WidthStretch with same weight.
//    - Columns sizing policy allowed: Stretch (default), Fixed/Auto.
//    - Fixed Columns (if any) will generally obtain their requested width (unless the table cannot fit them all).
//    - Stretch Columns will share the remaining width according to their respective weight.
//    - Mixed Fixed/Stretch columns is possible but has various side-effects on resizing behaviors.
//      The typical use of mixing sizing policies is: any number of LEADING Fixed columns, followed by one or two TRAILING Stretch columns.
//      (this is because the visible order of columns have subtle but necessary effects on how they react to manual resizing).
// - When ScrollX is on:
//    - Table defaults to VanGuiTableFlags_SizingFixedFit -> all Columns defaults to VanGuiTableColumnFlags_WidthFixed
//    - Columns sizing policy allowed: Fixed/Auto mostly.
//    - Fixed Columns can be enlarged as needed. Table will show a horizontal scrollbar if needed.
//    - When using auto-resizing (non-resizable) fixed columns, querying the content width to use item right-alignment e.g. SetNextItemWidth(-FLT_MIN) doesn't make sense, would create a feedback loop.
//    - Using Stretch columns OFTEN DOES NOT MAKE SENSE if ScrollX is on, UNLESS you have specified a value for 'inner_width' in BeginTable().
//      If you specify a value for 'inner_width' then effectively the scrolling space is known and Stretch or mixed Fixed/Stretch columns become meaningful again.
// - Read on documentation at the top of vangui_tables.cpp for details.
enum VanGuiTableFlags_
{
    // Features
    VanGuiTableFlags_None                       = 0,
    VanGuiTableFlags_Resizable                  = 1 << 0,   // Enable resizing columns.
    VanGuiTableFlags_Reorderable                = 1 << 1,   // Enable reordering columns in header row. (Need calling TableSetupColumn() + TableHeadersRow() to display headers, or using VanGuiTableFlags_ContextMenuInBody to access context-menu without headers).
    VanGuiTableFlags_Hideable                   = 1 << 2,   // Enable hiding/disabling columns in context menu.
    VanGuiTableFlags_Sortable                   = 1 << 3,   // Enable sorting. Call TableGetSortSpecs() to obtain sort specs. Also see VanGuiTableFlags_SortMulti and VanGuiTableFlags_SortTristate.
    VanGuiTableFlags_NoSavedSettings            = 1 << 4,   // Disable persisting columns order, width, visibility and sort settings in the .ini file.
    VanGuiTableFlags_ContextMenuInBody          = 1 << 5,   // Right-click on columns body/contents will also display table context menu. By default it is available in TableHeadersRow().
    // Decorations
    VanGuiTableFlags_RowBg                      = 1 << 6,   // Set each RowBg color with VanGuiCol_TableRowBg or VanGuiCol_TableRowBgAlt (equivalent of calling TableSetBgColor with VanGuiTableBgFlags_RowBg0 on each row manually)
    VanGuiTableFlags_BordersInnerH              = 1 << 7,   // Draw horizontal borders between rows.
    VanGuiTableFlags_BordersOuterH              = 1 << 8,   // Draw horizontal borders at the top and bottom.
    VanGuiTableFlags_BordersInnerV              = 1 << 9,   // Draw vertical borders between columns.
    VanGuiTableFlags_BordersOuterV              = 1 << 10,  // Draw vertical borders on the left and right sides.
    VanGuiTableFlags_BordersH                   = VanGuiTableFlags_BordersInnerH | VanGuiTableFlags_BordersOuterH, // Draw horizontal borders.
    VanGuiTableFlags_BordersV                   = VanGuiTableFlags_BordersInnerV | VanGuiTableFlags_BordersOuterV, // Draw vertical borders.
    VanGuiTableFlags_BordersInner               = VanGuiTableFlags_BordersInnerV | VanGuiTableFlags_BordersInnerH, // Draw inner borders.
    VanGuiTableFlags_BordersOuter               = VanGuiTableFlags_BordersOuterV | VanGuiTableFlags_BordersOuterH, // Draw outer borders.
    VanGuiTableFlags_Borders                    = VanGuiTableFlags_BordersInner | VanGuiTableFlags_BordersOuter,   // Draw all borders.
    VanGuiTableFlags_NoBordersInBody            = 1 << 11,  // [ALPHA] Disable vertical borders in columns Body (borders will always appear in Headers). -> May move to style
    VanGuiTableFlags_NoBordersInBodyUntilResize = 1 << 12,  // [ALPHA] Disable vertical borders in columns Body until hovered for resize (borders will always appear in Headers). -> May move to style
    // Sizing Policy (read above for defaults)
    VanGuiTableFlags_SizingFixedFit             = 1 << 13,  // Columns default to _WidthFixed or _WidthAuto (if resizable or not resizable), matching contents width.
    VanGuiTableFlags_SizingFixedSame            = 2 << 13,  // Columns default to _WidthFixed or _WidthAuto (if resizable or not resizable), matching the maximum contents width of all columns. Implicitly enable VanGuiTableFlags_NoKeepColumnsVisible.
    VanGuiTableFlags_SizingStretchProp          = 3 << 13,  // Columns default to _WidthStretch with default weights proportional to each columns contents widths.
    VanGuiTableFlags_SizingStretchSame          = 4 << 13,  // Columns default to _WidthStretch with default weights all equal, unless overridden by TableSetupColumn().
    // Sizing Extra Options
    VanGuiTableFlags_NoHostExtendX              = 1 << 16,  // Make outer width auto-fit to columns, overriding outer_size.x value. Only available when ScrollX/ScrollY are disabled and Stretch columns are not used.
    VanGuiTableFlags_NoHostExtendY              = 1 << 17,  // Make outer height stop exactly at outer_size.y (prevent auto-extending table past the limit). Only available when ScrollX/ScrollY are disabled. Data below the limit will be clipped and not visible.
    VanGuiTableFlags_NoKeepColumnsVisible       = 1 << 18,  // Disable keeping column always minimally visible when ScrollX is off and table gets too small. Not recommended if columns are resizable.
    VanGuiTableFlags_PreciseWidths              = 1 << 19,  // Disable distributing remainder width to stretched columns (width allocation on a 100-wide table with 3 columns: Without this flag: 33,33,34. With this flag: 33,33,33). With larger number of columns, resizing will appear to be less smooth.
    // Clipping
    VanGuiTableFlags_NoClip                     = 1 << 20,  // Disable clipping rectangle for every individual columns (reduce draw command count, items will be able to overflow into other columns). Generally incompatible with TableSetupScrollFreeze().
    // Padding
    VanGuiTableFlags_PadOuterX                  = 1 << 21,  // Default if BordersOuterV is on. Enable outermost padding. Generally desirable if you have headers.
    VanGuiTableFlags_NoPadOuterX                = 1 << 22,  // Default if BordersOuterV is off. Disable outermost padding.
    VanGuiTableFlags_NoPadInnerX                = 1 << 23,  // Disable inner padding between columns (double inner padding if BordersOuterV is on, single inner padding if BordersOuterV is off).
    // Scrolling
    VanGuiTableFlags_ScrollX                    = 1 << 24,  // Enable horizontal scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size. Changes default sizing policy. Because this creates a child window, ScrollY is currently generally recommended when using ScrollX.
    VanGuiTableFlags_ScrollY                    = 1 << 25,  // Enable vertical scrolling. Require 'outer_size' parameter of BeginTable() to specify the container size.
    // Sorting
    VanGuiTableFlags_SortMulti                  = 1 << 26,  // Hold shift when clicking headers to sort on multiple column. TableGetSortSpecs() may return specs where (SpecsCount > 1).
    VanGuiTableFlags_SortTristate               = 1 << 27,  // Allow no sorting, disable default sorting. TableGetSortSpecs() may return specs where (SpecsCount == 0).
    // Miscellaneous
    VanGuiTableFlags_HighlightHoveredColumn     = 1 << 28,  // Highlight column headers when hovered (may evolve into a fuller highlight)

    // [Internal] Combinations and masks
    VanGuiTableFlags_SizingMask_                = VanGuiTableFlags_SizingFixedFit | VanGuiTableFlags_SizingFixedSame | VanGuiTableFlags_SizingStretchProp | VanGuiTableFlags_SizingStretchSame,
};

// Flags for VanGui::TableSetupColumn()
enum VanGuiTableColumnFlags_
{
    // Input configuration flags
    VanGuiTableColumnFlags_None                  = 0,
    VanGuiTableColumnFlags_Disabled              = 1 << 0,   // Overriding/master disable flag: hide column, won't show in context menu (unlike calling TableSetColumnEnabled() which manipulates the user accessible state)
    VanGuiTableColumnFlags_DefaultHide           = 1 << 1,   // Default as a hidden/disabled column.
    VanGuiTableColumnFlags_DefaultSort           = 1 << 2,   // Default as a sorting column.
    VanGuiTableColumnFlags_WidthStretch          = 1 << 3,   // Column will stretch. Preferable with horizontal scrolling disabled (default if table sizing policy is _SizingStretchSame or _SizingStretchProp).
    VanGuiTableColumnFlags_WidthFixed            = 1 << 4,   // Column will not stretch. Preferable with horizontal scrolling enabled (default if table sizing policy is _SizingFixedFit and table is resizable).
    VanGuiTableColumnFlags_NoResize              = 1 << 5,   // Disable manual resizing.
    VanGuiTableColumnFlags_NoReorder             = 1 << 6,   // Disable manual reordering this column, this will also prevent other columns from crossing over this column.
    VanGuiTableColumnFlags_NoHide                = 1 << 7,   // Disable ability to hide/disable this column.
    VanGuiTableColumnFlags_NoClip                = 1 << 8,   // Disable clipping for this column (all NoClip columns will render in a same draw command).
    VanGuiTableColumnFlags_NoSort                = 1 << 9,   // Disable ability to sort on this field (even if VanGuiTableFlags_Sortable is set on the table).
    VanGuiTableColumnFlags_NoSortAscending       = 1 << 10,  // Disable ability to sort in the ascending direction.
    VanGuiTableColumnFlags_NoSortDescending      = 1 << 11,  // Disable ability to sort in the descending direction.
    VanGuiTableColumnFlags_NoHeaderLabel         = 1 << 12,  // TableHeadersRow() will submit an empty label for this column. Convenient for some small columns. Name will still appear in context menu or in angled headers. You may append into this cell by calling TableSetColumnIndex() right after the TableHeadersRow() call.
    VanGuiTableColumnFlags_NoHeaderWidth         = 1 << 13,  // Disable header text width contribution to automatic column width.
    VanGuiTableColumnFlags_PreferSortAscending   = 1 << 14,  // Make the initial sort direction Ascending when first sorting on this column (default).
    VanGuiTableColumnFlags_PreferSortDescending  = 1 << 15,  // Make the initial sort direction Descending when first sorting on this column.
    VanGuiTableColumnFlags_IndentEnable          = 1 << 16,  // Use current Indent value when entering cell (default for column 0).
    VanGuiTableColumnFlags_IndentDisable         = 1 << 17,  // Ignore current Indent value when entering cell (default for columns > 0). Indentation changes _within_ the cell will still be honored.
    VanGuiTableColumnFlags_AngledHeader          = 1 << 18,  // TableHeadersRow() will submit an angled header row for this column. Note this will add an extra row.

    // Output status flags, read-only via TableGetColumnFlags()
    VanGuiTableColumnFlags_IsEnabled             = 1 << 24,  // Status: is enabled == not hidden by user/api (referred to as "Hide" in _DefaultHide and _NoHide) flags.
    VanGuiTableColumnFlags_IsVisible             = 1 << 25,  // Status: is visible == is enabled AND not clipped by scrolling.
    VanGuiTableColumnFlags_IsSorted              = 1 << 26,  // Status: is currently part of the sort specs
    VanGuiTableColumnFlags_IsHovered             = 1 << 27,  // Status: is hovered by mouse

    // [Internal] Combinations and masks
    VanGuiTableColumnFlags_WidthMask_            = VanGuiTableColumnFlags_WidthStretch | VanGuiTableColumnFlags_WidthFixed,
    VanGuiTableColumnFlags_IndentMask_           = VanGuiTableColumnFlags_IndentEnable | VanGuiTableColumnFlags_IndentDisable,
    VanGuiTableColumnFlags_StatusMask_           = VanGuiTableColumnFlags_IsEnabled | VanGuiTableColumnFlags_IsVisible | VanGuiTableColumnFlags_IsSorted | VanGuiTableColumnFlags_IsHovered,
    VanGuiTableColumnFlags_NoDirectResize_       = 1 << 30,  // [Internal] Disable user resizing this column directly (it may however we resized indirectly from its left edge)
};

// Flags for VanGui::TableNextRow()
enum VanGuiTableRowFlags_
{
    VanGuiTableRowFlags_None                     = 0,
    VanGuiTableRowFlags_Headers                  = 1 << 0,   // Identify header row (set default background color + width of its contents accounted differently for auto column width)
};

// Enum for VanGui::TableSetBgColor()
// Background colors are rendering in 3 layers:
//  - Layer 0: draw with RowBg0 color if set, otherwise draw with ColumnBg0 if set.
//  - Layer 1: draw with RowBg1 color if set, otherwise draw with ColumnBg1 if set.
//  - Layer 2: draw with CellBg color if set.
// The purpose of the two row/columns layers is to let you decide if a background color change should override or blend with the existing color.
// When using VanGuiTableFlags_RowBg on the table, each row has the RowBg0 color automatically set for odd/even rows.
// If you set the color of RowBg0 target, your color will override the existing RowBg0 color.
// If you set the color of RowBg1 or ColumnBg1 target, your color will blend over the RowBg0 color.
enum VanGuiTableBgTarget_
{
    VanGuiTableBgTarget_None                     = 0,
    VanGuiTableBgTarget_RowBg0                   = 1,        // Set row background color 0 (generally used for background, automatically set when VanGuiTableFlags_RowBg is used)
    VanGuiTableBgTarget_RowBg1                   = 2,        // Set row background color 1 (generally used for selection marking)
    VanGuiTableBgTarget_CellBg                   = 3,        // Set cell background color (top-most color)
};

// Sorting specifications for a table (often handling sort specs for a single column, occasionally more)
// Obtained by calling TableGetSortSpecs().
// When 'SpecsDirty == true' you can sort your data. It will be true with sorting specs have changed since last call, or the first time.
// Make sure to set 'SpecsDirty = false' after sorting, else you may wastefully sort your data every frame!
struct VanGuiTableSortSpecs
{
    const VanGuiTableColumnSortSpecs* Specs;     // Pointer to sort spec array.
    int                         SpecsCount;     // Sort spec count. Most often 1. May be > 1 when VanGuiTableFlags_SortMulti is enabled. May be == 0 when VanGuiTableFlags_SortTristate is enabled.
    bool                        SpecsDirty;     // Set to true when specs have changed since last time! Use this to sort again, then clear the flag.

    VanGuiTableSortSpecs()       { memset((void*)this, 0, sizeof(*this)); } // POD-safe zero-init
};

// Sorting specification for one column of a table (sizeof == 12 bytes)
struct VanGuiTableColumnSortSpecs
{
    VanGuiID                     ColumnUserID;       // User id of the column (if specified by a TableSetupColumn() call)
    VanS16                       ColumnIndex;        // Index of the column
    VanS16                       SortOrder;          // Index within parent VanGuiTableSortSpecs (always stored in order starting from 0, tables sorted on a single criteria will always have a 0 here)
    VanGuiSortDirection          SortDirection;      // VanGuiSortDirection_Ascending or VanGuiSortDirection_Descending

    VanGuiTableColumnSortSpecs() { memset((void*)this, 0, sizeof(*this)); } // POD-safe zero-init
};

//-----------------------------------------------------------------------------
// [SECTION] Helpers: Debug log, memory allocations macros, VanVector<>
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Debug Logging into ShowDebugLogWindow(), tty and more.
//-----------------------------------------------------------------------------

#ifndef VANGUI_DISABLE_DEBUG_TOOLS
#define VANGUI_DEBUG_LOG(...)        VanGui::DebugLog(__VA_ARGS__)
#else
#define VANGUI_DEBUG_LOG(...)        ((void)0)
#endif

//-----------------------------------------------------------------------------
// VAN_MALLOC(), VAN_FREE(), VAN_NEW(), VAN_PLACEMENT_NEW(), VAN_DELETE()
// We call C++ constructor on own allocated memory via the placement "new(ptr) Type()" syntax.
// Defining a custom placement new() with a custom parameter allows us to bypass including <new> which on some platforms complains when user has disabled exceptions.
//-----------------------------------------------------------------------------

struct VanNewWrapper {};
inline void* operator new(size_t, VanNewWrapper, void* ptr) { return ptr; }
inline void  operator delete(void*, VanNewWrapper, void*)   {} // This is only required so we can use the symmetrical new()
#define VAN_ALLOC(_SIZE)                     VanGui::MemAlloc(_SIZE)
#define VAN_FREE(_PTR)                       VanGui::MemFree(_PTR)
#define VAN_PLACEMENT_NEW(_PTR)              new(VanNewWrapper(), _PTR)
#define VAN_NEW(_TYPE)                       new(VanNewWrapper(), VanGui::MemAlloc(sizeof(_TYPE))) _TYPE
template<typename T> void VAN_DELETE(T* p)   { if (p) { p->~T(); VanGui::MemFree(p); } }

//-----------------------------------------------------------------------------
// VanVector<>
// Lightweight std::vector<>-like class to avoid dragging dependencies (also, some implementations of STL with debug enabled are absurdly slow, we bypass it so our code runs fast in debug).
//-----------------------------------------------------------------------------
// - You generally do NOT need to care or use this ever. But we need to make it available in vangui.h because some of our public structures are relying on it.
// - We use std-like naming convention here, which is a little unusual for this codebase.
// - Important: clear() frees memory, resize(0) keep the allocated buffer. We use resize(0) a lot to intentionally recycle allocated buffers across frames and amortize our costs.
// - Important: our implementation does NOT call C++ constructors/destructors, we treat everything as raw data! This is intentional but be extra mindful of that,
//   Do NOT use this class as a std::vector replacement in your own code! Many of the structures used by VanGUI can be safely initialized by a zero-memset.
//-----------------------------------------------------------------------------

VAN_MSVC_RUNTIME_CHECKS_OFF
template<typename T>
struct VanVector
{
    int                 Size;
    int                 Capacity;
    T*                  Data;

    // Provide standard typedefs but we don't use them ourselves.
    using value_type      = T;
    using iterator        = value_type*;
    using const_iterator  = const value_type*;

    // Constructors, destructor
    VanVector()                                       { Size = Capacity = 0; Data = nullptr; }
    VanVector(const VanVector<T>& src)                 { Size = Capacity = 0; Data = nullptr; operator=(src); }
    VanVector<T>& operator=(const VanVector<T>& src)   { clear(); resize(src.Size); if (Data && src.Data) memcpy(Data, src.Data, static_cast<size_t>(Size) * sizeof(T)); return *this; }
    ~VanVector()                                      { if (Data) VAN_FREE(Data); } // Important: does not destruct anything

    void         clear()                             { if (Data) { Size = Capacity = 0; VAN_FREE(Data); Data = nullptr; } }  // Important: does not destruct anything
    void         clear_delete()                      { for (int n = 0; n < Size; n++) VAN_DELETE(Data[n]); clear(); }     // Important: never called automatically! always explicit.
    void         clear_destruct()                    { for (int n = 0; n < Size; n++) Data[n].~T(); clear(); }           // Important: never called automatically! always explicit.

    [[nodiscard]] bool         empty() const noexcept                       { return Size == 0; }
    [[nodiscard]] int          size() const noexcept                        { return Size; }
    [[nodiscard]] size_t       size_in_bytes() const noexcept               { return static_cast<size_t>(Size) * sizeof(T); }
    [[nodiscard]] int          max_size() const noexcept                    { return 0x7FFFFFFF / static_cast<int>(sizeof(T)); }
    [[nodiscard]] int          capacity() const noexcept                    { return Capacity; }
    T&           operator[](int i)                   { VAN_ASSERT(i >= 0 && i < Size); return Data[i]; }
    const T&     operator[](int i) const             { VAN_ASSERT(i >= 0 && i < Size); return Data[i]; }

    [[nodiscard]] T*           begin() noexcept                             { return Data; }
    [[nodiscard]] const T*     begin() const noexcept                       { return Data; }
    [[nodiscard]] T*           end() noexcept                               { return Data + Size; }
    [[nodiscard]] const T*     end() const noexcept                         { return Data + Size; }
    [[nodiscard]] T&           front()                             { VAN_ASSERT(Size > 0); return Data[0]; }
    [[nodiscard]] const T&     front() const                       { VAN_ASSERT(Size > 0); return Data[0]; }
    [[nodiscard]] T&           back()                              { VAN_ASSERT(Size > 0); return Data[Size - 1]; }
    [[nodiscard]] const T&     back() const                        { VAN_ASSERT(Size > 0); return Data[Size - 1]; }
    void         swap(VanVector<T>& rhs)              { int rhs_size = rhs.Size; rhs.Size = Size; Size = rhs_size; int rhs_cap = rhs.Capacity; rhs.Capacity = Capacity; Capacity = rhs_cap; T* rhs_data = rhs.Data; rhs.Data = Data; Data = rhs_data; }
    operator std::span<T>() noexcept { return std::span<T>(Data, static_cast<size_t>(Size)); }
    operator std::span<const T>() const noexcept { return std::span<const T>(Data, static_cast<size_t>(Size)); }

    [[nodiscard]] int          _grow_capacity(int sz) const        { int new_capacity = Capacity ? (Capacity + Capacity / 2) : 8; return new_capacity > sz ? new_capacity : sz; }
    void         resize(int new_size)                { if (new_size > Capacity) [[unlikely]] reserve(_grow_capacity(new_size)); Size = new_size; }
    void         resize(int new_size, const T& v)    { if (new_size > Capacity) [[unlikely]] reserve(_grow_capacity(new_size)); if (new_size > Size) for (int n = Size; n < new_size; n++) memcpy(&Data[n], &v, sizeof(v)); Size = new_size; }
    void         shrink(int new_size)                { VAN_ASSERT(new_size <= Size); Size = new_size; } // Resize a vector to a smaller size, guaranteed not to cause a reallocation
    void         reserve(int new_capacity)           { if (new_capacity <= Capacity) return; VAN_ASSERT(new_capacity >= 0 && static_cast<size_t>(new_capacity) < (SIZE_MAX / sizeof(T))); T* new_data = static_cast<T*>(VAN_ALLOC(static_cast<size_t>(new_capacity) * sizeof(T))); if (Data) { memcpy(new_data, Data, static_cast<size_t>(Size) * sizeof(T)); VAN_FREE(Data); } Data = new_data; Capacity = new_capacity; }
    void         reserve_discard(int new_capacity)   { if (new_capacity <= Capacity) return; VAN_ASSERT(new_capacity >= 0 && static_cast<size_t>(new_capacity) < (SIZE_MAX / sizeof(T))); if (Data) { VAN_FREE(Data); Data = nullptr; } Data = static_cast<T*>(VAN_ALLOC(static_cast<size_t>(new_capacity) * sizeof(T))); Capacity = new_capacity; }

    // NB: It is illegal to call push_back/push_front/insert with a reference pointing inside the VanVector data itself! e.g. v.push_back(v[10]) is forbidden.
    void         push_back(const T& v)               { if (Size == Capacity) [[unlikely]] reserve(_grow_capacity(Size + 1)); memcpy(&Data[Size], &v, sizeof(v)); Size++; }
    void         pop_back()                          { VAN_ASSERT(Size > 0); Size--; }
    void         push_front(const T& v)              { if (Size == 0) push_back(v); else insert(Data, v); }
    T*           erase(const T* it)                  { VAN_ASSERT(it >= Data && it < Data + Size); const ptrdiff_t off = it - Data; memmove(Data + off, Data + off + 1, (static_cast<size_t>(Size) - static_cast<size_t>(off) - 1) * sizeof(T)); Size--; return Data + off; }
    T*           erase(const T* it, const T* it_last){ VAN_ASSERT(it >= Data && it < Data + Size && it_last >= it && it_last <= Data + Size); const ptrdiff_t count = it_last - it; const ptrdiff_t off = it - Data; memmove(Data + off, Data + off + count, (static_cast<size_t>(Size) - static_cast<size_t>(off) - static_cast<size_t>(count)) * sizeof(T)); Size -= static_cast<int>(count); return Data + off; }
    T*           erase_unsorted(const T* it)         { VAN_ASSERT(it >= Data && it < Data + Size);  const ptrdiff_t off = it - Data; if (it < Data + Size - 1) memcpy(Data + off, Data + Size - 1, sizeof(T)); Size--; return Data + off; }
    T*           insert(const T* it, const T& v)     { VAN_ASSERT(it >= Data && it <= Data + Size); const ptrdiff_t off = it - Data; if (Size == Capacity) reserve(_grow_capacity(Size + 1)); if (off < static_cast<int>(Size)) memmove(Data + off + 1, Data + off, (static_cast<size_t>(Size) - static_cast<size_t>(off)) * sizeof(T)); memcpy(&Data[off], &v, sizeof(v)); Size++; return Data + off; }
    [[nodiscard]] bool         contains(const T& v) const          { const T* data = Data;  const T* data_end = Data + Size; while (data < data_end) if (*data++ == v) return true; return false; }
    [[nodiscard]] T*           find(const T& v)                    { T* data = Data;  const T* data_end = Data + Size; while (data < data_end) if (*data == v) break; else ++data; return data; }
    [[nodiscard]] const T*     find(const T& v) const              { const T* data = Data;  const T* data_end = Data + Size; while (data < data_end) if (*data == v) break; else ++data; return data; }
    [[nodiscard]] int          find_index(const T& v) const        { const T* data_end = Data + Size; const T* it = find(v); if (it == data_end) return -1; const ptrdiff_t off = it - Data; return static_cast<int>(off); }
    bool         find_erase(const T& v)              { const T* it = find(v); if (it < Data + Size) { erase(it); return true; } return false; }
    bool         find_erase_unsorted(const T& v)     { const T* it = find(v); if (it < Data + Size) { erase_unsorted(it); return true; } return false; }
    int          index_from_ptr(const T* it) const   { VAN_ASSERT(it >= Data && it < Data + Size); const ptrdiff_t off = it - Data; return static_cast<int>(off); }
};
VAN_MSVC_RUNTIME_CHECKS_RESTORE

//-----------------------------------------------------------------------------
// [SECTION] VanGuiStyle
//-----------------------------------------------------------------------------
// You may modify the VanGui::GetStyle() main instance during initialization and before NewFrame().
// During the frame, use VanGui::PushStyleVar(VanGuiStyleVar_XXXX)/PopStyleVar() to alter the main style values,
// and VanGui::PushStyleColor(VanGuiCol_XXX)/PopStyleColor() for colors.
//-----------------------------------------------------------------------------

struct VanGuiStyle
{
    // Font scaling
    // - recap: VanGui::GetFontSize() == FontSizeBase * (FontScaleMain * FontScaleDpi * other_scaling_factors)
    float       FontSizeBase;               // Current base font size before external global factors are applied. Use PushFont(nullptr, size) to modify. Use VanGui::GetFontSize() to obtain scaled value.
    float       FontScaleMain;              // Main global scale factor. May be set by application once, or exposed to end-user.
    float       FontScaleDpi;               // Additional global scale factor from viewport/monitor contents scale. In docking branch: when io.ConfigDpiScaleFonts is enabled, this is automatically overwritten when changing monitor DPI.

    float       Alpha;                      // Global alpha applies to everything in VanGUI.
    float       DisabledAlpha;              // Additional alpha multiplier applied by BeginDisabled(). Multiply over current value of Alpha.
    VanVec2      WindowPadding;              // Padding within a window.
    float       WindowRounding;             // Radius of window corners rounding. Set to 0.0f to have rectangular windows. Large values tend to lead to variety of artifacts and are not recommended.
    float       WindowBorderSize;           // Thickness of border around windows. Generally set to 0.0f or 1.0f. (Other values are not well tested and more CPU/GPU costly).
    float       WindowBorderHoverPadding;   // Hit-testing extent outside/inside resizing border. Also extend determination of hovered window. Generally meaningfully larger than WindowBorderSize to make it easy to reach borders.
    VanVec2      WindowMinSize;              // Minimum window size. This is a global setting. If you want to constrain individual windows, use SetNextWindowSizeConstraints().
    VanVec2      WindowTitleAlign;           // Alignment for title bar text. Defaults to (0.0f,0.5f) for left-aligned,vertically centered.
    VanGuiDir    WindowMenuButtonPosition;   // Side of the collapsing/docking button in the title bar (None/Left/Right). Defaults to VanGuiDir_Left.
    float       ChildRounding;              // Radius of child window corners rounding. Set to 0.0f to have rectangular windows.
    float       ChildBorderSize;            // Thickness of border around child windows. Generally set to 0.0f or 1.0f. (Other values are not well tested and more CPU/GPU costly).
    float       PopupRounding;              // Radius of popup window corners rounding. (Note that tooltip windows use WindowRounding)
    float       PopupBorderSize;            // Thickness of border around popup/tooltip windows. Generally set to 0.0f or 1.0f. (Other values are not well tested and more CPU/GPU costly).
    VanVec2      FramePadding;               // Padding within a framed rectangle (used by most widgets).
    float       FrameRounding;              // Radius of frame corners rounding. Set to 0.0f to have rectangular frame (used by most widgets).
    float       FrameBorderSize;            // Thickness of border around frames. Generally set to 0.0f or 1.0f. (Other values are not well tested and more CPU/GPU costly).
    VanVec2      ItemSpacing;                // Horizontal and vertical spacing between widgets/lines.
    VanVec2      ItemInnerSpacing;           // Horizontal and vertical spacing between within elements of a composed widget (e.g. a slider and its label).
    VanVec2      CellPadding;                // Padding within a table cell. Cellpadding.x is locked for entire table. CellPadding.y may be altered between different rows.
    VanVec2      TouchExtraPadding;          // Expand reactive bounding box for touch-based system where touch position is not accurate enough. Unfortunately we don't sort widgets so priority on overlap will always be given to the first widget. So don't grow this too much!
    float       IndentSpacing;              // Horizontal indentation when e.g. entering a tree node. Generally == (FontSize + FramePadding.x*2).
    float       ColumnsMinSpacing;          // Minimum horizontal spacing between two columns. Preferably > (FramePadding.x + 1).
    float       ScrollbarSize;              // Width of the vertical scrollbar, Height of the horizontal scrollbar.
    float       ScrollbarRounding;          // Radius of grab corners for scrollbar.
    float       ScrollbarPadding;           // Padding of scrollbar grab within its frame (same for both axes).
    float       GrabMinSize;                // Minimum width/height of a grab box for slider/scrollbar.
    float       GrabRounding;               // Radius of grabs corners rounding. Set to 0.0f to have rectangular slider grabs.
    float       LogSliderDeadzone;          // The size in pixels of the dead-zone around zero on logarithmic sliders that cross zero.
    float       ImageRounding;              // Rounding of Image() calls.
    float       ImageBorderSize;            // Thickness of border around Image() calls.
    float       TabRounding;                // Radius of upper corners of a tab. Set to 0.0f to have rectangular tabs.
    float       TabBorderSize;              // Thickness of border around tabs.
    float       TabMinWidthBase;            // Minimum tab width, to make tabs larger than their contents. TabBar buttons are not affected.
    float       TabMinWidthShrink;          // Minimum tab width after shrinking, when using VanGuiTabBarFlags_FittingPolicyMixed policy.
    float       TabCloseButtonMinWidthSelected;     // -1: always visible. 0.0f: visible when hovered. >0.0f: visible when hovered if minimum width. FLT_MAX: never shrink, will behave like VanGuiTabBarFlags_FittingPolicyScroll.
    float       TabCloseButtonMinWidthUnselected;   // -1: always visible. 0.0f: visible when hovered. >0.0f: visible when hovered if minimum width. FLT_MAX: never show close button when unselected.
    float       TabBarBorderSize;           // Thickness of tab-bar separator, which takes on the tab active color to denote focus.
    float       TabBarOverlineSize;         // Thickness of tab-bar overline, which highlights the selected tab-bar.
    float       TableAngledHeadersAngle;    // Angle of angled headers (supported values range from -50.0f degrees to +50.0f degrees).
    VanVec2      TableAngledHeadersTextAlign;// Alignment of angled headers within the cell
    VanGuiTreeNodeFlags TreeLinesFlags;      // Default way to draw lines connecting TreeNode hierarchy. VanGuiTreeNodeFlags_DrawLinesNone or VanGuiTreeNodeFlags_DrawLinesFull or VanGuiTreeNodeFlags_DrawLinesToNodes.
    float       TreeLinesSize;              // Thickness of outlines when using VanGuiTreeNodeFlags_DrawLines.
    float       TreeLinesRounding;          // Radius of lines connecting child nodes to the vertical line.
    float       DragDropTargetRounding;     // Radius of the drag and drop target frame. When <0.0f: use FrameRounding.
    float       DragDropTargetBorderSize;   // Thickness of the drag and drop target border.
    float       DragDropTargetPadding;      // Size to expand the drag and drop target from actual target item size.
    float       ColorMarkerSize;            // Size of R/G/B/A color markers for ColorEdit4() and for Drags/Sliders when using VanGuiSliderFlags_ColorMarkers.
    VanGuiDir    ColorButtonPosition;        // Side of the color button in the ColorEdit4 widget (left/right). Defaults to VanGuiDir_Right.
    VanVec2      ButtonTextAlign;            // Alignment of button text when button is larger than text. Defaults to (0.5f, 0.5f) (centered).
    VanVec2      SelectableTextAlign;        // Alignment of selectable text. Defaults to (0.0f, 0.0f) (top-left aligned). It's generally important to keep this left-aligned if you want to lay multiple items on a same line.
    float       InputTextCursorSize;        // Thickness of cursor/caret in InputText().
    float       SeparatorSize;              // Thickness of border in Separator(). Must be >= 1.0f.
    float       SeparatorTextBorderSize;    // Thickness of border in SeparatorText()
    VanVec2      SeparatorTextAlign;         // Alignment of text within the separator. Defaults to (0.0f, 0.5f) (left aligned, center).
    VanVec2      SeparatorTextPadding;       // Horizontal offset of text from each edge of the separator + spacing on other axis. Generally small values. .y is recommended to be == FramePadding.y.
    VanVec2      DisplayWindowPadding;       // Apply to regular windows: amount which we enforce to keep visible when moving near edges of your screen.
    VanVec2      DisplaySafeAreaPadding;     // Apply to every windows, menus, popups, tooltips: amount where we avoid displaying contents. Adjust if you cannot see the edges of your screen (e.g. on a TV where scaling has not been configured).
    float       MouseCursorScale;           // Scale software rendered mouse cursor (when io.MouseDrawCursor is enabled). We apply per-monitor DPI scaling over this scale. May be removed later.
    bool        AntiAliasedLines;           // Enable anti-aliased lines/borders. Disable if you are really tight on CPU/GPU. Latched at the beginning of the frame (copied to VanDrawList).
    bool        AntiAliasedLinesUseTex;     // Enable anti-aliased lines/borders using textures where possible. Require backend to render with bilinear filtering (NOT point/nearest filtering). Latched at the beginning of the frame (copied to VanDrawList).
    bool        AntiAliasedFill;            // Enable anti-aliased edges around filled shapes (rounded rectangles, circles, etc.). Disable if you are really tight on CPU/GPU. Latched at the beginning of the frame (copied to VanDrawList).
    float       CurveTessellationTol;       // Tessellation tolerance when using PathBezierCurveTo() without a specific number of segments. Decrease for highly tessellated curves (higher quality, more polygons), increase to reduce quality.
    float       CircleTessellationMaxError; // Maximum error (in pixels) allowed when using AddCircle()/AddCircleFilled() or drawing rounded corner rectangles with no explicit segment count specified. Decrease for higher quality but more geometry.

    // Colors
    VanVec4      Colors[VanGuiCol_COUNT];

    // Behaviors
    // (It is possible to modify those fields mid-frame if specific behavior need it, unlike e.g. configuration fields in VanGuiIO)
    float             HoverStationaryDelay;     // Delay for IsItemHovered(VanGuiHoveredFlags_Stationary). Time required to consider mouse stationary.
    float             HoverDelayShort;          // Delay for IsItemHovered(VanGuiHoveredFlags_DelayShort). Usually used along with HoverStationaryDelay.
    float             HoverDelayNormal;         // Delay for IsItemHovered(VanGuiHoveredFlags_DelayNormal). "
    VanGuiHoveredFlags HoverFlagsForTooltipMouse;// Default flags when using IsItemHovered(VanGuiHoveredFlags_ForTooltip) or BeginItemTooltip()/SetItemTooltip() while using mouse.
    VanGuiHoveredFlags HoverFlagsForTooltipNav;  // Default flags when using IsItemHovered(VanGuiHoveredFlags_ForTooltip) or BeginItemTooltip()/SetItemTooltip() while using keyboard/gamepad.

    // [Internal]
    float       _MainScale;                 // FIXME-WIP: Reference scale, as applied by ScaleAllSizes(). PLEASE DO NOT USE THIS FOR NOW.
    float       _NextFrameFontSizeBase;     // FIXME: Temporary hack until we finish remaining work.

    // Functions
    VANGUI_API   VanGuiStyle();
    VANGUI_API   void ScaleAllSizes(float scale_factor); // Scale all spacing/padding/thickness values. Do not scale fonts. See comments in definition. Consider not calling this if your initial scale factor if <1.0.

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    // TabMinWidthForCloseButton = TabCloseButtonMinWidthUnselected // Renamed in 1.91.9.
#endif
};

//-----------------------------------------------------------------------------
// [SECTION] VanGuiIO
//-----------------------------------------------------------------------------
// Communicate most settings and inputs/outputs to VanGUI using this structure.
// Access via VanGui::GetIO(). Read 'Programmer guide' section in .cpp file for general usage.
// It is generally expected that:
// - initialization: backends and user code writes to VanGuiIO.
// - main loop: backends writes to VanGuiIO, user code and vangui code reads from VanGuiIO.
//-----------------------------------------------------------------------------
// Also see VanGui::GetPlatformIO() and VanGuiPlatformIO struct for OS/platform related functions: clipboard, IME etc.
//-----------------------------------------------------------------------------

// [Internal] Storage used by IsKeyDown(), IsKeyPressed() etc functions.
// If prior to 1.87 you used io.KeysDownDuration[] (which was marked as internal), you should use GetKeyData(key)->DownDuration and *NOT* io.KeysData[key]->DownDuration.
struct VanGuiKeyData
{
    bool        Down;               // True for if key is down
    float       DownDuration;       // Duration the key has been down (<0.0f: not pressed, 0.0f: just pressed, >0.0f: time held)
    float       DownDurationPrev;   // Last frame duration the key has been down
    float       AnalogValue;        // 0.0f..1.0f for gamepad values
};

struct VanGuiIO
{
    //------------------------------------------------------------------
    // Configuration                            // Default value
    //------------------------------------------------------------------

    VanGuiConfigFlags   ConfigFlags;             // = 0              // See VanGuiConfigFlags_ enum. Set by user/application. Keyboard/Gamepad navigation options, etc.
    VanGuiBackendFlags  BackendFlags;            // = 0              // See VanGuiBackendFlags_ enum. Set by backend (vangui_impl_xxx files or custom backend) to communicate features supported by the backend.
    VanVec2      DisplaySize;                    // <unset>          // Main display size, in pixels (== GetMainViewport()->Size). May change every frame.
    VanVec2      DisplayFramebufferScale;        // = (1, 1)         // Main display density. For retina display where window coordinates are different from framebuffer coordinates. This will affect font density + will end up in VanDrawData::FramebufferScale.
    float       DeltaTime;                      // = 1.0f/60.0f     // Time elapsed since last frame, in seconds. May change every frame.
    float       IniSavingRate;                  // = 5.0f           // Minimum time between saving positions/sizes to .ini file, in seconds.
    const char* IniFilename;                    // = "vangui.ini"    // Path to .ini file (important: default "vangui.ini" is relative to current working dir!). Set nullptr to disable automatic .ini loading/saving or if you want to manually call LoadIniSettingsXXX() / SaveIniSettingsXXX() functions.
    const char* LogFilename;                    // = "vangui_log.txt"// Path to .log file (default parameter to VanGui::LogToFile when no file is specified).
    void*       UserData;                       // = nullptr           // Store your own data.

    // Font system
    VanFontAtlas*Fonts;                          // <auto>           // Font atlas: load, rasterize and pack one or more fonts into a single texture.
    VanFont*     FontDefault;                    // = nullptr           // Font to use on NewFrame(). Use nullptr to uses Fonts->Fonts[0].
    bool        FontAllowUserScaling;           // = false          // Allow user scaling text of individual window with Ctrl+Wheel.

    // Keyboard/Gamepad Navigation options
    bool        ConfigNavSwapGamepadButtons;    // = false          // Swap Activate<>Cancel (A<>B) buttons, matching typical "Nintendo/Japanese style" gamepad layout.
    bool        ConfigNavMoveSetMousePos;       // = false          // Directional/tabbing navigation teleports the mouse cursor. May be useful on TV/console systems where moving a virtual mouse is difficult. Will update io.MousePos and set io.WantSetMousePos=true.
    bool        ConfigNavCaptureKeyboard;       // = true           // Sets io.WantCaptureKeyboard when io.NavActive is set.
    bool        ConfigNavEscapeClearFocusItem;  // = true           // Pressing Escape can clear focused item + navigation id/highlight. Set to false if you want to always keep highlight on.
    bool        ConfigNavEscapeClearFocusWindow;// = false          // Pressing Escape can clear focused window as well (super set of io.ConfigNavEscapeClearFocusItem).
    bool        ConfigNavCursorVisibleAuto;     // = true           // Using directional navigation key makes the cursor visible. Mouse click hides the cursor.
    bool        ConfigNavCursorVisibleAlways;   // = false          // Navigation cursor is always visible.

    // Miscellaneous options
    // (you can visualize and interact with all options in 'Demo->Configuration')
    bool        MouseDrawCursor;                // = false          // Request VanGui to draw a mouse cursor for you (if you are on a platform without a mouse cursor). Cannot be easily renamed to 'io.ConfigXXX' because this is frequently used by backend implementations.
    bool        ConfigMacOSXBehaviors;          // = defined(__APPLE__) // Swap Cmd<>Ctrl keys + OS X style text editing cursor movement using Alt instead of Ctrl, Shortcuts using Cmd/Super instead of Ctrl, Line/Text Start and End using Cmd+Arrows instead of Home/End, Double click selects by word instead of selecting whole text, Multi-selection in lists uses Cmd/Super instead of Ctrl.
    bool        ConfigInputTrickleEventQueue;   // = true           // Enable input queue trickling: some types of events submitted during the same frame (e.g. button down + up) will be spread over multiple frames, improving interactions with low framerates.
    bool        ConfigInputTextCursorBlink;     // = true           // Enable blinking cursor (optional as some users consider it to be distracting).
    bool        ConfigInputTextEnterKeepActive; // = false          // [BETA] Pressing Enter will reactivate item and select all text (single-line only).
    bool        ConfigDragClickToInputText;     // = false          // [BETA] Enable turning DragXXX widgets into text input with a simple mouse click-release (without moving). Not desirable on devices without a keyboard.
    bool        ConfigWindowsResizeFromEdges;   // = true           // Enable resizing of windows from their edges and from the lower-left corner. This requires VanGuiBackendFlags_HasMouseCursors for better mouse cursor feedback. (This used to be a per-window VanGuiWindowFlags_ResizeFromAnySide flag)
    bool        ConfigWindowsMoveFromTitleBarOnly;  // = false      // Enable allowing to move windows only when clicking on their title bar. Does not apply to windows without a title bar.
    bool        ConfigWindowsCopyContentsWithCtrlC; // = false      // [EXPERIMENTAL] Ctrl+C copy the contents of focused window into the clipboard. Experimental because: (1) has known issues with nested Begin/End pairs (2) text output quality varies (3) text output is in submission order rather than spatial order.
    bool        ConfigScrollbarScrollByPage;    // = true           // Enable scrolling page by page when clicking outside the scrollbar grab. When disabled, always scroll to clicked location. When enabled, Shift+Click scrolls to clicked location.
    float       ConfigMemoryCompactTimer;       // = 60.0f          // Timer (in seconds) to free transient windows/tables memory buffers when unused. Set to -1.0f to disable.

    // Docking options (when VanGuiConfigFlags_DockingEnable is set)
    bool        ConfigDockingNoSplit            = false;    // Simplified docking mode: disable window splitting, so docking is limited to merging multiple windows together into tab-bars.
    bool        ConfigDockingWithShift          = false;    // Enable docking when holding Shift only (allow to drop in wider space, reduce dragging influence, make it easier to correctly set-up explicit DockSpace when you are not in the habit of trying to skip it).
    bool        ConfigDockingAlwaysTabBar       = false;    // [BETA] [FIXME: This currently creates regression with auto-sizing and general overhead] Make every single floating window display within a docking node.
    bool        ConfigDockingTransparentPayload = false;    // [BETA] Make window or viewport transparent when docking and only display docking boxes on the target viewport. Useful if rendering of multiple viewport cannot be synced. Best used with ConfigViewportsNoAutoMerge.

    // Viewport options (when VanGuiConfigFlags_ViewportsEnable is set)
    bool        ConfigDpiScaleFonts             = false;    // [BETA] Automatically update style.FontScaleDpi to match monitor DPI when the current window changes monitor. Requires Platform_GetWindowDpiScale or monitor DpiScale to be set by the backend.
    bool        ConfigViewportsNoAutoMerge      = false;    // Set to make all floating vangui windows always create their own viewport. Otherwise, they are merged into the main host viewports when overlapping it. May also set on individual window via VanGuiWindowFlags_NoMerge.
    bool        ConfigViewportsNoTaskBarIcon    = false;    // Disable default OS task bar icon flag for secondary viewports. When a viewport doesn't want a task bar icon, VanGuiViewportFlags_NoTaskBarIcon will be set on it.
    bool        ConfigViewportsNoDecoration     = true;     // Disable default OS window decoration flag for secondary viewports. When a viewport doesn't want window decorations, VanGuiViewportFlags_NoDecoration will be set on it. Enabling decoration can create subsequent issues at OS levels (e.g. minimum window size).
    bool        ConfigViewportsNoDefaultParent  = false;    // Disable default OS parenting to main viewport for secondary viewports. By default, viewports are marked with ParentViewportId = <main_viewport>, expecting the platform backend to setup a parent/child relationship between the OS windows (some backend may ignore this).

    // Inputs Behaviors
    // (other variables, ones which are expected to be tweaked within UI code, are exposed in VanGuiStyle)
    float       MouseDoubleClickTime;           // = 0.30f          // Time for a double-click, in seconds.
    float       MouseDoubleClickMaxDist;        // = 6.0f           // Distance threshold to stay in to validate a double-click, in pixels.
    float       MouseDragThreshold;             // = 6.0f           // Distance threshold before considering we are dragging.
    float       KeyRepeatDelay;                 // = 0.275f         // When holding a key/button, time before it starts repeating, in seconds (for buttons in Repeat mode, etc.).
    float       KeyRepeatRate;                  // = 0.050f         // When holding a key/button, rate at which it repeats, in seconds.

    //------------------------------------------------------------------
    // Debug options
    //------------------------------------------------------------------

    // Options to configure Error Handling and how we handle recoverable errors [EXPERIMENTAL]
    // - Error recovery is provided as a way to facilitate:
    //    - Recovery after a programming error (native code or scripting language - the latter tends to facilitate iterating on code while running).
    //    - Recovery after running an exception handler or any error processing which may skip code after an error has been detected.
    // - Error recovery is not perfect nor guaranteed! It is a feature to ease development.
    //   You not are not supposed to rely on it in the course of a normal application run.
    // - Functions that support error recovery are using VAN_ASSERT_USER_ERROR() instead of VAN_ASSERT().
    // - By design, we do NOT allow error recovery to be 100% silent. One of the three options needs to be checked!
    // - Always ensure that on programmers seats you have at minimum Asserts or Tooltips enabled when making direct vangui API calls!
    //   Otherwise it would severely hinder your ability to catch and correct mistakes!
    // - Programmer seats: keep asserts (default), or disable asserts and keep error tooltips (new and nice!)
    // - Non-programmer seats: maybe disable asserts, but make sure errors are resurfaced (tooltips, visible log entries, use callback etc.)
    // - Recovery after error/exception: record stack sizes with ErrorRecoveryStoreState(), disable assert, set log callback (to e.g. trigger high-level breakpoint), recover with ErrorRecoveryTryToRecoverState(), restore settings.
    bool        ConfigErrorRecovery;                // = true       // Enable error recovery support. Some errors won't be detected and lead to direct crashes if recovery is disabled.
    bool        ConfigErrorRecoveryEnableAssert;    // = true       // Enable asserts on recoverable error. By default call VAN_ASSERT() when returning from a failing VAN_ASSERT_USER_ERROR()
    bool        ConfigErrorRecoveryEnableDebugLog;  // = true       // Enable debug log output on recoverable errors.
    bool        ConfigErrorRecoveryEnableTooltip;   // = true       // Enable tooltip on recoverable errors. The tooltip include a way to enable asserts if they were disabled.

    // Option to enable various debug tools showing buttons that will call the VAN_DEBUG_BREAK() macro.
    // - The Item Picker tool will be available regardless of this being enabled, in order to maximize its discoverability.
    // - Requires a debugger being attached, otherwise VAN_DEBUG_BREAK() options will appear to crash your application.
    //   e.g. io.ConfigDebugIsDebuggerPresent = ::IsDebuggerPresent() on Win32, or refer to VanOsIsDebuggerPresent() vangui_test_engine/vangui_te_utils.cpp for a Unix compatible version.
    bool        ConfigDebugIsDebuggerPresent;   // = false          // Enable various tools calling VAN_DEBUG_BREAK().

    // Tools to detect code submitting items with conflicting/duplicate IDs
    // - Code should use PushID()/PopID() in loops, or append "##xx" to same-label identifiers.
    // - Empty label e.g. Button("") == same ID as parent widget/node. Use Button("##xx") instead!
    bool        ConfigDebugHighlightIdConflicts;// = true           // Highlight and show an error message popup when multiple items have conflicting identifiers.
    bool        ConfigDebugHighlightIdConflictsShowItemPicker;//=true // Show "Item Picker" button in aforementioned popup.

    // Tools to test correct Begin/End and BeginChild/EndChild behaviors.
    // - Presently Begin()/End() and BeginChild()/EndChild() needs to ALWAYS be called in tandem, regardless of return value of BeginXXX()
    // - This is inconsistent with other BeginXXX functions and create confusion for many users.
    // - We expect to update the API eventually. In the meanwhile we provide tools to facilitate checking user-code behavior.
    bool        ConfigDebugBeginReturnValueOnce;// = false          // First-time calls to Begin()/BeginChild() will return false. NEEDS TO BE SET AT APPLICATION BOOT TIME if you don't want to miss windows.
    bool        ConfigDebugBeginReturnValueLoop;// = false          // Some calls to Begin()/BeginChild() will return false. Will cycle through window depths then repeat. Suggested use: add "io.ConfigDebugBeginReturnValue = io.KeyShift" in your main loop then occasionally press SHIFT. Windows should be flickering while running.

    // Option to deactivate io.AddFocusEvent(false) handling.
    // - May facilitate interactions with a debugger when focus loss leads to clearing inputs data.
    // - Backends may have other side-effects on focus loss, so this will reduce side-effects but not necessary remove all of them.
    bool        ConfigDebugIgnoreFocusLoss;     // = false          // Ignore io.AddFocusEvent(false), consequently not calling io.ClearInputKeys()/io.ClearInputMouse() in input processing.

    // Option to audit .ini data
    bool        ConfigDebugIniSettings;         // = false          // Save .ini data with extra comments (particularly helpful for Docking, but makes saving slower)

    //------------------------------------------------------------------
    // Platform Identifiers
    // (the vangui_impl_xxxx backend files are setting those up for you)
    //------------------------------------------------------------------

    // Nowadays those would be stored in VanGuiPlatformIO but we are leaving them here for legacy reasons.
    // Optional: Platform/Renderer backend name (informational only! will be displayed in About Window) + User data for backend/wrappers to store their own stuff.
    const char* BackendPlatformName;            // = nullptr
    const char* BackendRendererName;            // = nullptr
    void*       BackendPlatformUserData;        // = nullptr           // User data for platform backend
    void*       BackendRendererUserData;        // = nullptr           // User data for renderer backend
    void*       BackendLanguageUserData;        // = nullptr           // User data for non C++ programming language backend

    //------------------------------------------------------------------
    // Input - Call before calling NewFrame()
    //------------------------------------------------------------------

    // Input Functions
    VANGUI_API void  AddKeyEvent(VanGuiKey key, bool down);                   // Queue a new key down/up event. Key should be "translated" (as in, generally VanGuiKey_A matches the key end-user would use to emit an 'A' character)
    VANGUI_API void  AddKeyAnalogEvent(VanGuiKey key, bool down, float v);    // Queue a new key down/up event for analog values (e.g. VanGuiKey_Gamepad_ values). Dead-zones should be handled by the backend.
    VANGUI_API void  AddMousePosEvent(float x, float y);                     // Queue a mouse position update. Use -FLT_MAX,-FLT_MAX to signify no mouse (e.g. app not focused and not hovered)
    VANGUI_API void  AddMouseButtonEvent(int button, bool down);             // Queue a mouse button change
    VANGUI_API void  AddMouseWheelEvent(float wheel_x, float wheel_y);       // Queue a mouse wheel update. wheel_y<0: scroll down, wheel_y>0: scroll up, wheel_x<0: scroll right, wheel_x>0: scroll left.
    VANGUI_API void  AddMouseSourceEvent(VanGuiMouseSource source);           // Queue a mouse source change (Mouse/TouchScreen/Pen)
    VANGUI_API void  AddFocusEvent(bool focused);                            // Queue a gain/loss of focus for the application (generally based on OS/platform focus of your window)
    VANGUI_API void  AddInputCharacter(unsigned int c);                      // Queue a new character input
    VANGUI_API void  AddInputCharacterUTF16(VanWchar16 c);                    // Queue a new character input from a UTF-16 character, it can be a surrogate
    VANGUI_API void  AddInputCharactersUTF8(const char* str);                // Queue a new characters input from a UTF-8 string

    VANGUI_API void  SetKeyEventNativeData(VanGuiKey key, int native_keycode, int native_scancode, int native_legacy_index = -1); // [Optional] Specify index for legacy <1.87 IsKeyXXX() functions with native indices + specify native keycode, scancode.
    VANGUI_API void  SetAppAcceptingEvents(bool accepting_events);           // Set master flag for accepting key/mouse/text events (default to true). Useful if you have native dialog boxes that are interrupting your application loop/refresh, and you want to disable events being queued while your app is frozen.
    VANGUI_API void  ClearEventsQueue();                                     // Clear all incoming events.
    VANGUI_API void  ClearInputKeys();                                       // Clear current keyboard/gamepad state + current frame text input buffer. Equivalent to releasing all keys/buttons.
    VANGUI_API void  ClearInputMouse();                                      // Clear current mouse state.

    //------------------------------------------------------------------
    // Output - Updated by NewFrame() or EndFrame()/Render()
    // (when reading from the io.WantCaptureMouse, io.WantCaptureKeyboard flags to dispatch your inputs, it is
    //  generally easier and more correct to use their state BEFORE calling NewFrame(). See FAQ for details!)
    //------------------------------------------------------------------

    bool        WantCaptureMouse;                   // Set when VanGUI will use mouse inputs, in this case do not dispatch them to your main game/application (either way, always pass on mouse inputs to vangui). (e.g. unclicked mouse is hovering over an vangui window, widget is active, mouse was clicked over an vangui window, etc.).
    bool        WantCaptureKeyboard;                // Set when VanGUI will use keyboard inputs, in this case do not dispatch them to your main game/application (either way, always pass keyboard inputs to vangui). (e.g. InputText active, or an vangui window is focused and navigation is enabled, etc.).
    bool        WantTextInput;                      // Mobile/console: when set, you may display an on-screen keyboard. This is set by VanGUI when it wants textual keyboard input to happen (e.g. when a InputText widget is active).
    bool        WantSetMousePos;                    // MousePos has been altered, backend should reposition mouse on next frame. Rarely used! Set only when io.ConfigNavMoveSetMousePos is enabled.
    bool        WantSaveIniSettings;                // When manual .ini load/save is active (io.IniFilename == nullptr), this will be set to notify your application that you can call SaveIniSettingsToMemory() and save yourself. Important: clear io.WantSaveIniSettings yourself after saving!
    bool        NavActive;                          // Keyboard/Gamepad navigation is currently allowed (will handle VanGuiKey_NavXXX events) = a window is focused and it doesn't use the VanGuiWindowFlags_NoNavInputs flag.
    bool        NavVisible;                         // Keyboard/Gamepad navigation highlight is visible and allowed (will handle VanGuiKey_NavXXX events).
    float       Framerate;                          // Estimate of application framerate (rolling average over 60 frames, based on io.DeltaTime), in frame per second. Solely for convenience. Slow applications may not want to use a moving average or may want to reset underlying buffers occasionally.
    int         MetricsRenderVertices;              // Vertices output during last call to Render()
    int         MetricsRenderIndices;               // Indices output during last call to Render() = number of triangles * 3
    int         MetricsRenderWindows;               // Number of visible windows
    int         MetricsActiveWindows;               // Number of active windows
    VanVec2      MouseDelta;                         // Mouse delta. Note that this is zero if either current or previous position are invalid (-FLT_MAX,-FLT_MAX), so a disappearing/reappearing mouse won't have a huge delta.

    //------------------------------------------------------------------
    // [Internal] VanGUI will maintain those fields. Forward compatibility not guaranteed!
    //------------------------------------------------------------------

    VanGuiContext* Ctx;                              // Parent UI context (needs to be set explicitly by parent).

    // Main Input State
    // (this block used to be written by backend, since 1.87 it is best to NOT write to those directly, call the AddXXX functions above instead)
    // (reading from those variables is fair game, as they are extremely unlikely to be moving anywhere)
    VanVec2      MousePos;                           // Mouse position, in pixels. Set to VanVec2(-FLT_MAX, -FLT_MAX) if mouse is unavailable (on another screen, etc.)
    bool        MouseDown[5];                       // Mouse buttons: 0=left, 1=right, 2=middle + extras (VanGuiMouseButton_COUNT == 5). VanGUI mostly uses left and right buttons. Other buttons allow us to track if the mouse is being used by your application + available to user as a convenience via IsMouse** API.
    float       MouseWheel;                         // Mouse wheel Vertical: 1 unit scrolls about 5 lines text. >0 scrolls Up, <0 scrolls Down. Hold Shift to turn vertical scroll into horizontal scroll.
    float       MouseWheelH;                        // Mouse wheel Horizontal. >0 scrolls Left, <0 scrolls Right. Most users don't have a mouse with a horizontal wheel, may not be filled by all backends.
    VanGuiMouseSource MouseSource;                   // Mouse actual input peripheral (Mouse/TouchScreen/Pen).
    bool        KeyCtrl;                            // Keyboard modifier down: Ctrl (non-macOS), Cmd (macOS)
    bool        KeyShift;                           // Keyboard modifier down: Shift
    bool        KeyAlt;                             // Keyboard modifier down: Alt
    bool        KeySuper;                           // Keyboard modifier down: Windows/Super (non-macOS), Ctrl (macOS)

    // Other state maintained from data above + IO function calls
    VanGuiKeyChord KeyMods;                          // Key mods flags (any of VanGuiMod_Ctrl/VanGuiMod_Shift/VanGuiMod_Alt/VanGuiMod_Super flags, same as io.KeyCtrl/KeyShift/KeyAlt/KeySuper but merged into flags). Read-only, updated by NewFrame()
    VanGuiKeyData  KeysData[VanGuiKey_NamedKey_COUNT];// Key state for all known keys. MUST use 'key - VanGuiKey_NamedKey_BEGIN' as index. Use IsKeyXXX() functions to access this.
    bool        WantCaptureMouseUnlessPopupClose;   // Alternative to WantCaptureMouse: (WantCaptureMouse == true && WantCaptureMouseUnlessPopupClose == false) when a click over void is expected to close a popup.
    VanVec2      MousePosPrev;                       // Previous mouse position (note that MouseDelta is not necessary == MousePos-MousePosPrev, in case either position is invalid)
    VanVec2      MouseClickedPos[5];                 // Position at time of clicking
    double      MouseClickedTime[5];                // Time of last click (used to figure out double-click)
    bool        MouseClicked[5];                    // Mouse button went from !Down to Down (same as MouseClickedCount[x] != 0)
    bool        MouseDoubleClicked[5];              // Has mouse button been double-clicked? (same as MouseClickedCount[x] == 2)
    VanU16       MouseClickedCount[5];               // == 0 (not clicked), == 1 (same as MouseClicked[]), == 2 (double-clicked), == 3 (triple-clicked) etc. when going from !Down to Down
    VanU16       MouseClickedLastCount[5];           // Count successive number of clicks. Stays valid after mouse release. Reset after another click is done.
    bool        MouseReleased[5];                   // Mouse button went from Down to !Down
    double      MouseReleasedTime[5];               // Time of last released (rarely used! but useful to handle delayed single-click when trying to disambiguate them from double-click).
    bool        MouseDownOwned[5];                  // Track if button was clicked inside a VanGUI window or over void blocked by a popup. We don't request mouse capture from the application if click started outside VanGui bounds.
    bool        MouseDownOwnedUnlessPopupClose[5];  // Track if button was clicked inside a VanGUI window.
    bool        MouseWheelRequestAxisSwap;          // On a non-Mac system, holding Shift requests WheelY to perform the equivalent of a WheelX event. On a Mac system this is already enforced by the system.
    bool        MouseCtrlLeftAsRightClick;          // (OSX) Set to true when the current click was a Ctrl+Click that spawned a simulated right click
    float       MouseDownDuration[5];               // Duration the mouse button has been down (0.0f == just clicked)
    float       MouseDownDurationPrev[5];           // Previous time the mouse button has been down
    float       MouseDragMaxDistanceSqr[5];         // Squared maximum distance of how much mouse has traveled from the clicking point (used for moving thresholds)
    float       PenPressure;                        // Touch/Pen pressure (0.0f to 1.0f, should be >0.0f only when MouseDown[0] == true). Helper storage currently unused by VanGUI.
    bool        AppFocusLost;                       // Only modify via AddFocusEvent()
    bool        AppAcceptingEvents;                 // Only modify via SetAppAcceptingEvents()
    VanWchar16   InputQueueSurrogate;                // For AddInputCharacterUTF16()
    VanVector<VanWchar> InputQueueCharacters;         // Queue of _characters_ input (obtained by platform backend). Fill using AddInputCharacter() helper.

    // Legacy: before 1.87, we required backend to fill io.KeyMap[] (vangui->native map) during initialization and io.KeysDown[] (native indices) every frame.
    // This is still temporarily supported as a legacy feature. However the new preferred scheme is for backend to call io.AddKeyEvent().
    //   Old (<1.87):  VanGui::IsKeyPressed(VanGui::GetIO().KeyMap[VanGuiKey_Space]) --> New (1.87+) VanGui::IsKeyPressed(VanGuiKey_Space)
    //   Old (<1.87):  VanGui::IsKeyPressed(MYPLATFORM_KEY_SPACE)                  --> New (1.87+) VanGui::IsKeyPressed(VanGuiKey_Space)
    //int       KeyMap[VanGuiKey_COUNT];             // [LEGACY] Input: map of indices into the KeysDown[512] entries array which represent your "native" keyboard state. The first 512 are now unused and should be kept zero. Legacy backend will write into KeyMap[] using VanGuiKey_ indices which are always >512.
    //bool      KeysDown[VanGuiKey_COUNT];           // [LEGACY] Input: Keyboard keys that are pressed (ideally left in the "native" order your engine has access to keyboard keys, so you can use your own defines/enums for keys). This used to be [512] sized. It is now VanGuiKey_COUNT to allow legacy io.KeysDown[GetKeyIndex(...)] to work without an overflow.
    //float     NavInputs[VanGuiNavInput_COUNT];     // [LEGACY] Since 1.88, NavInputs[] was removed. Backends from 1.60 to 1.86 won't build. Feed gamepad inputs via io.AddKeyEvent() and VanGuiKey_GamepadXXX enums.
    //void*     ImeWindowHandle;                    // [Obsoleted in 1.87] Set VanGuiViewport::PlatformHandleRaw instead. Set this to your HWND to get automatic IME cursor positioning.

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    float       FontGlobalScale;                    // Moved io.FontGlobalScale to style.FontScaleMain in 1.92 (June 2025)

    // Legacy: before 1.91.1, clipboard functions were stored in VanGuiIO instead of VanGuiPlatformIO.
    // As this is will affect all users of custom engines/backends, we are providing proper legacy redirection (will obsolete).
    const char* (*GetClipboardTextFn)(void* user_data);
    void        (*SetClipboardTextFn)(void* user_data, const char* text);
    void*       ClipboardUserData;

    //void ClearInputCharacters() { InputQueueCharacters.resize(0); } // [Obsoleted in 1.89.8] Clear the current frame text input buffer. Now included within ClearInputKeys(). Removed this as it is ambiguous/misleading and generally incorrect to use with the existence of a higher-level input queue.
#endif

    VANGUI_API   VanGuiIO();
};

//-----------------------------------------------------------------------------
// [SECTION] Misc data structures (VanGuiInputTextCallbackData, VanGuiSizeCallbackData, VanGuiPayload)
//-----------------------------------------------------------------------------

// Shared state of InputText(), passed as an argument to your callback when a VanGuiInputTextFlags_Callback* flag is used.
// The callback function should return 0 by default.
// Callbacks (follow a flag name and see comments in VanGuiInputTextFlags_ declarations for more details)
// - VanGuiInputTextFlags_CallbackEdit:        Callback on buffer edit. Note that InputText() already returns true on edit + you can always use IsItemEdited(). The callback is useful to manipulate the underlying buffer while focus is active.
// - VanGuiInputTextFlags_CallbackAlways:      Callback on each iteration
// - VanGuiInputTextFlags_CallbackCompletion:  Callback on pressing TAB
// - VanGuiInputTextFlags_CallbackHistory:     Callback on pressing Up/Down arrows
// - VanGuiInputTextFlags_CallbackCharFilter:  Callback on character inputs to replace or discard them. Modify 'EventChar' to replace or discard, or return 1 in callback to discard.
// - VanGuiInputTextFlags_CallbackResize:      Callback on buffer capacity changes request (beyond 'buf_size' parameter value), allowing the string to grow.
struct VanGuiInputTextCallbackData
{
    VanGuiContext*       Ctx;            // Parent UI context
    VanGuiInputTextFlags EventFlag;      // One VanGuiInputTextFlags_Callback*    // Read-only
    VanGuiInputTextFlags Flags;          // What user passed to InputText()      // Read-only
    void*               UserData;       // What user passed to InputText()      // Read-only
    VanGuiID             ID;             // Widget ID                            // Read-only

    // Arguments for the different callback events
    // - During Resize callback, Buf will be same as your input buffer.
    // - However, during Completion/History/Always callback, Buf always points to our own internal data (it is not the same as your buffer)! Changes to it will be reflected into your own buffer shortly after the callback.
    // - To modify the text buffer in a callback, prefer using the InsertChars() / DeleteChars() function. InsertChars() will take care of calling the resize callback if necessary.
    // - If you know your edits are not going to resize the underlying buffer allocation, you may modify the contents of 'Buf[]' directly. You need to update 'BufTextLen' accordingly (0 <= BufTextLen < BufSize) and set 'BufDirty'' to true so InputText can update its internal state.
    VanGuiKey            EventKey;       // Key pressed (Up/Down/TAB)            // Read-only    // [Completion,History]
    VanWchar             EventChar;      // Character input                      // Read-write   // [CharFilter] Replace character with another one, or set to zero to drop. return 1 is equivalent to setting EventChar=0;
    bool                EventActivated; // Input field just got activated       // Read-only    // [Always]
    bool                BufDirty;       // Set if you modify Buf/BufTextLen!    // Write        // [Completion,History,Always]
    char*               Buf;            // Text buffer                          // Read-write   // [Resize] Can replace pointer / [Completion,History,Always] Only write to pointed data, don't replace the actual pointer!
    int                 BufTextLen;     // Text length (in bytes)               // Read-write   // [Resize,Completion,History,Always] Exclude zero-terminator storage. In C land: == strlen(some_text), in C++ land: string.length()
    int                 BufSize;        // Buffer size (in bytes) = capacity+1  // Read-only    // [Resize,Completion,History,Always] Include zero-terminator storage. In C land: == ARRAYSIZE(my_char_array), in C++ land: string.capacity()+1
    int                 CursorPos;      //                                      // Read-write   // [Completion,History,Always,CharFilter]
    int                 SelectionStart; //                                      // Read-write   // [Completion,History,Always,CharFilter] == to SelectionEnd when no selection
    int                 SelectionEnd;   //                                      // Read-write   // [Completion,History,Always,CharFilter]

    // Helper functions for text manipulation.
    // Use those function to benefit from the CallbackResize behaviors. Calling those function reset the selection.
    VANGUI_API VanGuiInputTextCallbackData();
    VANGUI_API void      DeleteChars(int pos, int bytes_count);
    VANGUI_API void      InsertChars(int pos, const char* text, const char* text_end = nullptr);
    void                SelectAll()                 { SelectionStart = 0; CursorPos = SelectionEnd = BufTextLen; }
    void                SetSelection(int s, int e)  { VAN_ASSERT(s >= 0 && s <= BufTextLen); VAN_ASSERT(e >= 0 && e <= BufTextLen); SelectionStart = s; CursorPos = SelectionEnd = e; }
    void                ClearSelection()            { SelectionStart = SelectionEnd = BufTextLen; }
    bool                HasSelection() const        { return SelectionStart != SelectionEnd; }
};

// Resizing callback data to apply custom constraint. As enabled by SetNextWindowSizeConstraints(). Callback is called during the next Begin().
// NB: For basic min/max size constraint on each axis you don't need to use the callback! The SetNextWindowSizeConstraints() parameters are enough.
struct VanGuiSizeCallbackData
{
    void*   UserData;       // Read-only.   What user passed to SetNextWindowSizeConstraints(). Generally store an integer or float in here (need reinterpret_cast<>).
    VanVec2  Pos;            // Read-only.   Window position, for reference.
    VanVec2  CurrentSize;    // Read-only.   Current window size.
    VanVec2  DesiredSize;    // Read-write.  Desired size, based on user's mouse position. Write to this field to restrain resizing.
};

// Data payload for Drag and Drop operations: AcceptDragDropPayload(), GetDragDropPayload()
struct VanGuiPayload
{
    // Members
    void*           Data;               // Data (copied and owned by VanGUI)
    int             DataSize;           // Data size

    // [Internal]
    VanGuiID         SourceId;           // Source item id
    VanGuiID         SourceParentId;     // Source parent id (if available)
    int             DataFrameCount;     // Data timestamp
    char            DataType[32 + 1];   // Data type tag (short user-supplied string, 32 characters max)
    bool            Preview;            // Set when AcceptDragDropPayload() was called and mouse has been hovering the target item (nb: handle overlapping drag targets)
    bool            Delivery;           // Set when AcceptDragDropPayload() was called and mouse button is released over the target item.

    VanGuiPayload()  { Clear(); }
    void Clear()    { SourceId = SourceParentId = 0; Data = nullptr; DataSize = 0; memset(DataType, 0, sizeof(DataType)); DataFrameCount = -1; Preview = Delivery = false; }
    bool IsDataType(const char* type) const { return DataFrameCount != -1 && strcmp(type, DataType) == 0; }
    bool IsPreview() const                  { return Preview; }
    bool IsDelivery() const                 { return Delivery; }
};

//-----------------------------------------------------------------------------
// [SECTION] Helpers (VanGuiOnceUponAFrame, VanGuiTextFilter, VanGuiTextBuffer, VanGuiStorage, VanGuiListClipper, Math Operators, VanColor)
//-----------------------------------------------------------------------------

// Helper: Unicode defines
#define VAN_UNICODE_CODEPOINT_INVALID 0xFFFD     // Invalid Unicode code point (standard value).
#ifdef VANGUI_USE_WCHAR32
#define VAN_UNICODE_CODEPOINT_MAX     0x10FFFF   // Maximum Unicode code point supported by this build.
#else
#define VAN_UNICODE_CODEPOINT_MAX     0xFFFF     // Maximum Unicode code point supported by this build.
#endif

// Helper: Execute a block of code at maximum once a frame. Convenient if you want to quickly create a UI within deep-nested code that runs multiple times every frame.
// Usage: static VanGuiOnceUponAFrame oaf; if (oaf) VanGui::Text("This will be called only once per frame");
struct VanGuiOnceUponAFrame
{
    VanGuiOnceUponAFrame() { RefFrame = -1; }
    mutable int RefFrame;
    operator bool() const { int current_frame = VanGui::GetFrameCount(); if (RefFrame == current_frame) return false; RefFrame = current_frame; return true; }
};

// Helper: Parse and apply text filters. In format "aaaaa[,bbbb][,ccccc]"
struct VanGuiTextFilter
{
    VANGUI_API           VanGuiTextFilter(const char* default_filter = "");
    VANGUI_API bool      Draw(const char* label = "Filter (inc,-exc)", float width = 0.0f);  // Helper calling InputText+Build
    VANGUI_API bool      PassFilter(const char* text, const char* text_end = nullptr) const;
    VANGUI_API void      Build();
    void                Clear()          { InputBuf[0] = 0; Build(); }
    bool                IsActive() const { return !Filters.empty(); }

    // [Internal]
    struct VanGuiTextRange
    {
        const char*     b;
        const char*     e;

        VanGuiTextRange()                                { b = e = nullptr; }
        VanGuiTextRange(const char* _b, const char* _e)  { b = _b; e = _e; }
        bool            empty() const                   { return b == e; }
        VANGUI_API void  split(char separator, VanVector<VanGuiTextRange>* out) const;
    };
    char                    InputBuf[256];
    VanVector<VanGuiTextRange>Filters;
    int                     CountGrep;
};

// Helper: Growable text buffer for logging/accumulating text
// (this could be called 'VanGuiTextBuilder' / 'VanGuiStringBuilder')
struct VanGuiTextBuffer
{
    VanVector<char>      Buf;
    VANGUI_API static char EmptyString[1];

    VanGuiTextBuffer()   { }
    inline char         operator[](int i) const { VAN_ASSERT(Buf.Data != nullptr); return Buf.Data[i]; }
    const char*         begin() const           { return Buf.Data ? &Buf.front() : EmptyString; }
    const char*         end() const             { return Buf.Data ? &Buf.back() : EmptyString; } // Buf is zero-terminated, so end() will point on the zero-terminator
    int                 size() const            { return Buf.Size ? Buf.Size - 1 : 0; }
    bool                empty() const           { return Buf.Size <= 1; }
    void                clear()                 { Buf.clear(); }
    void                resize(int size)        { if (Buf.Size > size) Buf.Data[size] = 0; Buf.resize(size ? size + 1 : 0, 0); } // Similar to resize(0) on VanVector: empty string but don't free buffer.
    void                reserve(int capacity)   { Buf.reserve(capacity); }
    void                compact() noexcept      { if (Buf.Capacity > Buf.Size) { VanVector<char> tmp; tmp.resize(Buf.Size); if (Buf.Data && Buf.Size) memcpy(tmp.Data, Buf.Data, static_cast<size_t>(Buf.Size)); Buf.swap(tmp); } } // Trim excess capacity to match current size, reducing memory footprint of long-lived buffers.
    const char*         c_str() const           { return Buf.Data ? Buf.Data : EmptyString; }
    VANGUI_API void      append(const char* str, const char* str_end = nullptr);
    VANGUI_API void      appendf(const char* fmt, ...) VAN_FMTARGS(2);
    VANGUI_API void      appendfv(const char* fmt, va_list args) VAN_FMTLIST(2);
};

// [Internal] Key+Value for VanGuiStorage
struct VanGuiStoragePair
{
    VanGuiID     key;
    union       { int val_i; float val_f; void* val_p; };
    VanGuiStoragePair(VanGuiID _key, int _val)    { key = _key; val_i = _val; }
    VanGuiStoragePair(VanGuiID _key, float _val)  { key = _key; val_f = _val; }
    VanGuiStoragePair(VanGuiID _key, void* _val)  { key = _key; val_p = _val; }
};

// Helper: Key->Value storage
// Typically you don't have to worry about this since a storage is held within each Window.
// We use it to e.g. store collapse state for a tree (Int 0/1)
// This is optimized for efficient lookup (dichotomy into a contiguous buffer) and rare insertion (typically tied to user interactions aka max once a frame)
// You can use it as custom user storage for temporary values. Declare your own storage if, for example:
// - You want to manipulate the open/close state of a particular sub-tree in your interface (tree node uses Int 0/1 to store their state).
// - You want to store custom debug data easily without adding or editing structures in your code (probably not efficient, but convenient)
// Types are NOT stored, so it is up to you to make sure your Key don't collide with different types.
struct VanGuiStorage
{
    // [Internal]
    VanVector<VanGuiStoragePair>      Data;

    // - Get***() functions find pair, never add/allocate. Pairs are sorted so a query is O(log N)
    // - Set***() functions find pair, insertion on demand if missing.
    // - Sorted insertion is costly, paid once. A typical frame shouldn't need to insert any new pair.
    void                Clear() { Data.clear(); }
    VANGUI_API int       GetInt(VanGuiID key, int default_val = 0) const;
    VANGUI_API void      SetInt(VanGuiID key, int val);
    VANGUI_API bool      GetBool(VanGuiID key, bool default_val = false) const;
    VANGUI_API void      SetBool(VanGuiID key, bool val);
    VANGUI_API float     GetFloat(VanGuiID key, float default_val = 0.0f) const;
    VANGUI_API void      SetFloat(VanGuiID key, float val);
    VANGUI_API void*     GetVoidPtr(VanGuiID key) const; // default_val is nullptr
    VANGUI_API void      SetVoidPtr(VanGuiID key, void* val);

    // - Get***Ref() functions finds pair, insert on demand if missing, return pointer. Useful if you intend to do Get+Set.
    // - References are only valid until a new value is added to the storage. Calling a Set***() function or a Get***Ref() function invalidates the pointer.
    // - A typical use case where this is convenient for quick hacking (e.g. add storage during a live Edit&Continue session if you can't modify existing struct)
    //      float* pvar = VanGui::GetFloatRef(key); VanGui::SliderFloat("var", pvar, 0, 100.0f); some_var += *pvar;
    [[nodiscard]] VANGUI_API int*      GetIntRef(VanGuiID key, int default_val = 0);
    [[nodiscard]] VANGUI_API bool*     GetBoolRef(VanGuiID key, bool default_val = false);
    [[nodiscard]] VANGUI_API float*    GetFloatRef(VanGuiID key, float default_val = 0.0f);
    VANGUI_API void**    GetVoidPtrRef(VanGuiID key, void* default_val = nullptr);

    // Advanced: for quicker full rebuild of a storage (instead of an incremental one), you may add all your contents and then sort once.
    VANGUI_API void      BuildSortByKey();
    // Obsolete: use on your own storage if you know only integer are being stored (open/close all tree nodes)
    VANGUI_API void      SetAllInt(int val);

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    //typedef ::VanGuiStoragePair VanGuiStoragePair;  // 1.90.8: moved type outside struct
#endif
};

// Flags for VanGuiListClipper (currently not fully exposed in function calls: a future refactor will likely add this to VanGuiListClipper::Begin function equivalent)
enum VanGuiListClipperFlags_
{
    VanGuiListClipperFlags_None                  = 0,
    VanGuiListClipperFlags_NoSetTableRowCounters = 1 << 0,   // [Internal] Disabled modifying table row counters. Avoid assumption that 1 clipper item == 1 table row.
};

// Helper: Manually clip large list of items.
// If you have lots evenly spaced items and you have random access to the list, you can perform coarse
// clipping based on visibility to only submit items that are in view.
// The clipper calculates the range of visible items and advance the cursor to compensate for the non-visible items we have skipped.
// (VanGUI already clip items based on their bounds but: it needs to first layout the item to do so, and generally
//  fetching/submitting your own data incurs additional cost. Coarse clipping using VanGuiListClipper allows you to easily
//  scale using lists with tens of thousands of items without a problem)
// Usage:
//   VanGuiListClipper clipper;
//   clipper.Begin(1000);         // We have 1000 elements, evenly spaced.
//   while (clipper.Step())
//       for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
//           VanGui::Text("line number %d", i);
// Generally what happens is:
// - Clipper lets you process the first element (DisplayStart = 0, DisplayEnd = 1) regardless of it being visible or not.
// - User code submit that one element.
// - Clipper can measure the height of the first element
// - Clipper calculate the actual range of elements to display based on the current clipping rectangle, position the cursor before the first visible element.
// - User code submit visible elements.
// - The clipper also handles various subtleties related to keyboard/gamepad navigation, wrapping etc.
struct VanGuiListClipper
{
    int             DisplayStart;       // First item to display, updated by each call to Step()
    int             DisplayEnd;         // End of items to display (exclusive)
    int             UserIndex;          // Helper storage for user convenience/code. Optional, and otherwise unused if you don't use it.
    int             ItemsCount;         // [Internal] Number of items
    float           ItemsHeight;        // [Internal] Height of item after a first step and item submission can calculate it
    VanGuiListClipperFlags Flags;        // [Internal] Flags, currently not yet well exposed.
    double          StartPosY;          // [Internal] Cursor position at the time of Begin() or after table frozen rows are all processed
    double          StartSeekOffsetY;   // [Internal] Account for frozen rows in a table and initial loss of precision in very large windows.
    VanGuiContext*   Ctx;                // [Internal] Parent UI context
    void*           TempData;           // [Internal] Internal data

    // items_count: Use INT_MAX if you don't know how many items you have (in which case the cursor won't be advanced in the final step, and you can call SeekCursorForItem() manually if you need)
    // items_height: Use -1.0f to be calculated automatically on first step. Otherwise pass in the distance between your items, typically GetTextLineHeightWithSpacing() or GetFrameHeightWithSpacing().
    VANGUI_API VanGuiListClipper();
    VANGUI_API ~VanGuiListClipper();
    VANGUI_API void  Begin(int items_count, float items_height = -1.0f);
    VANGUI_API void  End();             // Automatically called on the last call of Step() that returns false.
    VANGUI_API bool  Step();            // Call until it returns false. The DisplayStart/DisplayEnd fields will be set and you can process/draw those items.

    // Call IncludeItemByIndex() or IncludeItemsByIndex() *BEFORE* first call to Step() if you need a range of items to not be clipped, regardless of their visibility.
    // (Due to alignment / padding of certain items it is possible that an extra item may be included on either end of the display range).
    inline void     IncludeItemByIndex(int item_index)                  { IncludeItemsByIndex(item_index, item_index + 1); }
    VANGUI_API void  IncludeItemsByIndex(int item_begin, int item_end);  // item_end is exclusive e.g. use (42, 42+1) to make item 42 never clipped.

    // Seek cursor toward given item. This is automatically called while stepping.
    // - The only reason to call this is: you can use VanGuiListClipper::Begin(INT_MAX) if you don't know item count ahead of time.
    // - In this case, after all steps are done, you'll want to call SeekCursorForItem(item_count).
    VANGUI_API void  SeekCursorForItem(int item_index);

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    //inline void IncludeRangeByIndices(int item_begin, int item_end)      { IncludeItemsByIndex(item_begin, item_end); } // [renamed in 1.89.9]
    //inline void ForceDisplayRangeByIndices(int item_begin, int item_end) { IncludeItemsByIndex(item_begin, item_end); } // [renamed in 1.89.6]
    //inline VanGuiListClipper(int items_count, float items_height = -1.0f) { memset((void*)this, 0, sizeof(*this)); ItemsCount = -1; Begin(items_count, items_height); } // [removed in 1.79]
#endif
};

// Helpers: VanVec2/VanVec4 operators
// - It is important that we are keeping those disabled by default so they don't leak in user space.
// - This is in order to allow user enabling implicit cast operators between VanVec2/VanVec4 and their own types (using VAN_VEC2_CLASS_EXTRA in vanconfig.h)
// - Add '#define VANGUI_DEFINE_MATH_OPERATORS' before including this file (or in vanconfig.h) to access courtesy maths operators for VanVec2 and VanVec4.
#ifdef VANGUI_DEFINE_MATH_OPERATORS
#define VANGUI_DEFINE_MATH_OPERATORS_IMPLEMENTED
VAN_MSVC_RUNTIME_CHECKS_OFF
// VanVec2 operators
inline VanVec2  operator*(const VanVec2& lhs, const float rhs)    { return VanVec2(lhs.x * rhs, lhs.y * rhs); }
inline VanVec2  operator*(const float lhs, const VanVec2& rhs)    { return VanVec2(lhs * rhs.x, lhs * rhs.y); }
inline VanVec2  operator/(const VanVec2& lhs, const float rhs)    { return VanVec2(lhs.x / rhs, lhs.y / rhs); }
inline VanVec2  operator+(const VanVec2& lhs, const VanVec2& rhs)  { return VanVec2(lhs.x + rhs.x, lhs.y + rhs.y); }
inline VanVec2  operator-(const VanVec2& lhs, const VanVec2& rhs)  { return VanVec2(lhs.x - rhs.x, lhs.y - rhs.y); }
inline VanVec2  operator*(const VanVec2& lhs, const VanVec2& rhs)  { return VanVec2(lhs.x * rhs.x, lhs.y * rhs.y); }
inline VanVec2  operator/(const VanVec2& lhs, const VanVec2& rhs)  { return VanVec2(lhs.x / rhs.x, lhs.y / rhs.y); }
inline VanVec2  operator+(const VanVec2& lhs)                     { return lhs; }
inline VanVec2  operator-(const VanVec2& lhs)                     { return VanVec2(-lhs.x, -lhs.y); }
inline VanVec2& operator*=(VanVec2& lhs, const float rhs)         { lhs.x *= rhs; lhs.y *= rhs; return lhs; }
inline VanVec2& operator/=(VanVec2& lhs, const float rhs)         { lhs.x /= rhs; lhs.y /= rhs; return lhs; }
inline VanVec2& operator+=(VanVec2& lhs, const VanVec2& rhs)       { lhs.x += rhs.x; lhs.y += rhs.y; return lhs; }
inline VanVec2& operator-=(VanVec2& lhs, const VanVec2& rhs)       { lhs.x -= rhs.x; lhs.y -= rhs.y; return lhs; }
inline VanVec2& operator*=(VanVec2& lhs, const VanVec2& rhs)       { lhs.x *= rhs.x; lhs.y *= rhs.y; return lhs; }
inline VanVec2& operator/=(VanVec2& lhs, const VanVec2& rhs)       { lhs.x /= rhs.x; lhs.y /= rhs.y; return lhs; }
inline bool    operator==(const VanVec2& lhs, const VanVec2& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
inline bool    operator!=(const VanVec2& lhs, const VanVec2& rhs) { return lhs.x != rhs.x || lhs.y != rhs.y; }
// VanVec4 operators
inline VanVec4  operator*(const VanVec4& lhs, const float rhs)    { return VanVec4(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs); }
inline VanVec4  operator*(const float lhs, const VanVec4& rhs)    { return VanVec4(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z, lhs * rhs.w); }
inline VanVec4  operator/(const VanVec4& lhs, const float rhs)    { return VanVec4(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs); }
inline VanVec4  operator+(const VanVec4& lhs, const VanVec4& rhs)  { return VanVec4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w); }
inline VanVec4  operator-(const VanVec4& lhs, const VanVec4& rhs)  { return VanVec4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w); }
inline VanVec4  operator*(const VanVec4& lhs, const VanVec4& rhs)  { return VanVec4(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w); }
inline VanVec4  operator/(const VanVec4& lhs, const VanVec4& rhs)  { return VanVec4(lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z, lhs.w / rhs.w); }
inline VanVec4  operator+(const VanVec4& lhs)                     { return lhs; }
inline VanVec4  operator-(const VanVec4& lhs)                     { return VanVec4(-lhs.x, -lhs.y, -lhs.z, -lhs.w); }
inline VanVec4& operator*=(VanVec4& lhs, const float rhs)         { lhs.x *= rhs; lhs.y *= rhs; lhs.z *= rhs; lhs.w *= rhs; return lhs; }
inline VanVec4& operator/=(VanVec4& lhs, const float rhs)         { lhs.x /= rhs; lhs.y /= rhs; lhs.z /= rhs; lhs.w /= rhs; return lhs; }
inline VanVec4& operator+=(VanVec4& lhs, const VanVec4& rhs)       { lhs.x += rhs.x; lhs.y += rhs.y; lhs.z += rhs.z; lhs.w += rhs.w; return lhs; }
inline VanVec4& operator-=(VanVec4& lhs, const VanVec4& rhs)       { lhs.x -= rhs.x; lhs.y -= rhs.y; lhs.z -= rhs.z; lhs.w -= rhs.w; return lhs; }
inline VanVec4& operator*=(VanVec4& lhs, const VanVec4& rhs)       { lhs.x *= rhs.x; lhs.y *= rhs.y; lhs.z *= rhs.z; lhs.w *= rhs.w; return lhs; }
inline VanVec4& operator/=(VanVec4& lhs, const VanVec4& rhs)       { lhs.x /= rhs.x; lhs.y /= rhs.y; lhs.z /= rhs.z; lhs.w /= rhs.w; return lhs; }
inline bool    operator==(const VanVec4& lhs, const VanVec4& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w; }
inline bool    operator!=(const VanVec4& lhs, const VanVec4& rhs) { return lhs.x != rhs.x || lhs.y != rhs.y || lhs.z != rhs.z || lhs.w != rhs.w; }
VAN_MSVC_RUNTIME_CHECKS_RESTORE
#endif

// Helpers macros to generate 32-bit encoded colors
// - User can declare their own format by #defining the 5 _SHIFT/_MASK macros in their vanconfig file.
// - Any setting other than the default will need custom backend support. The only standard backend that supports anything else than the default is DirectX9.
#ifndef VAN_COL32_R_SHIFT
#ifdef VANGUI_USE_BGRA_PACKED_COLOR
#define VAN_COL32_R_SHIFT    16
#define VAN_COL32_G_SHIFT    8
#define VAN_COL32_B_SHIFT    0
#define VAN_COL32_A_SHIFT    24
#define VAN_COL32_A_MASK     0xFF000000
#else
#define VAN_COL32_R_SHIFT    0
#define VAN_COL32_G_SHIFT    8
#define VAN_COL32_B_SHIFT    16
#define VAN_COL32_A_SHIFT    24
#define VAN_COL32_A_MASK     0xFF000000
#endif
#endif
#define VAN_COL32(R,G,B,A)    (((VanU32)(A)<<VAN_COL32_A_SHIFT) | ((VanU32)(B)<<VAN_COL32_B_SHIFT) | ((VanU32)(G)<<VAN_COL32_G_SHIFT) | ((VanU32)(R)<<VAN_COL32_R_SHIFT))
#define VAN_COL32_WHITE       VAN_COL32(255,255,255,255)  // Opaque white = 0xFFFFFFFF
#define VAN_COL32_BLACK       VAN_COL32(0,0,0,255)        // Opaque black
#define VAN_COL32_BLACK_TRANS VAN_COL32(0,0,0,0)          // Transparent black = 0x00000000

// Helper: VanColor() implicitly converts colors to either VanU32 (packed 4x1 byte) or VanVec4 (4x1 float)
// Prefer using VAN_COL32() macros if you want a guaranteed compile-time VanU32 for usage with VanDrawList API.
// **Avoid storing VanColor! Store either u32 of VanVec4. This is not a full-featured color class. MAY OBSOLETE.
// **None of the VanGui API are using VanColor directly but you can use it as a convenience to pass colors in either VanU32 or VanVec4 formats. Explicitly cast to VanU32 or VanVec4 if needed.
struct VanColor
{
    VanVec4          Value;

    constexpr VanColor()                                             { }
    constexpr VanColor(float r, float g, float b, float a = 1.0f)    : Value(r, g, b, a) { }
    constexpr VanColor(const VanVec4& col)                            : Value(col) {}
    constexpr VanColor(int r, int g, int b, int a = 255)             : Value(static_cast<float>(r) * (1.0f / 255.0f), static_cast<float>(g) * (1.0f / 255.0f), static_cast<float>(b) * (1.0f / 255.0f), static_cast<float>(a) * (1.0f / 255.0f)) {}
    constexpr VanColor(VanU32 rgba)                                   : Value(static_cast<float>((rgba >> VAN_COL32_R_SHIFT) & 0xFF) * (1.0f / 255.0f), static_cast<float>((rgba >> VAN_COL32_G_SHIFT) & 0xFF) * (1.0f / 255.0f), static_cast<float>((rgba >> VAN_COL32_B_SHIFT) & 0xFF) * (1.0f / 255.0f), static_cast<float>((rgba >> VAN_COL32_A_SHIFT) & 0xFF) * (1.0f / 255.0f)) {}
    inline operator VanU32() const                                   { return VanGui::ColorConvertFloat4ToU32(Value); }
    inline operator VanVec4() const                                  { return Value; }

    // FIXME-OBSOLETE: May need to obsolete/cleanup those helpers.
    inline void    SetHSV(float h, float s, float v, float a = 1.0f){ VanGui::ColorConvertHSVtoRGB(h, s, v, Value.x, Value.y, Value.z); Value.w = a; }
    static VanColor HSV(float h, float s, float v, float a = 1.0f)   { float r, g, b; VanGui::ColorConvertHSVtoRGB(h, s, v, r, g, b); return VanColor(r, g, b, a); }
};

//-----------------------------------------------------------------------------
// [SECTION] Multi-Select API flags and structures (VanGuiMultiSelectFlags, VanGuiSelectionRequestType, VanGuiSelectionRequest, VanGuiMultiSelectIO, VanGuiSelectionBasicStorage)
//-----------------------------------------------------------------------------

// Multi-selection system
// - Refer to 'Demo->Widgets->Selection State & Multi-Select' for demos using this.
// - This system implements standard multi-selection idioms (Ctrl+Mouse/Keyboard, Shift+Mouse/Keyboard, etc)
//   with support for clipper (skipping non-visible items), box-select and many other details.
// - Selectable(), Checkbox() are supported but custom widgets may use it as well.
// - TreeNode() is technically supported but... using this correctly is more complicated: you need some sort of linear/random access to your tree,
//   which is suited to advanced trees setups also implementing filters and clipper. We will work toward simplifying and demoing it.
// - In the spirit of VanGUI design, your code owns actual selection data.
//   This is designed to allow all kinds of selection storage you may use in your application e.g. set/map/hash.
// About VanGuiSelectionBasicStorage:
// - This is an optional helper to store a selection state and apply selection requests.
// - It is used by our demos and provided as a convenience to quickly implement multi-selection.
// Usage:
// - Identify submitted items with SetNextItemSelectionUserData(), most likely using an index into your current data-set.
// - Store and maintain actual selection data using persistent object identifiers.
// - Usage flow:
//     BEGIN - (1) Call BeginMultiSelect() and retrieve the VanGuiMultiSelectIO* result.
//           - (2) Honor request list (SetAll/SetRange requests) by updating your selection data. Same code as Step 6.
//           - (3) [If using clipper] You need to make sure RangeSrcItem is always submitted. Calculate its index and pass to clipper.IncludeItemByIndex(). If storing indices in VanGuiSelectionUserData, a simple clipper.IncludeItemByIndex(ms_io->RangeSrcItem) call will work.
//     LOOP  - (4) Submit your items with SetNextItemSelectionUserData() + Selectable()/TreeNode() calls.
//     END   - (5) Call EndMultiSelect() and retrieve the VanGuiMultiSelectIO* result.
//           - (6) Honor request list (SetAll/SetRange requests) by updating your selection data. Same code as Step 2.
//     If you submit all items (no clipper), Step 2 and 3 are optional and will be handled by each item themselves. It is fine to always honor those steps.
// About VanGuiSelectionUserData:
// - This can store an application-defined identifier (e.g. index or pointer) submitted via SetNextItemSelectionUserData().
// - In return we store them into RangeSrcItem/RangeFirstItem/RangeLastItem and other fields in VanGuiMultiSelectIO.
// - Most applications will store an object INDEX, hence the chosen name and type. Storing an index is natural, because
//   SetRange requests will give you two end-points and you will need to iterate/interpolate between them to update your selection.
// - However it is perfectly possible to store a POINTER or another IDENTIFIER inside VanGuiSelectionUserData.
//   Our system never assume that you identify items by indices, it never attempts to interpolate between two values.
// - If you enable VanGuiMultiSelectFlags_NoRangeSelect then it is guaranteed that you will never have to interpolate
//   between two VanGuiSelectionUserData, which may be a convenient way to use part of the feature with less code work.
// - As most users will want to store an index, for convenience and to reduce confusion we use VanS64 instead of void*,
//   being syntactically easier to downcast. Feel free to reinterpret_cast and store a pointer inside.

// Flags for BeginMultiSelect()
enum VanGuiMultiSelectFlags_
{
    VanGuiMultiSelectFlags_None                  = 0,
    VanGuiMultiSelectFlags_SingleSelect          = 1 << 0,   // Disable selecting more than one item. This is available to allow single-selection code to share same code/logic if desired. It essentially disables the main purpose of BeginMultiSelect() tho!
    VanGuiMultiSelectFlags_NoSelectAll           = 1 << 1,   // Disable Ctrl+A shortcut to select all.
    VanGuiMultiSelectFlags_NoRangeSelect         = 1 << 2,   // Disable Shift+selection mouse/keyboard support (useful for unordered 2D selection). With BoxSelect is also ensure contiguous SetRange requests are not combined into one. This allows not handling interpolation in SetRange requests.
    VanGuiMultiSelectFlags_NoAutoSelect          = 1 << 3,   // Disable selecting items when navigating (useful for e.g. supporting range-select in a list of checkboxes).
    VanGuiMultiSelectFlags_NoAutoClear           = 1 << 4,   // Disable clearing selection when navigating or selecting another one (generally used with VanGuiMultiSelectFlags_NoAutoSelect. useful for e.g. supporting range-select in a list of checkboxes).
    VanGuiMultiSelectFlags_NoAutoClearOnReselect = 1 << 5,   // Disable clearing selection when clicking/selecting an already selected item.
    VanGuiMultiSelectFlags_BoxSelect1d           = 1 << 6,   // Enable box-selection with same width and same x pos items (e.g. full row Selectable()). Box-selection works better with little bit of spacing between items hit-box in order to be able to aim at empty space.
    VanGuiMultiSelectFlags_BoxSelect2d           = 1 << 7,   // Enable box-selection with varying width or varying x pos items support (e.g. different width labels, or 2D layout/grid). This is slower: alters clipping logic so that e.g. horizontal movements will update selection of normally clipped items.
    VanGuiMultiSelectFlags_BoxSelectNoScroll     = 1 << 8,   // Disable scrolling when box-selecting and moving mouse near edges of scope.
    VanGuiMultiSelectFlags_ClearOnEscape         = 1 << 9,   // Clear selection when pressing Escape while scope is focused.
    VanGuiMultiSelectFlags_ClearOnClickVoid      = 1 << 10,  // Clear selection when clicking on empty location within scope.
    VanGuiMultiSelectFlags_ScopeWindow           = 1 << 11,  // Scope for _BoxSelect and _ClearOnClickVoid is whole window (Default). Use if BeginMultiSelect() covers a whole window or used a single time in same window.
    VanGuiMultiSelectFlags_ScopeRect             = 1 << 12,  // Scope for _BoxSelect and _ClearOnClickVoid is rectangle encompassing BeginMultiSelect()/EndMultiSelect(). Use if BeginMultiSelect() is called multiple times in same window.
    VanGuiMultiSelectFlags_SelectOnAuto          = 1 << 13,  // Apply selection on mouse down when clicking on unselected item, on mouse up when clicking on selected item. (Default)
    VanGuiMultiSelectFlags_SelectOnClickAlways   = 1 << 14,  // Apply selection on mouse down when clicking on any items. Prevents Drag and Drop from being used on multiple-selection, but allows e.g. BoxSelect to always reselect even when clicking inside an existing selection. (Excel style behavior)
    VanGuiMultiSelectFlags_SelectOnClickRelease  = 1 << 15,  // Apply selection on mouse release when clicking an unselected item. Allow dragging an unselected item without altering selection.
    //VanGuiMultiSelectFlags_RangeSelect2d       = 1 << 15,  // Shift+Selection uses 2d geometry instead of linear sequence, so possible to use Shift+up/down to select vertically in grid. Analogous to what BoxSelect does.
    VanGuiMultiSelectFlags_NavWrapX              = 1 << 16,  // [Temporary] Enable navigation wrapping on X axis. Provided as a convenience because we don't have a design for the general Nav API for this yet. When the more general feature be public we may obsolete this flag in favor of new one.
    VanGuiMultiSelectFlags_NoSelectOnRightClick  = 1 << 17,  // Disable default right-click processing, which selects item on mouse down, and is designed for context-menus.
    VanGuiMultiSelectFlags_SelectOnMask_         = VanGuiMultiSelectFlags_SelectOnAuto | VanGuiMultiSelectFlags_SelectOnClickAlways | VanGuiMultiSelectFlags_SelectOnClickRelease,

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiMultiSelectFlags_SelectOnClick         = VanGuiMultiSelectFlags_SelectOnAuto,         // RENAMED in 1.92.6
#endif
};

// Main IO structure returned by BeginMultiSelect()/EndMultiSelect().
// This mainly contains a list of selection requests.
// - Use 'Demo->Tools->Debug Log->Selection' to see requests as they happen.
// - Some fields are only useful if your list is dynamic and allows deletion (getting post-deletion focus/state right is shown in the demo)
// - Below: who reads/writes each fields? 'r'=read, 'w'=write, 'ms'=multi-select code, 'app'=application/user code.
struct VanGuiMultiSelectIO
{
    //------------------------------------------// BeginMultiSelect / EndMultiSelect
    VanVector<VanGuiSelectionRequest> Requests;   //  ms:w, app:r     /  ms:w  app:r   // Requests to apply to your selection data.
    VanGuiSelectionUserData      RangeSrcItem;   //  ms:w  app:r     /                // (If using clipper) Begin: Source item (often the first selected item) must never be clipped: use clipper.IncludeItemByIndex() to ensure it is submitted.
    VanGuiSelectionUserData      NavIdItem;      //  ms:w, app:r     /                // (If using deletion) Last known SetNextItemSelectionUserData() value for NavId (if part of submitted items).
    bool                        NavIdSelected;  //  ms:w, app:r     /        app:r   // (If using deletion) Last known selection state for NavId (if part of submitted items).
    bool                        RangeSrcReset;  //        app:w     /  ms:r          // (If using deletion) Set before EndMultiSelect() to reset ResetSrcItem (e.g. if deleted selection).
    int                         ItemsCount;     //  ms:w, app:r     /        app:r   // 'int items_count' parameter to BeginMultiSelect() is copied here for convenience, allowing simpler calls to your ApplyRequests handler. Not used internally.
};

// Selection request type
enum VanGuiSelectionRequestType
{
    VanGuiSelectionRequestType_None = 0,
    VanGuiSelectionRequestType_SetAll,           // Request app to clear selection (if Selected==false) or select all items (if Selected==true). We cannot set RangeFirstItem/RangeLastItem as its contents is entirely up to user (not necessarily an index)
    VanGuiSelectionRequestType_SetRange,         // Request app to select/unselect [RangeFirstItem..RangeLastItem] items (inclusive) based on value of Selected. Only EndMultiSelect() request this, app code can read after BeginMultiSelect() and it will always be false.
};

// Selection request item
struct VanGuiSelectionRequest
{
    //------------------------------------------// BeginMultiSelect / EndMultiSelect
    VanGuiSelectionRequestType   Type;           //  ms:w, app:r     /  ms:w, app:r   // Request type. You'll most often receive 1 Clear + 1 SetRange with a single-item range.
    bool                        Selected;       //  ms:w, app:r     /  ms:w, app:r   // Parameter for SetAll/SetRange requests (true = select, false = unselect)
    VanS8                        RangeDirection; //                  /  ms:w  app:r   // Parameter for SetRange request: +1 when RangeFirstItem comes before RangeLastItem, -1 otherwise. Useful if you want to preserve selection order on a backward Shift+Click.
    VanGuiSelectionUserData      RangeFirstItem; //                  /  ms:w, app:r   // Parameter for SetRange request (this is generally == RangeSrcItem when shift selecting from top to bottom).
    VanGuiSelectionUserData      RangeLastItem;  //                  /  ms:w, app:r   // Parameter for SetRange request (this is generally == RangeSrcItem when shift selecting from bottom to top). Inclusive!
};

// Optional helper to store multi-selection state + apply multi-selection requests.
// - Used by our demos and provided as a convenience to easily implement basic multi-selection.
// - Iterate selection with 'void* it = nullptr; VanGuiID id; while (selection.GetNextSelectedItem(&it, &id)) { ... }'
//   Or you can check 'if (Contains(id)) { ... }' for each possible object if their number is not too high to iterate.
// - USING THIS IS NOT MANDATORY. This is only a helper and not a required API.
// To store a multi-selection, in your application you could:
// - Use this helper as a convenience. We use our simple key->value VanGuiStorage as a std::set<VanGuiID> replacement.
// - Use your own external storage: e.g. std::set<MyObjectId>, std::vector<MyObjectId>, interval trees, intrusively stored selection etc.
// In VanGuiSelectionBasicStorage we:
// - always use indices in the multi-selection API (passed to SetNextItemSelectionUserData(), retrieved in VanGuiMultiSelectIO)
// - use the AdapterIndexToStorageId() indirection layer to abstract how persistent selection data is derived from an index.
// - use decently optimized logic to allow queries and insertion of very large selection sets.
// - do not preserve selection order.
// Many combinations are possible depending on how you prefer to store your items and how you prefer to store your selection.
// Large applications are likely to eventually want to get rid of this indirection layer and do their own thing.
struct VanGuiSelectionBasicStorage
{
    // Members
    int             Size;           //          // Number of selected items, maintained by this helper.
    bool            PreserveOrder;  // = false  // GetNextSelectedItem() will return ordered selection (currently implemented by two additional sorts of selection. Could be improved)
    void*           UserData;       // = nullptr   // User data for use by adapter function        // e.g. selection.UserData = (void*)my_items;
    VanGuiID         (*AdapterIndexToStorageId)(VanGuiSelectionBasicStorage* self, int idx);      // e.g. selection.AdapterIndexToStorageId = [](VanGuiSelectionBasicStorage* self, int idx) { return ((MyItems**)self->UserData)[idx]->ID; };
    int             _SelectionOrder;// [Internal] Increasing counter to store selection order
    VanGuiStorage    _Storage;       // [Internal] Selection set. Think of this as similar to e.g. std::set<VanGuiID>. Prefer not accessing directly: iterate with GetNextSelectedItem().

    // Methods
    VANGUI_API VanGuiSelectionBasicStorage();
    VANGUI_API void  ApplyRequests(VanGuiMultiSelectIO* ms_io);   // Apply selection requests coming from BeginMultiSelect() and EndMultiSelect() functions. It uses 'items_count' passed to BeginMultiSelect()
    VANGUI_API bool  Contains(VanGuiID id) const;                 // Query if an item id is in selection.
    VANGUI_API void  Clear();                                    // Clear selection
    VANGUI_API void  Swap(VanGuiSelectionBasicStorage& r);        // Swap two selections
    VANGUI_API void  SetItemSelected(VanGuiID id, bool selected); // Add/remove an item from selection (generally done by ApplyRequests() function)
    VANGUI_API bool  GetNextSelectedItem(void** opaque_it, VanGuiID* out_id); // Iterate selection with 'void* it = nullptr; VanGuiID id; while (selection.GetNextSelectedItem(&it, &id)) { ... }'
    inline VanGuiID  GetStorageIdFromIndex(int idx)              { return AdapterIndexToStorageId(this, idx); }  // Convert index to item id based on provided adapter.
};

// Optional helper to apply multi-selection requests to existing randomly accessible storage.
// Convenient if you want to quickly wire multi-select API on e.g. an array of bool or items storing their own selection state.
struct VanGuiSelectionExternalStorage
{
    // Members
    void*           UserData;       // User data for use by adapter function                                // e.g. selection.UserData = (void*)my_items;
    void            (*AdapterSetItemSelected)(VanGuiSelectionExternalStorage* self, int idx, bool selected); // e.g. AdapterSetItemSelected = [](VanGuiSelectionExternalStorage* self, int idx, bool selected) { ((MyItems**)self->UserData)[idx]->Selected = selected; }

    // Methods
    VANGUI_API VanGuiSelectionExternalStorage();
    VANGUI_API void  ApplyRequests(VanGuiMultiSelectIO* ms_io);   // Apply selection requests by using AdapterSetItemSelected() calls
};

//-----------------------------------------------------------------------------
// [SECTION] Drawing API (VanDrawCmd, VanDrawIdx, VanDrawVert, VanDrawChannel, VanDrawListSplitter, VanDrawListFlags, VanDrawList, VanDrawData)
// Hold a series of drawing commands. The user provides a renderer for VanDrawData which essentially contains an array of VanDrawList.
//-----------------------------------------------------------------------------

// The maximum line width to bake anti-aliased textures for. Build atlas with VanFontAtlasFlags_NoBakedLines to disable baking.
#ifndef VAN_DRAWLIST_TEX_LINES_WIDTH_MAX
#define VAN_DRAWLIST_TEX_LINES_WIDTH_MAX     (32)
#endif

// VanDrawIdx: vertex index. [Compile-time configurable type]
// - To use 16-bit indices + allow large meshes: backend need to set 'io.BackendFlags |= VanGuiBackendFlags_RendererHasVtxOffset' and handle VanDrawCmd::VtxOffset (recommended).
// - To use 32-bit indices: override with '#define VanDrawIdx unsigned int' in your vanconfig.h file.
#ifndef VanDrawIdx
using VanDrawIdx = unsigned short;   // Default: 16-bit (for maximum compatibility with renderer backends)
#endif

// VanDrawCallback: Draw callbacks for advanced uses [configurable type: override in vanconfig.h]
// NB: You most likely do NOT need to use draw callbacks just to create your own widget or customized UI rendering,
// you can poke into the draw list for that! Draw callback may be useful for example to:
//  A) Change your GPU render state,
//  B) render a complex 3D scene inside a UI element without an intermediate texture/render target, etc.
// The expected behavior from your rendering function is 'if (cmd.UserCallback != nullptr) { cmd.UserCallback(parent_list, cmd); } else { RenderTriangles() }'
// If you want to override the signature of VanDrawCallback, you can simply use e.g. '#define VanDrawCallback MyDrawCallback' (in vanconfig.h) + update rendering backend accordingly.
#ifndef VanDrawCallback
using VanDrawCallback = void (*)(const VanDrawList* parent_list, const VanDrawCmd* cmd);
#endif

// Typically, 1 command = 1 GPU draw call (unless command is a callback)
// - VtxOffset: When 'io.BackendFlags & VanGuiBackendFlags_RendererHasVtxOffset' is enabled,
//   this fields allow us to render meshes larger than 64K vertices while keeping 16-bit indices.
//   Backends made for <1.71. will typically ignore the VtxOffset fields.
// - The ClipRect/TexRef/VtxOffset fields must be contiguous as we memcmp() them together (this is asserted for).
struct VanDrawCmd
{
    VanVec4          ClipRect;           // 4*4  // Clipping rectangle (x1, y1, x2, y2). Subtract VanDrawData->DisplayPos to get clipping rectangle in "viewport" coordinates
    VanTextureRef    TexRef;             // 16   // Reference to a font/texture atlas (where backend called VanTextureData::SetTexID()) or to a user-provided texture ID (via e.g. VanGui::Image() calls). Both will lead to a VanTextureID value.
    unsigned int    VtxOffset;          // 4    // Start offset in vertex buffer. VanGuiBackendFlags_RendererHasVtxOffset: always 0, otherwise may be >0 to support meshes larger than 64K vertices with 16-bit indices.
    unsigned int    IdxOffset;          // 4    // Start offset in index buffer.
    unsigned int    ElemCount;          // 4    // Number of indices (multiple of 3) to be rendered as triangles. Vertices are stored in the callee VanDrawList's vtx_buffer[] array, indices in idx_buffer[].
    VanDrawCallback  UserCallback;       // 4-8  // If != nullptr, call the function instead of rendering the vertices. clip_rect and texture_id will be set normally.
    void*           UserCallbackData;   // 4-8  // Callback user data (when UserCallback != nullptr). If called AddCallback() with size == 0, this is a copy of the AddCallback() argument. If called AddCallback() with size > 0, this is pointing to a buffer where data is stored.
    int             UserCallbackDataSize;  // 4 // Size of callback user data when using storage, otherwise 0.
    int             UserCallbackDataOffset;// 4 // [Internal] Offset of callback user data when using storage, otherwise -1.

    VanDrawCmd()     { memset((void*)this, 0, sizeof(*this)); } // POD-safe zero-init; also ensures padding fields are zeroed

    // Since 1.83: returns VanTextureID associated with this draw call. Warning: DO NOT assume this is always same as 'TextureId' (we will change this function for an upcoming feature)
    // Since 1.92: removed VanDrawCmd::TextureId field, the getter function must be used!
    inline VanTextureID GetTexID() const;    // == (TexRef._TexData ? TexRef._TexData->TexID : TexRef._TexID)
};

// Vertex layout
#ifndef VANGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT
struct VanDrawVert
{
    VanVec2  pos;
    VanVec2  uv;
    VanU32   col;
};
static_assert(sizeof(VanDrawVert) == 20, "VanDrawVert size changed — check GPU vertex layout");
static_assert(alignof(VanDrawVert) == 4, "VanDrawVert alignment unexpected");
#else
// You can override the vertex format layout by defining VANGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT in vanconfig.h
// The code expect VanVec2 pos (8 bytes), VanVec2 uv (8 bytes), VanU32 col (4 bytes), but you can re-order them or add other fields as needed to simplify integration in your engine.
// The type has to be described within the macro (you can either declare the struct or use a typedef). This is because VanVec2/VanU32 are likely not declared at the time you'd want to set your type up.
// NOTE: VANGUI DOESN'T CLEAR THE STRUCTURE AND DOESN'T CALL A CONSTRUCTOR SO ANY CUSTOM FIELD WILL BE UNINITIALIZED. IF YOU ADD EXTRA FIELDS (SUCH AS A 'Z' COORDINATES) YOU WILL NEED TO CLEAR THEM DURING RENDER OR TO IGNORE THEM.
VANGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT;
#endif

// [Internal] For use by VanDrawList
struct VanDrawCmdHeader
{
    VanVec4          ClipRect;
    VanTextureRef    TexRef;
    unsigned int    VtxOffset;
};

// [Internal] For use by VanDrawListSplitter
struct VanDrawChannel
{
    VanVector<VanDrawCmd>         _CmdBuffer;
    VanVector<VanDrawIdx>         _IdxBuffer;
};

// Split/Merge functions are used to split the draw list into different layers which can be drawn into out of order.
// This is used by the Columns/Tables API, so items of each column can be batched together in a same draw call.
struct VanDrawListSplitter
{
    int                         _Current;    // Current channel number (0)
    int                         _Count;      // Number of active channels (1+)
    VanVector<VanDrawChannel>     _Channels;   // Draw channels (not resized down so _Count might be < Channels.Size)

    inline VanDrawListSplitter()  { memset((void*)this, 0, sizeof(*this)); } // VanVector<VanDrawChannel> member is zero-init-safe (Data=nullptr, Size=0, Capacity=0)
    inline ~VanDrawListSplitter() { ClearFreeMemory(); }
    inline void                 Clear() { _Current = 0; _Count = 1; } // Do not clear Channels[] so our allocations are reused next frame
    VANGUI_API void              ClearFreeMemory();
    VANGUI_API void              Split(VanDrawList* draw_list, int count);
    VANGUI_API void              Merge(VanDrawList* draw_list);
    VANGUI_API void              SetCurrentChannel(VanDrawList* draw_list, int channel_idx);
};

// Flags for VanDrawList functions
enum VanDrawFlags_
{
    VanDrawFlags_None                        = 0,

    // Rounding for AddRect(), AddRectFilled(), PathRect()
    // - When not specified, we defaults to VanDrawFlags_RoundCornersAll! So you only need to use those flags if you want another configuration.
    VanDrawFlags_RoundCornersTopLeft         = 1 << 4, // Round top-left corner only (when rounding > 0.0f, we default to all corners).
    VanDrawFlags_RoundCornersTopRight        = 1 << 5, // Round top-right corner only (when rounding > 0.0f, we default to all corners).
    VanDrawFlags_RoundCornersBottomLeft      = 1 << 6, // Round bottom-left corner only (when rounding > 0.0f, we default to all corners).
    VanDrawFlags_RoundCornersBottomRight     = 1 << 7, // Round bottom-right corner only (when rounding > 0.0f, we default to all corners).
    VanDrawFlags_RoundCornersNone            = 1 << 8, // Disable rounding even if `float rounding > 0.0f`. This is NOT zero, NOT an implicit flag!
    VanDrawFlags_RoundCornersAll             = VanDrawFlags_RoundCornersTopLeft | VanDrawFlags_RoundCornersTopRight | VanDrawFlags_RoundCornersBottomLeft | VanDrawFlags_RoundCornersBottomRight, // (Default!!)
    VanDrawFlags_RoundCornersDefault_        = VanDrawFlags_RoundCornersAll, // Default to ALL corners if none of the _RoundCornersXX flags are specified!
    VanDrawFlags_RoundCornersTop             = VanDrawFlags_RoundCornersTopLeft | VanDrawFlags_RoundCornersTopRight,
    VanDrawFlags_RoundCornersBottom          = VanDrawFlags_RoundCornersBottomLeft | VanDrawFlags_RoundCornersBottomRight,
    VanDrawFlags_RoundCornersLeft            = VanDrawFlags_RoundCornersBottomLeft | VanDrawFlags_RoundCornersTopLeft,
    VanDrawFlags_RoundCornersRight           = VanDrawFlags_RoundCornersBottomRight | VanDrawFlags_RoundCornersTopRight,
    VanDrawFlags_RoundCornersMask_           = VanDrawFlags_RoundCornersAll | VanDrawFlags_RoundCornersNone,

    VanDrawFlags_Closed                      = 1 << 9, // PathStroke(), AddPolyline(): specify that shape should be closed.
    VanDrawFlags_InvalidMask_                = ~0x7FFFFFF0, // == 0x8000000F,
};

// Flags for VanDrawList instance. Those are set automatically by VanGui:: functions from VanGuiIO settings, and generally not manipulated directly.
// It is however possible to temporarily alter flags between calls to VanDrawList:: functions.
enum VanDrawListFlags_
{
    VanDrawListFlags_None                    = 0,
    VanDrawListFlags_AntiAliasedLines        = 1 << 0,  // Enable anti-aliased lines/borders (*2 the number of triangles for 1.0f wide line or lines thin enough to be drawn using textures, otherwise *3 the number of triangles)
    VanDrawListFlags_AntiAliasedLinesUseTex  = 1 << 1,  // Enable anti-aliased lines/borders using textures when possible. Require backend to render with bilinear filtering (NOT point/nearest filtering).
    VanDrawListFlags_AntiAliasedFill         = 1 << 2,  // Enable anti-aliased edge around filled shapes (rounded rectangles, circles).
    VanDrawListFlags_AllowVtxOffset          = 1 << 3,  // Can emit 'VtxOffset > 0' to allow large meshes. Set when 'VanGuiBackendFlags_RendererHasVtxOffset' is enabled.
};

// Draw command list
// This is the low-level list of polygons that VanGui:: functions are filling. At the end of the frame,
// all command lists are passed to your VanGuiIO::RenderDrawListFn function for rendering.
// Each VanGUI window contains its own VanDrawList. You can use VanGui::GetWindowDrawList() to
// access the current window draw list and draw custom primitives.
// You can interleave normal VanGui:: calls and adding primitives to the current draw list.
// In single viewport mode, top-left is == GetMainViewport()->Pos (generally 0,0), bottom-right is == GetMainViewport()->Pos+Size (generally io.DisplaySize).
// You are totally free to apply whatever transformation matrix you want to the data (depending on the use of the transformation you may want to apply it to ClipRect as well!)
// Important: Primitives are always added to the list and not culled (culling is done at higher-level by VanGui:: functions), if you use this API a lot consider coarse culling your drawn objects.
struct VanDrawList
{
    // This is what you have to render
    VanVector<VanDrawCmd>     CmdBuffer;          // Draw commands. Typically 1 command = 1 GPU draw call, unless the command is a callback.
    VanVector<VanDrawIdx>     IdxBuffer;          // Index buffer. Each command consume VanDrawCmd::ElemCount of those
    VanVector<VanDrawVert>    VtxBuffer;          // Vertex buffer.
    VanDrawListFlags         Flags;              // Flags, you may poke into these to adjust anti-aliasing settings per-primitive.

    // [Internal, used while building lists]
    unsigned int            _VtxCurrentIdx;     // [Internal] generally == VtxBuffer.Size unless we are past 64K vertices, in which case this gets reset to 0.
    VanDrawListSharedData*   _Data;              // Pointer to shared draw data (you can use VanGui::GetDrawListSharedData() to get the one from current VanGui context)
    VanDrawVert*             _VtxWritePtr;       // [Internal] point within VtxBuffer.Data after each add command (to avoid using the VanVector<> operators too much)
    VanDrawIdx*              _IdxWritePtr;       // [Internal] point within IdxBuffer.Data after each add command (to avoid using the VanVector<> operators too much)
    VanVector<VanVec2>        _Path;              // [Internal] current path building
    VanDrawCmdHeader         _CmdHeader;         // [Internal] template of active commands. Fields should match those of CmdBuffer.back().
    VanDrawListSplitter      _Splitter;          // [Internal] for channels api (note: prefer using your own persistent instance of VanDrawListSplitter!)
    VanVector<VanVec4>        _ClipRectStack;     // [Internal]
    VanVector<VanTextureRef>  _TextureStack;      // [Internal]
    VanVector<VanU8>          _CallbacksDataBuf;  // [Internal]
    float                   _FringeScale;       // [Internal] anti-alias fringe is scaled by this value, this helps to keep things sharp while zooming at vertex buffer content
    const char*             _OwnerName;         // Pointer to owner window's name for debugging

    // If you want to create VanDrawList instances, pass them VanGui::GetDrawListSharedData().
    // (advanced: you may create and use your own VanDrawListSharedData so you can use VanDrawList without VanGui, but that's more involved)
    VANGUI_API VanDrawList(VanDrawListSharedData* shared_data);
    VANGUI_API ~VanDrawList();

    VANGUI_API void  PushClipRect(const VanVec2& clip_rect_min, const VanVec2& clip_rect_max, bool intersect_with_current_clip_rect = false);  // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level VanGui::PushClipRect() to affect logic (hit-testing and widget culling)
    VANGUI_API void  PushClipRectFullScreen();
    VANGUI_API void  PopClipRect();
    VANGUI_API void  PushTexture(VanTextureRef tex_ref);
    VANGUI_API void  PopTexture();
    inline VanVec2   GetClipRectMin() const { const VanVec4& cr = _ClipRectStack.back(); return VanVec2(cr.x, cr.y); }
    inline VanVec2   GetClipRectMax() const { const VanVec4& cr = _ClipRectStack.back(); return VanVec2(cr.z, cr.w); }

    // Primitives
    // - Filled shapes must always use clockwise winding order. The anti-aliasing fringe depends on it. Counter-clockwise shapes will have "inward" anti-aliasing.
    // - For rectangular primitives, "p_min" and "p_max" represent the upper-left and lower-right corners.
    // - For circle primitives, use "num_segments == 0" to automatically calculate tessellation (preferred).
    //   In older versions (until VanGUI 1.77) the AddCircle functions defaulted to num_segments == 12.
    //   In future versions we will use textures to provide cheaper and higher-quality circles.
    //   Use AddNgon() and AddNgonFilled() functions if you need to guarantee a specific number of sides.
    VANGUI_API void  AddLine(const VanVec2& p1, const VanVec2& p2, VanU32 col, float thickness = 1.0f);
    VANGUI_API void  AddLineH(float min_x, float max_x, float y, VanU32 col, float thickness = 1.0f);
    VANGUI_API void  AddLineV(float x, float min_y, float max_y, VanU32 col, float thickness = 1.0f);
    VANGUI_API void  AddRect(const VanVec2& p_min, const VanVec2& p_max, VanU32 col, float rounding = 0.0f, float thickness = 1.0f, VanDrawFlags flags = 0);   // a: upper-left, b: lower-right (== upper-left + size)
    VANGUI_API void  AddRectFilled(const VanVec2& p_min, const VanVec2& p_max, VanU32 col, float rounding = 0.0f, VanDrawFlags flags = 0);                     // a: upper-left, b: lower-right (== upper-left + size)
    VANGUI_API void  AddRectFilledMultiColor(const VanVec2& p_min, const VanVec2& p_max, VanU32 col_upr_left, VanU32 col_upr_right, VanU32 col_bot_right, VanU32 col_bot_left);
    VANGUI_API void  AddQuad(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, VanU32 col, float thickness = 1.0f);
    VANGUI_API void  AddQuadFilled(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, VanU32 col);
    VANGUI_API void  AddTriangle(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, VanU32 col, float thickness = 1.0f);
    VANGUI_API void  AddTriangleFilled(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, VanU32 col);
    VANGUI_API void  AddCircle(const VanVec2& center, float radius, VanU32 col, int num_segments = 0, float thickness = 1.0f);
    VANGUI_API void  AddCircleFilled(const VanVec2& center, float radius, VanU32 col, int num_segments = 0);
    VANGUI_API void  AddNgon(const VanVec2& center, float radius, VanU32 col, int num_segments, float thickness = 1.0f);
    VANGUI_API void  AddNgonFilled(const VanVec2& center, float radius, VanU32 col, int num_segments);
    VANGUI_API void  AddEllipse(const VanVec2& center, const VanVec2& radius, VanU32 col, float rot = 0.0f, int num_segments = 0, float thickness = 1.0f);
    VANGUI_API void  AddEllipseFilled(const VanVec2& center, const VanVec2& radius, VanU32 col, float rot = 0.0f, int num_segments = 0);
    VANGUI_API void  AddText(const VanVec2& pos, VanU32 col, const char* text_begin, const char* text_end = nullptr);
    VANGUI_API void  AddText(VanFont* font, float font_size, const VanVec2& pos, VanU32 col, const char* text_begin, const char* text_end = nullptr, float wrap_width = 0.0f, const VanVec4* cpu_fine_clip_rect = nullptr);
    VANGUI_API void  AddBezierCubic(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, VanU32 col, float thickness, int num_segments = 0); // Cubic Bezier (4 control points)
    VANGUI_API void  AddBezierQuadratic(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, VanU32 col, float thickness, int num_segments = 0);               // Quadratic Bezier (3 control points)

    // General polygon
    // - Only simple polygons are supported by filling functions (no self-intersections, no holes).
    // - Concave polygon fill is more expensive than convex one: it has O(N^2) complexity. Provided as a convenience for the user but not used by the main library.
    VANGUI_API void  AddPolyline(const VanVec2* points, int num_points, VanU32 col, float thickness, VanDrawFlags flags = 0);
    VANGUI_API void  AddConvexPolyFilled(const VanVec2* points, int num_points, VanU32 col);
    VANGUI_API void  AddConcavePolyFilled(const VanVec2* points, int num_points, VanU32 col);

    // Image primitives
    // - Read FAQ to understand what VanTextureID/VanTextureRef are.
    // - "p_min" and "p_max" represent the upper-left and lower-right corners of the rectangle.
    // - "uv_min" and "uv_max" represent the normalized texture coordinates to use for those corners. Using (0,0)->(1,1) texture coordinates will generally display the entire texture.
    VANGUI_API void  AddImage(VanTextureRef tex_ref, const VanVec2& p_min, const VanVec2& p_max, const VanVec2& uv_min = VanVec2(0, 0), const VanVec2& uv_max = VanVec2(1, 1), VanU32 col = VAN_COL32_WHITE);
    VANGUI_API void  AddImageQuad(VanTextureRef tex_ref, const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, const VanVec2& uv1 = VanVec2(0, 0), const VanVec2& uv2 = VanVec2(1, 0), const VanVec2& uv3 = VanVec2(1, 1), const VanVec2& uv4 = VanVec2(0, 1), VanU32 col = VAN_COL32_WHITE);
    VANGUI_API void  AddImageRounded(VanTextureRef tex_ref, const VanVec2& p_min, const VanVec2& p_max, const VanVec2& uv_min, const VanVec2& uv_max, VanU32 col, float rounding, VanDrawFlags flags = 0);

    // Stateful path API, add points then finish with PathFillConvex() or PathStroke()
    // - Important: filled shapes must always use clockwise winding order! The anti-aliasing fringe depends on it. Counter-clockwise shapes will have "inward" anti-aliasing.
    //   so e.g. 'PathArcTo(center, radius, PI * -0.5f, PI)' is ok, whereas 'PathArcTo(center, radius, PI, PI * -0.5f)' won't have correct anti-aliasing when followed by PathFillConvex().
    inline    void  PathClear()                                                 { _Path.Size = 0; }
    inline    void  PathLineTo(const VanVec2& pos)                               { _Path.push_back(pos); }
    inline    void  PathLineToMergeDuplicate(const VanVec2& pos)                 { if (_Path.Size == 0 || memcmp(&_Path.Data[_Path.Size - 1], &pos, 8) != 0) _Path.push_back(pos); }
    inline    void  PathFillConvex(VanU32 col)                                   { AddConvexPolyFilled(_Path.Data, _Path.Size, col); _Path.Size = 0; }
    inline    void  PathFillConcave(VanU32 col)                                  { AddConcavePolyFilled(_Path.Data, _Path.Size, col); _Path.Size = 0; }
    inline    void  PathStroke(VanU32 col, float thickness = 1.0f, VanDrawFlags flags = 0) { AddPolyline(_Path.Data, _Path.Size, col, thickness, flags); _Path.Size = 0; }
    VANGUI_API void  PathArcTo(const VanVec2& center, float radius, float a_min, float a_max, int num_segments = 0);
    VANGUI_API void  PathArcToFast(const VanVec2& center, float radius, int a_min_of_12, int a_max_of_12);                // Use precomputed angles for a 12 steps circle
    VANGUI_API void  PathEllipticalArcTo(const VanVec2& center, const VanVec2& radius, float rot, float a_min, float a_max, int num_segments = 0); // Ellipse
    VANGUI_API void  PathBezierCubicCurveTo(const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, int num_segments = 0); // Cubic Bezier (4 control points)
    VANGUI_API void  PathBezierQuadraticCurveTo(const VanVec2& p2, const VanVec2& p3, int num_segments = 0);               // Quadratic Bezier (3 control points)
    VANGUI_API void  PathRect(const VanVec2& rect_min, const VanVec2& rect_max, float rounding = 0.0f, VanDrawFlags flags = 0);

    // Advanced: Draw Callbacks
    // - May be used to alter render state (change sampler, blending, current shader). May be used to emit custom rendering commands (difficult to do correctly, but possible).
    // - Use special GetPlatformIO().DrawCallback_ResetRenderState callback to instruct backend to reset its render state to the default.
    // - See other standard callbacks in GetPlatformIO(), which may or not be supported by your backend.
    // - Your rendering loop must check for 'UserCallback' in VanDrawCmd and call the function instead of rendering triangles. All standard backends are honoring this.
    // - For some backends, the callback may access selected render-states exposed by the backend in a VanGui_ImplXXXX_RenderState structure pointed to by platform_io.Renderer_RenderState.
    // - IMPORTANT: please be mindful of the different level of indirection between using size==0 (copying argument) and using size>0 (copying pointed data into a buffer).
    //   - If userdata_size == 0: we copy/store the 'userdata' argument as-is. It will be available unmodified in VanDrawCmd::UserCallbackData during render.
    //   - If userdata_size > 0,  we copy/store 'userdata_size' bytes pointed to by 'userdata'. We store them in a buffer stored inside the drawlist. VanDrawCmd::UserCallbackData will point inside that buffer so you have to retrieve data from there. Your callback may need to use VanDrawCmd::UserCallbackDataSize if you expect dynamically-sized data.
    //   - Support for userdata_size > 0 was added in v1.91.4, October 2024. So earlier code always only allowed to copy/store a simple void*.
    VANGUI_API void  AddCallback(VanDrawCallback callback, void* userdata = nullptr, size_t userdata_size = 0);

    // Advanced: Miscellaneous
    VANGUI_API void  AddDrawCmd();                                               // This is useful if you need to forcefully create a new draw call (to allow for dependent rendering / blending). Otherwise primitives are merged into the same draw-call as much as possible
    [[nodiscard]] VANGUI_API VanDrawList* CloneOutput() const;                                  // Create a clone of the CmdBuffer/IdxBuffer/VtxBuffer. For multi-threaded rendering, consider using `vangui_threaded_rendering` instead.

    // Advanced: Channels
    // - Use to split render into layers. By switching channels to can render out-of-order (e.g. submit FG primitives before BG primitives)
    // - Use to minimize draw calls (e.g. if going back-and-forth between multiple clipping rectangles, prefer to append into separate channels then merge at the end)
    // - This API shouldn't have been in VanDrawList in the first place!
    //   Prefer using your own persistent instance of VanDrawListSplitter as you can stack them.
    //   Using the VanDrawList::ChannelsXXXX you cannot stack a split over another.
    inline void     ChannelsSplit(int count)    { _Splitter.Split(this, count); }
    inline void     ChannelsMerge()             { _Splitter.Merge(this); }
    inline void     ChannelsSetCurrent(int n)   { _Splitter.SetCurrentChannel(this, n); }

    // Advanced: Primitives allocations
    // - We render triangles (three vertices)
    // - All primitives needs to be reserved via PrimReserve() beforehand.
    VANGUI_API void  PrimReserve(int idx_count, int vtx_count);
    VANGUI_API void  PrimUnreserve(int idx_count, int vtx_count);
    VANGUI_API void  PrimRect(const VanVec2& a, const VanVec2& b, VanU32 col);      // Axis aligned rectangle (composed of two triangles)
    VANGUI_API void  PrimRectUV(const VanVec2& a, const VanVec2& b, const VanVec2& uv_a, const VanVec2& uv_b, VanU32 col);
    VANGUI_API void  PrimQuadUV(const VanVec2& a, const VanVec2& b, const VanVec2& c, const VanVec2& d, const VanVec2& uv_a, const VanVec2& uv_b, const VanVec2& uv_c, const VanVec2& uv_d, VanU32 col);
    inline    void  PrimWriteVtx(const VanVec2& pos, const VanVec2& uv, VanU32 col)    { _VtxWritePtr->pos = pos; _VtxWritePtr->uv = uv; _VtxWritePtr->col = col; _VtxWritePtr++; _VtxCurrentIdx++; }
    inline    void  PrimWriteIdx(VanDrawIdx idx)                                     { *_IdxWritePtr = idx; _IdxWritePtr++; }
    inline    void  PrimVtx(const VanVec2& pos, const VanVec2& uv, VanU32 col)         { PrimWriteIdx(static_cast<VanDrawIdx>(_VtxCurrentIdx)); PrimWriteVtx(pos, uv, col); } // Write vertex with unique index

    // Obsolete names
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    inline    void  AddRect(const VanVec2& p_min, const VanVec2& p_max, VanU32 col, float rounding, VanDrawFlags flags, float thickness) { AddRect(p_min, p_max, col, rounding, thickness, flags); } // OBSOLETED in 1.92.8: NEW FUNCTION SIGNATURE HAS 'thickness' AND 'flags' SWAPPED.
    inline    void  AddPolyline(const VanVec2* points, int num_points, VanU32 col, VanDrawFlags flags, float thickness)                 { AddPolyline(points, num_points, col, thickness, flags); } // OBSOLETED in 1.92.8: NEW FUNCTION SIGNATURE HAS 'thickness' AND 'flags' SWAPPED.
    inline    void  PathStroke(VanU32 col, VanDrawFlags flags, float thickness)                                                        { PathStroke(col, thickness, flags); }                      // OBSOLETED in 1.92.8: NEW FUNCTION SIGNATURE HAS 'thickness' AND 'flags' SWAPPED.
    inline    void  PushTextureID(VanTextureRef tex_ref) { PushTexture(tex_ref); }   // RENAMED in 1.92.0
    inline    void  PopTextureID()                      { PopTexture(); }           // RENAMED in 1.92.0
#else
    VANGUI_API void  AddRect(const VanVec2& p_min, const VanVec2& p_max, VanU32 col, float rounding /*= 0.0f*/, VanDrawFlags flags /*= 0*/, float thickness /*= 1.0f*/) = delete;
    VANGUI_API void  AddPolyline(const VanVec2* points, int num_points, VanU32 col, VanDrawFlags flags, float thickness) = delete;
    inline    void  PathStroke(VanU32 col, VanDrawFlags flags /*= 0*/, float thickness /*= 1.0f*/) = delete;
#endif
    //inline  void  AddEllipse(const VanVec2& center, float radius_x, float radius_y, VanU32 col, float rot = 0.0f, int num_segments = 0, float thickness = 1.0f) { AddEllipse(center, VanVec2(radius_x, radius_y), col, rot, num_segments, thickness); } // OBSOLETED in 1.90.5 (Mar 2024)
    //inline  void  AddEllipseFilled(const VanVec2& center, float radius_x, float radius_y, VanU32 col, float rot = 0.0f, int num_segments = 0) { AddEllipseFilled(center, VanVec2(radius_x, radius_y), col, rot, num_segments); }                        // OBSOLETED in 1.90.5 (Mar 2024)
    //inline  void  PathEllipticalArcTo(const VanVec2& center, float radius_x, float radius_y, float rot, float a_min, float a_max, int num_segments = 0) { PathEllipticalArcTo(center, VanVec2(radius_x, radius_y), rot, a_min, a_max, num_segments); } // OBSOLETED in 1.90.5 (Mar 2024)
    //inline  void  AddBezierCurve(const VanVec2& p1, const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, VanU32 col, float thickness, int num_segments = 0) { AddBezierCubic(p1, p2, p3, p4, col, thickness, num_segments); }                         // OBSOLETED in 1.80 (Jan 2021)
    //inline  void  PathBezierCurveTo(const VanVec2& p2, const VanVec2& p3, const VanVec2& p4, int num_segments = 0) { PathBezierCubicCurveTo(p2, p3, p4, num_segments); }                                                                                // OBSOLETED in 1.80 (Jan 2021)

    // [Internal helpers]
    VANGUI_API void  _SetDrawListSharedData(VanDrawListSharedData* data);
    VANGUI_API void  _ResetForNewFrame();
    VANGUI_API void  _ClearFreeMemory();
    VANGUI_API void  _PopUnusedDrawCmd();
    VANGUI_API void  _TryMergeDrawCmds();
    VANGUI_API void  _OnChangedClipRect();
    VANGUI_API void  _OnChangedTexture();
    VANGUI_API void  _OnChangedVtxOffset();
    VANGUI_API void  _SetTexture(VanTextureRef tex_ref);
    VANGUI_API int   _CalcCircleAutoSegmentCount(float radius) const;
    VANGUI_API void  _PathArcToFastEx(const VanVec2& center, float radius, int a_min_sample, int a_max_sample, int a_step);
    VANGUI_API void  _PathArcToN(const VanVec2& center, float radius, float a_min, float a_max, int num_segments);
};

// All draw data to render a VanGUI frame
// (NB: the style and the naming convention here is a little inconsistent, we currently preserve them for backward compatibility purpose,
// as this is one of the oldest structure exposed by the library! Basically, VanDrawList == CmdList)
struct VanDrawData
{
    bool                Valid;              // Only valid after Render() is called and before the next NewFrame() is called.
    int                 CmdListsCount;      // == CmdLists.Size. (OBSOLETE: exists for legacy reasons). Number of VanDrawList* to render.
    int                 TotalIdxCount;      // For convenience, sum of all VanDrawList's IdxBuffer.Size
    int                 TotalVtxCount;      // For convenience, sum of all VanDrawList's VtxBuffer.Size
    VanVector<VanDrawList*> CmdLists;         // Array of VanDrawList* to render. The VanDrawLists are owned by VanGuiContext and only pointed to from here.
    VanVec2              DisplayPos;         // Top-left position of the viewport to render (== top-left of the orthogonal projection matrix to use) (== GetMainViewport()->Pos for the main viewport, == (0.0) in most single-viewport applications)
    VanVec2              DisplaySize;        // Size of the viewport to render (== GetMainViewport()->Size for the main viewport, == io.DisplaySize in most single-viewport applications)
    VanVec2              FramebufferScale;   // Amount of pixels for each unit of DisplaySize. Copied from viewport->FramebufferScale (== io.DisplayFramebufferScale for main viewport). Generally (1,1) on normal display, (2,2) on OSX with Retina display.
    VanGuiViewport*      OwnerViewport;      // Viewport carrying the VanDrawData instance, might be of use to the renderer (generally not).
    VanVector<VanTextureData*>* Textures;     // List of textures to update. Most of the times the list is shared by all VanDrawData, has only 1 texture and it doesn't need any update. This almost always points to VanGui::GetPlatformIO().Textures[]. May be overridden or set to nullptr if you want to manually update textures.

    // Functions
    VanDrawData()    { Clear(); }
    VANGUI_API void  Clear();
    VANGUI_API void  AddDrawList(VanDrawList* draw_list);     // Helper to add an external draw list into an existing VanDrawData.
    VANGUI_API void  DeIndexAllBuffers();                    // Helper to convert all buffers from indexed to non-indexed, in case you cannot render indexed. Note: this is slow and most likely a waste of resources. Always prefer indexed rendering!
    VANGUI_API void  ScaleClipRects(const VanVec2& fb_scale); // Helper to scale the ClipRect field of each VanDrawCmd. Use if your final output buffer is at a different scale than VanGUI expects, or if there is a difference between your window resolution and framebuffer resolution.
};

//-----------------------------------------------------------------------------
// [SECTION] Texture API (VanTextureFormat, VanTextureStatus, VanTextureRect, VanTextureData)
//-----------------------------------------------------------------------------
// In principle, the only data types that user/application code should care about are 'VanTextureRef' and 'VanTextureID'.
// They are defined above in this header file. Read their description to the difference between VanTextureRef and VanTextureID.
// FOR ALL OTHER VanTextureXXXX TYPES: ONLY CORE LIBRARY AND RENDERER BACKENDS NEED TO KNOW AND CARE ABOUT THEM.
//-----------------------------------------------------------------------------

#undef Status // X11 headers are leaking this.

// We intentionally support a limited amount of texture formats to limit burden on CPU-side code and extension.
// Most standard backends only support RGBA32 but we provide a single channel option for low-resource/embedded systems.
enum VanTextureFormat
{
    VanTextureFormat_RGBA32,         // 4 components per pixel, each is unsigned 8-bit. Total size = TexWidth * TexHeight * 4
    VanTextureFormat_Alpha8,         // 1 component per pixel, each is unsigned 8-bit. Total size = TexWidth * TexHeight
};

// Status of a texture to communicate with Renderer Backend.
enum VanTextureStatus
{
    VanTextureStatus_OK,
    VanTextureStatus_Destroyed,      // Backend destroyed the texture.
    VanTextureStatus_WantCreate,     // Requesting backend to create the texture. Set status OK when done.
    VanTextureStatus_WantUpdates,    // Requesting backend to update specific blocks of pixels (write to texture portions which have never been used before). Set status OK when done.
    VanTextureStatus_WantDestroy,    // Requesting backend to destroy the texture. Set status to Destroyed when done.
};

// Coordinates of a rectangle within a texture.
// When a texture is in VanTextureStatus_WantUpdates state, we provide a list of individual rectangles to copy to the graphics system.
// You may use VanTextureData::Updates[] for the list, or VanTextureData::UpdateBox for a single bounding box.
struct VanTextureRect
{
    unsigned short      x, y;       // Upper-left coordinates of rectangle to update
    unsigned short      w, h;       // Size of rectangle to update (in pixels)
};

// Specs and pixel storage for a texture used by VanGUI.
// This is only useful for (1) core library and (2) backends. End-user/applications do not need to care about this.
// Renderer Backends will create a GPU-side version of this.
// Why does we store two identifiers: TexID and BackendUserData?
// - VanTextureID    TexID           = lower-level identifier stored in VanDrawCmd. VanDrawCmd can refer to textures not created by the backend, and for which there's no VanTextureData.
// - void*          BackendUserData = higher-level opaque storage for backend own book-keeping. Some backends may have enough with TexID and not need both.
 // In columns below: who reads/writes each fields? 'r'=read, 'w'=write, 'core'=main library, 'backend'=renderer backend
struct VanTextureData
{
    //------------------------------------------ core / backend ---------------------------------------
    int                 UniqueID;               // w    -   // [DEBUG] Sequential index to facilitate identifying a texture when debugging/printing. Unique per atlas.
    VanTextureStatus     Status;                 // rw   rw  // VanTextureStatus_OK/_WantCreate/_WantUpdates/_WantDestroy. Always use SetStatus() to modify!
    void*               BackendUserData;        // -    rw  // Convenience storage for backend. Some backends may have enough with TexID.
    VanTextureID         TexID;                  // r    w   // Backend-specific texture identifier. Always use SetTexID() to modify! The identifier will stored in VanDrawCmd::GetTexID() and passed to backend's RenderDrawData function.
    VanTextureFormat     Format;                 // w    r   // VanTextureFormat_RGBA32 (default) or VanTextureFormat_Alpha8
    int                 Width;                  // w    r   // Texture width
    int                 Height;                 // w    r   // Texture height
    int                 BytesPerPixel;          // w    r   // 4 or 1
    unsigned char*      Pixels;                 // w    r   // Pointer to buffer holding 'Width*Height' pixels and 'Width*Height*BytesPerPixels' bytes.
    VanTextureRect       UsedRect;               // w    r   // Bounding box encompassing all past and queued Updates[].
    VanTextureRect       UpdateRect;             // w    r   // Bounding box encompassing all queued Updates[].
    VanVector<VanTextureRect> Updates;            // w    r   // Array of individual updates.
    int                 UnusedFrames;           // w    r   // In order to facilitate handling Status==WantDestroy in some backend: this is a count successive frames where the texture was not used. Always >0 when Status==WantDestroy.
    unsigned short      RefCount;               // w    r   // Number of contexts using this texture. Used during backend shutdown.
    bool                UseColors;              // w    r   // Tell whether our texture data is known to use colors (rather than just white + alpha).
    bool                WantDestroyNextFrame;   // rw   -   // [Internal] Queued to set VanTextureStatus_WantDestroy next frame. May still be used in the current frame.

    // Functions
    // - If GetPixels() functions asserts while being called by your render loop, it could be caused by calling VanFontAtlas::Clear() instead of ClearFonts()?
    VanTextureData()     { memset((void*)this, 0, sizeof(*this)); Status = VanTextureStatus_Destroyed; TexID = VanTextureID_Invalid; } // VanVector<VanTextureRect> member is zero-init-safe (Data=nullptr, Size=0, Capacity=0)
    ~VanTextureData()    { DestroyPixels(); }
    VANGUI_API void      Create(VanTextureFormat format, int w, int h);
    VANGUI_API void      DestroyPixels();
    void*               GetPixels()                 { VAN_ASSERT(Pixels != nullptr); return Pixels; }
    void*               GetPixelsAt(int x, int y)   { VAN_ASSERT(Pixels != nullptr); return Pixels + (static_cast<size_t>(x) + static_cast<size_t>(y) * static_cast<size_t>(Width)) * static_cast<size_t>(BytesPerPixel); }
    size_t              GetSizeInBytes() const      { return static_cast<size_t>(Width) * static_cast<size_t>(Height) * static_cast<size_t>(BytesPerPixel); }
    size_t              GetPitch() const            { return static_cast<size_t>(Width) * static_cast<size_t>(BytesPerPixel); }
    VanTextureRef        GetTexRef()                 { VanTextureRef tex_ref; tex_ref._TexData = this; tex_ref._TexID = VanTextureID_Invalid; return tex_ref; }
    VanTextureID         GetTexID() const            { return TexID; }

    // Called by Renderer backend
    // - Call SetTexID() and SetStatus() after honoring texture requests. Never modify TexID and Status directly!
    // - A backend may decide to destroy a texture that we did not request to destroy, which is fine (e.g. freeing resources), but we immediately set the texture back in _WantCreate mode.
    void    SetTexID(VanTextureID tex_id)            { TexID = tex_id; }
    void    SetStatus(VanTextureStatus status)       { Status = status; if (status == VanTextureStatus_Destroyed && !WantDestroyNextFrame && Pixels != nullptr) Status = VanTextureStatus_WantCreate; }
};

//-----------------------------------------------------------------------------
// [SECTION] Font API (VanFontConfig, VanFontGlyph, VanFontAtlasFlags, VanFontAtlas, VanFontGlyphRangesBuilder, VanFont)
//-----------------------------------------------------------------------------

// A font input/source (we may rename this to VanFontSource in the future)
struct VanFontConfig
{
    // Data Source
    char            Name[40];               // <auto>   // Name (strictly to ease debugging, hence limited size buffer)
    void*           FontData;               //          // TTF/OTF data
    int             FontDataSize;           //          // TTF/OTF data size
    bool            FontDataOwnedByAtlas;   // true     // TTF/OTF data ownership taken by the owner VanFontAtlas (will delete memory itself). SINCE 1.92, THE DATA NEEDS TO PERSIST FOR WHOLE DURATION OF ATLAS.

    // Options
    bool            MergeMode;              // false    // Merge into previous VanFont, so you can combine multiple inputs font into one VanFont (e.g. ASCII font + icons + Japanese glyphs). You may want to use GlyphOffset.y when merge font of different heights.
    bool            PixelSnapH;             // false    // Align every glyph AdvanceX to pixel boundaries. Prevents fractional font size from working correctly! Useful e.g. if you are merging a non-pixel aligned font with the default font. If enabled, OversampleH/V will default to 1.
    VanS8            OversampleH;            // 0 (2)    // Rasterize at higher quality for sub-pixel positioning. 0 == auto == 1 or 2 depending on size. Note the difference between 2 and 3 is minimal. You can reduce this to 1 for large glyphs save memory. Read https://github.com/nothings/stb/blob/master/tests/oversample/README.md for details.
    VanS8            OversampleV;            // 0 (1)    // Rasterize at higher quality for sub-pixel positioning. 0 == auto == 1. This is not really useful as we don't use sub-pixel positions on the Y axis.
    VanWchar         EllipsisChar;           // 0        // Explicitly specify Unicode codepoint of ellipsis character. When fonts are being merged first specified ellipsis will be used.
    float           SizePixels;             //          // Output size in pixels for rasterizer (more or less maps to the resulting font height).
    const VanWchar*  GlyphRanges;            // nullptr     // *LEGACY* THE ARRAY DATA NEEDS TO PERSIST AS LONG AS THE FONT IS ALIVE. Pointer to a user-provided list of Unicode range (2 value per range, values are inclusive, zero-terminated list).
    const VanWchar*  GlyphExcludeRanges;     // nullptr     // Pointer to a small user-provided list of Unicode ranges (2 value per range, values are inclusive, zero-terminated list). This is very close to GlyphRanges[] but designed to exclude ranges from a font source, when merging fonts with overlapping glyphs. Use "Input Glyphs Overlap Detection Tool" to find about your overlapping ranges.
    //VanVec2        GlyphExtraSpacing;      // 0, 0     // (REMOVED AT IT SEEMS LARGELY OBSOLETE. PLEASE REPORT IF YOU WERE USING THIS). Extra spacing (in pixels) between glyphs when rendered: essentially add to glyph->AdvanceX. Only X axis is supported for now.
    VanVec2          GlyphOffset;            // 0, 0     // Offset (in pixels) all glyphs from this font input. Absolute value for default size, other sizes will scale this value.
    float           GlyphMinAdvanceX;       // 0        // Minimum AdvanceX for glyphs, set Min to align font icons, set both Min/Max to enforce mono-space font. Absolute value for default size, other sizes will scale this value.
    float           GlyphMaxAdvanceX;       // FLT_MAX  // Maximum AdvanceX for glyphs
    float           GlyphExtraAdvanceX;     // 0        // Extra spacing (in pixels) between glyphs. Please contact us if you are using this. // FIXME-NEWATLAS: Intentionally unscaled
    VanU32           FontNo;                 // 0        // Index of font within TTF/OTF file
    unsigned int    FontLoaderFlags;        // 0        // Settings for custom font builder. THIS IS BUILDER IMPLEMENTATION DEPENDENT. Leave as zero if unsure.
    //unsigned int  FontBuilderFlags;       // --       // [Renamed in 1.92] Use FontLoaderFlags.
    float           RasterizerMultiply;     // 1.0f     // Linearly brighten (>1.0f) or darken (<1.0f) font output. Brightening small fonts may be a good workaround to make them more readable. This is a silly thing we may remove in the future.
    float           RasterizerDensity;      // 1.0f     // [LEGACY: this only makes sense when VanGuiBackendFlags_RendererHasTextures is not supported] DPI scale multiplier for rasterization. Not altering other font metrics: makes it easy to swap between e.g. a 100% and a 400% fonts for a zooming display, or handle Retina screen. IMPORTANT: If you change this it is expected that you increase/decrease font scale roughly to the inverse of this, otherwise quality may look lowered.
    float           ExtraSizeScale;         // 1.0f     // Extra rasterizer scale over SizePixels.

    // [Internal]
    VanFontFlags     Flags;                  // Font flags (don't use just yet, will be exposed in upcoming 1.92.X updates)
    VanFont*         DstFont;                // Target font (as we merging fonts, multiple VanFontConfig may target the same font)
    const VanFontLoader* FontLoader;         // Custom font backend for this source (default source is the one stored in VanFontAtlas)
    void*           FontLoaderData;         // Font loader opaque storage (per font config)

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    bool            PixelSnapV;             // true    // [Obsoleted in 1.91.6] Align Scaled GlyphOffset.y to pixel boundaries.
#endif
    VANGUI_API VanFontConfig();
};

// Hold rendering data for one glyph.
// (Note: some language parsers may fail to convert the bitfield members, in this case maybe drop store a single u32 or we can rework this)
struct VanFontGlyph
{
    unsigned int    Colored : 1;        // Flag to indicate glyph is colored and should generally ignore tinting (make it usable with no shift on little-endian as this is used in loops)
    unsigned int    Visible : 1;        // Flag to indicate glyph has no visible pixels (e.g. space). Allow early out when rendering.
    unsigned int    SourceIdx : 4;      // Index of source in parent font
    unsigned int    Codepoint : 26;     // 0x0000..0x10FFFF
    float           AdvanceX;           // Horizontal distance to advance cursor/layout position.
    float           X0, Y0, X1, Y1;     // Glyph corners. Offsets from current cursor/layout position.
    float           U0, V0, U1, V1;     // Texture coordinates for the current value of VanFontAtlas->TexRef. Cached equivalent of calling GetCustomRect() with PackId.
    int             PackId;             // [Internal] VanFontAtlasRectId value (FIXME: Cold data, could be moved elsewhere?)

    VanFontGlyph()   { memset((void*)this, 0, sizeof(*this)); PackId = -1; } // POD-safe zero-init
};

// Helper to build glyph ranges from text/string data. Feed your application strings/characters to it then call BuildRanges().
// This is essentially a tightly packed of vector of 64k booleans = 8KB storage.
struct VanFontGlyphRangesBuilder
{
    VanVector<VanU32> UsedChars;            // Store 1-bit per Unicode code point (0=unused, 1=used)

    VanFontGlyphRangesBuilder()              { Clear(); }
    inline void     Clear()                 { int size_in_bytes = (VAN_UNICODE_CODEPOINT_MAX + 1) / 8; UsedChars.resize(size_in_bytes / static_cast<int>(sizeof(VanU32))); memset(UsedChars.Data, 0, static_cast<size_t>(size_in_bytes)); }
    inline bool     GetBit(size_t n) const  { int off = static_cast<int>(n >> 5); VanU32 mask = 1u << (n & 31); return (UsedChars[off] & mask) != 0; }  // Get bit n in the array
    inline void     SetBit(size_t n)        { int off = static_cast<int>(n >> 5); VanU32 mask = 1u << (n & 31); UsedChars[off] |= mask; }               // Set bit n in the array
    inline void     AddChar(VanWchar c)      { SetBit(c); }                      // Add character
    VANGUI_API void  AddText(const char* text, const char* text_end = nullptr);     // Add string (each character of the UTF-8 string are added)
    VANGUI_API void  AddRanges(const VanWchar* ranges);                           // Add ranges, e.g. builder.AddRanges(VanFontAtlas::GetGlyphRangesDefault()) to force add all of ASCII/Latin+Ext
    VANGUI_API void  BuildRanges(VanVector<VanWchar>* out_ranges);                 // Output new ranges
};

// An opaque identifier to a rectangle in the atlas. -1 when invalid.
// The rectangle may move and UV may be invalidated, use GetCustomRect() to retrieve it.
using VanFontAtlasRectId = int;
inline constexpr int VanFontAtlasRectId_Invalid = -1;

// Output of VanFontAtlas::GetCustomRect() when using custom rectangles.
// Those values may not be cached/stored as they are only valid for the current value of atlas->TexRef
// (this is in theory derived from VanTextureRect but we use separate structures for reasons)
struct VanFontAtlasRect
{
    unsigned short  x, y;               // Position (in current texture)
    unsigned short  w, h;               // Size
    VanVec2          uv0, uv1;           // UV coordinates (in current texture)

    VanFontAtlasRect() { memset((void*)this, 0, sizeof(*this)); } // POD-safe zero-init
};

// Flags for VanFontAtlas build
enum VanFontAtlasFlags_
{
    VanFontAtlasFlags_None               = 0,
    VanFontAtlasFlags_NoPowerOfTwoHeight = 1 << 0,   // Don't round the height to next power of two
    VanFontAtlasFlags_NoMouseCursors     = 1 << 1,   // Don't build software mouse cursors into the atlas (save a little texture memory)
    VanFontAtlasFlags_NoBakedLines       = 1 << 2,   // Don't build thick line textures into the atlas (save a little texture memory, allow support for point/nearest filtering). The AntiAliasedLinesUseTex features uses them, otherwise they will be rendered using polygons (more expensive for CPU/GPU).
};

// Load and rasterize multiple TTF/OTF fonts into a same texture. The font atlas will build a single texture holding:
//  - One or more fonts.
//  - Custom graphics data needed to render the shapes needed by VanGUI.
//  - Mouse cursor shapes for software cursor rendering (unless setting 'Flags |= VanFontAtlasFlags_NoMouseCursors' in the font atlas).
//  - If you don't call any AddFont*** functions, the default font embedded in the code will be loaded for you.
// It is the rendering backend responsibility to upload texture into your graphics API:
//  - VanGui_ImplXXXX_RenderDrawData() functions generally iterate platform_io->Textures[] to create/update/destroy each VanTextureData instance.
//  - Backend then set VanTextureData's TexID and BackendUserData.
//  - Texture id are passed back to you during rendering to identify the texture. Read FAQ entry about VanTextureID/VanTextureRef for more details.
// Legacy path:
//  - Call Build() + GetTexDataAsAlpha8() or GetTexDataAsRGBA32() to build and retrieve pixels data.
//  - Call SetTexID(my_tex_id); and pass the pointer/identifier to your texture in a format natural to your graphics API.
// Common pitfalls:
// - If you pass a 'glyph_ranges' array to AddFont*** functions, you need to make sure that your array persists up until the
//   atlas is build (when calling GetTexData*** or Build()). We only copy the pointer, not the data.
// - Important: By default, AddFontFromMemoryTTF() takes ownership of the data. Even though we are not writing to it, we will free the pointer on destruction.
//   You can set font_cfg->FontDataOwnedByAtlas=false to keep ownership of your data and it won't be freed,
// - Even though many functions are suffixed with "TTF", OTF data is supported just as well.
// - This is an old API and it is currently awkward for those and various other reasons! We will address them in the future!
struct VanFontAtlas
{
    VANGUI_API VanFontAtlas();
    VANGUI_API ~VanFontAtlas();
    [[nodiscard]] VANGUI_API VanFont*           AddFont(const VanFontConfig* font_cfg);
    [[nodiscard]] VANGUI_API VanFont*           AddFontDefault(const VanFontConfig* font_cfg = nullptr);        // Selects between AddFontDefaultVector() and AddFontDefaultBitmap().
    [[nodiscard]] VANGUI_API VanFont*           AddFontDefaultVector(const VanFontConfig* font_cfg = nullptr);  // Embedded scalable font. Recommended at any higher size.
    [[nodiscard]] VANGUI_API VanFont*           AddFontDefaultBitmap(const VanFontConfig* font_cfg = nullptr);  // Embedded classic pixel-clean font. Recommended at Size 13px with no scaling.
    [[nodiscard]] VANGUI_API VanFont*           AddFontFromFileTTF(const char* filename, float size_pixels = 0.0f, const VanFontConfig* font_cfg = nullptr, const VanWchar* glyph_ranges = nullptr);
    [[nodiscard]] VANGUI_API VanFont*           AddFontFromMemoryTTF(void* font_data, int font_data_size, float size_pixels = 0.0f, const VanFontConfig* font_cfg = nullptr, const VanWchar* glyph_ranges = nullptr); // Note: Transfer ownership of 'ttf_data' to VanFontAtlas! Will be deleted after destruction of the atlas. Set font_cfg->FontDataOwnedByAtlas=false to keep ownership of your data and it won't be freed.
    [[nodiscard]] VANGUI_API VanFont*           AddFontFromMemoryCompressedTTF(const void* compressed_font_data, int compressed_font_data_size, float size_pixels = 0.0f, const VanFontConfig* font_cfg = nullptr, const VanWchar* glyph_ranges = nullptr); // 'compressed_font_data' still owned by caller. Compress with binary_to_compressed_c.cpp.
    [[nodiscard]] VANGUI_API VanFont*           AddFontFromMemoryCompressedBase85TTF(const char* compressed_font_data_base85, float size_pixels = 0.0f, const VanFontConfig* font_cfg = nullptr, const VanWchar* glyph_ranges = nullptr);              // 'compressed_font_data_base85' still owned by caller. Compress with binary_to_compressed_c.cpp with -base85 parameter.
    VANGUI_API void              RemoveFont(VanFont* font);

    VANGUI_API void              Clear();                    // Clear everything (fonts + textures). Don't call mid-frame!
    VANGUI_API void              ClearFonts();               // Clear input+output font data/glyphs. You can call this mid-frame if you load new fonts afterwards!
    VANGUI_API void              CompactCache();             // Compact cached glyphs and texture.
    VANGUI_API void              SetFontLoader(const VanFontLoader* font_loader); // Change font loader at runtime.

    // As we are transitioning toward a new font system, we expect to obsolete those soon:
    VANGUI_API void              ClearInputData();           // [OBSOLETE] Clear input data (all VanFontConfig structures including sizes, TTF data, glyph ranges, etc.) = all the data used to build the texture and fonts.
    VANGUI_API void              ClearTexData();             // [OBSOLETE] Clear CPU-side copy of the texture data. Saves RAM once the texture has been copied to graphics memory.

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    // Legacy path for build atlas + retrieving pixel data.
    // - User is in charge of copying the pixels into graphics memory (e.g. create a texture with your engine). Then store your texture handle with SetTexID().
    // - The pitch is always = Width * BytesPerPixels (1 or 4)
    // - Building in RGBA32 format is provided for convenience and compatibility, but note that unless you manually manipulate or copy color data into
    //   the texture (e.g. when using the AddCustomRect*** api), then the RGB pixels emitted will always be white (~75% of memory/bandwidth waste).
    // - From 1.92 with backends supporting VanGuiBackendFlags_RendererHasTextures:
    //   - Calling Build(), GetTexDataAsAlpha8(), GetTexDataAsRGBA32() is not needed.
    //   - In backend: replace calls to VanFontAtlas::SetTexID() with calls to VanTextureData::SetTexID() after honoring texture creation.
    VANGUI_API bool  Build();                    // Build pixels data. This is called automatically for you by the GetTexData*** functions.
    VANGUI_API void  GetTexDataAsAlpha8(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel = nullptr); // 1 byte per-pixel
    VANGUI_API void  GetTexDataAsRGBA32(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel = nullptr); // 4 bytes-per-pixel
    void            SetTexID(VanTextureID id)    { VAN_ASSERT(TexRef._TexID == VanTextureID_Invalid); TexRef._TexData->TexID = id; }                               // Called by legacy backends. May be called before texture creation.
    void            SetTexID(VanTextureRef id)   { VAN_ASSERT(TexRef._TexID == VanTextureID_Invalid && id._TexData == nullptr); TexRef._TexData->TexID = id._TexID; } // Called by legacy backends.
    bool            IsBuilt() const { return Fonts.Size > 0 && TexIsBuilt; } // Bit ambiguous: used to detect when user didn't build texture but effectively we should check TexID != 0 except that would be backend dependent..
#endif

    //-------------------------------------------
    // Glyph Ranges
    //-------------------------------------------

    // Since 1.92: specifying glyph ranges is only useful/necessary if your backend doesn't support VanGuiBackendFlags_RendererHasTextures!
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesDefault();                // Basic Latin, Extended Latin
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    // Helpers to retrieve list of common Unicode ranges (2 value per range, values are inclusive, zero-terminated list)
    // NB: Make sure that your string are UTF-8 and NOT in your local code page.
    // NB: Consider using VanFontGlyphRangesBuilder to build glyph ranges from textual data.
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesGreek();                  // Default + Greek and Coptic
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesKorean();                 // Default + Korean characters
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesJapanese();               // Default + Hiragana, Katakana, Half-Width, Selection of 2999 Ideographs
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesChineseFull();            // Default + Half-Width + Japanese Hiragana/Katakana + full set of about 21000 CJK Unified Ideographs
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesChineseSimplifiedCommon();// Default + Half-Width + Japanese Hiragana/Katakana + set of 2500 CJK Unified Ideographs for common simplified Chinese
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesCyrillic();               // Default + about 400 Cyrillic characters
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesThai();                   // Default + Thai characters
    [[nodiscard]] VANGUI_API const VanWchar*    GetGlyphRangesVietnamese();             // Default + Vietnamese characters
#endif

    //-------------------------------------------
    // [ALPHA] Custom Rectangles/Glyphs API
    //-------------------------------------------

    // Register and retrieve custom rectangles
    // - You can request arbitrary rectangles to be packed into the atlas, for your own purpose.
    // - Since 1.92.0, packing is done immediately in the function call (previously packing was done during the Build call)
    // - You can render your pixels into the texture right after calling the AddCustomRect() functions.
    // - VERY IMPORTANT:
    //   - Texture may be created/resized at any time when calling VanGui or VanFontAtlas functions.
    //   - IT WILL INVALIDATE RECTANGLE DATA SUCH AS UV COORDINATES. Always use latest values from GetCustomRect().
    //   - UV coordinates are associated to the current texture identifier aka 'atlas->TexRef'. Both TexRef and UV coordinates are typically changed at the same time.
    // - If you render colored output into your custom rectangles: set 'atlas->TexPixelsUseColors = true' as this may help some backends decide of preferred texture format.
    // - Read docs/FONTS.md for more details about using colorful icons.
    // - Note: this API may be reworked further in order to facilitate supporting e.g. multi-monitor, varying DPI settings.
    // - (Pre-1.92 names) ------------> (1.92 names)
    //   - GetCustomRectByIndex()   --> Use GetCustomRect()
    //   - CalcCustomRectUV()       --> Use GetCustomRect() and read uv0, uv1 fields.
    //   - AddCustomRectRegular()   --> Renamed to AddCustomRect()
    //   - AddCustomRectFontGlyph() --> Prefer using custom VanFontLoader inside VanFontConfig
    //   - VanFontAtlasCustomRect    --> Renamed to VanFontAtlasRect
    VANGUI_API VanFontAtlasRectId AddCustomRect(int width, int height, VanFontAtlasRect* out_r = nullptr);// Register a rectangle. Return -1 (VanFontAtlasRectId_Invalid) on error.
    VANGUI_API void              RemoveCustomRect(VanFontAtlasRectId id);                             // Unregister a rectangle. Existing pixels will stay in texture until resized / garbage collected.
    VANGUI_API bool              GetCustomRect(VanFontAtlasRectId id, VanFontAtlasRect* out_r) const;  // Get rectangle coordinates for current texture. Valid immediately, never store this (read above)!

    //-------------------------------------------
    // Members
    //-------------------------------------------

    // Input
    VanFontAtlasFlags            Flags;              // Build flags (see VanFontAtlasFlags_)
    VanTextureFormat             TexDesiredFormat;   // Desired texture format (default to VanTextureFormat_RGBA32 but may be changed to VanTextureFormat_Alpha8).
    int                         TexGlyphPadding;    // FIXME: Should be called "TexPackPadding". Padding between glyphs within texture in pixels. Defaults to 1. If your rendering method doesn't rely on bilinear filtering you may set this to 0 (will also need to set AntiAliasedLinesUseTex = false).
    int                         TexMinWidth;        // Minimum desired texture width. Must be a power of two. Default to 512.
    int                         TexMinHeight;       // Minimum desired texture height. Must be a power of two. Default to 128.
    int                         TexMaxWidth;        // Maximum desired texture width. Must be a power of two. Default to 8192.
    int                         TexMaxHeight;       // Maximum desired texture height. Must be a power of two. Default to 8192.
    void*                       UserData;           // Store your own atlas related user-data (if e.g. you have multiple font atlas).

    // Output
    // - Because textures are dynamically created/resized, the current texture identifier may changed at *ANY TIME* during the frame.
    // - This should not affect you as you can always use the latest value. But note that any precomputed UV coordinates are only valid for the current TexRef.
#ifdef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanTextureRef                TexRef;             // Latest texture identifier == TexData->GetTexRef().
#else
    union { VanTextureRef TexRef; VanTextureRef TexID; }; // Latest texture identifier == TexData->GetTexRef(). // RENAMED TexID to TexRef in 1.92.0.
#endif
    VanTextureData*              TexData;            // Latest texture.

    // [Internal]
    VanVector<VanTextureData*>    TexList;            // Texture list (most often TexList.Size == 1). TexData is always == TexList.back(). DO NOT USE DIRECTLY, USE GetDrawData().Textures[]/GetPlatformIO().Textures[] instead!
    bool                        Locked;             // Marked as locked during VanGui::NewFrame()..EndFrame() scope if TexUpdates are not supported. Any attempt to modify the atlas will assert.
    bool                        RendererHasTextures;// Copy of (BackendFlags & VanGuiBackendFlags_RendererHasTextures) from supporting context.
    bool                        TexIsBuilt;         // Set when texture was built matching current font input. Mostly useful for legacy IsBuilt() call.
    bool                        TexPixelsUseColors; // Tell whether our texture data is known to use colors (rather than just alpha channel), in order to help backend select a format or conversion process.
    VanVec2                      TexUvScale;         // = (1.0f/TexData->TexWidth, 1.0f/TexData->TexHeight). May change as new texture gets created.
    VanVec2                      TexUvWhitePixel;    // Texture coordinates to a white pixel. May change as new texture gets created.
    VanVector<VanFont*>           Fonts;              // Hold all the fonts returned by AddFont*. Fonts[0] is the default font upon calling VanGui::NewFrame(), use VanGui::PushFont()/PopFont() to change the current font.
    VanVector<VanFontConfig>      Sources;            // Source/configuration data
    VanVec4                      TexUvLines[VAN_DRAWLIST_TEX_LINES_WIDTH_MAX + 1];  // UVs for baked anti-aliased lines
    int                         TexNextUniqueID;    // Next value to be stored in TexData->UniqueID
    int                         FontNextUniqueID;   // Next value to be stored in VanFont->FontID
    VanVector<VanDrawListSharedData*> DrawListSharedDatas; // List of users for this atlas. Typically one per VanGUI context.
    VanFontAtlasBuilder*         Builder;            // Opaque interface to our data that doesn't need to be public and may be discarded when rebuilding.
    const VanFontLoader*         FontLoader;         // Font loader opaque interface (default to use FreeType when VANGUI_ENABLE_FREETYPE is defined, otherwise default to use stb_truetype). Use SetFontLoader() to change this at runtime.
    const char*                 FontLoaderName;     // Font loader name (for display e.g. in About box) == FontLoader->Name
    void*                       FontLoaderData;     // Font backend opaque storage
    unsigned int                FontLoaderFlags;    // Shared flags (for all fonts) for font loader. THIS IS BUILD IMPLEMENTATION DEPENDENT (e.g. Per-font override is also available in VanFontConfig).
    int                         RefCount;           // Number of contexts using this atlas
    VanGuiContext*               OwnerContext;       // Context which own the atlas will be in charge of updating and destroying it.

    // [Obsolete]
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    // Legacy: You can request your rectangles to be mapped as font glyph (given a font + Unicode point), so you can render e.g. custom colorful icons and use them as regular glyphs. --> Prefer using a custom VanFontLoader.
    VanFontAtlasRect             TempRect;           // For old GetCustomRectByIndex() API
    inline VanFontAtlasRectId    AddCustomRectRegular(int w, int h)                                                          { return AddCustomRect(w, h); }                             // RENAMED in 1.92.0
    inline const VanFontAtlasRect* GetCustomRectByIndex(VanFontAtlasRectId id)                                                { return GetCustomRect(id, &TempRect) ? &TempRect : nullptr; } // OBSOLETED in 1.92.0
    inline void                 CalcCustomRectUV(const VanFontAtlasRect* r, VanVec2* out_uv_min, VanVec2* out_uv_max) const    { *out_uv_min = r->uv0; *out_uv_max = r->uv1; }             // OBSOLETED in 1.92.0
    VANGUI_API VanFontAtlasRectId AddCustomRectFontGlyph(VanFont* font, VanWchar codepoint, int w, int h, float advance_x, const VanVec2& offset = VanVec2(0, 0));                            // OBSOLETED in 1.92.0: Use custom VanFontLoader in VanFontConfig
    VANGUI_API VanFontAtlasRectId AddCustomRectFontGlyphForSize(VanFont* font, float font_size, VanWchar codepoint, int w, int h, float advance_x, const VanVec2& offset = VanVec2(0, 0));    // ADDED AND OBSOLETED in 1.92.0
#endif
    //unsigned int                      FontBuilderFlags;        // OBSOLETED in 1.92.0: Renamed to FontLoaderFlags.
    //int                               TexDesiredWidth;         // OBSOLETED in 1.92.0: Force texture width before calling Build(). Must be a power-of-two. If have many glyphs your graphics API have texture size restrictions you may want to increase texture width to decrease height.
    //typedef VanFontAtlasRect           VanFontAtlasCustomRect;   // OBSOLETED in 1.92.0
    //typedef VanFontAtlasCustomRect     CustomRect;              // OBSOLETED in 1.72+
    //typedef VanFontGlyphRangesBuilder  GlyphRangesBuilder;      // OBSOLETED in 1.67+
};

// Font runtime data for a given size
// Important: pointers to VanFontBaked are only valid for the current frame.
struct VanFontBaked
{
    // [Internal] Members: Hot ~20/24 bytes (for CalcTextSize)
    VanVector<float>             IndexAdvanceX;      // 12-16 // out // Sparse. Glyphs->AdvanceX in a directly indexable way (cache-friendly for CalcTextSize functions which only this info, and are often bottleneck in large UI).
    float                       FallbackAdvanceX;   // 4     // out // FindGlyph(FallbackChar)->AdvanceX
    float                       Size;               // 4     // in  // Height of characters/line, set during loading (doesn't change after loading)
    float                       RasterizerDensity;  // 4     // in  // Density this is baked at

    // [Internal] Members: Hot ~28/36 bytes (for RenderText loop)
    VanVector<VanU16>             IndexLookup;        // 12-16 // out // Sparse. Index glyphs by Unicode code-point.
    VanVector<VanFontGlyph>       Glyphs;             // 12-16 // out // All glyphs.
    int                         FallbackGlyphIndex; // 4     // out // Index of FontFallbackChar

    // [Internal] Members: Cold
    float                       Ascent, Descent;    // 4+4   // out // Ascent: distance from top to bottom of e.g. 'A' [0..FontSize] (unscaled)
    unsigned int                MetricsTotalSurface:26;// 3  // out // Total surface in pixels to get an idea of the font rasterization/texture cost (not exact, we approximate the cost of padding between glyphs)
    unsigned int                WantDestroy:1;         // 0  //     // Queued for destroy
    unsigned int                LoadNoFallback:1;      // 0  //     // Disable loading fallback in lower-level calls.
    unsigned int                LoadNoRenderOnLayout:1;// 0  //     // Enable a two-steps mode where CalcTextSize() calls will load AdvanceX *without* rendering/packing glyphs. Only advantageous if you know that the glyph is unlikely to actually be rendered, otherwise it is slower because we'd do one query on the first CalcTextSize and one query on the first Draw.
    int                         LastUsedFrame;         // 4  //     // Record of that time this was bounds
    VanGuiID                     BakedId;            // 4     //     // Unique ID for this baked storage
    VanFont*                     OwnerFont;          // 4-8   // in  // Parent font
    void*                       FontLoaderDatas;    // 4-8   //     // Font loader opaque storage (per baked font * sources): single contiguous buffer allocated by vangui, passed to loader.

    // Functions
    VANGUI_API VanFontBaked();
    VANGUI_API void              ClearOutputData();
    [[nodiscard]] VANGUI_API VanFontGlyph*      FindGlyph(VanWchar c);               // Return U+FFFD glyph if requested glyph doesn't exists.
    [[nodiscard]] VANGUI_API VanFontGlyph*      FindGlyphNoFallback(VanWchar c);     // Return nullptr if glyph doesn't exist
    VANGUI_API float             GetCharAdvance(VanWchar c);
    VANGUI_API bool              IsGlyphLoaded(VanWchar c);
};

// Font flags
// (in future versions as we redesign font loading API, this will become more important and better documented. for now please consider this as internal/advanced use)
enum VanFontFlags_
{
    VanFontFlags_None                    = 0,
    VanFontFlags_NoLoadError             = 1 << 1,   // Disable throwing an error/assert when calling AddFontXXX() with missing file/data. Calling code is expected to check AddFontXXX() return value.
    VanFontFlags_NoLoadGlyphs            = 1 << 2,   // [Internal] Disable loading new glyphs.
    VanFontFlags_LockBakedSizes          = 1 << 3,   // [Internal] Disable loading new baked sizes, disable garbage collecting current ones. e.g. if you want to lock a font to a single size. Important: if you use this to preload given sizes, consider the possibility of multiple font density used on Retina display.
    VanFontFlags_ImplicitRefSize         = 1 << 4,   // [Internal] Reference size was not set explicitly.
};

// Font runtime data and rendering
// - VanFontAtlas automatically loads a default embedded font for you if you didn't load one manually.
// - Since 1.92.0 a font may be rendered as any size! Therefore a font doesn't have one specific size.
// - Use 'font->GetFontBaked(size)' to retrieve the VanFontBaked* corresponding to a given size.
// - If you used g.Font + g.FontSize (which is frequent from the VanGui layer), you can use g.FontBaked as a shortcut, as g.FontBaked == g.Font->GetFontBaked(g.FontSize).
struct VanFont
{
    // [Internal] Members: Hot ~12-20 bytes
    VanFontBaked*                LastBaked;          // 4-8   // Cache last bound baked. NEVER USE DIRECTLY. Use GetFontBaked().
    VanFontAtlas*                OwnerAtlas;         // 4-8   // What we have been loaded into.
    VanFontFlags                 Flags;              // 4     // Font flags.
    float                       CurrentRasterizerDensity;    // Current rasterizer density. This is a varying state of the font.

    // [Internal] Members: Cold ~24-52 bytes
    // Conceptually Sources[] is the list of font sources merged to create this font.
    VanGuiID                     FontId;             // Unique identifier for the font
    float                       LegacySize;         // 4     // in  // Font size passed to AddFont(). Use for old code calling PushFont() expecting to use that size. (use VanGui::GetFontBaked() to get font baked at current bound size).
    VanVector<VanFontConfig*>     Sources;            // 16    // in  // List of sources. Pointers within OwnerAtlas->Sources[]
    VanWchar                     EllipsisChar;       // 2-4   // out // Character used for ellipsis rendering ('...'). If you ever want to temporarily swap this for an alternative/dummy char, make sure to clear EllipsisAutoBake.
    VanWchar                     FallbackChar;       // 2-4   // out // Character used if a glyph isn't found (U+FFFD, '?')
    VanU8                        Used8kPagesMap[(VAN_UNICODE_CODEPOINT_MAX+1)/8192/8]; // 1 bytes if VanWchar=VanWchar16, 17 bytes if VanWchar==VanWchar32. Store 1-bit for each block of 8K codepoints that has one active glyph. This is mainly used to facilitate iterations across all used codepoints.
    bool                        EllipsisAutoBake;   // 1     //     // Mark when the "..." glyph (== EllipsisChar) needs to be generated by combining multiple '.'.
    VanGuiStorage                RemapPairs;         // 16    //     // Remapping pairs when using AddRemapChar(), otherwise empty.
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    float                       Scale;              // 4     // in  // Legacy base font scale (~1.0f), multiplied by the per-window font scale which you can adjust with SetWindowFontScale()
#endif

    // Methods
    VANGUI_API VanFont();
    VANGUI_API ~VanFont();
    VANGUI_API bool              IsGlyphInFont(VanWchar c);
    bool                        IsLoaded() const                { return OwnerAtlas != nullptr; }
    const char*                 GetDebugName() const            { return Sources.Size ? Sources[0]->Name : "<unknown>"; } // Fill VanFontConfig::Name.

    // [Internal] Don't use!
    // 'max_width' stops rendering after a certain width (could be turned into a 2d size). FLT_MAX to disable.
    // 'wrap_width' enable automatic word-wrapping across multiple lines to fit into given width. 0.0f to disable.
    [[nodiscard]] VANGUI_API VanFontBaked*      GetFontBaked(float font_size, float density = -1.0f);  // Get or create baked data for given size
    VANGUI_API VanVec2            CalcTextSizeA(float size, float max_width, float wrap_width, const char* text_begin, const char* text_end = nullptr, const char** out_remaining = nullptr);
    [[nodiscard]] VANGUI_API const char*       CalcWordWrapPosition(float size, const char* text, const char* text_end, float wrap_width);
    VANGUI_API void              RenderChar(VanDrawList* draw_list, float size, const VanVec2& pos, VanU32 col, VanWchar c, const VanVec4* cpu_fine_clip = nullptr);
    VANGUI_API void              RenderText(VanDrawList* draw_list, float size, const VanVec2& pos, VanU32 col, const VanVec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width = 0.0f, VanDrawTextFlags flags = 0);
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    inline const char*          CalcWordWrapPositionA(float scale, const char* text, const char* text_end, float wrap_width) { return CalcWordWrapPosition(LegacySize * scale, text, text_end, wrap_width); }
#endif

    // [Internal] Don't use!
    VANGUI_API void              ClearOutputData();
    VANGUI_API void              AddRemapChar(VanWchar from_codepoint, VanWchar to_codepoint); // Makes 'from_codepoint' character points to 'to_codepoint' glyph.
    VANGUI_API bool              IsGlyphRangeUnused(unsigned int c_begin, unsigned int c_last);
};

// This is provided for consistency (but we don't actually use this)
inline VanTextureID VanTextureRef::GetTexID() const
{
    VAN_ASSERT(!(_TexData != nullptr && _TexID != VanTextureID_Invalid));
    return _TexData ? _TexData->TexID : _TexID;
}

// Using an indirection to avoid patching VanDrawCmd after a SetTexID() call (but this could be an alternative solution too)
inline VanTextureID VanDrawCmd::GetTexID() const
{
    // If you are getting this assert with VanTextureID_Invalid == 0 and your VanTextureID is used to store an index or an offset:
    // - You can add '#define VanTextureID_Invalid ((VanTextureID)-1)' in your vanconfig.h file.
    // If you are getting this assert with a renderer backend with support for VanGuiBackendFlags_RendererHasTextures (1.92+):
    // - You must correctly iterate and handle VanTextureData requests stored in VanDrawData::Textures[]. See docs/BACKENDS.md.
    VanTextureID tex_id = TexRef._TexData ? TexRef._TexData->TexID : TexRef._TexID; // == TexRef.GetTexID() above.
    if (TexRef._TexData != nullptr)
        VAN_ASSERT(tex_id != VanTextureID_Invalid && "VanDrawCmd is referring to VanTextureData that wasn't uploaded to graphics system. Backend must call VanTextureData::SetTexID() after handling VanTextureStatus_WantCreate request!");
    return tex_id;
}

//-----------------------------------------------------------------------------
// [SECTION] Viewports
//-----------------------------------------------------------------------------

// Flags stored in VanGuiViewport::Flags, giving indications to the platform backends.
enum VanGuiViewportFlags_
{
    VanGuiViewportFlags_None                    = 0,
    VanGuiViewportFlags_IsPlatformWindow        = 1 << 0,   // Represent a Platform Window
    VanGuiViewportFlags_IsPlatformMonitor       = 1 << 1,   // Represent a Platform Monitor (unused yet)
    VanGuiViewportFlags_OwnedByApp              = 1 << 2,   // Platform Window: Is created/managed by the user application? (rather than our backend)
    VanGuiViewportFlags_NoDecoration            = 1 << 3,   // Platform Window: Disable platform decorations: title bar, borders, etc.
    VanGuiViewportFlags_NoTaskBarIcon           = 1 << 4,   // Platform Window: Disable platform task bar icon (for popups, menus, or all windows with NoDecoration)
    VanGuiViewportFlags_NoFocusOnAppearing      = 1 << 5,   // Platform Window: Don't take focus when created.
    VanGuiViewportFlags_NoFocusOnClick          = 1 << 6,   // Platform Window: Don't take focus when clicked on.
    VanGuiViewportFlags_NoInputs                = 1 << 7,   // Platform Window: Make mouse pass through so we can drag this window while peaking behind it.
    VanGuiViewportFlags_NoRendererClear         = 1 << 8,   // Platform Window: Renderer doesn't need to clear the framebuffer ahead (because we will fill it entirely).
    VanGuiViewportFlags_NoAutoMerge             = 1 << 9,   // Platform Window: Avoid merging this window into another host window. This can only be set via VanGuiWindowClass viewport flags override (instead of tweaking this flag directly).
    VanGuiViewportFlags_TopMost                 = 1 << 10,  // Platform Window: Display on top (for tooltips only).
    VanGuiViewportFlags_CanHostOtherWindows     = 1 << 11,  // Viewport can host multiple VanGui windows (secondary viewports are associated to a single window). // FIXME: In practice there's still probably code making the assumption that this is always and only the main viewport.
    VanGuiViewportFlags_IsMinimized             = 1 << 12,  // Platform Window: Window is minimized, can skip render. When minimized we tend to avoid using the viewport pos/size for clipping window or testing if they are contained in the viewport.
    VanGuiViewportFlags_IsFocused               = 1 << 13,  // Platform Window: Window is focused (last call to Platform_GetWindowFocus() returned true)
};

// [Internal] Storage used by IsKeyDown(), IsKeyPressed() etc functions.
// If prior to 1.87 you used VanGuiIO::KeysDown[512] you can simply use VanGui::IsKeyDown(VanGuiKey_XXX).
// [SECTION] Docking support
struct VanGuiWindowClass
{
    VanGuiID             ClassId                    = 0;            // User data. 0 = Default class (unclassed). Windows of different classes cannot be docked with each others.
    VanGuiID             ParentViewportId            = static_cast<VanGuiID>(-1); // Hint for the platform backend. -1: use default. 0: request platform backend to not parent the platform. != 0: request platform backend to create a parent<>child relationship between the platform windows. Not conforming backends are free to e.g. parent every viewport to the main viewport or not.
    VanGuiID             FocusRouteParentWindowId    = 0;            // ID of parent window for shortcut focus route evaluation, e.g. Shortcut() call from Parent Window will succeed when this window is focused.
    VanGuiViewportFlags  ViewportFlagsOverrideSet    = 0;            // Viewport flags to set when a window of this class owns a viewport. This allows you to enforce OS decoration or task bar icon, override the defaults on a per-window basis.
    VanGuiViewportFlags  ViewportFlagsOverrideClear  = 0;            // Viewport flags to clear when a window of this class owns a viewport.
    VanGuiTabItemFlags   TabItemFlagsOverrideSet     = 0;            // [EXPERIMENTAL] TabItem flags to set when a window of this class gets submitted into a dock node tab bar. May use with VanGuiTabItemFlags_Leading or VanGuiTabItemFlags_Trailing.
    VanGuiDockNodeFlags  DockNodeFlagsOverrideSet    = 0;            // [EXPERIMENTAL] Dock node flags to set when a window of this class is hosted by a dock node (it doesn't have to be selected!)
    bool                 DockingAlwaysTabBar         = false;        // Set to true to enforce single-tab-bar on docking node (when set, docking nodes can only be merged in the tab bar direction, and any two dockspaces with this flag set cannot be merged together).
    bool                 DockingAllowUnclassed       = true;         // Set to true to allow windows of unknown class to be docked.
};

// - Currently represents the Platform Window created by the application which is hosting our VanGUI windows.
// - In 'docking' branch with multi-viewport enabled, we extend this concept to have multiple active viewports.
// - In the future we will extend this concept further to also represent Platform Monitor and support a "no main platform window" operation mode.
// - About Main Area vs Work Area:
//   - Main Area = entire viewport.
//   - Work Area = entire viewport minus sections used by main menu bars (for platform windows), or by task bar (for platform monitor).
//   - Windows are generally trying to stay within the Work Area of their host viewport.
struct VanGuiViewport
{
    VanGuiID             ID                  = 0;       // Unique identifier for the viewport
    VanGuiViewportFlags  Flags               = 0;       // See VanGuiViewportFlags_
    VanVec2              Pos;                           // Main Area: Position of the viewport (VanGUI coordinates are the same as OS desktop/native coordinates)
    VanVec2              Size;                          // Main Area: Size of the viewport.
    VanVec2              WorkPos;                       // Work Area: Position of the viewport minus task bars, menus bars, status bars (>= Pos)
    VanVec2              WorkSize;                      // Work Area: Size of the viewport minus task bars, menu bars, status bars (<= Size)
    float                DpiScale            = 1.0f;    // 1.0f = 96 DPI = No extra scale.
    VanGuiID             ParentViewportId    = 0;       // (Advanced) 0: no parent. Instruct the platform backend to setup a parent/child relationship between platform windows.
    VanDrawData*         DrawData            = nullptr; // The VanDrawData corresponding to this viewport. Valid after Render() and until the next call to NewFrame().

    // Platform/Backend Dependent Data
    // Our design separate the Renderer and Platform backends to facilitate combining default backends with each other.
    // When our create your own backend for a custom engine, it is possible that both Renderer and Platform will be handled by the same system and you may not need to use all the UserData/Handle fields.
    // The library never uses those fields, they are merely storage to facilitate backend implementation.
    void*                RendererUserData    = nullptr; // void* to hold custom data structure for the renderer (e.g. swap chain, framebuffers etc.). generally set by your Renderer_CreateWindow function.
    void*                PlatformUserData    = nullptr; // void* to hold custom data structure for the OS / platform (e.g. windowing info, render context). generally set by your Platform_CreateWindow function.
    void*                PlatformHandle      = nullptr; // void* for FindViewportByPlatformHandle(). (e.g. suggested to use natural platform handle such as HWND, GLFWWindow*, SDL_Window*)
    void*                PlatformHandleRaw   = nullptr; // void* to hold lower-level, platform-native window handle (under Win32 this is expected to be a HWND, unused for other platforms), when using an abstraction layer like GLFW or SDL (where PlatformHandle would be a SDL_Window*)
    bool                 PlatformWindowCreated = false; // Platform window has been created (Platform_CreateWindow() has been called). This is false during the first frame where a viewport is created.
    bool                 PlatformRequestMove   = false; // Platform window requested move (e.g. window was moved by the OS / host window manager, authoritative position will be OS window position)
    bool                 PlatformRequestResize = false; // Platform window requested resize (e.g. window was resized by the OS / host window manager, authoritative size will be OS window size)
    bool                 PlatformRequestClose  = false; // Platform window requested closure (e.g. window was moved by the OS / host window manager, e.g. pressing ALT-F4)

    [[nodiscard]] VanVec2 GetCenter() const noexcept      { return VanVec2(Pos.x + Size.x * 0.5f, Pos.y + Size.y * 0.5f); }
    [[nodiscard]] VanVec2 GetWorkCenter() const noexcept  { return VanVec2(WorkPos.x + WorkSize.x * 0.5f, WorkPos.y + WorkSize.y * 0.5f); }
};

//-----------------------------------------------------------------------------
// [SECTION] Platform Dependent Interfaces
//-----------------------------------------------------------------------------

// (Optional) This is required when enabling multi-viewport. Represent the bounds of each connected monitor/display and their DPI.
// We use this information for multiple DPI support + clamping the position of popups and tooltips so they don't straddle multiple monitors.
struct VanGuiPlatformMonitor
{
    VanVec2  MainPos;           // Coordinates of the area displayed on this monitor (Min = upper left, Max = lower right)
    VanVec2  MainSize;          // Coordinates of the area displayed on this monitor (Min = upper left, Max = lower right)
    VanVec2  WorkPos;           // Coordinates without task bars / side bars. ImVec2(0, 0) if MonitorHasWorkArea == false.
    VanVec2  WorkSize;          // Coordinates without task bars / side bars. (0, 0) if MonitorHasWorkArea == false.
    float    DpiScale = 1.0f;   // 1.0f = 96 DPI
    void*    PlatformHandle = nullptr; // Backend dependant data (e.g. HMONITOR, GLFWmonitor*, SDL Display Index, NSScreen*)

    VanGuiPlatformMonitor() { MainPos = MainSize = WorkPos = WorkSize = VanVec2(0.0f, 0.0f); }
};

// Access via VanGui::GetPlatformIO(). Read comments for details.
struct VanGuiPlatformIO
{
    VANGUI_API VanGuiPlatformIO();

    //------------------------------------------------------------------
    // Input - Backend interface/functions + Monitor List
    //------------------------------------------------------------------

    // (Optional) Platform functions (e.g. Win32, GLFW, SDL2)
    // For reference, the second column shows which function are generally calling the Platform Functions:
    //   N = VanGui::NewFrame()                        ~ beginning of the VanGui frame: read info from platform/OS windows (latest size/position)
    //   F = VanGui::Begin(), VanGui::EndFrame()       ~ during the VanGui frame
    //   U = VanGui::UpdatePlatformWindows()           ~ after the VanGui frame: create and update all platform windows
    //   R = VanGui::RenderPlatformWindowsDefault()    ~ render
    //   D = VanGui::DestroyPlatformWindows()          ~ application shutdown: destroy all platform windows
    void    (*Platform_CreateWindow)(VanGuiViewport* vp)                    = nullptr; // . . U . .  // Create a new platform window for the given viewport
    void    (*Platform_DestroyWindow)(VanGuiViewport* vp)                   = nullptr; // N . U . D  //
    void    (*Platform_ShowWindow)(VanGuiViewport* vp)                      = nullptr; // . . U . .  // Newly created windows are initially hidden so SetWindowPos/Size/Title can be called on them before showing the window
    void    (*Platform_SetWindowPos)(VanGuiViewport* vp, VanVec2 pos)       = nullptr; // . . U . .  // Set platform window position (given the upper-left corner of client area)
    VanVec2 (*Platform_GetWindowPos)(VanGuiViewport* vp)                    = nullptr; // N . . . .  //
    void    (*Platform_SetWindowSize)(VanGuiViewport* vp, VanVec2 size)     = nullptr; // . . U . .  // Set platform window client area size (ignoring OS decorations such as OS title bar etc.)
    VanVec2 (*Platform_GetWindowSize)(VanGuiViewport* vp)                   = nullptr; // N . . . .  // Get platform window client area size
    void    (*Platform_SetWindowFocus)(VanGuiViewport* vp)                  = nullptr; // N . . . .  // Move window to front and set input focus
    bool    (*Platform_GetWindowFocus)(VanGuiViewport* vp)                  = nullptr; // . . . . .  //
    bool    (*Platform_GetWindowMinimized)(VanGuiViewport* vp)              = nullptr; // N . . . .  // Get platform window minimized state. When minimized, we generally won't attempt to get/set size and contents will be culled more easily
    void    (*Platform_SetWindowTitle)(VanGuiViewport* vp, const char* str) = nullptr; // . . U . .  // Set platform window title (given an UTF-8 string)
    void    (*Platform_SetWindowAlpha)(VanGuiViewport* vp, float alpha)     = nullptr; // . . U . .  // (Optional) Setup global transparency (not per-pixel, not supported by all backends). Generally this is held in VanGuiViewport::Alpha but it could be stored by the backend as well.
    void    (*Platform_UpdateWindow)(VanGuiViewport* vp)                    = nullptr; // . . U . .  // (Optional) Called by UpdatePlatformWindows(). Optional hook to allow the platform backend to copy back their window position/size back to the viewport. Sometimes it is easier to keep a flag to notify the backend that the viewport has changed vs reading back the data every frame.
    void    (*Platform_RenderWindow)(VanGuiViewport* vp, void* render_arg)  = nullptr; // . . . R .  // (Optional) Main rendering (platform side! This is often unused, or just setting up a "current" context for OpenGL bindings). 'render_arg' is the value passed to RenderPlatformWindowsDefault().
    void    (*Platform_SwapBuffers)(VanGuiViewport* vp, void* render_arg)   = nullptr; // . . . R .  // (Optional) Call Present/SwapBuffers (platform side! This is often unused!). 'render_arg' is the value passed to RenderPlatformWindowsDefault().
    float   (*Platform_GetWindowDpiScale)(VanGuiViewport* vp)               = nullptr; // N . . . .  // (Optional) [BETA] FIXME-DPI: Implement on Windows, override some monitor DPI data
    void    (*Platform_OnChangedViewport)(VanGuiViewport* vp)               = nullptr; // . F . . .  // (Optional) [BETA] FIXME-DPI: DPI handling: called during Begin() when h/w framebuffer DPI changes (no longer just os/screen dpi)
    int     (*Platform_CreateVkSurface)(VanGuiViewport* vp, VanU64 vk_inst, const void* vk_allocators, VanU64* out_vk_surface) = nullptr; // (Optional) For a Vulkan Renderer to call into Platform code (since the surface creation needs to tie them both).

    // (Optional) Renderer functions (e.g. DirectX, OpenGL, Vulkan)
    void    (*Renderer_CreateWindow)(VanGuiViewport* vp)                    = nullptr; // . . U . .  // Create swap chain, frame buffers etc. (called after Platform_CreateWindow)
    void    (*Renderer_DestroyWindow)(VanGuiViewport* vp)                   = nullptr; // N . U . D  // Destroy swap chain, frame buffers etc. (called before Platform_DestroyWindow)
    void    (*Renderer_SetWindowSize)(VanGuiViewport* vp, VanVec2 size)     = nullptr; // . . U . .  // Resize swap chain, frame buffers etc. (called after Platform_SetWindowSize)
    void    (*Renderer_RenderWindow)(VanGuiViewport* vp, void* render_arg)  = nullptr; // . . . R .  // (Optional) Clear framebuffer, setup render target, then render the viewport->DrawData. 'render_arg' is the value passed to RenderPlatformWindowsDefault().
    void    (*Renderer_SwapBuffers)(VanGuiViewport* vp, void* render_arg)   = nullptr; // . . . R .  // (Optional) Call Present/SwapBuffers. 'render_arg' is the value passed to RenderPlatformWindowsDefault().

    // (Optional) Monitor list
    // - Updated by: app/backend. Update every frame to dynamically support changing monitor or DPI configuration.
    // - Used by: VanGUI to query DPI info, clamp popups/tooltips within same monitor and not have them straddle monitors.
    VanVector<VanGuiPlatformMonitor>  Monitors;

    //------------------------------------------------------------------
    // Output - List of viewports to render into platform windows
    //------------------------------------------------------------------
    // Viewport list (the list is updated by calling VanGui::NewFrame or VanGui::UpdatePlatformWindows)
    // (in the future we will attempt to organize this feature to remove the need for a "main" viewport)
    VanVector<VanGuiViewport*>        Viewports;                            // Main viewports, followed by all secondary viewports.

    //------------------------------------------------------------------
    // Input - Interface with OS and Platform backend (most common stuff)
    //------------------------------------------------------------------

    // Optional: Access OS clipboard
    // (default to use native Win32 clipboard on Windows, otherwise uses a private clipboard. Override to access OS clipboard on other architectures)
    const char* (*Platform_GetClipboardTextFn)(VanGuiContext* ctx);                      // Should return nullptr on failure (e.g. clipboard data is not text).
    void        (*Platform_SetClipboardTextFn)(VanGuiContext* ctx, const char* text);
    void*       Platform_ClipboardUserData;

    // Optional: Open link/folder/file in OS Shell
    // (default to use ShellExecuteW() on Windows, system() on Linux/Mac. expected to return false on failure, but some platforms may always return true)
    bool        (*Platform_OpenInShellFn)(VanGuiContext* ctx, const char* path);
    void*       Platform_OpenInShellUserData;

    // Optional: Notify OS Input Method Editor of the screen position of your cursor for text input position (e.g. when using Japanese/Chinese IME on Windows)
    // (default to use native imm32 api on Windows)
    void        (*Platform_SetImeDataFn)(VanGuiContext* ctx, VanGuiViewport* viewport, VanGuiPlatformImeData* data);
    void*       Platform_ImeUserData;
    //void      (*SetPlatformImeDataFn)(VanGuiViewport* viewport, VanGuiPlatformImeData* data); // [Renamed to platform_io.PlatformSetImeDataFn in 1.91.1]

    // Optional: Platform locale
    // [Experimental] Configure decimal point e.g. '.' or ',' useful for some languages (e.g. German), generally pulled from *localeconv()->decimal_point
    VanWchar     Platform_LocaleDecimalPoint;     // '.'

    //------------------------------------------------------------------
    // Input - Interface with Renderer Backend
    //------------------------------------------------------------------

    // Optional: Maximum texture size supported by renderer (used to adjust how we size textures). 0 if not known.
    int         Renderer_TextureMaxWidth;
    int         Renderer_TextureMaxHeight;

    // Written by some backends during VanGui_ImplXXXX_RenderDrawData() call to point backend_specific VanGui_ImplXXXX_RenderState* structure.
    void*       Renderer_RenderState;

    // Standard draw callbacks provided by renderer backend.
    VanDrawCallback  DrawCallback_ResetRenderState;      // Request to reset the graphics/render state.
    VanDrawCallback  DrawCallback_SetSamplerLinear;      // Request backend to set texture sampling to Linear.
    VanDrawCallback  DrawCallback_SetSamplerNearest;     // Request backend to set texture sampling to Nearest/Point.
    //VanDrawCallback  DrawCallback_SetSamplerCustom;    // Request backend to set texture sampling using Backend Specific data.

    //------------------------------------------------------------------
    // Output
    //------------------------------------------------------------------

    // Textures list (the list is updated by calling VanGui::EndFrame or VanGui::Render)
    // The VanGui_ImplXXXX_RenderDrawData() function of each backend generally access this via VanDrawData::Textures which points to this. The array is available here mostly because backends will want to destroy textures on shutdown.
    VanVector<VanTextureData*>        Textures;           // List of textures used by VanGUI (most often 1) + contents of external texture list is automatically appended into this.

    //------------------------------------------------------------------
    // Functions
    //------------------------------------------------------------------

    VANGUI_API void ClearPlatformHandlers();    // Clear all Platform_XXX fields. Typically called on Platform Backend shutdown.
    VANGUI_API void ClearRendererHandlers();    // Clear all Renderer_XXX fields. Typically called on Renderer Backend shutdown.
};

// (Optional) Support for IME (Input Method Editor) via the platform_io.Platform_SetImeDataFn() function. Handler is called during EndFrame().
struct VanGuiPlatformImeData
{
    bool    WantVisible;            // A widget wants the IME to be visible.
    bool    WantTextInput;          // A widget wants text input, not necessarily IME to be visible. This is automatically set to the upcoming value of io.WantTextInput.
    VanVec2  InputPos;               // Position of input cursor (for IME).
    float   InputLineHeight;        // Line height (for IME).
    VanGuiID ViewportId;             // ID of platform window/viewport.

    VanGuiPlatformImeData()          { memset((void*)this, 0, sizeof(*this)); } // POD-safe zero-init
};

//-----------------------------------------------------------------------------
// [SECTION] Obsolete functions and types
// (Will be removed! Read 'API BREAKING CHANGES' section in vangui.cpp for details)
// Please keep your copy of VanGUI up to date! Occasionally set '#define VANGUI_DISABLE_OBSOLETE_FUNCTIONS' in vanconfig.h to stay ahead.
//-----------------------------------------------------------------------------

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
namespace VanGui
{
    // OBSOLETED in 1.92.0 (from June 2025)
    inline void         PushFont(VanFont* font)                                  { PushFont(font, font ? font->LegacySize : 0.0f); }
    VANGUI_API void      SetWindowFontScale(float scale);                        // Set font scale factor for current window. Prefer using PushFont(nullptr, style.FontSizeBase * factor) or use style.FontScaleMain to scale all windows.
    // OBSOLETED in 1.91.9 (from February 2025)
    VANGUI_API void      Image(VanTextureRef tex_ref, const VanVec2& image_size, const VanVec2& uv0, const VanVec2& uv1, const VanVec4& tint_col, const VanVec4& border_col); // <-- 'border_col' was removed in favor of VanGuiCol_ImageBorder. If you use 'tint_col', use ImageWithBg() instead.
    // OBSOLETED in 1.91.0 (from July 2024)
    inline void         PushButtonRepeat(bool repeat)                           { PushItemFlag(VanGuiItemFlags_ButtonRepeat, repeat); }
    inline void         PopButtonRepeat()                                       { PopItemFlag(); }
    inline void         PushTabStop(bool tab_stop)                              { PushItemFlag(VanGuiItemFlags_NoTabStop, !tab_stop); }
    inline void         PopTabStop()                                            { PopItemFlag(); }
    // You do not need those functions! See #7838 on GitHub for more info.
    VANGUI_API VanVec2    GetContentRegionMax();                                  // Content boundaries max (e.g. window boundaries including scrolling, or current column boundaries). You should never need this. Always use GetCursorScreenPos() and GetContentRegionAvail()!
    VANGUI_API VanVec2    GetWindowContentRegionMin();                            // Content boundaries min for the window (roughly (0,0)-Scroll), in window-local coordinates. You should never need this. Always use GetCursorScreenPos() and GetContentRegionAvail()!
    VANGUI_API VanVec2    GetWindowContentRegionMax();                            // Content boundaries max for the window (roughly (0,0)+Size-Scroll), in window-local coordinates. You should never need this. Always use GetCursorScreenPos() and GetContentRegionAvail()!

    // Some of the older obsolete names along with their replacement (commented out so they are not reported in IDE)
    // OBSOLETED in 1.90.0 (from September 2023)
    //VANGUI_API bool      Combo(const char* label, int* current_item, bool (*old_callback)(void* user_data, int idx, const char** out_text), void* user_data, int items_count, int popup_max_height_in_items = -1); // Getter signature changed. See 2023/09/15 and 2026/02/27 commits.
    //VANGUI_API bool      ListBox(const char* label, int* current_item, bool (*old_callback)(void* user_data, int idx, const char** out_text), void* user_data, int items_count, int height_in_items = -1);         // Getter signature changed. See 2023/09/15 and 2026/02/27 commits.
    //inline bool         BeginChild(const char* str_id, const VanVec2& size_arg, bool borders, VanGuiWindowFlags window_flags) { return BeginChild(str_id, size_arg, borders ? VanGuiChildFlags_Borders : VanGuiChildFlags_None, window_flags); } // Unnecessary as true == VanGuiChildFlags_Borders
    //inline bool         BeginChild(VanGuiID id, const VanVec2& size_arg, bool borders, VanGuiWindowFlags window_flags)         { return BeginChild(id, size_arg, borders ? VanGuiChildFlags_Borders : VanGuiChildFlags_None, window_flags);     } // Unnecessary as true == VanGuiChildFlags_Borders
    //inline bool         BeginChildFrame(VanGuiID id, const VanVec2& size, VanGuiWindowFlags flags = 0) { return BeginChild(id, size, VanGuiChildFlags_FrameStyle, flags); }
    //inline void         EndChildFrame()                                                             { EndChild(); }
    //inline void         ShowStackToolWindow(bool* p_open = nullptr)                                    { ShowIDStackToolWindow(p_open); }
    // OBSOLETED in 1.89.7 (from June 2023)
    //VANGUI_API void      SetItemAllowOverlap();                                                      // Use SetNextItemAllowOverlap() _before_ item.
    //-- OBSOLETED in 1.89.4 (from March 2023)
    //static inline void  PushAllowKeyboardFocus(bool tab_stop)                                       { PushItemFlag(VanGuiItemFlags_NoTabStop, !tab_stop); }
    //static inline void  PopAllowKeyboardFocus()                                                     { PopItemFlag(); }
    //-- OBSOLETED in 1.89 (from August 2022)
    //VANGUI_API bool      ImageButton(VanTextureID user_texture_id, const VanVec2& size, const VanVec2& uv0 = VanVec2(0, 0), const VanVec2& uv1 = VanVec2(1, 1), int frame_padding = -1, const VanVec4& bg_col = VanVec4(0, 0, 0, 0), const VanVec4& tint_col = VanVec4(1, 1, 1, 1)); // --> Use new ImageButton() signature (explicit item id, regular FramePadding). Refer to code in 1.91 if you want to grab a copy of this version.
    //-- OBSOLETED in 1.88 (from May 2022)
    //static inline void  CaptureKeyboardFromApp(bool want_capture_keyboard = true)                   { SetNextFrameWantCaptureKeyboard(want_capture_keyboard); } // Renamed as name was misleading + removed default value.
    //static inline void  CaptureMouseFromApp(bool want_capture_mouse = true)                         { SetNextFrameWantCaptureMouse(want_capture_mouse); }       // Renamed as name was misleading + removed default value.
    //-- OBSOLETED in 1.87 (from February 2022, more formally obsoleted April 2024)
    //VANGUI_API VanGuiKey  GetKeyIndex(VanGuiKey key);                                                  { VAN_ASSERT(key >= VanGuiKey_NamedKey_BEGIN && key < VanGuiKey_NamedKey_END); const VanGuiKeyData* key_data = GetKeyData(key); return (VanGuiKey)(key_data - g.IO.KeysData); } // Map VanGuiKey_* values into legacy native key index. == io.KeyMap[key]. When using a 1.87+ backend using io.AddKeyEvent(), calling GetKeyIndex() with ANY VanGuiKey_XXXX values will return the same value!
    //static inline VanGuiKey GetKeyIndex(VanGuiKey key)                                                { VAN_ASSERT(key >= VanGuiKey_NamedKey_BEGIN && key < VanGuiKey_NamedKey_END); return key; }
    //-- OBSOLETED in 1.86 (from November 2021)
    //VANGUI_API void      CalcListClipping(int items_count, float items_height, int* out_items_display_start, int* out_items_display_end); // Code removed, see 1.90 for last version of the code. Calculate range of visible items for large list of evenly sized items. Prefer using VanGuiListClipper.
    //-- OBSOLETED in 1.85 (from August 2021)
    //static inline float GetWindowContentRegionWidth()                                               { return GetWindowContentRegionMax().x - GetWindowContentRegionMin().x; }
    //-- OBSOLETED in 1.81 (from February 2021)
    //static inline bool  ListBoxHeader(const char* label, const VanVec2& size = VanVec2(0, 0))         { return BeginListBox(label, size); }
    //static inline bool  ListBoxHeader(const char* label, int items_count, int height_in_items = -1) { float height = GetTextLineHeightWithSpacing() * ((height_in_items < 0 ? VanMin(items_count, 7) : height_in_items) + 0.25f) + GetStyle().FramePadding.y * 2.0f; return BeginListBox(label, VanVec2(0.0f, height)); } // Helper to calculate size from items_count and height_in_items
    //static inline void  ListBoxFooter()                                                             { EndListBox(); }
    //-- OBSOLETED in 1.79 (from August 2020)
    //static inline void  OpenPopupContextItem(const char* str_id = nullptr, VanGuiMouseButton mb = 1)    { OpenPopupOnItemClick(str_id, mb); } // Bool return value removed. Use IsWindowAppearing() in BeginPopup() instead. Renamed in 1.77, renamed back in 1.79. Sorry!
    //-- OBSOLETED in 1.78 (from June 2020): Old drag/sliders functions that took a 'float power > 1.0f' argument instead of VanGuiSliderFlags_Logarithmic.
    //VANGUI_API bool      DragScalar(const char* label, VanGuiDataType data_type, void* p_data, float v_speed, const void* p_min, const void* p_max, const char* format, float power = 1.0f)                                                            // OBSOLETED in 1.78 (from June 2020)
    //VANGUI_API bool      DragScalarN(const char* label, VanGuiDataType data_type, void* p_data, int components, float v_speed, const void* p_min, const void* p_max, const char* format, float power = 1.0f);                                          // OBSOLETED in 1.78 (from June 2020)
    //VANGUI_API bool      SliderScalar(const char* label, VanGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, float power = 1.0f);                                                                        // OBSOLETED in 1.78 (from June 2020)
    //VANGUI_API bool      SliderScalarN(const char* label, VanGuiDataType data_type, void* p_data, int components, const void* p_min, const void* p_max, const char* format, float power = 1.0f);                                                       // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  DragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, float power = 1.0f)    { return DragScalar(label, VanGuiDataType_Float, v, v_speed, &v_min, &v_max, format, power); }     // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  DragFloat2(const char* label, float v[2], float v_speed, float v_min, float v_max, const char* format, float power = 1.0f) { return DragScalarN(label, VanGuiDataType_Float, v, 2, v_speed, &v_min, &v_max, format, power); } // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  DragFloat3(const char* label, float v[3], float v_speed, float v_min, float v_max, const char* format, float power = 1.0f) { return DragScalarN(label, VanGuiDataType_Float, v, 3, v_speed, &v_min, &v_max, format, power); } // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  DragFloat4(const char* label, float v[4], float v_speed, float v_min, float v_max, const char* format, float power = 1.0f) { return DragScalarN(label, VanGuiDataType_Float, v, 4, v_speed, &v_min, &v_max, format, power); } // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, float power = 1.0f)                 { return SliderScalar(label, VanGuiDataType_Float, v, &v_min, &v_max, format, power); }            // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format, float power = 1.0f)              { return SliderScalarN(label, VanGuiDataType_Float, v, 2, &v_min, &v_max, format, power); }        // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format, float power = 1.0f)              { return SliderScalarN(label, VanGuiDataType_Float, v, 3, &v_min, &v_max, format, power); }        // OBSOLETED in 1.78 (from June 2020)
    //static inline bool  SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format, float power = 1.0f)              { return SliderScalarN(label, VanGuiDataType_Float, v, 4, &v_min, &v_max, format, power); }        // OBSOLETED in 1.78 (from June 2020)
    //-- OBSOLETED in 1.77 and before
    //static inline bool  BeginPopupContextWindow(const char* str_id, VanGuiMouseButton mb, bool over_items) { return BeginPopupContextWindow(str_id, mb | (over_items ? 0 : VanGuiPopupFlags_NoOpenOverItems)); } // OBSOLETED in 1.77 (from June 2020)
    //static inline void  TreeAdvanceToLabelPos()               { SetCursorPosX(GetCursorPosX() + GetTreeNodeToLabelSpacing()); }   // OBSOLETED in 1.72 (from July 2019)
    //static inline void  SetNextTreeNodeOpen(bool open, VanGuiCond cond = 0) { SetNextItemOpen(open, cond); }                       // OBSOLETED in 1.71 (from June 2019)
    //static inline float GetContentRegionAvailWidth()          { return GetContentRegionAvail().x; }                               // OBSOLETED in 1.70 (from May 2019)
    //static inline VanDrawList* GetOverlayDrawList()            { return GetForegroundDrawList(); }                                 // OBSOLETED in 1.69 (from Mar 2019)
    //static inline void  SetScrollHere(float ratio = 0.5f)     { SetScrollHereY(ratio); }                                          // OBSOLETED in 1.66 (from Nov 2018)
    //static inline bool  IsItemDeactivatedAfterChange()        { return IsItemDeactivatedAfterEdit(); }                            // OBSOLETED in 1.63 (from Aug 2018)
    //-- OBSOLETED in 1.60 and before
    //static inline bool  IsAnyWindowFocused()                  { return IsWindowFocused(VanGuiFocusedFlags_AnyWindow); }            // OBSOLETED in 1.60 (from Apr 2018)
    //static inline bool  IsAnyWindowHovered()                  { return IsWindowHovered(VanGuiHoveredFlags_AnyWindow); }            // OBSOLETED in 1.60 (between Dec 2017 and Apr 2018)
    //static inline void  ShowTestWindow()                      { return ShowDemoWindow(); }                                        // OBSOLETED in 1.53 (between Oct 2017 and Dec 2017)
    //static inline bool  IsRootWindowFocused()                 { return IsWindowFocused(VanGuiFocusedFlags_RootWindow); }           // OBSOLETED in 1.53 (between Oct 2017 and Dec 2017)
    //static inline bool  IsRootWindowOrAnyChildFocused()       { return IsWindowFocused(VanGuiFocusedFlags_RootAndChildWindows); }  // OBSOLETED in 1.53 (between Oct 2017 and Dec 2017)
    //static inline void  SetNextWindowContentWidth(float w)    { SetNextWindowContentSize(VanVec2(w, 0.0f)); }                      // OBSOLETED in 1.53 (between Oct 2017 and Dec 2017)
    //static inline float GetItemsLineHeightWithSpacing()       { return GetFrameHeightWithSpacing(); }                             // OBSOLETED in 1.53 (between Oct 2017 and Dec 2017)
    //VANGUI_API bool      Begin(char* name, bool* p_open, VanVec2 size_first_use, float bg_alpha = -1.0f, VanGuiWindowFlags flags=0); // OBSOLETED in 1.52 (between Aug 2017 and Oct 2017): Equivalent of using SetNextWindowSize(size, VanGuiCond_FirstUseEver) and SetNextWindowBgAlpha().
    //static inline bool  IsRootWindowOrAnyChildHovered()       { return IsWindowHovered(VanGuiHoveredFlags_RootAndChildWindows); }  // OBSOLETED in 1.52 (between Aug 2017 and Oct 2017)
    //static inline void  AlignFirstTextHeightToWidgets()       { AlignTextToFramePadding(); }                                      // OBSOLETED in 1.52 (between Aug 2017 and Oct 2017)
    //static inline void  SetNextWindowPosCenter(VanGuiCond c=0) { SetNextWindowPos(GetMainViewport()->GetCenter(), c, VanVec2(0.5f,0.5f)); } // OBSOLETED in 1.52 (between Aug 2017 and Oct 2017)
    //static inline bool  IsItemHoveredRect()                   { return IsItemHovered(VanGuiHoveredFlags_RectOnly); }               // OBSOLETED in 1.51 (between Jun 2017 and Aug 2017)
    //static inline bool  IsPosHoveringAnyWindow(const VanVec2&) { VAN_ASSERT(0); return false; }                                     // OBSOLETED in 1.51 (between Jun 2017 and Aug 2017): This was misleading and partly broken. You probably want to use the io.WantCaptureMouse flag instead.
    //static inline bool  IsMouseHoveringAnyWindow()            { return IsWindowHovered(VanGuiHoveredFlags_AnyWindow); }            // OBSOLETED in 1.51 (between Jun 2017 and Aug 2017)
    //static inline bool  IsMouseHoveringWindow()               { return IsWindowHovered(VanGuiHoveredFlags_AllowWhenBlockedByPopup | VanGuiHoveredFlags_AllowWhenBlockedByActiveItem); }       // OBSOLETED in 1.51 (between Jun 2017 and Aug 2017)
    //-- OBSOLETED in 1.50 and before
    //static inline bool  CollapsingHeader(char* label, const char* str_id, bool framed = true, bool default_open = false) { return CollapsingHeader(label, (default_open ? (1 << 5) : 0)); } // OBSOLETED in 1.49
    //static inline VanFont*GetWindowFont()                      { return GetFont(); }                                               // OBSOLETED in 1.48
    //static inline float GetWindowFontSize()                   { return GetFontSize(); }                                           // OBSOLETED in 1.48
    //static inline void  SetScrollPosHere()                    { SetScrollHere(); }                                                // OBSOLETED in 1.42
}

#define VanDrawCallback_ResetRenderState     (VanDrawCallback)(-8)    // OBSOLETED in 1.92.8: Use VanGui::GetPlatformIO().DrawCallback_ResetRenderState

//-- OBSOLETED in 1.92.0: VanFontAtlasCustomRect becomes VanTextureRect
// - VanFontAtlasCustomRect::X,Y          --> VanTextureRect::x,y
// - VanFontAtlasCustomRect::Width,Height --> VanTextureRect::w,h
// - VanFontAtlasCustomRect::GlyphColored --> if you need to write to this, instead you can write to 'font->Glyphs.back()->Colored' after calling AddCustomRectFontGlyph()
// We could make VanTextureRect an union to use old names, but 1) this would be confusing 2) the fix is easy 3) VanFontAtlasCustomRect was always a rather esoteric api.
using VanFontAtlasCustomRect = VanFontAtlasRect;
/*struct VanFontAtlasCustomRect
{
    unsigned short  X, Y;           // Output   // Packed position in Atlas
    unsigned short  Width, Height;  // Input    // [Internal] Desired rectangle dimension
    unsigned int    GlyphID:31;     // Input    // [Internal] For custom font glyphs only (ID < 0x110000)
    unsigned int    GlyphColored:1; // Input    // [Internal] For custom font glyphs only: glyph is colored, removed tinting.
    float           GlyphAdvanceX;  // Input    // [Internal] For custom font glyphs only: glyph xadvance
    VanVec2          GlyphOffset;    // Input    // [Internal] For custom font glyphs only: glyph display offset
    VanFont*         Font;           // Input    // [Internal] For custom font glyphs only: target font
    VanFontAtlasCustomRect()         { X = Y = 0xFFFF; Width = Height = 0; GlyphID = 0; GlyphColored = 0; GlyphAdvanceX = 0.0f; GlyphOffset = VanVec2(0, 0); Font = nullptr; }
    bool IsPacked() const           { return X != 0xFFFF; }
};*/

//-- OBSOLETED in 1.82 (from Mars 2021): flags for AddRect(), AddRectFilled(), AddImageRounded(), PathRect()
//typedef VanDrawFlags VanDrawCornerFlags;
//enum VanDrawCornerFlags_
//{
//    VanDrawCornerFlags_None      = VanDrawFlags_RoundCornersNone,         // Was == 0 prior to 1.82, this is now == VanDrawFlags_RoundCornersNone which is != 0 and not implicit
//    VanDrawCornerFlags_TopLeft   = VanDrawFlags_RoundCornersTopLeft,      // Was == 0x01 (1 << 0) prior to 1.82. Order matches VanDrawFlags_NoRoundCorner* flag (we exploit this internally).
//    VanDrawCornerFlags_TopRight  = VanDrawFlags_RoundCornersTopRight,     // Was == 0x02 (1 << 1) prior to 1.82.
//    VanDrawCornerFlags_BotLeft   = VanDrawFlags_RoundCornersBottomLeft,   // Was == 0x04 (1 << 2) prior to 1.82.
//    VanDrawCornerFlags_BotRight  = VanDrawFlags_RoundCornersBottomRight,  // Was == 0x08 (1 << 3) prior to 1.82.
//    VanDrawCornerFlags_All       = VanDrawFlags_RoundCornersAll,          // Was == 0x0F prior to 1.82
//    VanDrawCornerFlags_Top       = VanDrawCornerFlags_TopLeft | VanDrawCornerFlags_TopRight,
//    VanDrawCornerFlags_Bot       = VanDrawCornerFlags_BotLeft | VanDrawCornerFlags_BotRight,
//    VanDrawCornerFlags_Left      = VanDrawCornerFlags_TopLeft | VanDrawCornerFlags_BotLeft,
//    VanDrawCornerFlags_Right     = VanDrawCornerFlags_TopRight | VanDrawCornerFlags_BotRight,
//};

// RENAMED and MERGED both VanGuiKey_ModXXX and VanGuiModFlags_XXX into VanGuiMod_XXX (from September 2022)
// RENAMED VanGuiKeyModFlags -> VanGuiModFlags in 1.88 (from April 2022). Exceptionally commented out ahead of obsolescence schedule to reduce confusion and because they were not meant to be used in the first place.
//typedef VanGuiKeyChord VanGuiModFlags;      // == int. We generally use VanGuiKeyChord to mean "a VanGuiKey or-ed with any number of VanGuiMod_XXX value", so you may store mods in there.
//enum VanGuiModFlags_ { VanGuiModFlags_None = 0, VanGuiModFlags_Ctrl = VanGuiMod_Ctrl, VanGuiModFlags_Shift = VanGuiMod_Shift, VanGuiModFlags_Alt = VanGuiMod_Alt, VanGuiModFlags_Super = VanGuiMod_Super };
//typedef VanGuiKeyChord VanGuiKeyModFlags; // == int
//enum VanGuiKeyModFlags_ { VanGuiKeyModFlags_None = 0, VanGuiKeyModFlags_Ctrl = VanGuiMod_Ctrl, VanGuiKeyModFlags_Shift = VanGuiMod_Shift, VanGuiKeyModFlags_Alt = VanGuiMod_Alt, VanGuiKeyModFlags_Super = VanGuiMod_Super };

//#define VAN_OFFSETOF(_TYPE,_MEMBER)  offsetof(_TYPE, _MEMBER)  // OBSOLETED IN 1.90 (now using C++11 standard version)

#endif // #ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS

#define VAN_ARRAYSIZE                VAN_COUNTOF                  // RENAMED IN 1.92.6: VAN_ARRAYSIZE -> VAN_COUNTOF

// RENAMED VANGUI_DISABLE_METRICS_WINDOW > VANGUI_DISABLE_DEBUG_TOOLS in 1.88 (from June 2022)
#ifdef VANGUI_DISABLE_METRICS_WINDOW
#error VANGUI_DISABLE_METRICS_WINDOW was renamed to VANGUI_DISABLE_DEBUG_TOOLS, please use new name.
#endif

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _MSC_VER
#pragma warning (pop)
#endif

// Include vangui_user.h at the end of vangui.h
// May be convenient for some users to only explicitly include vanilla vangui.h and have extra stuff included.
#ifdef VANGUI_INCLUDE_VANGUI_USER_H
#ifdef VANGUI_USER_H_FILENAME
#include VANGUI_USER_H_FILENAME
#else
#include "vangui_user.h"
#endif
#endif

#endif // #ifndef VANGUI_DISABLE
