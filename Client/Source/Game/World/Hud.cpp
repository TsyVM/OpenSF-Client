// The match's HUD, drawn with VanGUI in the original client's own art (data/menu, inf/):
//
//   bottom left    the force's soldier (sf_c_<force>_up, _down crouched), health in the HUD's
//                  digits (inf/0..9.tga), the force's name plate (inf/<force>.tga)
//   bottom right   the magazine and the reserve in the same digits (inf/slash.tga, infinity.tga
//                  for a blade), the weapon's own HUD picture with its name (inf/weapon/<id>.tga)
//   top right      the kill feed: names in team colours, the weapon's icon (inf/weapon/w_<id>.bmp,
//                  keyed off its blue filler), the wall-shot and head-shot marks
//   top centre     the clock, the score between the team shields (inf/r.bmp, inf/b.bmp)
//   top centre     your kills, as the client's own table has them (inf/killeffect/killimage.txt,
//                  World/KillEffects.hpp): the words (HEAD SHOT, DOUBLE KILL, MULTI KILL, SPECIAL
//                  FORCE, REVENGE KILL, GRENADE KILL, KNIFE KILL, CAPTAIN KILL, RAGE KILL, SPECIAL
//                  POINT ...) over the mark, each where, as big and for as long as the table says;
//                  the undead's game types and the Pirate Ship have their own; a wall shot ours
//   right          the "ENEMY" diagram lighting the part you hit (hitzone/hitzone_<part><1..5>)
//   around the aim where the damage came from (inf/up.jpg, up_right.jpg, ...: arcs on black, drawn
//                  as light)
//   round's end    YOU WIN / YOU LOSE / DRAW (inf/tab/icon/win.tga, ...)
//
// The radar is World/Radar.cpp; the crosshair, the scoreboard, the chat and the radio list are here.
#include "Game/World/GameWorld.hpp"

#include "Game/App.hpp"
#include "Game/Items.hpp"
#include "Game/Screens/Screens.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Crosshair.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "SF/Image.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsf {

ui::Picture weapon_icon(App& app, const WeaponDef& w);

using eng::Vec3;
using namespace proto;

namespace {

// A name at stage coordinates in its team colour, or, with a Colored Codename, in that colour glowing.
void name_text(VanDrawList* dl, VanFont* font, float size, float x, float y, VanU32 team_ink, std::string_view s, u8 colour) {
    if (s.empty()) return;
    if (!colour) {
        ui::text(dl, font, size, x, y, team_ink, s);
        return;
    }
    ui::glow_text(dl, font, ui::px(size), ui::stage(x, y), s, colour, (team_ink & 0xFF000000u) | 0x00FFFFFFu);
}

ui::Picture menu_art(std::string_view key) {
    ui::Atlas* a = ui::atlas();
    return a ? a->picture(sf::Pack::Menu, key) : ui::Picture{};
}

ui::Picture menu_glow(std::string_view key) {
    ui::Atlas* a = ui::atlas();
    return a ? a->glow_picture(sf::Pack::Menu, key) : ui::Picture{};
}

// A picture at stage coordinates, centred on (cx, cy), `h` tall, its own aspect kept.
void art_centred(VanDrawList* dl, const ui::Picture& p, float cx, float cy, float h, VanU32 tint = 0xFFFFFFFF) {
    if (!p.valid()) return;
    const float w = h * p.w / std::max(1.0f, p.h);
    ui::picture(dl, p, cx - w * 0.5f, cy - h * 0.5f, w, h, tint, false);
}

VanU32 white(float a) { return VAN_COL32(255, 255, 255, int(std::clamp(a, 0.0f, 1.0f) * 255)); }

// The wall shot's mark (below, with the kill effects): the feed draws it small.
void draw_wall_mark(VanDrawList* dl, float cx, float cy, float size, float a);

// The HUD's digits (inf/0..9.tga and slash.tga, 32x32 with the figure in the middle half), `h`
// stage units tall. Returns the width drawn.
float digits(VanDrawList* dl, std::string_view s, float x, float y, float h, VanU32 tint, ui::Align align = ui::Align::Left) {
    const float adv = h * 0.56f;
    const float w = adv * float(s.size());
    if (align == ui::Align::Right) x -= w;
    else if (align == ui::Align::Center) x -= w * 0.5f;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        std::string key;
        if (c >= '0' && c <= '9') key = std::string("inf/") + c + ".tga";
        else if (c == '/') key = "inf/slash.tga";
        else continue;
        const ui::Picture p = menu_art(key);
        const float cx = x + adv * (float(i) + 0.5f);
        if (p.valid()) {
            ui::picture(dl, p, cx - h * 0.5f + 1.5f, y + 1.5f, h, h, VAN_COL32(0, 0, 0, (tint >> VAN_COL32_A_SHIFT) * 3 / 4), false);
            ui::picture(dl, p, cx - h * 0.5f, y, h, h, tint, false);
        } else {
            ui::text(dl, ui::font_heading(), h, cx, y, tint, std::string(1, c), ui::Align::Center);
        }
    }
    return w;
}

// A force's HUD pieces are named for its model, with three exceptions the archive spells its own way.
std::string silhouette_key(std::string_view model, bool crouched) {
    std::string m(model);
    if (m == "delta") m = "deltaforce";
    else if (m == "rokmc") m = "kormarine";
    else if (m == "psu") m = "gsg9";   // the PSU has none of its own
    return "inf/sf_c_" + m + (crouched ? "_down.tga" : "_up.tga");
}

}  // namespace

ui::Picture GameWorld::weapon_art(const WeaponDef& w, char kind) {
    const std::string id = sf::lower(art_model(w));
    std::vector<std::string> keys;
    switch (kind) {
        case 'w': keys = {"inf/weapon/w_" + id + ".bmp", "inf/weapon/w_" + id + ".tga", "inf/weapon/" + id + ".tga"}; break;
        case 'h': keys = {"inf/weapon/h_" + id + ".bmp", "inf/weapon/h_" + id + ".tga"}; break;
        default: keys = {"inf/weapon/" + id + ".tga", "inf/weapon/h_" + id + ".bmp"}; break;
    }
    for (const std::string& k : keys)
        if (app_.data().resolve(sf::Pack::Menu, k)) return menu_art(k);
    // Not every gun has every picture: the feed falls back to the banner and the box to the shop's.
    if (kind == 'w') return weapon_art(w, 'h');
    return weapon_icon(app_, w);
}

// ── The crosshair ──────────────────────────────────────────────────────────────

void GameWorld::draw_crosshair(double now) {
    if (!alive_ || scoped_) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const float W = ui::stage_w(), H = ui::stage_h();
    // The ticks stand where the cone's edge is on screen: what a shot can do now, in the air
    // (twice the cone) as on the ground; a burst's share of it toned down (kCrosshairShots).
    const float cone = current_cone(kCrosshairShots);
    const float fov_y = 2.0f * std::atan(std::tan(camera_.fov_x * 0.5f * eng::kDegToRad) / (16.0f / 9.0f));
    const float edge = std::tan(std::min(cone, 30.0f) * eng::kDegToRad) / std::tan(fov_y * 0.5f) * (H * 0.5f);
    const float open = ui::follow("xh", edge, 0.06f);
    const VanVec2 c = ui::stage(W * 0.5f, H * 0.5f);
    // A picture pulled over a screen of another shape is wider than it was drawn: so is the cone
    // on screen, and the side ticks stand out by as much.
    const ui::ViewRect vr = ui::view();
    const float wide = std::clamp((vr.w / std::max(1.0f, vr.h)) / std::max(0.1f, app_.picture_aspect()), 0.5f, 2.0f);
    // The player's own (the options' Crosshair page), opened by the cone.
    ui::draw_crosshair(dl, c, ui::px(1), app_.settings().crosshair, open, wide);
    // A hit: four strokes that open as they fade (red on the one that killed).
    const float hm = float(now - hit_marker_);
    if (hm < 0.25f) {
        const float a = 1.0f - hm / 0.25f, o = ui::px(8 + hm * 30);
        const bool killed = now - kill_fx_.at < 0.25;
        const VanU32 ink = killed ? VAN_COL32(255, 70, 50, int(a * 255)) : VAN_COL32(255, 255, 255, int(a * 255));
        for (int sx : {-1, 1})
            for (int sy : {-1, 1})
                dl->AddLine({c.x + sx * o, c.y + sy * o}, {c.x + sx * (o + ui::px(7)), c.y + sy * (o + ui::px(7))}, ink, 2.0f);
    }
}

// ── Top centre: the clock and the score ────────────────────────────────────────

void GameWorld::draw_clock() {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w();
    const int secs = std::max(0, int(std::ceil(round_time_)));
    char clock[16];
    std::snprintf(clock, sizeof clock, "%d:%02d", secs / 60, secs % 60);
    const float cx = W * 0.5f;
    dl->AddRectFilledMultiColor(ui::stage(cx - 150, 6), ui::stage(cx + 150, 62), VAN_COL32(0, 0, 0, 150), VAN_COL32(0, 0, 0, 150),
                                VAN_COL32(0, 0, 0, 40), VAN_COL32(0, 0, 0, 40));
    ui::text(dl, ui::font_heading(), 30, cx, 8, ui::col(secs <= 30 ? pal.warn : pal.text), clock, ui::Align::Center);
    if (mode_info(settings_.mode).teams) {
        // The two shields, the score beside each.
        art_centred(dl, menu_art("inf/r.bmp"), cx - 118, 34, 44);
        art_centred(dl, menu_art("inf/b.bmp"), cx + 118, 34, 44);
        digits(dl, std::to_string(red_score_), cx - 88, 34, 26, ui::col(pal.red), ui::Align::Left);
        digits(dl, std::to_string(blue_score_), cx + 88, 34, 26, ui::col(pal.blue), ui::Align::Right);
        if (mode_info(settings_.mode).rounds)
            ui::text(dl, ui::font_bold(), 13, cx, 44, ui::col(pal.text_dim), eng::str::format("ROUND %u", unsigned(round_)), ui::Align::Center);
    } else {
        ui::text(dl, ui::font_bold(), 14, cx, 42, ui::col(pal.text_dim), mode_name(settings_.mode), ui::Align::Center);
    }
}

// ── Top right: the kill feed ───────────────────────────────────────────────────

void GameWorld::draw_kill_feed(double now) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w();
    float fy = 12;
    const ui::Picture hs = menu_art("inf/killeffect/icon_headshot1.tga");
    for (auto it = feed_.rbegin(); it != feed_.rend(); ++it) {
        const float age = float(now - it->time);
        if (age > 7.0f) continue;
        const float a = std::clamp(7.0f - age, 0.0f, 1.0f);
        // In from the right over a quarter second (timed off the line itself: ui::slide's intros
        // belong to the menus' visits and never settle during a match).
        const float in = std::clamp(age / 0.25f, 0.0f, 1.0f);
        const float slide = 60.0f * (1.0f - in) * (1.0f - in);
        const WeaponDef* w = weapon(it->weapon_id);
        const ui::Picture icon = w ? weapon_art(*w, 'w') : ui::Picture{};
        const float icon_h = 26, icon_w = icon.valid() ? icon_h * icon.w / std::max(1.0f, icon.h) : 0;
        const bool headshot = it->flags & kKillHeadshot, wall = it->flags & kKillWall;
        // A teammate's grenade: said in red after its picture.
        const char* tk = (it->flags & kKillTeam) ? "TEAM KILL" : nullptr;
        const float tkw = tk ? ui::text_size(ui::font_bold(), 12, tk).x / ui::scale() + 10 : 0;
        const float kw = it->killer.empty() ? 0 : ui::text_size(ui::font_bold(), 15, it->killer).x / ui::scale();
        const float vw = ui::text_size(ui::font_bold(), 15, it->victim).x / ui::scale();
        const float mid = (icon_w > 0 ? icon_w : ui::text_size(ui::font_body(), 13, it->weapon).x / ui::scale()) + (headshot ? 26 : 0) + (wall ? 26 : 0) + tkw + 20;
        const float total = kw + mid + vw + 16;
        float x = W - 14 - total + slide;
        dl->AddRectFilled(ui::stage(x - 6, fy - 1), ui::stage(W - 8 + slide, fy + 29), VAN_COL32(0, 0, 0, int(120 * a)));
        const bool mine = it->killer_id == me_ || it->victim_id == me_;
        if (mine) dl->AddRect(ui::stage(x - 6, fy - 1), ui::stage(W - 8 + slide, fy + 29), ui::col(pal.gold, 0.7f * a));
        name_text(dl, ui::font_bold(), 15, x, fy + 5, ui::col(ui::team_colour(it->killer_team), a), it->killer, it->killer_colour);
        x += kw + 10;
        if (icon.valid()) {
            ui::picture(dl, icon, x, fy + 1, icon_w, icon_h, white(a), false);
            x += icon_w;
        } else {
            ui::text(dl, ui::font_body(), 13, x, fy + 6, ui::col(pal.text, a), it->weapon);
            x += ui::text_size(ui::font_body(), 13, it->weapon).x / ui::scale();
        }
        // Through a wall, then to the head: the marks in the order it happened.
        if (wall) {
            draw_wall_mark(dl, x + 14, fy + 14, 30, a);
            x += 26;
        }
        if (headshot && hs.valid()) {
            ui::picture(dl, hs, x + 2, fy + 2, 24, 24, white(a), false);
            x += 26;
        }
        if (tk) {
            ui::text(dl, ui::font_bold(), 12, x + 8, fy + 8, VAN_COL32(255, 90, 70, int(255 * a)), tk);
            x += tkw;
        }
        x += 10;
        name_text(dl, ui::font_bold(), 15, x, fy + 5, ui::col(ui::team_colour(it->victim_team), a), it->victim, it->victim_colour);
        fy += 32;
    }
}

// ── Centre: your kills ─────────────────────────────────────────────────────────

namespace {

// The gold of the original's kill marks.
VanU32 mark_gold(float a) { return VAN_COL32(247, 203, 30, int(std::clamp(a, 0.0f, 1.0f) * 255)); }

// The wall shot's mark. The original's art has none (its table stops at the bomb's), so this one is
// drawn here in the same gold: a card of bricks with a bullet's hole burst through it. `size` is
// the card's height in stage units, centred on (cx, cy).
void draw_wall_mark(VanDrawList* dl, float cx, float cy, float size, float a) {
    const VanU32 gold = mark_gold(a), dark = VAN_COL32(20, 16, 4, int(a * 215));
    const float h = size * 0.74f, w = h * 0.86f;
    const float x0 = cx - w * 0.5f, y0 = cy - h * 0.5f, x1 = cx + w * 0.5f, y1 = cy + h * 0.5f;
    const float line = std::max(1.5f, ui::px(size * 0.035f));
    dl->AddRectFilled(ui::stage(x0, y0), ui::stage(x1, y1), dark, ui::px(size * 0.05f));
    dl->AddRect(ui::stage(x0, y0), ui::stage(x1, y1), gold, ui::px(size * 0.05f), 0, line * 1.4f);
    // Four courses of bricks, every other one set half a brick over.
    for (int row = 1; row < 4; ++row) {
        const float y = y0 + h * float(row) / 4.0f;
        dl->AddLine(ui::stage(x0, y), ui::stage(x1, y), gold, line);
    }
    for (int row = 0; row < 4; ++row) {
        const float ya = y0 + h * float(row) / 4.0f, yb = y0 + h * float(row + 1) / 4.0f;
        for (int k = 1; k <= 2; ++k) {
            const float x = x0 + w * (float(k) - (row & 1 ? 0.5f : 0.0f)) / 2.5f + (row & 1 ? 0 : w * 0.1f);
            if (x > x0 + 2 && x < x1 - 2) dl->AddLine(ui::stage(x, ya), ui::stage(x, yb), gold, line);
        }
    }
    // The hole, and the burst round it.
    const VanVec2 c = ui::stage(cx, cy);
    const float r = ui::px(size * 0.15f);
    for (int k = 0; k < 10; ++k) {
        const float th = float(k) * 0.6283185f + 0.2f;
        const float tip = r * (k & 1 ? 2.9f : 2.1f);
        dl->AddLine({c.x + std::cos(th) * r * 1.1f, c.y + std::sin(th) * r * 1.1f}, {c.x + std::cos(th) * tip, c.y + std::sin(th) * tip}, gold, line * 1.2f);
    }
    dl->AddCircleFilled(c, r * 1.25f, gold, 20);
    dl->AddCircleFilled(c, r * 0.8f, VAN_COL32(10, 8, 2, int(a * 255)), 20);
}

// Words in the kill marks' lettering (the art's is a heavy white italic with a dark edge): for the
// marks the original has no picture of. Centred on (cx, cy), `size` stage units tall.
void mark_text(VanDrawList* dl, std::string_view s, float cx, float cy, float size, float a) {
    VanFont* font = ui::font_display();
    const float px = ui::px(size);
    const VanVec2 extent = ui::text_size(font, size, s);
    const VanVec2 at = ui::stage(cx, cy);
    const VanVec2 pos{at.x - extent.x * 0.5f, at.y - px * 0.5f};
    const int first = dl->VtxBuffer.Size;
    const VanU32 edge = VAN_COL32(0, 0, 0, int(a * 200));
    const float o = std::max(1.0f, px * 0.05f);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) dl->AddText(font, px, {pos.x + float(dx) * o, pos.y + float(dy) * o}, edge, s.data(), s.data() + s.size());
    dl->AddText(font, px, pos, white(a), s.data(), s.data() + s.size());
    // Leant over, as the art's letters are: every corner moved along by how far it stands above the foot.
    const float foot = pos.y + px;
    for (int i = first; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.x += (foot - dl->VtxBuffer[i].pos.y) * 0.22f - px * 0.11f;
}

}  // namespace

// A kill of yours, as the original's table shows it (World/KillEffects.hpp): the icon and the words
// in their places, at their sizes, for their time. A wall shot adds its own line under whatever the
// kill was, and has a mark of its own when it was nothing else.
void GameWorld::draw_kill_effect(double now) {
    const float t = float(now - kill_fx_.at);
    if (t < 0 || t > 4.0f || kill_fx_.effect.empty()) return;
    if (!kill_effects_.loaded()) kill_effects_.load(app_.data());
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const float W = ui::stage_w(), H = ui::stage_h();
    const float k = H / 768.0f;   // the table's sizes are pixels of the original's 768 lines
    float alpha = 0;              // how much of the effect is still up (the wall shot's line goes with it)
    auto draw = [&](int index) {
        const KillImage* im = kill_effects_.image(index);
        if (!im || im->textures.empty() || t > im->show + im->fade_out) return;
        const float a = t <= im->show ? 1.0f : 1.0f - (t - im->show) / std::max(0.01f, im->fade_out);
        alpha = std::max(alpha, a);
        // FADE_STATE 7: its pictures a tenth of a second each, then the first one held.
        size_t frame = 0;
        if (im->state == 7 && im->textures.size() > 1) {
            const size_t f = size_t(t / 0.1f);
            frame = f < im->textures.size() ? f : 0;
        }
        const ui::Picture p = menu_art(im->textures[frame]);
        if (!p.valid()) return;
        const float w = im->w * k, h = im->h * k;
        ui::picture(dl, p, W * im->x - w * 0.5f, H * im->y - h * 0.5f, w, h, white(a), false);
    };
    if (const KillEffect* fx = kill_effects_.find(kill_fx_.effect)) {
        draw(fx->image);
        draw(fx->text);
        if (kill_fx_.wall && alpha > 0) mark_text(dl, "WALL SHOT", W * 0.5f, H * 0.372f, 24, alpha);
    } else if (kill_fx_.effect == "WALLSHOT") {
        // Ours: in the table's own places (the words at 0.18, the mark at 0.28) and for its time.
        constexpr float kShow = 1.8f, kFade = 0.1f;
        if (t > kShow + kFade) return;
        const float a = t <= kShow ? 1.0f : 1.0f - (t - kShow) / kFade;
        mark_text(dl, "WALL SHOT", W * 0.5f, H * 0.18f, 40 * k, a);
        draw_wall_mark(dl, W * 0.5f, H * 0.28f, 100 * k, a);
    }
}

// Which of the table's effects a kill of yours shows. A streak outranks how the kill was made;
// then a captain, revenge, rage, and the way it was done. The undead's game types and the Pirate
// Ship have sets of their own.
void GameWorld::show_kill(u16 f, u32 victim) {
    const Mode mode = settings_.mode;
    const char* name = nullptr;
    if (mode == Mode::Horror || mode == Mode::Horror2) {
        (void)victim;
        if (me_undead()) name = f & kKillHeadshot ? "Z_HEADSHOT" : "H_DIE";
        else name = f & kKillSpecialForce ? "Z_SPECIALFORCE" : f & kKillMulti ? "Z_MULTI" : f & kKillDouble ? "Z_DOUBLE" : f & kKillHeadshot ? "H_HEADSHOT"
                    : f & kKillKnife ? "Z_KNIFE" : f & kKillGrenade ? "Z_GRANADE" : "Z_DIE";
    } else if (mode == Mode::Pirate) {
        name = f & kKillSpecialForce ? "P_SPECIALFORCE" : f & kKillMulti ? "P_MULTIKILL" : f & kKillDouble ? "P_DOUBLEKILL" : f & kKillHeadshot ? "P_HEADSHOT"
               : f & kKillKnife ? "P_KNIFE" : "P_NORMAL";
    } else {
        name = f & kKillSpecialForce ? "SPECIALFORCE" : f & kKillMulti ? "MULTIKILL" : f & kKillDouble ? "DOUBLEKILL" : f & kKillCaptain ? "CAPTAINKILL"
               : f & kKillRevenge ? "REVENGEKILL_CENTERIMAGE" : f & kKillRage ? "RAGEKILL_CENTERIMAGE" : f & kKillHeadshot ? "HEADSHOT"
               : f & kKillGrenade ? "GRENADEKILL" : f & kKillKnife ? "KNIFEKILL" : f & kKillBomb ? "C4BOMB" : f & kKillWall ? "WALLSHOT" : nullptr;
    }
    audio_->kill_call(f);
    show_effect(name, (f & kKillWall) != 0);
}

void GameWorld::show_effect(const char* name, bool wall) {
    kill_fx_.at = app_.now();
    kill_fx_.effect = name ? name : "";
    kill_fx_.wall = wall;
}

// ── Right: where you hit them ──────────────────────────────────────────────────

void GameWorld::draw_hit_zone(double now) {
    const float t = float(now - zone_hit_at_);
    const float show = zone_hit_kill_ ? 2.2f : 1.4f;
    if (t < 0 || t > show) return;
    const char* part = nullptr;
    switch (HitZone(zone_hit_)) {
        case HitZone::Head: part = "head"; break;
        case HitZone::Chest:
        case HitZone::Stomach: part = "body"; break;
        case HitZone::Arms: part = "arm"; break;
        case HitZone::Legs: part = "leg"; break;
        default: part = nullptr; break;
    }
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const float a = std::clamp((show - t) / 0.4f, 0.0f, 1.0f) * std::min(1.0f, t / 0.08f + 0.3f);
    const std::string key = part ? eng::str::format("inf/hitzone/hitzone_%s%d.tga", part, 1 + int(t * 16.0f) % 5) : "inf/hitzone/hitzone_basic.tga";
    const float W = ui::stage_w(), H = ui::stage_h();
    ui::picture(dl, menu_art(key), W - 124, H * 0.34f, 96, 146, white(a * 0.95f), false);
}

// ── Around the aim: where the damage came from ─────────────────────────────────

void GameWorld::draw_damage_arcs(double now) {
    const float t = float(now - damaged_at_);
    if (t < 0 || t > 1.2f || !alive_) return;
    const Vec3 to = damage_from_ - move_.origin;
    if (to.x * to.x + to.z * to.z < 1.0f) return;
    // 0 in front, growing clockwise.
    float ang = std::atan2(to.x, to.z) * eng::kRadToDeg - yaw_;
    ang = std::fmod(std::fmod(ang, 360.0f) + 360.0f, 360.0f);
    static const char* kArcs[] = {"up", "up_right", "right", "down_right", "down", "down_left", "left", "up_left"};
    const int i = int(std::lround(ang / 45.0f)) % 8;
    const float a = std::clamp((1.2f - t) / 0.6f, 0.0f, 1.0f);
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const float W = ui::stage_w(), H = ui::stage_h();
    art_centred(dl, menu_glow(std::string("inf/") + kArcs[i] + ".jpg"), W * 0.5f, H * 0.5f, 420, white(a));
}

// ── Bottom left: you ───────────────────────────────────────────────────────────

void GameWorld::draw_health() {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float H = ui::stage_h();
    const ForceDef* f = force(players_.contains(me_) ? players_.at(me_).force : 0);
    dl->AddRectFilledMultiColor(ui::stage(0, H - 120), ui::stage(300, H), VAN_COL32(0, 0, 0, 0), VAN_COL32(0, 0, 0, 0), VAN_COL32(0, 0, 0, 0),
                                VAN_COL32(0, 0, 0, 150));
    if (f) ui::picture(dl, menu_art(silhouette_key(art_model(*f), move_.ducked)), 12, H - 112, 50, 100, 0xFFFFFFFF, false);
    const int hp = std::max(0, health_);
    const float frac = std::clamp(float(hp) / float(kMaxHealth), 0.0f, 1.0f);
    const VanU32 ink = frac < 0.3f ? ui::col(pal.bad, ui::pulse(3.0f, 0.55f)) : VAN_COL32(245, 245, 240, 255);
    digits(dl, std::to_string(hp), 66, H - 90, 52, ink);
    if (f) {
        // A pack character's stand-in art spells a base force's name: its own is written instead.
        const ui::Picture name = f->id >= kFirstPackForce ? ui::Picture{} : menu_art(std::string("inf/") + art_model(*f) + ".tga");
        if (name.valid()) ui::picture(dl, name, 36, H - 40, 128, 32, VAN_COL32(230, 230, 224, 255), false);
        else ui::text(dl, ui::font_heading(), 16, 72, H - 34, ui::col(pal.text), f->name);
    }
    // The ghost of what was lost, then what is left.
    const float ghost = ui::follow("hpghost", frac, 0.6f);
    dl->AddRectFilled(ui::stage(72, H - 12), ui::stage(232, H - 8), VAN_COL32(10, 10, 8, 200));
    dl->AddRectFilled(ui::stage(72, H - 12), ui::stage(72 + 160 * ghost, H - 8), VAN_COL32(200, 60, 40, 200));
    dl->AddRectFilled(ui::stage(72, H - 12), ui::stage(72 + 160 * frac, H - 8), ui::col(frac < 0.3f ? pal.bad : pal.good));
}

// ── Bottom right: the weapon ───────────────────────────────────────────────────

void GameWorld::draw_weapon_box() {
    const WeaponDef* w = my_weapon(slot_);
    if (!w) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    dl->AddRectFilledMultiColor(ui::stage(W - 320, H - 170), ui::stage(W, H), VAN_COL32(0, 0, 0, 0), VAN_COL32(0, 0, 0, 0), VAN_COL32(0, 0, 0, 160),
                                VAN_COL32(0, 0, 0, 0));
    // The magazine and the reserve, or the blade's infinity.
    if (w->klass == WeaponClass::Knife) {
        art_centred(dl, menu_art("inf/infinity.tga"), W - 88, H - 150, 34);
    } else {
        const int c = clip_[size_t(slot_)], r = reserve_[size_t(slot_)];
        const bool low = w->magazine > 0 && c <= std::max(1, w->magazine / 5);
        const std::string text = w->klass == WeaponClass::Grenade ? std::to_string(c) : std::to_string(c) + "/" + std::to_string(r);
        digits(dl, text, W - 22, H - 170, 42, low ? ui::col(pal.warn, ui::pulse(2.5f, 0.6f)) : VAN_COL32(245, 245, 240, 255), ui::Align::Right);
    }
    // The weapon's picture, its name drawn into it; ours for the guns without one. A server's own
    // gun stands in another's art, whose name would be wrong: its own name is written instead.
    const ui::Picture art = w->id >= kFirstPackWeapon ? ui::Picture{} : weapon_art(*w, 'b');
    if (art.valid()) {
        const float h = 110, aw = std::min(260.0f, h * art.w / std::max(1.0f, art.h));
        const float ah = aw * art.h / std::max(1.0f, art.w);
        ui::picture(dl, art, W - 16 - aw, H - 8 - ah, aw, ah, 0xFFFFFFFF, false);
    }
    if (!art.valid())
        ui::text(dl, ui::font_heading(), 16, W - 24, H - 28, ui::col(pal.gold_bright), w->name, ui::Align::Right);
    if (reload_left_ > 0) {
        const float f = 1.0f - reload_left_ / std::max(0.1f, w->reload_time);
        ui::progress(VanGui::GetForegroundDrawList(), f, W * 0.5f - 90, H * 0.5f + 60, 180, 8, "RELOADING");
    }
    // The kit, its icons stacked above: the key each is on (the throwables all on 4).
    float y = H - 206;
    for (int k = int(kLoadoutSlots) - 1; k >= 0; --k) {
        const WeaponDef* s = my_weapon(k);
        if (!s) continue;
        const bool on = k == slot_;
        const ui::Picture icon = weapon_art(*s, 'w');
        const float ih = on ? 30 : 24, iw = icon.valid() ? ih * icon.w / std::max(1.0f, icon.h) : 0;
        if (icon.valid()) ui::picture(dl, icon, W - 40 - iw, y - ih, iw, ih, on ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 110), false);
        ui::text(dl, ui::font_bold(), 13, W - 22, y - ih + 4, ui::col(on ? pal.lime : pal.text_mute), std::to_string(int(cell_slot(size_t(k))) + 1), ui::Align::Right);
        y -= ih + 4;
    }
}

// ── Top: the switch bar ────────────────────────────────────────────────────────

void GameWorld::draw_weapon_bar(double now) {
    // Two seconds after a weapon is taken out, in at once and out over the last third of one.
    constexpr double kShown = 2.0, kFade = 0.35;
    const double t = now - weapon_bar_at_;
    if (t < 0 || t > kShown || menu_open_ || scoreboard_ || match_over_) return;
    const float a = float(std::clamp(std::min(t / 0.08, (kShown - t) / kFade), 0.0, 1.0));
    // Over the rest of the HUD (the radar, the score, the mission's line) while it shows: solid, as the
    // original's was.
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w();
    auto alpha = [&](int v) { return int(float(v) * a); };
    // One panel: the cell's weapon, `badge` its key's number (0: none), `lit` the one in hand.
    auto panel = [&](float x, float y, float pw, float ph, int cell, int badge, bool lit) {
        const int fill = 255;
        dl->AddRectFilledMultiColor(ui::stage(x, y), ui::stage(x + pw, y + ph), VAN_COL32(46, 48, 44, alpha(fill)), VAN_COL32(30, 32, 28, alpha(fill)),
                                    VAN_COL32(12, 13, 11, alpha(fill)), VAN_COL32(22, 23, 20, alpha(fill)));
        dl->AddRect(ui::stage(x, y), ui::stage(x + pw, y + ph), lit ? ui::col(pal.lime, a) : VAN_COL32(96, 98, 90, alpha(255)), 0, 0, ui::px(lit ? 2.0f : 1.0f));
        float left = x + 8;
        if (badge > 0) {
            const ui::Picture b = menu_art(eng::str::format("inf/weapon/%d.bmp", badge));
            if (b.valid()) ui::picture(dl, b, x + 5, y + 5, 26, 26, white(a), false);
            else ui::text(dl, ui::font_heading(), 18, x + 18, y + 7, white(a), std::to_string(badge), ui::Align::Center);
            left = x + 34;
        }
        const WeaponDef* w = my_weapon(cell);
        if (!w) {
            ui::text(dl, ui::font_body(), 13, x + pw * 0.5f, y + ph * 0.5f - 8, VAN_COL32(150, 150, 140, alpha(220)), "Empty", ui::Align::Center);
            return;
        }
        // The gun's own HUD picture, its name drawn into it (inf/weapon/<id>.tga, the weapon box's);
        // a gun with none, ours and its name written.
        const std::string id = sf::lower(art_model(*w));
        const bool named = w->id < kFirstPackWeapon && (app_.data().resolve(sf::Pack::Menu, "inf/weapon/" + id + ".tga") ||
                                                        app_.data().resolve(sf::Pack::Menu, "inf/weapon/h_" + id + ".bmp"));
        const ui::Picture pic = weapon_art(*w, 'b');
        if (pic.valid()) {
            const float bw = x + pw - 6 - left, bh = ph - 8;
            const float s = std::min(bw / std::max(1.0f, pic.w), bh / std::max(1.0f, pic.h));
            const float iw = pic.w * s, ih = pic.h * s;
            ui::picture(dl, pic, left + (bw - iw) * 0.5f, y + 4 + (bh - ih) * 0.5f, iw, ih, lit ? white(a) : VAN_COL32(205, 205, 198, alpha(255)), false);
        }
        if (!named || !pic.valid())
            ui::text(dl, ui::font_heading(), 14, x + pw - 8, y + ph - 22, lit ? white(a) : VAN_COL32(185, 185, 175, alpha(240)), eng::str::upper(w->name), ui::Align::Right);
        // A throwable: how many of it are left.
        if (w->klass == WeaponClass::Grenade) {
            const int n = clip_[size_t(cell)] + reserve_[size_t(cell)];
            ui::text(dl, ui::font_bold(), 13, x + pw - 7, y + 6, ui::col(n > 0 ? pal.text : pal.bad, a), "x" + std::to_string(n), ui::Align::Right);
        }
    };
    // A panel a key, centred along the top: 1 the primary, 2 the sidearm, 3 the blade, 4 the first
    // throwable carried; the other throwables in panels of their own under it, in the kit's order.
    constexpr int kKeys = 4;
    constexpr float kY0 = 8, kGap = 6, kH = 96, kThrowH = 74;
    const float pw = std::min(240.0f, (W - 24 - kGap * float(kKeys - 1)) / float(kKeys));
    const float x0 = (W - (pw * float(kKeys) + kGap * float(kKeys - 1))) * 0.5f;
    for (int k = 0; k < int(Slot::Throw); ++k) panel(x0 + float(k) * (pw + kGap), kY0, pw, kH, k, k + 1, slot_ == k);
    std::vector<int> thrown;
    for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c)
        if (loadout_[c] != kNoWeapon) thrown.push_back(int(c));
    const float tx = x0 + float(Slot::Throw) * (pw + kGap);
    if (thrown.empty()) panel(tx, kY0, pw, kH, int(kFirstThrowCell), int(Slot::Throw) + 1, false);
    float ty = kY0;
    for (size_t i = 0; i < thrown.size(); ++i) {
        const float ph = i == 0 ? kH : kThrowH;
        panel(tx, ty, pw, ph, thrown[i], i == 0 ? int(Slot::Throw) + 1 : 0, slot_ == thrown[i]);
        ty += ph + 4;
    }
}

// ── Banners ────────────────────────────────────────────────────────────────────

void GameWorld::draw_banner(double now) {
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const float rt = float(now - result_at_);
    if (result_art_ >= 0 && rt < 3.5f) {
        static const char* kArt[] = {"inf/tab/icon/win.tga", "inf/tab/icon/lose.tga", "inf/tab/icon/draw.tga"};
        const float a = std::min(1.0f, std::min(rt * 4.0f, (3.5f - rt) * 2.0f));
        const float s = 1.0f + 0.2f * (1.0f - std::min(1.0f, rt * 3));
        art_centred(dl, menu_art(kArt[result_art_]), W * 0.5f, H * 0.3f, 150 * s, white(a));
        return;
    }
    // Your Rebirth went (Horror Mode's item): the original's wings, for a moment.
    if (now - reborn_at_ < 2.5) {
        const float t = float(now - reborn_at_);
        art_centred(dl, menu_art("ui/texture/common/ui_image_zombieitem_rebirth.tga"), W * 0.5f, H * 0.42f, 380, white(std::min(1.0f, std::min(t * 4.0f, (2.5f - t) * 1.5f))));
    }
    // The round's challenges done: "SPECIAL POINT +30" and the original's words for it, a line each.
    while (!specials_.empty() && now - specials_.front().time > 5.0) specials_.pop_front();
    {
        float y = H * 0.36f;
        for (const SpecialLine& line : specials_) {
            const float t = float(now - line.time);
            const float a = std::min(1.0f, std::min(t * 5.0f, (5.0f - t) * 1.5f));
            ui::text(dl, ui::font_heading(), 22, W * 0.5f, y, ui::col(pal.lime, a), line.head, ui::Align::Center);
            ui::text(dl, ui::font_body(), 14, W * 0.5f, y + 26, ui::col(pal.text, a), line.text, ui::Align::Center);
            y += 50;
        }
    }
    if (now - banner_at_ < 3.0 && !banner_.empty()) {
        const float t = float(now - banner_at_);
        const float a = std::min(1.0f, std::min(t * 4.0f, (3.0f - t) * 2.0f));
        const float s = 1.0f + 0.15f * (1.0f - std::min(1.0f, t * 3));
        ui::text(dl, ui::font_display(), 44 * s, W * 0.5f, H * 0.28f, ui::col(pal.gold_bright, a), banner_, ui::Align::Center);
    }
}

// ── Tests ──────────────────────────────────────────────────────────────────────

// Tests: each kill mark in turn (0, 1, 2 ...), with a line in the feed to go with it: the classic
// set, the wall shot's, the undead's and the Pirate Ship's. Its name, or null past the last.
const char* GameWorld::test_kill_mark(int index) {
    struct Mark {
        const char* effect;
        u16 flags;
        const char* model;   // the weapon in the feed's line
    };
    static const Mark kMarks[] = {
        {"HEADSHOT", kKillHeadshot, "m4a1"},
        {"GRENADEKILL", kKillGrenade, "m67"},
        {"KNIFEKILL", kKillKnife, "m9"},
        {"WALLSHOT", kKillWall, "m4a1"},
        {"HEADSHOT", u16(kKillHeadshot | kKillWall), "psg1"},
        {"DOUBLEKILL", kKillDouble, "m4a1"},
        {"MULTIKILL", kKillMulti, "m4a1"},
        {"SPECIALFORCE", kKillSpecialForce, "m4a1"},
        {"REVENGEKILL_CENTERIMAGE", kKillRevenge, "m4a1"},
        {"RAGEKILL_CENTERIMAGE", kKillRage, "m4a1"},
        {"CAPTAINKILL", kKillCaptain, "m4a1"},
        {"SPECIALPOINT", 0, nullptr},
        {"C4BOMB", kKillBomb, nullptr},
        {"Z_DIE", 0, "m4a1"},
        {"H_DIE", 0, nullptr},
        {"H_HEADSHOT", kKillHeadshot, "m4a1"},
        {"Z_HEADSHOT", kKillHeadshot, nullptr},
        {"Z_KNIFE", kKillKnife, "m9"},
        {"Z_GRANADE", kKillGrenade, "m67"},
        {"Z_DOUBLE", kKillDouble, "m4a1"},
        {"Z_MULTI", kKillMulti, "m4a1"},
        {"Z_SPECIALPOINT", 0, nullptr},
        {"P_NORMAL", 0, "m4a1"},
        {"P_HEADSHOT", kKillHeadshot, "m4a1"},
        {"P_KNIFE", kKillKnife, "m9"},
        {"P_CANNON", 0, nullptr},
        {"P_DOUBLEKILL", kKillDouble, "m4a1"},
        {"P_MULTIKILL", kKillMulti, "m4a1"},
        {"P_SPECIALFORCE", kKillSpecialForce, "m4a1"},
        {"P_SPECIALPOINT", 0, nullptr},
        {"P_OCCUPY", 0, nullptr},
    };
    if (index < 0 || index >= int(std::size(kMarks))) return nullptr;
    const Mark& m = kMarks[index];
    const double now = app_.now();
    feed_.clear();
    if (m.model) {
        KillFeedLine l;
        l.killer = players_.contains(me_) ? players_.at(me_).name : std::string("You");
        l.victim = "Curtis";
        l.killer_id = me_, l.victim_id = 0xFFFFFFF1u;
        l.killer_team = u8(team_), l.victim_team = u8(team_ == Team::Red ? Team::Blue : Team::Red);
        if (const WeaponDef* w = weapon_by_model(m.model)) l.weapon = w->name, l.weapon_id = w->id;
        l.flags = m.flags;
        l.time = now - 0.5;   // settled: the feed's slide is not what is being photographed
        feed_.push_back(l);
    }
    if (!kill_effects_.loaded()) kill_effects_.load(app_.data());
    show_effect(m.effect, (m.flags & kKillWall) != 0);
    audio_->kill_call(m.flags);
    return m.effect;
}

void GameWorld::test_hud(int variant) {
    const double now = app_.now();
    const std::string me = players_.contains(me_) ? players_.at(me_).name : std::string("You");
    auto feed = [&](const char* killer, const char* victim, const char* code, u16 flags, u8 kt, u8 vt, bool mine) {
        KillFeedLine l;
        l.killer = killer, l.victim = victim;
        l.killer_id = mine ? me_ : 0xFFFFFFF0u, l.victim_id = 0xFFFFFFF1u;
        l.killer_team = kt, l.victim_team = vt;
        if (mine && players_.contains(me_)) l.killer_colour = players_.at(me_).name_colour;
        for (const WeaponDef& w : weapons())
            if (w.code == code) l.weapon = w.name, l.weapon_id = w.id;
        l.flags = flags;
        l.time = now;
        feed_.push_back(l);
        while (feed_.size() > 6) feed_.pop_front();
    };
    if (variant == 0) {
        feed("Valdentia", "Goku", "A013", 0, 0, 1, false);
        feed("taboo", "CTZN", "A009", kKillHeadshot, 1, 0, false);
        feed(me.c_str(), "Curtis", "A006", kKillHeadshot, u8(team_), 1, true);
        show_effect("HEADSHOT");
        zone_hit_at_ = now, zone_hit_ = u8(HitZone::Head), zone_hit_kill_ = true;
        hit_marker_ = now;
    } else {
        feed(me.c_str(), "steve_125", "A168", kKillSpecialForce, u8(team_), 1, true);
        show_effect("SPECIALFORCE");
        zone_hit_at_ = now, zone_hit_ = u8(HitZone::Chest), zone_hit_kill_ = true;
        // A hit from behind and to the left.
        const float th = (yaw_ + 225.0f) * eng::kDegToRad;
        damage_from_ = move_.origin + Vec3{std::sin(th) * 800.0f, 0, std::cos(th) * 800.0f};
        damaged_at_ = now;
    }
}

// ── The HUD ────────────────────────────────────────────────────────────────────

void GameWorld::draw_hud() {
    if (!ready_) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const double now = app_.now();
    const float W = ui::stage_w(), H = ui::stage_h();
    if (watching_) {
        draw_watch_hud(now);
        return;
    }

    // Flashbang white-out and smoke haze.
    // Over the picture (past it are the bars, which stay black).
    const VanVec2 pic0 = ui::stage(0, 0), pic1 = ui::stage(W, H);
    if (now < flashed_until_)
        VanGui::GetForegroundDrawList()->AddRectFilled(pic0, pic1, VAN_COL32(255, 255, 255, int(std::min(1.0, flashed_until_ - now) * 255)));
    // In a cloud of smoke the screen greys over; not through a Blind Cleanse (Horror Mode's item).
    const RoleNow* my_role = role_of(me_);
    if (!(my_role && (my_role->flags & kRoleCleansed)))
        for (const Smoke& s : smokes_)
            if (eng::length(s.pos - camera_.eye) < 400) dl->AddRectFilled(pic0, pic1, VAN_COL32(170, 170, 165, 150));

    if (scoped_) draw_scope();

    // H: the HUD off. The world's own (a scope, a flash, smoke) stays, and what you must answer
    // (the chat line, the menu, the score tab, Horror Mode 2's window).
    // The touch controls (a phone's): over the HUD, under the menus; with the HUD off too (they are
    // how the match is played), and gone while a menu, the chat or a dialog has the fingers.
    const auto draw_touch = [&] {
        if (control_) touch_.draw(VanGui::GetBackgroundDrawList(), app_.settings().touch);
    };
    if (hud_hidden_) {
        draw_touch();
        draw_chat();
        if (scoreboard_ || match_over_ || test_scores_) draw_scoreboard();
        if (briefing_held_) draw_briefing();
        draw_class_select(now);
        draw_hud_tip(now);
        if (menu_open_) draw_menu();
        return;
    }

    draw_damage_arcs(now);
    // Not through the scoreboard's glass.
    if (app_.settings().damage_numbers && !(scoreboard_ || match_over_ || test_scores_)) draw_damage_numbers(now);
    draw_crosshair(now);
    draw_radar(now);
    draw_clock();
    draw_kill_feed(now);
    draw_hit_zone(now);
    if (alive_) {
        draw_health();
        // An undead's skill bar stands where the gun's box is; the claws have none.
        if (!me_undead()) draw_weapon_box(), draw_weapon_bar(now);
    }
    draw_markers(now);
    draw_pick_up_prompt();
    draw_objectives(now);
    if (alive_) draw_skills(now);
    if (alive_) draw_horror_items(now);
    draw_cannons(now);
    draw_kill_effect(now);
    draw_banner(now);
    draw_round_summary(now);
    // The death card, but not under Horror Mode 2's select window.
    const RoleNow* role = role_of(me_);
    const bool picking = test_undead_ != Undead::None ? test_undead_picking_ : role && (role->flags & kRolePicking) && !pick_sent_;
    if (!alive_ && !match_over_ && !picking) draw_death(now);
    draw_netgraph(now);

    draw_touch();
    draw_chat();
    draw_radio_menu();
    if (briefing_held_ && !(scoreboard_ || match_over_ || test_scores_)) draw_briefing();
    if (scoreboard_ || match_over_ || test_scores_) draw_scoreboard();
    draw_hud_tip(now);
    draw_class_select(now);
    if (menu_open_) draw_menu();
}

// The radio list Z, X or C opened: its lines by number, at the left under the chat.
void GameWorld::draw_radio_menu() {
    if (radio_menu_ < 0) return;
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const u8 g = u8(radio_menu_);
    const int n = radio_count(g);
    const eng::Gamepad* pad = app_.pad_in_hand();
    const float x = 16, w = 300, h = 40 + 22.0f * float(n) + (pad ? 24.0f : 0.0f);
    const float y = ui::stage_h() * 0.62f;
    ui::panel(dl, x, y, w, h, eng::str::format("RADIO  %c  %s", "ZXC"[g], radio_group_name(g)).c_str());
    for (int i = 0; i < n; ++i) {
        const float ry = y + 34 + 22.0f * float(i);
        // The line a controller has picked.
        if (pad && i == radio_sel_) dl->AddRectFilled(ui::stage(x + 6, ry - 1), ui::stage(x + w - 6, ry + 20), ui::col(pal.select));
        ui::text(dl, ui::font_bold(), 15, x + 14, ry, ui::col(pal.lime), eng::str::format("%d.", (i + 1) % 10));
        ui::text(dl, ui::font_body(), 15, x + 40, ry, ui::col(pal.text), radio_line(g, u8(i)));
    }
    if (pad)
        ui::text(dl, ui::font_body(), 13, x + 14, y + h - 24, ui::col(pal.gold),
                 eng::str::format("D-pad: choose    %s: say    %s: close", eng::Gamepad::button_label(eng::kPadA, pad->kind()).c_str(),
                                  eng::Gamepad::button_label(eng::kPadB, pad->kind()).c_str()));
}

void GameWorld::draw_chat() {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ui::Palette& pal = ui::pal();
    const auto& lines = app_.session().room_chat;
    const double now = app_.now();
    float y = ui::stage_h() * 0.55f;
    int shown = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && shown < 6; ++it, ++shown) {
        const float age = float(now - it->time);
        const float a = chat_open_ ? 1.0f : std::clamp(10.0f - age, 0.0f, 1.0f);
        if (a <= 0) continue;
        const ChatScope scope = ChatScope(it->line.scope);
        if (scope == ChatScope::All || scope == ChatScope::Team || scope == ChatScope::Room || scope == ChatScope::Lobby) {
            // The match's own: the name in the speaker's side's colour.
            const std::string head = (scope == ChatScope::Team ? "(Team) " : "") + (it->line.from.empty() ? std::string() : it->line.from + ": ");
            ui::text(dl, ui::font_bold(), 14, 18, y, ui::col(ui::team_colour(it->line.team), a), head);
            ui::text(dl, ui::font_body(), 14, 18 + ui::text_size(ui::font_bold(), 14, head).x / ui::scale(), y, ui::col(pal.text, a), it->line.text);
        } else {
            // A whisper, the clan, global chat, the server: as the lobby shows them (Screens/Social.cpp).
            const ui::Line v = chat_view(it->line);
            const VanU32 ink = (v.colour & 0x00FFFFFFu) | (u32(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24);
            ui::text(dl, ui::font_bold(), 14, 18, y, ink, v.text);
        }
        y -= 20;
    }
    if (chat_open_) {
        static const char* hints[] = {"Say to everyone...  (F3; /w name, /r, /c, /g work too)", "Team chat...  (F4)", "Your clan, wherever they are...  (F5)",
                                      "Global chat...  (F6)"};
        ui::begin_area("##matchchat", 16, ui::stage_h() * 0.55f + 26, 560, 30);
        VanGui::SetKeyboardFocusHere();
        const bool send = ui::input("##chatin", hints[std::clamp(chat_scope_, 0, 3)], chat_text_, 560, false, VanGuiInputTextFlags_EnterReturnsTrue);
        ui::end_area();
        if (send) {
            if (!chat_text_.empty() && !offline_) {
                std::string line = chat_text_;
                if (line[0] != '/' && chat_scope_ == 2) line = "/c " + line;
                if (line[0] != '/' && chat_scope_ == 3) line = "/g " + line;
                (void)send_chat(app_, line, chat_scope_ == 1 ? ChatScope::Team : ChatScope::All);
            }
            chat_text_.clear();
            chat_open_ = false;
        }
        if (app_.window().input().key_pressed(VK_ESCAPE)) chat_open_ = false, chat_text_.clear();
    }
}

// ── The scoreboard (Tab): the client's own board for the game type (menu inf/tab/base/*_tab.tga) ──
//
// 575 x 517 with its header painted on ("SOLDIER FRONT  Code Name  Kill  Death ..."), the red side's
// half over the blue's (or one panel, a game everyone plays alone or that changes sides), the SF crest
// faint behind; under it the [Game Info] strip (tab_underbar_normal), or at the end the result strip
// (tab_underbar_result) with YOU WIN / YOU LOSE / DRAW (inf/tab/icon). Each soldier's row is under the
// board's own words: his rank, his name where "Code Name" starts, each number centred under its column.

namespace {

enum class TabCol : u8 { Kill, Death, Assist, Score, Extra0, Extra1, Extra2, Headshot, Ping };
struct TabColumn {
    float x;   // the word's middle on the board
    TabCol what;
};
struct TabBoard {
    const char* art;
    float name_x;     // where "Code Name" starts
    float top;        // the first row's top
    float split;      // where the blue half starts (0: one panel)
    int n;
    TabColumn cols[5];
};
// The words' middles, measured off each board (Tools: the header strip's lit runs).
const TabBoard kTeamTab{"inf/tab/base/team_tab.tga", 151, 30, 274, 4, {{290, TabCol::Kill}, {359, TabCol::Death}, {426, TabCol::Assist}, {479, TabCol::Ping}}};
const TabBoard kDeathmatchTab{"inf/tab/base/deathmatch_tab.tga", 146, 30, 274, 4, {{287, TabCol::Kill}, {345, TabCol::Death}, {413, TabCol::Extra0}, {489, TabCol::Ping}}};
const TabBoard kPrivTab{"inf/tab/base/priv_tab.tga", 151, 30, 0, 3, {{350, TabCol::Kill}, {419, TabCol::Death}, {479, TabCol::Ping}}};
const TabBoard kPracTab{"inf/tab/base/prac_tab.tga", 156, 28, 0, 3, {{338, TabCol::Kill}, {427, TabCol::Headshot}, {499, TabCol::Ping}}};
const TabBoard kCtcTab{"inf/tab/base/ctc_tab.tga", 146, 30, 274, 4, {{287, TabCol::Kill}, {356, TabCol::Death}, {427, TabCol::Extra0}, {489, TabCol::Ping}}};
const TabBoard kOccupyTab{"inf/tab/base/occupy_tab.tga", 150, 28, 274, 5,
                          {{268, TabCol::Kill}, {319, TabCol::Death}, {381, TabCol::Extra0}, {453, TabCol::Extra1}, {522, TabCol::Ping}}};
const TabBoard kZombieTab{"inf/tab/base/zombie_tab.tga", 107, 30, 0, 4, {{270, TabCol::Score}, {356, TabCol::Extra0}, {451, TabCol::Extra1}, {525, TabCol::Ping}}};
const TabBoard kZombie2Tab{"inf/tab/base/zombie2_tab.tga", 126, 36, 275, 5,
                           {{285, TabCol::Extra0}, {356, TabCol::Extra1}, {416, TabCol::Death}, {467, TabCol::Extra2}, {515, TabCol::Ping}}};
const TabBoard kPirateTab{"inf/tab/base/pirate_tab.tga", 122, 40, 286, 4, {{283, TabCol::Extra0}, {357, TabCol::Extra1}, {436, TabCol::Kill}, {516, TabCol::Ping}}};

const TabBoard& tab_board(Mode m) {
    switch (m) {
        case Mode::TeamDeathmatch:
        case Mode::TeamSlayer: return kDeathmatchTab;
        case Mode::SingleBattle: return kPrivTab;
        case Mode::Training: return kPracTab;
        case Mode::CaptureTheCaptain:
        case Mode::Captain: return kCtcTab;
        case Mode::Occupy: return kOccupyTab;
        case Mode::Horror: return kZombieTab;
        case Mode::Horror2: return kZombie2Tab;
        case Mode::Pirate: return kPirateTab;
        default: return kTeamTab;
    }
}

constexpr float kBoardW = 575, kBoardH = 517, kRowH = 29;
constexpr float kBoardScale = 1.12f;   // the board's pixels on the 900-high HUD stage (it filled 2/3 of the original's 768)
constexpr float kBoardTop = 78;

}  // namespace

bool GameWorld::result_strip(float& x0, float& y0, float& x1, float& y1) const {
    if (!match_over_) return false;
    const float s = kBoardScale;
    x0 = ui::stage_w() * 0.5f - kBoardW * s * 0.5f;
    x1 = x0 + 576 * s;
    y0 = kBoardTop + kBoardH * s;
    y1 = y0 + 125 * s;
    return true;
}

void GameWorld::draw_scoreboard() {
    // Over everything while the match is played; at the end under the Result screen's own button.
    VanDrawList* dl = match_over_ ? VanGui::GetBackgroundDrawList() : VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const TabBoard& b = tab_board(settings_.mode);
    const float s = kBoardScale;
    const float x0 = ui::stage_w() * 0.5f - kBoardW * s * 0.5f, y0 = kBoardTop;
    auto X = [&](float bx) { return x0 + bx * s; };
    auto Y = [&](float by) { return y0 + by * s; };
    ui::picture(dl, menu_art(b.art), x0, y0, kBoardW * s, kBoardH * s, 0xFFFFFFFF, false);

    // Who goes where: by side into the halves (a one-panel board: everyone, the best first).
    std::vector<const PlayerView*> rows;
    for (const auto& [id, p] : players_)
        if (p.team != Team::Observer) rows.push_back(&p);
    std::sort(rows.begin(), rows.end(), [](const PlayerView* a, const PlayerView* c) {
        if (a->score != c->score) return a->score > c->score;
        return a->kills != c->kills ? a->kills > c->kills : a->deaths < c->deaths;
    });
    VanFont* bold = ui::font_page_bold();
    const float text_size = 14.5f * s;
    auto value_of = [&](const PlayerView& p, TabCol c) -> std::string {
        switch (c) {
            case TabCol::Kill: return std::to_string(p.kills);
            case TabCol::Death: return std::to_string(p.deaths);
            case TabCol::Assist: return std::to_string(p.assists);
            case TabCol::Score: return std::to_string(p.score);
            case TabCol::Extra0: return std::to_string(p.extra[0]);
            case TabCol::Extra1: return std::to_string(p.extra[1]);
            case TabCol::Extra2: return std::to_string(p.extra[2]);
            case TabCol::Headshot: return std::to_string(p.headshots);
            case TabCol::Ping: return std::to_string(p.ping);
        }
        return {};
    };
    auto draw_row = [&](const PlayerView& p, float by) {
        const float ry = Y(by), rh = kRowH * s;
        if (p.id == me_) dl->AddRectFilled(ui::stage(X(4), ry + 1), ui::stage(X(kBoardW - 4), ry + rh - 1), ui::col(pal.select, 0.75f));
        // His rank before his name, and what the game made of him (a captain, an undead) after it.
        if (ui::Atlas* a = ui::atlas()) ui::picture(dl, a->rank_badge(rank_for_xp(p.xp)), X(b.name_x) - 24 * s, ry + 4 * s, 20 * s, 20 * s, 0xFFFFFFFF, false);
        const VanU32 ink = p.alive || match_over_ ? VAN_COL32(236, 236, 230, 255) : VAN_COL32(150, 150, 146, 255);
        name_text(dl, bold, text_size, X(b.name_x), ry + 6 * s, ink, p.name, p.name_colour);
        float marks_x = X(b.cols[0].x - 30);
        if (const RoleNow* r = role_of(p.id)) {
            const char* what = MatchRole(r->role) == MatchRole::Captain ? "CAPTAIN" : MatchRole(r->role) == MatchRole::Host ? "HOST"
                               : MatchRole(r->role) == MatchRole::Zombie                                                ? undead_def(Undead(r->undead)).name
                               : MatchRole(r->role) == MatchRole::Escaped                                               ? "ESCAPED"
                                                                                                                         : nullptr;
            if (what) ui::text(dl, bold, 10.0f * s, X(b.cols[0].x - 34), ry + 9 * s, ui::col(pal.warn), what, ui::Align::Right), marks_x -= 58 * s;
        }
        // His row's marks, in the original's own icons, from the first column leftward: what he
        // has bought to earn more, a set worn whole, an event running.
        static const struct {
            u16 bit;
            const char* art;
        } kMarks[] = {{kMarkEvent, "inf/tab/icon/guerillaevent.tga"},      {kMarkExpX5, "inf/tab/icon/pointx5.bmp"},   {kMarkExpX3, "inf/tab/icon/pointx3.bmp"},
                      {kMarkSanta, "inf/tab/icon/santa.bmp"},              {kMarkBlackDragon, "inf/tab/icon/setitem_blackdragon.bmp"},
                      {kMarkSpecial, "inf/tab/icon/special_point.tga"},    {kMarkHolyBless, "inf/tab/icon/holy bless_32.tga"},
                      {kMarkDoubleUp, "inf/tab/icon/doubleup.bmp"},        {kMarkPointsX2, "inf/tab/icon/point2x.bmp"}};
        for (const auto& m : kMarks) {
            if (!(p.marks & m.bit)) continue;
            const ui::Picture pic = menu_art(m.art);
            if (!pic.valid() || pic.h <= 0) continue;
            const float h = 18 * s, w = h * float(pic.w) / float(pic.h);
            marks_x -= w + 2 * s;
            ui::picture(dl, pic, marks_x, ry + 5 * s, w, h, 0xFFFFFFFF, false);
        }
        for (int i = 0; i < b.n; ++i) {
            const TabColumn& c = b.cols[i];
            if (c.what == TabCol::Ping) {
                const char* light = p.ping < 80 ? "inf/ui_ping_green.tga" : p.ping < 160 ? "inf/ui_ping_orange.tga" : "inf/ui_ping_red.tga";
                ui::picture(dl, menu_art(light), X(c.x) - 30 * s, ry + 4 * s, 20 * s, 20 * s, 0xFFFFFFFF, false);
            }
            ui::text(dl, bold, text_size, X(c.x), ry + 6 * s, ink, value_of(p, c.what), ui::Align::Center);
        }
    };
    const bool halves = b.split > 0 && mode_info(settings_.mode).teams;
    if (halves) {
        for (int side = 0; side < 2; ++side) {
            const Team t = side == 0 ? Team::Red : Team::Blue;
            float by = side == 0 ? b.top : b.split + 4;
            const float limit = side == 0 ? b.split : kBoardH - 4;
            for (const PlayerView* p : rows)
                if (p->team == t && by + kRowH <= limit) draw_row(*p, by), by += kRowH;
            // The side's score, faint, at the half's right foot.
            const u16 sc = side == 0 ? red_score_ : blue_score_;
            ui::text(dl, bold, 22.0f * s, X(kBoardW - 14), Y(limit - 32), VAN_COL32(255, 255, 255, 110), std::to_string(sc), ui::Align::Right);
        }
    } else {
        float by = b.top;
        for (const PlayerView* p : rows)
            if (by + kRowH <= kBoardH - 4) draw_row(*p, by), by += kRowH;
    }

    // Under the board: [Game Info] while it is played; at the end, the result strip with its plate.
    const float uy = Y(kBoardH);
    if (match_over_) {
        ui::picture(dl, menu_art("inf/tab/base/tab_underbar_result.tga"), x0, uy, 576 * s, 125 * s, 0xFFFFFFFF, false);
        const char* plate = banner_ == "YOU WIN" ? "inf/tab/icon/win.tga" : banner_ == "YOU LOSE" ? "inf/tab/icon/lose.tga" : banner_ == "DRAW" ? "inf/tab/icon/draw.tga"
                                                                                                                                                  : "inf/tab/icon/wait.tga";
        ui::picture(dl, menu_art(plate), X(12), uy + 8 * s, 128 * s, 64 * s, 0xFFFFFFFF, false);
        if (const auto it = players_.find(me_); it != players_.end())
            ui::text(dl, bold, 14.0f * s, X(152), uy + 14 * s, VAN_COL32(236, 236, 230, 255),
                     eng::str::format("%u kills   %u deaths   %u points", unsigned(it->second.kills), unsigned(it->second.deaths), unsigned(it->second.score)));
    } else {
        ui::picture(dl, menu_art("inf/tab/base/tab_underbar_normal.tga"), x0, uy, 576 * s, 35 * s, 0xFFFFFFFF, false);
        std::string info = eng::str::format("[Game Info] %s, %s", mode_name(settings_.mode), app_.map_title(settings_.map).c_str());
        if (mode_info(settings_.mode).teams) info += eng::str::format("   Red %u : %u Blue", unsigned(red_score_), unsigned(blue_score_));
        if (settings_.goal) info += eng::str::format("   (%s %u)", mode_info(settings_.mode).goal_label, unsigned(settings_.goal));
        ui::text(dl, bold, 13.0f * s, X(14), uy + 10 * s, VAN_COL32(224, 224, 216, 255), info);
    }
}

void GameWorld::draw_menu() {
    // The veil and panel go in the menu's own window, under its buttons; the foreground list
    // would paint over them.
    const float W = ui::stage_w(), H = ui::stage_h();
    ui::begin_area("##matchmenu", 0, 0, W, H);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->AddRectFilled(ui::stage(0, 0), ui::stage(W, H), VAN_COL32(0, 0, 0, 90));
    // Resume, Settings, Report a player, Kick a player (a vote), the staff panel for staff, Leave.
    ScreenState& st = app_.state();
    const bool staff = app_.session().profile.staff();
    const bool online = !offline_;
    const int count = 3 + (online ? 2 : 0) + (staff ? 1 : 0);
    const float top = H * 0.5f - 25.0f * float(count) - 20, height = 50.0f * float(count) + 54;
    ui::panel(dl, W * 0.5f - 170, top, 340, height, "MENU");
    float y = top + 40;
    auto next = [&](const char* label, ui::Style style = ui::Style::Normal) {
        VanGui::SetCursorScreenPos(ui::stage(W * 0.5f - 150, y));
        y += 50;
        return ui::button(label, 300, 44, style);
    };
    if (next("Resume", ui::Style::Primary)) menu_open_ = false;
    if (next("Settings")) open_settings(app_, -1, -1);
    if (online && next("Report a player")) st.report_open = true, st.report_player = 0;
    if (online && next("Kick a player (vote)")) st.vote_open = true, st.vote_target = 0;
    if (staff && next("Staff panel  (F9)")) st.staff_open = true, st.staff_asked = false;
    if (next("Leave the match", ui::Style::Danger)) {
        menu_open_ = false;
        app_.leave_match();
    }
    ui::end_area();
}

// The scope, in the weapon's own art (weapon.kst SCOPE_IMAGE, effect scope/): drawn over a 4:3 box
// across the screen's height, as the original stretched it over its 4:3 screen (the square pictures'
// lenses are drawn tall for that), the sides black where the art's edges are; a thin cross through
// the lens; a dot_ picture is a red-dot sight's reticle alone, at the centre.
void GameWorld::draw_scope() {
    const WeaponDef* w = my_weapon(slot_);
    if (!w) return;
    const std::string key = w->scope_image.empty() ? std::string("scope2.bmp") : w->scope_image;
    auto it = scope_art_.find(key);
    if (it == scope_art_.end()) {
        ScopeArt art;
        eng::Image img;
        const auto bytes = app_.data().read(sf::Pack::Effect, "scope/" + key);
        if (bytes && sf::decode_image(*bytes, img) && img.width && img.height && ui::atlas()) {
            bool any_alpha = false;
            for (size_t k = 3; k < img.rgba.size(); k += 4) any_alpha |= img.rgba[k] < 250;
            if (!any_alpha) {
                // A mask: black covers, white is the lens.
                for (size_t k = 0; k + 3 < img.rgba.size(); k += 4) {
                    const int lum = (img.rgba[k] * 3 + img.rgba[k + 1] * 6 + img.rgba[k + 2]) / 10;
                    img.rgba[k] = img.rgba[k + 1] = img.rgba[k + 2] = 0;
                    img.rgba[k + 3] = u8(255 - lum);
                }
            }
            const size_t mid = (size_t(img.height / 2) * img.width) * 4;
            art.sides = img.rgba[mid + 3] > 128 && img.rgba[mid + size_t(img.width - 1) * 4 + 3] > 128;
            // The lens: the clear run through the middle, across and down.
            auto alpha = [&](u32 x, u32 y) { return img.rgba[(size_t(y) * img.width + x) * 4 + 3]; };
            const u32 cx = img.width / 2, cy = img.height / 2;
            u32 l = cx, r = cx, t = cy, b = cy;
            while (l > 0 && alpha(l - 1, cy) < 128) --l;
            while (r + 1 < img.width && alpha(r + 1, cy) < 128) ++r;
            while (t > 0 && alpha(cx, t - 1) < 128) --t;
            while (b + 1 < img.height && alpha(cx, b + 1) < 128) ++b;
            if (alpha(cx, cy) < 128 && r > l && b > t) {
                art.lens_w = float(r - l + 1) / float(img.width);
                art.lens_h = float(b - t + 1) / float(img.height);
            }
            art.reticle = key.rfind("dot_", 0) == 0;
            art.pic = ui::atlas()->image_picture("scope|" + key, img);
        } else {
            LOG_WARN("Scope art scope/%s: not readable", key.c_str());
        }
        it = scope_art_.emplace(key, art).first;
    }
    const ScopeArt& art = it->second;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    // Over the picture (all of the window, or the part of it between the bars).
    const ui::ViewRect vr = ui::view();
    const VanVec2 p0{vr.x, vr.y}, p1{vr.x + vr.w, vr.y + vr.h};
    // As the original laid it over its 4:3 screen, then enlarged about the middle until the lens
    // stands 94% of the screen's height: the art's own lens fills the view, its rim and mount at
    // the edges (a lens already that tall is drawn as it is).
    const float base_h = vr.h, base_w = vr.h * 4.0f / 3.0f;
    const float grow = std::max(1.0f, 0.94f / std::max(0.05f, art.lens_h));
    const float bh = base_h * grow, bw = base_w * grow;
    const VanVec2 c{vr.x + vr.w * 0.5f, vr.y + vr.h * 0.5f};
    const float x0 = c.x - bw * 0.5f, x1 = c.x + bw * 0.5f, y0 = c.y - bh * 0.5f, y1 = c.y + bh * 0.5f;
    const VanU32 black = VAN_COL32(0, 0, 0, 255);
    if (art.reticle && art.pic.valid()) {
        const float s = bh * 0.14f;
        dl->AddImage(VanTextureRef(VanTextureID(art.pic.tex)), {c.x - s * 0.5f, c.y - s * 0.5f}, {c.x + s * 0.5f, c.y + s * 0.5f});
        return;
    }
    dl->PushClipRect(p0, p1, true);
    if (art.pic.valid()) {
        dl->AddImage(VanTextureRef(VanTextureID(art.pic.tex)), {x0, y0}, {x1, y1});
        if (art.sides) {
            if (x0 > p0.x) dl->AddRectFilled(p0, {x0 + 1, p1.y}, black);
            if (x1 < p1.x) dl->AddRectFilled({x1 - 1, p0.y}, p1, black);
        }
    } else {
        // No art: a plain round lens.
        const float r = bh * 0.46f;
        dl->AddRectFilled(p0, {c.x - r, p1.y}, black);
        dl->AddRectFilled({c.x + r, p0.y}, p1, black);
        dl->AddCircle(c, r + bh * 0.25f, black, 128, bh * 0.5f);
    }
    // The cross, thin, the full width of the lens (the art's solid parts hide its ends).
    const float t = std::max(1.0f, vr.h / 768.0f);
    dl->AddLine({p0.x, c.y}, {p1.x, c.y}, VAN_COL32(0, 0, 0, 235), t);
    dl->AddLine({c.x, p0.y}, {c.x, p1.y}, VAN_COL32(0, 0, 0, 235), t);
    dl->PopClipRect();
}

// Each hit of yours as a number over the soldier who took it, the newest at the foot of the hits
// (a head shot's in gold), and your total for him under them all, larger; the
// lot fades out three seconds after your last hit on him.
void GameWorld::draw_damage_numbers(double now) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    VanFont* font = ui::font_heading();
    for (auto it = damage_numbers_.begin(); it != damage_numbers_.end();) {
        DamageStack& s = it->second;
        const double age = now - s.last;
        if (age > 3.0) {
            it = damage_numbers_.erase(it);
            continue;
        }
        // Over his head while he is seen; where he was last seen when he is not.
        if (auto p = players_.find(it->first); p != players_.end() && !p->second.hidden) {
            const float h = hull_height_by_flags(move_def_, p->second.flags);
            s.above = p->second.position + Vec3{0, h + 26.0f, 0};
        }
        VanVec2 at;
        if (!to_screen(s.above, at)) {
            ++it;
            continue;
        }
        const float fade = float(std::clamp((3.0 - age) / 0.5, 0.0, 1.0));
        auto ink = [&](int r, int g, int b) { return VAN_COL32(r, g, b, int(255 * fade)); };
        const VanU32 shade = VAN_COL32(0, 0, 0, int(200 * fade));
        // The total at the foot, the hits stacked up from it.
        const float total_size = 28.0f, hit_size = 20.0f;
        float y = at.y - total_size;
        const std::string total = std::to_string(s.total);
        ui::text(dl, font, total_size, at.x + 1.5f, y + 1.5f, shade, total, ui::Align::Center, false);
        ui::text(dl, font, total_size, at.x, y, ink(255, 222, 96), total, ui::Align::Center, false);
        // A rule between the hits and their sum, once there are two to add up.
        if (s.hits.size() > 1) dl->AddLine(ui::stage(at.x - 22, y - 2), ui::stage(at.x + 22, y - 2), ink(255, 222, 96), ui::px(1.5f));
        y -= 4;
        if (s.hits.size() > 1)
            for (auto h = s.hits.rbegin(); h != s.hits.rend(); ++h) {
                y -= hit_size;
                // The newest pops in.
                const float pop = float(std::max(0.0, 1.0 - (now - h->at) / 0.18));
                const float size = hit_size * (1.0f + 0.35f * pop);
                const VanU32 c = h->kind == 1 ? ink(255, 196, 60) : ink(240, 240, 236);
                const std::string n = std::to_string(h->amount);
                ui::text(dl, font, size, at.x + 1.2f, y + 1.2f, shade, n, ui::Align::Center, false);
                ui::text(dl, font, size, at.x, y, c, n, ui::Align::Center, false);
            }
        ++it;
    }
}

// ── H, B and O ─────────────────────────────────────────────────────────────────

// What H or B just did, the original's way ("[Blood effect off]"): a line at the top left for a few
// seconds, over the HUD or where it was.
void GameWorld::draw_hud_tip(double now) {
    const double age = now - hud_tip_at_;
    if (hud_tip_.empty() || age > 4.0) return;
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const float a = float(std::clamp(4.0 - age, 0.0, 1.0));
    const float y = hud_hidden_ ? 18.0f : 214.0f;
    ui::text(dl, ui::font_bold(), 15, 21, y + 1, VAN_COL32(0, 0, 0, int(200 * a)), hud_tip_);
    ui::text(dl, ui::font_bold(), 15, 20, y, VAN_COL32(255, 222, 96, int(255 * a)), hud_tip_);
}

// O held: the mission, as the waiting room's MISSION box says it -- the game type's brief, the
// map's own words for your side, the goal and the clock.
void GameWorld::draw_briefing() {
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const ui::Palette& pal = ui::pal();
    const float W = ui::stage_w(), H = ui::stage_h();
    const float w = 560, h = 230, x = W * 0.5f - w * 0.5f, y = H * 0.5f - h * 0.5f - 60;
    ui::panel(dl, x, y, w, h, nullptr);
    const ModeInfo& mi = mode_info(settings_.mode);
    ui::text(dl, ui::font_bold(), 13, x + 18, y + 12, ui::col(pal.text_dim), "[Mission]");
    ui::text(dl, ui::font_display(), 26, x + 18, y + 28, ui::col(pal.gold_bright), std::string(mi.name) + "  -  " + app_.map_title(settings_.map));
    std::string words;
    if (const char* brief = mode_brief(settings_.mode)) {
        words = brief;
    } else if (const sf::MapInfo* info = app_.map_info(settings_.map)) {
        // Team Battle: the map's own words for the side you are on (red attacks, blue defends).
        words = team_ == Team::Blue ? info->defence_text : info->attack_text;
    }
    if (words.empty() && settings_.mode == Mode::TeamBattle) words = "Wipe out the other side.";
    // The words, wrapped to the panel (the map's own lines kept where it breaks them).
    float ty = y + 66;
    for (std::string_view para : eng::str::split(words, '\n')) {
        std::string line;
        for (std::string_view word : eng::str::split(para, ' ')) {
            if (word.empty()) continue;
            const std::string next = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && ui::text_size(ui::font_body(), 15, next).x / ui::scale() > w - 36) {
                ui::text(dl, ui::font_body(), 15, x + 18, ty, ui::col(pal.text), line);
                ty += 20;
                line = std::string(word);
            } else {
                line = next;
            }
        }
        if (!line.empty()) ui::text(dl, ui::font_body(), 15, x + 18, ty, ui::col(pal.text), line), ty += 20;
        if (ty > y + h - 54) break;
    }
    std::string goal = mi.goal_label ? eng::str::format("%s: %u", mi.goal_label, unsigned(settings_.goal)) : std::string();
    goal += eng::str::format("%s%u minute%s%s", goal.empty() ? "" : "     ", unsigned(settings_.minutes), settings_.minutes == 1 ? "" : "s", mi.rounds ? " a round" : "");
    ui::text(dl, ui::font_bold(), 14, x + 18, y + h - 34, ui::col(pal.lime), goal);
}

}  // namespace lsf
