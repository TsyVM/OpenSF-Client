#include "Engine/Asset/ImageDecode.hpp"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#else
// Elsewhere (Android): stb's decoders and PNG writer (vendor/stb, public domain).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include <stb_image.h>
#include <stb_image_write.h>
#endif

#include <algorithm>
#include <cstring>

namespace eng {

namespace {

#ifdef _WIN32
struct ComInit {
    bool owns = false;
    ComInit() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns = SUCCEEDED(hr);
    }
    ~ComInit() {
        if (owns) CoUninitialize();
    }
};

template <typename T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() { return p; }
    explicit operator bool() const { return p != nullptr; }
};
#endif

bool looks_like_tga(std::span<const u8> b) {
    if (b.size() < 18) return false;
    u8 cmap = b[1], type = b[2], bpp = b[16];
    bool known = type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11;
    return known && cmap <= 1 && (bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32);
}

}  // namespace

bool decode_tga(std::span<const u8> b, Image& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (b.size() < 18) return fail("tga: truncated header");
    u8 id_len = b[0], cmap_type = b[1], type = b[2];
    u16 cmap_first = u16(b[3] | b[4] << 8), cmap_len = u16(b[5] | b[6] << 8);
    u8 cmap_bits = b[7];
    u16 w = u16(b[12] | b[13] << 8), h = u16(b[14] | b[15] << 8);
    u8 bpp = b[16], desc = b[17];
    if (w == 0 || h == 0) return fail("tga: empty");
    size_t at = 18 + id_len;
    std::vector<u8> palette;
    if (cmap_type == 1) {
        size_t entry = (cmap_bits + 7) / 8;
        size_t bytes = entry * cmap_len;
        if (at + bytes > b.size()) return fail("tga: truncated palette");
        palette.assign(b.begin() + at, b.begin() + at + bytes);
        at += bytes;
    }
    const bool rle = type >= 9;
    const u8 base = rle ? u8(type - 8) : type;
    const size_t pixel_bytes = (bpp + 7) / 8;
    size_t count = size_t(w) * h;
    out.width = w;
    out.height = h;
    out.rgba.assign(count * 4, 255);

    auto read_pixel = [&](const u8* p, u8* dst) {
        auto from_bits = [&](const u8* q, size_t nbytes, u8 bits) {
            if (nbytes == 1) {
                dst[0] = dst[1] = dst[2] = q[0];
                dst[3] = 255;
            } else if (nbytes == 2) {
                u16 v = u16(q[0] | q[1] << 8);
                dst[2] = u8(((v) & 31) * 255 / 31);
                dst[1] = u8(((v >> 5) & 31) * 255 / 31);
                dst[0] = u8(((v >> 10) & 31) * 255 / 31);
                (void)bits;
                dst[3] = 255;
            } else {
                dst[0] = q[2];
                dst[1] = q[1];
                dst[2] = q[0];
                dst[3] = nbytes == 4 ? q[3] : 255;
            }
        };
        if (base == 1) {
            size_t index = (pixel_bytes == 2 ? size_t(p[0] | p[1] << 8) : p[0]);
            index = index >= cmap_first ? index - cmap_first : 0;
            size_t entry = (cmap_bits + 7) / 8;
            if (entry == 0 || (index + 1) * entry > palette.size()) {
                dst[0] = dst[1] = dst[2] = 0;
                dst[3] = 255;
                return;
            }
            from_bits(palette.data() + index * entry, entry, cmap_bits);
        } else if (base == 3) {
            dst[0] = dst[1] = dst[2] = p[0];
            dst[3] = pixel_bytes == 2 ? p[1] : 255;
        } else {
            from_bits(p, pixel_bytes, bpp);
        }
    };

    std::vector<u8> pixels(count * 4);
    size_t i = 0;
    while (i < count) {
        if (rle) {
            if (at >= b.size()) return fail("tga: truncated rle");
            u8 packet = b[at++];
            size_t n = size_t(packet & 0x7F) + 1;
            if (packet & 0x80) {
                if (at + pixel_bytes > b.size()) return fail("tga: truncated rle");
                u8 px[4];
                read_pixel(b.data() + at, px);
                at += pixel_bytes;
                for (size_t k = 0; k < n && i < count; ++k, ++i) std::memcpy(pixels.data() + i * 4, px, 4);
            } else {
                for (size_t k = 0; k < n && i < count; ++k, ++i) {
                    if (at + pixel_bytes > b.size()) return fail("tga: truncated rle");
                    read_pixel(b.data() + at, pixels.data() + i * 4);
                    at += pixel_bytes;
                }
            }
        } else {
            if (at + pixel_bytes * count > b.size()) return fail("tga: truncated pixels");
            for (; i < count; ++i, at += pixel_bytes) read_pixel(b.data() + at, pixels.data() + i * 4);
        }
    }

    const bool top_down = desc & 0x20;
    const bool right_to_left = desc & 0x10;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u32 sy = top_down ? y : h - 1 - y;
            u32 sx = right_to_left ? w - 1 - x : x;
            std::memcpy(out.rgba.data() + (size_t(y) * w + x) * 4, pixels.data() + (size_t(sy) * w + sx) * 4, 4);
        }

    // Many tools write 32-bit files with an all-zero alpha channel: treat as opaque.
    if (bpp == 32) {
        bool any = false;
        for (size_t k = 0; k < count && !any; ++k) any = out.rgba[k * 4 + 3] != 0;
        if (!any)
            for (size_t k = 0; k < count; ++k) out.rgba[k * 4 + 3] = 255;
    }
    return true;
}

bool decode_image(std::span<const u8> bytes, Image& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "DDS ", 4) != 0 && looks_like_tga(bytes)) {
        // PNG/JPEG/BMP signatures never look like TGA headers; try WIC first only for those.
        bool known_sig = (bytes[0] == 0x89 && bytes[1] == 'P') || (bytes[0] == 0xFF && bytes[1] == 0xD8) ||
                         (bytes[0] == 'B' && bytes[1] == 'M') || (bytes[0] == 'G' && bytes[1] == 'I');
        if (!known_sig) return decode_tga(bytes, out, error);
    }

#ifndef _WIN32
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
    if (!px) return fail(stbi_failure_reason() ? stbi_failure_reason() : "unrecognised image format");
    out.width = u32(w);
    out.height = u32(h);
    out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return true;
#else
    ComInit com;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return fail("WIC unavailable");
    Com<IStream> stream;
    stream.p = SHCreateMemStream(bytes.data(), UINT(bytes.size()));
    if (!stream) return fail("out of memory");
    Com<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
        return fail("unrecognised image format");
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return fail("no image frame");
    Com<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return fail("no converter");
    if (FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        return fail("pixel conversion failed");
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) return fail("empty image");
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, UINT(out.rgba.size()), out.rgba.data())))
        return fail("pixel copy failed");
    return true;
#endif
}

std::vector<u8> encode_png(const Image& image) {
    std::vector<u8> result;
#ifndef _WIN32
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            auto* r = static_cast<std::vector<u8>*>(ctx);
            r->insert(r->end(), static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
        },
        &result, int(image.width), int(image.height), 4, image.rgba.data(), int(image.width * 4));
    return result;
#else
    ComInit com;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return result;
    Com<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return result;
    Com<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return result;
    encoder->Initialize(stream.p, WICBitmapEncoderNoCache);
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(&frame, &props))) return result;
    frame->Initialize(props.p);
    frame->SetSize(image.width, image.height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    frame->SetPixelFormat(&format);
    std::vector<u8> pixels = image.rgba;
    if (format != GUID_WICPixelFormat32bppRGBA) {
        // Fall back to BGRA.
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    }
    frame->WritePixels(image.height, image.width * 4, UINT(pixels.size()), pixels.data());
    frame->Commit();
    encoder->Commit();
    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    result.resize(size_t(stat.cbSize.QuadPart));
    ULONG read = 0;
    stream->Read(result.data(), ULONG(result.size()), &read);
    result.resize(read);
    return result;
#endif
}

Image resize_image(const Image& in, u32 width, u32 height) {
    Image out;
    out.width = std::max<u32>(width, 1);
    out.height = std::max<u32>(height, 1);
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (u32 y = 0; y < out.height; ++y) {
        u32 y0 = u32(u64(y) * in.height / out.height);
        u32 y1 = std::max(y0 + 1, u32(u64(y + 1) * in.height / out.height));
        for (u32 x = 0; x < out.width; ++x) {
            u32 x0 = u32(u64(x) * in.width / out.width);
            u32 x1 = std::max(x0 + 1, u32(u64(x + 1) * in.width / out.width));
            u32 sum[4] = {0, 0, 0, 0};
            u32 n = 0;
            for (u32 sy = y0; sy < y1 && sy < in.height; ++sy)
                for (u32 sx = x0; sx < x1 && sx < in.width; ++sx, ++n)
                    for (int c = 0; c < 4; ++c) sum[c] += in.rgba[(size_t(sy) * in.width + sx) * 4 + c];
            for (int c = 0; c < 4; ++c) out.rgba[(size_t(y) * out.width + x) * 4 + c] = u8(n ? sum[c] / n : 0);
        }
    }
    return out;
}

}  // namespace eng
