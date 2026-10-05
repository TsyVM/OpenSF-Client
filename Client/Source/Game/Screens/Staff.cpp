// Staff, reports and votes on the client (TacticalFPS's StaffPanel, in this game's own kit).
//
// The staff panel (F9, or the match's Esc menu) is drawn only for a Moderator or a Game Master,
// and that is all the client's copy of the role is used for: every request is checked again on
// the server. Three tabs:
//   Reports     the queue: who, by whom, why, the match they shared -- Look up, Watch, Actioned, Dismiss
//   Accounts    a search, the account's card, and what may be done to it: kick, mute, ban and their
//               undoing; for a Game Master, the rank and the account editor (code name, SP, XP, rank,
//               what is owned)
//   Recordings  every match the server kept, newest first -- Watch downloads it and plays it back
//   Events, Rewards, Shop   a Game Master's editors (Screens/EventsEditor.cpp, Screens/ShopEditor.cpp)
//
// A player reports or calls a vote from the match's Esc menu, picking from the match's soldiers;
// the vote itself is a banner with Yes / No (F1 / F2) on every screen of the room and the match.
#include "Game/Screens/Screens.hpp"

#include "Game/App.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"
#include "Game/World/GameWorld.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <ctime>

namespace lsf {

using namespace proto;

namespace {

constexpr VanU32 kInk = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kDim = VAN_COL32(160, 160, 152, 255);
constexpr VanU32 kLime = VAN_COL32(170, 220, 70, 255);
constexpr VanU32 kWarn = VAN_COL32(240, 190, 70, 255);
constexpr VanU32 kBad = VAN_COL32(235, 90, 70, 255);

std::string date_text(u64 t) {
    if (!t) return "-";
    const std::time_t tt = std::time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    return eng::str::format("%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

std::string length_text(u64 seconds) {
    if (seconds >= 86400) return eng::str::format("%llud %lluh", (unsigned long long)(seconds / 86400), (unsigned long long)(seconds % 86400 / 3600));
    if (seconds >= 3600) return eng::str::format("%lluh %llum", (unsigned long long)(seconds / 3600), (unsigned long long)(seconds % 3600 / 60));
    return eng::str::format("%llum %02llus", (unsigned long long)(seconds / 60), (unsigned long long)(seconds % 60));
}

void label(float x, float y, std::string_view s, VanU32 c = kDim, float size = 12.0f) { ui::text_at(x, y, x + 400, y + 18, s, c, ui::Align::Left, true, size); }

// Words over up to `lines` lines of about the box's width (Tahoma at 12 is ~6.5 page units a letter).
void wrapped(float x0, float y0, float x1, const std::string& text, VanU32 c, int lines) {
    const size_t per = size_t(std::max(10.0f, (x1 - x0) / 6.5f));
    size_t at = 0;
    for (int l = 0; l < lines && at < text.size(); ++l) {
        size_t end = std::min(text.size(), at + per);
        if (end < text.size())
            if (const size_t sp = text.rfind(' ', end); sp != std::string::npos && sp > at) end = sp;
        ui::text_at(x0, y0 + float(l) * 17, x1, y0 + float(l) * 17 + 17, text.substr(at, end - at), c, ui::Align::Left, false, 12.0f);
        at = end + (end < text.size() && text[end] == ' ' ? 1 : 0);
    }
}

struct Choice {
    const char* name;
    u32 seconds;
};
constexpr Choice kMutes[] = {{"10 minutes", 600}, {"1 hour", 3600}, {"1 day", 86400}, {"7 days", 7 * 86400}};
constexpr Choice kBans[] = {{"1 hour", 3600}, {"1 day", 86400}, {"7 days", 7 * 86400}, {"30 days", 30 * 86400}, {"For good", 0}};

struct Field {
    AccountField field;
    const char* name;
    const char* hint;
    bool number;
};
// Team Vanilla's account, from SFLegacy Staff's game to TVAS (RL-1): everything universal.
constexpr Field kGlobalFields[] = {
    {AccountField::Sp, "Set SP", "SP", true},
    {AccountField::Coins, "Set Coins", "capsule coins", true},
    {AccountField::Xp, "Set XP", "experience", true},
    {AccountField::Rank, "Set rank", "rank number (0 = Trainee)", true},
    {AccountField::CodeName, "Rename", "new code name", false},
    {AccountField::GrantWeapon, "Grant weapon", "weapon code and days: A009 30", false},
    {AccountField::RevokeWeapon, "Revoke weapon", "weapon code", false},
    {AccountField::GrantForce, "Grant force", "force name and days: Delta 0", false},
    {AccountField::RevokeForce, "Revoke force", "force name", false},
    {AccountField::GrantItem, "Grant item", "item code and days: E1010 30", false},
    {AccountField::RevokeItem, "Revoke item", "item code", false},
    {AccountField::ClearLoadout, "Clear loadout", "", false},
    {AccountField::GrantBox, "Grant box", "duffle_a..d, gift...; how many after", false},
    {AccountField::Durability, "Gun's wear", "weapon code and what is left: A006 40", false},
};
// This server's own data (§6.2): its custom items, by their codes, and its loadout overrides.
constexpr Field kServerFields[] = {
    {AccountField::GrantWeapon, "Grant weapon", "x:<pack>:<name> and days (0: for good)", false},
    {AccountField::RevokeWeapon, "Revoke weapon", "x:<pack>:<name>", false},
    {AccountField::GrantForce, "Grant character", "x:<pack>:<name> and days", false},
    {AccountField::RevokeForce, "Revoke character", "x:<pack>:<name>", false},
    {AccountField::GrantItem, "Grant item", "x:<pack>:<name> and days", false},
    {AccountField::RevokeItem, "Revoke item", "x:<pack>:<name>", false},
    {AccountField::ClearLoadout, "Clear overrides", "", false},
};

// A card's account as TVAS takes it: "#12 name" -> "12".
std::string tv_token(const std::string& account) {
    if (account.empty() || account[0] != '#') return account;
    size_t end = 1;
    while (end < account.size() && account[end] >= '0' && account[end] <= '9') ++end;
    return account.substr(1, end - 1);
}

void reports_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!st.staff_asked) s.staff_list_reports(st.staff_open_only), st.staff_asked = true;
    if (ui::check(9601, X0, Y0, "Open reports only", st.staff_open_only)) st.staff_asked = false;
    if (ui::text_button(9602, X1 - 90, Y0 - 3, X1, Y0 + 19, "Refresh")) st.staff_asked = false;
    const std::vector<ReportEntry> none;
    const auto& list = s.staff_reports ? s.staff_reports->reports : none;
    const float split = X0 + 430;
    int sel_index = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (int(list[i].id) == st.staff_report_sel) sel_index = int(i);
    const int clicked = ui::rows(9603, X0, Y0 + 26, split - 8, Y1, int(list.size()), 34, sel_index,
                                 [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
                                     const ReportEntry& e = list[size_t(i)];
                                     const VanU32 c = e.state == u8(ReportState::Open) ? kInk : kDim;
                                     ui::text_at(x0 + 6, y0 + 2, x1 - 6, y0 + 17, e.target_code + "  (" + e.target + ")", c, ui::Align::Left, true, 12.0f);
                                     ui::text_at(x1 - 150, y0 + 2, x1 - 6, y0 + 17, date_text(e.time), kDim, ui::Align::Right, false, 11.0f);
                                     ui::text_at(x0 + 6, y0 + 17, x1 - 6, y1, std::string(report_reason_name(ReportReason(e.reason))) + "  by " + e.reporter,
                                                 e.reporter == "System" ? kWarn : kDim, ui::Align::Left, false, 11.0f);
                                     (void)y1;
                                 });
    if (clicked >= 0) st.staff_report_sel = int(list[size_t(clicked)].id);
    if (list.empty()) ui::text_at(X0, Y0 + 60, split - 8, Y0 + 80, s.staff_reports ? "No reports." : "Asking the server...", kDim, ui::Align::Center);
    if (sel_index < 0) {
        ui::text_at(split, Y0 + 60, X1, Y0 + 80, "Pick a report.", kDim, ui::Align::Center);
        return;
    }
    const ReportEntry& e = list[size_t(sel_index)];
    float y = Y0 + 26;
    ui::heading(split, y, X1, eng::str::format("Report #%u", e.id));
    y += 22;
    auto row = [&](const char* k, const std::string& v, VanU32 c = kInk) {
        label(split, y, k);
        ui::text_at(split + 90, y, X1, y + 18, v, c, ui::Align::Left, false, 12.0f);
        y += 20;
    };
    row("Soldier", e.target_code + " (" + e.target + ")");
    row("Reason", report_reason_name(ReportReason(e.reason)));
    row("Reported by", e.reporter, e.reporter == "System" ? kWarn : kInk);
    row("When", date_text(e.time));
    row("State", e.state == u8(ReportState::Open) ? "Open" : e.state == u8(ReportState::Actioned) ? "Actioned" : "Dismissed",
        e.state == u8(ReportState::Open) ? kLime : kDim);
    row("Match", e.match ? eng::str::format("#%u (recorded)", e.match) : std::string("none kept"));
    label(split, y, "Note");
    ui::well(split, y + 18, X1, y + 78);
    wrapped(split + 6, y + 22, X1 - 6, e.note.empty() ? std::string("(none)") : e.note, e.note.empty() ? kDim : kInk, 3);
    y += 90;
    const float bw = (X1 - split - 12) / 2;
    if (ui::text_button(9610, split, y, split + bw, y + 26, "Look up the account")) {
        st.staff_tab = 1;
        st.staff_search = e.target;
        s.staff_card.reset();
        s.staff_find(e.target);
    }
    if (ui::text_button(9611, split + bw + 12, y, X1, y + 26, "Watch the match", e.match != 0,
                        e.match ? nullptr : "The match was not recorded, or is no longer kept."))
        st.staff_recording_sel = int(e.match), s.request_replay(e.match), st.staff_tab = 2;
    y += 34;
    const bool open = e.state == u8(ReportState::Open);
    if (ui::text_button(9612, split, y, split + bw, y + 26, "Mark actioned", open)) s.staff_resolve(e.id, ReportState::Actioned), st.staff_asked = false;
    if (ui::text_button(9613, split + bw + 12, y, X1, y + 26, "Dismiss", open)) s.staff_resolve(e.id, ReportState::Dismissed), st.staff_asked = false;
}

void accounts_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    // RL-1: SFLegacy Staff choose where they act. This server: its own bans, mutes, staff and custom
    // items. Team Vanilla: the account itself, on every server, at TVAS.
    const u8 tv_role = std::max(s.tv.global_role, s.profile.global_role);
    const bool sfl = tv_role >= u8(GlobalRole::Moderator);
    if (!sfl) st.staff_global = false;
    const bool global = st.staff_global;
    if (sfl) {
        if (ui::radio(9619, X0, Y0, "This server", !global, true, "Its own bans and mutes, its staff, its custom items") && global)
            st.staff_global = false, s.staff_card.reset();
        if (ui::radio(9618, X0 + 130, Y0, "Team Vanilla (every server)", global, true, "The account itself: global bans, rank, SP, items, password") && !global)
            st.staff_global = true, s.staff_card.reset();
        Y0 += 28;
    }
    label(X0, Y0, "Account or code name");
    const bool go = ui::edit_at(9620, X0 + 160, Y0 - 2, X1 - 100, Y0 + 20, st.staff_search, 24, "type the start of one");
    if ((ui::text_button(9621, X1 - 90, Y0 - 3, X1, Y0 + 21, "Find") || go) && !st.staff_search.empty()) {
        if (global) s.global_staff_find(st.staff_search);
        else s.staff_find(st.staff_search);
    }
    if (!s.staff_card) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, global ? "Find a Team Vanilla account to see it." : "Find a soldier of this server to see it.", kDim, ui::Align::Center);
        return;
    }
    const StaffAccountCard& c = *s.staff_card;
    const std::string who = global ? tv_token(c.account) : c.account;
    const u64 now = u64(std::time(nullptr));
    const float mid = X0 + 330;
    float y = Y0 + 34;
    ui::heading(X0, y, mid - 12, c.code_name.empty() ? c.account : c.code_name);
    y += 22;
    auto row = [&](const char* k, const std::string& v, VanU32 col = kInk) {
        label(X0, y, k);
        ui::text_at(X0 + 96, y, mid - 12, y + 18, v, col, ui::Align::Left, false, 12.0f);
        y += 19;
    };
    row("Account", c.account);
    if (global) row("Staff", c.role ? global_role_name(GlobalRole(c.role)) : std::string("no"), c.role ? kWarn : kDim);
    else row("Here", std::string(role_name(Role(c.role))) + (c.online ? "   (online)" : "   (offline)"), c.role ? kWarn : kInk);
    row("Soldier", eng::str::format("%s, %u XP", rank_name(rank_for_xp(c.xp)), c.xp));
    row("SP", eng::str::format("%u", c.sp));
    row("Record", eng::str::format("%u kills, %u deaths, %u wins, %u losses, %u games", c.kills, c.deaths, c.wins, c.losses, c.matches));
    row("Owns", eng::str::format("%u weapons, %u forces, %u items", c.weapons, c.forces, c.items));
    if (!c.clan.empty()) row("Clan", c.clan);
    row("Since", date_text(c.created));
    row("Muted", c.muted_until > now ? "for " + length_text(c.muted_until - now) : std::string("no"), c.muted_until > now ? kWarn : kDim);
    if (!global)
        row("Global chat", c.global_muted_until > now ? "off it for " + length_text(c.global_muted_until - now) : std::string("on it"),
            c.global_muted_until > now ? kWarn : kDim);
    row(global ? "Banned everywhere" : "Banned here",
        c.banned_until == StaffAccountCard::kForever ? std::string("for good")
        : c.banned_until > now                       ? "for " + length_text(c.banned_until - now)
                                                     : std::string("no"),
        c.banned_until ? kBad : kDim);
    if (c.banned_until && !c.ban_reason.empty()) row("Ban reason", c.ban_reason, kBad);

    // What may be done to it: the server (or TVAS) says no to what the rank does not allow.
    auto act = [&](StaffOp op, u32 seconds) {
        if (global) s.global_staff_action(who, op, seconds, st.staff_reason);
        else s.staff_action(who, op, seconds, st.staff_reason);
    };
    float ry = Y0 + 34;
    ui::heading(mid, ry, X1, global ? "Actions on every server" : "Actions on this server");
    ry += 22;
    label(mid, ry, "Reason");
    (void)ui::edit_at(9622, mid + 70, ry - 2, X1, ry + 20, st.staff_reason, int(kStaffReasonMax), "shown to them, and logged");
    ry += 28;
    const float bw = 120;
    if (ui::text_button(9623, mid, ry, mid + bw, ry + 24, "Kick", c.online || global, c.online || global ? "Put them off the server now." : "They are not online."))
        act(StaffOp::Kick, 0);
    ry += 30;
    if (const int d = ui::arrows(9624, mid + bw + 8, ry, X1, ry + 22, kMutes[st.staff_mute].name); d)
        st.staff_mute = (st.staff_mute + d + int(std::size(kMutes))) % int(std::size(kMutes));
    if (ui::text_button(9625, mid, ry, mid + bw, ry + 24, "Mute")) act(StaffOp::Mute, kMutes[st.staff_mute].seconds);
    ry += 30;
    if (ui::text_button(9626, mid, ry, mid + bw, ry + 24, "Unmute", c.muted_until > now)) act(StaffOp::Unmute, 0);
    if (!global) {
        // Global chat alone, for the length picked above: the rest of their chat stands.
        const float gx = mid + bw + 8, gw = (X1 - gx - 6) * 0.5f;
        const bool off_global = c.global_muted_until > now;
        if (ui::text_button(9637, gx, ry, gx + gw, ry + 24, "Off global chat", true, "Take them off global chat alone, for the length picked above"))
            act(StaffOp::GlobalMute, kMutes[st.staff_mute].seconds);
        if (ui::text_button(9638, gx + gw + 6, ry, X1, ry + 24, "Back on it", off_global, off_global ? "Let them talk on global chat again" : "They are on global chat"))
            act(StaffOp::GlobalUnmute, 0);
    }
    ry += 30;
    if (const int d = ui::arrows(9627, mid + bw + 8, ry, X1, ry + 22, kBans[st.staff_ban].name); d)
        st.staff_ban = (st.staff_ban + d + int(std::size(kBans))) % int(std::size(kBans));
    if (ui::text_button(9628, mid, ry, mid + bw, ry + 24, "Ban", true, global ? "From every server: no tickets anywhere (EN-1)" : "From this server")) act(StaffOp::Ban, kBans[st.staff_ban].seconds);
    ry += 30;
    if (ui::text_button(9629, mid, ry, mid + bw, ry + 24, "Unban", c.banned_until != 0)) act(StaffOp::Unban, 0);
    ry += 36;

    const bool gm = global ? tv_role >= u8(GlobalRole::GameMaster) : s.profile.game_master();
    if (!gm) {
        ui::text_at(mid, ry, X1, ry + 40,
                    global ? "SFLegacy Game Masters and Admins also edit the account and reset passwords." : "A Game Master can also give and take this server's own items.",
                    kDim, ui::Align::Left, false, 11.0f);
        return;
    }
    ui::heading(mid, ry, X1, global ? "SFLegacy Game Master" : "Game Master");
    ry += 22;
    // Who may name whom (§7.2, RL-3): the server and TVAS refuse a tier at or above your own.
    if (global) {
        static const char* roles[] = {"Player", "SFL Mod", "SFL GM"};
        for (int r = 0; r < 3; ++r)
            if (ui::radio(9630 + r, mid + float(r) * 92, ry, roles[r], st.staff_role == r)) st.staff_role = r;
        if (ui::text_button(9633, X1 - 100, ry - 3, X1, ry + 21, "Set role", true, "SFLegacy Admins name Moderators and Game Masters"))
            s.global_staff_action(who, StaffOp::SetRole, 0, st.staff_reason, u8(st.staff_role));
    } else {
        static const char* roles[] = {"Player", "Moderator", "Game Master", "Admin"};
        st.staff_role = std::clamp(st.staff_role, 0, 3);
        for (int r = 0; r < 4; ++r)
            if (ui::radio(9630 + r, mid + float(r) * 84, ry, roles[r], st.staff_role == r, r < 3 || s.profile.server_role >= u8(Role::Owner) || tv_role >= u8(GlobalRole::GameMaster)))
                st.staff_role = r;
        if (ui::text_button(9633, X1 - 76, ry - 3, X1, ry + 21, "Set", true, "This server's staff: the Owner names Admins, an Admin those below"))
            s.staff_action(who, StaffOp::SetRole, 0, st.staff_reason, Role(st.staff_role));
    }
    ry += 30;
    const std::span<const Field> fields = global ? std::span<const Field>(kGlobalFields) : std::span<const Field>(kServerFields);
    st.staff_field = std::clamp(st.staff_field, 0, int(fields.size()) - 1);
    const Field& f = fields[size_t(st.staff_field)];
    if (const int d = ui::arrows(9634, mid, ry, mid + 190, ry + 22, f.name); d)
        st.staff_field = (st.staff_field + d + int(fields.size())) % int(fields.size()), st.staff_value.clear();
    if (f.field != AccountField::ClearLoadout) (void)ui::edit_at(9635, mid + 198, ry - 1, X1 - 80, ry + 21, st.staff_value, 40, f.hint);
    if (ui::text_button(9636, X1 - 72, ry - 2, X1, ry + 22, "Apply")) {
        const bool grant = f.field == AccountField::GrantItem || f.field == AccountField::GrantWeapon || f.field == AccountField::GrantForce;
        const bool pair = grant || f.field == AccountField::GrantBox || f.field == AccountField::Durability;
        u32 v = 0;
        std::string text = st.staff_value;
        if (pair) {
            // "E1010 30": the item and its days (or how many, or what is left).
            const auto parts = eng::str::split(st.staff_value, ' ');
            text = parts.empty() ? std::string() : std::string(parts[0]);
            v = parts.size() > 1 ? u32(std::max(0, std::atoi(std::string(parts[1]).c_str()))) : 0;
        } else if (f.number) {
            v = u32(std::max<long long>(0, std::atoll(st.staff_value.c_str())));
        }
        if (global) s.global_staff_edit(who, f.field, v, text);
        else s.staff_edit(who, f.field, v, text);
    }
    ry += 32;
    if (!global) return;
    // ID-12: a forgotten password. Checked first that the account is the asker's; the temporary
    // password is shown once, to pass on privately; it must be changed at the next sign-in.
    ui::heading(mid, ry, X1, "Forgotten password");
    ry += 22;
    (void)ui::edit_at(9639, mid, ry - 1, X1 - 130, ry + 21, st.staff_checked, 300, "what you checked: code names, clan, friends, when made");
    const bool armed = app.now() - st.staff_delete_armed < 3.0;
    const bool checked = eng::str::trim(st.staff_checked).size() >= 10;
    if (ui::text_button(9617, X1 - 124, ry - 2, X1, ry + 22, armed ? "Sure? Reset" : "Reset password", checked,
                        checked ? "Every session ends; they get a system mail; the reset is logged" : "Write down what you checked first")) {
        if (armed) {
            s.global_staff_reset_password(who, std::string(eng::str::trim(st.staff_checked)));
            st.staff_checked.clear();
            st.staff_delete_armed = -10;
        } else {
            st.staff_delete_armed = app.now();
        }
    }
    (void)Y1;
}

void recordings_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!st.staff_asked) s.staff_list_recordings(), st.staff_asked = true;
    label(X0, Y0, "Every match this server played, newest first: what everyone was sent, seen from nobody's seat.");
    if (ui::text_button(9640, X1 - 90, Y0 - 3, X1, Y0 + 19, "Refresh")) st.staff_asked = false;
    const std::vector<RecordingEntry> none;
    const auto& list = s.staff_recordings ? s.staff_recordings->recordings : none;
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (int(list[i].match) == st.staff_recording_sel) sel = int(i);
    const float bottom = Y1 - 40;
    const int clicked = ui::rows(9641, X0, Y0 + 26, X1, bottom, int(list.size()), 36, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const RecordingEntry& e = list[size_t(i)];
        ui::text_at(x0 + 6, y0 + 2, x0 + 80, y0 + 17, eng::str::format("#%u", e.match), kInk, ui::Align::Left, true, 12.0f);
        ui::text_at(x0 + 80, y0 + 2, x0 + 360, y0 + 17, std::string(mode_name(Mode(e.mode))) + " on " + app.map_title(e.map), kInk, ui::Align::Left, true, 12.0f);
        ui::text_at(x0 + 360, y0 + 2, x0 + 520, y0 + 17, date_text(e.started), kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(x0 + 520, y0 + 2, x0 + 600, y0 + 17, length_text(e.seconds), kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(x0 + 600, y0 + 2, x1 - 6, y0 + 17,
                    e.live ? std::string("being played") : eng::str::format("%.0f KB", double(e.bytes) / 1024.0), e.live ? kLime : kDim, ui::Align::Right, false, 11.0f);
        ui::text_at(x0 + 6, y0 + 18, x1 - 120, y1, e.names, kDim, ui::Align::Left, false, 11.0f);
        if (e.reports) ui::text_at(x1 - 120, y0 + 18, x1 - 6, y1, eng::str::format("%u report%s", e.reports, e.reports == 1 ? "" : "s"), kWarn, ui::Align::Right, true, 11.0f);
    });
    if (clicked >= 0) st.staff_recording_sel = int(list[size_t(clicked)].match);
    if (list.empty()) ui::text_at(X0, Y0 + 70, X1, Y0 + 90, s.staff_recordings ? "No recordings kept." : "Asking the server...", kDim, ui::Align::Center);
    // The download, then the watching.
    const auto& dl = s.replay;
    std::string state;
    if (dl && dl->match == u32(st.staff_recording_sel)) {
        if (!dl->error.empty()) state = dl->error;
        else if (dl->done) state = "Downloaded.";
        else state = dl->total ? eng::str::format("Downloading... %.0f%%", 100.0 * double(dl->got) / double(dl->total)) : "Asking the server...";
    }
    ui::text_at(X0, bottom + 10, X1 - 140, bottom + 30, state, dl && !dl->error.empty() ? kBad : kDim, ui::Align::Left, false, 12.0f);
    const bool busy = dl && dl->match == u32(st.staff_recording_sel) && !dl->done && dl->error.empty();
    // Not from inside a match being played: watching replaces the world on screen.
    const bool playing = app.screen() == Screen::Loading || app.screen() == Screen::Match || app.screen() == Screen::Result;
    if (ui::text_button(9642, X1 - 130, bottom + 6, X1, bottom + 32, "Watch", sel >= 0 && !busy && !playing,
                        playing ? "Leave the match first." : nullptr))
        s.request_replay(u32(st.staff_recording_sel));
    if (dl && dl->done && dl->error.empty() && dl->match == u32(st.staff_recording_sel)) {
        std::vector<u8> file = std::move(s.replay->bytes);
        s.replay.reset();
        st.staff_open = false;
        app.watch_replay(std::move(file));
    }
}

// The Owner's and Server Admins' channels (§10.2) and the message of the day (ML-8). Every edit
// carries the list's version (CH-6): one made on a list someone else has changed meanwhile is
// refused, and the list comes again.
void load_channel(ScreenState& st, const ChannelInfo& c) {
    st.staff_channel_edit = c;
    st.staff_channel_maps.clear();
    for (const std::string& m : c.maps) st.staff_channel_maps += (st.staff_channel_maps.empty() ? "" : ", ") + m;
    st.staff_channel_loaded = true;
}

void channels_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!s.channels_editable) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, "The Owner and Server Admins edit this server's channels.", kDim, ui::Align::Center);
        return;
    }
    const auto& list = s.channels;
    const float split = X0 + 360;
    ui::heading(X0, Y0, split - 12, eng::str::format("CHANNELS  %zu of %d", list.size(), int(kMaxChannels)));
    // A new channel being made becomes the one picked once the server's list has it (the copy
    // being edited stays as it is).
    if (st.staff_channel == 0 && st.staff_channel_loaded)
        for (const ChannelInfo& c : list)
            if (c.id == st.staff_channel_edit.id) st.staff_channel = int(c.id);
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (int(list[i].id) == st.staff_channel) sel = int(i);
    const float list_bottom = Y1 - 150;
    const int clicked = ui::rows(13000, X0, Y0 + 22, split - 12, list_bottom, int(list.size()), 34, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const ChannelInfo& c = list[size_t(i)];
        ui::text_at(x0 + 6, y0 + 2, x0 + 30, y0 + 17, eng::str::format("%u", unsigned(c.id)), kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(x0 + 30, y0 + 2, x1 - 80, y0 + 17, c.name + (c.hidden ? "  (hidden)" : ""), kInk, ui::Align::Left, true, 12.0f);
        ui::text_at(x1 - 80, y0 + 2, x1 - 6, y0 + 17, eng::str::format("%u / %u", unsigned(c.players), unsigned(c.capacity)), kDim, ui::Align::Right, false, 11.0f);
        ui::text_at(x0 + 30, y0 + 17, x1 - 6, y1, std::string(channel_kind_name(ChannelKind(c.kind))) + "  -  " + (c.limit.empty() ? std::string("anyone") : c.limit), kDim,
                    ui::Align::Left, false, 11.0f);
    });
    if (clicked >= 0) {
        st.staff_channel = int(list[size_t(clicked)].id);
        load_channel(st, list[size_t(clicked)]);
        sel = clicked;
    }
    // A new one: the first free number, at the bottom, open to all.
    float by = list_bottom + 8;
    const float bw = (split - 12 - X0 - 18) / 4;
    if (ui::text_button(13001, X0, by, X0 + bw, by + 24, "New", list.size() < size_t(kMaxChannels))) {
        ChannelInfo c;
        for (u8 id = 1; id <= u8(kMaxChannels); ++id)
            if (std::none_of(list.begin(), list.end(), [&](const ChannelInfo& o) { return o.id == id; })) {
                c.id = id;
                break;
            }
        c.name = "New Channel";
        c.kind = u8(ChannelKind::Free);
        c.min_rank = 0, c.max_rank = 74;
        c.capacity = 100;
        c.order = u8(std::min<size_t>(list.size() + 1, 255));
        st.staff_channel = 0;
        load_channel(st, c);
    }
    if (ui::text_button(13002, X0 + (bw + 6), by, X0 + (bw + 6) + bw, by + 24, "Move up", sel > 0) && sel > 0) {
        ChannelInfo c = list[size_t(sel)];
        c.order = u8(sel);   // its place, 1 the top: one above where it is
        s.channel_edit(ChannelOp::Move, c);
    }
    if (ui::text_button(13003, X0 + (bw + 6) * 2, by, X0 + (bw + 6) * 2 + bw, by + 24, "Move down", sel >= 0 && sel + 1 < int(list.size())) && sel >= 0) {
        ChannelInfo c = list[size_t(sel)];
        c.order = u8(sel + 2);
        s.channel_edit(ChannelOp::Move, c);
    }
    // CH-5: deleting a busy channel sends its lobby back to the list; its matches play out.
    const bool armed = app.now() - st.staff_delete_armed < 3.0;
    if (ui::text_button(13004, X0 + (bw + 6) * 3, by, split - 12, by + 24, armed ? "Sure?" : "Delete", sel >= 0 && list.size() > 1,
                        "Its lobby goes back to the channel list; matches being played finish first") &&
        sel >= 0) {
        if (armed) {
            s.channel_edit(ChannelOp::Delete, list[size_t(sel)]);
            st.staff_channel = -1, st.staff_channel_loaded = false, st.staff_delete_armed = -10;
        } else {
            st.staff_delete_armed = app.now();
        }
    }
    if (st.staff_channel >= 1 && sel < 0) st.staff_channel = -1, st.staff_channel_loaded = false;   // deleted meanwhile

    // The message of the day (ML-8): shown to everyone joining, the Owner's and Admins' alone.
    float my = by + 36;
    ui::heading(X0, my, X1, "MESSAGE OF THE DAY");
    my += 22;
    if (!st.staff_motd_loaded) st.staff_motd = s.motd, st.staff_motd_loaded = true;
    (void)ui::edit_at(13005, X0, my, X1 - 170, my + 22, st.staff_motd, 512, "shown to everyone joining this server");
    if (ui::text_button(13006, X1 - 164, my - 1, X1 - 84, my + 23, "Save", st.staff_motd != s.motd, "No claim to be official; at most 512 letters")) {
        s.motd_edit(st.staff_motd);
        s.motd = st.staff_motd;
    }
    if (ui::text_button(13007, X1 - 78, my - 1, X1, my + 23, "Undo", st.staff_motd != s.motd)) st.staff_motd = s.motd;
    ui::text_at(X0, my + 28, X1, my + 44, "Owners cannot mail players: this line is how a server speaks to everyone who comes (D29).", kDim, ui::Align::Left, true, 11.0f);

    // The channel being edited.
    if (!st.staff_channel_loaded) {
        ui::text_at(split, Y0 + 80, X1, Y0 + 100, "Pick a channel, or New.", kDim, ui::Align::Center);
        return;
    }
    ChannelInfo& c = st.staff_channel_edit;
    float y = Y0;
    ui::heading(split, y, X1, st.staff_channel == 0 ? eng::str::format("NEW CHANNEL  #%u", unsigned(c.id)) : eng::str::format("CHANNEL  #%u", unsigned(c.id)));
    y += 26;
    const float fx = split + 96;
    label(split, y, "Name");
    (void)ui::edit_at(13010, fx, y - 2, X1, y + 20, c.name, 32, "1 to 32 letters");
    y += 28;
    label(split, y, "Kind");
    if (const int d = ui::arrows(13011, fx, y, X1, y + 20, channel_kind_name(ChannelKind(c.kind))); d)
        c.kind = u8((int(c.kind) + d + int(ChannelKind::Count)) % int(ChannelKind::Count));
    y += 28;
    // CH-1: both ends inclusive.
    label(split, y, "Lowest rank");
    if (const int d = ui::arrows(13012, fx, y, X1, y + 20, eng::str::format("%u  %s", unsigned(c.min_rank), rank_name(c.min_rank))); d)
        c.min_rank = u8(std::clamp(int(c.min_rank) + d, 0, int(c.max_rank)));
    y += 28;
    label(split, y, "Highest rank");
    if (const int d = ui::arrows(13013, fx, y, X1, y + 20, eng::str::format("%u  %s", unsigned(c.max_rank), rank_name(c.max_rank))); d)
        c.max_rank = u8(std::clamp(int(c.max_rank) + d, int(c.min_rank), 74));
    y += 28;
    label(split, y, "Lowest K/D");
    (void)ui::trackbar(13014, fx, y, X1, y + 18, c.min_kd, 0.0f, 10.0f, "%.1f");
    y += 28;
    label(split, y, "Soldiers");
    if (const int d = ui::arrows(13015, fx, y, X1, y + 20, eng::str::format("%u at most", unsigned(c.capacity))); d)
        c.capacity = u16(std::clamp(int(c.capacity) + d * (c.capacity >= 100 ? 10 : 5), 5, 512));
    y += 28;
    (void)ui::check(13016, split, y, "Hidden from those who cannot enter", c.hidden);
    y += 28;
    // Games rooms may be made with (none ticked: every one).
    label(split, y, c.modes ? "Games" : "Games: every one");
    y += 20;
    const float cw = (X1 - split) / 3;
    for (int m = 0; m < int(Mode::Count); ++m) {
        const float cx = split + cw * float(m % 3), cy = y + float(m / 3) * 22;
        bool on = (c.modes >> m) & 1u;
        if (ui::check(13020 + m, cx, cy, mode_info(Mode(m)).short_name, on, true, mode_name(Mode(m)))) c.modes = u16(on ? c.modes | (1u << m) : c.modes & ~(1u << m));
    }
    y += float((int(Mode::Count) + 2) / 3) * 22 + 6;
    label(split, y, "Maps");
    (void)ui::edit_at(13040, fx, y - 2, X1, y + 20, st.staff_channel_maps, 400, "empty: all its games offer; else map ids, commas");
    y += 32;
    const bool named = !eng::str::trim(c.name).empty();
    if (ui::text_button(13041, X1 - 110, y, X1, y + 26, "Save", named, "Everyone sees the change at once; nobody is put out (CH-4)")) {
        ChannelInfo out = c;
        out.name = std::string(eng::str::trim(c.name));
        out.maps.clear();
        for (std::string_view part : eng::str::split(st.staff_channel_maps, ',')) {
            const std::string id = eng::str::lower(eng::str::trim(part));
            if (!id.empty() && out.maps.size() < 64) out.maps.push_back(id.substr(0, 32));
        }
        // A new one stays "new" until the server's list has it (above); a refusal leaves the
        // copy here to mend.
        s.channel_edit(ChannelOp::Save, out);
    }
    if (ui::text_button(13042, X1 - 220, y, X1 - 116, y + 26, "Revert", st.staff_channel >= 1) && sel >= 0) load_channel(st, list[size_t(sel)]);
}

// The server's Game Masters: the game types and maps played on the whole server. A game type
// switched off is never made, changed to or started (a room that had it picks another); a map
// switched off is never chosen or drawn at random (a room that had it draws another at the start).
// A channel's own games and maps narrow it further.
void games_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!s.games_editable) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, "This server's Game Masters choose what it plays.", kDim, ui::Align::Center);
        return;
    }
    if (!st.staff_games_loaded) {
        st.staff_games = s.games;
        st.staff_games_maps = app.every_map();
        st.staff_games_loaded = true;
    }
    ServerGames& g = st.staff_games;
    // A map switched off that no longer comes with the server's packs stays listed, to be put back.
    for (const std::string& id : g.maps_off)
        if (std::find(st.staff_games_maps.begin(), st.staff_games_maps.end(), id) == st.staff_games_maps.end()) st.staff_games_maps.push_back(id);

    // ── Left: the game types ──
    const float split = X0 + 300;
    int modes_on = 0;
    for (int m = 0; m < int(Mode::Count); ++m) modes_on += g.takes_mode(Mode(m));
    ui::heading(X0, Y0, split - 16, eng::str::format("GAME TYPES  %d of %d played", modes_on, int(Mode::Count)));
    float y = Y0 + 26;
    for (int m = 0; m < int(Mode::Count); ++m) {
        bool on = g.takes_mode(Mode(m));
        // The last one on stays on: a server plays something.
        if (ui::check(13500 + m, X0 + 4, y, mode_name(Mode(m)), on, !on || modes_on > 1, on && modes_on == 1 ? "A server plays at least one game type" : nullptr))
            g.modes_off = u16(on ? g.modes_off & ~(1u << m) : g.modes_off | (1u << m));
        y += 24;
    }
    y += 8;
    if (ui::text_button(13520, X0, y, X0 + 120, y + 24, "All on", g.modes_off != 0)) g.modes_off = 0;
    y += 36;
    wrapped(X0, y, split - 16, "Switched off: no room is made with it, changed to it or started with it. A room that already has it picks another before it starts.", kDim, 4);
    wrapped(X0, y + 76, split - 16, "A map switched off is never chosen or drawn at random; a room that had it draws another when it starts.", kDim, 4);

    // ── Right: the maps ──
    const auto& maps = st.staff_games_maps;
    int maps_on = 0;
    for (const std::string& id : maps) maps_on += g.takes_map(id);
    ui::heading(split, Y0, X1, eng::str::format("MAPS  %d of %zu played", maps_on, maps.size()));
    label(split, Y0 + 24, "Find");
    (void)ui::edit_at(13521, split + 44, Y0 + 22, X1 - 130, Y0 + 44, st.staff_games_find, 32, "a map's name or id");
    if (ui::text_button(13522, X1 - 124, Y0 + 21, X1, Y0 + 45, "All on", !g.maps_off.empty())) g.maps_off.clear();
    const std::string find = eng::str::lower(eng::str::trim(st.staff_games_find));
    std::vector<int> shown;
    for (int i = 0; i < int(maps.size()); ++i)
        if (find.empty() || eng::str::lower(app.map_title(maps[size_t(i)])).find(find) != std::string::npos || maps[size_t(i)].find(find) != std::string::npos)
            shown.push_back(i);
    const float list_bottom = Y1 - 40;
    const int clicked = ui::rows(13523, split, Y0 + 52, X1, list_bottom, int(shown.size()), 26, -1, [&](int i, float x0, float y0, float x1, float y1, bool hovered, bool) {
        const std::string& id = maps[size_t(shown[size_t(i)])];
        const bool on = g.takes_map(id);
        ui::text_at(x0 + 8, y0 + 4, x0 + 260, y1, app.map_title(id), on ? kInk : kDim, ui::Align::Left, true, 12.0f);
        ui::text_at(x0 + 264, y0 + 5, x1 - 70, y1, id, kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(x1 - 66, y0 + 4, x1 - 8, y1, on ? "ON" : "OFF", on ? kLime : kBad, ui::Align::Right, true, 12.0f);
        if (hovered) VanGui::SetTooltip(on ? "Click: switch it off" : "Click: switch it on");
    });
    if (clicked >= 0) {
        const std::string& id = maps[size_t(shown[size_t(clicked)])];
        if (g.takes_map(id)) {
            if (g.maps_off.size() < kMaxMapsOff) g.maps_off.push_back(id);
        } else {
            std::erase(g.maps_off, id);
        }
    }
    if (shown.empty()) ui::text_at(split, Y0 + 90, X1, Y0 + 110, maps.empty() ? "No maps read." : "No map by that name.", kDim, ui::Align::Center);

    // Saving: everyone on the server gets the new lists at once.
    auto same = [](const ServerGames& a, const ServerGames& b) {
        if (a.modes_off != b.modes_off || a.maps_off.size() != b.maps_off.size()) return false;
        return std::all_of(a.maps_off.begin(), a.maps_off.end(), [&](const std::string& id) { return !b.takes_map(id); });
    };
    const bool changed = !same(g, s.games);
    const float by = list_bottom + 8;
    if (ui::text_button(13524, X1 - 110, by, X1, by + 26, "Save", changed && modes_on > 0, "Everyone on this server sees the change at once")) s.games_edit(g);
    if (ui::text_button(13525, X1 - 220, by, X1 - 116, by + 26, "Revert", changed)) g = s.games;
    if (changed) ui::text_at(split, by + 4, X1 - 230, by + 24, "Not saved yet.", kWarn, ui::Align::Left, true, 12.0f);
}

// SFLegacy Staff's servers (§7.2, PR-5, EN-2, EN-3, MN-6, MN-8): every registered server, its tier,
// and what may be done to it. TVAS checks each action against the global role.
void servers_tab(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const u8 tv_role = std::max(s.tv.global_role, s.profile.global_role);
    if (tv_role < u8(GlobalRole::Moderator)) {
        ui::text_at(X0, Y0 + 80, X1, Y0 + 100, "SFLegacy Staff see every server here.", kDim, ui::Align::Center);
        return;
    }
    if (!st.staff_asked) s.global_staff_servers(), st.staff_asked = true;
    if (ui::text_button(13100, X1 - 90, Y0 - 3, X1, Y0 + 19, "Refresh")) st.staff_asked = false;
    static const char* const tiers[] = {"Unverified", "Verified", "Official"};
    static const char* const states[] = {"", "revoked", "closed"};
    const std::vector<Session::StaffServer> none;
    const auto& list = s.staff_servers ? *s.staff_servers : none;
    label(X0, Y0, eng::str::format("%zu servers registered", list.size()));
    int sel = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == st.staff_server_sel) sel = int(i);
    const float split = X0 + 470;
    const u64 now = s.tv_now();
    const int clicked = ui::rows(13101, X0, Y0 + 26, split - 12, Y1, int(list.size()), 36, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const Session::StaffServer& e = list[size_t(i)];
        const bool fresh = e.last_beat && now < e.last_beat + 120;
        const VanU32 c = e.state ? kBad : fresh ? kInk : kDim;
        ui::text_at(x0 + 6, y0 + 2, x1 - 150, y0 + 17, eng::str::format("#%llu  %s", (unsigned long long)e.id, e.name.c_str()), c, ui::Align::Left, true, 12.0f);
        ui::text_at(x1 - 150, y0 + 2, x1 - 6, y0 + 17, e.state ? std::string(states[std::min<u8>(e.state, 2)]) : std::string(tiers[std::min<u8>(e.tier, 2)]),
                    e.state ? kBad : e.tier == u8(Tier::Official) ? kLime : e.tier ? kWarn : kDim, ui::Align::Right, true, 11.0f);
        std::string line = e.owner.empty() ? std::string() : "by " + e.owner + "  -  ";
        line += e.address + eng::str::format("  -  %u on  -  ", unsigned(e.players));
        line += fresh ? std::string("beating") : e.last_beat ? "last beat " + date_text(e.last_beat) : std::string("never beat");
        ui::text_at(x0 + 6, y0 + 18, x1 - 6, y1, line, kDim, ui::Align::Left, false, 11.0f);
    });
    if (clicked >= 0) st.staff_server_sel = list[size_t(clicked)].id, st.staff_server_tier = list[size_t(clicked)].tier, sel = clicked;
    if (list.empty()) ui::text_at(X0, Y0 + 70, split - 12, Y0 + 90, s.staff_servers ? "No servers." : "Asking Team Vanilla...", kDim, ui::Align::Center);
    if (sel < 0) {
        ui::text_at(split, Y0 + 70, X1, Y0 + 90, "Pick a server.", kDim, ui::Align::Center);
        return;
    }
    const Session::StaffServer& e = list[size_t(sel)];
    float y = Y0 + 26;
    ui::heading(split, y, X1, e.name);
    y += 22;
    if (!e.note.empty()) {
        wrapped(split, y, X1, e.note, kWarn, 2);
        y += 36;
    }
    label(split, y, "Why (logged; players are told on a close)");
    y += 18;
    (void)ui::edit_at(13102, split, y, X1, y + 22, st.staff_server_text, 200, "a reason, or the line to broadcast");
    y += 30;
    const std::string why(eng::str::trim(st.staff_server_text));
    const bool gm = tv_role >= u8(GlobalRole::GameMaster), admin = tv_role >= u8(GlobalRole::Admin);
    // PR-5, D44: the badge is SFLegacy Staff's alone; Official, an Admin's.
    if (const int d = ui::arrows(13103, split, y, split + 200, y + 22, tiers[std::clamp(st.staff_server_tier, 0, 2)], gm); d)
        st.staff_server_tier = std::clamp(st.staff_server_tier + d, 0, admin ? 2 : 1);
    if (ui::text_button(13104, split + 208, y - 1, X1, y + 23, "Set tier", gm && st.staff_server_tier != e.tier, "Unverified, Verified (GM), Official (Admin)"))
        s.global_staff_server(e.id, "tier", why, u32(st.staff_server_tier));
    y += 32;
    const float bw = (X1 - split - 6) / 2;
    // EN-3: a command the server collects with its next heartbeat.
    if (ui::text_button(13105, split, y, split + bw, y + 26, "Broadcast the line", !why.empty(), "Shown on that server to everyone, with its next heartbeat"))
        s.global_staff_command(e.id, "broadcast", why);
    if (ui::text_button(13106, split + bw + 6, y, X1, y + 26, "Take off the list", true, "The server stops listing itself (EN-3)")) s.global_staff_command(e.id, "unlist", why);
    y += 32;
    // MN-8: kept open past 60 days offline (a long, announced break).
    if (ui::text_button(13107, split, y, split + bw, y + 26, "Keep open past 60 days", gm)) s.global_staff_server(e.id, "keep_open", why, 1);
    if (ui::text_button(13108, split + bw + 6, y, X1, y + 26, "Close after 60 days", gm)) s.global_staff_server(e.id, "keep_open", why, 0);
    y += 32;
    // EN-2: no tickets, no listing, no reports. Restored only while not closed.
    if (e.state == 1) {
        if (ui::text_button(13109, split, y, split + bw, y + 26, "Restore its key", admin)) s.global_staff_server(e.id, "restore", why);
    } else if (ui::text_button(13109, split, y, split + bw, y + 26, "Revoke its key", admin && e.state == 0, "No tickets, no listing, no reports (EN-2)")) {
        s.global_staff_server(e.id, "revoke", why);
    }
    // MN-6: closed for good, every SP and Coin spent on its custom items and capsules refunded.
    const bool armed = app.now() - st.staff_delete_armed < 3.0;
    if (ui::text_button(13110, split + bw + 6, y, X1, y + 26, armed ? "Sure? Close it" : "Close the server", gm && e.state != 2 && !why.empty(),
                        why.empty() ? "Say why first: its players are told" : "For good: its custom items and capsules are refunded (MN-6)")) {
        if (armed) s.global_staff_server(e.id, "close", why), st.staff_delete_armed = -10;
        else st.staff_delete_armed = app.now();
    }
}

}  // namespace

std::vector<Pickable> pickable_players(App& app) {
    std::vector<Pickable> out;
    Session& s = app.session();
    const RoomMember* me = s.me_in_room();
    if (!s.room) {
        // In the lobby: its soldiers (a report from a name's menu; a vote needs a room).
        for (const auto& [id, u] : s.users) {
            if (id == s.session_id) continue;
            Pickable p;
            p.id = id;
            p.name = u.name;
            p.why_not = "not in your room";
            out.push_back(std::move(p));
        }
        return out;
    }
    const bool teams = mode_info(s.room->settings.mode).teams;
    for (const RoomMember& m : s.room->members) {
        if (m.id == s.session_id) continue;
        Pickable p;
        p.id = m.id;
        p.name = m.name;
        if (m.team == u8(Team::Observer)) p.why_not = "observing";
        else if (me && teams && m.team != me->team) p.why_not = "the other side";
        p.votable = p.why_not.empty();
        out.push_back(std::move(p));
    }
    return out;
}

void staff_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (!s.profile.staff()) {
        st.staff_open = false;
        return;
    }
    constexpr float X0 = 92, Y0 = 84, X1 = 932, Y1 = 690;
    // Who you are here, and everywhere (§7.1): the two roles are separate (RL-1).
    const u8 tv_role = std::max(s.tv.global_role, s.profile.global_role);
    std::string title = std::string("STAFF  -  ") + role_name(Role(s.profile.server_role));
    if (tv_role) title += std::string(", ") + global_role_name(GlobalRole(tv_role));
    if (!ui::dialog_begin("Staff", X0, Y0, X1, Y1, title, &st.staff_open)) {
        st.staff_open = false;
        return;
    }
    // The tabs this soldier may use: Channels for the Owner and Server Admins (§10.2), the game
    // types and maps played and the shop for this server's Game Masters (D11), Team Vanilla's
    // events and rewards for SFLegacy Game Masters (MN-5), every registered server for SFLegacy Staff.
    struct Tab {
        int id;
        const char* name;
        bool shown;
    };
    const Tab tabs[] = {
        {0, "Reports", true},
        {1, "Accounts", true},
        {2, "Recordings", true},
        {6, "Channels", s.channels_editable},
        {8, "Games", s.games_editable},
        {3, "Events", tv_role >= u8(GlobalRole::GameMaster)},
        {4, "Rewards", tv_role >= u8(GlobalRole::GameMaster)},
        {5, "Shop", s.profile.game_master()},
        {7, "Servers", tv_role >= u8(GlobalRole::Moderator)},
    };
    bool current_shown = false;
    int shown_count = 0;
    for (const Tab& t : tabs) current_shown |= t.shown && t.id == st.staff_tab, shown_count += t.shown;
    if (!current_shown) st.staff_tab = 0;
    // As many as fit across: 102 apart, closer when every tab is shown.
    const float pitch = std::min(102.0f, (X1 - X0 - 28) / float(std::max(1, shown_count)));
    float tx = X0 + 14;
    for (const Tab& t : tabs) {
        if (!t.shown) continue;
        if (ui::text_button(9590 + t.id, tx, Y0 + 32, tx + pitch - 4, Y0 + 58, t.name) && st.staff_tab != t.id) {
            st.staff_tab = t.id, st.staff_asked = false;
            if (!st.staff_cfg_dirty) st.staff_cfg_asked = false;   // fresh from the server, unless there are changes waiting
            if (t.id == 6) st.staff_motd_loaded = false;
            if (t.id == 8) st.staff_games_loaded = false;
        }
        if (st.staff_tab == t.id) ui::fill_at(tx - 2, Y0 + 58, tx + pitch - 2, Y0 + 60, kLime);
        tx += pitch;
    }
    const float x0 = X0 + 16, y0 = Y0 + 74, x1 = X1 - 16, y1 = Y1 - 16;
    switch (st.staff_tab) {
        case 0: reports_tab(app, x0, y0, x1, y1); break;
        case 1: accounts_tab(app, x0, y0, x1, y1); break;
        case 2: recordings_tab(app, x0, y0, x1, y1); break;
        case 3: staff_events_tab(app, x0, y0, x1, y1); break;
        case 4: staff_rewards_tab(app, x0, y0, x1, y1); break;
        case 5: staff_shop_tab(app, x0, y0, x1, y1); break;
        case 6: channels_tab(app, x0, y0, x1, y1); break;
        case 8: games_tab(app, x0, y0, x1, y1); break;
        default: servers_tab(app, x0, y0, x1, y1); break;
    }
    ui::dialog_end();
}

// Report a soldier from the match: one of them, a reason, a note.
void report_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 262, Y0 = 150, X1 = 762, Y1 = 600;
    if (!ui::dialog_begin("Report", X0, Y0, X1, Y1, "REPORT A PLAYER", &st.report_open)) {
        st.report_open = false;
        return;
    }
    const auto people = pickable_players(app);
    int sel = -1;
    for (size_t i = 0; i < people.size(); ++i)
        if (people[i].id == st.report_player) sel = int(i);
    label(X0 + 16, Y0 + 36, "Who");
    const int clicked = ui::rows(9660, X0 + 16, Y0 + 56, X0 + 230, Y1 - 60, int(people.size()), 24, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        ui::text_at(x0 + 6, y0 + 3, x1 - 6, y1, people[size_t(i)].name, kInk, ui::Align::Left, true, 12.0f);
    });
    if (clicked >= 0) st.report_player = people[size_t(clicked)].id, st.report_name = people[size_t(clicked)].name;
    if (people.empty()) ui::text_at(X0 + 16, Y0 + 80, X0 + 230, Y0 + 100, "Nobody else is here.", kDim, ui::Align::Center);
    const float rx = X0 + 246;
    label(rx, Y0 + 36, "Why");
    for (int r = 0; r < int(ReportReason::Count); ++r)
        if (ui::radio(9670 + r, rx, Y0 + 58 + float(r) * 24, report_reason_name(ReportReason(r)), st.report_reason == r)) st.report_reason = r;
    const float ny = Y0 + 58 + float(int(ReportReason::Count)) * 24 + 10;
    label(rx, ny, "What happened (optional)");
    (void)ui::edit_at(9680, rx, ny + 20, X1 - 16, ny + 42, st.report_note, int(kReportNoteMax), "a few words for staff");
    ui::text_at(rx, ny + 50, X1 - 16, ny + 90, "Staff see the report and the match's recording. Reporting someone falsely is itself reported.", kDim,
                ui::Align::Left, false, 11.0f);
    const bool ready = st.report_player != 0;
    if (ui::text_button(9681, X1 - 236, Y1 - 46, X1 - 126, Y1 - 16, "Report", ready, ready ? nullptr : "Pick who.")) {
        s.report(st.report_player, st.report_name, ReportReason(st.report_reason), st.report_note);
        st.report_open = false, st.report_note.clear(), st.report_player = 0;
        ui::dialog_close();
    }
    if (ui::text_button(9682, X1 - 116, Y1 - 46, X1 - 16, Y1 - 16, "Cancel")) {
        st.report_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

// Call a vote to put a soldier on your side out of the room. Everyone is listed; those who
// cannot be voted on are greyed with the reason (TacticalFPS's rule: never hidden).
void vote_call_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 312, Y0 = 170, X1 = 712, Y1 = 580;
    if (!ui::dialog_begin("VoteCall", X0, Y0, X1, Y1, "KICK A PLAYER", &st.vote_open)) {
        st.vote_open = false;
        return;
    }
    const auto people = pickable_players(app);
    int sel = -1;
    for (size_t i = 0; i < people.size(); ++i)
        if (people[i].id == st.vote_target) sel = int(i);
    label(X0 + 16, Y0 + 36, "Your side votes; a majority of it puts them out for 10 minutes.");
    const int clicked = ui::rows(9690, X0 + 16, Y0 + 58, X1 - 16, Y1 - 60, int(people.size()), 26, sel, [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
        const Pickable& p = people[size_t(i)];
        ui::text_at(x0 + 6, y0 + 4, x1 - 6, y1, p.name, p.votable ? kInk : kDim, ui::Align::Left, true, 12.0f);
        if (!p.votable) ui::text_at(x0 + 6, y0 + 4, x1 - 8, y1, p.why_not, kDim, ui::Align::Right, false, 11.0f);
    });
    if (clicked >= 0 && people[size_t(clicked)].votable) st.vote_target = people[size_t(clicked)].id;
    if (people.empty()) ui::text_at(X0 + 16, Y0 + 90, X1 - 16, Y0 + 110, "Nobody else is here.", kDim, ui::Align::Center);
    const bool ready = sel >= 0 && people[size_t(sel)].votable;
    if (ui::text_button(9691, X1 - 236, Y1 - 46, X1 - 126, Y1 - 16, "Call the vote", ready)) {
        s.call_vote(st.vote_target);
        st.vote_open = false, st.vote_target = 0;
        ui::dialog_close();
    }
    if (ui::text_button(9692, X1 - 116, Y1 - 46, X1 - 16, Y1 - 16, "Cancel")) {
        st.vote_open = false;
        ui::dialog_close();
    }
    ui::dialog_end();
}

void vote_banner(App& app) {
    Session& s = app.session();
    if (!s.vote) return;
    const double since = app.now() - s.vote_at;
    VoteState& v = *s.vote;
    if (!v.active) {
        // The result, for a few seconds.
        if (since > 5.0) {
            s.vote.reset();
            return;
        }
    } else if (since > double(v.seconds_left) + 1.0) {
        s.vote.reset();
        return;
    }
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const float W = ui::kStageW;
    const float x = W * 0.5f - 260, y = 92, w = 520, h = v.active ? 74.0f : 40.0f;
    ui::panel(dl, x, y, w, h, nullptr);
    if (!v.active) {
        ui::text(dl, ui::font_bold(), 15, x + w * 0.5f, y + 11, ui::col(ui::pal().gold_bright), v.result, ui::Align::Center);
        return;
    }
    ui::text(dl, ui::font_bold(), 15, x + 14, y + 8, ui::col(ui::pal().gold_bright), "VOTE: remove " + v.target_name + " from the room?");
    ui::text(dl, ui::font_body(), 13, x + 14, y + 30, ui::col(ui::pal().text_dim),
             eng::str::format("Called by %s.  Yes %u, No %u (%u of %u needed).  %.0f s", v.caller_name.c_str(), v.yes, v.no, v.needed, v.voters,
                              std::max(0.0, double(v.seconds_left) - since)));
    if (v.can_vote) {
        ui::text(dl, ui::font_bold(), 13, x + 14, y + 50, ui::col(ui::pal().lime), "F1  Yes", ui::Align::Left);
        ui::text(dl, ui::font_bold(), 13, x + 110, y + 50, ui::col(ui::pal().red), "F2  No", ui::Align::Left);
        eng::Input& in = app.window().input();
        if (in.key_pressed(VK_F1)) s.cast_vote(true);
        else if (in.key_pressed(VK_F2)) s.cast_vote(false);
    } else {
        ui::text(dl, ui::font_body(), 13, x + 14, y + 50, ui::col(ui::pal().text_dim), v.target == s.session_id ? "This vote is about you." : "You have voted.");
    }
}

}  // namespace lsf
