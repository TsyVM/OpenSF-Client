#include "Engine/Audio/Pcm.hpp"

// minimp3 (vendor/minimp3, CC0): the MPEG audio decoder, built here and nowhere else.
#define MINIMP3_IMPLEMENTATION
#include <minimp3.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace eng::audio {

namespace {

u16 rd16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 rd32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

constexpr int kImaSteps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int kImaIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct ImaState {
    int predictor = 0;
    int index = 0;
    i16 step(int nibble) {
        const int s = kImaSteps[index];
        int diff = s >> 3;
        if (nibble & 1) diff += s >> 2;
        if (nibble & 2) diff += s >> 1;
        if (nibble & 4) diff += s;
        if (nibble & 8) diff = -diff;
        predictor = std::clamp(predictor + diff, -32768, 32767);
        index = std::clamp(index + kImaIndex[nibble & 15], 0, 88);
        return i16(predictor);
    }
};

bool decode_ima(const u8* data, size_t bytes, int channels, int block_align, Pcm& out) {
    if (channels < 1 || channels > 2 || block_align < 4 * channels) return false;
    for (size_t at = 0; at + size_t(block_align) <= bytes; at += size_t(block_align)) {
        const u8* b = data + at;
        ImaState st[2];
        for (int c = 0; c < channels; ++c) {
            st[c].predictor = i16(rd16(b + c * 4));
            st[c].index = std::clamp(int(b[c * 4 + 2]), 0, 88);
            out.samples.push_back(i16(st[c].predictor));
        }
        const u8* p = b + 4 * channels;
        const u8* end = b + block_align;
        if (channels == 1) {
            for (; p < end; ++p) {
                out.samples.push_back(st[0].step(*p & 15));
                out.samples.push_back(st[0].step(*p >> 4));
            }
        } else {
            // Stereo: 4 bytes (8 samples) of the left channel, then 4 of the right, repeated.
            while (p + 8 <= end) {
                i16 l[8], r[8];
                for (int k = 0; k < 4; ++k) {
                    l[k * 2] = st[0].step(p[k] & 15);
                    l[k * 2 + 1] = st[0].step(p[k] >> 4);
                    r[k * 2] = st[1].step(p[4 + k] & 15);
                    r[k * 2 + 1] = st[1].step(p[4 + k] >> 4);
                }
                for (int k = 0; k < 8; ++k) {
                    out.samples.push_back(l[k]);
                    out.samples.push_back(r[k]);
                }
                p += 8;
            }
        }
    }
    return !out.samples.empty();
}

}  // namespace

bool decode_wav(std::span<const u8> file, Pcm& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const u8* p = file.data();
    const size_t n = file.size();
    if (n < 12 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return fail("not a RIFF WAVE file");
    int format = 0, channels = 0, rate = 0, bits = 0, block_align = 0;
    const u8* data = nullptr;
    size_t data_bytes = 0;
    for (size_t at = 12; at + 8 <= n;) {
        const u32 size = rd32(p + at + 4);
        const u8* body = p + at + 8;
        const size_t avail = std::min<size_t>(size, n - (at + 8));
        if (std::memcmp(p + at, "fmt ", 4) == 0 && avail >= 16) {
            format = rd16(body);
            channels = rd16(body + 2);
            rate = int(rd32(body + 4));
            block_align = rd16(body + 12);
            bits = rd16(body + 14);
            if (format == 0xFFFE && avail >= 26) format = rd16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub-format
        } else if (std::memcmp(p + at, "data", 4) == 0) {
            data = body;
            data_bytes = avail;
        }
        at += 8 + size + (size & 1);
    }
    if (!data || channels <= 0 || rate <= 0) return fail("no format or data chunk");
    out = Pcm{};
    out.channels = channels;
    out.rate = rate;
    if (format == 1) {
        if (bits == 8) {
            out.samples.resize(data_bytes);
            for (size_t i = 0; i < data_bytes; ++i) out.samples[i] = i16((int(data[i]) - 128) << 8);
        } else if (bits == 16) {
            out.samples.resize(data_bytes / 2);
            std::memcpy(out.samples.data(), data, out.samples.size() * 2);
        } else if (bits == 24) {
            out.samples.resize(data_bytes / 3);
            for (size_t i = 0; i < out.samples.size(); ++i) out.samples[i] = i16(data[i * 3 + 1] | (data[i * 3 + 2] << 8));
        } else if (bits == 32) {
            out.samples.resize(data_bytes / 4);
            for (size_t i = 0; i < out.samples.size(); ++i) out.samples[i] = i16(i32(rd32(data + i * 4)) >> 16);
        } else {
            return fail("unsupported PCM sample width");
        }
    } else if (format == 3 && bits == 32) {
        out.samples.resize(data_bytes / 4);
        for (size_t i = 0; i < out.samples.size(); ++i) {
            float f;
            std::memcpy(&f, data + i * 4, 4);
            out.samples[i] = i16(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        }
    } else if (format == 0x11) {
        if (!decode_ima(data, data_bytes, channels, block_align, out)) return fail("bad IMA ADPCM data");
    } else {
        return fail("unsupported WAVE codec");
    }
    return !out.samples.empty() || fail("empty sound");
}

namespace {

// The length in bytes of the MPEG audio frame whose header is at `h`, or 0 when it is not one.
size_t mpeg_frame_length(const u8* h) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    const int version = (h[1] >> 3) & 3;   // 0: 2.5, 2: 2, 3: 1
    const int layer = (h[1] >> 1) & 3;     // 1: III, 2: II, 3: I
    const int bitrate_index = h[2] >> 4, rate_index = (h[2] >> 2) & 3, padding = (h[2] >> 1) & 1;
    if (version == 1 || layer == 0 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) return 0;
    static constexpr int kV1[3][15] = {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
                                       {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
                                       {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
    static constexpr int kV2[3][15] = {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
                                       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
                                       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
    static constexpr int kRates[3] = {44100, 48000, 32000};
    const int l = 3 - layer;                // 0: I, 1: II, 2: III
    const int kbps = version == 3 ? kV1[l][bitrate_index] : kV2[l][bitrate_index];
    const int rate = kRates[rate_index] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    if (l == 0) return size_t((12000 * kbps / rate + padding) * 4);
    const int per = (l == 2 && version != 3) ? 72000 : 144000;
    return size_t(per * kbps / rate + padding);
}

}  // namespace

bool decode_mp3(std::span<const u8> file, Pcm& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const u8* p = file.data();
    size_t left = file.size();
    // An ID3v2 tag: ten bytes, then its size as four seven-bit bytes.
    if (left > 10 && std::memcmp(p, "ID3", 3) == 0) {
        const size_t tag = 10 + ((size_t(p[6] & 0x7F) << 21) | (size_t(p[7] & 0x7F) << 14) | (size_t(p[8] & 0x7F) << 7) | size_t(p[9] & 0x7F));
        if (tag >= left) return fail("an ID3 tag and nothing after it");
        p += tag;
        left -= tag;
    }
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info{};
    short frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    out = Pcm{};
    out.channels = 0;
    auto keep = [&](int got) {
        if (got <= 0) return;               // a tag or junk between frames
        if (out.channels == 0) {
            out.channels = info.channels;
            out.rate = info.hz;
        }
        if (info.channels == out.channels) out.samples.insert(out.samples.end(), frame, frame + size_t(got) * size_t(info.channels));
    };
    const u8* const start = p;
    const size_t total = left;
    while (left > 0) {
        const int got = mp3dec_decode_frame(&dec, p, int(std::min<size_t>(left, INT_MAX)), frame, &info);
        if (info.frame_bytes <= 0) break;   // nothing more that looks like audio
        p += info.frame_bytes;
        left -= size_t(info.frame_bytes);
        keep(got);
    }
    if (out.samples.empty()) {
        // A few of the client's files have a broken frame early on, and minimp3 will not lock on
        // to a stream whose frames do not follow each other: walk it a frame at a time instead,
        // each frame's length read from its own header.
        mp3dec_init(&dec);
        for (size_t pos = 0; pos + 4 <= total;) {
            const size_t len = mpeg_frame_length(start + pos);
            if (!len) {
                ++pos;
                continue;
            }
            const size_t n = std::min(len, total - pos);
            keep(mp3dec_decode_frame(&dec, start + pos, int(n), frame, &info));
            pos += n;
        }
    }
    if (out.samples.empty()) return fail("no MPEG audio frames");
    return true;
}

bool decode_sound(std::span<const u8> file, Pcm& out, std::string* error) {
    if (file.size() >= 12 && std::memcmp(file.data(), "RIFF", 4) == 0) return decode_wav(file, out, error);
    return decode_mp3(file, out, error);
}

}  // namespace eng::audio
