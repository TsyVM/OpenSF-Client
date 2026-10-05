// Friends, messages and global chat on the client:
//
//   - how every chat line reads by its scope (a whisper, the clan, global, the system), in the
//     lobby's and the room's logs and over a match alike (chat_view);
//   - the commands every chat box takes: /w name text, /r text, /c text, /g text, /t text, /a text
//     (send_chat);
//   - the lobby's Friend tab (the original's messenger): friends and where they are, those asking
//     to be one, whisper or join from it (friend_panel);
//   - the Friends dialog: your friends, the requests both ways, whom you block, and the inbox of
//     messages left while you were away (social_modal).
//
// The server keeps all of it (Server/Social.cpp); this is what the player sees and asks.
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <ctime>

namespace lsf {

using namespace proto;

namespace {

constexpr VanU32 kInk = VAN_COL32(230, 230, 226, 255);
constexpr VanU32 kLabel = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kLime = VAN_COL32(202, 228, 80, 255);
constexpr VanU32 kGold = VAN_COL32(224, 196, 110, 255);
constexpr VanU32 kOnline = VAN_COL32(96, 208, 72, 255);
constexpr VanU32 kWarn = VAN_COL32(236, 146, 38, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);
// The scopes' inks: the original's chat had a colour a kind.
constexpr VanU32 kWhisperInk = VAN_COL32(236, 150, 226, 255);
constexpr VanU32 kClanInk = VAN_COL32(120, 226, 168, 255);
constexpr VanU32 kGlobalInk = VAN_COL32(240, 196, 92, 255);
constexpr VanU32 kSystemInk = VAN_COL32(130, 220, 90, 255);

}  // namespace

// ── Chat ───────────────────────────────────────────────────────────────────────

ui::Line chat_view(const ChatLine& l) {
    const std::string staff = l.role >= u8(Role::GameMaster) ? "[GM] " : l.role >= u8(Role::Moderator) ? "[MOD] " : "";
    switch (ChatScope(l.scope)) {
        case ChatScope::System: return {l.text, kSystemInk};
        case ChatScope::Whisper: return {(l.to.empty() ? "[From " + l.from + "] " : "[To " + l.to + "] ") + l.text, kWhisperInk};
        case ChatScope::Clan: return {"[Clan] " + l.from + ": " + l.text, kClanInk};
        case ChatScope::Global: return {"[Global] " + staff + l.from + (l.clan.empty() ? "" : " <" + l.clan + ">") + ": " + l.text, kGlobalInk};
        default: return {staff + "[" + l.from + "] " + l.text, 0xFFFFFFFF};
    }
}

bool send_chat(App& app, const std::string& raw, ChatScope plain) {
    Session& s = app.session();
    std::string line(eng::str::trim(raw));
    if (line.empty() || !s.signed_in()) return false;
    if (line[0] != '/') {
        s.chat(plain, line);
        return true;
    }
    // A command: its word, then the rest.
    const size_t sp = line.find(' ');
    const std::string cmd = eng::str::lower(line.substr(0, sp));
    std::string rest = sp == std::string::npos ? std::string() : std::string(eng::str::trim(line.substr(sp + 1)));
    const bool in_match = app.screen() == Screen::Match;
    auto need_text = [&](const char* usage) {
        if (!rest.empty()) return true;
        ui::toast(ui::Toast::Info, "%s", usage);
        return false;
    };
    if (cmd == "/w" || cmd == "/whisper" || cmd == "/m" || cmd == "/msg") {
        const size_t gap = rest.find(' ');
        if (gap == std::string::npos) return ui::toast(ui::Toast::Info, "Whisper: /w name text"), false;
        s.chat(ChatScope::Whisper, std::string(eng::str::trim(rest.substr(gap + 1))), rest.substr(0, gap));
        return true;
    }
    if (cmd == "/r" || cmd == "/reply") {
        if (s.last_whisper_from.empty()) return ui::toast(ui::Toast::Info, "Nobody has whispered you yet."), false;
        if (!need_text("Answer the last whisper: /r text")) return false;
        s.chat(ChatScope::Whisper, rest, s.last_whisper_from);
        return true;
    }
    if (cmd == "/c" || cmd == "/clan") {
        if (!need_text("Your clan: /c text")) return false;
        s.chat(ChatScope::Clan, rest);
        return true;
    }
    if (cmd == "/g" || cmd == "/global") {
        if (!need_text("Global chat: /g text")) return false;
        if (!s.global_chat()) return ui::toast(ui::Toast::Info, "Global chat is off: turn it on beside the chat box or in Options."), false;
        s.chat(ChatScope::Global, rest);
        return true;
    }
    if (cmd == "/t" || cmd == "/team") {
        if (!need_text("Your team: /t text")) return false;
        s.chat(in_match || s.room ? ChatScope::Team : plain, rest);
        return true;
    }
    if (cmd == "/a" || cmd == "/all") {
        if (!need_text("Everyone here: /a text")) return false;
        s.chat(in_match ? ChatScope::All : plain, rest);
        return true;
    }
    ui::toast(ui::Toast::Info, "Unknown command %s: try /w /r /c /g /t /a", cmd.c_str());
    return false;
}

std::string ago(App& app, u64 t) {
    if (!t) return "a while ago";
    const u64 server = app.session().server_now(app.now());
    const u64 now = server ? server : u64(std::time(nullptr));
    const u64 d = now > t ? now - t : 0;
    if (d < 60) return "just now";
    if (d < 3600) return eng::str::format("%llu min ago", (unsigned long long)(d / 60));
    if (d < 86400) return eng::str::format("%llu h ago", (unsigned long long)(d / 3600));
    return eng::str::format("%llu day%s ago", (unsigned long long)(d / 86400), d / 86400 == 1 ? "" : "s");
}

int social_waiting(const Session& s) { return s.friend_requests() + s.unread_mail(); }

namespace {

// A friend on another server than yours (§14.3): joining them switches servers (FR-2).
bool elsewhere(const Session& s, const FriendEntry& e) { return e.server && (!s.server_info || e.server != s.server_info->server_id); }

// Whether "Join" can take you to them: a waiting room with a seat here, or anywhere they show
// (another channel, another server: TVAS hands the intent over, FR-2). Only friends who show
// where they are (FR-3).
bool may_join(const Session& s, const FriendEntry& e) {
    if (e.state != u8(FriendState::Friend) || !e.online) return false;
    if (elsewhere(s, e)) return true;
    return e.joinable;
}

}  // namespace

void open_social(App& app, int tab, const std::string& whisper_to) {
    ScreenState& st = app.state();
    st.social_open = true;
    if (tab >= 0) st.social_tab = tab;
    if (!whisper_to.empty()) st.whisper_to = whisper_to;
}

// ── A soldier's menu ───────────────────────────────────────────────────────────

void open_soldier_menu(App& app, u32 id, const std::string& name, const std::string& clan) {
    if (name.empty()) return;
    // Your own name: your own ID card (there is nothing else to ask of yourself).
    if (name == app.session().profile.code_name) return open_id_card(app, name);
    ScreenState& st = app.state();
    st.menu_id = id, st.menu_name = name, st.menu_clan = clan;
    st.menu_asked = true;
}

void soldier_menu(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (st.menu_asked) {
        VanGui::OpenPopup("##soldier");
        st.menu_asked = false;
    }
    if (!VanGui::BeginPopup("##soldier")) return;
    const std::string& name = st.menu_name;
    if (name.empty()) {   // put away from outside (a test)
        VanGui::CloseCurrentPopup();
        VanGui::EndPopup();
        return;
    }
    // What they are to you now: a friend, asking, asked, blocked, or nothing yet.
    int state = -1;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries)
            if (e.name == name) state = e.state;
    VanGui::TextDisabled("%s", name.c_str());
    VanGui::Separator();
    if (VanGui::MenuItem("ID card")) open_id_card(app, name);
    if (VanGui::MenuItem("Whisper")) {
        st.chat_scope = 1;
        st.chat_line = name + " ";
    }
    if (state == int(FriendState::Friend)) {
        if (VanGui::MenuItem("Remove friend")) s.friend_action(FriendOp::Remove, name);
    } else if (state == int(FriendState::Incoming)) {
        if (VanGui::MenuItem("Accept friend request")) s.friend_action(FriendOp::Accept, name);
    } else if (state == int(FriendState::Outgoing)) {
        if (VanGui::MenuItem("Take back friend request")) s.friend_action(FriendOp::Cancel, name);
    } else if (state != int(FriendState::Blocked)) {
        if (VanGui::MenuItem("Add friend")) s.friend_action(FriendOp::Add, name);
    }
    // From a room: into it (the server says why not, when it cannot be).
    if (s.room && state != int(FriendState::Blocked)) {
        bool here = false;
        for (const RoomMember& m : s.room->members) here |= m.name == name;
        if (VanGui::MenuItem("Invite to room", nullptr, false, !here)) s.room_invite(name);
    }
    // Lieutenants and above: into your clan (greyed for a soldier who has one).
    if (s.clan && s.clan->member && clan_may(ClanRank(s.clan->my_rank), ClanPower::Invite))
        if (VanGui::MenuItem("Ask into clan", nullptr, false, st.menu_clan.empty())) s.clan_invite(name);
    VanGui::Separator();
    if (state == int(FriendState::Blocked)) {
        if (VanGui::MenuItem("Unblock")) s.friend_action(FriendOp::Unblock, name);
    } else if (VanGui::MenuItem("Block")) {
        s.friend_action(FriendOp::Block, name);
    }
    if (st.menu_id && VanGui::MenuItem("Report...")) {
        st.report_open = true;
        st.report_player = st.menu_id;
        st.report_name = name;
    }
    VanGui::EndPopup();
}

// ── A room's invitations ───────────────────────────────────────────────────────

// The room's Invite plate: friends who are on and in no room (wherever they are), then the
// channel's lobby, to pick from; or a code name. The server says why not when it cannot be.
void room_invite_dialog(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 292, Y0 = 150, X1 = 732, Y1 = 618;
    if (!s.room || !ui::dialog_begin("Invite", X0, Y0, X1, Y1, "INVITE TO THE ROOM", &st.invite_open)) {
        st.invite_open = false;
        return;
    }
    struct Soldier {
        std::string name, where, clan;
        u32 xp = 0;
        bool is_friend = false;
    };
    std::vector<Soldier> list;
    auto in_room = [&](const std::string& name) {
        for (const RoomMember& m : s.room->members)
            if (m.name == name) return true;
        return false;
    };
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries)
            if (e.state == u8(FriendState::Friend) && e.online && e.room == 0 && !in_room(e.name)) list.push_back({e.name, e.where, e.clan, e.xp, true});
    for (const auto& [id, u] : s.users) {
        if (id == s.session_id || u.room != 0 || in_room(u.name)) continue;
        if (std::any_of(list.begin(), list.end(), [&](const Soldier& o) { return o.name == u.name; })) continue;
        list.push_back({u.name, "Lobby", u.clan, u.xp, false});
    }
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == st.invite_sel) sel = int(i);
    const float x0 = X0 + 18, x1 = X1 - 18;
    ui::heading(x0, Y0 + 36, x1, eng::str::format("SOLDIERS FREE TO COME  %zu", list.size()));
    const double now = app.now();
    auto asked = [&](const std::string& name) {
        const auto it = st.invited_at.find(name);
        return it != st.invited_at.end() && now - it->second < 10.0;
    };
    const int clicked = ui::rows(9870, x0, Y0 + 58, x1, Y1 - 124, int(list.size()), 26, sel, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const Soldier& o = list[size_t(i)];
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(o.xp)), rx0 + 6, ry0 + 4, rx0 + 24, ry0 + 22);
        ui::text_at(rx0 + 30, ry0, rx0 + 170, ry1, o.name, o.is_friend ? kLime : kInk, ui::Align::Left, true);
        if (asked(o.name)) ui::text_at(rx0 + 170, ry0, rx1 - 6, ry1, "invited", kGold, ui::Align::Right, true, 11.0f);
        else ui::text_at(rx0 + 170, ry0, rx1 - 6, ry1, o.is_friend ? "Friend  -  " + o.where : o.where, o.is_friend ? kLabel : kDim, ui::Align::Right, false, 11.0f);
    });
    if (clicked >= 0) st.invite_sel = list[size_t(clicked)].name, sel = clicked;
    if (list.empty())
        ui::text_wrapped(x0 + 12, Y0 + 110, x1 - 12, Y0 + 170, "Nobody is free just now: your friends who are on are in rooms, and the lobby is empty. A code name below asks anyone.",
                         kDim, 12.0f);
    const Soldier* picked = sel >= 0 ? &list[size_t(sel)] : nullptr;
    auto invite = [&](const std::string& name) {
        s.room_invite(name);
        st.invited_at[name] = now;
    };
    const bool full = int(s.room->members.size()) >= int(s.room->settings.max_players);
    const bool can_pick = picked && !asked(picked->name) && !full;
    if ((ui::text_button(9871, x0, Y1 - 116, x0 + 130, Y1 - 92, "Invite", can_pick, full ? "The room is full" : !picked ? "Pick a soldier" : nullptr) ||
         (clicked >= 0 && VanGui::IsMouseDoubleClicked(0) && can_pick)) &&
        picked)
        invite(picked->name);
    // Or anyone, by code name.
    ui::text_at(x0, Y1 - 82, x0 + 90, Y1 - 62, "Code name", kLabel, ui::Align::Left, true);
    const bool enter = ui::edit_at(9872, x0 + 90, Y1 - 82, x1 - 96, Y1 - 62, st.invite_name, int(kNameMax), "anyone signed in, wherever they are");
    const bool can_name = !st.invite_name.empty() && !full;
    if ((ui::text_button(9873, x1 - 90, Y1 - 84, x1, Y1 - 60, "Invite", can_name) || (enter && can_name)) && can_name) {
        invite(std::string(eng::str::trim(st.invite_name)));
        st.invite_name.clear();
    }
    if (ui::kit_button(9874, "close_1", X1 - 91, Y1 - 50, X1 - 18, Y1 - 9)) {
        st.invite_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

// A room asks you in: which, who asks, what it plays. Yes takes you there.
void room_invited_modal(App& app) {
    Session& s = app.session();
    if (s.room_invites.empty()) return;
    // In a room already (the invitation crossed your own joining): it is turned down for you.
    if (s.room) {
        for (const RoomInvited& inv : s.room_invites) s.room_invite_answer(inv.room, false);
        s.room_invites.clear();
        return;
    }
    const RoomInvited inv = s.room_invites.front();
    constexpr float X0 = 302, Y0 = 262, X1 = 722, Y1 = 486;
    bool open = true;
    if (!ui::dialog_begin("Room invitation", X0, Y0, X1, Y1, "INVITATION", &open)) return;
    const Mode mode = Mode(std::min<u8>(inv.mode, u8(Mode::Count) - 1));
    ui::well(X0 + 18, Y0 + 38, X0 + 150, Y0 + 137);
    ui::picture_at(app.atlas().map_picture(inv.map), X0 + 20, Y0 + 40, X0 + 148, Y0 + 135);
    ui::text_at(X0 + 164, Y0 + 38, X1 - 18, Y0 + 58, eng::str::format("%s invites you to a room", inv.from.c_str()), kGold, ui::Align::Left, true, 13.0f);
    ui::text_at(X0 + 164, Y0 + 62, X1 - 18, Y0 + 80, eng::str::format("%03u  %s", unsigned(inv.room), inv.title.c_str()), kInk, ui::Align::Left, true);
    ui::text_at(X0 + 164, Y0 + 82, X1 - 18, Y0 + 100, std::string(mode_name(mode)) + "  -  " + app.map_title(inv.map), kLabel, ui::Align::Left, true);
    ui::text_at(X0 + 164, Y0 + 102, X1 - 18, Y0 + 120,
                eng::str::format("%u / %u soldiers, %s", unsigned(inv.players), unsigned(inv.max_players), inv.playing ? "the game under way" : "waiting to start"),
                inv.playing ? kWarn : kOnline, ui::Align::Left, true);
    if (inv.channel != s.channel && !inv.channel_name.empty())
        ui::text_at(X0 + 164, Y0 + 122, X1 - 18, Y0 + 138, "In " + inv.channel_name + ": yes takes you there.", kDim, ui::Align::Left, true, 11.0f);
    ui::text_at(X0 + 18, Y0 + 146, X1 - 18, Y0 + 162, "The lobby's Accept Invites plate turns invitations off.", kDim, ui::Align::Center, true, 11.0f);
    const float BY = Y1 - 44, cx = (X0 + X1) * 0.5f;
    int answer = -1;
    if (ui::kit_button(9880, "yes_1", cx - 88, BY, cx - 5, BY + 29)) answer = 1;
    if (ui::kit_button(9881, "no_1", cx + 5, BY, cx + 88, BY + 29) || !open) answer = 0;
    if (answer >= 0) {
        s.room_invite_answer(inv.room, answer == 1);
        s.room_invites.pop_front();
        // A yes answers the rest with a no: one room at a time.
        if (answer == 1) {
            for (const RoomInvited& other : s.room_invites) s.room_invite_answer(other.room, false);
            s.room_invites.clear();
        }
        if (open) ui::dialog_close();
    }
    ui::dialog_end();
}

// ── The lobby's Friend tab ─────────────────────────────────────────────────────

void friend_panel(App& app, const ui::Page& page, int list_id) {
    ScreenState& st = app.state();
    Session& s = app.session();
    static const std::string titles[] = {"Codename", "Location"};
    (void)ui::list(page, list_id, {}, -1, titles);   // the script's frame and its titles
    float x0, y0, x1, y1;
    if (!page.rect(list_id, x0, y0, x1, y1)) return;
    // Those asking first, then friends signed in, then the rest (the server's order).
    std::vector<const FriendEntry*> shown;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries)
            if (e.state == u8(FriendState::Friend) || e.state == u8(FriendState::Incoming)) shown.push_back(&e);
    const float ry0 = y0 + 22, ry1 = y1 - 34;
    int sel = -1;
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i]->name == st.friend_sel) sel = int(i);
    if (shown.empty()) {
        ui::text_wrapped(x0 + 10, ry0 + 20, x1 - 10, ry0 + 90,
                         s.friends ? "No friends yet. Open Friends below and ask a soldier by code name, or right-click a name in the All list."
                                   : "Asking the server...",
                         kDim, 12.0f);
    } else {
        const int clicked = ui::rows(9950, x0 + 2, ry0, x1 - 2, ry1, int(shown.size()), 24, sel, [&](int i, float rx0, float rty0, float rx1, float rty1, bool, bool) {
            const FriendEntry& e = *shown[size_t(i)];
            const bool asking = e.state == u8(FriendState::Incoming);
            VanDrawList* dl = VanGui::GetWindowDrawList();
            dl->AddCircleFilled(ui::pg(rx0 + 9, (rty0 + rty1) * 0.5f), ui::pgy(3.5f), asking ? kGold : e.online ? kOnline : VAN_COL32(90, 90, 86, 255), 12);
            ui::text_at(rx0 + 18, rty0, rx0 + 150, rty1, e.name, asking ? kGold : e.online ? kInk : kDim, ui::Align::Left, true);
            const std::string where = asking ? "asks to be your friend" : e.online ? e.where : "Offline, " + ago(app, e.last_seen);
            ui::text_at(rx0 + 150, rty0, rx1 - 4, rty1, where, asking ? kGold : e.online ? kLabel : kDim, ui::Align::Right, false, 11.0f);
        });
        if (clicked >= 0) st.friend_sel = shown[size_t(clicked)]->name, sel = clicked;
        // A double click whispers them: the chat box turns to Whisper with their name in it.
        if (clicked >= 0 && VanGui::IsMouseDoubleClicked(0) && shown[size_t(clicked)]->state == u8(FriendState::Friend)) {
            st.chat_scope = 1;
            st.chat_line = shown[size_t(clicked)]->name + " ";
        }
    }
    const FriendEntry* picked = sel >= 0 ? shown[size_t(sel)] : nullptr;
    const float by0 = y1 - 30, by1 = y1 - 6;
    const float w = (x1 - x0 - 16) / 3.0f;
    auto bx = [&](int k) { return x0 + 4 + (w + 4) * float(k); };
    if (picked && picked->state == u8(FriendState::Incoming)) {
        if (ui::text_button(9951, bx(0), by0, bx(0) + w, by1, "Accept", true, "Be friends")) s.friend_action(FriendOp::Accept, picked->name);
        if (ui::text_button(9952, bx(1), by0, bx(1) + w, by1, "Decline")) s.friend_action(FriendOp::Decline, picked->name);
    } else {
        if (ui::text_button(9951, bx(0), by0, bx(0) + w, by1, "Whisper", picked != nullptr, "Whisper them (left in their inbox if they are away)") && picked) {
            st.chat_scope = 1;
            st.chat_line = picked->name + " ";
        }
        // Into their room: here, in another channel, or on another server (asked first, FR-2).
        const bool can_join = picked && may_join(s, *picked) && !s.room;
        const char* why = !picked ? "Pick a friend"
                          : s.room ? "Leave your room first"
                          : picked && elsewhere(s, *picked) ? "Switch servers to join them (you are asked first)"
                                                            : "Into their room, beside them";
        if (ui::text_button(9952, bx(1), by0, bx(1) + w, by1, "Join", can_join, why) && picked) {
            if (!elsewhere(s, *picked) && picked->channel == s.channel) s.join_room(picked->room);
            else s.join_friend(picked->name);
        }
    }
    const int waiting = social_waiting(s);
    if (ui::text_button(9953, bx(2), by0, x1 - 4, by1, waiting ? eng::str::format("Friends (%d)", waiting) : std::string("Friends"), true,
                        "Your friends, requests, whom you block, and your inbox"))
        open_social(app, -1);
}

// ── The header's Friends plate ─────────────────────────────────────────────────

bool friends_plate(App& app, int key, float x0, float y0, float x1, float y1) {
    const Session& s = app.session();
    const bool clicked = ui::text_button(key, x0, y0, x1, y1, "Friends", s.signed_in(), "Your friends, requests, whom you block, and your inbox");
    if (const int waiting = social_waiting(s); waiting > 0) {
        // A count where something waits (a request, a message), like the Rewards plate's dot.
        VanDrawList* dl = VanGui::GetWindowDrawList();
        const float pulse = 0.65f + 0.35f * std::sin(float(app.now()) * 4.0f);
        dl->AddCircleFilled(ui::pg(x1 - 4, y0 + 4), ui::pgy(7.0f), VAN_COL32(220, 60, 50, int(255 * pulse)), 16);
        ui::text_at(x1 - 11, y0 - 3, x1 + 3, y0 + 11, waiting > 9 ? "9+" : std::to_string(waiting), 0xFFFFFFFF, ui::Align::Center, true, 9.5f);
    }
    if (clicked) open_social(app, -1);
    return clicked;
}

// ── Who sees you, who writes to you (D25, D28) ─────────────────────────────────

float privacy_rows(App& app, float x0, float y, float x1, bool stacked) {
    Session& s = app.session();
    static const char* const shown[] = {"Show where I am", "Show only that I'm on", "Appear offline"};
    static const char* const from[] = {"Anyone", "Friends and clanmates", "Friends only"};
    const int p = std::clamp(int(s.tv.presence), 0, 2), m = std::clamp(int(s.tv.messages_from), 0, 2);
    const bool on = s.tv.signed_in;
    int np = p, nm = m;
    if (stacked) {
        ui::text_at(x0, y, x0 + 104, y + 18, "Others see", on ? kLabel : kDim, ui::Align::Left, true, 12.0f);
        if (const int d = ui::arrows(13300, x0 + 104, y, x1, y + 18, shown[p], on); d) np = (p + d + 3) % 3;
        y += 26;
        ui::text_at(x0, y, x0 + 104, y + 18, "Messages from", on ? kLabel : kDim, ui::Align::Left, true, 12.0f);
        if (const int d = ui::arrows(13301, x0 + 104, y, x1, y + 18, from[m], on); d) nm = (m + d + 3) % 3;
        y += 26;
    } else {
        const float mid = x0 + (x1 - x0) * 0.47f;
        ui::text_at(x0, y, x0 + 80, y + 18, "Others see", on ? kLabel : kDim, ui::Align::Left, true, 12.0f);
        if (const int d = ui::arrows(13300, x0 + 80, y, mid - 12, y + 18, shown[p], on); d) np = (p + d + 3) % 3;
        ui::text_at(mid, y, mid + 100, y + 18, "Messages from", on ? kLabel : kDim, ui::Align::Left, true, 12.0f);
        if (const int d = ui::arrows(13301, mid + 100, y, x1, y + 18, from[m], on); d) nm = (m + d + 3) % 3;
        y += 26;
    }
    if (np != p || nm != m) s.set_social(PresenceMode(np), MessagesFrom(nm));
    return y;
}

// ── The Friends dialog ─────────────────────────────────────────────────────────

namespace {

void friends_list_tab(App& app, float x0, float y0, float x1, float y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    std::vector<const FriendEntry*> list;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries)
            if (e.state == u8(FriendState::Friend)) list.push_back(&e);
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i]->name == st.friend_sel) sel = int(i);
    int online = 0;
    for (const FriendEntry* e : list) online += e->online;
    ui::heading(x0, y0, x1, eng::str::format("FRIENDS  %d online of %zu (up to %zu)", online, list.size(), kFriendsMax));
    // Who sees where you are, and who may write to you (D25, ML-7): kept with your account.
    privacy_rows(app, x0, y0 + 24, x1);
    const float ly0 = y0 + 52;
    if (list.empty()) {
        ui::well(x0, ly0, x1, y1 - 40);
        ui::text_at(x0, ly0 + 100, x1, ly0 + 120, "No friends yet: ask a soldier by code name above.", kDim, ui::Align::Center, true);
    } else {
        const int clicked = ui::rows(9960, x0, ly0, x1, y1 - 40, int(list.size()), 26, sel, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
            const FriendEntry& e = *list[size_t(i)];
            VanDrawList* dl = VanGui::GetWindowDrawList();
            dl->AddCircleFilled(ui::pg(rx0 + 10, (ry0 + ry1) * 0.5f), ui::pgy(4.0f), e.online ? kOnline : VAN_COL32(90, 90, 86, 255), 12);
            ui::picture_at(app.atlas().rank_badge(rank_for_xp(e.xp)), rx0 + 20, ry0 + 4, rx0 + 38, ry0 + 22);
            ui::text_at(rx0 + 44, ry0, rx0 + 190, ry1, e.name, e.online ? kInk : kDim, ui::Align::Left, true);
            ui::text_at(rx0 + 190, ry0, rx0 + 300, ry1, e.clan.empty() ? "-" : e.clan, e.clan.empty() ? kDim : kClanInk, ui::Align::Left, true, 11.0f);
            ui::text_at(rx0 + 300, ry0, rx1 - 6, ry1, e.online ? e.where : "Offline, last seen " + ago(app, e.last_seen), e.online ? kLabel : kDim,
                        ui::Align::Right, false, 11.0f);
        });
        if (clicked >= 0) st.friend_sel = list[size_t(clicked)]->name, sel = clicked;
    }
    const FriendEntry* picked = sel >= 0 ? list[size_t(sel)] : nullptr;
    const float by0 = y1 - 32, by1 = y1 - 6;
    if (ui::text_button(9961, x0, by0, x0 + 120, by1, "Whisper", picked != nullptr, "Write to them below (left in their inbox if they are away)") && picked)
        st.whisper_to = picked->name;
    if (s.room) {
        // From a room the plate asks them into yours instead.
        const bool can = picked && picked->online && picked->room == 0;
        if (ui::text_button(9962, x0 + 126, by0, x0 + 246, by1, "Invite to room", can,
                            !picked ? "Pick a friend" : !picked->online ? "They are away" : picked->room ? "They are in a room already" : "Ask them into your room") &&
            picked) {
            s.room_invite(picked->name);
            st.invited_at[picked->name] = app.now();
        }
    } else {
        // Here, in another channel, or on another server: the game asks before it switches (FR-2).
        const bool joinable = picked && may_join(s, *picked);
        const bool away = picked && elsewhere(s, *picked);
        if (ui::text_button(9962, x0 + 126, by0, x0 + 246, by1, away ? "Join them there" : "Join their room", joinable,
                            !picked                ? "Pick a friend"
                            : away                 ? "On another server: you are asked before the game switches"
                            : !picked->joinable    ? "They are in no room you could join"
                                                   : "Into their waiting room") &&
            picked) {
            if (!away && picked->channel == s.channel) s.join_room(picked->room);
            else s.join_friend(picked->name);
            st.social_open = false;
            ui::dialog_close();
        }
    }
    const bool can_invite = picked && s.clan && s.clan->member && picked->clan.empty() && clan_may(ClanRank(s.clan->my_rank), ClanPower::Invite);
    if (ui::text_button(9963, x0 + 252, by0, x0 + 372, by1, "Ask into clan", can_invite, "Ask them into your clan (Lieutenant and up)") && picked)
        s.clan_invite(picked->name);
    // Removing and blocking ask twice.
    const bool armed = app.now() - st.social_armed < 3.0 && st.social_armed_for == (picked ? picked->name : std::string());
    if (ui::text_button(9964, x1 - 244, by0, x1 - 124, by1, armed && st.social_armed_what == 1 ? "Sure? Remove" : "Remove", picked != nullptr) && picked) {
        if (armed && st.social_armed_what == 1) s.friend_action(FriendOp::Remove, picked->name), st.social_armed = -10;
        else st.social_armed = app.now(), st.social_armed_for = picked->name, st.social_armed_what = 1;
    }
    if (ui::text_button(9965, x1 - 120, by0, x1, by1, armed && st.social_armed_what == 2 ? "Sure? Block" : "Block", picked != nullptr,
                        "No requests, whispers, messages or global lines from them") &&
        picked) {
        if (armed && st.social_armed_what == 2) s.friend_action(FriendOp::Block, picked->name), st.social_armed = -10;
        else st.social_armed = app.now(), st.social_armed_for = picked->name, st.social_armed_what = 2;
    }
}

void requests_tab(App& app, float x0, float y0, float x1, float y1) {
    Session& s = app.session();
    std::vector<const FriendEntry*> in, out;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries) {
            if (e.state == u8(FriendState::Incoming)) in.push_back(&e);
            if (e.state == u8(FriendState::Outgoing)) out.push_back(&e);
        }
    const float mid = (y0 + y1) * 0.5f;
    ui::heading(x0, y0, x1, eng::str::format("ASKING YOU  %zu", in.size()));
    ui::rows(9970, x0, y0 + 22, x1, mid - 12, int(in.size()), 28, -1, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const FriendEntry& e = *in[size_t(i)];
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(e.xp)), rx0 + 6, ry0 + 5, rx0 + 24, ry0 + 23);
        ui::text_at(rx0 + 30, ry0, rx0 + 200, ry1, e.name, kGold, ui::Align::Left, true);
        ui::text_at(rx0 + 200, ry0, rx0 + 320, ry1, e.clan.empty() ? "" : e.clan, kClanInk, ui::Align::Left, true, 11.0f);
        if (ui::text_button(9971 + i * 3, rx1 - 250, ry0 + 3, rx1 - 170, ry1 - 3, "Accept")) s.friend_action(FriendOp::Accept, e.name);
        if (ui::text_button(9972 + i * 3, rx1 - 166, ry0 + 3, rx1 - 86, ry1 - 3, "Decline")) s.friend_action(FriendOp::Decline, e.name);
        if (ui::text_button(9973 + i * 3, rx1 - 82, ry0 + 3, rx1 - 4, ry1 - 3, "Block")) s.friend_action(FriendOp::Block, e.name);
    });
    if (in.empty()) ui::text_at(x0, y0 + 40, x1, y0 + 60, "Nobody is asking you right now.", kDim, ui::Align::Center, true);
    ui::heading(x0, mid, x1, eng::str::format("YOU ASKED  %zu", out.size()));
    ui::rows(9990, x0, mid + 22, x1, y1, int(out.size()), 28, -1, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const FriendEntry& e = *out[size_t(i)];
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(e.xp)), rx0 + 6, ry0 + 5, rx0 + 24, ry0 + 23);
        ui::text_at(rx0 + 30, ry0, rx0 + 200, ry1, e.name, kInk, ui::Align::Left, true);
        ui::text_at(rx0 + 200, ry0, rx1 - 90, ry1, "waiting for an answer", kDim, ui::Align::Left, false, 11.0f);
        if (ui::text_button(9991 + i, rx1 - 82, ry0 + 3, rx1 - 4, ry1 - 3, "Cancel")) s.friend_action(FriendOp::Cancel, e.name);
    });
    if (out.empty()) ui::text_at(x0, mid + 40, x1, mid + 60, "You are not waiting on anyone.", kDim, ui::Align::Center, true);
}

void blocked_tab(App& app, float x0, float y0, float x1, float y1) {
    Session& s = app.session();
    std::vector<const FriendEntry*> list;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries)
            if (e.state == u8(FriendState::Blocked)) list.push_back(&e);
    ui::heading(x0, y0, x1, eng::str::format("BLOCKED  %zu", list.size()));
    ui::text_at(x0, y0 + 20, x1, y0 + 36, "Those you block cannot ask you, whisper you, leave you messages or be heard by you on global chat.", kDim,
                ui::Align::Left, true, 11.0f);
    ui::rows(10010, x0, y0 + 42, x1, y1, int(list.size()), 28, -1, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const FriendEntry& e = *list[size_t(i)];
        ui::text_at(rx0 + 10, ry0, rx0 + 240, ry1, e.name, kInk, ui::Align::Left, true);
        if (ui::text_button(10011 + i, rx1 - 92, ry0 + 3, rx1 - 4, ry1 - 3, "Unblock")) s.friend_action(FriendOp::Unblock, e.name);
    });
    if (list.empty()) ui::text_at(x0, y0 + 80, x1, y0 + 100, "You block nobody.", kDim, ui::Align::Center, true);
}

// ML-3: how long a message has left, by TVAS's clock (ML-2).
std::string kept_text(const MailItem& m, u64 now) {
    if (m.saved) return "Saved";
    if (!m.expires) return {};
    const u64 left = m.expires > now ? m.expires - now : 0;
    if (left >= 86400) return eng::str::format("Deleted in %llu days", (unsigned long long)((left + 86399) / 86400));
    if (left >= 3600) return eng::str::format("Deleted in %llu h", (unsigned long long)(left / 3600));
    return "Deleted within the hour";
}

// The inbox: the soldiers' messages, or Team Vanilla's own (ML-4: refunds, warnings, staff), each
// kept 7 days unless saved (ML-2, ML-5: 30 unsaved, 50 saved).
void inbox_tab(App& app, float x0, float y0, float x1, float y1, bool system) {
    ScreenState& st = app.state();
    Session& s = app.session();
    std::vector<MailItem> items;
    int unread = 0, unsaved = 0, saved = 0;
    if (s.mailbox)
        for (const MailItem& m : s.mailbox->items) {
            if (m.saved) ++saved;
            else if (!m.system) ++unsaved;
            if (m.system != system) continue;
            unread += !m.read;
            items.push_back(m);
        }
    const u16 saved_max = s.mailbox ? s.mailbox->saved_max : 50, unsaved_max = s.mailbox ? s.mailbox->unsaved_max : 30;
    if (system) ui::heading(x0, y0, x1, eng::str::format("FROM TEAM VANILLA  %d unread", unread));
    else ui::heading(x0, y0, x1, eng::str::format("INBOX  %d unread, %d of %u kept, %d of %u saved", unread, unsaved, unsigned(unsaved_max), saved, unsigned(saved_max)));
    ui::text_at(x0, y0 + 20, x1, y0 + 36,
                system ? "Refunds, a server's closing, a reset password, staff: these always reach you. Kept 7 days unless you save them."
                       : "Messages are kept 7 days unless you save them. A message you report goes to SFLegacy Staff with its words.",
                kDim, ui::Align::Left, true, 11.0f);
    const u64 now = s.tv_now();
    int sel = -1;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == st.mail_sel) sel = int(i);
    const int clicked = ui::rows(system ? 13320 : 10030, x0, y0 + 42, x1, y1 - 40, int(items.size()), 40, sel, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const MailItem& m = items[size_t(i)];
        if (!m.read) VanGui::GetWindowDrawList()->AddCircleFilled(ui::pg(rx0 + 8, ry0 + 11), ui::pgy(3.5f), kGold, 12);
        ui::text_at(rx0 + 16, ry0 + 2, rx0 + 200, ry0 + 20, m.from.empty() ? std::string("Team Vanilla") : m.from, m.read ? kLabel : kGold, ui::Align::Left, true);
        const std::string kept = kept_text(m, now);
        const bool soon = !m.saved && m.expires && m.expires < now + 86400;
        ui::text_at(rx0 + 200, ry0 + 2, rx1 - 150, ry0 + 20, kept, m.saved ? kLime : soon ? kWarn : kDim, ui::Align::Right, false, 11.0f);
        ui::text_at(rx1 - 140, ry0 + 2, rx1 - 6, ry0 + 20, ago(app, m.time), kDim, ui::Align::Right, false, 11.0f);
        ui::text_at(rx0 + 16, ry0 + 19, rx1 - 6, ry1 - 2, m.text, m.read ? kDim : kInk, ui::Align::Left, false, 12.0f);
    });
    if (clicked >= 0) {
        st.mail_sel = items[size_t(clicked)].id;
        sel = clicked;
        if (!items[size_t(clicked)].read) s.mail_action(MailOp::Read, items[size_t(clicked)].id);
    }
    if (items.empty())
        ui::text_at(x0, y0 + 130, x1, y0 + 150, system ? "Nothing from Team Vanilla." : "Nothing here. A whisper to you while you are away waits here.", kDim, ui::Align::Center, true);
    const MailItem* picked = sel >= 0 ? &items[size_t(sel)] : nullptr;
    const float by0 = y1 - 32, by1 = y1 - 6;
    float bx = x0;
    auto next = [&](float w) {
        const float at = bx;
        bx += w + 6;
        return at;
    };
    if (!system) {
        const float rx = next(84);
        if (ui::text_button(10031, rx, by0, rx + 84, by1, "Reply", picked != nullptr) && picked) st.whisper_to = picked->from;
    }
    // ML-5: Save stops at 50; Unsave puts it back on the 7 days.
    const bool full = saved >= int(saved_max);
    const float sx = next(84);
    if (picked && picked->saved) {
        if (ui::text_button(13310, sx, by0, sx + 84, by1, "Unsave", true, "Back on the 7 days, counted from now")) s.mail_action(MailOp::Unsave, picked->id);
    } else if (ui::text_button(13310, sx, by0, sx + 84, by1, "Save", picked && !full, full ? "50 saved already: delete one first" : "Keep it until you delete it") && picked) {
        s.mail_action(MailOp::Save, picked->id);
    }
    const float dx = next(84);
    if (ui::text_button(10032, dx, by0, dx + 84, by1, "Delete", picked != nullptr) && picked) s.mail_action(MailOp::Delete, picked->id), st.mail_sel = 0;
    if (!system) {
        // ML-6: to SFLegacy Staff, kept for them past its 7 days. Asks twice.
        const bool armed = app.now() - st.social_armed < 3.0 && st.social_armed_what == 3 && picked && st.social_armed_for == std::to_string(picked->id);
        const float rx = next(96);
        if (ui::text_button(13311, rx, by0, rx + 96, by1, armed ? "Sure? Report" : "Report", picked != nullptr, "Send this message to SFLegacy Staff") && picked) {
            if (armed) s.mail_action(MailOp::Report, picked->id), st.social_armed = -10;
            else st.social_armed = app.now(), st.social_armed_for = std::to_string(picked->id), st.social_armed_what = 3;
        }
    }
    if (ui::text_button(10033, x1 - 236, by0, x1 - 120, by1, "Mark all read", unread > 0)) s.mail_action(MailOp::ReadAll);
    if (ui::text_button(10034, x1 - 114, by0, x1, by1, "Delete read", !items.empty(), "Every message read and not saved")) s.mail_action(MailOp::DeleteRead), st.mail_sel = 0;
}

}  // namespace

void social_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 142, Y0 = 92, X1 = 882, Y1 = 676;
    if (!ui::dialog_begin("Friends", X0, Y0, X1, Y1, "FRIENDS", &st.social_open)) {
        st.social_open = false;
        return;
    }
    const int requests = s.friend_requests();
    int blocked = 0, unread = 0, unread_system = 0;
    if (s.friends)
        for (const FriendEntry& e : s.friends->entries) blocked += e.state == u8(FriendState::Blocked);
    if (s.mailbox)
        for (const MailItem& m : s.mailbox->items) (m.system ? unread_system : unread) += !m.read;
    const std::string tabs[] = {"Friends", requests ? eng::str::format("Requests (%d)", requests) : std::string("Requests"),
                                blocked ? eng::str::format("Blocked (%d)", blocked) : std::string("Blocked"),
                                unread ? eng::str::format("Inbox (%d)", unread) : std::string("Inbox"),
                                unread_system ? eng::str::format("System (%d)", unread_system) : std::string("System")};
    for (int t = 0; t < 5; ++t)
        if (ui::tab_button(9940 + t, X0 + 16 + float(t) * 86, Y0 + 32, X0 + 98 + float(t) * 86, Y0 + 58, tabs[t], st.social_tab == t)) st.social_tab = t;
    // Ask a soldier, by code name.
    const bool enter = ui::edit_at(9945, X1 - 286, Y0 + 34, X1 - 112, Y0 + 56, st.friend_add, int(kNameMax), "a code name");
    if ((ui::text_button(9946, X1 - 106, Y0 + 32, X1 - 16, Y0 + 58, "Add friend", !st.friend_add.empty()) || enter) && !st.friend_add.empty()) {
        s.friend_action(FriendOp::Add, std::string(eng::str::trim(st.friend_add)));
        st.friend_add.clear();
    }
    ui::fill_at(X0 + 12, Y0 + 62, X1 - 12, Y0 + 63, kRule);
    const float x0 = X0 + 20, y0 = Y0 + 74, x1 = X1 - 20, y1 = Y1 - 66;
    if (!s.friends) ui::text_at(X0, Y0 + 260, X1, Y0 + 290, "Asking the server...", kDim, ui::Align::Center, true);
    else if (st.social_tab == 0) friends_list_tab(app, x0, y0, x1, y1);
    else if (st.social_tab == 1) requests_tab(app, x0, y0, x1, y1);
    else if (st.social_tab == 2) blocked_tab(app, x0, y0, x1, y1);
    else inbox_tab(app, x0, y0, x1, y1, st.social_tab == 4);

    // A whisper, from any tab: to whom, and the line.
    ui::fill_at(X0 + 12, Y1 - 58, X1 - 12, Y1 - 57, kRule);
    ui::text_at(X0 + 20, Y1 - 46, X0 + 52, Y1 - 26, "To", kLabel, ui::Align::Left, true);
    (void)ui::edit_at(9947, X0 + 52, Y1 - 46, X0 + 200, Y1 - 26, st.whisper_to, int(kNameMax), "code name");
    const bool send = ui::edit_at(9948, X0 + 208, Y1 - 46, X1 - 210, Y1 - 26, st.whisper_text, int(kChatMax), "a whisper (left in their inbox if they are away)");
    const bool can = !st.whisper_to.empty() && !st.whisper_text.empty();
    if ((ui::text_button(9949, X1 - 204, Y1 - 48, X1 - 112, Y1 - 24, "Send", can) || send) && can) {
        s.chat(ChatScope::Whisper, st.whisper_text, std::string(eng::str::trim(st.whisper_to)));
        st.whisper_text.clear();
    }
    if (ui::kit_button(9939, "close_1", X1 - 91, Y1 - 50, X1 - 18, Y1 - 9)) {
        st.social_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

}  // namespace lsf
