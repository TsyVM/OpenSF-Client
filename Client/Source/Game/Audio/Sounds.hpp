// The game's sounds, read from the client's own sound archives (data/sound/*.sff) the way its
// tables name them (SF/SoundTables.hpp: SFSound.xml, Weapon.kst), decoded once and kept.
//
// The front end's set is the client's `Menu` sound (ButtonClick bu4.mp3, UseMoney
// Spent_sp_cash.mp3, WeaponTypeChange, WeaponChangeInven), the capsule machine's (capsule_*.mp3),
// and its `BGM` (glum.mp3, looped): read on the boot thread so the first click is not a hitch. The match's sounds are placed in
// the world (play_at) around a listener: each fades with distance by its own table's reach and
// sits left or right of where the listener faces.
#pragma once

#include "Engine/Audio/Mixer.hpp"
#include "Engine/Core/Math.hpp"
#include "Game/Rules.hpp"
#include "SF/Data.hpp"
#include "SF/SoundTables.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace lsf {

struct Settings;

class Sounds {
public:
    enum class Menu : eng::u8 {
        Click, Spend, WeaponType, WeaponEquip,
        CapsuleBuy, CapsuleShake, CapsuleOpen, CapsuleWin, CapsuleLose, CapsuleList,
        Count
    };

    void attach(eng::audio::Mixer* mixer, const sf::Data* data) { mixer_ = mixer, data_ = data; }
    // The tables, the menu set and the music, read now (the boot thread).
    void preload();
    const sf::SoundTables& tables() const { return tables_; }
    // A server's own weapons are numbered per session: which sounds a number has is forgotten when
    // its packs come or go.
    void forget_weapons();

    // A sound from the archives by its table name ("General\\hit1.mp3"), decoded the first
    // time and kept. Null when the archives lack it. Safe from any thread.
    std::shared_ptr<const eng::audio::Pcm> get(std::string_view name);

    void play(Menu which);
    // The lobby's music: fades in when on, out when off; the same track carries on between.
    void set_music(bool on);
    // The settings' volumes onto the mixer's buses.
    void apply(const Settings& s);
    // Once a frame: a click per frame at most, however many controls fire together.
    void tick() { ++frame_; }

    // ── The match ───────────────────────────────────────────────────────────────
    // A weapon's sounds (its own row of Weapon.kst, an SF namesake's, or its class's stock gun's).
    const sf::WeaponSounds* weapon_sounds(u16 weapon_id);
    // Where the ears are (centimetres, yaw in degrees).
    void set_listener(const eng::Vec3& position, float yaw);
    // A file at a point in the world: full volume within `min` metres, fading to nothing a
    // little past `max`. 0 when it is too far to hear or missing.
    eng::audio::VoiceId play_at(std::string_view file, const eng::Vec3& at, float min, float max, float volume = 1, float pitch = 1);
    // A file in the listener's own head (your gun, your footsteps, what hits you).
    eng::audio::VoiceId play_2d(std::string_view file, float volume = 1, float pitch = 1);
    // A table sound (SFSound.xml): one of its files for `key` (a material or an event) at
    // random, at `at` (null: in your head), with its own reach and loudness.
    eng::audio::VoiceId play_named(std::string_view name, std::string_view key, const eng::Vec3* at, float volume = 1, float pitch = 1);
    void stop(eng::audio::VoiceId id, float fade_seconds = 0.05f) {
        if (mixer_ && id) mixer_->stop(id, fade_seconds);
    }
    // Everything the match started stops (leaving it).
    void stop_world();
    // Tests: every world sound into the log (what, how far, how loud).
    void set_logging(bool on) { logging_ = on; }

private:
    eng::audio::Mixer* mixer_ = nullptr;
    const sf::Data* data_ = nullptr;
    sf::SoundTables tables_;
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const eng::audio::Pcm>> cache_;
    std::map<u16, const sf::WeaponSounds*> weapon_cache_;
    eng::audio::VoiceId music_ = 0;
    bool music_on_ = false;
    int last_click_frame_ = -1;
    int frame_ = 0;
    eng::Vec3 ear_;
    float ear_yaw_ = 0;
    eng::u32 rng_ = 0x9E3779B9u;
    bool logging_ = false;
};

}  // namespace lsf
