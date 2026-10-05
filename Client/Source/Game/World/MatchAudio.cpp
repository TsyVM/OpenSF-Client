#include "Game/World/MatchAudio.hpp"

#include "SF/Level.hpp"

#include <algorithm>
#include <cmath>

namespace lsf {

using eng::Vec3;

namespace {

// Centimetres a running soldier covers between footfalls (470 cm/s at about three steps a second).
constexpr float kStride = 150.0f;
// Below this (cm/s) a soldier is standing about, not running.
constexpr float kQuietSpeed = 120.0f;
// Centimetres climbed between rungs heard.
constexpr float kRung = 55.0f;

const char* material(u8 id) { return sf::SoundTables::material_name(id); }

}  // namespace

float MatchAudio::jitter() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return 0.95f + 0.1f * float((rng_ >> 8) & 0xFFFF) / 65535.0f;
}

void MatchAudio::prepare(const sf::Level& level, const std::vector<u16>& weapons) {
    sf::build_sound_mesh(level, surfaces_);
    have_surfaces_ = !surfaces_.empty();
    // Everything the match plays, decoded before it starts rather than on the first shot.
    auto want = [&](const std::string& file) {
        if (!file.empty()) (void)sounds_.get(file);
    };
    for (u16 w : weapons)
        if (const sf::WeaponSounds* ws = sounds_.weapon_sounds(w)) {
            want(ws->fire.file), want(ws->draw.file), want(ws->rolling.file);
            for (const sf::WeaponSound& r : ws->reload) want(r.file);
        }
    std::vector<std::string> named = {"Walk", "Jump", "Impact", "KnifeImpact", "GrenadeImpact", "BodyCenter", "BodyVoice", "Explosion",
                                      "FlashBang", "SmokeGrenadeImpact", "Operation", "Kill"};
    for (const std::string& name : named)
        if (const sf::SoundDef* def = sounds_.tables().sound(name))
            for (const auto& [key, files] : def->files)
                for (const std::string& f : files) want(f);
    // The radio in the language chosen.
    for (u8 g = 0; g <= kRadioAuto; ++g)
        for (int i = 0; i < radio_count(g); ++i)
            if (const sf::SoundDef* def = sounds_.tables().sound(radio_sound(g, u8(i))))
                for (const auto& [key, files] : def->files)
                    for (const std::string& f : files) want(voiced(f));
    want("general/sf_h_ric_head.mp3");
}

u8 MatchAudio::material_at(const Vec3& point, const Vec3& dir) const {
    if (!have_surfaces_) return sf::kSoundConcrete;
    const eng::TraceResult tr = surfaces_.trace_ray(point - dir * 25.0f, point + dir * 25.0f);
    return tr.hit() ? tr.surface : u8(sf::kSoundConcrete);
}

u8 MatchAudio::material_under(const Vec3& feet) const {
    if (!have_surfaces_) return sf::kSoundConcrete;
    const eng::TraceResult tr = surfaces_.trace_ray(feet + Vec3{0, 20, 0}, feet - Vec3{0, 60, 0});
    return tr.hit() ? tr.surface : u8(sf::kSoundConcrete);
}

void MatchAudio::fire(u32 who, u16 weapon, const Vec3* at) {
    const sf::WeaponSounds* ws = sounds_.weapon_sounds(weapon);
    if (!ws || !ws->fire.valid()) return;
    const sf::WeaponSound& f = ws->fire;
    const eng::audio::VoiceId v = at ? sounds_.play_at(f.file, *at, f.min, f.max, f.volume * 0.8f) : sounds_.play_2d(f.file, f.volume * 0.5f);
    if (!v) return;
    // A burst keeps the shot before ringing under the new one; older tails go, or an automatic's
    // overlapping reports pile up past anything the original sounded like.
    auto& ring = shots_[who];
    ring.push_back(v);
    if (ring.size() > 2) {
        sounds_.stop(ring.front(), 0.06f);
        ring.erase(ring.begin());
    }
}

void MatchAudio::empty_click() { (void)sounds_.play_named("Operation", "FireEmptyGun", nullptr); }

void MatchAudio::reload(u32 who, u16 weapon, float seconds, const Vec3* at) {
    const sf::WeaponSounds* ws = sounds_.weapon_sounds(weapon);
    std::erase_if(pending_, [&](const Pending& p) { return p.who == who; });
    if (!ws || ws->reload.empty()) return;
    // One file is the whole reload; several are its stages, spread over it.
    const size_t n = ws->reload.size();
    for (size_t k = 0; k < n; ++k) {
        const sf::WeaponSound& r = ws->reload[k];
        Pending p;
        p.at = now_ + double(seconds) * 0.85 * double(k) / double(n);
        p.file = r.file;
        p.min = r.min, p.max = r.max, p.volume = r.volume;
        p.who = who;
        p.positional = at != nullptr;
        if (at) p.where = *at;
        pending_.push_back(std::move(p));
    }
    update(now_);
}

void MatchAudio::draw(u32 who, u16 weapon, const Vec3* at) {
    if (auto it = draws_.find(who); it != draws_.end()) sounds_.stop(it->second, 0.04f);
    const sf::WeaponSounds* ws = sounds_.weapon_sounds(weapon);
    if (!ws || !ws->draw.valid()) {
        draws_.erase(who);
        return;
    }
    const sf::WeaponSound& d = ws->draw;
    draws_[who] = at ? sounds_.play_at(d.file, *at, d.min, d.max, d.volume * 0.7f) : sounds_.play_2d(d.file, d.volume * 0.7f);
}

void MatchAudio::impact(const Vec3& point, const Vec3& dir, bool blade) {
    (void)sounds_.play_named(blade ? "KnifeImpact" : "Impact", material(material_at(point, dir)), &point, 1.0f, jitter());
}

void MatchAudio::body_hit(const Vec3& at, proto::HitZone zone, bool you, bool woman, bool blade) {
    const Vec3* where = you ? nullptr : &at;
    if (zone == proto::HitZone::Head && !blade) {
        if (you) (void)sounds_.play_2d("general/sf_h_ric_head.mp3", 0.9f, jitter());
        else (void)sounds_.play_at("general/sf_h_ric_head.mp3", at, 0, 50, 1.0f, jitter());
    } else {
        (void)sounds_.play_named(blade ? "KnifeImpact" : "BodyCenter", blade ? "Fabric" : "Default", where, 1.0f, jitter());
    }
    (void)sounds_.play_named("BodyVoice", woman ? "Woman" : "Man", where, 0.9f);
}

void MatchAudio::grenade_bounce(const Vec3& at, const Vec3& dir) {
    (void)sounds_.play_named("GrenadeImpact", material(material_at(at, dir)), &at, 1.0f, jitter());
}

void MatchAudio::explosion(u16 weapon, const Vec3& at) {
    const WeaponDef* w = lsf::weapon(weapon);
    if (!w) return;
    switch (w->grenade) {
        case GrenadeKind::Flash: (void)sounds_.play_named("FlashBang", "Default", &at); break;
        case GrenadeKind::Smoke:
        case GrenadeKind::Gas: (void)sounds_.play_named("SmokeGrenadeImpact", "Default", &at); break;
        default:
            (void)sounds_.play_named("Explosion", w->model == "rgd5" ? "RGD5" : "M69", &at);
            // And what it threw up: glass, earth, stone, splinters (where the table has one).
            (void)sounds_.play_named("Explosion", material(material_under(at + Vec3{0, 30, 0})), &at, 0.6f);
            break;
    }
}

// The announcer's call for a kill (SFSound.xml's Kill: the kill table's KILL_SOUND numbers by
// name). A streak outranks how it was made; a captain brought down is "Excellent!"; a plain kill
// has none.
void MatchAudio::kill_call(u16 flags) {
    const char* call = flags & proto::kKillSpecialForce ? "SpecialForceKill"
                       : flags & proto::kKillMulti      ? "MultiKill"
                       : flags & proto::kKillDouble     ? "DoubleKill"
                       : flags & proto::kKillCaptain    ? "Exellent"
                       : flags & proto::kKillHeadshot   ? "HeadShotKill"
                       : flags & proto::kKillKnife      ? "KnifeKill"
                       : flags & proto::kKillGrenade    ? "GrenadeKill"
                                                        : nullptr;
    if (call) (void)sounds_.play_named("Kill", call, nullptr);
}

void MatchAudio::special_point() { (void)sounds_.play_named("Kill", "SpecialPoint", nullptr); }

std::string MatchAudio::voiced(const std::string& file) const {
    if (radio_folder_ == "eng" || !file.starts_with("radio_eng")) return file;
    return "radio_" + radio_folder_ + file.substr(9);
}

void MatchAudio::radio(u8 group, u8 line, bool woman) {
    const char* name = radio_sound(group, line);
    if (!*name) return;
    const sf::SoundDef* def = sounds_.tables().sound(name);
    const std::vector<std::string>* files = sounds_.tables().files(name, woman ? "Woman" : "Man");
    if (!def || !files || files->empty()) return;
    rng_ = rng_ * 1664525u + 1013904223u;
    const std::string& file = (*files)[(rng_ >> 8) % files->size()];
    // In the language chosen; a line the language lacks is said in English.
    if (!sounds_.play_2d(voiced(file), def->volume)) (void)sounds_.play_2d(file, def->volume);
}

MatchAudio::Step MatchAudio::track(u32 who, const Vec3& feet, const Vec3& velocity, bool on_ground, bool on_ladder, bool quiet, bool you,
                                   double now) {
    Step out;
    Walker& w = walkers_[who];
    if (!w.seen) {
        w.seen = true;
        w.last = feet;
        w.grounded = on_ground || on_ladder;
        return out;
    }
    const Vec3 delta = feet - w.last;
    w.last = feet;
    const float moved = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const Vec3* where = you ? nullptr : &feet;
    // A ladder: a rung every so far climbed, in metal (walking up it is as quiet as walking).
    if (on_ladder) {
        w.grounded = true;
        if (quiet || std::fabs(delta.y) > 300.0f) return out;
        w.stride += std::fabs(delta.y);
        if (w.stride < kRung) return out;
        w.stride -= kRung;
        w.left_foot = !w.left_foot;
        (void)sounds_.play_named("Walk", material(sf::kSoundMetal), where, you ? 0.4f : 0.9f, w.left_foot ? 0.95f : 1.05f);
        out.rung = true, out.material = sf::kSoundMetal;
        return out;
    }
    // Down again after a jump or a fall: the landing, on what is underfoot.
    if (on_ground && !w.grounded && now - w.left_ground > 0.3) {
        out.landing = true, out.material = material_under(feet);
        (void)sounds_.play_named("Jump", material(out.material), where, you ? 0.6f : 1.0f, jitter());
    }
    if (!on_ground && w.grounded) w.left_ground = now;
    w.grounded = on_ground;
    if (!on_ground) return out;
    const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
    // Walking and crouching are silent; a jump across the map (a respawn) is not a step.
    if (quiet || speed < kQuietSpeed || moved > 300.0f) {
        w.stride = std::min(w.stride, kStride * 0.5f);
        return out;
    }
    w.stride += moved;
    if (w.stride < kStride) return out;
    w.stride -= kStride;
    w.left_foot = !w.left_foot;
    out.step = true, out.material = material_under(feet);
    (void)sounds_.play_named("Walk", material(out.material), where, you ? 0.45f : 1.0f, w.left_foot ? 0.97f : 1.03f);
    return out;
}

void MatchAudio::update(double now) {
    now_ = now;
    for (size_t i = 0; i < pending_.size();) {
        const Pending& p = pending_[i];
        if (p.at > now) {
            ++i;
            continue;
        }
        if (p.positional) (void)sounds_.play_at(p.file, p.where, p.min, p.max, p.volume);
        else (void)sounds_.play_2d(p.file, p.volume * 0.85f);
        pending_.erase(pending_.begin() + std::ptrdiff_t(i));
    }
}

}  // namespace lsf
