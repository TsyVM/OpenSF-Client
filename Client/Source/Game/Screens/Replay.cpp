// The Replay screen: a match recording being watched (App::watch_replay, World/Watch.cpp). The
// map loads as for a match; then the world plays the recording, and a bar along the bottom has
// Play / Pause (Space), the speed ([ and ]), the timeline to drag, the time, and Leave (Esc).
#include "Game/Screens/Screens.hpp"

#include "Game/App.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace lsf {

using namespace ui;

namespace {

std::string clock_text(double ms) {
    const int s = int(ms / 1000.0);
    return eng::str::format("%d:%02d", s / 60, s % 60);
}

constexpr float kSpeeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

}  // namespace

void draw_replay(App& app) {
    App::Watching* w = app.watching();
    GameWorld* world = app.world();
    if (!w || !world) {
        app.go(Screen::Lobby);
        return;
    }
    const Palette& p = pal();
    if (!world->ready()) {
        const HudStage hud;
        backdrop(app.atlas().picture(sf::Pack::Lobby, "sf.jpg"), 0.4f);
        VanDrawList* dl = VanGui::GetForegroundDrawList();
        text(dl, font_display(), 36, 60, 720, col(p.gold_bright), eng::str::format("MATCH #%u", w->match));
        text(dl, font_bold(), 18, 60, 770, col(p.lime), std::string(mode_name(world->room().mode)) + " on " + app.map_title(w->map));
        progress(dl, follow("replayload", world->progress(), 0.25f), 60, 846, stage_w() - 120, 12);
        text(dl, font_body(), 14, stage_w() - 60, 818, col(p.text_dim), world->status(), Align::Right);
        return;
    }
    {
        const HudStage hud;
        world->draw_hud();
    }
    eng::Input& in = app.window().input();
    const bool typing = VanGui::GetIO().WantTextInput;
    const bool modal = app.state().staff_open || app.state().settings_open;
    int speed = 2;
    for (int i = 0; i < int(std::size(kSpeeds)); ++i)
        if (std::fabs(kSpeeds[i] - w->speed) < 0.01f) speed = i;
    if (!typing && !modal) {
        if (in.key_pressed(VK_SPACE) && world->watch_view() != GameWorld::WatchView::Free) w->paused = !w->paused;
        if (in.key_pressed(VK_OEM_4)) w->speed = kSpeeds[std::max(0, speed - 1)];
        if (in.key_pressed(VK_OEM_6)) w->speed = kSpeeds[std::min(int(std::size(kSpeeds)) - 1, speed + 1)];
        if (in.key_pressed(VK_ESCAPE)) {
            app.leave_replay();
            return;
        }
    }
    // The bar, on the page grid along the bottom.
    page_begin("##replaybar");
    const float y0 = 712, y1 = 756;
    fill_at(0, y0 - 6, kPageW, kPageH, VAN_COL32(0, 0, 0, 170));
    if (text_button(9701, 12, y0, 92, y0 + 26, w->paused ? "Play" : "Pause")) w->paused = !w->paused;
    if (const int d = arrows(9702, 100, y0 + 2, 200, y0 + 24, eng::str::format("x%g", double(w->speed))); d)
        w->speed = kSpeeds[std::clamp(speed + d, 0, int(std::size(kSpeeds)) - 1)];
    const double length = double(app.replay_length_ms());
    float at = float(w->ms);
    if (trackbar(9703, 212, y0 + 2, 850, y0 + 24, at, 0.0f, float(std::max(1.0, length)))) app.replay_seek(double(at));
    text_at(856, y0 + 4, 940, y0 + 22, clock_text(w->ms) + " / " + clock_text(length), VAN_COL32(236, 236, 232, 255), Align::Left, true, 12.0f);
    if (text_button(9704, 944, y0, 1012, y0 + 26, "Leave")) {
        page_end();
        app.leave_replay();
        return;
    }
    text_at(12, y0 + 30, 1012, y1, eng::str::format("Match #%u  -  %s on %s%s.  Space: pause   [ ]: speed   Esc: leave", w->match, mode_name(world->room().mode),
                                                    app.map_title(w->map).c_str(), w->complete ? "" : "  (cut short)"),
            VAN_COL32(160, 160, 152, 255), Align::Left, false, 11.0f);
    page_end();
}

}  // namespace lsf
