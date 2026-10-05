#include "Game/Modes.hpp"

#include "Engine/Core/Strings.hpp"
#include "SF/Level.hpp"

#include <algorithm>
#include <iterator>

namespace lsf {

MapRules map_rules(const sf::Level& level) {
    MapRules r;
    r.id = level.id;
    const std::string o = eng::str::lower(level.objective);
    for (const sf::LevelObjective& obj : level.objectives) {
        if (eng::str::lower(obj.type) == "bomb") ++r.sites;
        else if (eng::str::lower(obj.file).find("silolcd") == std::string::npos) r.item = true;   // the Silo's consoles are not taken
    }
    for (const sf::LevelZone& z : level.zones) (z.team == sf::Team::Blue ? r.blue_zone : r.red_zone) = true;
    r.horror2 = !level.zombie_spawns.empty() && !level.human_spawns.empty();
    r.horror = !r.horror2 && level.id.size() > 6 && level.id.ends_with("horror");
    // Art Center's script says Escape and gives it nowhere to escape to: it is one of the sniper maps
    // (Docs/Research.md §3: "Sniper -- Base Camp, Toy Tower, Art Center").
    r.sniper = o == "sniper" || level.id == "artcenter";
    r.occupy = o == "occupy";
    r.pirate = o == "pirateship";
    r.deathmatch = o == "deathmatch";
    if ((o == "destroy" || o == "destory") && r.sites > 0) r.mission = Mission::Blast;
    else if (o == "takeback" && r.item && r.red_zone) r.mission = Mission::Capture;
    else if (o == "escape" && r.red_zone) r.mission = Mission::Escape;
    else if (o == "dual" && r.item && r.red_zone && r.blue_zone) r.mission = Mission::Dual;
    return r;
}

int stand_cell(const NavGraph& nav, const eng::Vec3& at, const std::vector<char>& walked, float radius) {
    for (int c : nav.nearest_cells(at, -1, radius, 500.0f, 2000))
        if (walked.empty() || walked[size_t(c)]) return c;
    return -1;
}

float item_reach(const NavGraph& nav, const eng::Vec3& item, const std::vector<char>& walked) {
    const int c = stand_cell(nav, item, walked, 600.0f);
    if (c < 0) return -1.0f;
    const eng::Vec3 chest = nav.node(c).pos + eng::Vec3{0, kChestHeight, 0};
    return std::max(kTakeReach, eng::length(item - chest) + 25.0f);
}

std::span<const CannonSpot> pirate_cannons(std::string_view map) {
    // map_cannon_state.kst, its first sheet, halved: the deck's two, then each shore's two.
    static const CannonSpot kSpots[] = {{{-1400, 1032, -2050}, {-1, 0, 0}}, {{1400, 1032, -2050}, {1, 0, 0}},  {{-5500, 2.5f, -225}, {1, 0, 0}},
                                        {{-5500, 2.5f, -3500}, {1, 0, 0}},  {{5500, 2.5f, -225}, {-1, 0, 0}},  {{5500, 2.5f, -3500}, {-1, 0, 0}}};
    if (map != "pirateship") return {};
    return kSpots;
}

eng::Vec3 cannon_aim(const CannonSpot& c, const eng::Vec3& aim) {
    eng::Vec3 a = eng::length_sq(aim) > 1e-6f ? eng::normalize(aim) : c.forward;
    // Up and down, then left and right, each within what the mount turns.
    const float pitch = std::clamp(std::asin(std::clamp(a.y, -1.0f, 1.0f)), -kCannonDown * eng::kDegToRad, kCannonUp * eng::kDegToRad);
    eng::Vec3 flat{a.x, 0, a.z};
    flat = eng::length_sq(flat) > 1e-6f ? eng::normalize(flat) : c.forward;
    const float limit = std::cos(kCannonSide * eng::kDegToRad);
    if (const float along = eng::dot(flat, c.forward); along < limit) {
        // Past its stop: as far round as it goes, on the side asked for.
        eng::Vec3 side = flat - c.forward * along;
        side = eng::length_sq(side) > 1e-6f ? eng::normalize(side) : eng::Vec3{-c.forward.z, 0, c.forward.x};
        flat = eng::normalize(c.forward * limit + side * std::sqrt(std::max(0.0f, 1.0f - limit * limit)));
    }
    return eng::normalize(flat * std::cos(pitch) + eng::Vec3{0, std::sin(pitch), 0});
}

int cannon_damage(float distance) {
    if (distance > kCannonBlast) return 0;
    if (distance <= kCannonFull) return kCannonDamageMax;
    const float t = (distance - kCannonFull) / (kCannonBlast - kCannonFull);
    return int(std::lround(float(kCannonDamageMax) + (float(kCannonDamageMin) - float(kCannonDamageMax)) * t));
}

eng::Vec3 cannon_ball_at(const eng::Vec3& origin, const eng::Vec3& velocity, float t) {
    return origin + velocity * t + eng::Vec3{0, -0.5f * kCannonGravity * t * t, 0};
}

const HorrorItemInfo& horror_item(HorrorItem i) {
    static const HorrorItemInfo kItems[kHorrorItems] = {
        {"Rebirth", "ui/texture/icon/ui_icon_zombieitem_rebirth.tga", "Brought down by the undead, you come back a human where you fell. It goes by itself.", false, 3000, 0},
        {"Rescue Kit", "ui/texture/icon/ui_icon_zombieitem_rescuekit.tga", "100 health back, at once.", false, 1000, 0},
        {"Silver Bullet", "ui/texture/icon/ui_icon_zombieitem_silverbullet.tga", "Your hits are 30% harder for 10 seconds.", false, 1500, 10},
        {"Blind Cleanse", "ui/texture/icon/ui_icon_zombieitem_blindcleanse.tga", "Black fog and smoke do not blind you for 6 seconds.", false, 800, 6},
        {"Blood Sucking", "ui/texture/icon/ui_icon_zombieitem_bloodsucking.tga", "An undead's: for 10 seconds your claws give you the health they take.", true, 1500, 10},
        {"Shout of Anger", "ui/texture/icon/ui_icon_zombieitem_shoutofanger.tga", "An undead's: your blows are 30% harder for 10 seconds.", true, 1500, 10},
        {"Undead Speed Up", "ui/texture/icon/ui_icon_zombieitem_undeadspeedup.tga", "An undead's: Super Speed's run for 7 seconds.", true, 1000, 7},
    };
    return kItems[size_t(i) < size_t(kHorrorItems) ? size_t(i) : 0];
}

bool mode_offers(Mode mode, const MapRules& m) {
    const bool own = m.horror || m.horror2 || m.pirate;   // maps made for one game type
    switch (mode) {
        case Mode::TeamBattle: return !own && m.mission != Mission::Elimination;
        case Mode::TeamDeathmatch:
        case Mode::TeamSlayer:
        case Mode::SingleBattle:
        case Mode::CaptureTheCaptain:
        case Mode::Captain: return !own;
        case Mode::Sniper: return m.sniper;
        case Mode::Horror: return m.horror;
        case Mode::Horror2: return m.horror2;
        case Mode::Occupy: return m.occupy;
        case Mode::Pirate: return m.pirate;
        case Mode::Training: return !m.horror && !m.horror2;
        default: return false;
    }
}

const SkillDef& skill_def(Skill s) {
    static const SkillDef kSkills[] = {
        {"", 0, 0},
        {"Super Speed", 20, 6},
        {"Super Jump", 15, 8},
        {"Black Fog", 25, 0},
        {"Search", 30, 6},
        {"Self Bomb", 0, 0},
        {"Crush", 12, 0},
        {"Drilling", 10, 1.2f},
        {"Throw", 10, 0},
        {"Snatch", 15, 0},
    };
    static_assert(std::size(kSkills) == size_t(Skill::Count));
    return kSkills[std::min<size_t>(size_t(s), size_t(Skill::Count) - 1)];
}

const UndeadDef& undead_def(Undead u) {
    using S = Skill;
    constexpr u8 sp = u8(S::Speed), jm = u8(S::Jump), sm = u8(S::Smoke), se = u8(S::Search), sb = u8(S::SelfBomb), cr = u8(S::Crush),
                 dr = u8(S::Drilling), th = u8(S::Throw), sn = u8(S::Snatch);
    // Each class's skills are the ones its own clips are for (motion/z<class>/): the Driller's
    // a_drilling and u_b_smoke, the Heavy's a_crash_jump and its u_08 throw, the Hunter's u_pulling;
    // the Boss has none of its own (a dash and a search, from the skill bar's art). Horror's zombies
    // have the five Docs/Research.md §3 lists (zwoman's l_bomb is the self bomb).
    //  name       model      motion     health power speed bars   HP    claw speed   skills               rank
    static const UndeadDef kUndead[] = {
        {"", "", "", 0, 0, 0, 0, 0, 1.0f, {}, 1},
        {"Zombie", "zman", "zman", 0, 0, 0, kInfectedHealth, kClawDamage, 1.05f, {sp, jm, sm, se, sb}, 1},
        {"Zombie", "zwoman", "zwoman", 0, 0, 0, kInfectedHealth, kClawDamage, 1.05f, {sp, jm, sm, se, sb}, 1},
        {"Boss", "zboss", "zboss", 0.50f, 0.46f, 0.63f, 1800, 48, 1.08f, {sp, se}, 1},
        {"Driller", "zdriller", "zdriller", 0.41f, 0.80f, 0.50f, 1584, 68, 1.00f, {dr, sm}, 2},
        {"Heavy", "zheavy", "zheavy", 0.41f, 0.80f, 0.50f, 1584, 68, 1.00f, {cr, th}, 1},
        {"Hunter", "zhunter", "zhunter", 0.24f, 0.52f, 0.50f, 1176, 51, 1.00f, {jm, sn}, 1},
    };
    static_assert(std::size(kUndead) == size_t(Undead::Count));
    return kUndead[std::min<size_t>(size_t(u), size_t(Undead::Count) - 1)];
}

Undead infected_body(u8 force_id) {
    const ForceDef* f = force(force_id);
    return f && std::string_view(f->model) == "mulan" ? Undead::Woman : Undead::Man;
}

const char* pirate_grade(int points) {
    static const struct {
        int at;
        const char* grade;
    } kGrades[] = {{60, "S"}, {45, "A+"}, {35, "A"}, {25, "B+"}, {18, "B"}, {12, "C"}, {7, "D"}, {3, "E"}};
    for (const auto& g : kGrades)
        if (points >= g.at) return g.grade;
    return "F";
}

}  // namespace lsf
