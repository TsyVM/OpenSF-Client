// Loading (the map's own loading picture), the match (the world and its HUD) and the results.
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_loading.h>

#include <algorithm>

namespace lsf {

using namespace ui;

void draw_loading(App& app) {
    const HudStage hud;
    const Palette& p = pal();
    GameWorld* world = app.world();
    const std::string map = world ? world->map_id() : "";
    const sf::Level* level = world ? world->level() : nullptr;
    // SF_L_<map>.jpg: the loading picture the world script names.
    const Picture pic = app.atlas().loading_picture(level ? level->load_image : "", map);
    ui::backdrop(pic.valid() ? pic : app.atlas().picture(sf::Pack::Lobby, "sf.jpg"), 0.1f);
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    dl->AddRectFilledMultiColor(stage(0, 700), stage(stage_w(), stage_h()), 0, 0, VAN_COL32(0, 0, 0, 230), VAN_COL32(0, 0, 0, 230));
    text(dl, font_display(), 40, 60, 730, col(p.gold_bright), app.map_title(map));
    if (world) {
        text(dl, font_bold(), 18, 60, 782, col(p.lime), std::string(mode_name(world->room().mode)) + "  \xC2\xB7  " + time_of_day_name(world->room().time_of_day));
        if (const sf::MapInfo* info = app.map_info(map)) text(dl, font_body(), 16, 60, 808, col(p.text_dim), info->mission + ": " + info->attack_text);
        const float f = follow("loadbar", world->progress(), 0.25f);
        progress(dl, f, 60, 846, stage_w() - 120, 12);
        text(dl, font_body(), 14, stage_w() - 60, 818, col(p.text_dim), world->status(), Align::Right);
        spinner("##loadspin", stage_w() - 76, 760, 16, p.gold);
        if (world->ready()) app.go(Screen::Match);
    }
    // Tips, as the original's loading screens told them.
    static const char* tips[] = {
        "Crouch in the air as you jump to climb onto ledges (the K-jump).",
        "Headshots and knife kills score extra in Team Deathmatch.",
        "Press Tab for the scores, Enter to talk, Y for team chat.",
        "Z, X and C send radio messages to your team.",
        "The guns you buy wear with use: mend them in the inventory before they break.",
    };
    const int tip = int(app.now() / 5.0) % 5;
    text(dl, font_body(), 15, stage_w() * 0.5f, 875, col(p.text_dim, 0.8f), tips[tip], Align::Center);
}

void draw_match(App& app) {
    GameWorld* world = app.world();
    if (!world) {
        app.go(app.session().room ? Screen::Room : Screen::Servers);
        return;
    }
    if (!world->ready()) {
        draw_loading(app);
        return;
    }
    const HudStage hud;
    world->draw_hud();
}

void draw_result(App& app) {
    const HudStage hud;
    const Palette& p = pal();
    GameWorld* world = app.world();
    ScreenState& st = app.state();
    if (app.screen_time() < 0.05) st.result_since = app.now();
    if (world) world->draw_hud();
    // Background list: the "Back to the room" button's window must sit above the panel.
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const auto& over = app.session().match_over;
    if (over) {
        // In the scoreboard's own result strip (the client's tab_underbar_result, under the board).
        float sx0 = stage_w() * 0.5f - 322, sy0 = 657, sx1 = sx0 + 645, sy1 = sy0 + 140;
        if (world) (void)world->result_strip(sx0, sy0, sx1, sy1);
        const float k = (sx1 - sx0) / 576.0f;
        const float y = sy0 - 4 + slide("result.panel", 30);
        const float lx = sx0 + 152 * k;
        for (const proto::Reward& r : over->rewards) {
            if (r.id != app.session().session_id) continue;
            text(dl, font_heading(), 24, lx, y + 36, col(p.lime), eng::str::format("+%u XP", r.xp));
            text(dl, font_heading(), 24, lx + 150, y + 36, col(p.gold_bright), eng::str::format("+%u SP", r.sp));
            if (r.ranked_up) text(dl, font_heading(), 24, lx + 300, y + 36, col(p.warn), "PROMOTED!");
            // What the rewards added: an event's pay, a promotion's duffle bags, a box dropped.
            std::string extra;
            if (r.sp_pct > 100 || r.xp_pct > 100) {
                if (r.sp_pct > 100) extra += eng::str::format("Event SP x%.2g   ", double(r.sp_pct) / 100.0);
                if (r.xp_pct > 100) extra += eng::str::format("Event XP x%.2g   ", double(r.xp_pct) / 100.0);
            }
            if (r.clan_points) extra += eng::str::format("Clan Point : +%u   ", unsigned(r.clan_points));
            if (r.special) extra += eng::str::format("Special points +%u SP   ", unsigned(r.special));
            if (const SetInfo& set = set_info(ItemSet(r.set)); set.xp_pct > 0) extra += eng::str::format("%s set: XP +%d%%   ", set.name, set.xp_pct);
            if (r.bags) extra += eng::str::format("%u Duffle Bag%s   ", unsigned(r.bags), r.bags == 1 ? "" : "s");
            if (r.dropped < kBoxKinds) extra += std::string("Found: ") + box_info(r.dropped).name;
            if (!extra.empty()) text(dl, font_body(), 14, lx, y + 68, col(p.gold_bright), extra);
            // TVAS works the pay out (PR-3); the day's limit is said plainly (PR-13).
            const float ly = extra.empty() ? y + 68 : y + 88;
            if (r.pending) text(dl, font_body(), 13, lx, ly, col(p.text_dim), "Team Vanilla is counting this match's pay...");
            else if (r.xp_capped) text(dl, font_body(), 13, lx, ly, col(p.warn), "Daily experience limit reached: SP still counts; resets at 00:00 UTC");
            else if (r.levels_left < 25) text(dl, font_body(), 13, lx, ly, col(p.text_dim), eng::str::format("%u of today's 25 levels left", unsigned(r.levels_left)));
        }
        const float left = std::max(0.0f, 12.0f - float(app.now() - st.result_since));
        text(dl, font_body(), 13, sx0 + 16 * k, sy1 - 30, col(p.text_dim), eng::str::format("Back to the room in %.0f s", double(left)));
        if (button_at("##backroom", "Back to the room", sx1 - 236, sy1 - 52, 220, 40, Style::Primary) || left <= 0) {
            app.session().match_over.reset();
            app.leave_match();
        }
    } else if (app.screen_time() > 2.0) {
        app.leave_match();
    }
}

}  // namespace lsf
