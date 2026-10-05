// Soldier Front's pictures: DDS (DXT1/3/5 and uncompressed), TGA, JPEG, PNG and BMP.
//
// The decoder is picked from the bytes, never the name: several shipped ".jpg" files are
// PNGs, and CrossRoads' texture table names .jpg files the archives ship only as .dds.
#pragma once

#include "Engine/Asset/ImageDecode.hpp"
#include "Engine/Core/Types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace sf {

using eng::u32;
using eng::u64;
using eng::u8;

// A texture the way a graphics card takes it: RGBA8 with a full mip chain, or a DDS's own
// DXT blocks and mips passed through untouched.
struct Texture {
    enum class Kind : u8 { Rgba8, Bc1, Bc2, Bc3 };
    Kind kind = Kind::Rgba8;
    u32 width = 0, height = 0;
    std::vector<std::vector<u8>> mips;   // largest first
    bool cutout = false;                 // alpha is a mask to test (fences, leaves, decals)
    bool translucent = false;            // alpha varies smoothly (glass, smoke): blend it
    bool invisible = false;              // nothing opaque at all: the surface is not drawn
    u32 row_pitch(size_t mip) const;     // bytes per row (of 4x4 blocks when compressed)
    size_t bytes() const;
};

// Any of the formats above into top-down RGBA8.
bool decode_image(std::span<const std::byte> bytes, eng::Image& out, std::string* error = nullptr);

// Into a GPU-ready texture. `compressed_ok` keeps a DDS's blocks (every desktop card, and the
// OpenGL ES device decodes them where the phone cannot); otherwise everything is RGBA8.
bool load_texture(std::span<const std::byte> bytes, Texture& out, bool compressed_ok = true, std::string* error = nullptr);

// An RGBA8 texture from a decoded image, with mips made by box filtering.
Texture texture_from_image(const eng::Image& image, bool mips = true);

// Alpha survey: a mask, a smooth blend, or nothing opaque at all.
void classify_alpha(const eng::Image& image, bool& cutout, bool& translucent, bool& invisible);

// One block-compressed level (bc = 1, 2 or 3 for DXT1, DXT3, DXT5) into RGBA8.
bool decode_bc_level(const u8* src, size_t size, u32 width, u32 height, int bc, eng::Image& out);

// Pure-black keyed art (the lobby's .bmp atlases have no alpha): black becomes transparent.
void key_black(eng::Image& image, u8 threshold = 8);
// The old atlases key on pure blue (0,0,255) or pure red (255,0,0) filler; those become transparent.
void key_colour(eng::Image& image, u8 r, u8 g, u8 b, u8 tolerance = 24);
// The same filler, but only where it reaches the picture's edge: the lobby's .bmp pictures
// (rank badges, the server banner) sit on pure blue and several use that blue inside the art
// too (Division_10's taeguk). The keyed pixels take their opaque neighbours' colour, so a
// scaled picture has no blue fringe.
void key_colour_from_edges(eng::Image& image, u8 r, u8 g, u8 b, u8 tolerance = 12);

}  // namespace sf
