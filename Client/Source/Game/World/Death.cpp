// Down, and between rounds (TacticalFPS's Death.cpp, Gameplan 2.5-2.7 and 2.9):
//
//   - the death screen: who killed you, with what, from how far, and how much they had left -- the
//     question everybody asks -- with what each of you did to the other this life, and how long
//     until you are back (a respawn) or that you wait for the next round;
//   - spectating, in a round with no respawn: after a moment on your own body, a living teammate's
//     view (anyone's in a game without sides): click for the next, right click the one before,
//     Space their eyes or behind them;
//   - the round's summary under the original's win / lose / draw art: why it ended, the score, and
//     who did the most;
//   - the netgraph (the options' "Show the net graph"): the ping and the frame rate over the last three
//     seconds, sampled every 50 ms so the graph means the same at any frame rate.
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Screens/Screens.hpp"
#include "Game/Settings.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

using namespace proto;

namespace {

constexpr double kRespawnSeconds = 3.0;   // the server's (Server/Match.cpp)
constexpr size_t kNetSamples = 60;        // three seconds at 20 a second

}  // namespace

void GameWorld::spectate_next(int dir) {
    const bool teams = mode_info(settings_.mode).teams;
    std::vector<u32> ids;
    for (const auto& [id, p] : players_)
        if (id != me_ && p.alive && p.body.model && (!teams || p.team == team_)) ids.push_back(id);
    if (ids.empty()) {
        spectating_ = 0;
        return;
    }
    auto it = std::find(ids.begin(), ids.end(), spectating_);
    const int at = it == ids.end() ? (dir > 0 ? -1 : 0) : int(it - ids.begin());
    spectating_ = ids[size_t((at + dir + int(ids.size())) % int(ids.size()))];
}

void GameWorld::spectate_update() {
    eng::Input& in = app_.window().input();
    const bool free = !dialog_up() && !menu_open_ && !chat_open_;
    // The one watched went down (or left): the next living one.
    if (spectating_) {
        auto it = players_.find(spectating_);
        if (it == players_.end() || !it->second.alive) spectating_ = 0;
    }
    if (!spectating_) spectate_next(1);
    if (free) {
        if (in.mouse_pressed(0)) spectate_next(1);
        if (in.mouse_pressed(1)) spectate_next(-1);
        if (in.key_pressed(VK_SPACE)) spec_view_ = spec_view_ == WatchView::Eyes ? WatchView::Chase : WatchView::Eyes;
        // On the touch screen: Fire steps through the team, Jump changes the view.
        if (touch_.pressed(Action::Shoot)) spectate_next(1);
        if (touch_.pressed(Action::Jump)) spec_view_ = spec_view_ == WatchView::Eyes ? WatchView::Chase : WatchView::Eyes;
        // On a controller: the shoulder buttons step through the team, A (Cross) changes the view.
        if (const eng::Gamepad* pad = app_.pad_in_hand()) {
            if (pad->pressed(eng::kPadRB)) spectate_next(1);
            if (pad->pressed(eng::kPadLB)) spectate_next(-1);
            if (pad->pressed(eng::kPadA)) spec_view_ = spec_view_ == WatchView::Eyes ? WatchView::Chase : WatchView::Eyes;
        }
    }
    if (spectating_) camera_on(spectating_, spec_view_);
}

void GameWorld::draw_death(double now) {
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    const ModeInfo& mode = mode_info(settings_.mode);
    const double since = now - died_at_;
    const bool spectating = spectating_ && !mode.respawn && since > 2.5;
    if (spectating) {
        const auto it = players_.find(spectating_);
        const std::string who = it != players_.end() ? it->second.name : std::string("?");
        ui::panel(dl, W * 0.5f - 230, 96, 460, 52, nullptr);
        ui::text(dl, ui::font_bold(), 17, W * 0.5f, 102, ui::col(pal.gold_bright), "SPECTATING  " + who, ui::Align::Center);
        const char* view = spec_view_ == WatchView::Eyes ? "behind them" : "their eyes";
        ui::text(dl, ui::font_body(), 12, W * 0.5f, 126, ui::col(pal.text_dim),
                 touch_.shown() ? std::string("FIRE: next   JUMP: ") + view : std::string("Click: next   Right click: previous   Space: ") + view,
                 ui::Align::Center);
    }
    // The panel: full while the death is fresh, a strip along the bottom while spectating.
    const float w = 560, h = spectating ? 58.0f : 132.0f;
    const float x = W * 0.5f - w * 0.5f, y = spectating ? H - 190.0f : H * 0.58f;
    ui::panel(dl, x, y, w, h, nullptr);
    const DeathInfo d = death_.value_or(DeathInfo{});
    const VanU32 team_ink = ui::col(d.team == Team::Blue ? pal.blue : d.team == Team::Red ? pal.red : pal.gold_bright);
    if (!death_ || d.killer == 0) {
        const char* what = !death_ ? "YOU ARE DOWN" : (d.flags & kKillFall) ? "YOU FELL TOO FAR" : "YOU TOOK YOURSELF OUT";
        ui::text(dl, ui::font_display(), 28, x + w * 0.5f, y + 12, ui::col(pal.text), what, ui::Align::Center);
    } else {
        ui::text(dl, ui::font_bold(), 14, x + 16, y + 10, ui::col(pal.text_dim), (d.flags & kKillTeam) ? "KILLED BY A TEAMMATE" : "KILLED BY");
        ui::text(dl, ui::font_display(), 28, x + 16, y + 26, team_ink, d.name);
        if (const WeaponDef* wd = weapon(d.weapon)) {
            const ui::Picture pic = weapon_art(*wd, 'h');
            if (pic.valid() && !spectating) {
                const float ph = 44, pw = std::min(190.0f, ph * pic.w / std::max(1.0f, pic.h));
                ui::picture(dl, pic, x + w - 16 - pw, y + 12, pw, ph, 0xFFFFFFFFu, false);
            }
            ui::text(dl, ui::font_body(), 13, x + w - 16, spectating ? y + 12 : y + 58, ui::col(pal.text_dim), wd->name, ui::Align::Right);
        }
        if (!spectating) {
            std::string line = d.killer_hp >= 0 ? eng::str::format("They had %d HP left", d.killer_hp) : std::string();
            if (d.distance > 0) line += eng::str::format("%s%.0f m away", line.empty() ? "" : "   -   ", double(d.distance));
            if (d.flags & kKillWall) line += line.empty() ? "THROUGH A WALL" : "   -   THROUGH A WALL";
            if (d.flags & kKillHeadshot) line += line.empty() ? "HEADSHOT" : "   -   HEADSHOT";
            ui::text(dl, ui::font_bold(), 15, x + 16, y + 66, ui::col(d.killer_hp > 0 && d.killer_hp <= 25 ? pal.warn : pal.text), line);
            ui::text(dl, ui::font_body(), 13, x + 16, y + 90, ui::col(pal.text_dim),
                     eng::str::format("They hit you %d time%s for %d.   You hit them %d time%s for %d.", d.hits_taken, d.hits_taken == 1 ? "" : "s", d.taken,
                                      d.hits_dealt, d.hits_dealt == 1 ? "" : "s", d.dealt));
        }
    }
    // When you are back (the game type's say: Horror's infected rise in two seconds, Horror Mode 2's
    // undead come back, a captain or an escaped soldier is out until the next round).
    std::string back;
    const RoleNow* role = role_of(me_);
    const bool undead_back = settings_.mode == Mode::Horror2 && me_undead();
    const bool rising = settings_.mode == Mode::Horror && me_undead() && since < 3.0;
    const bool out = role && (MatchRole(role->role) == MatchRole::Captain || MatchRole(role->role) == MatchRole::Escaped);
    if (rising) back = "Rising...";
    else if (undead_back) back = eng::str::format("Back in %.1f s", std::max(0.0, double(kUndeadRespawn) - since));
    else if (mode.respawn && !out) back = eng::str::format("Back in %.1f s", std::max(0.0, kRespawnSeconds - since));
    else back = "Waiting for the next round";
    ui::text(dl, ui::font_bold(), 14, x + w - 16, y + h - 22, ui::col(pal.lime), back, ui::Align::Right);
}

void GameWorld::test_death() {
    DeathInfo d;
    d.killer = 0xFFFFFFF0u;
    d.name = "Valdentia";
    d.team = team_ == Team::Red ? Team::Blue : Team::Red;
    for (const WeaponDef& w : weapons())
        if (w.code == "A013") d.weapon = w.id;
    d.flags = kKillHeadshot;
    d.killer_hp = 23;
    d.distance = 18.4f;
    d.taken = 104, d.hits_taken = 3;
    d.dealt = 77, d.hits_dealt = 2;
    death_ = d;
    alive_ = false;
    died_at_ = app_.now();
}

void GameWorld::test_round_end() {
    test_summary_ = true;
    result_art_ = 0;
    result_at_ = app_.now();
    round_reason_ = 0;
    red_score_ = 3, blue_score_ = 1;
}

void GameWorld::draw_round_summary(double now) {
    if (result_art_ < 0 || (!mode_info(settings_.mode).rounds && !test_summary_)) return;
    const float rt = float(now - result_at_);
    if (rt < 0.4f || rt > 4.6f) return;
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    const float a = std::min(1.0f, std::min((rt - 0.4f) * 4.0f, (4.6f - rt) * 2.0f));
    static const char* reasons[] = {"Every soldier of one side is down.", "Time ran out: the defenders held.", "The mission was carried out.",
                                    "One side surrendered."};
    const float x = W * 0.5f - 250, y = H * 0.3f + 90, w = 500;
    // The most kills so far, three of them.
    std::vector<const PlayerView*> best;
    for (const auto& [id, p] : players_)
        if (p.team != Team::Observer) best.push_back(&p);
    std::sort(best.begin(), best.end(), [](const PlayerView* l, const PlayerView* r) { return l->kills != r->kills ? l->kills > r->kills : l->deaths < r->deaths; });
    if (best.size() > 3) best.resize(3);
    const float h = 72.0f + 22.0f * float(best.size());
    ui::panel(dl, x, y, w, h, nullptr);
    ui::text(dl, ui::font_body(), 14, x + w * 0.5f, y + 10, ui::col(pal.text_dim, a), reasons[std::min<int>(round_reason_, 3)], ui::Align::Center);
    ui::text(dl, ui::font_display(), 26, x + w * 0.5f - 20, y + 32, ui::col(pal.red, a), std::to_string(red_score_), ui::Align::Right);
    ui::text(dl, ui::font_display(), 26, x + w * 0.5f, y + 32, ui::col(pal.text, a), ":", ui::Align::Center);
    ui::text(dl, ui::font_display(), 26, x + w * 0.5f + 20, y + 32, ui::col(pal.blue, a), std::to_string(blue_score_));
    float ly = y + 70;
    for (const PlayerView* p : best) {
        const VanU32 ink = ui::col(p->team == Team::Blue ? pal.blue : p->team == Team::Red ? pal.red : pal.text, a);
        ui::text(dl, ui::font_bold(), 14, x + 40, ly, ink, p->name);
        ui::text(dl, ui::font_body(), 14, x + w - 40, ly, ui::col(pal.text, a), eng::str::format("%u kills  %u deaths", unsigned(p->kills), unsigned(p->deaths)),
                 ui::Align::Right);
        ly += 22;
    }
}

void GameWorld::draw_netgraph(double now) {
    if (!app_.settings().show_netgraph) return;
    if (now - net_sampled_ >= 0.05) {
        net_sampled_ = now;
        net_ping_.push_back(offline_ ? 0.0f : float(app_.session().ping_ms()));
        net_fps_.push_back(app_.fps());
        while (net_ping_.size() > kNetSamples) net_ping_.pop_front();
        while (net_fps_.size() > kNetSamples) net_fps_.pop_front();
    }
    if (net_ping_.empty()) return;
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    const float x = W - 270, y = H - 250, w = 240, h = 44;
    dl->AddRectFilled(ui::stage(x, y), ui::stage(x + w, y + h), VAN_COL32(0, 0, 0, 120));
    // The ping as bars against 200 ms; green under 80, orange under 160, red above.
    const float bw = w / float(kNetSamples);
    for (size_t i = 0; i < net_ping_.size(); ++i) {
        const float ms = net_ping_[i];
        const float bh = std::min(1.0f, ms / 200.0f) * h;
        const VanU32 c = ms < 80 ? VAN_COL32(110, 200, 70, 220) : ms < 160 ? VAN_COL32(230, 170, 60, 220) : VAN_COL32(230, 80, 60, 220);
        const float bx = x + w - bw * float(net_ping_.size() - i);
        dl->AddRectFilled(ui::stage(bx, y + h - bh), ui::stage(bx + bw - 1, y + h), c);
    }
    const float ping = net_ping_.back(), fps = net_fps_.back();
    float worst = 0;
    for (float f : net_fps_) worst = worst == 0 ? f : std::min(worst, f);
    ui::text(dl, ui::font_body(), 12, x, y - 18, ui::col(pal.text_dim),
             offline_ ? eng::str::format("offline   %.0f fps (low %.0f)", double(fps), double(worst))
                      : eng::str::format("ping %.0f ms   %.0f fps (low %.0f)", double(ping), double(fps), double(worst)));
}

}  // namespace lsf
