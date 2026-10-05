// The match's sounds, as the client's tables give them (Game/Audio/Sounds, SF/SoundTables):
//
//   guns       fire, reload (its stages spread over the reload), draw, the empty click; yours in
//              your head, everyone else's where they stand, as far as the table says they carry;
//   feet       a step per stride while running (walking and crouching are silent, as in the
//              original), a landing after a fall, each on what is underfoot; a rung climbed;
//   hits       bullets and knives on the map by the surface they meet, on bodies, a voice;
//   grenades   the throw, bounces, the blast (and its debris on what it lands on);
//   kills      the announcer's call for yours (head shot, knife, double, multi, ...).
//
// What is underfoot or struck comes from the map's own texture table (SF/Level: LevelMaterial::
// sound), looked up on a second, render-geometry mesh built for it while the map loads.
#pragma once

#include "Game/Audio/Sounds.hpp"
#include "Game/Protocol.hpp"

#include "Engine/Physics/CollisionMesh.hpp"

#include <map>
#include <memory>
#include <vector>

namespace sf {
struct Level;
}

namespace lsf {

class MatchAudio {
public:
    explicit MatchAudio(Sounds& sounds) : sounds_(sounds) {}

    // On the loader thread: the surface mesh from the map's geometry, and every sound the match
    // will want decoded now (the weapons are everyone's loadouts).
    void prepare(const sf::Level& level, const std::vector<u16>& weapons);
    // What a point of the map sounds like (a sound material id), meeting it along `dir`.
    u8 material_at(const eng::Vec3& point, const eng::Vec3& dir) const;
    u8 material_under(const eng::Vec3& feet) const;

    // `at` null: yours, in your head.
    void fire(u32 who, u16 weapon, const eng::Vec3* at);
    Sounds& sounds() { return sounds_; }
    void empty_click();
    void reload(u32 who, u16 weapon, float seconds, const eng::Vec3* at);
    // A weapon taken out. One at a time a soldier: switching again cuts the last one's sound short
    // (weapons flicked through fast were a pile of them).
    void draw(u32 who, u16 weapon, const eng::Vec3* at);
    // A bullet or a blade stopped by the map at `point`, travelling along `dir`.
    void impact(const eng::Vec3& point, const eng::Vec3& dir, bool blade);
    void body_hit(const eng::Vec3& at, proto::HitZone zone, bool you, bool woman, bool blade);
    void grenade_bounce(const eng::Vec3& at, const eng::Vec3& dir);
    void explosion(u16 weapon, const eng::Vec3& at);
    void kill_call(u16 kill_flags);
    void special_point();   // the medal's: an objective of yours done
    // A radio line in the speaker's voice (a radio: heard alike wherever they are), in the
    // radio's language (`folder`: the archives' radio_<folder>, eng ger kor spa).
    void radio(u8 group, u8 line, bool woman);
    void set_radio_language(std::string_view folder) { radio_folder_ = folder; }

    // Footsteps and landings for one soldier, every frame. `feet` is their origin. On a ladder a
    // step is a rung, every so far climbed, in metal. What it played comes back, so the steps can be
    // seen too (World/Effects.cpp footstep_fx).
    struct Step {
        bool step = false, landing = false, rung = false;
        u8 material = 0;            // a sound material id (SF/Level.hpp kSound*)
    };
    Step track(u32 who, const eng::Vec3& feet, const eng::Vec3& velocity, bool on_ground, bool on_ladder, bool quiet, bool you, double now);
    void forget(u32 who) { walkers_.erase(who); }
    // Reload stages coming due.
    void update(double now);

private:
    // A radio file (SFSound.xml names radio_eng's) in the language chosen: the archives keep the
    // same names in each.
    std::string voiced(const std::string& file) const;
    std::string radio_folder_ = "eng";

    struct Walker {
        float stride = 0;
        bool grounded = true;
        double left_ground = 0;
        eng::Vec3 last;
        bool seen = false;
        bool left_foot = false;
    };
    struct Pending {
        double at = 0;
        std::string file;
        float min = 0, max = 0, volume = 1;
        u32 who = 0;
        bool positional = false;
        eng::Vec3 where;
    };
    Sounds& sounds_;
    eng::CollisionMesh surfaces_;
    bool have_surfaces_ = false;
    std::map<u32, Walker> walkers_;
    std::vector<Pending> pending_;
    // A gun's last few shots, so a long burst lets its oldest tails go.
    std::map<u32, std::vector<eng::audio::VoiceId>> shots_;
    std::map<u32, eng::audio::VoiceId> draws_;   // each soldier's weapon being taken out
    double now_ = 0;
    float jitter();
    eng::u32 rng_ = 0x2545F491u;
};

}  // namespace lsf
