// Channels (Page1), the lobby (Page2) with Make Room, and the waiting room (Page3).
#include "Game/Screens/Screens.hpp"

#include "Game/Render/ModelStage.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_loading.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>

namespace lsf {

using namespace ui;

// ── Pictures the lobby screens share ───────────────────────────────────────────

Picture weapon_icon(App& app, const WeaponDef& w) {
    // SF_A_<CLASS>_<MODEL>_LB: the shop's picture of a weapon.
    const char* classes[] = {"rifle", "sniper", "mg", "pistol", "hg", "knife", "shotgun", "smg"};
    for (const char* c : classes)
        for (const char* ext : {"_lb.tga", "_lb.bmp"}) {
            const std::string key = std::string("sf_a_") + c + "_" + art_model(w) + ext;
            if (app.data().resolve(sf::Pack::Lobby, key)) return app.atlas().picture(sf::Pack::Lobby, key);
        }
    return {};
}

// ── The lobby's dialogs (the lobby itself is drawn from Page2, Screens/Pages.cpp) ──

namespace {

constexpr VanU32 kLabel = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kLimeInk = VAN_COL32(202, 228, 80, 255);
constexpr VanU32 kAttack = VAN_COL32(236, 110, 90, 255);
constexpr VanU32 kDefend = VAN_COL32(110, 150, 255, 255);

// Wrapped lines in the page's face, from (x0, y) across to x1; returns the y under them.
float words_at(float x0, float y, float x1, std::string_view s, VanU32 colour) {
    if (s.empty()) return y;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float size = ui::pgy(12.0f);
    VanFont* f = font_page_bold();
    const VanVec2 ts = f->CalcTextSizeA(size, 1e9f, ui::pgx(x1 - x0), s.data(), s.data() + s.size());
    dl->AddText(f, size, ui::pg(x0, y), colour, s.data(), s.data() + s.size(), ui::pgx(x1 - x0));
    return y + ts.y / ui::pgy(1.0f) + 3;
}

std::string players_label(const RoomSettings& r) {
    if (mode_info(r.mode).teams) return eng::str::format("%u vs %u", unsigned(r.max_players / 2), unsigned(r.max_players / 2));
    return eng::str::format("%u players", unsigned(r.max_players));
}

std::string goal_label(const RoomSettings& r) {
    const ModeInfo& mi = mode_info(r.mode);
    if (mi.goal_max == 0) return "-";
    if (mi.rounds) return eng::str::format("%u WIN", unsigned(r.goal));
    std::string unit = mi.goal_label;
    for (char& c : unit) c = char(std::toupper((unsigned char)c));
    return eng::str::format("%u %s", unsigned(r.goal), unit.c_str());
}

}  // namespace

// What a game type asks of you, where the map's own mission does not say (Docs/Modes.md).
const char* mode_brief(Mode m) {
    switch (m) {
        case Mode::TeamDeathmatch: return "Both sides respawn. The first team to the point goal wins: 2 a kill, 1 more for a headshot, grenade or knife kill.";
        case Mode::SingleBattle: return "Everyone for themselves, back in at once wherever there is room. The first to the kill count wins.";
        case Mode::Sniper: return "Reach the score by eliminating the enemy's snipers: sniper rifles, sidearms and knives only. Soldiers come back.";
        case Mode::CaptureTheCaptain: return "Kill the captain on the other side first: a big head, 1,000 HP and the captain's mark. A captain a round, each in turn; the rest come back.";
        case Mode::Captain: return "Kill every captain on the other side. A captain for every three, the side's health shared out between them, and every captain shown to all.";
        case Mode::Horror: return "The infected host zombies spread the virus to every last human; the humans must bring them down to survive. Friends won't stay friends for long.";
        case Mode::Training: return "Targets that stand and move. Nothing wears out and nothing counts.";
        case Mode::TeamSlayer: return "Points to the goal: more for a double, a multi, a head shot, a grenade or a knife. Rage after three deaths, magazines from the fallen, your killer marked.";
        case Mode::Occupy: return "The Silo: take both missile consoles, or bring the sample to one. The defenders hold until the clock runs out.";
        case Mode::Horror2: return "A quarter start as undead and pick a class: Boss, Driller, Heavy or Hunter, each with skills of its own. The undead come back; the humans have supplies and a girl to rescue.";
        case Mode::Pirate: return "Pirates against marines on the Pirate Ship: hold the three strongholds for points, open the treasure when it washes up.";
        default: return nullptr;
    }
}

// Make Room, in the kit: the map on the left (its picture as the hour will draw it, the arrows,
// the whole list), the room on the right (name, the staff's password, the game, the hour, the
// options) and the map's mission for each side under them.
void make_room_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 187, Y0 = 112, X1 = 837, Y1 = 652;
    if (!ui::dialog_begin("Make Room", X0, Y0, X1, Y1, "NEW ROOM", &st.create_open)) {
        st.create_open = false;
        return;
    }
    RoomSettings& r = st.create;
    // A game type the channel or the server does not play: the first one it does.
    const u16 allowed = s.modes_allowed();
    if (allowed && !(allowed & (1u << unsigned(r.mode)))) {
        r.mode = Mode(std::countr_zero(unsigned(allowed)));
        r.no_snipers = false;
        r.goal = mode_info(r.mode).goal_default;
        r.minutes = mode_info(r.mode).minutes_default;
        if (mode_info(r.mode).teams && (r.max_players & 1)) ++r.max_players;
    }

    // The maps this game type is played on, by the names the lobby gives them, All Random and Hot
    // Random first (App::maps_for). A map the game type is not played on is swapped for its first.
    const std::vector<std::string> maps = app.maps_for(r.mode);
    std::vector<std::string> titles;
    for (const std::string& id : maps) titles.push_back(app.map_title(id));
    int sel = -1;
    for (int i = 0; i < int(maps.size()); ++i)
        if (maps[size_t(i)] == r.map) sel = i;
    auto pick_map = [&](int i) {
        if (maps.empty()) return;
        sel = (i + int(maps.size())) % int(maps.size());
        r.map = maps[size_t(sel)];
        // The hour follows the map's own bake until the host chooses one.
        if (!st.create_hour_set) r.time_of_day = baked_time_of_day(r.map);
    };
    if (sel < 0 && !maps.empty()) {
        // The first real map (past the random two), else the first there is.
        int first = 0;
        while (first < int(maps.size()) - 1 && is_random_map(maps[size_t(first)])) ++first;
        pick_map(first);
    }

    // ── Left: the map ──
    ui::heading(199, 134, 469, "MAP");
    ui::well(199, 154, 469, 334);
    if (sel >= 0) {
        // The thumbnail as the hour will look: moonlight over a day map's picture.
        const bool night_look = r.time_of_day == TimeOfDay::Night && !baked_at_night(r.map);
        const VanU32 tint = night_look ? VAN_COL32(96, 118, 178, 255) : 0xFFFFFFFF;
        ui::picture_at(app.atlas().map_picture(r.map), 201, 156, 467, 332, tint);
        ui::text_at(203, 312, 465, 330, time_of_day_name(r.time_of_day), r.time_of_day == TimeOfDay::Night ? VAN_COL32(170, 190, 255, 255) : VAN_COL32(255, 222, 130, 255),
                    ui::Align::Right, true, 12.0f);
    }
    if (const int d = ui::arrows(1, 199, 340, 469, 359, sel >= 0 ? titles[size_t(sel)] : "-", maps.size() > 1); d != 0) pick_map(sel + d);
    const sf::MapInfo* info = sel >= 0 ? app.map_info(r.map) : nullptr;
    if (info && !info->mission.empty()) {
        ui::text_at(199, 362, 280, 380, "Mission :", kLabel, ui::Align::Left, true);
        ui::text_at(262, 362, 469, 380, info->mission, kLimeInk, ui::Align::Left, true);
    }
    if (const int clicked = ui::pick_list(2, 199, 384, 469, 592, titles, sel); clicked >= 0) pick_map(clicked);

    // ── Right: the room ──
    const bool staff = s.profile.game_master();
    ui::heading(489, 134, 825, "ROOM");
    ui::text_at(489, 154, 578, 173, "Room name", kLabel, ui::Align::Left, true);
    const bool enter = ui::edit_at(3, 578, 154, 825, 173, r.title, 30, "a name for the room");
    ui::text_at(489, 178, 578, 197, "Password", staff ? kLabel : VAN_COL32(128, 128, 122, 255), ui::Align::Left, true);
    if (!staff) r.password.clear();
    (void)ui::edit_at(4, 578, 178, 825, 197, r.password, 16, staff ? "none: an open room" : "staff only", true, staff);
    if (!staff) {
        bool hovered = false;
        (void)ui::region(40, 578, 178, 825, 197, &hovered);
        if (hovered) VanGui::SetTooltip("Only staff can lock a room with a password.");
    }

    ui::heading(489, 208, 825, "GAME");
    const ModeInfo& mi = mode_info(r.mode);
    const float lx = 489, cx0 = 578, cx1 = 825;
    ui::text_at(lx, 228, cx0, 247, "Game type", kLabel, ui::Align::Left, true);
    // Each game type, then its "(no sniper)" variant where the original had one.
    if (const int d = ui::arrows(5, cx0, 228, cx1, 247, game_type_name(r)); d != 0) step_game_type(r, d, allowed);
    ui::text_at(lx, 252, cx0, 271, mi.goal_max ? mi.goal_label : "Win condition", kLabel, ui::Align::Left, true);
    if (const int d = ui::arrows(6, cx0, 252, cx1, 271, goal_label(r), mi.goal_max > 0); d != 0)
        r.goal = u8(std::clamp(int(r.goal) + d * std::max<int>(1, mi.goal_step), int(mi.goal_min), int(mi.goal_max)));
    ui::text_at(lx, 276, cx0, 295, mi.rounds ? "Round time" : "Time", kLabel, ui::Align::Left, true);
    if (const int d = ui::arrows(7, cx0, 276, cx1, 295, eng::str::format("%u MIN", unsigned(r.minutes))); d != 0)
        r.minutes = u8(std::clamp(int(r.minutes) + d, 1, 30));
    ui::text_at(lx, 300, cx0, 319, "Players", kLabel, ui::Align::Left, true);
    if (const int d = ui::arrows(8, cx0, 300, cx1, 319, players_label(r)); d != 0) {
        const int step = mi.teams ? 2 : 1;
        r.max_players = u8(std::clamp(int(r.max_players) + d * step, 2, kMaxRoomPlayers));
    }
    ui::text_at(lx, 326, cx0, 344, "Time of day", kLabel, ui::Align::Left, true);
    for (int t = 0; t < int(TimeOfDay::Count); ++t) {
        const TimeOfDay hour = TimeOfDay(t);
        const char* tip = hour == TimeOfDay::Day ? (baked_at_night(r.map) ? "Daylight over the map's night" : "The map as it was made: daylight")
                                                 : (baked_at_night(r.map) ? "The map as it was made: night" : "Moonlight");
        if (ui::radio(9 + t, cx0 + 4 + 84.0f * float(t), 326, time_of_day_name(hour), r.time_of_day == hour, true, tip)) {
            r.time_of_day = hour;
            st.create_hour_set = true;
        }
    }

    ui::heading(489, 352, 825, "OPTIONS");
    (void)ui::check(20, 495, 372, "3rd person view (Q)", r.third_person, true, "Q switches to a camera behind your soldier");
    (void)ui::check(21, 662, 372, "Join in progress", r.free_join, true, "Others may join once the game has started");
    (void)ui::check(22, 495, 394, "Observer seats", r.observers, true, "Spectators may watch from the observer seats");
    (void)ui::check(23, 662, 394, "Balance the teams", r.team_balance, mi.teams, "New arrivals join the smaller side");

    // The map's mission, for each side.
    ui::heading(489, 420, 825, "MISSION");
    ui::well(489, 440, 825, 592);
    constexpr VanU32 kWords = VAN_COL32(230, 230, 226, 255);
    if (const char* brief = mode_brief(r.mode)) {
        float y = words_at(495, 446, 819, mode_name(r.mode), kLimeInk);
        (void)words_at(495, y, 819, brief, kWords);
    } else if (info && !info->attack_text.empty()) {
        float y = 446;
        y = words_at(495, y, 819, "Attack", kAttack);
        y = words_at(495, y, 819, info->attack_text, kWords) + 4;
        y = words_at(495, y, 819, "Defend", kDefend);
        (void)words_at(495, y, 819, info->defence_text, kWords);
    } else {
        // A map MapName.txt does not describe: its mission in general words.
        const Mission mission = is_random_map(r.map) ? Mission::Elimination : app.map_mission(r.map);
        float y = words_at(495, 446, 819, "Attack", kAttack);
        y = words_at(495, y, 819, mission_attack_text(mission), kWords) + 4;
        y = words_at(495, y, 819, "Defend", kDefend);
        (void)words_at(495, y, 819, mission_defence_text(mission), kWords);
    }

    // Confirm / Cancel, under the middle.
    const bool ok = !r.map.empty();
    if (ui::kit_button(30, "confirm_1", 434, 602, 507, 643, ok, "Make the room") || (enter && ok)) {
        sanitize(r);
        s.create_room(r);
        st.create_open = false;
        ui::dialog_close();
    }
    if (ui::kit_button(31, "cancel_1", 517, 602, 590, 643)) {
        st.create_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

void password_modal(App& app) {
    ScreenState& st = app.state();
    if (!begin_modal("Room password", "ROOM PASSWORD", 420, 220)) {
        st.password_open = false;
        return;
    }
    VanGui::TextUnformatted("This room is locked.");
    const bool enter = input("##rpw", "password", st.join_password, 388, true, VanGuiInputTextFlags_EnterReturnsTrue);
    VanGui::Dummy({0, px(10)});
    if (button("Join", 180, 42, Style::Primary) || enter) {
        app.session().join_room(st.password_room, st.join_password);
        st.password_open = false;
        st.join_password.clear();
        VanGui::CloseCurrentPopup();
    }
    VanGui::SameLine();
    if (button("Cancel", 180, 42)) {
        st.password_open = false;
        VanGui::CloseCurrentPopup();
    }
    end_modal();
}

}  // namespace lsf
