// Decoded sound: 16-bit samples, channels interleaved, and the readers that make them. Soldier
// Front's sounds (data/sound/*.sff) are mostly MP3s (SFSound.xml names them), with RIFF WAVE files
// among them: 8- or 16-bit PCM, and a few IMA ADPCM ones from the Miles tools.
#pragma once

#include "Engine/Core/Types.hpp"

#include <span>
#include <string>
#include <vector>

namespace eng::audio {

struct Pcm {
    int channels = 1;
    int rate = 22050;
    std::vector<i16> samples;   // interleaved
    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
    float seconds() const { return rate > 0 ? float(frames()) / float(rate) : 0.0f; }
};

// A whole RIFF WAVE file: PCM 8/16/24/32-bit, IEEE float, or IMA ADPCM.
bool decode_wav(std::span<const u8> file, Pcm& out, std::string* error = nullptr);
// A whole MPEG audio file (layer I, II or III; an ID3v2 tag in front is skipped). Most of Soldier
// Front's sounds are MP3s whatever their archive calls them.
bool decode_mp3(std::span<const u8> file, Pcm& out, std::string* error = nullptr);
// Either of the two, picked from the bytes (RIFF: a WAVE, anything else: MPEG).
bool decode_sound(std::span<const u8> file, Pcm& out, std::string* error = nullptr);

}  // namespace eng::audio
