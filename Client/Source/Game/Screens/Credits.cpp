// The credits: whose game Soldier Front is, who made this one, and who stood behind it while it
// was made. Opened from the options (the Credits plate beside Default).
//
// The original game's lines were checked in October 2026 against Dragonfly's and the publishers'
// own histories (Docs/Options.md, Credits): Special Force, Dragonfly, Seoul, first released in
// Korea in July 2004 with Neowiz (Pmang); Soldier Front in North America with NHN USA on ijji.com
// from February 2007, and with Aeria Games from 2011 (the client this game reads carries Aeria's
// own launcher).
//
// The names under the last two headings are as their owners carry them in the TeamVanilla
// Discord. A name may stand under both: someone who tested and gave.
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <iterator>
#include <string_view>

namespace lsf {

namespace {

enum class Kind { Title, Head, Line, Name, Small, Gap };
struct Entry {
    Kind kind;
    const char* text;
};

const Entry kCredits[] = {
    {Kind::Title, "SOLDIER FRONT LEGACY"},
    {Kind::Small, "Soldier Front, rewritten by TeamVanilla"},
    {Kind::Small, "for the players who never left."},
    {Kind::Gap, ""},

    {Kind::Head, "THE ORIGINAL GAME"},
    {Kind::Line, "Soldier Front"},
    {Kind::Small, "released in Korea as Special Force"},
    {Kind::Gap, ""},
    {Kind::Small, "Created and developed by"},
    {Kind::Line, "Dragonfly"},
    {Kind::Small, "Dragonfly GF Co., Ltd.  -  Seoul, South Korea"},
    {Kind::Gap, ""},
    {Kind::Small, "First released in Korea in July 2004, published by"},
    {Kind::Line, "Neowiz"},
    {Kind::Gap, ""},
    {Kind::Small, "Published in North America as Soldier Front by"},
    {Kind::Line, "NHN USA  (ijji.com, 2007)"},
    {Kind::Line, "Aeria Games  (from 2011)"},
    {Kind::Gap, ""},
    {Kind::Small, "Soldier Front, Special Force, and the maps, soldiers, weapons,"},
    {Kind::Small, "sounds and art this game reads from your Soldier Front client"},
    {Kind::Small, "are the work and the property of Dragonfly and its publishers."},
    {Kind::Small, "Soldier Front Legacy is a fan project. It is not made, endorsed"},
    {Kind::Small, "or supported by them, and it owes them the game it loves."},
    {Kind::Gap, ""},

    {Kind::Head, "SOLDIER FRONT LEGACY"},
    {Kind::Small, "Owner"},
    {Kind::Line, "Skook"},
    {Kind::Gap, ""},
    {Kind::Small, "Made by"},
    {Kind::Line, "TeamVanilla"},
    {Kind::Gap, ""},

    {Kind::Head, "DONATED DURING DEVELOPMENT"},
    {Kind::Name, "[Mod] Dnos"},
    {Kind::Name, "Mr-MoFo"},
    {Kind::Name, "sillylacrosse"},
    {Kind::Gap, ""},

    {Kind::Head, "BETA TESTERS"},
    {Kind::Name, "[GM] Ty."},
    {Kind::Name, "[Mod] Corp"},
    {Kind::Name, "[Mod] Dnos"},
    {Kind::Name, "[TV] Toma"},
    {Kind::Name, "Bryan"},
    {Kind::Name, "Dunks"},
    {Kind::Name, "Ferros"},
    {Kind::Name, "Frogecorns"},
    {Kind::Name, "Guncle"},
    {Kind::Name, "haloeightysix"},
    {Kind::Name, "heff"},
    {Kind::Name, "Jay"},
    {Kind::Name, "KiLLuAgaiN"},
    {Kind::Name, "KingCamacho"},
    {Kind::Name, "Marvel"},
    {Kind::Name, "Mirusan"},
    {Kind::Name, "Moe"},
    {Kind::Name, "Mr-MoFo"},
    {Kind::Name, "Oldman"},
    {Kind::Name, "OnlyJudge"},
    {Kind::Name, "OnlyTwist"},
    {Kind::Name, "Rage"},
    {Kind::Name, "roodneusje"},
    {Kind::Name, "sillylacrosse"},
    {Kind::Gap, ""},

    {Kind::Head, "MADE WITH"},
    {Kind::Small, "VanGUI  -  TeamVanilla"},
    {Kind::Small, "ANGLE  -  The ANGLE Project Authors"},
    {Kind::Small, "minimp3  -  lieff"},
    {Kind::Small, "stb  -  Sean Barrett"},
    {Kind::Gap, ""},
    {Kind::Gap, ""},
    {Kind::Line, "Thank you for playing."},
    {Kind::Gap, ""},
};

float height_of(Kind k) {
    switch (k) {
        case Kind::Title: return 34;
        case Kind::Head: return 30;
        case Kind::Line: return 20;
        case Kind::Name: return 18;
        case Kind::Small: return 16;
        default: return 12;
    }
}

}  // namespace

int credits_names(const char* heading) {
    int n = 0;
    bool in = false;
    for (const Entry& e : kCredits) {
        if (e.kind == Kind::Head) in = std::string_view(e.text) == heading;
        else if (in && e.kind == Kind::Name) ++n;
    }
    return n;
}

void credits_modal(App& app) {
    ScreenState& st = app.state();
    constexpr float X0 = 252, Y0 = 110, X1 = 772, Y1 = 660;
    if (!ui::dialog_begin("Credits", X0, Y0, X1, Y1, "CREDITS", &st.credits_open)) {
        st.credits_open = false;
        return;
    }
    const float wx0 = X0 + 14, wy0 = Y0 + 36, wx1 = X1 - 14, wy1 = Y1 - 58;
    ui::well(wx0, wy0, wx1, wy1);
    float total = 0;
    for (const Entry& e : kCredits) total += height_of(e.kind);
    const float view_h = wy1 - wy0 - 16;
    const float most = std::max(0.0f, total - view_h);
    // It rolls by itself after a moment, and stops for good once the wheel (or the arrows) takes it.
    float& scroll = st.credits_scroll;
    bool& by_hand = st.credits_by_hand;
    const VanVec2 m = ui::pointer();
    const bool over = m.x >= wx0 && m.x < wx1 && m.y >= wy0 && m.y < wy1;
    if (const float w = ui::wheel(); w != 0 && over) scroll -= w * 40.0f, by_hand = true;
    if (VanGui::IsKeyPressed(VanGuiKey_DownArrow)) scroll += 40, by_hand = true;
    if (VanGui::IsKeyPressed(VanGuiKey_UpArrow)) scroll -= 40, by_hand = true;
    if (!by_hand && app.now() - st.credits_since > 2.0) scroll += app.dt() * 22.0f;
    scroll = std::clamp(scroll, 0.0f, most);

    constexpr VanU32 kTitle = VAN_COL32(243, 211, 142, 255), kHead = VAN_COL32(202, 228, 80, 255), kLine = VAN_COL32(236, 236, 232, 255),
                     kSmall = VAN_COL32(168, 168, 160, 255), kRule = VAN_COL32(80, 80, 78, 255);
    ui::clip_begin(wx0 + 1, wy0 + 1, wx1 - 1, wy1 - 1);
    float y = wy0 + 8 - scroll;
    for (const Entry& e : kCredits) {
        const float h = height_of(e.kind);
        if (y + h >= wy0 && y <= wy1) {
            switch (e.kind) {
                case Kind::Title: ui::text_at(wx0, y, wx1, y + h, e.text, kTitle, ui::Align::Center, true, 20.0f); break;
                case Kind::Head: {
                    // The words between two keylines.
                    ui::text_at(wx0, y + 8, wx1, y + h, e.text, kHead, ui::Align::Center, true, 13.0f);
                    const float half = float(std::string_view(e.text).size()) * 4.4f + 14;
                    const float cx = (wx0 + wx1) * 0.5f, cy = y + 8 + (h - 8) * 0.5f;
                    ui::fill_at(wx0 + 30, cy, cx - half, cy + 1, kRule);
                    ui::fill_at(cx + half, cy, wx1 - 30, cy + 1, kRule);
                    break;
                }
                case Kind::Line: ui::text_at(wx0, y, wx1, y + h, e.text, kLine, ui::Align::Center, true, 14.0f); break;
                case Kind::Name: ui::text_at(wx0, y, wx1, y + h, e.text, kLine, ui::Align::Center, true, 13.0f); break;
                case Kind::Small: ui::text_at(wx0, y, wx1, y + h, e.text, kSmall, ui::Align::Center, false, 12.0f); break;
                default: break;
            }
        }
        y += h;
    }
    ui::clip_end();
    // How far down it is.
    if (most > 0) {
        const float t0 = wy0 + 4, t1 = wy1 - 4, len = std::max(18.0f, (t1 - t0) * view_h / total);
        const float ty = t0 + (t1 - t0 - len) * (scroll / most);
        ui::fill_at(wx1 - 6, ty, wx1 - 3, ty + len, VAN_COL32(92, 92, 86, 255));
    }
    if (ui::kit_button(9520, "close_1", X1 - 87, Y1 - 50, X1 - 14, Y1 - 9)) {
        st.credits_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

}  // namespace lsf
