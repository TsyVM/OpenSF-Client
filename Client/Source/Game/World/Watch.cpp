// Watching a match recording (Game/Replay.hpp): the match world fed from the file, with a camera
// of its own. TacticalFPS's watching mode, the same rules:
//   - the viewer's seat is nobody's (MatchLoad.you = 0), so nothing here moves, shoots or sends;
//   - a seek backwards is a reset and the frames fed again from the start, `fast` (the state only:
//     no shots, sounds or effects) until the last moment before the place sought;
//   - every HUD piece that assumes a local soldier has a watching answer: this one.
//
// The camera: a soldier's eyes (their aim, their body not drawn), a chase camera behind them, or
// free (W A S D, the right mouse button held to look, Shift faster). Left / Right or a click pick
// the soldier; V changes the view.
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

using eng::Vec3;
using namespace proto;

bool GameWorld::dialog_up() const {
    const ScreenState& st = const_cast<App&>(app_).state();
    return st.staff_open || st.report_open || st.vote_open || st.settings_open;
}

void GameWorld::start_watching(const MatchLoad& load) {
    watching_ = true;
    start_loading(load, true);
}

void GameWorld::feed(std::span<const u8> msg, bool fast) {
    if (fast) {
        // While seeking: what changes the state, not what only looks or sounds.
        switch (peek_id(msg)) {
            case Msg::ShotFx:
            case Msg::GrenadeFx:
            case Msg::RadioFx:
            case Msg::Damage:
            case Msg::ChatLine:
                return;
            default:
                break;
        }
        on_message(msg);
        return;
    }
    watch_inbox_.emplace_back(msg.begin(), msg.end());
}

void GameWorld::watch_reset() {
    watch_inbox_.clear();
    tracers_.clear();
    grenades_.clear();
    smokes_.clear();
    cases_.clear();
    marks_.clear();
    puffs_.clear();
    blasts_.clear();
    feed_.clear();
    flashed_until_ = -10;
    banner_.clear();
    red_score_ = blue_score_ = 0;
    for (auto& [id, p] : players_) {
        p.alive = false;
        p.samples.clear();
        p.snap_time = 0;
        p.kills = p.deaths = p.assists = p.score = 0;
    }
}

void GameWorld::follow_next(int dir) {
    std::vector<u32> ids;
    for (const auto& [id, p] : players_)
        if (p.body.model && p.team != Team::Observer) ids.push_back(id);
    if (ids.empty()) return;
    auto it = std::find(ids.begin(), ids.end(), follow_);
    int at = it == ids.end() ? 0 : int(it - ids.begin());
    // The next living one, if anyone is living.
    for (size_t k = 0; k < ids.size(); ++k) {
        at = (at + dir + int(ids.size())) % int(ids.size());
        if (players_[ids[size_t(at)]].alive) break;
    }
    follow_ = ids[size_t(at)];
}

void GameWorld::update_watch(float dt) {
    eng::Input& in = app_.window().input();
    const bool typing = VanGui::GetIO().WantTextInput;
    const bool panel = VanGui::IsAnyItemHovered() || VanGui::IsAnyItemActive() || app_.state().staff_open;
    if (follow_ && !players_.contains(follow_)) follow_ = 0;
    if (!follow_) follow_next(1);
    if (!typing) {
        if (in.key_pressed(VK_RIGHT) || (in.mouse_pressed(0) && !panel)) follow_next(1);
        if (in.key_pressed(VK_LEFT)) follow_next(-1);
        if (in.key_pressed('V')) watch_view_ = WatchView((int(watch_view_) + 1) % 3);
    }
    scoreboard_ = !typing && in.key_down(VK_TAB);
    const PlayerView* p = follow_ && players_.contains(follow_) ? &players_[follow_] : nullptr;
    camera_.fov_x = app_.settings().fov;
    if (watch_view_ == WatchView::Free || !p) {
        // Free: where the chase camera last was, then wherever it is flown.
        if (!free_placed_) {
            free_eye_ = p ? p->position + Vec3{0, 260, 0} : Vec3{0, 400, 0};
            free_yaw_ = p ? p->yaw : 0, free_pitch_ = -20;
            free_placed_ = true;
        }
        const bool look = in.mouse_down(1) && !panel;
        app_.window().set_mouse_captured(look && app_.window().focused());
        if (look) {
            const float sens = 0.022f * 3.0f * app_.settings().sensitivity;
            free_yaw_ = eng::wrap_degrees(free_yaw_ + in.mouse_dx() * sens);
            free_pitch_ = std::clamp(free_pitch_ - in.mouse_dy() * sens, -89.0f, 89.0f);
        }
        if (!typing) {
            const Vec3 fwd = eng::angles_to_forward(free_yaw_, free_pitch_), right = eng::yaw_to_right(free_yaw_);
            Vec3 move{};
            if (in.key_down('W')) move = move + fwd;
            if (in.key_down('S')) move = move - fwd;
            if (in.key_down('D')) move = move + right;
            if (in.key_down('A')) move = move - right;
            if (in.key_down('E') || in.key_down(VK_SPACE)) move = move + Vec3{0, 1, 0};
            if (in.key_down('Q') || in.key_down(VK_CONTROL)) move = move - Vec3{0, 1, 0};
            if (eng::length_sq(move) > 0) free_eye_ = free_eye_ + eng::normalize(move) * ((in.key_down(VK_SHIFT) ? 1800.0f : 600.0f) * dt);
        }
        camera_.eye = free_eye_;
        camera_.yaw = free_yaw_;
        camera_.pitch = free_pitch_;
        camera_.update(app_.picture_aspect());
        app_.sounds().set_listener(camera_.eye, camera_.yaw);
    } else {
        app_.window().set_mouse_captured(false);
        camera_on(follow_, watch_view_);
        free_eye_ = camera_.eye, free_yaw_ = camera_.yaw, free_pitch_ = camera_.pitch;
        free_placed_ = true;
    }
}

bool GameWorld::camera_on(u32 id, WatchView view) {
    auto it = players_.find(id);
    if (it == players_.end()) return false;
    const PlayerView& p = it->second;
    const Vec3 eye = p.position + Vec3{0, eye_height_by_flags(move_def_, p.flags), 0};
    camera_.yaw = p.yaw;
    camera_.pitch = p.pitch;
    if (view == WatchView::Eyes && p.alive) {
        camera_.eye = eye;
    } else {
        // Behind and a little above, pulled in short of a wall.
        camera_.pitch = std::clamp(p.pitch * 0.5f, -35.0f, 35.0f) - 10.0f;
        Vec3 want = eye + camera_.forward() * -220.0f + Vec3{0, 40, 0};
        if (collision_)
            if (const eng::TraceResult tr = collision_->trace_ray(eye, want); tr.hit()) want = eye + (want - eye) * std::max(0.1f, tr.fraction - 0.05f);
        camera_.eye = want;
    }
    camera_.update(app_.picture_aspect());
    app_.sounds().set_listener(camera_.eye, camera_.yaw);
    return true;
}

u32 GameWorld::hidden_body() const {
    if (watching_) return watch_view_ == WatchView::Eyes ? follow_ : 0;
    return !alive_ && spectating_ && spec_view_ == WatchView::Eyes ? spectating_ : 0;
}

void GameWorld::draw_watch_hud(double now) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w();
    draw_clock();
    draw_kill_feed(now);
    // Whose view, and how they stand.
    if (const auto it = players_.find(follow_); it != players_.end()) {
        const PlayerView& p = it->second;
        const float x = 24, y = ui::stage_h() - 200, w = 360, h = 66;   // above the replay bar
        ui::panel(dl, x, y, w, h, nullptr);
        static const char* views[] = {"EYES", "CHASE", "FREE"};
        ui::text(dl, ui::font_bold(), 18, x + 14, y + 8, ui::col(p.team == Team::Blue ? pal.blue : p.team == Team::Red ? pal.red : pal.gold_bright),
                 p.name.empty() ? std::string("?") : p.name);
        ui::text(dl, ui::font_body(), 13, x + w - 14, y + 11, ui::col(pal.text_dim), views[int(watch_view_)], ui::Align::Right);
        const WeaponDef* wd = weapon(p.weapon);
        ui::text(dl, ui::font_body(), 14, x + 14, y + 38, ui::col(p.alive ? pal.text : pal.text_mute),
                 p.alive ? eng::str::format("%d HP   %s   %u kills, %u deaths", p.health, wd ? wd->name.c_str() : "", unsigned(p.kills), unsigned(p.deaths))
                         : std::string("Down"));
    }
    ui::text(dl, ui::font_body(), 13, W * 0.5f, 54, ui::col(pal.text_dim),
             "Left / Right or click: next soldier    V: eyes, chase, free    Tab: scores", ui::Align::Center);
    if (scoreboard_) draw_scoreboard();
}

}  // namespace lsf
