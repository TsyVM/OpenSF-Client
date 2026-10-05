// A server bot's state between ticks (TacticalFPS's Server/Bots.hpp). The thinking is in
// Server/Bots.cpp, as Match's own: a bot is a soldier in a match like any other, and reads the
// match the way the rules do -- the same sight table the snapshot cull fills (it sees only what a
// player in its place would be sent), the same map, the same weapons.
#pragma once

#include "Engine/Core/Hash.hpp"
#include "Engine/Core/Math.hpp"
#include "Game/Navigation.hpp"
#include "Game/Rules.hpp"

namespace lsfs {

using lsf::BotSkill;

struct BotBrain {
    BotSkill skill = BotSkill::Normal;
    eng::Rng rng{1};

    // Getting about.
    lsf::NavFollower follow;
    int goal = -1;
    double repath_at = 0;
    eng::Vec3 last_pos;
    double last_progress = 0;
    int stuck = 0;
    double wander_until = 0;
    bool still = false;          // a Training target that stands rather than wanders
    eng::u8 test_pace = 0;       // tests (Match::test_bots_pace): 1 crouch-walks, 2 runs, side to side where it stands
    // The game type's objective, and the cell it stands on to reach it (one it has a path to).
    eng::Vec3 objective_at{1e9f, 0, 0};
    int objective_cell = -1;

    // Fighting.
    eng::u32 target = 0;         // a soldier's id, 0 none
    double seen_since = 0;       // when the target came into view
    double shoot_from = 0;       // reaction time: may open fire after this
    bool head = false;           // aiming for the head this engagement
    float err_yaw = 0, err_pitch = 0;   // where its aim is off by, closing while it tracks
    float yaw = 0, pitch = 0;    // where it is looking
    double burst_until = 0, rest_until = 0;
    double strafe_until = 0;
    int strafe = 1;
    // What it last knew of an enemy it cannot see: a shot heard, or where one went out of sight.
    eng::Vec3 lead;
    double lead_until = 0;

    // Its gun: the primary's magazine, the next shot, a reload, the recoil on the view.
    int clip = 0, reserve = 0;
    double next_shot = 0, reload_until = 0;
    float recoil = 0, cone = 0;
};

}  // namespace lsfs
