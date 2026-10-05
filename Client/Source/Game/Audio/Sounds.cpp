#include "Game/Audio/Sounds.hpp"

#include "Game/Settings.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace lsf {

namespace {

// SFSound.xml's Menu set, in Sounds::Menu's order, and its BGM.
constexpr const char* kMenu[] = {"bu4.mp3", "Spent_sp_cash.mp3", "weapon_type_change.mp3", "weapon_change_inven.mp3",
                                 "capsule_buy.mp3", "capsule_shake.mp3", "capsule_open.mp3", "capsule_success.mp3", "capsule_fail.mp3",
                                 "capsule_listupdown.mp3"};
static_assert(std::size(kMenu) == size_t(Sounds::Menu::Count));
constexpr const char* kMusic = "glum.mp3";

// How loud a sound is `d` metres off, given its table's reach: whole inside `min` (never under a
// metre or two, or a table's 0 would make a footstep silent at your own feet), falling off as
// min/d to `max`, then on to nothing over as far again.
float distance_gain(float d, float min, float max) {
    const float a = std::max(min, 2.0f);
    const float b = std::max(max, a + 1.0f);
    if (d <= a) return 1.0f;
    if (d <= b) return a / d;
    return (a / b) * std::max(0.0f, 1.0f - (d - b) / b);
}

}  // namespace

std::shared_ptr<const eng::audio::Pcm> Sounds::get(std::string_view name) {
    // The archives' keys are the xml's names, lower case, with forward slashes.
    std::string key = sf::lower(name);
    for (char& c : key)
        if (c == '\\') c = '/';
    {
        std::lock_guard lock(mutex_);
        if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    }
    std::shared_ptr<const eng::audio::Pcm> out;
    if (data_) {
        if (auto bytes = data_->read(sf::Pack::Sound, key)) {
            auto pcm = std::make_shared<eng::audio::Pcm>();
            std::string err;
            const auto span = std::span(reinterpret_cast<const eng::u8*>(bytes->data()), bytes->size());
            if (eng::audio::decode_sound(span, *pcm, &err)) out = std::move(pcm);
            else LOG_WARN("Sound %s: %s", key.c_str(), err.c_str());
        } else {
            LOG_WARN("Sound %s: not in the sound archives", key.c_str());
        }
    }
    std::lock_guard lock(mutex_);
    cache_[key] = out;
    return out;
}

void Sounds::preload() {
    if (data_) {
        std::string err;
        if (!tables_.load(*data_, &err)) LOG_WARN("Sounds: %s", err.c_str());
    }
    int ready = 0;
    for (const char* name : kMenu) ready += get(name) != nullptr;
    const auto music = get(kMusic);
    LOG_INFO("Sounds: %d of %d menu sounds, music %.1f s at %d Hz", ready, int(Menu::Count), music ? double(music->seconds()) : 0.0,
             music ? music->rate : 0);
}

void Sounds::play(Menu which) {
    if (!mixer_ || which >= Menu::Count) return;
    if (which == Menu::Click) {
        if (last_click_frame_ == frame_) return;
        last_click_frame_ = frame_;
    }
    if (auto pcm = get(kMenu[size_t(which)])) {
        eng::audio::PlayParams p;
        p.bus = eng::audio::Bus::Ui;
        p.volume = which == Menu::Click ? 0.8f : 1.0f;
        (void)mixer_->play(std::move(pcm), p);
    }
}

void Sounds::set_music(bool on) {
    if (!mixer_ || on == music_on_) return;
    music_on_ = on;
    if (on) {
        if (music_ && mixer_->playing(music_)) {
            mixer_->set_volume(music_, 1.0f, 1.2f);
            return;
        }
        if (auto pcm = get(kMusic)) {
            eng::audio::PlayParams p;
            p.bus = eng::audio::Bus::Music;
            p.loop = true;
            p.fade_in = 1.5f;
            music_ = mixer_->play(std::move(pcm), p);
        }
    } else if (music_) {
        mixer_->stop(music_, 1.0f);
        music_ = 0;
    }
}

void Sounds::apply(const Settings& s) {
    if (!mixer_) return;
    mixer_->set_master(s.master_volume);
    mixer_->set_bus_volume(eng::audio::Bus::Sfx, s.effects_volume);
    mixer_->set_bus_volume(eng::audio::Bus::Ambience, s.effects_volume);
    mixer_->set_bus_volume(eng::audio::Bus::Dialogue, s.effects_volume);
    mixer_->set_bus_volume(eng::audio::Bus::Music, s.music_volume);
    mixer_->set_bus_volume(eng::audio::Bus::Ui, s.ui_volume);
}

// ── The match ──────────────────────────────────────────────────────────────────

void Sounds::forget_weapons() {
    std::lock_guard lock(mutex_);
    weapon_cache_.clear();
}

const sf::WeaponSounds* Sounds::weapon_sounds(u16 weapon_id) {
    std::lock_guard lock(mutex_);   // the match's loader thread asks too
    if (auto it = weapon_cache_.find(weapon_id); it != weapon_cache_.end()) return it->second;
    const sf::WeaponSounds* out = nullptr;
    if (const WeaponDef* w = weapon(weapon_id)) {
        // A server's own gun sounds as the base gun it is like (Game/Registry.hpp).
        if (const WeaponDef* like = w->like.empty() ? nullptr : weapon_by_model(w->like)) w = like;
        out = tables_.weapon(w->code, w->model);
        if (!out) out = tables_.weapon_named(w->name);
        if (!out) out = tables_.weapon("", stock_sound_model(w->klass));
    }
    weapon_cache_[weapon_id] = out;
    return out;
}

void Sounds::set_listener(const eng::Vec3& position, float yaw) {
    ear_ = position;
    ear_yaw_ = yaw;
}

eng::audio::VoiceId Sounds::play_at(std::string_view file, const eng::Vec3& at, float min, float max, float volume, float pitch) {
    if (!mixer_ || file.empty()) return 0;
    const eng::Vec3 to = at - ear_;
    const float d = eng::length(to) * 0.01f;   // centimetres to metres
    const float gain = distance_gain(d, min, max) * volume;
    if (logging_) LOG_INFO("Sound: %.*s at %.1f m, gain %.2f (t %.3f)", int(file.size()), file.data(), double(d), double(gain), eng::time::now());
    if (gain < 0.01f) return 0;
    auto pcm = get(file);
    if (!pcm) return 0;
    // Left or right of where the listener faces; a little quieter behind.
    float pan = 0, behind = 1;
    const eng::Vec3 flat{to.x, 0, to.z};
    if (eng::length(flat) > 1.0f) {
        const eng::Vec3 dir = eng::normalize(flat);
        pan = eng::dot(dir, eng::yaw_to_right(ear_yaw_)) * std::clamp(d / 3.0f, 0.0f, 0.85f);
        behind = 0.82f + 0.18f * std::max(0.0f, eng::dot(dir, eng::angles_to_forward(ear_yaw_, 0)) + 1.0f) * 0.5f;
    }
    eng::audio::PlayParams p;
    p.bus = eng::audio::Bus::Sfx;
    p.volume = gain * behind;
    p.pan = pan;
    p.pitch = pitch;
    return mixer_->play(std::move(pcm), p);
}

eng::audio::VoiceId Sounds::play_2d(std::string_view file, float volume, float pitch) {
    if (!mixer_ || file.empty()) return 0;
    if (logging_) LOG_INFO("Sound: %.*s (yours), gain %.2f (t %.3f)", int(file.size()), file.data(), double(volume), eng::time::now());
    auto pcm = get(file);
    if (!pcm) return 0;
    eng::audio::PlayParams p;
    p.bus = eng::audio::Bus::Sfx;
    p.volume = volume;
    p.pitch = pitch;
    return mixer_->play(std::move(pcm), p);
}

eng::audio::VoiceId Sounds::play_named(std::string_view name, std::string_view key, const eng::Vec3* at, float volume, float pitch) {
    const sf::SoundDef* def = tables_.sound(name);
    const std::vector<std::string>* files = tables_.files(name, key);
    if (!def || !files || files->empty()) return 0;
    rng_ = rng_ * 1664525u + 1013904223u;
    const std::string& file = (*files)[(rng_ >> 8) % files->size()];
    if (!at || !def->positional) return play_2d(file, def->volume * volume, pitch);
    return play_at(file, *at, def->min, def->max, def->volume * volume, pitch);
}

void Sounds::stop_world() {
    if (mixer_) mixer_->stop_bus(eng::audio::Bus::Sfx, 0.15f);
}

}  // namespace lsf
