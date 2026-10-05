#include "Engine/Audio/Mixer.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <xaudio2.h>
#elif defined(__ANDROID__)
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#else
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <thread>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eng::audio {

const char* bus_name(Bus b) {
    switch (b) {
        case Bus::Sfx: return "sfx";
        case Bus::Car: return "car";
        case Bus::Dialogue: return "dialogue";
        case Bus::Music: return "music";
        case Bus::Ambience: return "ambience";
        case Bus::Ui: return "ui";
        case Bus::Movie: return "movie";
        default: return "?";
    }
}

namespace {

constexpr size_t kBlock = 480;   // 10 ms
constexpr int kQueued = 3;       // blocks in flight on the device

// Equal-power pan of a mono source: left and right gains.
void pan_gains(float pan, float& l, float& r) {
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    l = std::cos(a) * 1.41421356f;
    r = std::sin(a) * 1.41421356f;
    l = std::min(l, 1.0f);
    r = std::min(r, 1.0f);
}

// Soft limit: straight up to 0.8, then eased into +-1.
float soft_clip(float x) {
    const float a = std::fabs(x);
    if (a <= 0.8f) return x;
    const float over = (a - 0.8f) / 0.2f;
    const float y = 0.8f + 0.2f * (over / (1.0f + over));
    return x < 0 ? -y : y;
}

}  // namespace

// ── Device ─────────────────────────────────────────────────────────────────────

#if !defined(_WIN32) && !defined(__ANDROID__)
// -- Linux and macOS -------------------------------------------------------------

namespace {
constexpr int kDeskQueued = 3;   // blocks of kBlock frames in flight
}  // namespace

#if defined(__APPLE__)
// macOS: an AudioQueue of 16-bit stereo blocks, each rendered when the queue gives it back.
struct Mixer::Device {
    AudioQueueRef queue = nullptr;
    AudioQueueBufferRef buffers[kDeskQueued] = {};
    Mixer* mixer = nullptr;
    std::vector<float> scratch = std::vector<float>(kBlock * 2);
    std::atomic<bool> paused{false};

    void fill(AudioQueueBufferRef b) {
        auto* out = static_cast<i16*>(b->mAudioData);
        if (paused) {
            std::memset(out, 0, kBlock * 2 * sizeof(i16));
        } else {
            mixer->render(scratch.data(), kBlock);
            for (size_t i = 0; i < kBlock * 2; ++i) out[i] = i16(std::lround(std::clamp(scratch[i], -1.0f, 1.0f) * 32767.0f));
        }
        b->mAudioDataByteSize = UInt32(kBlock * 2 * sizeof(i16));
        AudioQueueEnqueueBuffer(queue, b, 0, nullptr);
    }
    static void on_done(void* self, AudioQueueRef, AudioQueueBufferRef b) { static_cast<Device*>(self)->fill(b); }
    ~Device() {
        if (queue) {
            AudioQueueStop(queue, true);
            AudioQueueDispose(queue, true);
        }
    }
};

Mixer::Mixer() { scratch_.resize(kBlock * 2); }

Mixer::~Mixer() {
    close_device();
    stop_capture();
}

bool Mixer::open_device() {
    if (device_) return true;
    auto d = std::make_unique<Device>();
    d->mixer = this;
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = kRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    fmt.mBytesPerFrame = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = 4;
    if (AudioQueueNewOutput(&fmt, &Device::on_done, d.get(), nullptr, nullptr, 0, &d->queue) != noErr) {
        LOG_WARN("Audio: no Core Audio output; the game runs silent");
        return false;
    }
    for (auto& b : d->buffers) {
        if (AudioQueueAllocateBuffer(d->queue, UInt32(kBlock * 2 * sizeof(i16)), &b) != noErr) {
            LOG_WARN("Audio: no Core Audio buffers; the game runs silent");
            return false;
        }
    }
    device_ = std::move(d);
    for (auto& b : device_->buffers) device_->fill(b);
    AudioQueueStart(device_->queue, nullptr);
    LOG_INFO("Audio: Core Audio output, %d Hz stereo, %zu-frame blocks x %d", kRate, kBlock, kDeskQueued);
    return true;
}

void Mixer::close_device() { device_.reset(); }

void Mixer::set_device_paused(bool paused) {
    if (device_) device_->paused = paused;
}
#else
// Linux: ALSA's own playback (PulseAudio and PipeWire desktops take it through their ALSA plug-in),
// opened at run time, and a thread that renders a block and writes it (the write waits for room,
// which paces the thread).
namespace {
struct Alsa {
    void* so = nullptr;
    int (*open)(void**, const char*, int, int) = nullptr;
    int (*set_params)(void*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
    long (*writei)(void*, const void*, unsigned long) = nullptr;
    int (*recover)(void*, int, int) = nullptr;
    int (*close)(void*) = nullptr;
    const char* (*strerror)(int) = nullptr;
    bool load() {
        if (so) return true;
        so = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!so) return false;
        open = reinterpret_cast<decltype(open)>(dlsym(so, "snd_pcm_open"));
        set_params = reinterpret_cast<decltype(set_params)>(dlsym(so, "snd_pcm_set_params"));
        writei = reinterpret_cast<decltype(writei)>(dlsym(so, "snd_pcm_writei"));
        recover = reinterpret_cast<decltype(recover)>(dlsym(so, "snd_pcm_recover"));
        close = reinterpret_cast<decltype(close)>(dlsym(so, "snd_pcm_close"));
        strerror = reinterpret_cast<decltype(strerror)>(dlsym(so, "snd_strerror"));
        return open && set_params && writei && recover && close;
    }
} g_alsa;
constexpr int kStreamPlayback = 0, kFormatS16LE = 2, kAccessRwInterleaved = 3;
}  // namespace

struct Mixer::Device {
    void* pcm = nullptr;
    Mixer* mixer = nullptr;
    std::thread thread;
    std::atomic<bool> run{true}, paused{false};
    std::vector<float> scratch = std::vector<float>(kBlock * 2);
    std::vector<i16> block = std::vector<i16>(kBlock * 2);

    void loop() {
        while (run) {
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            mixer->render(scratch.data(), kBlock);
            for (size_t i = 0; i < block.size(); ++i) block[i] = i16(std::lround(std::clamp(scratch[i], -1.0f, 1.0f) * 32767.0f));
            size_t done = 0;
            while (done < kBlock && run) {
                const long n = g_alsa.writei(pcm, block.data() + done * 2, kBlock - done);
                if (n < 0) {
                    if (g_alsa.recover(pcm, int(n), 1) < 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        break;
                    }
                    continue;
                }
                done += size_t(n);
            }
        }
    }
    ~Device() {
        run = false;
        if (thread.joinable()) thread.join();
        if (pcm) g_alsa.close(pcm);
    }
};

Mixer::Mixer() { scratch_.resize(kBlock * 2); }

Mixer::~Mixer() {
    close_device();
    stop_capture();
}

bool Mixer::open_device() {
    if (device_) return true;
    if (!g_alsa.load()) {
        LOG_WARN("Audio: no ALSA library (libasound.so.2); the game runs silent");
        return false;
    }
    auto d = std::make_unique<Device>();
    d->mixer = this;
    if (const int err = g_alsa.open(&d->pcm, "default", kStreamPlayback, 0); err < 0) {
        LOG_WARN("Audio: no sound device (%s); the game runs silent", g_alsa.strerror ? g_alsa.strerror(err) : "?");
        d->pcm = nullptr;
        return false;
    }
    // 16-bit stereo at the mixer's rate (ALSA resamples for a card that wants another), about
    // kDeskQueued blocks of latency.
    if (const int err = g_alsa.set_params(d->pcm, kFormatS16LE, kAccessRwInterleaved, 2, unsigned(kRate), 1,
                                          unsigned(size_t(kDeskQueued) * kBlock * 1000000 / size_t(kRate)));
        err < 0) {
        LOG_WARN("Audio: the sound device took no 16-bit stereo (%s); the game runs silent", g_alsa.strerror ? g_alsa.strerror(err) : "?");
        return false;
    }
    Device* raw = d.get();
    device_ = std::move(d);
    raw->thread = std::thread([raw] { raw->loop(); });
    LOG_INFO("Audio: ALSA output, %d Hz stereo, %zu-frame blocks", kRate, kBlock);
    return true;
}

void Mixer::close_device() { device_.reset(); }

void Mixer::set_device_paused(bool paused) {
    if (device_) device_->paused = paused;
}
#endif
#elif defined(__ANDROID__)
// Android: an OpenSL ES player fed from a queue of 16-bit blocks, each rendered when the one
// before it is taken (every Android version has it; it runs over AAudio where there is one).
namespace {
constexpr size_t kSlBlock = kBlock * 2;   // 20 ms: a busy phone's scheduler is coarser than a PC's
constexpr int kSlQueued = 3;
}  // namespace

struct Mixer::Device {
    SLObjectItf engine_obj = nullptr, mix_obj = nullptr, player_obj = nullptr;
    SLEngineItf engine = nullptr;
    SLPlayItf play = nullptr;
    SLAndroidSimpleBufferQueueItf queue = nullptr;
    Mixer* mixer = nullptr;
    std::vector<float> scratch = std::vector<float>(kSlBlock * 2);
    std::vector<i16> blocks[kSlQueued];
    int next = 0;

    void submit() {
        mixer->render(scratch.data(), kSlBlock);
        std::vector<i16>& b = blocks[next];
        next = (next + 1) % kSlQueued;
        for (size_t i = 0; i < b.size(); ++i) b[i] = i16(std::lround(std::clamp(scratch[i], -1.0f, 1.0f) * 32767.0f));
        (*queue)->Enqueue(queue, b.data(), SLuint32(b.size() * sizeof(i16)));
    }
    static void on_done(SLAndroidSimpleBufferQueueItf, void* self) { static_cast<Device*>(self)->submit(); }
    ~Device() {
        if (player_obj) (*player_obj)->Destroy(player_obj);
        if (mix_obj) (*mix_obj)->Destroy(mix_obj);
        if (engine_obj) (*engine_obj)->Destroy(engine_obj);
    }
};

Mixer::Mixer() { scratch_.resize(kBlock * 2); }

Mixer::~Mixer() {
    close_device();
    stop_capture();
}

bool Mixer::open_device() {
    if (device_) return true;
    auto d = std::make_unique<Device>();
    d->mixer = this;
    auto fail = [](const char* what) {
        LOG_WARN("Audio: %s; the game runs silent", what);
        return false;
    };
    if (slCreateEngine(&d->engine_obj, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return fail("no OpenSL ES engine");
    (*d->engine_obj)->Realize(d->engine_obj, SL_BOOLEAN_FALSE);
    (*d->engine_obj)->GetInterface(d->engine_obj, SL_IID_ENGINE, &d->engine);
    if ((*d->engine)->CreateOutputMix(d->engine, &d->mix_obj, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return fail("no output mix");
    (*d->mix_obj)->Realize(d->mix_obj, SL_BOOLEAN_FALSE);
    SLDataLocator_AndroidSimpleBufferQueue loc{SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, SLuint32(kSlQueued)};
    SLDataFormat_PCM fmt{SL_DATAFORMAT_PCM,           2, SL_SAMPLINGRATE_48, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                         SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource src{&loc, &fmt};
    SLDataLocator_OutputMix out{SL_DATALOCATOR_OUTPUTMIX, d->mix_obj};
    SLDataSink sink{&out, nullptr};
    const SLInterfaceID ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[] = {SL_BOOLEAN_TRUE};
    if ((*d->engine)->CreateAudioPlayer(d->engine, &d->player_obj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS) return fail("no audio player");
    (*d->player_obj)->Realize(d->player_obj, SL_BOOLEAN_FALSE);
    (*d->player_obj)->GetInterface(d->player_obj, SL_IID_PLAY, &d->play);
    (*d->player_obj)->GetInterface(d->player_obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &d->queue);
    (*d->queue)->RegisterCallback(d->queue, &Device::on_done, d.get());
    for (auto& b : d->blocks) b.assign(kSlBlock * 2, 0);
    device_ = std::move(d);
    for (int i = 0; i < kSlQueued; ++i) device_->submit();
    (*device_->play)->SetPlayState(device_->play, SL_PLAYSTATE_PLAYING);
    LOG_INFO("Audio: OpenSL ES output, %d Hz stereo, %zu-frame blocks x %d", kRate, kSlBlock, kSlQueued);
    return true;
}

void Mixer::close_device() {
    if (!device_) return;
    (*device_->play)->SetPlayState(device_->play, SL_PLAYSTATE_STOPPED);
    device_.reset();
}

void Mixer::set_device_paused(bool paused) {
    if (device_ && device_->play) (*device_->play)->SetPlayState(device_->play, paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING);
}
#else
struct Mixer::Device : IXAudio2VoiceCallback {
    IXAudio2* xa = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* source = nullptr;
    Mixer* mixer = nullptr;
    std::vector<float> blocks[kQueued];
    int next = 0;
    bool com = false;

    void submit() {
        std::vector<float>& b = blocks[next];
        next = (next + 1) % kQueued;
        mixer->render(b.data(), kBlock);
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = UINT32(b.size() * sizeof(float));
        buf.pAudioData = reinterpret_cast<const BYTE*>(b.data());
        source->SubmitSourceBuffer(&buf);
    }
    void STDMETHODCALLTYPE OnBufferEnd(void*) override { submit(); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

Mixer::Mixer() { scratch_.resize(kBlock * 2); }

Mixer::~Mixer() {
    close_device();
    stop_capture();
}

bool Mixer::open_device() {
    if (device_) return true;
    auto d = std::make_unique<Device>();
    d->mixer = this;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    d->com = SUCCEEDED(co);
    if (FAILED(XAudio2Create(&d->xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        LOG_WARN("Audio: XAudio2 unavailable; the game runs silent");
        return false;
    }
    if (FAILED(d->xa->CreateMasteringVoice(&d->master, 2, kRate))) {
        LOG_WARN("Audio: no output device; the game runs silent");
        d->xa->Release();
        return false;
    }
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    if (FAILED(d->xa->CreateSourceVoice(&d->source, &fmt, 0, 2.0f, d.get()))) {
        LOG_WARN("Audio: could not create the output voice; the game runs silent");
        d->master->DestroyVoice();
        d->xa->Release();
        return false;
    }
    for (auto& b : d->blocks) b.assign(kBlock * 2, 0.0f);
    device_ = std::move(d);
    for (int i = 0; i < kQueued; ++i) device_->submit();
    device_->source->Start();
    LOG_INFO("Audio: XAudio2 output, %d Hz stereo, %zu-frame blocks x %d", kRate, kBlock, kQueued);
    return true;
}

void Mixer::close_device() {
    if (!device_) return;
    device_->source->Stop();
    device_->source->DestroyVoice();
    device_->master->DestroyVoice();
    device_->xa->Release();
    if (device_->com) CoUninitialize();
    device_.reset();
}

void Mixer::set_device_paused(bool paused) {
    if (!device_) return;
    if (paused) device_->source->Stop();
    else device_->source->Start();
}
#endif

// ── Voices ─────────────────────────────────────────────────────────────────────

Mixer::Voice* Mixer::find(VoiceId id) {
    for (auto& v : voices_)
        if (v.id == id) return &v;
    return nullptr;
}
const Mixer::Voice* Mixer::find(VoiceId id) const {
    for (const auto& v : voices_)
        if (v.id == id) return &v;
    return nullptr;
}

VoiceId Mixer::play(std::shared_ptr<const Pcm> pcm, const PlayParams& p) {
    if (!pcm || pcm->frames() == 0) return 0;
    std::lock_guard lock(mutex_);
    Voice v;
    v.id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    v.pcm = std::move(pcm);
    v.bus = p.bus;
    v.loop = p.loop;
    v.pitch = std::max(0.01f, p.pitch);
    v.pan = v.pan_target = p.pan;
    v.vol_target = std::max(0.0f, p.volume);
    if (p.fade_in > 0) {
        v.vol = 0;
        v.vol_step = v.vol_target / (p.fade_in * kRate);
    } else {
        v.vol = v.vol_target;
    }
    v.pos = std::clamp(p.start * v.pcm->rate, 0.0, double(v.pcm->frames() - 1));
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

VoiceId Mixer::play_stream(std::shared_ptr<StreamSource> stream, const PlayParams& p) {
    if (!stream) return 0;
    std::lock_guard lock(mutex_);
    Voice v;
    v.id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    v.stream = std::move(stream);
    v.bus = p.bus;
    v.pan = v.pan_target = p.pan;
    v.vol_target = std::max(0.0f, p.volume);
    if (p.fade_in > 0) {
        v.vol = 0;
        v.vol_step = v.vol_target / (p.fade_in * kRate);
    } else {
        v.vol = v.vol_target;
    }
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Mixer::set(VoiceId id, float volume, float pitch, float pan) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(id); v && !v->stopping) {
        v->vol_target = std::max(0.0f, volume);
        v->vol_step = std::fabs(v->vol_target - v->vol) / (0.02f * kRate);
        v->pitch = std::max(0.01f, pitch);
        v->pan_target = std::clamp(pan, -1.0f, 1.0f);
    }
}

void Mixer::set_volume(VoiceId id, float volume, float ramp) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(id); v && !v->stopping) {
        v->vol_target = std::max(0.0f, volume);
        v->vol_step = std::fabs(v->vol_target - v->vol) / (std::max(0.001f, ramp) * kRate);
    }
}

void Mixer::stop(VoiceId id, float fade) {
    std::lock_guard lock(mutex_);
    if (Voice* v = find(id)) {
        v->stopping = true;
        v->vol_target = 0;
        v->vol_step = std::max(v->vol, 1e-4f) / (std::max(0.001f, fade) * kRate);
    }
}

void Mixer::stop_bus(Bus bus, float fade) {
    std::lock_guard lock(mutex_);
    for (auto& v : voices_)
        if (v.bus == bus) {
            v.stopping = true;
            v.vol_target = 0;
            v.vol_step = std::max(v.vol, 1e-4f) / (std::max(0.001f, fade) * kRate);
        }
}

bool Mixer::playing(VoiceId id) const {
    std::lock_guard lock(mutex_);
    const Voice* v = find(id);
    return v && !v->stopping;
}

double Mixer::position(VoiceId id) const {
    std::lock_guard lock(mutex_);
    const Voice* v = find(id);
    if (!v) return 0.0;
    return v->pcm ? v->pos / std::max(1, v->pcm->rate) : v->pos / kRate;
}

void Mixer::set_bus_volume(Bus bus, float volume, float ramp) {
    std::lock_guard lock(mutex_);
    BusState& b = buses_[size_t(bus)];
    b.target = std::max(0.0f, volume);
    b.step = std::fabs(b.target - b.vol) / (std::max(0.001f, ramp) * kRate);
}

void Mixer::set_bus_paused(Bus bus, bool paused) {
    std::lock_guard lock(mutex_);
    buses_[size_t(bus)].paused = paused;
}

void Mixer::set_master(float volume) {
    std::lock_guard lock(mutex_);
    master_ = std::max(0.0f, volume);
}

int Mixer::voices() const {
    std::lock_guard lock(mutex_);
    return int(voices_.size());
}

int Mixer::bus_voices(Bus bus) const {
    std::lock_guard lock(mutex_);
    int n = 0;
    for (const auto& v : voices_) n += v.bus == bus && !v.stopping;
    return n;
}

// ── Mixing ─────────────────────────────────────────────────────────────────────

void Mixer::mix_stream(Voice& v, float* out, size_t frames, float bus_gain, float bus_step) {
    if (stream_scratch_.size() < frames * 2) stream_scratch_.resize(frames * 2);
    float* s = stream_scratch_.data();
    const size_t got = v.stream->pull(s, frames);
    std::fill(s + got * 2, s + frames * 2, 0.0f);
    const float pan0 = v.pan, pan1 = v.pan_target;
    for (size_t i = 0; i < frames; ++i) {
        if (v.vol < v.vol_target) v.vol = std::min(v.vol_target, v.vol + v.vol_step);
        else if (v.vol > v.vol_target) v.vol = std::max(v.vol_target, v.vol - v.vol_step);
        const float g = v.vol * (bus_gain + bus_step * float(i));
        const float pan = pan0 + (pan1 - pan0) * (float(i) / float(frames));
        float l = s[i * 2], r = s[i * 2 + 1];
        if (pan < 0) r *= 1.0f + pan;
        else if (pan > 0) l *= 1.0f - pan;
        out[i * 2] += l * g;
        out[i * 2 + 1] += r * g;
    }
    v.pos += double(got);
    v.pan = pan1;
    // Run dry and nothing more to come: the voice is over.
    if (got < frames && v.stream->ended()) {
        v.stopping = true;
        v.vol = v.vol_target = 0;
    }
}

void Mixer::mix_voice(Voice& v, float* out, size_t frames, float bus_gain, float bus_step) {
    if (v.stream) {
        mix_stream(v, out, frames, bus_gain, bus_step);
        return;
    }
    const Pcm& p = *v.pcm;
    const size_t n = p.frames();
    const int ch = p.channels;
    const i16* s = p.samples.data();
    const double step = double(v.pitch) * double(p.rate) / double(kRate);
    constexpr float kScale = 1.0f / 32768.0f;
    // Pan moves to its target over the block.
    const float pan0 = v.pan, pan1 = v.pan_target;
    for (size_t i = 0; i < frames; ++i) {
        if (v.pos >= double(n - 1)) {
            if (v.loop) v.pos = std::fmod(v.pos, double(n - 1));
            else {
                v.stopping = true;
                v.vol = 0;
                v.vol_target = 0;
                break;
            }
        }
        // Volume ramp.
        if (v.vol < v.vol_target) v.vol = std::min(v.vol_target, v.vol + v.vol_step);
        else if (v.vol > v.vol_target) v.vol = std::max(v.vol_target, v.vol - v.vol_step);
        const size_t i0 = size_t(v.pos);
        const float t = float(v.pos - double(i0));
        size_t i1 = i0 + 1;
        if (i1 >= n) i1 = v.loop ? 0 : n - 1;
        const float g = v.vol * (bus_gain + bus_step * float(i));
        const float pan = pan0 + (pan1 - pan0) * (float(i) / float(frames));
        float l, r;
        if (ch == 1) {
            const float x = (float(s[i0]) + (float(s[i1]) - float(s[i0])) * t) * kScale;
            float gl, gr;
            pan_gains(pan, gl, gr);
            l = x * gl;
            r = x * gr;
        } else {
            const i16* a = s + i0 * size_t(ch);
            const i16* b = s + i1 * size_t(ch);
            l = (float(a[0]) + (float(b[0]) - float(a[0])) * t) * kScale;
            r = (float(a[1]) + (float(b[1]) - float(a[1])) * t) * kScale;
            if (pan < 0) r *= 1.0f + pan;
            else if (pan > 0) l *= 1.0f - pan;
        }
        out[i * 2] += l * g;
        out[i * 2 + 1] += r * g;
        v.pos += step;
    }
    v.pan = pan1;
    if (v.stopping && v.vol <= 0) v.vol = 0;
}

void Mixer::render(float* out, size_t frames) {
    std::memset(out, 0, frames * 2 * sizeof(float));
    std::lock_guard lock(mutex_);
    float bus_start[size_t(Bus::Count)], bus_step[size_t(Bus::Count)];
    for (size_t b = 0; b < size_t(Bus::Count); ++b) {
        BusState& s = buses_[b];
        bus_start[b] = s.vol;
        float end = s.vol;
        if (s.vol < s.target) end = std::min(s.target, s.vol + s.step * float(frames));
        else if (s.vol > s.target) end = std::max(s.target, s.vol - s.step * float(frames));
        bus_step[b] = (end - s.vol) / float(frames);
        s.vol = end;
    }
    for (auto& v : voices_) {
        const size_t b = size_t(v.bus);
        if (buses_[b].paused && !v.stopping) continue;
        mix_voice(v, out, frames, bus_start[b], bus_step[b]);
    }
    std::erase_if(voices_, [](const Voice& v) { return v.stopping && v.vol <= 0; });
    finish_block(out, frames);
}

void Mixer::finish_block(float* out, size_t frames) {
    for (size_t i = 0; i < frames * 2; ++i) {
        const float x = soft_clip(out[i] * master_);
        out[i] = x;
        peak_ = std::max(peak_, std::fabs(x));
        sum_sq_ += double(x) * double(x);
    }
    level_frames_ += frames;
    if (capturing_)
        for (size_t i = 0; i < frames * 2; ++i) capture_.push_back(i16(std::lround(std::clamp(out[i], -1.0f, 1.0f) * 32767.0f)));
}

void Mixer::pump(double seconds) {
    if (device_) return;
    pump_debt_ += seconds * kRate;
    while (pump_debt_ >= 1.0) {
        const size_t n = std::min(kBlock, size_t(pump_debt_));
        render(scratch_.data(), n);
        pump_debt_ -= double(n);
    }
}

void Mixer::take_levels(float& peak, float& rms) {
    std::lock_guard lock(mutex_);
    peak = peak_;
    rms = level_frames_ ? float(std::sqrt(sum_sq_ / double(level_frames_ * 2))) : 0.0f;
    peak_ = 0;
    sum_sq_ = 0;
    level_frames_ = 0;
}

bool Mixer::start_capture(const std::filesystem::path& wav) {
    std::lock_guard lock(mutex_);
    capture_.clear();
    capture_path_ = wav;
    capturing_ = true;
    return true;
}

void Mixer::stop_capture() {
    std::vector<i16> data;
    std::filesystem::path path;
    {
        std::lock_guard lock(mutex_);
        if (!capturing_) return;
        capturing_ = false;
        data.swap(capture_);
        path = capture_path_;
    }
    if (!write_wav(path, data, kRate)) LOG_WARN("Audio: could not write %s", path.string().c_str());
    else LOG_INFO("Audio: wrote %s (%.1f s)", path.string().c_str(), double(data.size()) / 2.0 / kRate);
}

bool write_wav(const std::filesystem::path& file, const std::vector<i16>& stereo, int rate) {
    std::vector<u8> out(44 + stereo.size() * 2);
    auto put32 = [&](size_t at, u32 v) { std::memcpy(out.data() + at, &v, 4); };
    auto put16 = [&](size_t at, u16 v) { std::memcpy(out.data() + at, &v, 2); };
    std::memcpy(out.data(), "RIFF", 4);
    put32(4, u32(36 + stereo.size() * 2));
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 2);
    put32(24, u32(rate));
    put32(28, u32(rate * 4));
    put16(32, 4);
    put16(34, 16);
    std::memcpy(out.data() + 36, "data", 4);
    put32(40, u32(stereo.size() * 2));
    std::memcpy(out.data() + 44, stereo.data(), stereo.size() * 2);
    return fs::write_file(file, out.data(), out.size());
}

}  // namespace eng::audio
