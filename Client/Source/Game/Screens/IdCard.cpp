// The ID card (gametext 283; locale_string_table IDCARD_*): any soldier's card, opened from a name's
// menu, and your own from the top bar's. Three tabs in the kit's own plates (idcard_tab_total,
// _equip, _weapon): the record (winning percentage, kill / death, forfeits, accuracy, survival
// rate, head shots, team kills, accomplished missions, attendance), the soldier as he is dressed
// with what his parts add, and the four weapons he carries as he has made them. The line at the
// foot is the soldier's own to write. The server keeps and sends all of it (Server/Social.cpp).
#include "Game/Screens/Screens.hpp"

#include "Game/Items.hpp"
#include "Game/Render/ModelStage.hpp"
#include "Game/Ui/Emblem.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

ui::Picture weapon_icon(App& app, const WeaponDef& w);

void open_id_card(App& app, const std::string& code_name) {
    if (code_name.empty()) return;
    ScreenState& st = app.state();
    st.card_open = true;
    st.card_name = code_name;
    st.card_tab = 0;
    st.card_editing = false;
    app.session().id_card.reset();
    app.session().request_id_card(code_name);
}

namespace {

using ui::Align;

constexpr VanU32 kWhite = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kSoft = VAN_COL32(200, 200, 194, 255);
constexpr VanU32 kDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kLime = VAN_COL32(173, 239, 16, 255);
constexpr VanU32 kGreen = VAN_COL32(96, 208, 72, 255);
constexpr VanU32 kOrange = VAN_COL32(236, 160, 48, 255);

constexpr float X0 = 242, Y0 = 136, X1 = 782, Y1 = 632;

// One line of the record: its label, and its number at the column's right.
void line(float x0, float x1, float y, const char* label, const std::string& value, VanU32 ink = kWhite) {
    ui::text_at(x0, y, x1, y + 20, label, kSoft, Align::Left, true, 12.0f);
    ui::text_at(x0, y, x1, y + 20, value, ink, Align::Right, true, 12.0f);
}

std::string percent(double part, double whole) { return whole > 0 ? eng::str::format("%.1f %%", 100.0 * part / whole) : std::string("-"); }

void record_tab(const proto::IdCard& c, float y0, float y1) {
    ui::well(X0 + 16, y0, X1 - 16, y1);
    const float lx0 = X0 + 30, lx1 = X0 + 262, rx0 = X0 + 284, rx1 = X1 - 30;
    float y = y0 + 12;
    const u32 games = c.wins + c.losses;
    line(lx0, lx1, y, "Winning Pct", percent(c.wins, games));
    line(rx0, rx1, y, "Win / Lose", eng::str::format("%u / %u", c.wins, c.losses));
    y += 26;
    line(lx0, lx1, y, "Kill / Death", eng::str::format("%.3f", c.deaths ? double(c.kills) / double(c.deaths) : double(c.kills)));
    line(rx0, rx1, y, "Kills / Deaths", eng::str::format("%u / %u", c.kills, c.deaths));
    y += 26;
    line(lx0, lx1, y, "Accuracy", percent(double(c.hits), double(c.shots)));
    line(rx0, rx1, y, "Headshot", eng::str::format("%u", c.headshots));
    y += 26;
    line(lx0, lx1, y, "Survival rate", percent(c.survived, c.rounds));
    line(rx0, rx1, y, "Accomplished Missions", eng::str::format("%u", c.missions));
    y += 26;
    line(lx0, lx1, y, "Number of forfeits", eng::str::format("%u", c.forfeits), c.forfeits ? kOrange : kWhite);
    line(rx0, rx1, y, "Number of team kills", eng::str::format("%u", c.team_kills), c.team_kills ? kOrange : kWhite);
    y += 26;
    line(lx0, lx1, y, "Games played", eng::str::format("%u", c.matches));
    line(rx0, rx1, y, "Attendance", eng::str::format("%u day%s", c.attended, c.attended == 1 ? "" : "s"));
}

void equip_tab(App& app, const proto::IdCard& c, float y0, float y1) {
    ui::well(X0 + 16, y0, X0 + 250, y1);
    const ui::Picture pic = app.stage().live(c.force, 14.0f * std::sin(float(app.now()) * 0.6f), c.parts);
    VanDrawList* dl = VanGui::GetWindowDrawList();
    dl->PushClipRect(ui::pg(X0 + 17, y0 + 1), ui::pg(X0 + 249, y1 - 1), true);
    if (pic.valid()) ui::picture_at(pic, X0 - 24, y0 - 4, X0 + 290, y1 + 56, 0xFFFFFFFF, true);
    else if (app.stage().loading(c.force, c.parts)) ui::text_at(X0 + 16, y0, X0 + 250, y1, "Loading...", kDim, Align::Center, true);
    dl->PopClipRect();
    const ForceDef* f = force(c.force);
    const float x0 = X0 + 266, x1 = X1 - 16;
    ui::heading(x0, y0 + 2, x1, f ? f->name : "FORCE");
    const PartTotals parts = part_totals(c.parts, c.force);
    float y = y0 + 22;
    if (f) {
        const float speed = (f->speed - 1.0f) * 10.0f + parts.speed;
        line(x0 + 6, x1 - 6, y, "Moving speed", eng::str::format("%+.1f", double(speed)), speed > 0 ? kGreen : kWhite), y += 22;
        line(x0 + 6, x1 - 6, y, "Avoid headshot", eng::str::format("+%.0f%%", double(f->avoid_headshot * 100 + parts.head))), y += 22;
        line(x0 + 6, x1 - 6, y, "Upper Defense", eng::str::format("+%.0f%%", double(f->upper_defense * 100 + parts.upper))), y += 22;
        line(x0 + 6, x1 - 6, y, "Legs Defense", eng::str::format("+%.0f%%", double(f->lower_defense * 100 + parts.legs))), y += 22;
        line(x0 + 6, x1 - 6, y, "Special point", eng::str::format("+%.0f%%", double(parts.point)), parts.point > 0 ? kGreen : kWhite), y += 26;
    }
    ui::heading(x0, y, x1, "WEARING");
    y += 20;
    int shown = 0;
    for (u16 id : c.parts) {
        const ItemDef* d = item(id);
        if (!d || y + 18 > y1) continue;
        ui::text_at(x0 + 6, y, x1 - 6, y + 18, d->name, kSoft, Align::Left, true, 11.0f);
        y += 18;
        ++shown;
    }
    if (!shown) ui::text_at(x0 + 6, y, x1 - 6, y + 18, "The force's own uniform.", kDim, Align::Left, true, 11.0f);
}

void weapon_tab(App& app, const proto::IdCard& c, float y0, float y1) {
    const float mid = (X0 + X1) * 0.5f, h = (y1 - y0 - 8) * 0.5f;
    for (int k = 0; k < 4; ++k) {
        const float x0 = k % 2 == 0 ? X0 + 16 : mid + 4, x1 = k % 2 == 0 ? mid - 4 : X1 - 16;
        const float ya = y0 + (k / 2) * (h + 8), yb = ya + h;
        ui::well(x0, ya, x1, yb);
        ui::text_at(x0 + 8, ya + 4, x1 - 8, ya + 22, slot_name(Slot(k)), kLime, Align::Left, true, 11.0f);
        // The throwables share the last well, side by side.
        if (Slot(k) == Slot::Throw) {
            std::vector<const WeaponDef*> thrown;
            for (size_t cell = kFirstThrowCell; cell < kLoadoutSlots; ++cell)
                if (const WeaponDef* t = weapon(c.loadout[cell])) thrown.push_back(t);
            if (thrown.empty()) ui::text_at(x0, ya, x1, yb, "Nothing", kDim, Align::Center, true);
            const float cw = (x1 - x0 - 16) / float(std::max<size_t>(1, thrown.size()));
            for (size_t i = 0; i < thrown.size(); ++i)
                ui::picture_at(weapon_icon(app, *thrown[i]), x0 + 8 + cw * float(i) + 4, ya + 24, x0 + 8 + cw * float(i + 1) - 4, yb - 22, 0xFFFFFFFF, true);
            continue;
        }
        const WeaponDef* w = weapon(c.loadout[size_t(k)]);
        if (!w) {
            ui::text_at(x0, ya, x1, yb, "Nothing", kDim, Align::Center, true);
            continue;
        }
        ui::text_at(x0 + 8, ya + 4, x1 - 8, ya + 22, w->name, kWhite, Align::Right, true, 11.0f);
        ui::picture_at(weapon_icon(app, *w), x0 + 20, ya + 24, x1 - 20, yb - 22, 0xFFFFFFFF, true);
    }
}

}  // namespace

void id_card_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!ui::dialog_begin("ID card", X0, Y0, X1, Y1, "ID CARD", &st.card_open)) {
        st.card_open = false;
        return;
    }
    const proto::IdCard* c = s.id_card && eng::str::iequals(s.id_card->code_name, st.card_name) ? &*s.id_card : nullptr;
    if (!c) {
        const bool refused = s.id_card && !s.id_card->found;
        ui::text_at(X0, Y0 + 40, X1, Y1 - 60, refused ? "No soldier is called " + st.card_name + "." : std::string("Asking for ") + st.card_name + "'s card...", refused ? kOrange : kDim,
                    Align::Center, true);
    } else if (!c->found) {
        ui::text_at(X0, Y0 + 40, X1, Y1 - 60, "No soldier is called " + st.card_name + ".", kOrange, Align::Center, true);
    } else {
        // Who: the rank's mark, the code name in its colour, the clan and its emblem, where he is.
        const int rank = rank_for_xp(c->xp);
        if (ui::Atlas* a = ui::atlas()) ui::picture_at(a->rank_badge(rank), X0 + 20, Y0 + 44, X0 + 68, Y0 + 92, 0xFFFFFFFF, true);
        ui::name_text_at(X0 + 80, Y0 + 42, X1 - 150, Y0 + 66, c->code_name, c->name_colour, Align::Left, 17.0f);
        ui::text_at(X0 + 80, Y0 + 68, X1 - 150, Y0 + 86, rank_name(rank), kSoft, Align::Left, true, 12.0f);
        ui::text_at(X1 - 150, Y0 + 44, X1 - 16, Y0 + 62, !c->online ? "Off duty" : c->room ? eng::str::format("In room %u", unsigned(c->room)) : std::string("On duty"),
                    c->online ? kGreen : kDim, Align::Right, true, 11.0f);
        if (!c->clan.empty()) {
            if (!c->clan_mark.layers.empty()) ui::draw_emblem(VanGui::GetWindowDrawList(), c->clan_mark, ui::pg(X1 - 40, Y0 + 66), ui::pg(X1 - 16, Y0 + 90));
            ui::name_text_at(X1 - 240, Y0 + 68, X1 - 46, Y0 + 88, c->clan, c->clan_colour, Align::Right, 12.0f);
        } else {
            ui::text_at(X1 - 240, Y0 + 68, X1 - 16, Y0 + 88, "No clan", kDim, Align::Right, true, 11.0f);
        }
        // The card's three tabs, in the kit's own plates (two states each).
        static const char* const kTabs[] = {"idcard_tab_total", "idcard_tab_equip", "idcard_tab_weapon"};
        static const char* const kTips[] = {"The record", "The soldier as dressed", "The weapons carried"};
        for (int t = 0; t < 3; ++t) {
            const float x = X0 + 16 + 61.0f * float(t), y = Y0 + 102;
            bool hovered = false;
            if (ui::region(9900 + t, x, y, x + 59, y + 26, &hovered) && st.card_tab != t) st.card_tab = t;
            ui::sprite_at(kTabs[t], st.card_tab == t ? 1 : 0, 2, x, y, x + 59, y + 26, st.card_tab == t || hovered ? 0xFFFFFFFF : VAN_COL32(200, 200, 200, 255));
            if (hovered) ui::tip(kTips[t]);
        }
        VanGui::GetWindowDrawList()->AddLine(ui::pg(X0 + 16, Y0 + 128), ui::pg(X1 - 16, Y0 + 128), VAN_COL32(96, 96, 90, 255));
        const float ty0 = Y0 + 136, ty1 = Y1 - 116;
        if (st.card_tab == 1) equip_tab(app, *c, ty0, ty1);
        else if (st.card_tab == 2) weapon_tab(app, *c, ty0, ty1);
        else record_tab(*c, ty0, ty1);

        // The soldier's own line; yours to write.
        const bool mine = c->code_name == s.profile.code_name;
        ui::heading(X0 + 16, Y1 - 108, X1 - 16, "IN THEIR OWN WORDS");
        if (mine && st.card_editing) {
            const bool enter = ui::edit_at(9910, X0 + 16, Y1 - 88, X1 - 110, Y1 - 64, st.card_message, proto::kCardMessageMax, "A line about yourself");
            if (ui::text_button(9911, X1 - 104, Y1 - 88, X1 - 16, Y1 - 64, "Save") || enter) {
                s.set_card_message(st.card_message);
                st.card_editing = false;
            }
        } else {
            ui::text_at(X0 + 22, Y1 - 88, mine ? X1 - 110 : X1 - 16, Y1 - 64, c->message.empty() ? std::string(mine ? "Nothing yet: write a line." : "Nothing written.") : c->message,
                        c->message.empty() ? kDim : kWhite, Align::Left, true, 12.0f);
            if (mine && ui::text_button(9912, X1 - 104, Y1 - 88, X1 - 16, Y1 - 64, "Write")) {
                st.card_editing = true;
                st.card_message = c->message;
            }
        }
    }
    if (ui::kit_button(9920, "close_1", X1 - 90, Y1 - 56, X1 - 17, Y1 - 15)) {
        st.card_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

}  // namespace lsf
