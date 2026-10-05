// VanGUI: FreeType font builder (used as a replacement for the stb_truetype builder)
// (headers)

#pragma once
#include <vangui/vangui.h>      // VANGUI_API
#ifndef VANGUI_DISABLE

// Usage:
// - Add '#define VANGUI_ENABLE_FREETYPE' in your vanconfig to automatically enable support
//   for vangui_freetype in vangui. It is equivalent to selecting the default loader with:
//      io.Fonts->SetFontLoader(VanGuiFreeType::GetFontLoader())

// Optional support for OpenType SVG fonts:
// - Add '#define VANGUI_ENABLE_FREETYPE_PLUTOSVG' to use plutosvg (not provided). See #7927.
// - Add '#define VANGUI_ENABLE_FREETYPE_LUNASVG' to use lunasvg (not provided). See #6591.

// Forward declarations
struct VanFontAtlas;
struct VanFontLoader;

// Hinting greatly impacts visuals (and glyph sizes).
// - By default, hinting is enabled and the font's native hinter is preferred over the auto-hinter.
// - When disabled, FreeType generates blurrier glyphs, more or less matches the stb_truetype.h
// - The Default hinting mode usually looks good, but may distort glyphs in an unusual way.
// - The Light hinting mode generates fuzzier glyphs but better matches Microsoft's rasterizer.
// You can set those flags globally in VanFontAtlas::FontLoaderFlags
// You can set those flags on a per font basis in VanFontConfig::FontLoaderFlags
typedef unsigned int VanGuiFreeTypeLoaderFlags;
enum VanGuiFreeTypeLoaderFlags_
{
    VanGuiFreeTypeLoaderFlags_NoHinting     = 1 << 0,   // Disable hinting. This generally generates 'blurrier' bitmap glyphs when the glyph are rendered in any of the anti-aliased modes.
    VanGuiFreeTypeLoaderFlags_NoAutoHint    = 1 << 1,   // Disable auto-hinter.
    VanGuiFreeTypeLoaderFlags_ForceAutoHint = 1 << 2,   // Indicates that the auto-hinter is preferred over the font's native hinter.
    VanGuiFreeTypeLoaderFlags_LightHinting  = 1 << 3,   // A lighter hinting algorithm for gray-level modes. Many generated glyphs are fuzzier but better resemble their original shape. This is achieved by snapping glyphs to the pixel grid only vertically (Y-axis), as is done by Microsoft's ClearType and Adobe's proprietary font renderer. This preserves inter-glyph spacing in horizontal text.
    VanGuiFreeTypeLoaderFlags_MonoHinting   = 1 << 4,   // Strong hinting algorithm that should only be used for monochrome output.
    VanGuiFreeTypeLoaderFlags_Bold          = 1 << 5,   // Styling: Should we artificially embolden the font?
    VanGuiFreeTypeLoaderFlags_Oblique       = 1 << 6,   // Styling: Should we slant the font, emulating italic style?
    VanGuiFreeTypeLoaderFlags_Monochrome    = 1 << 7,   // Disable anti-aliasing. Combine this with MonoHinting for best results!
    VanGuiFreeTypeLoaderFlags_LoadColor     = 1 << 8,   // Enable FreeType color-layered glyphs
    VanGuiFreeTypeLoaderFlags_Bitmap        = 1 << 9,   // Enable FreeType bitmap glyphs

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    VanGuiFreeTypeBuilderFlags_NoHinting     = VanGuiFreeTypeLoaderFlags_NoHinting,
    VanGuiFreeTypeBuilderFlags_NoAutoHint    = VanGuiFreeTypeLoaderFlags_NoAutoHint,
    VanGuiFreeTypeBuilderFlags_ForceAutoHint = VanGuiFreeTypeLoaderFlags_ForceAutoHint,
    VanGuiFreeTypeBuilderFlags_LightHinting  = VanGuiFreeTypeLoaderFlags_LightHinting,
    VanGuiFreeTypeBuilderFlags_MonoHinting   = VanGuiFreeTypeLoaderFlags_MonoHinting,
    VanGuiFreeTypeBuilderFlags_Bold          = VanGuiFreeTypeLoaderFlags_Bold,
    VanGuiFreeTypeBuilderFlags_Oblique       = VanGuiFreeTypeLoaderFlags_Oblique,
    VanGuiFreeTypeBuilderFlags_Monochrome    = VanGuiFreeTypeLoaderFlags_Monochrome,
    VanGuiFreeTypeBuilderFlags_LoadColor     = VanGuiFreeTypeLoaderFlags_LoadColor,
    VanGuiFreeTypeBuilderFlags_Bitmap        = VanGuiFreeTypeLoaderFlags_Bitmap,
#endif
};

// Obsolete names (will be removed)
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
typedef VanGuiFreeTypeLoaderFlags_ VanGuiFreeTypeBuilderFlags_;
#endif

namespace VanGuiFreeType
{
    // This is automatically assigned when using '#define VANGUI_ENABLE_FREETYPE'.
    // If you need to dynamically select between multiple builders:
    // - you can manually assign this builder with 'atlas->SetFontLoader(VanGuiFreeType::GetFontLoader())'
    // - prefer deep-copying this into your own VanFontLoader instance if you use hot-reloading that messes up static data.
    VANGUI_API const VanFontLoader*       GetFontLoader();

    // Override allocators. By default VanGuiFreeType will use VAN_ALLOC()/VAN_FREE()
    // However, as FreeType does lots of allocations we provide a way for the user to redirect it to a separate memory heap if desired.
    VANGUI_API void                      SetAllocatorFunctions(void* (*alloc_func)(size_t sz, void* user_data), void (*free_func)(void* ptr, void* user_data), void* user_data = nullptr);

    // Display UI to edit VanFontAtlas::FontLoaderFlags (shared) or VanFontConfig::FontLoaderFlags (single source)
    VANGUI_API bool                      DebugEditFontLoaderFlags(VanGuiFreeTypeLoaderFlags* p_font_loader_flags);

    // Obsolete names (will be removed)
#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
    //VANGUI_API const VanFontBuilderIO* GetBuilderForFreeType(); // Renamed/changed in 1.92. Change 'io.Fonts->FontBuilderIO = VanGuiFreeType::GetBuilderForFreeType()' to 'io.Fonts->SetFontLoader(VanGuiFreeType::GetFontLoader())' if you need runtime selection.
    //static inline bool BuildFontAtlas(VanFontAtlas* atlas, unsigned int flags = 0) { atlas->FontBuilderIO = GetBuilderForFreeType(); atlas->FontLoaderFlags = flags; return atlas->Build(); } // Prefer using '#define VANGUI_ENABLE_FREETYPE'
#endif
}

#endif // #ifndef VANGUI_DISABLE
