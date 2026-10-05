#include "Game/Ui/Ui.hpp"

#include "Engine/Platform/System.hpp"

#include <vangui/misc/vangui_anim.h>
#include <vangui/misc/vangui_loading.h>
#include <vangui/misc/vangui_notify.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace lsf::ui {

namespace {

float g_scale = 1.0f;
Atlas* g_atlas = nullptr;
float g_ox = 0.0f, g_oy = 0.0f;
float g_win_w = 1600, g_win_h = 900;
ViewRect g_view;
bool g_fill = false;
float g_stage_w = kStageW, g_stage_h = kStageH;

void place_stage() {
    const ViewRect& v = g_view;
    if (g_fill) {
        // The picture's own shape; a picture narrower than 11:9 keeps the HUD's width and gains height.
        g_scale = std::max(0.25f, std::min(v.h / kStageH, v.w / 1100.0f));
        g_stage_w = v.w / g_scale;
        g_stage_h = v.h / g_scale;
        g_ox = v.x;
        g_oy = v.y;
    } else {
        g_scale = std::max(0.25f, std::min(v.w / kStageW, v.h / kStageH));
        g_stage_w = kStageW;
        g_stage_h = kStageH;
        g_ox = v.x + std::floor((v.w - kStageW * g_scale) * 0.5f);
        g_oy = v.y + std::floor((v.h - kStageH * g_scale) * 0.5f);
    }
}
const eng::UiFonts* g_fonts = nullptr;
double g_now = 0;
float g_dt = 0;
std::map<std::string, double> g_intros;

// A button's hover warmth, keyed by its id in the current ID scope.
float hover_ease(VanGuiID id, bool hovered, float seconds = 0.12f) {
    return VanGui::Anim::AnimFloat(id, hovered ? 1.0f : 0.0f, {seconds, VanGui::Anim::VanEasing_CubicOut});
}

}  // namespace

const Palette& pal() {
    static const Palette p;
    return p;
}

VanU32 col(const VanVec4& c, float alpha) {
    return VAN_COL32(int(std::clamp(c.x, 0.0f, 1.0f) * 255), int(std::clamp(c.y, 0.0f, 1.0f) * 255), int(std::clamp(c.z, 0.0f, 1.0f) * 255),
                     int(std::clamp(c.w * alpha, 0.0f, 1.0f) * 255));
}

VanVec4 mix(const VanVec4& a, const VanVec4& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

VanVec4 team_colour(eng::u8 team) {
    if (team == 0) return pal().red;
    if (team == 1) return pal().blue;
    return pal().text_dim;
}

void apply_theme(VanGuiStyle& s) {
    const Palette& p = pal();
    s.WindowRounding = 0;
    s.ChildRounding = 0;
    s.FrameRounding = 2;
    s.PopupRounding = 2;
    s.ScrollbarRounding = 2;
    s.GrabRounding = 2;
    s.TabRounding = 0;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    s.PopupBorderSize = 1;
    s.WindowPadding = {10, 10};
    s.FramePadding = {8, 5};
    s.ItemSpacing = {8, 6};
    s.ItemInnerSpacing = {6, 4};
    s.ScrollbarSize = 12;
    s.GrabMinSize = 12;
    VanVec4* c = s.Colors;
    c[VanGuiCol_Text] = p.text;
    c[VanGuiCol_TextDisabled] = p.text_mute;
    c[VanGuiCol_WindowBg] = VanVec4(0.08f, 0.08f, 0.07f, 0.96f);
    c[VanGuiCol_ChildBg] = VanVec4(0, 0, 0, 0);
    c[VanGuiCol_PopupBg] = VanVec4(0.10f, 0.10f, 0.09f, 0.98f);
    c[VanGuiCol_Border] = VanVec4(0.0f, 0.0f, 0.0f, 0.9f);
    c[VanGuiCol_BorderShadow] = VanVec4(0, 0, 0, 0);
    c[VanGuiCol_FrameBg] = VanVec4(0.05f, 0.05f, 0.045f, 0.95f);
    c[VanGuiCol_FrameBgHovered] = VanVec4(0.12f, 0.12f, 0.10f, 0.95f);
    c[VanGuiCol_FrameBgActive] = VanVec4(0.16f, 0.16f, 0.12f, 0.95f);
    c[VanGuiCol_TitleBg] = p.panel_title;
    c[VanGuiCol_TitleBgActive] = p.panel_title;
    c[VanGuiCol_TitleBgCollapsed] = p.panel_title;
    c[VanGuiCol_MenuBarBg] = p.panel;
    c[VanGuiCol_ScrollbarBg] = VanVec4(0.02f, 0.02f, 0.02f, 0.6f);
    c[VanGuiCol_ScrollbarGrab] = VanVec4(0.36f, 0.34f, 0.25f, 1.0f);
    c[VanGuiCol_ScrollbarGrabHovered] = VanVec4(0.50f, 0.46f, 0.30f, 1.0f);
    c[VanGuiCol_ScrollbarGrabActive] = p.gold;
    c[VanGuiCol_CheckMark] = p.lime;
    c[VanGuiCol_SliderGrab] = p.gold;
    c[VanGuiCol_SliderGrabActive] = p.gold_bright;
    c[VanGuiCol_Button] = VanVec4(0.20f, 0.19f, 0.16f, 1.0f);
    c[VanGuiCol_ButtonHovered] = VanVec4(0.33f, 0.30f, 0.20f, 1.0f);
    c[VanGuiCol_ButtonActive] = VanVec4(0.45f, 0.39f, 0.22f, 1.0f);
    c[VanGuiCol_Header] = p.select;
    c[VanGuiCol_HeaderHovered] = p.hover;
    c[VanGuiCol_HeaderActive] = VanVec4(0.33f, 0.38f, 0.14f, 1.0f);
    c[VanGuiCol_Separator] = VanVec4(0.0f, 0.0f, 0.0f, 0.8f);
    c[VanGuiCol_Tab] = VanVec4(0.16f, 0.16f, 0.14f, 1.0f);
    c[VanGuiCol_TabHovered] = VanVec4(0.30f, 0.29f, 0.20f, 1.0f);
    c[VanGuiCol_TabSelected] = p.select;
    c[VanGuiCol_TableHeaderBg] = p.panel_title;
    c[VanGuiCol_TableBorderStrong] = VanVec4(0, 0, 0, 1);
    c[VanGuiCol_TableBorderLight] = VanVec4(0.2f, 0.2f, 0.17f, 0.6f);
    c[VanGuiCol_TableRowBg] = VanVec4(0, 0, 0, 0);
    c[VanGuiCol_TableRowBgAlt] = VanVec4(1, 1, 1, 0.025f);
    c[VanGuiCol_TextSelectedBg] = VanVec4(0.45f, 0.39f, 0.22f, 0.6f);
    c[VanGuiCol_ModalWindowDimBg] = VanVec4(0, 0, 0, 0.55f);
    c[VanGuiCol_NavCursor] = p.gold;
}

// ── Stage ──────────────────────────────────────────────────────────────────────

void set_view(float w, float h, const ViewRect& picture) {
    g_win_w = w;
    g_win_h = h;
    g_view = picture;
    if (g_view.w < 1 || g_view.h < 1) g_view = {0, 0, w, h};
    place_stage();
}
ViewRect view() { return g_view; }
void set_stage(float w, float h) { set_view(w, h, {0, 0, w, h}); }
void stage_fill(bool fill) {
    g_fill = fill;
    place_stage();
}
bool stage_filled() { return g_fill; }
float stage_w() { return g_stage_w; }
float stage_h() { return g_stage_h; }
float scale() { return g_scale; }
VanVec2 stage(float x, float y) { return {g_ox + x * g_scale, g_oy + y * g_scale}; }
float px(float d) { return d * g_scale; }
VanVec2 stage_origin() { return {g_ox, g_oy}; }

// ── Fonts ──────────────────────────────────────────────────────────────────────

void set_fonts(const eng::UiFonts* f) { g_fonts = f; }
void set_atlas(Atlas* a) { g_atlas = a; }
Atlas* atlas() { return g_atlas; }
VanFont* font_body() { return g_fonts && g_fonts->body ? g_fonts->body : VanGui::GetFont(); }
VanFont* font_kit() { return g_fonts && g_fonts->kit ? g_fonts->kit : font_body(); }
VanFont* font_bold() { return g_fonts && g_fonts->bold ? g_fonts->bold : font_body(); }
VanFont* font_heading() { return g_fonts && g_fonts->heading ? g_fonts->heading : font_bold(); }
VanFont* font_display() { return g_fonts && g_fonts->display ? g_fonts->display : font_bold(); }
VanFont* font_mono() { return g_fonts && g_fonts->mono ? g_fonts->mono : font_body(); }
VanFont* font_page() { return g_fonts && g_fonts->page ? g_fonts->page : font_body(); }
VanFont* font_page_bold() { return g_fonts && g_fonts->page_bold ? g_fonts->page_bold : font_bold(); }

VanVec2 text_size(VanFont* font, float size, std::string_view t) {
    return font->CalcTextSizeA(px(size), 1e9f, 0.0f, t.data(), t.data() + t.size());
}

void text(VanDrawList* dl, VanFont* font, float size, float x, float y, VanU32 colour, std::string_view s, Align align, bool shadow) {
    const VanVec2 sz = text_size(font, size, s);
    VanVec2 p = stage(x, y);
    if (align == Align::Center) p.x -= sz.x * 0.5f;
    if (align == Align::Right) p.x -= sz.x;
    if (shadow) dl->AddText(font, px(size), {p.x + px(1), p.y + px(1)}, VAN_COL32(0, 0, 0, (colour >> 24) * 3 / 4), s.data(), s.data() + s.size());
    dl->AddText(font, px(size), p, colour, s.data(), s.data() + s.size());
}

// ── Motion ─────────────────────────────────────────────────────────────────────

void tick(double now, float dt) {
    g_now = now;
    g_dt = dt;
}
double now() { return g_now; }

float follow(const char* id, float target, float seconds) {
    return VanGui::Anim::AnimFloat(id, target, {seconds, VanGui::Anim::VanEasing_CubicOut});
}

float intro(const std::string& key, float seconds, float delay) {
    auto [it, fresh] = g_intros.emplace(key, g_now);
    const float t = std::clamp(float(g_now - it->second - delay) / std::max(0.01f, seconds), 0.0f, 1.0f);
    if (t < 1.0f) VanGui::Anim::KeepAnimating();
    return VanGui::Anim::Ease(VanGui::Anim::VanEasing_CubicOut, t);
}

void reset_intros(std::string_view prefix) {
    for (auto it = g_intros.begin(); it != g_intros.end();) {
        if (it->first.starts_with(prefix)) it = g_intros.erase(it);
        else ++it;
    }
}

float slide(const std::string& key, float distance, float seconds, float delay) { return (1.0f - intro(key, seconds, delay)) * distance; }

float pulse(float hz, float low) {
    const float s = 0.5f + 0.5f * std::sin(float(g_now) * hz * 6.2831853f);
    VanGui::Anim::KeepAnimating();
    return low + (1.0f - low) * s;
}

// ── Chrome ─────────────────────────────────────────────────────────────────────

void backdrop(const Picture& pic, float darken) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const ViewRect& v = g_view;
    const VanVec2 a{v.x, v.y}, b{v.x + v.w, v.y + v.h};
    dl->AddRectFilled(a, b, col(pal().ink));
    if (pic.valid()) {
        // Cover the whole picture at the art's own aspect (never past it: the bars stay black).
        const float aspect = pic.w / std::max(1.0f, pic.h);
        float w = v.w, h = v.w / aspect;
        if (h < v.h) {
            h = v.h;
            w = v.h * aspect;
        }
        const float x = v.x + (v.w - w) * 0.5f, y = v.y + (v.h - h) * 0.5f;
        dl->PushClipRect(a, b, true);
        dl->AddImage(VanTextureRef(VanTextureID(pic.tex)), {x, y}, {x + w, y + h});
        dl->PopClipRect();
    }
    if (darken > 0) dl->AddRectFilled(a, b, VAN_COL32(0, 0, 0, int(std::clamp(darken, 0.0f, 1.0f) * 255)));
}

void sprite(VanDrawList* dl, const SpriteRef& s, float x, float y, float w, float h, VanU32 tint) {
    if (!s.valid()) return;
    dl->AddImage(VanTextureRef(VanTextureID(s.tex)), stage(x, y), stage(x + w, y + h), s.uv0, s.uv1, tint);
}

void picture(VanDrawList* dl, const Picture& p, float x, float y, float w, float h, VanU32 tint, bool cover) {
    if (!p.valid()) {
        dl->AddRectFilled(stage(x, y), stage(x + w, y + h), VAN_COL32(12, 12, 10, 255));
        return;
    }
    VanVec2 uv0{0, 0}, uv1{1, 1};
    if (cover) {
        // Crop to the box's aspect rather than squash.
        const float box = w / std::max(1.0f, h), img = p.w / std::max(1.0f, p.h);
        if (img > box) {
            const float keep = box / img;
            uv0.x = (1 - keep) * 0.5f;
            uv1.x = 1 - uv0.x;
        } else if (img < box) {
            const float keep = img / box;
            uv0.y = (1 - keep) * 0.5f;
            uv1.y = 1 - uv0.y;
        }
    }
    dl->AddImage(VanTextureRef(VanTextureID(p.tex)), stage(x, y), stage(x + w, y + h), uv0, uv1, tint);
}

void panel(VanDrawList* dl, float x, float y, float w, float h, const char* title, bool brown) {
    const Palette& p = pal();
    const VanVec2 a = stage(x, y), b = stage(x + w, y + h);
    // A soft drop shadow, then the kit's grey (or brown) fill, the black keyline and a bevel.
    dl->AddRectFilled({a.x + px(4), a.y + px(5)}, {b.x + px(4), b.y + px(5)}, VAN_COL32(0, 0, 0, 90));
    dl->AddRectFilled(a, b, col(brown ? p.panel_brown : p.panel));
    dl->AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, col(p.keyline), 0, 0, 1.0f);
    dl->AddRect({a.x + 1, a.y + 1}, {b.x - 1, b.y - 1}, col(p.bevel), 0, 0, 1.0f);
    if (title) {
        const float th = 28;
        const VanVec2 tb = stage(x + w, y + th);
        dl->AddRectFilledMultiColor(a, tb, col(p.panel_title), col(p.panel_title), VAN_COL32(28, 28, 24, 240), VAN_COL32(28, 28, 24, 240));
        dl->AddLine({a.x, tb.y}, {tb.x, tb.y}, col(p.keyline), 1.0f);
        dl->AddLine({a.x, tb.y + 1}, {tb.x, tb.y + 1}, col(p.gold, 0.35f), 1.0f);
        // A gold tick at the left, like the kit's title boxes.
        dl->AddRectFilled(stage(x + 8, y + 9), stage(x + 12, y + th - 9), col(p.gold));
        text(dl, font_heading(), 17, x + 20, y + 5, col(p.gold_bright), title);
    }
}

bool begin_area(const char* id, float x, float y, float w, float h, VanGuiWindowFlags extra) {
    VanGui::SetNextWindowPos(stage(x, y));
    VanGui::SetNextWindowSize({px(w), px(h)});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    const VanGuiWindowFlags flags = VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoMove |
                                    VanGuiWindowFlags_NoSavedSettings | VanGuiWindowFlags_NoBringToFrontOnFocus | extra;
    const bool open = VanGui::Begin(id, nullptr, flags);
    VanGui::PopStyleVar(2);
    if (VanGui::IsWindowAppearing()) VanGui::SetNavCursorVisible(false);
    return open;
}

void end_area() { VanGui::End(); }

// ── Controls ───────────────────────────────────────────────────────────────────

bool sprite_button(const char* id, const char* sprite_name, float x, float y, float w, float h, bool enabled, int states, const char* tooltip) {
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    VanGui::SetNextWindowPos(stage(x, y));
    VanGui::SetNextWindowSize({px(w), px(h)});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {0, 0});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    VanGui::Begin(id, nullptr,
                  VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoMove | VanGuiWindowFlags_NoSavedSettings |
                      VanGuiWindowFlags_NoFocusOnAppearing);
    VanGui::PopStyleVar(2);
    dl = VanGui::GetWindowDrawList();
    VanGui::BeginDisabled(!enabled);
    const bool pressed = VanGui::InvisibleButton("##b", {px(w), px(h)});
    const bool hovered = VanGui::IsItemHovered();
    const bool held = VanGui::IsItemActive();
    VanGui::EndDisabled();
    const float warm = hover_ease(VanGui::GetID("##warm"), hovered && enabled);
    int state = 0;
    if (!enabled) state = states >= 4 ? 3 : 0;
    else if (held) state = std::min(2, states - 1);
    else if (hovered) state = std::min(1, states - 1);
    const SpriteRef s = g_atlas ? g_atlas->sprite(sprite_name, state, states) : SpriteRef{};
    if (s.valid()) {
        sprite(dl, s, x, y, w, h, enabled ? 0xFFFFFFFF : VAN_COL32(150, 150, 150, 200));
    } else {
        dl->AddRectFilled(stage(x, y), stage(x + w, y + h), col(pal().steel));
        text(dl, font_bold(), 13, x + w * 0.5f, y + h * 0.5f - 8, col(pal().text), sprite_name, Align::Center);
    }
    if (warm > 0.01f) {
        // A gold edge glow easing in over the sprite's own hover state.
        dl->AddRect(stage(x - 1, y - 1), stage(x + w + 1, y + h + 1), col(pal().gold, 0.6f * warm), px(2), 0, px(1.5f));
    }
    if (tooltip && hovered) VanGui::SetTooltip("%s", tooltip);
    VanGui::End();
    if (pressed && enabled) click_sound();
    return pressed && enabled;
}

bool button(const char* label, float w, float h, Style style, bool enabled) {
    const Palette& p = pal();
    VanGui::BeginDisabled(!enabled);
    const VanVec2 pos = VanGui::GetCursorScreenPos();
    const VanVec2 size{px(w), px(h)};
    const bool pressed = VanGui::InvisibleButton(label, size);
    const bool hovered = VanGui::IsItemHovered();
    const bool held = VanGui::IsItemActive();
    VanGui::EndDisabled();
    const float warm = hover_ease(VanGui::GetID(label), hovered && enabled);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    VanVec4 top = VanVec4(0.24f, 0.23f, 0.19f, 1), bot = VanVec4(0.10f, 0.10f, 0.08f, 1), edge = VanVec4(0.02f, 0.02f, 0.02f, 1);
    VanVec4 label_col = p.text;
    switch (style) {
        case Style::Primary: top = VanVec4(0.47f, 0.38f, 0.17f, 1), bot = VanVec4(0.24f, 0.19f, 0.08f, 1), label_col = p.gold_bright; break;
        case Style::Danger: top = VanVec4(0.45f, 0.13f, 0.10f, 1), bot = VanVec4(0.22f, 0.05f, 0.04f, 1); break;
        case Style::Red: top = VanVec4(0.55f, 0.12f, 0.06f, 1), bot = VanVec4(0.28f, 0.05f, 0.02f, 1); break;
        case Style::Blue: top = VanVec4(0.13f, 0.20f, 0.55f, 1), bot = VanVec4(0.05f, 0.08f, 0.28f, 1); break;
        case Style::Ghost: top = VanVec4(0, 0, 0, 0.25f), bot = VanVec4(0, 0, 0, 0.45f); break;
        default: break;
    }
    top = mix(top, mix(top, VanVec4(0.62f, 0.52f, 0.28f, 1), 0.35f), warm);
    if (held) std::swap(top, bot);
    const float a = enabled ? 1.0f : 0.45f;
    const VanVec2 b{pos.x + size.x, pos.y + size.y};
    dl->AddRectFilledMultiColor(pos, b, col(top, a), col(top, a), col(bot, a), col(bot, a));
    dl->AddRect(pos, b, col(edge, a), 0, 0, 1.0f);
    dl->AddRect({pos.x + 1, pos.y + 1}, {b.x - 1, b.y - 1}, col(mix(p.bevel, p.gold, warm), a * (0.55f + 0.45f * warm)), 0, 0, 1.0f);
    const float fs = std::min(17.0f, h * 0.48f);
    const char* end = std::strstr(label, "##");
    if (!end) end = label + std::strlen(label);
    const VanVec2 tsz = font_bold()->CalcTextSizeA(px(fs), 1e9f, 0, label, end);
    const VanVec2 tp{pos.x + (size.x - tsz.x) * 0.5f, pos.y + (size.y - tsz.y) * 0.5f + (held ? 1.0f : 0.0f)};
    dl->AddText(font_bold(), px(fs), {tp.x + 1, tp.y + 1}, VAN_COL32(0, 0, 0, int(200 * a)), label, end);
    dl->AddText(font_bold(), px(fs), tp, col(mix(label_col, p.gold_bright, warm * 0.6f), a), label, end);
    if (pressed && enabled) click_sound();
    return pressed && enabled;
}

bool button_at(const char* id, const char* label, float x, float y, float w, float h, Style style, bool enabled) {
    begin_area(id, x, y, w, h);
    const bool r = button(label, w, h, style, enabled);
    end_area();
    return r;
}

bool tabs(const char* id, const std::vector<std::string>& labels, int& selected, float w, float h) {
    const Palette& p = pal();
    VanGui::PushID(id);
    bool changed = false;
    const float tw = labels.empty() ? w : w / float(labels.size());
    VanDrawList* dl = VanGui::GetWindowDrawList();
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i) VanGui::SameLine(0, 0);
        VanGui::PushID(int(i));
        const VanVec2 pos = VanGui::GetCursorScreenPos();
        if (VanGui::InvisibleButton("##t", {px(tw), px(h)}) && selected != int(i)) {
            selected = int(i);
            changed = true;
            click_sound();
        }
        const bool hov = VanGui::IsItemHovered();
        const float on = follow("##on", selected == int(i) ? 1.0f : 0.0f, 0.15f);
        const float warm = hover_ease(VanGui::GetID("##h"), hov);
        const VanVec2 b{pos.x + px(tw), pos.y + px(h)};
        dl->AddRectFilled(pos, b, col(mix(VanVec4(0.12f, 0.12f, 0.10f, 0.95f), p.select, on)));
        dl->AddRect(pos, b, col(p.keyline), 0, 0, 1.0f);
        if (on > 0.01f) dl->AddRectFilled({pos.x + 2, b.y - px(3)}, {pos.x + 2 + (px(tw) - 4) * on, b.y - 1}, col(p.lime, on));
        const std::string& l = labels[i];
        const float fs = std::min(15.0f, h * 0.5f);
        const VanVec2 ts = font_bold()->CalcTextSizeA(px(fs), 1e9f, 0, l.c_str());
        dl->AddText(font_bold(), px(fs), {pos.x + (px(tw) - ts.x) * 0.5f, pos.y + (px(h) - ts.y) * 0.5f},
                    col(mix(mix(p.text_dim, p.text, warm), p.lime, on)), l.c_str());
        VanGui::PopID();
    }
    VanGui::PopID();
    return changed;
}

bool stepper(const char* id, const char* value, int& index, int count, float w, float h) {
    const Palette& p = pal();
    VanGui::PushID(id);
    bool changed = false;
    const float bw = h;
    if (button("<", bw, h, Style::Ghost, count > 1)) {
        index = (index + count - 1) % std::max(1, count);
        changed = true;
    }
    VanGui::SameLine(0, px(2));
    const VanVec2 pos = VanGui::GetCursorScreenPos();
    VanGui::Dummy({px(w - bw * 2 - 4), px(h)});
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const VanVec2 b{pos.x + px(w - bw * 2 - 4), pos.y + px(h)};
    dl->AddRectFilled(pos, b, col(p.panel_brown));
    dl->AddRect(pos, b, col(p.keyline));
    const float fs = std::min(15.0f, h * 0.55f);
    const VanVec2 ts = font_bold()->CalcTextSizeA(px(fs), 1e9f, 0, value);
    dl->AddText(font_bold(), px(fs), {pos.x + (b.x - pos.x - ts.x) * 0.5f, pos.y + (px(h) - ts.y) * 0.5f}, col(p.lime), value);
    VanGui::SameLine(0, px(2));
    if (button(">", bw, h, Style::Ghost, count > 1)) {
        index = (index + 1) % std::max(1, count);
        changed = true;
    }
    VanGui::PopID();
    return changed;
}

bool checkbox(const char* label, bool& v) { return VanGui::Checkbox(label, &v); }

bool input(const char* id, const char* hint, std::string& value, float w, bool password, VanGuiInputTextFlags flags) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    VanGui::SetNextItemWidth(px(w));
    if (password) flags |= VanGuiInputTextFlags_Password;
    const bool r = VanGui::InputTextWithHint(id, hint, buf, sizeof(buf), flags);
    phone_keyboard(buf, int(sizeof(buf) - 1));
    value = buf;
    return r;
}

// ── A phone's keyboard (Android: the system's, over the game) ──────────────────

namespace {

struct PhoneKeyboard {
    VanGuiID owner = 0;     // the field it types into
    std::string line;       // the keyboard's own line, as last seen
    int seen = -1;          // the frame that field was last drawn with the focus
    bool away = false;      // put away (Done, or Back) while the field kept the focus
};
PhoneKeyboard g_phone;

void press(VanGuiKey key) {
    VanGuiIO& io = VanGui::GetIO();
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

bool continuation(const std::string& s, size_t i) { return i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80; }

// The keyboard's line went from `was` to `now`: the same change made in the field, as a real
// keyboard would make it (rub-outs from the end, then the new letters).
void type_change(const std::string& was, const std::string& now) {
    size_t same = 0;
    while (same < was.size() && same < now.size() && was[same] == now[same]) ++same;
    while (same > 0 && (continuation(was, same) || continuation(now, same))) --same;   // whole letters
    for (size_t i = same; i < was.size(); ++i)
        if (!continuation(was, i)) press(VanGuiKey_Backspace);
    if (same < now.size()) VanGui::GetIO().AddInputCharactersUTF8(now.c_str() + same);
}

}  // namespace

void phone_keyboard(const char* text, int limit) {
    if (!eng::platform::text_input_native()) return;
    const VanGuiID id = VanGui::GetItemID();
    if (!VanGui::IsItemActive()) {
        if (VanGui::IsItemDeactivated() && g_phone.owner == id) {
            if (!g_phone.away) eng::platform::stop_typing();
            g_phone = {};
        }
        return;
    }
    g_phone.seen = VanGui::GetFrameCount();
    // Up when the field takes the focus, and again at a tap on it (which also puts the field's caret
    // back at the end, where the keyboard's line has it).
    if (g_phone.owner != id || VanGui::IsItemClicked(VanGuiMouseButton_Left)) {
        g_phone = {id, text, VanGui::GetFrameCount(), false};
        eng::platform::start_typing(g_phone.line, limit, false);
        press(VanGuiKey_End);
        return;
    }
    std::string now;
    bool done = false;
    if (eng::platform::text_answer(now, done)) {
        type_change(g_phone.line, now);
        g_phone.line = now;
        g_phone.away = true;
        if (done) press(VanGuiKey_Enter);   // Done is Enter: the sign-in signs in, a chat line goes
    } else if (eng::platform::typed_text(now)) {
        type_change(g_phone.line, now);
        g_phone.line = now;
    }
}

const char* this_device() {
#if defined(__ANDROID__)
    return "this device";
#elif defined(__APPLE__)
    return "this Mac";
#else
    return "this PC";
#endif
}

void phone_keyboard_frame() {
    // Its field gone from the screen (the screen changed under it): the keyboard goes too.
    if (g_phone.owner && VanGui::GetFrameCount() - g_phone.seen > 1) {
        if (!g_phone.away) eng::platform::stop_typing();
        g_phone = {};
    }
}

void meter(float fraction, float w, float h, const VanVec4& colour, const char* overlay) {
    const VanVec2 pos = VanGui::GetCursorScreenPos();
    VanGui::Dummy({px(w), px(h)});
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const VanVec2 b{pos.x + px(w), pos.y + px(h)};
    VanGui::Detail::ProgressBarPrim(dl, pos, b, fraction, VAN_COL32(10, 10, 8, 230), col(colour), 0);
    dl->AddRect(pos, b, col(pal().keyline));
    if (overlay) {
        const VanVec2 ts = font_bold()->CalcTextSizeA(px(12), 1e9f, 0, overlay);
        dl->AddText(font_bold(), px(12), {pos.x + (px(w) - ts.x) * 0.5f, pos.y + (px(h) - ts.y) * 0.5f}, col(pal().text), overlay);
    }
}

void marquee(VanDrawList* dl, std::string_view s, float x, float y, float w, float size) {
    const VanVec2 ts = text_size(font_bold(), size, s);
    const float span = px(w) + ts.x;
    const float offset = std::fmod(float(g_now) * px(70), span);
    const VanVec2 a = stage(x, y), b = stage(x + w, y + size * 1.4f);
    dl->PushClipRect(a, b, true);
    dl->AddText(font_bold(), px(size), {b.x - offset, a.y}, col(pal().marquee), s.data(), s.data() + s.size());
    dl->PopClipRect();
    VanGui::Anim::KeepAnimating();
}

void spinner(const char* id, float cx, float cy, float radius, const VanVec4& colour) {
    const float r = px(radius);
    VanGui::SetNextWindowPos({stage(cx, cy).x - r - 2, stage(cx, cy).y - r - 2});
    VanGui::SetNextWindowSize({r * 2 + 4, r * 2 + 4});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {2, 2});
    VanGui::PushStyleVar(VanGuiStyleVar_WindowBorderSize, 0.0f);
    VanGui::Begin(id, nullptr,
                  VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoBackground | VanGuiWindowFlags_NoInputs | VanGuiWindowFlags_NoSavedSettings |
                      VanGuiWindowFlags_NoFocusOnAppearing);
    VanGui::PopStyleVar(2);
    VanGui::Spinner("##spin", r, std::max(2.0f, r * 0.16f), col(colour), 1.2f);
    VanGui::End();
}

void progress(VanDrawList* dl, float fraction, float x, float y, float w, float h, const char* caption) {
    const Palette& p = pal();
    const VanVec2 a = stage(x, y), b = stage(x + w, y + h);
    dl->AddRectFilled(a, b, VAN_COL32(6, 6, 5, 230));
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    const VanVec2 fb{a.x + (b.x - a.x) * f, b.y};
    dl->AddRectFilledMultiColor(a, fb, col(p.gold_bright), col(p.gold), col(mix(p.gold, p.ink, 0.4f)), col(mix(p.gold, p.ink, 0.4f)));
    // A moving sheen along the filled part, so a stall still reads as alive.
    const float sheen = std::fmod(float(g_now) * 0.8f, 1.0f);
    const float sx = a.x + (fb.x - a.x) * sheen;
    if (fb.x - a.x > px(20)) dl->AddRectFilledMultiColor({std::max(a.x, sx - px(30)), a.y}, {sx, b.y}, 0, VAN_COL32(255, 255, 255, 70), VAN_COL32(255, 255, 255, 70), 0);
    dl->AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, col(p.keyline));
    dl->AddRect({a.x + 1, a.y + 1}, {b.x - 1, b.y - 1}, col(p.bevel));
    if (caption) text(dl, font_bold(), 13, x + w * 0.5f, y + h + 6, col(p.text), caption, Align::Center);
    VanGui::Anim::KeepAnimating();
}

void veil(float alpha) {
    VanGui::GetForegroundDrawList()->AddRectFilled({0, 0}, {g_win_w, g_win_h}, VAN_COL32(0, 0, 0, int(std::clamp(alpha, 0.0f, 1.0f) * 255)));
}

// ── Sound ──────────────────────────────────────────────────────────────────────

namespace {
std::function<void()> g_click;
}

void set_click_sound(std::function<void()> play) { g_click = std::move(play); }

void click_sound() {
    if (g_click) g_click();
}

// ── Modals ─────────────────────────────────────────────────────────────────────

void open_modal(const char* id) {
    reset_intros(std::string("modal.") + id);
    VanGui::OpenPopup(id);
}

bool begin_modal(const char* id, const char* title, float w, float h, bool* open) {
    const float t = intro(std::string("modal.") + id, 0.22f);
    const float s = 0.94f + 0.06f * t;
    const VanVec2 c{g_view.x + g_view.w * 0.5f, g_view.y + g_view.h * 0.5f + px(12) * (1 - t)};
    VanGui::SetNextWindowPos({c.x - px(w) * s * 0.5f, c.y - px(h) * s * 0.5f});
    VanGui::SetNextWindowSize({px(w) * s, px(h) * s});
    VanGui::PushStyleVar(VanGuiStyleVar_Alpha, 0.25f + 0.75f * t);
    VanGui::PushStyleVar(VanGuiStyleVar_WindowPadding, {px(16), px(40)});
    VanGui::PushStyleColor(VanGuiCol_WindowBg, VanVec4(0, 0, 0, 0));
    const bool shown = VanGui::BeginPopupModal(id, open, VanGuiWindowFlags_NoDecoration | VanGuiWindowFlags_NoSavedSettings | VanGuiWindowFlags_NoMove);
    VanGui::PopStyleColor();
    VanGui::PopStyleVar(2);
    if (!shown) return false;
    // The kit's chrome behind the modal's contents.
    VanDrawList* dl = VanGui::GetWindowDrawList();
    const VanVec2 wp = VanGui::GetWindowPos(), ws = VanGui::GetWindowSize();
    const VanVec2 o = stage_origin();
    const float sx = (wp.x - o.x) / g_scale, sy = (wp.y - o.y) / g_scale;
    dl->PushClipRectFullScreen();
    panel(dl, sx, sy, ws.x / g_scale, ws.y / g_scale, title);
    dl->PopClipRect();
    return true;
}

void end_modal() { VanGui::EndPopup(); }

// ── Toasts ─────────────────────────────────────────────────────────────────────

namespace {
std::function<void(Toast, const std::string&)> g_toast_sink;
}

void set_toast_sink(std::function<void(Toast, const std::string&)> sink) { g_toast_sink = std::move(sink); }

void toast(Toast kind, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (g_toast_sink) {
        g_toast_sink(kind, buf);
        return;
    }
    const VanGui::VanNotifyType type = kind == Toast::Good      ? VanGui::VanNotifyType_Success
                                       : kind == Toast::Warning ? VanGui::VanNotifyType_Warning
                                       : kind == Toast::Bad     ? VanGui::VanNotifyType_Error
                                                                : VanGui::VanNotifyType_Info;
    VanGui::InsertNotification(type, kind == Toast::Bad ? 6000.0f : 4000.0f, "%s", buf);
}

int pending(const char* t) {
    const int id = VanGui::InsertNotification(VanGui::VanNotifyType_Info, 60000.0f, "%s", t);
    VanGui::SetNotificationProgress(id, 0.0f);
    return id;
}

void resolve(int id, bool ok, const std::string& t) {
    VanGui::DismissNotification(id);
    toast(ok ? Toast::Good : Toast::Bad, "%s", t.c_str());
}

void pending_progress(int id, float fraction) { VanGui::SetNotificationProgress(id, fraction); }

}  // namespace lsf::ui
