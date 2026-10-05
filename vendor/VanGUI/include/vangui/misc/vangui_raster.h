// vangui_raster.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — CPU rasterizer for VanGUI draw data.
//
// Turns a frame's VanDrawData (or any VanDrawList) into pixels without a GPU:
// triangles with per-vertex colour, font-atlas texturing (bilinear), scissor
// rectangles and the same straight-alpha "over" blend every VanGUI renderer
// backend uses. Pixel centres, top-left fill rule — so shared edges are never
// covered twice, which is exactly what the vector module's no-overlap strokes
// need to be checked against.
//
// Uses:
//   * headless screenshots and golden-image tests (null backend + this);
//   * baking vector icons into font glyphs (vangui_icons);
//   * "save this panel as an image" features in tools.
//
// PNG I/O is self-contained (zlib deflate/inflate included): WritePNG writes
// compressed 8-bit RGBA; ReadPNG reads 8-bit RGBA/RGB/grey(+alpha) PNGs.
//
// Opt-in / zero-cost via VANGUI_ENABLE_RASTER.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

namespace VanGui {

// An RGBA8 image in VanGUI's packed colour layout (VAN_COL32), straight alpha.
struct VanImage
{
    int               Width  = 0;
    int               Height = 0;
    VanVector<VanU32> Pixels;

    void   Create(int w, int h, VanU32 fill = 0) { Width = w; Height = h; Pixels.resize(w * h); for (int i = 0; i < w * h; ++i) Pixels[i] = fill; }
    VanU32 Get(int x, int y) const { return (x >= 0 && y >= 0 && x < Width && y < Height) ? Pixels[y * Width + x] : 0; }
    void   Set(int x, int y, VanU32 c) { if (x >= 0 && y >= 0 && x < Width && y < Height) Pixels[y * Width + x] = c; }
    bool   Empty() const { return Width <= 0 || Height <= 0; }
};

// Resolves a user texture (one the draw data refers to only by VanTextureID) to
// pixels. Return nullptr to draw it as solid white.
typedef const VanImage* (*VanRasterTextureResolver)(VanTextureID id, void* user_data);

struct VanRasterOptions
{
    VanU32                   ClearColor = VAN_COL32(0, 0, 0, 255);   // background the frame is composited over
    bool                     Clear      = true;                      // false: draw over what `out` already holds (same size)
    VanRasterTextureResolver Resolve    = nullptr;
    void*                    ResolveUserData = nullptr;
};

#ifdef VANGUI_ENABLE_RASTER

// Rasterize a whole frame. The image is DisplaySize × FramebufferScale.
VANGUI_API void RasterizeDrawData(const VanDrawData* draw_data, VanImage& out, const VanRasterOptions& opt = {});
// Rasterize one draw list into `out`, mapping `origin` to pixel (0,0) and scaling by `scale`.
VANGUI_API void RasterizeDrawList(const VanDrawList* draw_list, VanImage& out, const VanVec2& origin, float scale = 1.0f,
                                  const VanRasterOptions& opt = {});
// Coverage only: rasterize triangles [idx_begin, end) of `draw_list` into an 8-bit mask
// (vertex alpha × texture alpha, composited with "over"). For baking vector art into
// glyphs. `dst` must be pre-cleared by the caller.
VANGUI_API void RasterizeCoverage(const VanDrawList* draw_list, int idx_begin, unsigned char* dst, int w, int h, int pitch,
                                  const VanVec2& origin, float scale = 1.0f);

// Convenience: rasterize the current context's last rendered frame (VanGui::GetDrawData()).
VANGUI_API bool CaptureFrame(VanImage& out, const VanRasterOptions& opt = {});
// Crop `src` to the rectangle [x, y, x+w, y+h) (clamped).
VANGUI_API void CropImage(const VanImage& src, int x, int y, int w, int h, VanImage& out);

VANGUI_API bool WritePNG(const char* path, const VanImage& img);
VANGUI_API bool ReadPNG(const char* path, VanImage& out);
VANGUI_API bool EncodePNG(const VanImage& img, VanVector<unsigned char>& out_bytes);
VANGUI_API bool DecodePNG(const unsigned char* data, int size, VanImage& out);

// Compare two images. Returns the fraction of pixels whose largest channel difference
// exceeds `tolerance` (0..255). Different sizes compare as 1.0.
VANGUI_API float CompareImages(const VanImage& a, const VanImage& b, int tolerance = 8, int* out_max_diff = nullptr);

#else // ----------------------------- shims -----------------------------------

inline void RasterizeDrawData(const VanDrawData*, VanImage&, const VanRasterOptions& = {}) {}
inline void RasterizeDrawList(const VanDrawList*, VanImage&, const VanVec2&, float = 1.0f, const VanRasterOptions& = {}) {}
inline void RasterizeCoverage(const VanDrawList*, int, unsigned char*, int, int, int, const VanVec2&, float = 1.0f) {}
inline bool CaptureFrame(VanImage&, const VanRasterOptions& = {}) { return false; }
inline void CropImage(const VanImage&, int, int, int, int, VanImage&) {}
inline bool WritePNG(const char*, const VanImage&) { return false; }
inline bool ReadPNG(const char*, VanImage&) { return false; }
inline bool EncodePNG(const VanImage&, VanVector<unsigned char>&) { return false; }
inline bool DecodePNG(const unsigned char*, int, VanImage&) { return false; }
inline float CompareImages(const VanImage&, const VanImage&, int = 8, int* = nullptr) { return 1.0f; }

#endif // VANGUI_ENABLE_RASTER

} // namespace VanGui
