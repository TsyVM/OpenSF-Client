// The sound mixer: every playing voice is resampled (pitch and the source's own rate), panned
// and summed here into 48 kHz stereo, through a bus per kind of sound, then soft-limited.
//
// With a device (XAudio2) the mix is pulled in small blocks by the device's thread. Without one
// (the automated tests) nothing is heard: the game calls pump() once a frame and exactly that
// frame's worth is mixed, which keeps a scripted run's sound in step with its fixed time step
// and lets it be written to a WAV and measured.
#pragma once

#include "Engine/Audio/Pcm.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace eng::audio {

enum class Bus : u8 { Sfx, Car, Dialogue, Music, Ambience, Ui, Movie, Count };
const char* bus_name(Bus b);

using VoiceId = u32;   // 0 = none

// Sound made as it plays (a movie's track, decoded on a thread of its own): the mixer pulls it
// already at its own rate, stereo, on the mixer's thread (under the mixer's lock, so briefly).
class StreamSource {
public:
    virtual ~StreamSource() = default;
    // Up to `frames` interleaved stereo frames at Mixer::kRate into `out`; how many there were
    // (the rest of the block is silence: the source is behind, or done).
    virtual size_t pull(float* out, size_t frames) = 0;
    // Nothing more will come: the voice ends once it has run dry.
    virtual bool ended() const = 0;
};

struct PlayParams {
    Bus bus = Bus::Sfx;
    float volume = 1;
    float pitch = 1;
    float pan = 0;          // -1 left .. 1 right
    bool loop = false;
    float fade_in = 0;      // seconds
    double start = 0;       // seconds into the sound
};

class Mixer {
public:
    static constexpr int kRate = 48000;

    Mixer();
    ~Mixer();
    // Opens the default output device. False leaves the mixer silent (pump() drives it).
    bool open_device();
    void close_device();
    bool has_device() const { return device_ != nullptr; }
    // The app is in the background (Android): the output stops until it comes back.
    void set_device_paused(bool paused);

    VoiceId play(std::shared_ptr<const Pcm> pcm, const PlayParams& p);
    // A stream (pitch and start are not used; loop neither).
    VoiceId play_stream(std::shared_ptr<StreamSource> stream, const PlayParams& p);
    // Changes a playing voice; volume and pan ramp over a few milliseconds, pitch at once.
    void set(VoiceId id, float volume, float pitch, float pan);
    void set_volume(VoiceId id, float volume, float ramp_seconds = 0.02f);
    void stop(VoiceId id, float fade_seconds = 0.03f);
    void stop_bus(Bus bus, float fade_seconds = 0.1f);
    bool playing(VoiceId id) const;
    double position(VoiceId id) const;   // seconds into the sound
    // A bus's volume (ramped) and whether its voices are held where they are.
    void set_bus_volume(Bus bus, float volume, float ramp_seconds = 0.1f);
    void set_bus_paused(Bus bus, bool paused);
    void set_master(float volume);
    int voices() const;
    int bus_voices(Bus bus) const;   // playing (not fading out) on one bus

    // Without a device: mixes `seconds` of sound now (into the capture, if one is open).
    void pump(double seconds);
    // Writes everything mixed from now on to a 16-bit stereo WAV until stop_capture().
    bool start_capture(const std::filesystem::path& wav);
    void stop_capture();
    // Levels of what was mixed since the last call: peak and RMS of the master output.
    void take_levels(float& peak, float& rms);

    // Mixes `frames` stereo frames (interleaved floats) - the device thread's entry point.
    void render(float* out, size_t frames);

private:
    struct Voice {
        VoiceId id = 0;
        std::shared_ptr<const Pcm> pcm;
        std::shared_ptr<StreamSource> stream;   // instead of pcm
        double pos = 0;             // source frames
        float pitch = 1;
        float vol = 0, vol_target = 1, vol_step = 0;
        float pan = 0, pan_target = 0;
        Bus bus = Bus::Sfx;
        bool loop = false;
        bool stopping = false;
    };
    struct BusState {
        float vol = 1, target = 1, step = 0;
        bool paused = false;
    };
    Voice* find(VoiceId id);
    const Voice* find(VoiceId id) const;
    void mix_voice(Voice& v, float* out, size_t frames, float bus_gain_start, float bus_gain_step);
    void mix_stream(Voice& v, float* out, size_t frames, float bus_gain_start, float bus_gain_step);
    void finish_block(float* out, size_t frames);

    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    BusState buses_[size_t(Bus::Count)];
    float master_ = 1;
    VoiceId next_id_ = 1;
    std::vector<float> scratch_, stream_scratch_;
    double pump_debt_ = 0;
    // Capture and levels.
    std::vector<i16> capture_;
    std::filesystem::path capture_path_;
    bool capturing_ = false;
    float peak_ = 0;
    double sum_sq_ = 0;
    size_t level_frames_ = 0;
    struct Device;
    std::unique_ptr<Device> device_;
};

bool write_wav(const std::filesystem::path& file, const std::vector<i16>& stereo, int rate);

}  // namespace eng::audio
