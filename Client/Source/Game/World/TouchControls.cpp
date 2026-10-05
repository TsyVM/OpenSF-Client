#include "Game/World/TouchControls.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Window.hpp"
#include "Game/Ui/Ui.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace lsf {

namespace {

// A phone shows the controls from the start of a match; a desktop only once a finger comes.
#ifdef __ANDROID__
constexpr bool kTouchFirst = true;
#else
constexpr bool kTouchFirst = false;
#endif

// Sizes are in points of a 1080-tall screen (times the size settings): the same reach for the
// thumbs on every phone.
constexpr float kStickRadius = 120.0f;
constexpr float kDeadZone = 0.12f;   // of the stick's reach: nothing
constexpr float kWalkZone = 0.6f;    // under it the soldier walks (quietly, as the Walk key); past it he runs
// A swipe the height of the screen turns 140 degrees at look 1.
constexpr float kLookDegreesPerPoint = 0.13f;

enum class Kind { Hold, Toggle, Tap };
struct ButtonInfo {
    const char* label;
    Action action;   // Action::Count: none (Menu and Chat are the match's own; the stick is no button)
    Kind kind;
};
// In TouchPart's order.
constexpr ButtonInfo kInfo[kTouchParts] = {
    {"FIRE", Action::Shoot, Kind::Hold},       {"FIRE", Action::Shoot, Kind::Hold},     {"AIM", Action::Zoom, Kind::Hold},
    {"JUMP", Action::Jump, Kind::Hold},        {"CROUCH", Action::Knee, Kind::Toggle},  {"RELOAD", Action::Reload, Kind::Hold},
    {"SWAP", Action::NextWeapon, Kind::Hold},  {"USE", Action::Use, Kind::Hold},        {"MENU", Action::Count, Kind::Tap},
    {"CHAT", Action::Count, Kind::Tap},        {"SCORE", Action::ScoreView, Kind::Toggle}, {"", Action::Count, Kind::Hold},
};
constexpr int kStick = int(TouchPart::Stick);
constexpr int kButtons = kStick;   // the parts before the stick are buttons

struct Colours {
    VanU32 fill, rest, lit, ring, ink, faint;
};
Colours colours(float opacity) {
    const float op = std::clamp(opacity, 0.2f, 1.0f);
    auto a = [&](float v) { return int(std::clamp(v * op, 0.0f, 255.0f)); };
    return {VAN_COL32(12, 12, 10, a(150)), VAN_COL32(12, 12, 10, a(75)),     VAN_COL32(170, 220, 70, a(190)),
            VAN_COL32(221, 219, 207, a(210)), VAN_COL32(240, 238, 228, a(255)), VAN_COL32(221, 219, 207, a(70))};
}

void draw_button(VanDrawList* dl, const TouchControls::Place& p, const char* label, bool on, bool big, const Colours& c, float unit) {
    dl->AddCircleFilled({p.x, p.y}, p.r, on ? c.lit : c.fill, 40);
    dl->AddCircle({p.x, p.y}, p.r, c.ring, 40, (big ? 3.0f : 2.0f) * unit);
    VanFont* font = ui::font_bold();
    if (!font || !*label) return;
    const float size = big ? std::min(p.r * 0.30f, 40.0f * unit) : std::clamp(p.r * 0.32f, 12.0f * unit, 26.0f * unit);
    const VanVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, label);
    dl->AddText(font, size, {p.x - ts.x * 0.5f, p.y - ts.y * 0.5f}, on ? VAN_COL32(10, 12, 6, 255) : c.ink, label);
}

void draw_stick(VanDrawList* dl, float x, float y, float r, float kx, float ky, bool live, bool walking, const Colours& c, float unit) {
    dl->AddCircleFilled({x, y}, r, live ? c.fill : c.rest, 48);
    dl->AddCircle({x, y}, r, c.ring, 48, 2.5f * unit);
    dl->AddCircle({x, y}, r * kWalkZone, c.faint, 40, 1.5f * unit);
    dl->AddCircleFilled({kx, ky}, r * 0.42f, live ? (walking ? c.ring : c.lit) : c.faint, 32);
}

}  // namespace

TouchControls::TouchControls() : shown_(kTouchFirst) {}

// ── Where the controls are ─────────────────────────────────────────────────────

TouchControls::Layout TouchControls::lay_out(const Screen& sc, const TouchSettings& s, bool scope, bool arranging) {
    const float W = std::max(1.0f, sc.w), H = std::max(1.0f, sc.h);
    const float u = H / 1080.0f * s.size;
    const float left = sc.inset[0], top = sc.inset[1], right = W - sc.inset[2], bottom = H - sc.inset[3];
    Layout L{};
    auto at = [&](TouchPart part, float x, float y, float r, bool visible) {
        const TouchPlace& tp = s.places[size_t(part)];
        Place& p = L[size_t(part)];
        p.x = tp.moved() ? tp.x * W : x;
        p.y = tp.moved() ? tp.y * H : y;
        p.r = r * u * std::clamp(tp.scale, 0.5f, 2.0f);
        p.visible = visible;
    };
    // Out of the box: the right thumb's with Fire biggest, where the thumb rests, the rest around it;
    // the left thumb's second Fire over the stick; Menu, Score and Chat either side of the clock.
    at(TouchPart::Fire, right - 215 * u, bottom - 300 * u, 100, true);
    at(TouchPart::Aim, right - 400 * u, bottom - 165 * u, 58, scope || arranging);
    at(TouchPart::Crouch, right - 85 * u, bottom - 120 * u, 55, true);
    at(TouchPart::Jump, right - 95 * u, bottom - 470 * u, 60, true);
    at(TouchPart::Reload, right - 420 * u, bottom - 340 * u, 55, true);
    at(TouchPart::Swap, right - 290 * u, bottom - 520 * u, 52, true);
    at(TouchPart::Use, right - 470 * u, bottom - 520 * u, 48, true);
    at(TouchPart::LeftFire, left + 230 * u, bottom - 520 * u, 62, s.left_fire);
    at(TouchPart::Menu, W * 0.5f - 320 * u, top + 48 * u, 36, true);
    at(TouchPart::Score, W * 0.5f + 320 * u, top + 48 * u, 36, true);
    at(TouchPart::Chat, W * 0.5f + 410 * u, top + 48 * u, 36, true);
    at(TouchPart::Stick, left + 260 * u, bottom - 250 * u, kStickRadius, true);
    return L;
}

// ── In a match ─────────────────────────────────────────────────────────────────

bool TouchControls::down(Action a) const {
    for (int b = 0; b < kButtons; ++b)
        if (kInfo[b].action == a && (held_[size_t(b)] || on_[size_t(b)])) return true;
    return false;
}

bool TouchControls::pressed(Action a) const {
    for (int b = 0; b < kButtons; ++b)
        if (kInfo[b].action == a && hit_[size_t(b)]) return true;
    return false;
}

void TouchControls::let_go() {
    fingers_.clear();
    held_.fill(false);
    moving_ = walking_ = stick_live_ = false;
    forward_ = side_ = 0;
}

int TouchControls::button_at(float x, float y) const {
    for (int b = 0; b < kButtons; ++b) {
        const Place& p = place_[size_t(b)];
        // A little past the drawn edge: a thumb is wide.
        if (p.visible && std::hypot(x - p.x, y - p.y) <= p.r * 1.15f) return b;
    }
    return -1;
}

// The stick comes to a thumb landing low on its side of the screen (the side it rests on, so a
// left-handed layout has it on the right).
bool TouchControls::stick_zone(float x, float y, float W, float H) const {
    const Place& rest = place_[size_t(kStick)];
    const bool on_left = rest.x < W * 0.5f;
    const float top = std::min(H * 0.28f, rest.y - rest.r * 1.5f);
    return (on_left ? x < W * 0.45f : x > W * 0.55f) && y > top;
}

void TouchControls::update(const eng::Window& window, const TouchSettings& s, const Context& c) {
    Screen sc;
    sc.w = float(std::max(1, window.width())), sc.h = float(std::max(1, window.height()));
#ifndef _WIN32
    for (int i = 0; i < 4; ++i) sc.inset[i] = float(window.safe_inset[i]);
#endif
    const float W = sc.w, H = sc.h;
    place_ = lay_out(sc, s, c.scope);
    unit_ = H / 1080.0f * s.size;
    const Place& rest = place_[size_t(kStick)];
    if (!stick_live_) stick_x_ = knob_x_ = rest.x, stick_y_ = knob_y_ = rest.y;

    hit_.fill(false);
    menu_pressed_ = chat_pressed_ = false;
    look_x_ = look_y_ = 0;
    if (!c.alive) on_[size_t(TouchPart::Crouch)] = false;
    if (!s.enabled) {
        let_go();
        shown_ = false;
        return;
    }
    // A controller or the keyboard in use: the controls step aside until the next touch.
    if (c.other_input && shown_) {
        let_go();
        on_.fill(false);
        shown_ = false;
    }
    // A menu, the chat or a dialog up: the fingers are the menus' pointer again.
    if (!c.control) {
        let_go();
        return;
    }

    const auto& touches = window.touches();
    // The fingers lifted let go of what they held.
    std::erase_if(fingers_, [&](const Finger& f) {
        const bool gone = std::none_of(touches.begin(), touches.end(), [&](const eng::Window::Touch& t) { return t.id == f.id; });
        if (gone && f.part == Finger::Part::Button) held_[size_t(f.button)] = false;
        return gone;
    });
    for (const eng::Window::Touch& t : touches) {
        auto it = std::find_if(fingers_.begin(), fingers_.end(), [&](const Finger& f) { return f.id == t.id; });
        if (it == fingers_.end()) {
            // A finger already down before the match had the controls is not theirs.
            if (!t.pressed) continue;
            shown_ = true;
            Finger f;
            f.id = t.id;
            f.x = t.x, f.y = t.y;
            const bool stick_taken = std::any_of(fingers_.begin(), fingers_.end(), [](const Finger& g) { return g.part == Finger::Part::Stick; });
            if (const int b = button_at(t.x0, t.y0); b >= 0) {
                f.part = Finger::Part::Button;
                f.button = b;
                held_[size_t(b)] = true;
                hit_[size_t(b)] = true;
                if (kInfo[b].kind == Kind::Toggle) on_[size_t(b)] = !on_[size_t(b)];
                if (b == int(TouchPart::Menu)) menu_pressed_ = true;
                if (b == int(TouchPart::Chat)) chat_pressed_ = true;
            } else if (!stick_taken && stick_zone(t.x0, t.y0, W, H)) {
                // The stick comes to the thumb, kept whole on the screen.
                f.part = Finger::Part::Stick;
                f.bx = std::clamp(t.x0, rest.r + 12 * unit_, W - rest.r - 12 * unit_);
                f.by = std::clamp(t.y0, rest.r + 12 * unit_, H - rest.r - 12 * unit_);
            } else {
                f.part = Finger::Part::Look;
            }
            fingers_.push_back(f);
            it = fingers_.end() - 1;
        }
        // Looking: a finger on the open screen, and Fire's while it is held (aim as you shoot).
        if (it->part == Finger::Part::Look || (it->part == Finger::Part::Button && it->button == int(TouchPart::Fire))) {
            look_x_ += t.x - it->x;
            look_y_ += t.y - it->y;
        }
        it->x = t.x, it->y = t.y;
    }
    const float degrees = kLookDegreesPerPoint * 1080.0f / H * s.look;
    look_x_ *= degrees;
    look_y_ *= degrees;

    moving_ = walking_ = stick_live_ = false;
    forward_ = side_ = 0;
    for (const Finger& f : fingers_) {
        if (f.part != Finger::Part::Stick) continue;
        const float dx = f.x - f.bx, dy = f.y - f.by, len = std::hypot(dx, dy);
        const float reach = std::min(len, rest.r);
        stick_live_ = true;
        stick_x_ = f.bx, stick_y_ = f.by;
        knob_x_ = len > 0 ? f.bx + dx / len * reach : f.bx;
        knob_y_ = len > 0 ? f.by + dy / len * reach : f.by;
        const float push = len / std::max(1.0f, rest.r);
        if (push > kDeadZone) {
            moving_ = true;
            walking_ = push < kWalkZone;
            forward_ = -dy / len;
            side_ = dx / len;
        }
    }
}

void TouchControls::draw(VanDrawList* dl, const TouchSettings& s) const {
    if (!shown_ || !dl) return;
    const Colours c = colours(s.opacity);
    // The stick: its ring where the thumb came down (or where it rests), the knob under the thumb.
    draw_stick(dl, stick_x_, stick_y_, place_[size_t(kStick)].r, knob_x_, knob_y_, stick_live_, walking_, c, unit_);
    for (int b = 0; b < kButtons; ++b) {
        const Place& p = place_[size_t(b)];
        if (p.visible) draw_button(dl, p, kInfo[b].label, held_[size_t(b)] || on_[size_t(b)], b == int(TouchPart::Fire), c, unit_);
    }
}

// ── The arranging screen ───────────────────────────────────────────────────────

bool TouchControls::arrange(TouchSettings& s, const Screen& sc, int& picked) {
    VanGuiIO& io = VanGui::GetIO();
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float W = std::max(1.0f, sc.w), H = std::max(1.0f, sc.h);
    const float unit = H / 1080.0f * s.size;
    picked = std::clamp(picked, 0, int(kTouchParts) - 1);
    // The part held by the pointer, and where on it the pointer took hold.
    static int dragging = -1;
    static VanVec2 grip{0, 0};

    dl->AddRectFilled({0, 0}, {W, H}, VAN_COL32(0, 0, 0, 110));
    // The thirds, to line things up by.
    for (int i = 1; i < 3; ++i) {
        dl->AddLine({W * float(i) / 3.0f, 0}, {W * float(i) / 3.0f, H}, VAN_COL32(221, 219, 207, 30), 1.0f);
        dl->AddLine({0, H * float(i) / 3.0f}, {W, H * float(i) / 3.0f}, VAN_COL32(221, 219, 207, 30), 1.0f);
    }

    // The bar: what is picked, and what can be done to it.
    bool open = true;
    TouchPlace& tp = s.places[size_t(picked)];
    const float bw = ui::px(96), bh = ui::px(34), gap = ui::px(8);
    const float bar_w = bw * 5 + gap * 4;
    float bx = W * 0.5f - bar_w * 0.5f;
    const float by = H * 0.5f - bh * 0.5f;
    const std::string title = eng::str::format("%s: %d%%", touch_part_label(TouchPart(picked)), int(std::lround(tp.scale * 100)));
    if (VanFont* f = ui::font_bold()) {
        const float size = ui::px(15);
        const VanVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0, title.c_str());
        dl->AddText(f, size, {W * 0.5f - ts.x * 0.5f, by - ts.y - ui::px(8)}, VAN_COL32(236, 214, 132, 255), title.c_str());
        const char* hint = "Drag a control to move it. Pick one, then make it smaller or bigger.";
        const VanVec2 hs = f->CalcTextSizeA(ui::px(12), FLT_MAX, 0, hint);
        dl->AddText(f, ui::px(12), {W * 0.5f - hs.x * 0.5f, by + bh + ui::px(10)}, VAN_COL32(221, 219, 207, 220), hint);
    }
    auto bar_button = [&](const char* label) {
        VanGui::SetCursorScreenPos({bx, by});
        bx += bw + gap;
        return ui::button(label, bw / ui::px(1), bh / ui::px(1));
    };
    if (bar_button("Smaller")) tp.scale = std::max(0.5f, std::round((tp.scale - 0.1f) * 10) / 10);
    if (bar_button("Bigger")) tp.scale = std::min(2.0f, std::round((tp.scale + 0.1f) * 10) / 10);
    if (bar_button("Reset")) tp = TouchPlace{};
    if (bar_button("Reset all")) s.places = {};
    if (bar_button("Done") || VanGui::IsKeyPressed(VanGuiKey_Escape)) open = false;

    // Dragging: a part taken where the pointer came down (the last drawn, on top, first), moved as
    // the pointer moves, kept whole on the screen.
    const Layout L = lay_out(sc, s, true, true);
    if (io.MouseClicked[0] && !VanGui::IsAnyItemHovered()) {
        dragging = -1;
        for (int p = int(kTouchParts) - 1; p >= 0; --p) {
            const Place& pl = L[size_t(p)];
            if (pl.visible && std::hypot(io.MousePos.x - pl.x, io.MousePos.y - pl.y) <= pl.r * 1.1f) {
                dragging = picked = p;
                grip = {pl.x - io.MousePos.x, pl.y - io.MousePos.y};
                break;
            }
        }
    }
    if (dragging >= 0 && io.MouseDown[0]) {
        const Place& pl = L[size_t(dragging)];
        const float x = std::clamp(io.MousePos.x + grip.x, pl.r, W - pl.r), y = std::clamp(io.MousePos.y + grip.y, pl.r, H - pl.r);
        TouchPlace& moved = s.places[size_t(dragging)];
        moved.x = x / W, moved.y = y / H;
    }
    if (!io.MouseDown[0]) dragging = -1;

    // The controls, where they are now; the one picked ringed in gold.
    const Layout now = lay_out(sc, s, true, true);
    const Colours c = colours(std::max(0.8f, s.opacity));
    const Place& st = now[size_t(kStick)];
    draw_stick(dl, st.x, st.y, st.r, st.x, st.y, false, false, c, unit);
    for (int b = 0; b < kButtons; ++b)
        if (now[size_t(b)].visible) draw_button(dl, now[size_t(b)], kInfo[b].label, b == dragging, b == int(TouchPart::Fire), c, unit);
    if (VanFont* f = ui::font_bold(); f && now[size_t(kStick)].visible) {
        const VanVec2 ts = f->CalcTextSizeA(16 * unit, FLT_MAX, 0, "STICK");
        dl->AddText(f, 16 * unit, {st.x - ts.x * 0.5f, st.y + st.r * 0.55f}, c.ink, "STICK");
    }
    if (const Place& pk = now[size_t(picked)]; pk.visible) dl->AddCircle({pk.x, pk.y}, pk.r + 5 * unit, VAN_COL32(236, 214, 132, 255), 48, 3.0f * unit);
    return open;
}

}  // namespace lsf
