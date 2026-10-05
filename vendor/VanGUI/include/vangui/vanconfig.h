//-----------------------------------------------------------------------------
// VANGUI COMPILE-TIME OPTIONS
// Runtime options (clipboard callbacks, enabling various features, etc.) can generally be set via the VanGuiIO structure.
// You can use VanGui::SetAllocatorFunctions() before calling VanGui::CreateContext() to rewire memory allocation functions.
//-----------------------------------------------------------------------------
// A) You may edit vanconfig.h (and not overwrite it when updating VanGUI, or maintain a patch/rebased branch with your modifications to it)
// B) or '#define VANGUI_USER_CONFIG "my_vangui_config.h"' in your project and then add directives in your own file without touching this template.
//-----------------------------------------------------------------------------
// You need to make sure that configuration settings are defined consistently _everywhere_ VanGUI is used, which include the vangui*.cpp
// files but also _any_ of your code that uses VanGUI. This is because some compile-time options have an affect on data structures.
// Defining those options in vanconfig.h will ensure every compilation unit gets to see the same data structure layouts.
// Call VANGUI_CHECKVERSION() from your .cpp file to verify that the data structures your files are using are matching the ones vangui.cpp is using.
//-----------------------------------------------------------------------------

#pragma once

//---- Define assertion handler. Defaults to calling assert().
// - If your macro uses multiple statements, make sure is enclosed in a 'do { .. } while (0)' block so it can be used as a single statement.
// - Compiling with NDEBUG will usually strip out assert() to nothing, which is NOT recommended because we use asserts to notify of programmer mistakes.
//#define VAN_ASSERT(_EXPR)  MyAssert(_EXPR)
//#define VAN_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

//---- Define attributes of all API symbols declarations, e.g. for DLL under Windows
// Using VanGUI via a shared library is not recommended, because of function call overhead and because we don't guarantee backward nor forward ABI compatibility.
// - Windows DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()
//   for each static/DLL boundary you are calling from. Read "Context and Memory Allocators" section of vangui.cpp for more details.
//#define VANGUI_API __declspec(dllexport)                   // MSVC Windows: DLL export
//#define VANGUI_API __declspec(dllimport)                   // MSVC Windows: DLL import
//#define VANGUI_API __attribute__((visibility("default")))  // GCC/Clang: override visibility when set is hidden

//---- Don't define obsolete functions/enums/behaviors. Consider enabling from time to time after updating to clean your code of obsolete function/names.
//#define VANGUI_DISABLE_OBSOLETE_FUNCTIONS

//---- Disable all of VanGUI or don't implement standard windows/tools.
// It is very strongly recommended to NOT disable the demo windows and debug tool during development. They are extremely useful in day to day work. Please read comments in vangui_demo.cpp.
//#define VANGUI_DISABLE                                     // Disable everything: all headers and source files will be empty.
//#define VANGUI_DISABLE_DEMO_WINDOWS                        // Disable demo windows: ShowDemoWindow()/ShowStyleEditor() will be empty.
//#define VANGUI_DISABLE_DEBUG_TOOLS                         // Disable metrics/debugger and other debug tools: ShowMetricsWindow(), ShowDebugLogWindow() and ShowIDStackToolWindow() will be empty.

//---- Don't implement some functions to reduce linkage requirements.
//#define VANGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS   // [Win32] Don't implement default clipboard handler. Won't use and link with OpenClipboard/GetClipboardData/CloseClipboard etc. (user32.lib/.a, kernel32.lib/.a)
//#define VANGUI_ENABLE_WIN32_DEFAULT_IME_FUNCTIONS          // [Win32] [Default with Visual Studio] Implement default IME handler (require imm32.lib/.a, auto-link for Visual Studio, -limm32 on command-line for MinGW)
//#define VANGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS         // [Win32] [Default with non-Visual Studio compilers] Don't implement default IME handler (won't require imm32.lib/.a)
//#define VANGUI_DISABLE_WIN32_FUNCTIONS                     // [Win32] Won't use and link with any Win32 function (clipboard, IME).
//#define VANGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS      // [OSX] Implement default OSX clipboard handler (need to link with '-framework ApplicationServices', this is why this is not the default).
//#define VANGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS             // Don't implement default platform_io.Platform_OpenInShellFn() handler (Win32: ShellExecute(), require shell32.lib/.a, Mac/Linux: use system("")).
//#define VANGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS            // Don't implement VanFormatString/VanFormatStringV so you can implement them yourself (e.g. if you don't want to link with vsnprintf)
//#define VANGUI_DISABLE_DEFAULT_MATH_FUNCTIONS              // Don't implement VanFabs/VanSqrt/VanPow/VanFmod/VanCos/VanSin/VanAcos/VanAtan2 so you can implement them yourself.
//#define VANGUI_DISABLE_FILE_FUNCTIONS                      // Don't implement VanFileOpen/VanFileClose/VanFileRead/VanFileWrite and VanFileHandle at all (replace them with dummies)
//#define VANGUI_DISABLE_DEFAULT_FILE_FUNCTIONS              // Don't implement VanFileOpen/VanFileClose/VanFileRead/VanFileWrite and VanFileHandle so you can implement them yourself if you don't want to link with fopen/fclose/fread/fwrite. This will also disable the LogToTTY() function.
//#define VANGUI_DISABLE_DEFAULT_ALLOCATORS                  // Don't implement default allocators calling malloc()/free() to avoid linking with them. You will need to call VanGui::SetAllocatorFunctions().
//#define VANGUI_DISABLE_DEFAULT_FONT                        // Disable default embedded fonts (ProggyClean/ProggyForever), remove ~9 KB + ~14 KB from output binary. AddFontDefaultXXX() functions will assert.
//#define VANGUI_DISABLE_SSE                                 // Disable use of SSE intrinsics even if available

//---- Enable Test Engine / Automation features.
//#define VANGUI_ENABLE_TEST_ENGINE                          // Enable vangui_test_engine hooks. Generally set automatically by include "vangui_te_config.h", see Test Engine for details.

//---- Include vangui_user.h at the end of vangui.h as a convenience
// May be convenient for some users to only explicitly include vanilla vangui.h and have extra stuff included.
//#define VANGUI_INCLUDE_VANGUI_USER_H
//#define VANGUI_USER_H_FILENAME         "my_folder/my_vangui_user.h"

//---- Pack vertex colors as BGRA8 instead of RGBA8 (to avoid converting from one to another). Need dedicated backend support.
//#define VANGUI_USE_BGRA_PACKED_COLOR

//---- Use legacy CRC32-adler tables (used before 1.91.6), in order to preserve old .ini data that you cannot afford to invalidate.
//#define VANGUI_USE_LEGACY_CRC32_ADLER

//---- Use 32-bit for VanWchar (default is 16-bit) to support Unicode planes 1-16. (e.g. point beyond 0xFFFF like emoticons, dingbats, symbols, shapes, ancient languages, etc...)
//#define VANGUI_USE_WCHAR32

//---- Avoid multiple STB libraries implementations, or redefine path/filenames to prioritize another version
// By default the embedded implementations are declared static and not available outside of VanGUI sources files.
//#define VANGUI_STB_TRUETYPE_FILENAME   "my_folder/stb_truetype.h"
//#define VANGUI_STB_RECT_PACK_FILENAME  "my_folder/stb_rect_pack.h"
//#define VANGUI_STB_SPRINTF_FILENAME    "my_folder/stb_sprintf.h"    // only used if VANGUI_USE_STB_SPRINTF is defined.
//#define VANGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION
//#define VANGUI_DISABLE_STB_RECT_PACK_IMPLEMENTATION
//#define VANGUI_DISABLE_STB_SPRINTF_IMPLEMENTATION                   // only disabled if VANGUI_USE_STB_SPRINTF is defined.

//---- Use stb_sprintf.h for a faster implementation of vsnprintf instead of the one from libc (unless VANGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS is defined)
// Compatibility checks of arguments and formats done by clang and GCC will be disabled in order to support the extra formats provided by stb_sprintf.h.
//#define VANGUI_USE_STB_SPRINTF

//---- Use FreeType to build and rasterize the font atlas (instead of stb_truetype which is embedded by default in VanGUI)
// Requires FreeType headers to be available in the include path. Requires program to be compiled with 'misc/freetype/vangui_freetype.cpp' (in this repository) + the FreeType library (not provided).
// Note that vangui_freetype.cpp may be used _without_ this define, if you manually call VanFontAtlas::SetFontLoader(). The define is simply a convenience.
// On Windows you may use vcpkg with 'vcpkg install freetype --triplet=x64-windows' + 'vcpkg integrate install'.
//#define VANGUI_ENABLE_FREETYPE

//---- Use FreeType + plutosvg or lunasvg to render OpenType SVG fonts (SVGinOT)
// Only works in combination with VANGUI_ENABLE_FREETYPE.
// - plutosvg is currently easier to install, as e.g. it is part of vcpkg. It will support more fonts and may load them faster. See misc/freetype/README for instructions.
// - Both require headers to be available in the include path + program to be linked with the library code (not provided).
// - (note: lunasvg implementation is based on Freetype's rsvg-port.c which is licensed under CeCILL-C Free Software License Agreement)
//#define VANGUI_ENABLE_FREETYPE_PLUTOSVG
//#define VANGUI_ENABLE_FREETYPE_LUNASVG

//---- Use stb_truetype to build and rasterize the font atlas (default)
// The only purpose of this define is if you want force compilation of the stb_truetype backend ALONG with the FreeType backend.
//#define VANGUI_ENABLE_STB_TRUETYPE

//---- Define constructor and implicit cast operators to convert back<>forth between your math types and VanVec2/VanVec4.
// This will be inlined as part of VanVec2 and VanVec4 class declarations.
/*
#define VAN_VEC2_CLASS_EXTRA                                                     \
        constexpr VanVec2(const MyVec2& f) : x(f.x), y(f.y) {}                   \
        operator MyVec2() const { return MyVec2(x,y); }

#define VAN_VEC4_CLASS_EXTRA                                                     \
        constexpr VanVec4(const MyVec4& f) : x(f.x), y(f.y), z(f.z), w(f.w) {}   \
        operator MyVec4() const { return MyVec4(x,y,z,w); }
*/
//---- ...Or use VanGUI's own very basic math operators.
//#define VANGUI_DEFINE_MATH_OPERATORS

//---- Use 32-bit vertex indices (default is 16-bit) is one way to allow large meshes with more than 64K vertices.
// Your renderer backend will need to support it (most example renderer backends support both 16/32-bit indices).
// Another way to allow large meshes while keeping 16-bit indices is to handle VanDrawCmd::VtxOffset in your renderer.
// Read about VanGuiBackendFlags_RendererHasVtxOffset for details.
//#define VanDrawIdx unsigned int

//---- Override VanDrawCallback signature (will need to modify renderer backends accordingly)
//struct VanDrawList;
//struct VanDrawCmd;
//typedef void (*MyImDrawCallback)(const VanDrawList* draw_list, const VanDrawCmd* cmd, void* my_renderer_user_data);
//#define VanDrawCallback MyImDrawCallback

//---- Debug Tools: Macro to break in Debugger (we provide a default implementation of this in the codebase)
// (use 'Metrics->Tools->Item Picker' to pick widgets with the mouse and break into them for easy debugging.)
//#define VAN_DEBUG_BREAK  VAN_ASSERT(0)
//#define VAN_DEBUG_BREAK  __debugbreak()

//---- Debug Tools: Enable highlight ID conflicts _before_ hovering items. When io.ConfigDebugHighlightIdConflicts is set.
// (THIS WILL SLOW DOWN VANGUI. Only use occasionally and disable after use)
//#define VANGUI_DEBUG_HIGHLIGHT_ALL_ID_CONFLICTS

//---- Debug Tools: Enable slower asserts
//#define VANGUI_DEBUG_PARANOID

//---- Tip: You can add extra functions within the VanGui:: namespace from anywhere (e.g. your own sources/header files)
/*
namespace VanGui
{
    void MyFunction(const char* name, MyMatrix44* mtx);
}
*/
