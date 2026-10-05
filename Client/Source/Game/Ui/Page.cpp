#include "Game/Ui/Page.hpp"

#include "Game/Ui/Kit.hpp"

#include "Game/Items.hpp"

#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/misc/vangui_anim.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace lsf::ui {

namespace {

float g_sx = 1, g_sy = 1;
float g_px = 0, g_py = 0;   // the page's corner in the window (the picture's: past it are the bars)

// The page grid over the picture.
void place_page() {
    const ViewRect v = view();
    g_sx = v.w / kPageW;
    g_sy = v.h / kPageH;
    g_px = v.x;
    g_py = v.y;
}

// The pointer, on the page grid.
VanVec2 page_mouse() {
    const VanVec2 m = VanGui::GetIO().MousePos;
    return {(m.x - g_px) / g_sx, (m.y - g_py) / g_sy};
}

// What the scripts leave to the engine, measured off screenshots of the English client.
constexpr VanU32 kOutline = VAN_COL32(80, 80, 78, 255);     // a *LINEFRAME: grey, whatever the script says
constexpr VanU32 kHeader = VAN_COL32(34, 34, 32, 235);      // a list's title bar
constexpr VanU32 kDivider = VAN_COL32(96, 96, 92, 255);     // between its titles
constexpr VanU32 kTrack = VAN_COL32(14, 14, 12, 200);       // a scroll track
constexpr VanU32 kThumb = VAN_COL32(92, 92, 86, 255);
constexpr VanU32 kWell = VAN_COL32(0, 0, 0, 165);           // under a list or a chat log
constexpr float kText = 12.0f;                              // page units
constexpr float kHeaderH = 23.0f, kRowH = 21.0f, kScrollW = 19.0f;

std::vector<int> ints(const sf::PageNode& n, std::string_view key) {
    std::vector<int> out;
    auto it = n.props.find(std::string(key));
    if (it == n.props.end()) return out;
    for (const std::string& v : it->second) out.push_back(std::atoi(v.c_str()));
    return out;
}

// "a r g b", as the scripts write a colour.
std::optional<VanU32> argb(const sf::PageNode& n, std::string_view key) {
    const auto v = ints(n, key);
    if (v.size() < 4) return std::nullopt;
    return VAN_COL32(std::clamp(v[1], 0, 255), std::clamp(v[2], 0, 255), std::clamp(v[3], 0, 255), std::clamp(v[0], 0, 255));
}

bool node_rect(const sf::PageNode& n, float& x0, float& y0, float& x1, float& y1) {
    int a, b, c, d;
    if (!n.rect(a, b, c, d)) return false;
    x0 = float(a), y0 = float(b), x1 = float(c), y1 = float(d);
    return x1 > x0 && x1 > 0 && y1 > 0;   // hidden elements sit at negative rects
}

// How many states a sprite stacks down itself for an element `h` high (a button's normal /
// hover / pressed / selected, a tab's normal / selected).
std::string_view kit_name(std::string_view name);

int states_of(std::string_view name, float h) {
    Atlas* a = atlas();
    if (!a || h <= 0) return 1;
    const sf::Sprite* s = a->table().find(kit_name(name));
    if (!s) return 1;
    return std::max(1, int(std::lround(float(s->height()) / h)));
}

// A few of the newest scripts still name a 2004-kit sprite where the English client showed the
// 2010 kit's (the lobby's Friend tab, the room's Ready): that one, when the kit has it.
std::string_view kit_name(std::string_view name) {
    static const std::pair<std::string_view, std::string_view> kNewer[] = {{"friend_off", "friend_on_off_1"}, {"ready", "ready_1"}};
    for (const auto& [old_name, newer] : kNewer)
        if (name == old_name && atlas() && atlas()->table().find(newer)) return newer;
    return name;
}

void draw_sprite(VanDrawList* dl, std::string_view name, int state, int states, float x0, float y0, float x1, float y1, VanU32 tint = 0xFFFFFFFF) {
    Atlas* a = atlas();
    if (!a) return;
    name = kit_name(name);
    // The kit redrawn at the screen's own size (Ui/Kit.hpp), where it has a drawing of the piece.
    if (kit_draw(dl, name, state, states, pg(x0, y0), pg(x1, y1), tint, g_sy)) return;
    const SpriteRef s = a->sprite(name, state, states);
    if (s.valid()) dl->AddImage(VanTextureRef(VanTextureID(s.tex)), pg(x0, y0), pg(x1, y1), s.uv0, s.uv1, tint);
}

VanFont* face(bool bold) { return bold ? font_page_bold() : font_page(); }

// A *TEXT is written from the top of its rect (the scripts' rects are often taller than a line —
// "Total SP:" sits in one 69 high inside a box 22 high); list cells and captions are centred.
void put_text(VanDrawList* dl, float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour, Align align, bool bold, float size,
              bool clip = true, bool top = false) {
    if (s.empty()) return;
    VanFont* f = face(bold);
    const float px_size = std::max(6.0f, size * g_sy);
    const VanVec2 ts = f->CalcTextSizeA(px_size, 1e9f, 0.0f, s.data(), s.data() + s.size());
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    float x = a.x + 2 * g_sx;
    if (align == Align::Center) x = (a.x + b.x - ts.x) * 0.5f;
    if (align == Align::Right) x = b.x - ts.x - 2 * g_sx;
    const float y = top ? a.y + 2 * g_sy : (a.y + b.y - ts.y) * 0.5f;
    if (clip) dl->PushClipRect(a, b, true);
    dl->AddText(f, px_size, {std::floor(x) + 1, std::floor(y) + 1}, VAN_COL32(0, 0, 0, (colour >> 24) * 2 / 3), s.data(), s.data() + s.size());
    dl->AddText(f, px_size, {std::floor(x), std::floor(y)}, colour, s.data(), s.data() + s.size());
    if (clip) dl->PopClipRect();
}

void outline(VanDrawList* dl, float x0, float y0, float x1, float y1, VanU32 c) {
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    dl->AddRect({std::floor(a.x) + 0.5f, std::floor(a.y) + 0.5f}, {std::floor(b.x) - 0.5f, std::floor(b.y) - 0.5f}, c, 0, 0, 1.0f);
}

// A spot for the host window's next control, `id`-scoped.
bool hit(const char* tag, int id, float x0, float y0, float x1, float y1, bool* hovered = nullptr, bool* held = nullptr) {
    VanGui::SetCursorScreenPos(pg(x0, y0));
    VanGui::PushID(id);
    const VanVec2 size{std::max(1.0f, pgx(x1 - x0)), std::max(1.0f, pgy(y1 - y0))};
    const bool clicked = VanGui::InvisibleButton(tag, size);
    if (hovered) *hovered = VanGui::IsItemHovered();
    if (held) *held = VanGui::IsItemActive();
    VanGui::PopID();
    return clicked;
}

void draw_node(VanDrawList* dl, const sf::PageNode& n, std::initializer_list<int> skip) {
    const int id = n.id();
    const bool skipped = id != 0 && std::find(skip.begin(), skip.end(), id) != skip.end();
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const bool placed = node_rect(n, x0, y0, x1, y1);
    const std::string tex = n.text("UNITTEX");
    if (!skipped && placed) {
        if (n.kind == "IMAGE") {
            if (!tex.empty()) draw_sprite(dl, tex, 0, 1, x0, y0, x1, y1);
            else if (auto c = argb(n, "BACKCOLOR")) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), *c);
        } else if (n.kind == "LINEFRAME") {
            outline(dl, x0, y0, x1, y1, kOutline);
        } else if (n.kind == "BUTTON" || n.kind == "TAB" || n.kind == "BUTTONTAB" || n.kind == "LEFT" || n.kind == "RIGHT") {
            if (!tex.empty()) draw_sprite(dl, tex, 0, states_of(tex, y1 - y0), x0, y0, x1, y1);
        } else if (n.kind == "TEXT") {
            const std::string label = n.text("TEXT");
            // SP is the only currency here: the pages' eCoin, G Coin and cash labels are not drawn.
            const bool currency = label.find("Coin") != std::string::npos || label.find("CASH") != std::string::npos;
            if (!label.empty() && y1 > y0 && !currency)
                put_text(dl, x0, y0, x1, y1, label, argb(n, "FONTCOLOR").value_or(0xFFFFFFFF), Align::Left, true, kText, false, true);
        } else if (n.kind == "HORIZBAR") {
            const float bw = float(n.number("BTN_WIDTH", 14)), bh = float(n.number("BTN_HEIGHT", 16));
            const float cy = (y0 + y1) * 0.5f;
            const std::string l = n.text("LEFTUT"), r = n.text("RIGHTUT");
            draw_sprite(dl, l, 0, states_of(l, bh), x0, cy - bh * 0.5f, x0 + bw, cy + bh * 0.5f);
            draw_sprite(dl, r, 0, states_of(r, bh), x1 - bw, cy - bh * 0.5f, x1, cy + bh * 0.5f);
        } else if (n.kind == "VALUEBAR") {
            const std::string bottom = n.text("BOTTOMUT");
            draw_sprite(dl, bottom, 0, 1, x0, y0, x1, y1);
        } else if (n.kind == "FLOWTEXT") {
            // Drawn here with the script's own words unless the screen skips it for marquee().
        }
    }
    for (const sf::PageNode& c : n.children) draw_node(dl, c, skip);
}

void flow(VanDrawList* dl, float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour) {
    if (s.empty()) return;
    VanFont* f = face(true);
    const float px_size = kText * g_sy;
    const VanVec2 ts = f->CalcTextSizeA(px_size, 1e9f, 0.0f, s.data(), s.data() + s.size());
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    const float span = (b.x - a.x) + ts.x;
    const float offset = std::fmod(float(now()) * 60.0f * g_sx, span);
    dl->PushClipRect(a, b, true);
    dl->AddText(f, px_size, {std::floor(b.x - offset), std::floor((a.y + b.y - ts.y) * 0.5f)}, colour, s.data(), s.data() + s.size());
    dl->PopClipRect();
    VanGui::Anim::KeepAnimating();
}

}  // namespace

// ── Frame ──────────────────────────────────────────────────────────────────────

VanVec2 pg(float x, float y) { return {g_px + x * g_sx, g_py + y * g_sy}; }
float pgx(float w) { return w * g_sx; }
float pgy(float h) { return h * g_sy; }

void page_begin(const char* id) {
    const VanVec2 ds = VanGui::GetIO().DisplaySize;
    place_page();
    VanGui::SetNextWindowPos({0, 0});
    VanGui::SetNextWindowSize(ds);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    VanGui::PushStyleVar(VanGuiStyleVar_ItemSpacing, {0, 0});
    (void)VanGui::Begin(id, nullptr,
                        VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoMove | VanGuiWindowFlags_NoSavedSettings |
                            VanGuiWindowFlags_NoBringToFrontOnFocus | VanGuiWindowFlags_NoScrollWithMouse | VanGuiWindowFlags_NoNav);
}

void page_end() {
    VanGui::End();
    VanGui::PopStyleVar(3);
}

// ── Page ───────────────────────────────────────────────────────────────────────

bool Page::load(const sf::Data& data, std::string_view name) {
    auto page = sf::load_page(data, name);
    loaded_ = page.has_value();
    if (page) root_ = std::move(*page);
    return loaded_;
}

bool Page::rect(int id, float& x0, float& y0, float& x1, float& y1, std::string_view kind) const {
    const sf::PageNode* n = find(id, kind);
    return n && node_rect(*n, x0, y0, x1, y1);
}

// ── Drawing ────────────────────────────────────────────────────────────────────

void draw_static(const Page& page, std::initializer_list<int> skip) {
    if (!page.loaded()) return;
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    draw_node(dl, page.root(), skip);
    // The marquees, with the script's own message, where the screen did not take them over.
    std::vector<const sf::PageNode*> flows;
    page.root().collect("FLOWTEXT", flows);
    for (const sf::PageNode* f : flows) {
        if (std::find(skip.begin(), skip.end(), f->id()) != skip.end()) continue;
        float x0, y0, x1, y1;
        if (node_rect(*f, x0, y0, x1, y1)) flow(dl, x0, y0, x1, y1, f->text("MSG"), argb(*f, "COLOR").value_or(0xFF4A4AFB));
    }
}

bool button(const Page& page, int id, bool enabled, const char* sprite, const char* tooltip, bool latched) {
    const sf::PageNode* n = page.find(id, "BUTTON");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return false;
    const std::string name = sprite ? std::string(sprite) : n->text("UNITTEX");
    const int states = states_of(name, y1 - y0);
    bool hovered = false, held = false;
    const bool clicked = hit("##button", id, x0, y0, x1, y1, &hovered, &held);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    int state = 0;
    if (enabled && held && hovered) state = std::min(2, states - 1);
    else if (enabled && hovered) state = std::min(1, states - 1);
    if (latched) state = std::min(2, states - 1);   // held down: Ready while you are
    if (state != 0 || sprite) draw_sprite(dl, name, state, states, x0, y0, x1, y1);
    if (!enabled) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 120));
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) click_sound();
    return clicked && enabled;
}

// The 2010 kit's tab plate without its word: an olive gradient in a one-pixel frame, the frame
// lime when chosen (measured off tab_sub_primary_1).
void tab_plate(VanDrawList* dl, float x0, float y0, float x1, float y1, std::string_view label, bool chosen, bool hovered) {
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    if (kit_sharp()) {
        kit_plate(dl, a, b, g_sy, label, chosen, hovered);
        return;
    }
    dl->AddRectFilledMultiColor(a, b, VAN_COL32(58, 54, 38, 255), VAN_COL32(58, 54, 38, 255), VAN_COL32(70, 68, 47, 255), VAN_COL32(70, 68, 47, 255));
    const VanU32 frame = chosen ? VAN_COL32(202, 228, 80, 255) : hovered ? VAN_COL32(202, 228, 80, 130) : VAN_COL32(67, 69, 62, 255);
    dl->AddRect({std::floor(a.x) + 0.5f, std::floor(a.y) + 0.5f}, {std::floor(b.x) - 0.5f, std::floor(b.y) - 0.5f}, frame, 0, 0, chosen ? 2.0f : 1.0f);
    const VanU32 ink = chosen ? VAN_COL32(217, 246, 94, 255) : hovered ? VAN_COL32(232, 232, 226, 255) : VAN_COL32(202, 202, 198, 255);
    put_text(dl, x0, y0, x1, y1, label, ink, Align::Center, true, 13.0f);
}

int tabs(const Page& page, int ctrl_id, int selected, const std::function<bool(int)>& enabled, std::span<const std::string> labels) {
    const sf::PageNode* ctrl = page.find(ctrl_id, "TABCTRL");
    if (!ctrl) ctrl = page.find(ctrl_id, "BTNTABCTRL");
    if (!ctrl) return -1;
    int clicked_id = -1;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    VanGui::PushID(ctrl_id);
    size_t order = 0;
    for (const sf::PageNode& t : ctrl->children) {
        float x0, y0, x1, y1;
        if ((t.kind != "TAB" && t.kind != "BUTTONTAB") || !node_rect(t, x0, y0, x1, y1)) continue;
        const size_t index = order++;
        const int id = t.id();
        const bool on = !enabled || enabled(id);
        const std::string name = t.text("UNITTEX");
        const int states = states_of(name, y1 - y0);
        bool hovered = false, held = false;
        if (hit("##tab", id, x0, y0, x1, y1, &hovered, &held) && on) clicked_id = id;
        if (index < labels.size() && !labels[index].empty()) {
            tab_plate(dl, x0, y0, x1, y1, labels[index], id == selected, on && hovered);
            if (!on) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 120));
            continue;
        }
        int state = 0;
        if (id == selected) state = states - 1;                      // the last state: chosen
        else if (on && held && hovered) state = std::min(2, states - 1);
        else if (on && hovered) state = std::min(1, states - 1);
        if (state != 0) {
            // A two-state tab has no hover of its own: its chosen look, fainter.
            const bool faint = states == 2 && id != selected;
            draw_sprite(dl, name, state, states, x0, y0, x1, y1, faint ? VAN_COL32(255, 255, 255, 150) : 0xFFFFFFFF);
        }
        if (!on) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 120));
    }
    VanGui::PopID();
    if (clicked_id >= 0 && clicked_id != selected) click_sound();
    return clicked_id;
}

bool toggle(const Page& page, int id, bool second_state) {
    const sf::PageNode* n = page.find(id, "TAB");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return false;
    const std::string name = n->text("UNITTEX");
    const int states = states_of(name, y1 - y0);
    bool hovered = false;
    const bool clicked = hit("##toggle", id, x0, y0, x1, y1, &hovered);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    if (second_state) draw_sprite(dl, name, std::min(1, states - 1), states, x0, y0, x1, y1);
    if (hovered) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(255, 255, 255, 18));
    if (clicked) click_sound();
    return clicked;
}

int selector(const Page& page, int id, std::string_view label, bool enabled) {
    const sf::PageNode* n = page.find(id, "HORIZBAR");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return 0;
    const float bw = float(n->number("BTN_WIDTH", 14)), bh = float(n->number("BTN_HEIGHT", 16));
    const float cy = (y0 + y1) * 0.5f;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    int result = 0;
    for (int side = 0; side < 2; ++side) {
        const std::string name = n->text(side == 0 ? "LEFTUT" : "RIGHTUT");
        const float ax0 = side == 0 ? x0 : x1 - bw;
        const int states = states_of(name, bh);
        bool hovered = false, held = false;
        if (hit(side == 0 ? "##left" : "##right", id, ax0, cy - bh * 0.5f, ax0 + bw, cy + bh * 0.5f, &hovered, &held) && enabled) result = side == 0 ? -1 : 1;
        const int state = !enabled ? 0 : held && hovered ? std::min(2, states - 1) : hovered ? std::min(1, states - 1) : 0;
        if (state != 0) draw_sprite(dl, name, state, states, ax0, cy - bh * 0.5f, ax0 + bw, cy + bh * 0.5f);
    }
    put_text(dl, x0 + bw, y0, x1 - bw, y1, label, enabled ? 0xFFFFFFFF : VAN_COL32(150, 150, 146, 255), Align::Center, true, kText);
    if (result != 0) click_sound();
    return result;
}

void meter(const Page& page, int id, float fraction, VanU32 tint) {
    const sf::PageNode* n = page.find(id, "VALUEBAR");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return;
    Atlas* a = atlas();
    if (!a) return;
    const SpriteRef bar = a->sprite(n->text("BARUT"));
    if (!bar.valid()) return;
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    const VanVec2 uv1{bar.uv0.x + (bar.uv1.x - bar.uv0.x) * f, bar.uv1.y};
    VanGui::GetWindowDrawList()->AddImage(VanTextureRef(VanTextureID(bar.tex)), pg(x0, y0), pg(x0 + (x1 - x0) * f, y1), bar.uv0, uv1, tint);
}

void meter_at(float x0, float y0, float x1, float y1, float fraction, VanU32 tint) {
    Atlas* a = atlas();
    if (!a) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    if (const SpriteRef bottom = a->sprite("value_bottom_1"); bottom.valid())
        dl->AddImage(VanTextureRef(VanTextureID(bottom.tex)), pg(x0, y0), pg(x1, y1), bottom.uv0, bottom.uv1);
    else dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 200));
    const SpriteRef bar = a->sprite("durability_bar_1");
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (!bar.valid() || f <= 0) return;
    const VanVec2 uv1{bar.uv0.x + (bar.uv1.x - bar.uv0.x) * f, bar.uv1.y};
    dl->AddImage(VanTextureRef(VanTextureID(bar.tex)), pg(x0, y0), pg(x0 + (x1 - x0) * f, y1), bar.uv0, uv1, tint);
}

void picture(const Page& page, int id, const Picture& pic, bool keep_aspect) {
    const sf::PageNode* n = page.find(id, "IMAGE");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1) || !pic.valid()) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    if (keep_aspect && pic.w > 0 && pic.h > 0) {
        const float s = std::min((x1 - x0) / pic.w, (y1 - y0) / pic.h);
        const float w = pic.w * s, h = pic.h * s;
        const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    }
    dl->AddImage(VanTextureRef(VanTextureID(pic.tex)), pg(x0, y0), pg(x1, y1));
}

void text(const Page& page, int id, std::string_view s, VanU32 colour, Align align, bool bold) {
    const sf::PageNode* n = page.find(id, "TEXT");
    int a, b, c, d;
    if (!n || !n->rect(a, b, c, d)) return;
    // Some of the scripts' text rects are written bottom-up; a line is 18 high.
    float x0 = float(a), y0 = float(std::min(b, d)), x1 = float(c), y1 = float(std::max(b, d));
    if (y1 - y0 < 12) y1 = y0 + 18;
    put_text(VanGui::GetWindowDrawList(), x0, y0, x1, y1, s, colour, align, bold, kText, align != Align::Left, true);
}

void paragraph(const Page& page, int id, std::string_view s, VanU32 colour) {
    const sf::PageNode* n = page.find(id, "TEXT");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1) || s.empty()) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float px_size = kText * g_sy;
    dl->PushClipRect(pg(x0, y0), pg(x1, y1), true);
    dl->AddText(font_page_bold(), px_size, pg(x0, y0), colour, s.data(), s.data() + s.size(), pgx(x1 - x0));
    dl->PopClipRect();
}

void text_at(float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour, Align align, bool bold, float size) {
    put_text(VanGui::GetWindowDrawList(), x0, y0, x1, y1, s, colour, align, bold, size);
}

void text_wrapped(float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour, float size) {
    if (s.empty()) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    VanFont* f = face(true);
    const float px_size = std::max(6.0f, size * g_sy);
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    dl->PushClipRect(a, b, true);
    dl->AddText(f, px_size, {std::floor(a.x + 2 * g_sx), std::floor(a.y + g_sy)}, colour, s.data(), s.data() + s.size(), b.x - a.x - 4 * g_sx);
    dl->PopClipRect();
}

namespace {
std::string* g_tip_sink = nullptr;
}

void set_tip_sink(std::string* sink) { g_tip_sink = sink; }

void tip(const char* text) {
    if (!text || !*text) return;
    if (g_tip_sink) *g_tip_sink = text;
    else VanGui::SetTooltip("%s", text);
}

VanU32 name_ink(u8 colour) {
    if (colour == 0 || colour >= u8(kNameColourCount)) return VAN_COL32(230, 230, 230, 255);
    const u32 c = kNameColours[colour];
    return VAN_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
}

void glow_text(VanDrawList* dl, VanFont* f, float px_size, VanVec2 at, std::string_view s, u8 colour, VanU32 plain) {
    if (colour == 0 || colour >= u8(kNameColourCount)) {
        dl->AddText(f, px_size, {at.x + 1, at.y + 1}, VAN_COL32(0, 0, 0, (plain >> 24) * 2 / 3), s.data(), s.data() + s.size());
        dl->AddText(f, px_size, at, plain, s.data(), s.data() + s.size());
        return;
    }
    // A coloured name glows: its colour a few times over, soft and a pixel or two out, under a
    // dark keyline and the name itself.
    const VanU32 ink = (name_ink(colour) & 0x00FFFFFFu) | (plain & 0xFF000000u);
    const VanU32 halo = (ink & 0x00FFFFFFu) | (u32((plain >> 24) * 60 / 255) << 24);
    const float r = std::max(1.0f, px_size * 0.09f);
    for (float dy = -r; dy <= r; dy += r)
        for (float dx = -r; dx <= r; dx += r)
            if (dx != 0 || dy != 0) dl->AddText(f, px_size, {at.x + dx, at.y + dy}, halo, s.data(), s.data() + s.size());
    dl->AddText(f, px_size, {at.x + 1, at.y + 1}, VAN_COL32(0, 0, 0, (plain >> 24) * 2 / 3), s.data(), s.data() + s.size());
    dl->AddText(f, px_size, at, ink, s.data(), s.data() + s.size());
}

void name_text_at(float x0, float y0, float x1, float y1, std::string_view name, u8 colour, Align align, float size) {
    if (name.empty()) return;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    VanFont* f = face(true);
    const float px_size = std::max(6.0f, size * g_sy);
    const VanVec2 ts = f->CalcTextSizeA(px_size, 1e9f, 0.0f, name.data(), name.data() + name.size());
    const VanVec2 a = pg(x0, y0), b = pg(x1, y1);
    float x = a.x + 2 * g_sx;
    if (align == Align::Center) x = (a.x + b.x - ts.x) * 0.5f;
    if (align == Align::Right) x = b.x - ts.x - 2 * g_sx;
    dl->PushClipRect(a, b, true);
    glow_text(dl, f, px_size, {std::floor(x), std::floor((a.y + b.y - ts.y) * 0.5f)}, name, colour, VAN_COL32(230, 230, 230, 255));
    dl->PopClipRect();
}

void page_background(std::string_view sprite) { draw_sprite(VanGui::GetBackgroundDrawList(), sprite, 0, 1, 0, 0, kPageW, kPageH); }

void sprite_at(std::string_view name, int state, int states, float x0, float y0, float x1, float y1, VanU32 tint) {
    draw_sprite(VanGui::GetWindowDrawList(), name, state, states, x0, y0, x1, y1, tint);
}

void picture_at(const Picture& pic, float x0, float y0, float x1, float y1, VanU32 tint, bool keep_aspect) {
    if (!pic.valid()) return;
    if (keep_aspect && pic.w > 0 && pic.h > 0) {
        const float s = std::min((x1 - x0) / pic.w, (y1 - y0) / pic.h);
        const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
        x0 = cx - pic.w * s * 0.5f, x1 = cx + pic.w * s * 0.5f, y0 = cy - pic.h * s * 0.5f, y1 = cy + pic.h * s * 0.5f;
    }
    VanGui::GetWindowDrawList()->AddImage(VanTextureRef(VanTextureID(pic.tex)), pg(x0, y0), pg(x1, y1), {0, 0}, {1, 1}, tint);
}

void fill_at(float x0, float y0, float x1, float y1, VanU32 colour) { VanGui::GetWindowDrawList()->AddRectFilled(pg(x0, y0), pg(x1, y1), colour); }

bool region(int id, float x0, float y0, float x1, float y1, bool* hovered) { return hit("##region", id, x0, y0, x1, y1, hovered); }

void marquee(const Page& page, int id, std::string_view message) {
    const sf::PageNode* n = page.find(id, "FLOWTEXT");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return;
    flow(VanGui::GetWindowDrawList(), x0, y0, x1, y1, message.empty() ? std::string_view(n->text("MSG")) : message,
         argb(*n, "COLOR").value_or(0xFF4A4AFB));
}

bool edit(const Page& page, int id, std::string& value, const char* hint, bool password) {
    const sf::PageNode* n = page.find(id, "EDIT");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return false;
    return edit_at(id, x0, y0, x1, y1, value, std::clamp(n->number("LIMITTEXT", 64), 1, 250), hint, password, true,
                   argb(*n, "BACKCOLOR").value_or(VAN_COL32(0, 0, 0, 170)), argb(*n, "FONTCOLOR").value_or(0xFFFFFFFF));
}

bool edit_at(int key, float x0, float y0, float x1, float y1, std::string& value, int limit, const char* hint, bool password, bool enabled, VanU32 back,
             VanU32 ink) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), back);
    outline(dl, x0, y0, x1, y1, kOutline);
    limit = std::clamp(limit, 1, 250);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    buf[std::min<size_t>(size_t(limit), sizeof(buf) - 1)] = '\0';
    const float px_size = kText * g_sy;
    const float scale = std::max(0.01f, VanGui::GetStyle().FontScaleMain);
    VanGui::PushFont(font_page_bold(), px_size / scale);
    VanGui::PushStyleVar(VanGuiStyleVar_FramePadding, {4 * g_sx, std::max(0.0f, (pgy(y1 - y0) - px_size) * 0.5f)});
    VanGui::PushStyleVar(VanGuiStyleVar_FrameBorderSize, 0.0f);
    VanGui::PushStyleColor(VanGuiCol_FrameBg, VAN_COL32(0, 0, 0, 0));
    VanGui::PushStyleColor(VanGuiCol_FrameBgHovered, VAN_COL32(255, 255, 255, 10));
    VanGui::PushStyleColor(VanGuiCol_FrameBgActive, VAN_COL32(255, 255, 255, 14));
    VanGui::PushStyleColor(VanGuiCol_Text, ink);
    VanGui::SetCursorScreenPos(pg(x0, y0));
    VanGui::SetNextItemWidth(pgx(x1 - x0));
    VanGui::PushID(key);
    VanGuiInputTextFlags flags = VanGuiInputTextFlags_EnterReturnsTrue;
    if (password) flags |= VanGuiInputTextFlags_Password;
    VanGui::BeginDisabled(!enabled);
    const bool enter = VanGui::InputTextWithHint("##edit", hint ? hint : "", buf, size_t(limit) + 1, flags);
    phone_keyboard(buf, limit);
    VanGui::EndDisabled();
    VanGui::PopID();
    VanGui::PopStyleColor(4);
    VanGui::PopStyleVar(2);
    VanGui::PopFont();
    if (!enabled) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 110));
    value = buf;
    return enter && enabled;
}

bool combo(const Page& page, int id, int& selected, std::span<const std::string> items) {
    const sf::PageNode* n = page.find(id, "COMBOBOX");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1) || items.empty()) return false;
    selected = std::clamp(selected, 0, int(items.size()) - 1);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float arrow = (y1 - y0);
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(22, 22, 20, 235));
    outline(dl, x0, y0, x1, y1, kOutline);
    // The drop button: the lobby's own scroll arrow.
    const bool up = n->text("STYLE") == "up";
    draw_sprite(dl, up ? "scroll_up_button_1" : "scroll_down_button_1", 0, 2, x1 - arrow, y0 + 2, x1 - 2, y1 - 2);
    put_text(dl, x0 + 2, y0, x1 - arrow, y1, items[size_t(selected)], VAN_COL32(200, 200, 196, 255), Align::Left, true, kText);
    VanGui::PushID(id);
    const bool open = hit("##combo", id, x0, y0, x1, y1);
    const std::string popup = "##combo_popup";
    if (open) {
        click_sound();
        VanGui::OpenPopup(popup.c_str());
    }
    bool changed = false;
    const float row = 20.0f;
    const float list_h = row * float(items.size());
    VanGui::SetNextWindowPos(pg(x0, up ? y0 - list_h : y1));
    VanGui::SetNextWindowSize({pgx(x1 - x0), pgy(list_h)});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    VanGui::PushStyleColor(VanGuiCol_PopupBg, VAN_COL32(18, 18, 16, 245));
    if (VanGui::BeginPopup(popup.c_str(), VanGuiWindowFlags_NoMove)) {
        VanDrawList* pdl = VanGui::GetWindowDrawList();
        const float base = up ? y0 - list_h : y1;
        for (size_t i = 0; i < items.size(); ++i) {
            const float ry = base + row * float(i);
            bool hovered = false;
            if (hit("##item", int(i), x0, ry, x1, ry + row, &hovered)) {
                selected = int(i);
                changed = true;
                click_sound();
                VanGui::CloseCurrentPopup();
            }
            if (hovered) pdl->AddRectFilled(pg(x0, ry), pg(x1, ry + row), VAN_COL32(255, 255, 255, 24));
            put_text(pdl, x0 + 2, ry, x1, ry + row, items[i], int(i) == selected ? VAN_COL32(224, 221, 94, 255) : 0xFFE6E6E6, Align::Left, true, kText);
        }
        outline(pdl, x0, base, x1, base + list_h, kOutline);
        VanGui::EndPopup();
    }
    VanGui::PopStyleColor();
    VanGui::PopStyleVar();
    VanGui::PopID();
    return changed;
}

void text_log(const Page& page, int id, std::span<const Line> lines) {
    const sf::PageNode* n = page.find(id, "TEXT");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return;
    VanGui::GetWindowDrawList()->AddRectFilled(pg(x0, y0), pg(x1, y1), kWell);
    VanGui::SetCursorScreenPos(pg(x0, y0));
    VanGui::PushID(id);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {3 * g_sx, 2 * g_sy});
    VanGui::PushStyleVar(VanGuiStyleVar_ScrollbarSize, 10 * g_sx);
    VanGui::PushStyleColor(VanGuiCol_ScrollbarBg, kTrack);
    VanGui::PushStyleColor(VanGuiCol_ScrollbarGrab, kThumb);
    if (VanGui::BeginChild("##log", {pgx(x1 - x0), pgy(y1 - y0)}, 0, VanGuiWindowFlags_NoBackground)) {
        const float px_size = kText * g_sy;
        const float scale = std::max(0.01f, VanGui::GetStyle().FontScaleMain);
        VanGui::PushFont(font_page_bold(), px_size / scale);
        VanGui::PushTextWrapPos(0);
        for (const Line& l : lines) {
            VanGui::PushStyleColor(VanGuiCol_Text, l.colour);
            VanGui::TextUnformatted(l.text.c_str());
            VanGui::PopStyleColor();
        }
        VanGui::PopTextWrapPos();
        VanGui::PopFont();
        if (VanGui::GetScrollY() >= VanGui::GetScrollMaxY() - 4) VanGui::SetScrollHereY(1.0f);
    }
    VanGui::EndChild();
    VanGui::PopStyleColor(2);
    VanGui::PopStyleVar(2);
    VanGui::PopID();
}

int card_grid(const Page& page, int id, int count, int selected, const std::function<void(const Card&)>& fill) {
    // By kind: a page's tabs reuse small ids (PageCharacter's Accessory tab is also 7).
    const sf::PageNode* n = page.find(id, "BTNON3BTNCTRL");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return -1;
    constexpr float kCardW = 203, kCardH = 144, kPitchX = 213, kPitchY = 152, kMargin = 2;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), kWell);
    static std::map<const void*, float> scrolled;
    float& top = scrolled[static_cast<const void*>(n)];
    const int rows = (count + 2) / 3;
    const float view_h = y1 - y0 - kMargin;
    const float most = std::max(0.0f, float(rows) * kPitchY - view_h);
    // The wheel anywhere over the grid.
    const VanVec2 m = VanGui::GetIO().MousePos;
    if (VanGui::IsWindowHovered() && m.x >= pg(x0, y0).x && m.x < pg(x1, y1).x && m.y >= pg(x0, y0).y && m.y < pg(x1, y1).y) {
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) top -= wheel * kPitchY * 0.5f;
    }
    top = std::clamp(top, 0.0f, most);
    int clicked = -1;
    dl->PushClipRect(pg(x0, y0), pg(x1, y1), true);
    for (int i = 0; i < count; ++i) {
        const float cx = x0 + kMargin + float(i % 3) * kPitchX;
        const float cy = y0 + kMargin + float(i / 3) * kPitchY - top;
        if (cy + kCardH < y0 || cy > y1) continue;
        // Clipped to the grid, so a half-scrolled card cannot be clicked outside it.
        const float hy0 = std::max(cy, y0), hy1 = std::min(cy + kCardH, y1);
        bool hovered = false;
        VanGui::SetNextItemAllowOverlap();
        if (hit("##card", id * 1000 + i, cx, hy0, cx + kCardW, hy1, &hovered)) {
            clicked = i;
            click_sound();
        }
        const bool chosen = i == selected;
        draw_sprite(dl, "item_bottom_1", chosen ? 2 : hovered ? 1 : 0, 3, cx, cy, cx + kCardW, cy + kCardH);
        fill({cx, cy, cx + kCardW, cy + kCardH, i, hovered, chosen});
    }
    dl->PopClipRect();
    // The scroll strip, just outside the grid's right edge.
    const float sx0 = x1 + 3, sx1 = x1 + 22, ah = 20;
    bool up_h = false, dn_h = false;
    if (hit("##gridup", id, sx0, y0, sx1, y0 + ah, &up_h)) top = std::max(0.0f, top - kPitchY);
    if (hit("##griddown", id, sx0, y1 - ah, sx1, y1, &dn_h)) top = std::min(most, top + kPitchY);
    dl->AddRectFilled(pg(sx0, y0), pg(sx1, y1), kTrack);
    draw_sprite(dl, "scroll_up_button_1", up_h ? 1 : 0, 2, sx0, y0, sx1, y0 + ah);
    draw_sprite(dl, "scroll_down_button_1", dn_h ? 1 : 0, 2, sx0, y1 - ah, sx1, y1);
    if (most > 0) {
        const float t0 = y0 + ah + 2, t1 = y1 - ah - 2;
        const float len = std::max(14.0f, (t1 - t0) * view_h / (view_h + most));
        const float ty = t0 + (t1 - t0 - len) * (top / most);
        dl->AddRectFilled(pg(sx0 + 5, ty), pg(sx1 - 5, ty + len), kThumb);
    }
    return clicked;
}

bool card_button(int key, std::string_view sprite, float x, float y, bool enabled, const char* tooltip) {
    constexpr float kW = 43, kH = 23;
    bool hovered = false, held = false;
    const bool clicked = hit("##cardbtn", key, x, y, x + kW, y + kH, &hovered, &held);
    const int state = !enabled ? 0 : held && hovered ? 2 : hovered ? 1 : 0;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    draw_sprite(dl, sprite, state, 3, x, y, x + kW, y + kH);
    if (!enabled) dl->AddRectFilled(pg(x, y), pg(x + kW, y + kH), VAN_COL32(0, 0, 0, 130));
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) click_sound();
    return clicked && enabled;
}

ListResult list(const Page& page, int id, std::span<const Row> rows, int selected_key, std::span<const std::string> titles) {
    ListResult out;
    const sf::PageNode* n = page.find(id, "LISTBOX");
    float x0, y0, x1, y1;
    if (!n || !node_rect(*n, x0, y0, x1, y1)) return out;
    struct Column {
        std::string title;
        float w;
    };
    std::vector<Column> cols;
    for (const sf::PageNode& c : n->children)
        if (c.kind == "COLUMN") cols.push_back({c.text("TITLE"), float(c.number("WIDTH", 60))});
    for (size_t i = 0; i < cols.size() && i < titles.size(); ++i) cols[i].title = titles[i];
    const bool header = n->text("TITLEBAR") != "false";
    const bool scroll = n->props.contains("SCROLLID");
    const float head = header ? kHeaderH : 0.0f;
    const float right = scroll ? x1 - kScrollW : x1;
    // The last column takes whatever the others leave.
    float used = 0;
    for (size_t i = 0; i + 1 < cols.size(); ++i) used += cols[i].w;
    if (!cols.empty()) cols.back().w = std::max(cols.back().w, right - x0 - used);

    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), kWell);
    static std::map<const void*, float> scrolled;
    const void* key = static_cast<const void*>(n);
    float& top = scrolled[key];
    const int visible = std::max(1, int((y1 - y0 - head) / kRowH));
    const int most = std::max(0, int(rows.size()) - visible);

    // The title bar.
    if (header) {
        dl->AddRectFilled(pg(x0, y0), pg(x1, y0 + head), kHeader);
        float cx = x0;
        for (size_t i = 0; i < cols.size(); ++i) {
            const float w = cols[i].w;
            put_text(dl, cx, y0, std::min(cx + w, right), y0 + head, cols[i].title, 0xFFFFFFFF, Align::Center, true, kText);
            if (i + 1 < cols.size()) dl->AddLine(pg(cx + w, y0 + 5), pg(cx + w, y0 + head - 5), kDivider, 1.0f);
            cx += w;
        }
    }

    // Input over the rows.
    bool hovered = false;
    const float ry0 = y0 + head;
    const bool clicked = hit("##rows", id, x0, ry0, right, y1, &hovered);
    const VanVec2 mouse = VanGui::GetIO().MousePos;
    int hover_row = -1;
    if (hovered) {
        const float my = (mouse.y - g_py) / g_sy;
        hover_row = int((my - ry0) / kRowH) + int(top);
        if (hover_row < 0 || hover_row >= int(rows.size())) hover_row = -1;
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) top = std::clamp(top - wheel * 3.0f, 0.0f, float(most));
    }
    top = std::clamp(top, 0.0f, float(most));
    if (hover_row >= 0) {
        out.key = rows[size_t(hover_row)].key;
        if (clicked) out.clicked = true;
        if (VanGui::IsMouseDoubleClicked(0)) out.activated = true;
    }
    if (hovered && VanGui::IsMouseClicked(1)) out.context = true;

    // The rows.
    dl->PushClipRect(pg(x0, ry0), pg(right, y1), true);
    const int first = int(top);
    for (int r = first; r < int(rows.size()) && r < first + visible + 1; ++r) {
        const float y = ry0 + kRowH * float(r - first);
        const Row& row = rows[size_t(r)];
        if (row.key == selected_key) draw_sprite(dl, "list_selected_1", 0, 1, x0, y, right, y + kRowH);
        else if (r == hover_row) dl->AddRectFilled(pg(x0, y), pg(right, y + kRowH), VAN_COL32(255, 255, 255, 16));
        float cx = x0;
        for (size_t c = 0; c < cols.size() && c < row.cells.size(); ++c) {
            const Cell& cell = row.cells[c];
            const float w = cols[c].w;
            float tx0 = cx + 2;
            const float icon = kRowH - 5;
            // An icon on its own sits in the middle of its column (a rank's chevron); with text,
            // at the left of it.
            const float ix = cell.text.empty() ? cx + (w - icon) * 0.5f : cx + 3;
            if (cell.icon.valid()) {
                dl->AddImage(VanTextureRef(VanTextureID(cell.icon.tex)), pg(ix, y + 2.5f), pg(ix + icon, y + 2.5f + icon));
                tx0 += icon + 2;
            } else if (cell.sprite.valid()) {
                dl->AddImage(VanTextureRef(VanTextureID(cell.sprite.tex)), pg(ix, y + 2.5f), pg(ix + icon, y + 2.5f + icon), cell.sprite.uv0, cell.sprite.uv1);
                tx0 += icon + 2;
            }
            put_text(dl, tx0, y, cx + w - 2, y + kRowH, cell.text, cell.colour, cell.align, cell.bold, kText);
            cx += w;
        }
    }
    dl->PopClipRect();

    // The scroll strip: the lobby's orange arrows at either end of a dark track.
    if (scroll) {
        const float sx0 = right, sx1 = x1;
        const float ah = 20.0f;
        dl->AddRectFilled(pg(sx0, y0), pg(sx1, y1), kTrack);
        bool up_h = false, dn_h = false;
        if (hit("##up", id, sx0, y0, sx1, y0 + ah, &up_h)) top = std::max(0.0f, top - 1.0f);
        if (hit("##down", id, sx0, y1 - ah, sx1, y1, &dn_h)) top = std::min(float(most), top + 1.0f);
        draw_sprite(dl, "scroll_up_button_1", up_h ? 1 : 0, 2, sx0, y0, sx1, y0 + ah);
        draw_sprite(dl, "scroll_down_button_1", dn_h ? 1 : 0, 2, sx0, y1 - ah, sx1, y1);
        if (most > 0) {
            const float track0 = y0 + ah + 2, track1 = y1 - ah - 2;
            const float len = std::max(12.0f, (track1 - track0) * float(visible) / float(rows.size()));
            const float t = top / float(most);
            const float ty = track0 + (track1 - track0 - len) * t;
            dl->AddRectFilled(pg(sx0 + 5, ty), pg(sx1 - 5, ty + len), kThumb);
        }
    }
    return out;
}


// ── Dialogs ────────────────────────────────────────────────────────────────────

namespace {

constexpr VanU32 kDialogBody = VAN_COL32(27, 27, 25, 248);
constexpr VanU32 kLime = VAN_COL32(202, 228, 80, 255);       // the kit's lit lettering
constexpr VanU32 kSoftInk = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kDimInk = VAN_COL32(128, 128, 122, 255);
constexpr float kTitleH = 24.0f;

// The orange arrows at either end of a dark track, with a thumb when there is more to see.
void scroll_strip(int id, float sx0, float y0, float sx1, float y1, float& top, int most, int visible, int count) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float ah = 20.0f;
    dl->AddRectFilled(pg(sx0, y0), pg(sx1, y1), kTrack);
    bool up_h = false, dn_h = false;
    if (hit("##up", id, sx0, y0, sx1, y0 + ah, &up_h)) top = std::max(0.0f, top - 1.0f);
    if (hit("##down", id, sx0, y1 - ah, sx1, y1, &dn_h)) top = std::min(float(most), top + 1.0f);
    draw_sprite(dl, "scroll_up_button_1", up_h ? 1 : 0, 2, sx0, y0, sx1, y0 + ah);
    draw_sprite(dl, "scroll_down_button_1", dn_h ? 1 : 0, 2, sx0, y1 - ah, sx1, y1);
    if (most > 0 && count > 0) {
        const float t0 = y0 + ah + 2, t1 = y1 - ah - 2;
        const float len = std::max(12.0f, (t1 - t0) * float(visible) / float(count));
        const float ty = t0 + (t1 - t0 - len) * (top / float(most));
        dl->AddRectFilled(pg(sx0 + 5, ty), pg(sx1 - 5, ty + len), kThumb);
    }
}

// How wide a label is on the page grid.
float label_width(std::string_view s, float size = kText) {
    if (s.empty()) return 0;
    const VanVec2 ts = face(true)->CalcTextSizeA(size * g_sy, 1e9f, 0.0f, s.data(), s.data() + s.size());
    return ts.x / g_sx + 4;
}

}  // namespace

namespace {
bool g_keep_on_escape = false;
}

void dialog_keep_on_escape() { g_keep_on_escape = true; }

bool dialog_begin(const char* id, float x0, float y0, float x1, float y1, std::string_view title, bool* open) {
    const bool keep = g_keep_on_escape;
    g_keep_on_escape = false;
    const VanVec2 ds = VanGui::GetIO().DisplaySize;
    place_page();
    if (!VanGui::IsPopupOpen(id)) VanGui::OpenPopup(id);
    VanGui::SetNextWindowPos({0, 0});
    VanGui::SetNextWindowSize(ds);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    const bool shown = VanGui::BeginPopupModal(id, nullptr,
                                               VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoMove |
                                                   VanGuiWindowFlags_NoSavedSettings | VanGuiWindowFlags_NoScrollWithMouse | VanGuiWindowFlags_NoNav);
    VanGui::PopStyleVar(2);
    if (!shown) return false;
    VanGui::PushStyleVar(VanGuiStyleVar_ItemSpacing, {0, 0});
    VanDrawList* dl = VanGui::GetWindowDrawList();
    // A shadow, the body, the title band under the kit's title tile.
    dl->AddRectFilled(pg(x0 + 4, y0 + 5), pg(x1 + 4, y1 + 5), VAN_COL32(0, 0, 0, 120));
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), kDialogBody);
    dl->AddRectFilledMultiColor(pg(x0, y0), pg(x1, y0 + kTitleH), VAN_COL32(66, 66, 60, 255), VAN_COL32(66, 66, 60, 255), VAN_COL32(38, 38, 35, 255),
                                VAN_COL32(38, 38, 35, 255));
    draw_sprite(dl, "title_box_1", 0, 1, x0, y0, x1, y0 + kTitleH, VAN_COL32(255, 255, 255, 90));
    outline(dl, x0, y0, x1, y1, kOutline);
    outline(dl, x0 + 1, y0 + 1, x1 - 1, y1 - 1, VAN_COL32(0, 0, 0, 255));
    dl->AddLine(pg(x0 + 1, y0 + kTitleH), pg(x1 - 1, y0 + kTitleH), kOutline);
    dl->AddRectFilled(pg(x0 + 9, y0 + 8), pg(x0 + 13, y0 + kTitleH - 8), kLime);
    put_text(dl, x0 + 17, y0, x1 - 30, y0 + kTitleH, title, VAN_COL32(236, 236, 232, 255), Align::Left, true, 13.0f);
    if (kit_button(-7001, "X_1", x1 - 24, y0 + 1, x1 - 3, y0 + kTitleH - 1, true, "Close") ||
        (!keep && VanGui::IsKeyPressed(VanGuiKey_Escape) && !VanGui::IsAnyItemActive())) {
        if (open) *open = false;
        VanGui::CloseCurrentPopup();
    }
    return true;
}

void dialog_end() {
    VanGui::PopStyleVar();
    VanGui::EndPopup();
}

void dialog_close() { VanGui::CloseCurrentPopup(); }

bool kit_button(int key, std::string_view sprite, float x0, float y0, float x1, float y1, bool enabled, const char* tooltip, int states) {
    bool hovered = false, held = false;
    const bool clicked = hit("##kit", key, x0, y0, x1, y1, &hovered, &held);
    const int state = !enabled ? 0 : held && hovered ? std::min(2, states - 1) : hovered ? std::min(1, states - 1) : 0;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    draw_sprite(dl, sprite, state, states, x0, y0, x1, y1);
    if (!enabled) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 130));
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) click_sound();
    return clicked && enabled;
}

void heading(float x0, float y, float x1, std::string_view label) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float cy = y + 8;
    dl->AddCircleFilled(pg(x0 + 5, cy), pgy(3.5f), kLime);
    dl->AddCircle(pg(x0 + 5, cy), pgy(4.5f), VAN_COL32(0, 0, 0, 220));
    put_text(dl, x0 + 11, y, x1, y + 16, label, kLime, Align::Left, true, 12.0f);
    const float lx = x0 + 11 + label_width(label, 12.0f) + 4;
    if (lx < x1) dl->AddLine(pg(lx, cy), pg(x1, cy), kOutline, 1.0f);
}

void well(float x0, float y0, float x1, float y1) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->AddRectFilled(pg(x0, y0), pg(x1, y1), kWell);
    outline(dl, x0, y0, x1, y1, kOutline);
}

bool radio(int key, float x, float y, std::string_view label, bool on, bool enabled, const char* tooltip) {
    constexpr float kSize = 18;
    const float w = kSize + 3 + label_width(label);
    bool hovered = false;
    const bool clicked = hit("##radio", key, x, y, x + w, y + kSize, &hovered);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    draw_sprite(dl, "radio_btn_1", on ? 1 : 0, 2, x, y, x + kSize, y + kSize, enabled ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 110));
    const VanU32 ink = !enabled ? kDimInk : on ? kLime : hovered ? 0xFFFFFFFF : kSoftInk;
    put_text(dl, x + kSize + 1, y, x + w + 8, y + kSize, label, ink, Align::Left, true, kText, false);
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) click_sound();
    return clicked && enabled;
}

bool check(int key, float x, float y, std::string_view label, bool& value, bool enabled, const char* tooltip) {
    constexpr float kBox = 13, kRow = 18;
    const float w = kBox + 5 + label_width(label);
    bool hovered = false;
    const bool clicked = hit("##check", key, x, y, x + w, y + kRow, &hovered);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float by = y + (kRow - kBox) * 0.5f;
    draw_sprite(dl, "checkbox", value ? 1 : 0, 2, x, by, x + kBox, by + kBox, enabled ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 110));
    if (hovered && enabled) outline(dl, x - 1, by - 1, x + kBox + 1, by + kBox + 1, kLime);
    const VanU32 ink = !enabled ? kDimInk : hovered ? 0xFFFFFFFF : kSoftInk;
    put_text(dl, x + kBox + 3, y, x + w + 8, y + kRow, label, ink, Align::Left, true, kText, false);
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) {
        value = !value;
        click_sound();
        return true;
    }
    return false;
}

int arrows(int key, float x0, float y0, float x1, float y1, std::string_view label, bool enabled) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    draw_sprite(dl, "bg_gray_1", 0, 1, x0, y0, x1, y1);
    constexpr float bw = 13, bh = 16;   // m_left_1 / m_right_1: 13x50, three states
    const float cy = (y0 + y1) * 0.5f;
    int result = 0;
    for (int side = 0; side < 2; ++side) {
        const float ax0 = side == 0 ? x0 + 1 : x1 - bw - 1;
        bool hovered = false, held = false;
        if (hit(side == 0 ? "##left" : "##right", key, ax0, cy - bh * 0.5f, ax0 + bw, cy + bh * 0.5f, &hovered, &held) && enabled) result = side == 0 ? -1 : 1;
        const int state = !enabled ? 0 : held && hovered ? 2 : hovered ? 1 : 0;
        draw_sprite(dl, side == 0 ? "m_left_1" : "m_right_1", state, 3, ax0, cy - bh * 0.5f, ax0 + bw, cy + bh * 0.5f,
                    enabled ? 0xFFFFFFFF : VAN_COL32(255, 255, 255, 70));
    }
    put_text(dl, x0 + bw, y0, x1 - bw, y1, label, enabled ? 0xFFFFFFFF : VAN_COL32(150, 150, 146, 255), Align::Center, true, kText);
    if (result != 0) click_sound();
    return result;
}

bool trackbar(int key, float x0, float y0, float x1, float y1, float& value, float lo, float hi, const char* format, bool enabled) {
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const float gx1 = format ? x1 - 46 : x1;
    const float cy = (y0 + y1) * 0.5f;
    const float g0 = x0 + 5, g1 = gx1 - 5;
    VanGui::SetCursorScreenPos(pg(x0, y0));
    VanGui::PushID(key);
    VanGui::BeginDisabled(!enabled);
    (void)VanGui::InvisibleButton("##track", {std::max(1.0f, pgx(gx1 - x0)), std::max(1.0f, pgy(y1 - y0))});
    const bool hovered = VanGui::IsItemHovered(), active = VanGui::IsItemActive();
    if (VanGui::IsItemActivated()) click_sound();
    VanGui::EndDisabled();
    VanGui::PopID();
    const float before = value;
    if (active && g1 > g0) {
        const float t = std::clamp((page_mouse().x - g0) / (g1 - g0), 0.0f, 1.0f);
        value = lo + (hi - lo) * t;
    } else if (hovered && enabled) {
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) value = std::clamp(value + wheel * (hi - lo) / 20.0f, std::min(lo, hi), std::max(lo, hi));
    }
    const float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const float tx = g0 + (g1 - g0) * t;
    dl->AddRectFilled(pg(g0, cy - 2), pg(g1, cy + 2), VAN_COL32(6, 6, 5, 255));
    dl->AddRectFilled(pg(g0, cy - 2), pg(tx, cy + 2), enabled ? VAN_COL32(150, 172, 58, 255) : VAN_COL32(90, 90, 86, 255));
    outline(dl, g0 - 1, cy - 3, g1 + 1, cy + 3, kOutline);
    const VanU32 thumb = !enabled ? VAN_COL32(255, 255, 255, 110) : hovered || active ? 0xFFFFFFFF : VAN_COL32(225, 225, 220, 255);
    draw_sprite(dl, "trackbar", 0, 1, tx - 5, cy - 8.5f, tx + 5, cy + 8.5f, thumb);
    if (format) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), format, double(value));
        put_text(dl, gx1, y0, x1, y1, buf, enabled ? kSoftInk : kDimInk, Align::Right, true, kText);
    }
    return value != before;
}

int pick_list(int key, float x0, float y0, float x1, float y1, std::span<const std::string> items, int selected, bool* activated) {
    constexpr float kRow = 18;
    well(x0, y0, x1, y1);
    const float right = x1 - kScrollW;
    struct Scroll {
        float top = 0;
        int last = -2;
    };
    static std::map<int, Scroll> scrolls;
    Scroll& sc = scrolls[key];
    const int count = int(items.size());
    const int visible = std::max(1, int((y1 - y0 - 2) / kRow));
    const int most = std::max(0, count - visible);
    if (selected != sc.last) {
        // Chosen from outside (the arrows beside a picture): bring it into view.
        if (selected >= 0) {
            if (float(selected) < sc.top) sc.top = float(selected);
            else if (float(selected) >= sc.top + float(visible)) sc.top = float(selected - visible + 1);
        }
        sc.last = selected;
    }
    bool hovered = false;
    const bool clicked = hit("##pick", key, x0, y0 + 1, right, y1 - 1, &hovered);
    int hover_row = -1;
    if (hovered) {
        hover_row = int((page_mouse().y - (y0 + 1)) / kRow) + int(sc.top);
        if (hover_row < 0 || hover_row >= count) hover_row = -1;
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) sc.top -= wheel * 3.0f;
    }
    sc.top = std::clamp(sc.top, 0.0f, float(most));
    int result = -1;
    if (hover_row >= 0 && clicked) {
        result = hover_row;
        sc.last = hover_row;
        click_sound();
    }
    if (hover_row >= 0 && activated && VanGui::IsMouseDoubleClicked(0)) *activated = true;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->PushClipRect(pg(x0 + 1, y0 + 1), pg(right, y1 - 1), true);
    const int first = int(sc.top);
    for (int r = first; r < count && r < first + visible + 1; ++r) {
        const float y = y0 + 1 + kRow * float(r - first);
        if (r == selected) draw_sprite(dl, "list_selected_1", 0, 1, x0 + 1, y, right, y + kRow);
        else if (r == hover_row) dl->AddRectFilled(pg(x0 + 1, y), pg(right, y + kRow), VAN_COL32(255, 255, 255, 16));
        put_text(dl, x0 + 4, y, right - 2, y + kRow, items[size_t(r)], r == selected ? 0xFFFFFFFF : kSoftInk, Align::Left, true, kText);
    }
    dl->PopClipRect();
    scroll_strip(key, right, y0, x1, y1, sc.top, most, visible, count);
    return result;
}

int rows(int key, float x0, float y0, float x1, float y1, int count, float row_h, int selected,
         const std::function<void(int, float, float, float, float, bool, bool)>& draw) {
    well(x0, y0, x1, y1);
    const float right = x1 - kScrollW;
    static std::map<int, float> scrolls;
    float& top = scrolls[key];
    const int visible = std::max(1, int((y1 - y0 - 2) / row_h));
    const int most = std::max(0, count - visible);
    bool hovered = false;
    const bool clicked = hit("##rows", key, x0, y0 + 1, right, y1 - 1, &hovered);
    int hover_row = -1;
    if (hovered) {
        hover_row = int((page_mouse().y - (y0 + 1)) / row_h) + int(top);
        if (hover_row < 0 || hover_row >= count) hover_row = -1;
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) top -= wheel * 3.0f;
    }
    top = std::clamp(top, 0.0f, float(most));
    int result = -1;
    if (hover_row >= 0 && clicked) {
        result = hover_row;
        click_sound();
    }
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->PushClipRect(pg(x0 + 1, y0 + 1), pg(right, y1 - 1), true);
    const int first = int(top);
    for (int r = first; r < count && r < first + visible + 1; ++r) {
        const float y = y0 + 1 + row_h * float(r - first);
        if (r == selected) draw_sprite(dl, "list_selected_1", 0, 1, x0 + 1, y, right, y + row_h);
        else if (r == hover_row) dl->AddRectFilled(pg(x0 + 1, y), pg(right, y + row_h), VAN_COL32(255, 255, 255, 16));
        draw(r, x0 + 1, y, right, y + row_h, r == hover_row, r == selected);
    }
    dl->PopClipRect();
    scroll_strip(key, right, y0, x1, y1, top, most, visible, count);
    return result;
}

int tile_grid(int key, float x0, float y0, float x1, float y1, int count, float tile, int selected,
              const std::function<void(int, float, float, float, float)>& draw) {
    well(x0, y0, x1, y1);
    const float right = x1 - kScrollW;
    constexpr float kGap = 4;
    const int across = std::max(1, int((right - x0 - kGap) / (tile + kGap)));
    const int lines = (count + across - 1) / across;
    const float pitch = tile + kGap;
    static std::map<int, float> scrolls;
    float& top = scrolls[key];
    const int visible = std::max(1, int((y1 - y0 - kGap) / pitch));
    const int most = std::max(0, lines - visible);
    bool hovered = false;
    const bool clicked = hit("##tiles", key, x0, y0 + 1, right, y1 - 1, &hovered);
    int hover = -1;
    const VanVec2 m = page_mouse();
    if (hovered) {
        const int cx = int((m.x - (x0 + kGap)) / pitch), cy = int((m.y - (y0 + kGap)) / pitch) + int(top);
        const float fx = std::fmod(m.x - (x0 + kGap), pitch), fy = std::fmod(m.y - (y0 + kGap), pitch);
        if (cx >= 0 && cx < across && fx < tile && fy < tile && cx + cy * across < count) hover = cx + cy * across;
        const float wheel = VanGui::GetIO().MouseWheel;
        if (wheel != 0) top -= wheel;
    }
    top = std::clamp(top, 0.0f, float(most));
    int result = -1;
    if (hover >= 0 && clicked) {
        result = hover;
        click_sound();
    }
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->PushClipRect(pg(x0 + 1, y0 + 1), pg(right, y1 - 1), true);
    const int first = int(top) * across;
    for (int i = first; i < count && i < first + (visible + 1) * across; ++i) {
        const float tx = x0 + kGap + pitch * float(i % across), ty = y0 + kGap + pitch * float(i / across - int(top));
        dl->AddRectFilled(pg(tx, ty), pg(tx + tile, ty + tile), VAN_COL32(40, 40, 37, 255));
        draw(i, tx, ty, tx + tile, ty + tile);
        if (i == selected) {
            outline(dl, tx - 1, ty - 1, tx + tile + 1, ty + tile + 1, kLime);
            outline(dl, tx, ty, tx + tile, ty + tile, kLime);
        } else if (i == hover) {
            outline(dl, tx - 1, ty - 1, tx + tile + 1, ty + tile + 1, VAN_COL32(236, 236, 232, 200));
        }
    }
    dl->PopClipRect();
    scroll_strip(key + 50000, right, y0, x1, y1, top, most, visible, lines);
    return result;
}

bool tab_button(int key, float x0, float y0, float x1, float y1, std::string_view label, bool chosen) {
    bool hovered = false;
    const bool clicked = hit("##tabtext", key, x0, y0, x1, y1, &hovered);
    tab_plate(VanGui::GetWindowDrawList(), x0, y0, x1, y1, label, chosen, hovered);
    if (clicked && !chosen) click_sound();
    return clicked;
}

void clip_begin(float x0, float y0, float x1, float y1) { VanGui::GetWindowDrawList()->PushClipRect(pg(x0, y0), pg(x1, y1), true); }
void clip_end() { VanGui::GetWindowDrawList()->PopClipRect(); }
VanVec2 pointer() { return page_mouse(); }
float wheel() { return VanGui::GetIO().MouseWheel; }

bool text_button(int key, float x0, float y0, float x1, float y1, std::string_view label, bool enabled, const char* tooltip) {
    bool hovered = false, held = false;
    const bool clicked = hit("##text", key, x0, y0, x1, y1, &hovered, &held);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    tab_plate(dl, x0, y0 + (held && hovered ? 1.0f : 0.0f), x1, y1, label, false, enabled && hovered);
    if (!enabled) dl->AddRectFilled(pg(x0, y0), pg(x1, y1), VAN_COL32(0, 0, 0, 130));
    if (tooltip && hovered) tip(tooltip);
    if (clicked && enabled) click_sound();
    return clicked && enabled;
}

}  // namespace lsf::ui
