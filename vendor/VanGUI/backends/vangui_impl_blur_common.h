// vangui_impl_blur_common.h
// -----------------------------------------------------------------------------
// Shared by the backdrop-blur handlers (vangui_impl_dx11_blur.cpp,
// vangui_impl_dx12.cpp, vangui_impl_opengl3_blur.cpp): how far to downsample,
// and the Gaussian kernel packed into bilinear taps. Header-only, no API calls.
//
// The blur: the region behind a panel, grown by three sigma so the edges have
// something to blur with, is copied out of the render target, reduced by a power
// of two until sigma is at most kMaxSigma texels, blurred horizontally then
// vertically, and drawn back inside the panel's rounded rectangle. Downsampling
// is what keeps a 30 px blur as cheap as a 5 px one, and the blur itself hides it.
// -----------------------------------------------------------------------------

#pragma once

#include <math.h>

namespace VanBlur {

constexpr float kMaxSigma = 4.0f;   // texels, after downsampling
constexpr int   kMaxTaps  = 12;     // bilinear taps per side, centre included

// Sigma in framebuffer pixels for a requested blur radius.
inline float SigmaFor(float radius) { return radius * 0.5f; }

// The power of two the region is reduced by: sigma / factor <= kMaxSigma, at most 8.
inline int Downsample(float sigma)
{
    int f = 1;
    while (f < 8 && sigma / float(f) > kMaxSigma) f *= 2;
    return f;
}

// How far outside the panel the copy reaches, in framebuffer pixels.
inline float Margin(float sigma) { return ceilf(sigma * 3.0f) + 2.0f; }

// A symmetric Gaussian as bilinear taps: Offsets[0] is 0 (the centre texel) and each later
// tap sits between two texels, weighted so one filtered fetch returns both. Applied as
// Weights[0]*s(0) + sum Weights[i]*(s(+Offsets[i]) + s(-Offsets[i])).
struct Kernel
{
    int   Taps = 1;
    float Offsets[kMaxTaps] = {};
    float Weights[kMaxTaps] = {};
};

inline Kernel MakeKernel(float sigma)
{
    Kernel k;
    if (sigma < 0.3f) { k.Weights[0] = 1.0f; return k; }
    const int reach = (int)ceilf(sigma * 3.0f);   // texels on each side
    float w[64];
    const int n = reach < 63 ? reach : 63;
    float sum = 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        w[i] = expf(-0.5f * float(i * i) / (sigma * sigma));
        sum += i == 0 ? w[i] : 2.0f * w[i];
    }
    for (int i = 0; i <= n; ++i) w[i] /= sum;
    k.Offsets[0] = 0.0f;
    k.Weights[0] = w[0];
    int t = 1;
    for (int i = 1; i <= n && t < kMaxTaps; i += 2, ++t)
    {
        const float a = w[i], b = (i + 1 <= n) ? w[i + 1] : 0.0f;
        k.Weights[t] = a + b;
        k.Offsets[t] = (a + b) > 0.0f ? (float(i) * a + float(i + 1) * b) / (a + b) : float(i);
    }
    k.Taps = t;
    return k;
}

// The pixel rectangle to copy for a panel: the panel grown by the margin, clipped to the
// framebuffer. Returns false when nothing of it is on screen.
struct Region { int X0, Y0, X1, Y1; };

inline bool RegionFor(float x0, float y0, float x1, float y1, float margin, int fb_w, int fb_h, Region* out)
{
    int ax = (int)floorf(x0 - margin), ay = (int)floorf(y0 - margin);
    int bx = (int)ceilf(x1 + margin),  by = (int)ceilf(y1 + margin);
    if (ax < 0) ax = 0;
    if (ay < 0) ay = 0;
    if (bx > fb_w) bx = fb_w;
    if (by > fb_h) by = fb_h;
    if (bx - ax < 2 || by - ay < 2) return false;
    out->X0 = ax; out->Y0 = ay; out->X1 = bx; out->Y1 = by;
    return true;
}

} // namespace VanBlur
