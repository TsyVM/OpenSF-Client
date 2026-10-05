#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Input.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lsf {

using namespace ui;

void backdrop(App& app, float darken) {
    // The lobby's own background: the SF Legacy shield on brick (default_bottom.bmp, the top
    // 1024x768 of its sheet).
    ui::backdrop(app.atlas().picture(sf::Pack::Lobby, "default_bottom.bmp", 1024, 768), darken);
}

Nav top_bar(App& app, bool allow_shop) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const Palette& p = pal();
    // A dark band across the top, the kit's keyline under it.
    dl->AddRectFilledMultiColor(stage(0, 0), stage(kStageW, 84), VAN_COL32(6, 6, 5, 235), VAN_COL32(6, 6, 5, 235), VAN_COL32(14, 14, 11, 200),
                                VAN_COL32(14, 14, 11, 200));
    dl->AddLine(stage(0, 84), stage(kStageW, 84), col(p.keyline), px(2));
    dl->AddLine(stage(0, 86), stage(kStageW, 86), col(p.gold, 0.35f), 1.0f);
    const float in = intro("topbar", 0.4f);
    sprite(dl, app.atlas().sprite("sf_mark_1"), 14 - (1 - in) * 40, 7, 258, 64, col(VanVec4(1, 1, 1, in)));

    Nav nav = Nav::None;
    // The nav plates (the kit's four-state buttons), sliding in one after another.
    struct Plate {
        const char* sprite;
        const char* tip;
        Nav nav;
    };
    const Plate plates[] = {
        {"char_shop_1", "Character shop: forces and outfits", Nav::CharShop},
        {"arms_shop_1", "Weapon shop", Nav::WeaponShop},
        {"inventory_1", "Inventory: what you own and what you carry", Nav::Inventory},
        {"environment_1", "Options", Nav::Options},
    };
    // 620..990, leaving kBackPlateX free before the player card at 1090.
    float x = 620;
    for (int i = 0; i < 4; ++i) {
        const float dy = slide("topbar.plate" + std::to_string(i), -60, 0.35f, 0.05f * float(i));
        const bool enabled = plates[i].nav == Nav::Options || allow_shop;
        if (sprite_button(eng::str::format("##plate%d", i).c_str(), plates[i].sprite, x, 18 + dy, 88, 49, enabled, 4, plates[i].tip))
            nav = plates[i].nav;
        x += 94;
    }

    // The player card.
    const Session& s = app.session();
    if (s.has_profile) {
        const float cx = 1090;
        panel(dl, cx, 10, 420, 66, nullptr, true);
        const int rank = s.profile.rank();
        picture(dl, app.atlas().rank_badge(rank), cx + 10, 18, 50, 50, 0xFFFFFFFF, false);
        text(dl, font_heading(), 22, cx + 70, 14, col(p.gold_bright), s.profile.code_name);
        text(dl, font_body(), 13, cx + 70, 42, col(p.text_dim), rank_name(rank));
        // Experience through the rank, as a thin bar.
        const float prog = rank_progress(s.profile.xp);
        dl->AddRectFilled(stage(cx + 70, 62), stage(cx + 250, 66), VAN_COL32(10, 10, 8, 220));
        dl->AddRectFilled(stage(cx + 70, 62), stage(cx + 70 + 180 * follow("xpbar", prog, 0.6f), 66), col(p.lime));
        text(dl, font_bold(), 15, cx + 408, 16, col(p.lime), eng::str::format("%s SP", eng::str::format("%u", s.profile.sp).c_str()), Align::Right);
        text(dl, font_body(), 13, cx + 408, 40, col(p.text_dim),
             eng::str::format("K/D %.2f   W %u  L %u", double(s.profile.kd()), s.profile.wins, s.profile.losses), Align::Right);
        // PR-13: the day's 25 levels, counted across every server together.
        if (s.tv.signed_in)
            text(dl, font_body(), 11, cx + 408, 56, col(s.tv.xp_capped ? p.warn : p.text_mute),
                 s.tv.xp_capped ? std::string("XP limit reached today") : eng::str::format("%u levels left today", unsigned(s.tv.levels_left)), Align::Right);
    }
    if (sprite_button("##exit", "exit_1", 1526, 14, 59, 56, true, 3, "Exit Soldier Front")) nav = Nav::Exit;
    return nav;
}

void bottom_bar(App& app, const char* where) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const Palette& p = pal();
    dl->AddRectFilled(stage(0, 870), stage(kStageW, kStageH), VAN_COL32(4, 4, 3, 230));
    dl->AddLine(stage(0, 870), stage(kStageW, 870), col(p.keyline), px(2));
    const Session& s = app.session();
    std::string left = s.server_name.empty() ? std::string("Soldier Front Legacy") : s.server_name;
    if (where && *where) left += std::string("  \xC2\xB7  ") + where;
    text(dl, font_bold(), 14, 16, 876, col(p.gold), left);
    marquee(dl, marquee_text(s), 480, 875, 640, 15);
    std::string right;
    if (s.signed_in()) right = eng::str::format("Ping %d ms   ", std::max(0, s.ping_ms()));
    if (app.settings().show_fps) right += eng::str::format("%.0f FPS   %s", double(app.fps()), eng::api_name(app.device().api()));
    text(dl, font_body(), 13, kStageW - 16, 877, col(p.text_dim), right, Align::Right);
}

void chat_panel(App& app, float x, float y, float w, float h, bool room) {
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    const Palette& p = pal();
    Session& s = app.session();
    ScreenState& st = app.state();
    panel(dl, x, y, w, h, room ? "ROOM CHAT" : "CHAT");
    const auto& lines = room ? s.room_chat : s.lobby_chat;
    begin_area(room ? "##chat_room" : "##chat_lobby", x + 8, y + 34, w - 16, h - 76);
    (void)VanGui::BeginChild("##log", {px(w - 16), px(h - 78)}, 0, VanGuiWindowFlags_NoBackground);
    for (const auto& e : lines) {
        const VanVec4 name_col = e.line.scope == u8(proto::ChatScope::System) ? p.warn : e.line.scope == u8(proto::ChatScope::Whisper) ? p.lime
                                 : e.line.team == u8(Team::Red)                   ? p.red
                                 : e.line.team == u8(Team::Blue)                  ? mix(p.blue, p.text, 0.3f)
                                                                                 : p.gold;
        VanGui::PushStyleColor(VanGuiCol_Text, name_col);
        VanGui::TextUnformatted((e.line.from + ":").c_str());
        VanGui::PopStyleColor();
        VanGui::SameLine();
        VanGui::PushTextWrapPos(0);
        VanGui::TextUnformatted(e.line.text.c_str());
        VanGui::PopTextWrapPos();
    }
    if (VanGui::GetScrollY() >= VanGui::GetScrollMaxY() - 4) VanGui::SetScrollHereY(1.0f);
    VanGui::EndChild();
    end_area();
    begin_area(room ? "##chatin_room" : "##chatin_lobby", x + 8, y + h - 38, w - 16, 32);
    const char* scopes[] = {room ? "Room" : "All", "Team", "Whisper"};
    VanGui::SetNextItemWidth(px(90));
    (void)VanGui::Combo("##scope", &st.chat_scope, scopes, room ? 3 : 1);
    if (!room) st.chat_scope = 0;
    VanGui::SameLine();
    const bool send = input("##line", st.chat_scope == 2 ? "name message..." : "Type a message and press Enter", st.chat_line, w - 16 - 100 - 8, false,
                            VanGuiInputTextFlags_EnterReturnsTrue);
    end_area();
    if (send && !st.chat_line.empty()) {
        if (st.chat_scope == 2) {
            const auto sp = st.chat_line.find(' ');
            if (sp != std::string::npos) s.chat(proto::ChatScope::Whisper, st.chat_line.substr(sp + 1), st.chat_line.substr(0, sp));
        } else {
            s.chat(room ? (st.chat_scope == 1 ? proto::ChatScope::Team : proto::ChatScope::Room) : proto::ChatScope::Lobby, st.chat_line);
        }
        st.chat_line.clear();
    }
}

void handle_nav(App& app, Nav nav) {
    ScreenState& st = app.state();
    switch (nav) {
        case Nav::CharShop:
        case Nav::WeaponShop:
        case Nav::ItemShop:
        case Nav::Inventory:
            st.shop_tab = nav == Nav::CharShop ? 1 : nav == Nav::Inventory ? 2 : nav == Nav::ItemShop ? 3 : 0;
            st.item_sel = -1;
            st.shop_sel = -1;
            // From one shop plate to another, Out still goes back where you came in from.
            if (app.screen() != Screen::Shop && app.screen() != Screen::Recordings) st.shop_return = app.screen();
            app.go(Screen::Shop);
            break;
        case Nav::Recordings:
            // Your own matches' recordings (PageReplay); Out goes back where you came in from.
            if (app.screen() != Screen::Recordings && app.screen() != Screen::Shop) st.shop_return = app.screen();
            st.replays_asked = false;
            app.go(Screen::Recordings);
            break;
        case Nav::Clan:
            st.clan_open = true;
            st.clan_member_sel.clear();
            st.clan_notice_editing = false;
            st.clan_browse_at = -100;
            app.session().request_clan();
            break;
        case Nav::Options:
            open_settings(app, -1, -1);
            break;
        case Nav::Exit:
            st.confirm_quit = true;
            break;
        default:
            break;
    }
    // Opened here, where it is drawn: a popup is found by its name within the window that opened
    // it, so one opened inside a page's window and drawn out here never showed (the server and
    // channel lists' Exit did nothing).
    if (st.confirm_quit && !VanGui::IsPopupOpen("Leave Soldier Front?")) open_modal("Leave Soldier Front?");
    if (st.confirm_quit) {
        if (begin_modal("Leave Soldier Front?", "LEAVE SOLDIER FRONT?", 420, 190)) {
            VanGui::TextWrapped("Close the game and return to the desktop?");
            VanGui::Dummy({0, px(20)});
            if (button("Leave", 180, 42, Style::Danger)) {
                app.quit();
                st.confirm_quit = false;
                VanGui::CloseCurrentPopup();
            }
            VanGui::SameLine();
            if (button("Stay", 180, 42)) {
                st.confirm_quit = false;
                VanGui::CloseCurrentPopup();
            }
            end_modal();
        } else {
            st.confirm_quit = false;
        }
    }
}

}  // namespace lsf
