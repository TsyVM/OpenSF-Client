// Clans: the Clan Lobby plate's dialog and the invitation an officer sends.
//
//   - with no clan: the clans there are, to join one (an open clan at once, a closed one by an
//     application its officers answer), or the client's own mark builder, to found your own;
//   - in a clan: its mark, facts and notice, then a tab each for the members (promote, demote,
//     remove, hand the clan over, ask a soldier in), the applications, the log and the other
//     clans. What each plate may do is the rank's (Game/Rules.hpp clan_may: Owner, Co-Owner,
//     Lieutenant, Member, Recruit); the server says no to the rest whatever is pressed.
//
// Drawn in the lobby kit (Game/Ui/Page.hpp); the server keeps the clans (Server/Clans.cpp).
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Emblem.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cmath>

namespace lsf {

namespace {

constexpr VanU32 kInk = VAN_COL32(230, 230, 226, 255);
constexpr VanU32 kLabel = VAN_COL32(206, 206, 200, 255);
constexpr VanU32 kDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kLime = VAN_COL32(202, 228, 80, 255);
constexpr VanU32 kGold = VAN_COL32(224, 196, 110, 255);
constexpr VanU32 kOnline = VAN_COL32(96, 208, 72, 255);
constexpr VanU32 kOfficer = VAN_COL32(120, 226, 168, 255);
constexpr VanU32 kWarn = VAN_COL32(236, 146, 38, 255);
constexpr VanU32 kRule = VAN_COL32(80, 80, 78, 255);

const char* kLayerNames[] = {"Background", "Frame", "Symbol"};

u8& layer_value(ClanMark& m, int layer) { return layer == 0 ? m.background : layer == 1 ? m.frame : m.symbol; }

}  // namespace

void clan_mark(App& app, const ClanMark& m, float x0, float y0, float x1, float y1) {
    // The clan's own emblem, when it has made one; else Soldier Front's pieces.
    if (m.own()) {
        ui::draw_emblem(VanGui::GetWindowDrawList(), m, ui::pg(x0, y0), ui::pg(x1, y1));
        return;
    }
    ui::Atlas& a = app.atlas();
    ui::picture_at(a.clan_piece(sf::MarkLayer::Background, m.background), x0, y0, x1, y1);
    if (m.frame) ui::picture_at(a.clan_piece(sf::MarkLayer::Frame, m.frame), x0, y0, x1, y1);
    if (m.symbol) ui::picture_at(a.clan_piece(sf::MarkLayer::Symbol, m.symbol), x0, y0, x1, y1);
}

namespace {

// No clan yet: the mark builder, the name, the notice.
void found_clan(App& app, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const sf::ClanMarks& marks = app.clan_marks();

    // The mark as it stands, and the layer being chosen.
    ui::heading(X0 + 18, Y0 + 34, X0 + 164, "MARK");
    ui::well(X0 + 18, Y0 + 54, X0 + 164, Y0 + 200);
    clan_mark(app, st.clan_mark, X0 + 27, Y0 + 63, X0 + 155, Y0 + 191);
    for (int l = 0; l < 3; ++l) {
        const float y = Y0 + 212 + 24.0f * float(l);
        const int count = marks.count(sf::MarkLayer(l));
        if (ui::radio(100 + l, X0 + 18, y, kLayerNames[l], st.clan_layer == l)) st.clan_layer = l;
        const int v = layer_value(st.clan_mark, l);
        ui::text_at(X0 + 110, y, X0 + 164, y + 18, v ? eng::str::format("%d/%d", v, count) : std::string("none"), kDim, ui::Align::Right, true, 11.0f);
    }
    // A mark picked at random: somewhere to start.
    if (ui::text_button(110, X0 + 18, Y0 + 290, X0 + 164, Y0 + 312, "Surprise me")) {
        const int b = std::max(1, marks.count(sf::MarkLayer::Background)), f = marks.count(sf::MarkLayer::Frame), sy = std::max(1, marks.count(sf::MarkLayer::Symbol));
        const u32 r = u32(app.now() * 1000.0) * 2654435761u;
        st.clan_mark = {u8(1 + r % u32(b)), u8(f ? (r >> 8) % u32(f + 1) : 0), u8(1 + (r >> 16) % u32(sy))};
    }

    // The clan's own emblem instead, made in the emblem maker.
    const bool own = st.clan_mark.own();
    if (ui::text_button(111, X0 + 184, Y0 + 290, X0 + 384, Y0 + 312, own ? "Change your own emblem" : "Make your own emblem", true,
                        "The emblem maker: shapes you place, size, turn and colour yourself"))
        open_emblem_maker(app, st.clan_mark, false);
    if (own) {
        if (ui::text_button(112, X0 + 392, Y0 + 290, X1 - 18, Y0 + 312, "Use Soldier Front's", true, "Back to the pieces of Soldier Front's own mark builder"))
            st.clan_mark.layers.clear();
        ui::heading(X0 + 184, Y0 + 34, X1 - 18, "YOUR OWN EMBLEM");
        ui::well(X0 + 184, Y0 + 54, X1 - 18, Y0 + 284);
        ui::text_at(X0 + 184, Y0 + 150, X1 - 18, Y0 + 170, eng::str::format("Made of %zu shape%s.", st.clan_mark.layers.size(), st.clan_mark.layers.size() == 1 ? "" : "s"),
                    kInk, ui::Align::Center, true);
        ui::text_at(X0 + 184, Y0 + 172, X1 - 18, Y0 + 190, "It is the clan's mark everywhere a mark is shown.", kDim, ui::Align::Center, true, 11.0f);
    }
    // The layer's pieces; a frame or a symbol may be left out (the first tile).
    const sf::MarkLayer layer = sf::MarkLayer(st.clan_layer);
    const bool optional = layer != sf::MarkLayer::Background;
    const int pieces = marks.count(layer);
    u8& value = layer_value(st.clan_mark, st.clan_layer);
    const int first = optional ? 0 : 1;
    int picked = -1;
    if (!own) ui::heading(X0 + 184, Y0 + 34, X1 - 18, eng::str::format("%s  (%d)", kLayerNames[st.clan_layer], pieces));
    if (!own)
        picked = ui::tile_grid(200 + st.clan_layer, X0 + 184, Y0 + 54, X1 - 18, Y0 + 284, pieces + (optional ? 1 : 0), 44, int(value) - first,
                                     [&](int i, float x0, float y0, float x1, float y1) {
                                         const int n = i + first;
                                         if (n == 0) {
                                             ui::text_at(x0, y0, x1, y1, "none", kDim, ui::Align::Center, true, 11.0f);
                                             return;
                                         }
                                         // Frames and symbols are seen best on the background chosen.
                                         if (layer != sf::MarkLayer::Background)
                                             ui::picture_at(app.atlas().clan_piece(sf::MarkLayer::Background, st.clan_mark.background), x0, y0, x1, y1,
                                                            VAN_COL32(255, 255, 255, 90));
                                         ui::picture_at(app.atlas().clan_piece(layer, n), x0, y0, x1, y1);
                                     });
    if (picked >= 0) value = u8(picked + first);

    // The name and the notice.
    ui::heading(X0 + 18, Y0 + 326, X1 - 18, "CLAN");
    ui::text_at(X0 + 18, Y0 + 346, X0 + 110, Y0 + 365, "Clan name", kLabel, ui::Align::Left, true);
    const bool enter = ui::edit_at(120, X0 + 110, Y0 + 346, X1 - 18, Y0 + 365, st.clan_name, int(kClanNameMax), "2 to 12 letters, digits, spaces");
    ui::text_at(X0 + 18, Y0 + 370, X0 + 110, Y0 + 389, "Notice", kLabel, ui::Align::Left, true);
    (void)ui::edit_at(121, X0 + 110, Y0 + 370, X1 - 18, Y0 + 389, st.clan_notice, int(kClanNoticeMax), "a line for the members (optional)");
    std::string why;
    const std::string name(eng::str::trim(st.clan_name));
    const bool ok = valid_clan_name(name, &why);
    ui::text_at(X0 + 18, Y0 + 396, X1 - 18, Y0 + 414,
                name.empty() || ok ? "You become the clan's Owner. It is founded closed (soldiers apply); you can open it. Its name is kept for good." : why,
                name.empty() || ok ? kDim : kWarn, ui::Align::Left, true, 11.0f);

    const float BY = Y1 - 50, cx = (X0 + X1) * 0.5f;
    if (ui::kit_button(130, "confirm_1", cx - 78, BY, cx - 5, BY + 41, ok, "Found the clan") || (enter && ok)) s.create_clan(name, st.clan_notice, st.clan_mark);
    // The emblem maker takes this dialog's place and hands it back.
    if (st.emblem_open) ui::dialog_close();
    if (ui::kit_button(131, "cancel_1", cx + 5, BY, cx + 78, BY + 41)) {
        st.clan_open = false;
        ui::dialog_close();
    }
}

// ── What the dialog's parts share ──────────────────────────────────────────────

// The ink a clan rank is written in.
VanU32 rank_ink(ClanRank r) {
    switch (r) {
        case ClanRank::Owner: return kGold;
        case ClanRank::CoOwner: return kLime;
        case ClanRank::Lieutenant: return kOfficer;
        case ClanRank::Member: return kInk;
        default: return kLabel;
    }
}

// A plate that asks twice: pressed once it reads `sure` for three seconds, and pressed again in
// them it is done (true). `what` and `who` tell one such plate from another (ScreenState).
bool twice(App& app, int key, int what, const std::string& who, float x0, float y0, float x1, float y1, const char* label, const char* sure, bool enabled,
           const char* tooltip = nullptr) {
    ScreenState& st = app.state();
    const bool armed = enabled && st.clan_armed_what == what && st.clan_armed_for == who && app.now() - st.clan_armed < 3.0;
    if (!ui::text_button(key, x0, y0, x1, y1, armed ? sure : label, enabled, tooltip)) return false;
    if (armed) {
        st.clan_armed = -10;
        return true;
    }
    st.clan_armed = app.now(), st.clan_armed_what = what, st.clan_armed_for = who;
    return false;
}

// The clans there are, the largest first: each one's mark, name, owner, how many are in it and
// whether it is open. Asked for when first shown and every few seconds while it is. Returns the
// clan picked, or null.
const proto::ClanListing* clans_list(App& app, float x0, float y0, float x1, float y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    if (app.now() - st.clan_browse_at > 5.0) {
        st.clan_browse_at = app.now();
        s.browse_clans();
    }
    if (!s.clan_directory || s.clan_directory->clans.empty()) {
        ui::well(x0, y0, x1, y1);
        ui::text_at(x0, y0 + 60, x1, y0 + 80, s.clan_directory ? "There are no clans yet. Found the first one." : "Asking the server for the clans...", kDim,
                    ui::Align::Center, true);
        return nullptr;
    }
    const std::vector<proto::ClanListing>& clans = s.clan_directory->clans;
    int sel = -1;
    for (size_t i = 0; i < clans.size(); ++i)
        if (clans[i].name == st.clan_browse_sel) sel = int(i);
    const int clicked = ui::rows(640, x0, y0, x1, y1, int(clans.size()), 36, sel, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const proto::ClanListing& c = clans[size_t(i)];
        clan_mark(app, c.mark, rx0 + 4, ry0 + 3, rx0 + 34, ry0 + 33);
        ui::text_at(rx0 + 42, ry0 + 1, rx0 + 250, ry0 + 19, c.name, kGold, ui::Align::Left, true, 13.0f);
        ui::text_at(rx0 + 42, ry0 + 18, rx0 + 250, ry1 - 1,
                    c.wins + c.losses + c.draws ? eng::str::format("Rank %u   %u-%u-%u   %s pts", unsigned(c.rank), c.wins, c.losses, c.draws, eng::str::thousands(c.points).c_str())
                                                : "Owner  " + c.owner,
                    c.wins + c.losses + c.draws ? kGold : kLabel, ui::Align::Left, false, 11.0f);
        ui::text_at(rx0 + 250, ry0, rx1 - 214, ry1, c.notice, kDim, ui::Align::Left, false, 11.0f);
        const bool full = int(c.members) >= kClanMaxMembers;
        ui::text_at(rx1 - 210, ry0 + 1, rx1 - 80, ry0 + 19, eng::str::format("%u / %d members", unsigned(c.members), kClanMaxMembers), full ? kWarn : kInk,
                    ui::Align::Right, true, 11.0f);
        ui::text_at(rx1 - 210, ry0 + 18, rx1 - 80, ry1 - 1, eng::str::format("%u online", unsigned(c.online)), c.online ? kOnline : kDim, ui::Align::Right, false,
                    11.0f);
        ui::text_at(rx1 - 74, ry0, rx1 - 6, ry1, c.open ? "OPEN" : "CLOSED", c.open ? kOnline : kWarn, ui::Align::Center, true, 11.0f);
    });
    if (clicked >= 0) st.clan_browse_sel = clans[size_t(clicked)].name, sel = clicked;
    return sel >= 0 ? &clans[size_t(sel)] : nullptr;
}

// ── No clan yet ────────────────────────────────────────────────────────────────
// The clans there are, to join one (an open clan takes you at once; a closed one's officers
// answer your application), or the mark builder, to found your own.
void no_clan(App& app, const proto::ClanState& c, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const float x0 = X0 + 18, x1 = X1 - 18;
    if (ui::tab_button(600, x0, Y0 + 32, x0 + 130, Y0 + 58, "Join a clan", st.clan_tab == 3)) st.clan_tab = 3;
    if (ui::tab_button(601, x0 + 134, Y0 + 32, x0 + 264, Y0 + 58, "Found a clan", st.clan_tab == 4)) st.clan_tab = 4;
    ui::fill_at(X0 + 12, Y0 + 62, X1 - 12, Y0 + 63, kRule);
    if (st.clan_tab == 4) return found_clan(app, X0, Y0 + 34, X1, Y1);

    float y = Y0 + 72;
    // An application waiting on an answer.
    if (!c.applied.empty()) {
        ui::well(x0, y, x1, y + 28);
        ui::text_at(x0 + 10, y, x1 - 120, y + 28, "You asked to join " + c.applied + ". Its officers will answer; you may wait, or take it back.", kGold,
                    ui::Align::Left, true);
        if (ui::text_button(641, x1 - 112, y + 3, x1 - 3, y + 25, "Take it back", true, "Withdraw your application")) s.clan_action(proto::ClanOp::CancelApply, {});
        y += 36;
    }
    ui::heading(x0, y, x1, eng::str::format("CLANS  %zu", s.clan_directory ? s.clan_directory->clans.size() : size_t(0)));
    const proto::ClanListing* picked = clans_list(app, x0, y + 22, x1, Y1 - 100);
    ui::text_wrapped(x0, Y1 - 94, x1, Y1 - 60,
                     picked ? (picked->open ? picked->name + " is open: join, and you are in at once, as a Recruit."
                                            : picked->name + " is closed: apply, and its Lieutenants, Co-Owners or Owner answer. You are told when they do.")
                            : "Pick a clan. An open clan takes you at once; a closed one's officers answer your application.",
                     kDim, 11.0f);

    const float BY = Y1 - 50;
    const bool full = picked && int(picked->members) >= kClanMaxMembers;
    const bool asked = picked && eng::str::iequals(picked->name, c.applied);
    const char* label = !picked || picked->open ? "Join" : asked ? "Asked" : "Apply";
    const char* why = !picked ? "Pick a clan first" : full ? "That clan is full" : asked ? "You asked already: its officers will answer" : nullptr;
    if (ui::text_button(642, x0, BY + 8, x0 + 130, BY + 32, label, picked && !full && !asked, why) && picked) s.clan_action(proto::ClanOp::Apply, {}, picked->name);
    if (ui::text_button(643, x0 + 136, BY + 8, x0 + 236, BY + 32, "Refresh", true, "Ask the server for the list again")) st.clan_browse_at = -100;
    if (ui::kit_button(644, "close_1", X1 - 91, BY, X1 - 18, BY + 41)) {
        st.clan_open = false;
        ui::dialog_close();
    }
}

// ── In a clan ──────────────────────────────────────────────────────────────────

// The members, the highest rank first: promote, demote, put out, hand the clan over, ask a
// soldier in. Each plate is live only for what your rank may do to the member picked.
void members_tab(App& app, const proto::ClanState& c, ClanRank mine, float x0, float y0, float x1, float y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    int sel = -1;
    for (size_t i = 0; i < c.members.size(); ++i)
        if (c.members[i].name == st.clan_member_sel) sel = int(i);
    ui::text_at(x0 + 28, y0, x0 + 200, y0 + 18, "Soldier", kDim, ui::Align::Left, true, 11.0f);
    ui::text_at(x0 + 200, y0, x0 + 350, y0 + 18, "Level", kDim, ui::Align::Left, true, 11.0f);
    ui::text_at(x0 + 350, y0, x0 + 460, y0 + 18, "Clan rank", kDim, ui::Align::Left, true, 11.0f);
    ui::text_at(x0 + 460, y0, x1 - 90, y0 + 18, "Joined", kDim, ui::Align::Left, true, 11.0f);
    const int clicked = ui::rows(610, x0, y0 + 20, x1, y1 - 36, int(c.members.size()), 24, sel, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const proto::ClanMember& m = c.members[size_t(i)];
        const ClanRank r = ClanRank(std::min<u8>(m.rank, u8(ClanRank::Owner)));
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(m.xp)), rx0 + 4, ry0 + 3, rx0 + 22, ry0 + 21);
        ui::text_at(rx0 + 28, ry0, rx0 + 200, ry1, m.name, m.name == s.profile.code_name ? VAN_COL32(224, 221, 94, 255) : kInk, ui::Align::Left, true);
        ui::text_at(rx0 + 200, ry0, rx0 + 350, ry1, rank_name(rank_for_xp(m.xp)), kLabel, ui::Align::Left, false, 11.0f);
        ui::text_at(rx0 + 350, ry0, rx0 + 460, ry1, clan_rank_name(r), rank_ink(r), ui::Align::Left, true);
        ui::text_at(rx0 + 460, ry0, rx1 - 90, ry1, m.joined ? ago(app, m.joined) : std::string("-"), kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(rx1 - 86, ry0, rx1 - 6, ry1, m.online ? "Online" : "Offline", m.online ? kOnline : kDim, ui::Align::Right, true, 11.0f);
    });
    if (clicked >= 0) st.clan_member_sel = c.members[size_t(clicked)].name, sel = clicked;
    const proto::ClanMember* picked = sel >= 0 ? &c.members[size_t(sel)] : nullptr;
    const bool other = picked && picked->name != s.profile.code_name;
    const ClanRank theirs = picked ? ClanRank(std::min<u8>(picked->rank, u8(ClanRank::Owner))) : ClanRank::Recruit;
    const std::string who = picked ? picked->name : std::string();

    const float by0 = y1 - 30, by1 = y1 - 6;
    const bool can_up = other && clan_may(mine, ClanPower::Promote, theirs);
    const std::string up_tip = can_up ? std::string("Make ") + who + " a " + clan_rank_name(ClanRank(u8(theirs) + 1)) : std::string("You promote up to the rank below your own");
    if (ui::text_button(611, x0, by0, x0 + 84, by1, "Promote", can_up, up_tip.c_str()) && picked) s.clan_action(proto::ClanOp::Promote, who);
    const bool can_down = other && clan_may(mine, ClanPower::Demote, theirs);
    const std::string down_tip = can_down ? std::string("Make ") + who + " a " + clan_rank_name(ClanRank(u8(theirs) - 1)) : std::string("You demote those below your own rank");
    if (ui::text_button(612, x0 + 90, by0, x0 + 174, by1, "Demote", can_down, down_tip.c_str()) && picked) s.clan_action(proto::ClanOp::Demote, who);
    const bool can_kick = other && clan_may(mine, ClanPower::Kick, theirs);
    if (twice(app, 613, 3, who, x0 + 180, by0, x0 + 280, by1, "Remove", "Sure? Remove", can_kick,
              can_kick ? "Put them out of the clan" : "Lieutenants and above remove those below their own rank"))
        s.clan_kick(who);
    // The Owner's alone: the clan handed to another member (the Owner becomes a Co-Owner).
    if (mine == ClanRank::Owner &&
        twice(app, 614, 1, who, x0 + 286, by0, x0 + 396, by1, "Make Owner", "Sure? Hand over", other, "Hand the clan to them: you become a Co-Owner"))
        s.clan_action(proto::ClanOp::Transfer, who);
    // Ask a soldier in by code name (they answer yes or no): open or closed, the same.
    const bool may_invite = clan_may(mine, ClanPower::Invite);
    const bool enter = ui::edit_at(615, x1 - 250, by0 + 2, x1 - 92, by1 - 2, st.clan_invite_name, int(proto::kNameMax), "a code name", false, may_invite);
    const bool can = may_invite && !st.clan_invite_name.empty();
    if ((ui::text_button(616, x1 - 86, by0, x1, by1, "Ask in", can, may_invite ? "Ask this soldier into the clan" : "Lieutenants and above ask soldiers in") || (enter && can)) && can) {
        s.clan_invite(std::string(eng::str::trim(st.clan_invite_name)));
        st.clan_invite_name.clear();
    }
}

// Those asking to join a closed clan, for its Lieutenants and above to answer.
void applications_tab(App& app, const proto::ClanState& c, ClanRank mine, float x0, float y0, float x1, float y1) {
    Session& s = app.session();
    const bool may = clan_may(mine, ClanPower::Answer);
    ui::heading(x0, y0, x1, may ? eng::str::format("ASKING TO JOIN  %zu", c.applicants.size()) : std::string("APPLICATIONS"));
    ui::text_at(x0, y0 + 20, x1, y0 + 36,
                c.open ? "The clan is open: soldiers join at once, and nobody waits here."
                       : "The clan is closed: soldiers apply, and a Lieutenant, a Co-Owner or the Owner answers.",
                kDim, ui::Align::Left, true, 11.0f);
    if (!may) {
        ui::well(x0, y0 + 42, x1, y1);
        ui::text_at(x0, y0 + 110, x1, y0 + 130, "Lieutenants and above see and answer applications.", kDim, ui::Align::Center, true);
        return;
    }
    ui::rows(620, x0, y0 + 42, x1, y1, int(c.applicants.size()), 28, -1, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const proto::ClanApplicant& a = c.applicants[size_t(i)];
        VanGui::GetWindowDrawList()->AddCircleFilled(ui::pg(rx0 + 10, (ry0 + ry1) * 0.5f), ui::pgy(4.0f), a.online ? kOnline : VAN_COL32(90, 90, 86, 255), 12);
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(a.xp)), rx0 + 20, ry0 + 5, rx0 + 38, ry0 + 23);
        ui::text_at(rx0 + 44, ry0, rx0 + 210, ry1, a.name, kGold, ui::Align::Left, true);
        ui::text_at(rx0 + 210, ry0, rx0 + 360, ry1, rank_name(rank_for_xp(a.xp)), kLabel, ui::Align::Left, false, 11.0f);
        ui::text_at(rx0 + 360, ry0, rx1 - 180, ry1, "asked " + ago(app, a.time), kDim, ui::Align::Left, false, 11.0f);
        if (ui::text_button(3400 + i * 2, rx1 - 172, ry0 + 3, rx1 - 92, ry1 - 3, "Accept", true, "Let them in, as a Recruit")) s.clan_action(proto::ClanOp::Accept, a.name);
        if (ui::text_button(3401 + i * 2, rx1 - 86, ry0 + 3, rx1 - 6, ry1 - 3, "Decline", true, "Turn the application down")) s.clan_action(proto::ClanOp::Decline, a.name);
    });
    if (c.applicants.empty()) ui::text_at(x0, y0 + 110, x1, y0 + 130, "Nobody is asking to join right now.", kDim, ui::Align::Center, true);
}

// What happened in the clan, the newest first.
void log_tab(App& app, const proto::ClanState& c, float x0, float y0, float x1, float y1) {
    ui::rows(630, x0, y0, x1, y1, int(c.log.size()), 20, -1, [&](int i, float rx0, float ry0, float rx1, float ry1, bool, bool) {
        const proto::ClanLogLine& l = c.log[size_t(i)];
        ui::text_at(rx0 + 8, ry0, rx0 + 110, ry1, ago(app, l.time), kDim, ui::Align::Left, false, 11.0f);
        ui::text_at(rx0 + 114, ry0, rx1 - 6, ry1, l.text, kInk, ui::Align::Left, false, 12.0f);
    });
    if (c.log.empty()) ui::text_at(x0, y0 + 80, x1, y0 + 100, "Nothing has happened yet.", kDim, ui::Align::Center, true);
}

// In a clan: its mark, its facts, its notice; then the members, the applications, the log and
// the other clans, a tab each; leaving it, or (its Owner) disbanding it.
void my_clan(App& app, const proto::ClanState& c, float X0, float Y0, float X1, float Y1) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const ClanRank mine = ClanRank(std::min<u8>(c.my_rank, u8(ClanRank::Owner)));
    const float x0 = X0 + 18, x1 = X1 - 18;

    // ── The clan ──
    ui::well(x0, Y0 + 36, x0 + 100, Y0 + 136);
    clan_mark(app, c.mark, x0 + 4, Y0 + 40, x0 + 96, Y0 + 132);
    ui::text_at(x0 + 114, Y0 + 34, x1 - 330, Y0 + 58, c.name, kGold, ui::Align::Left, true, 18.0f);
    ui::text_at(x1 - 330, Y0 + 36, x1 - 160, Y0 + 58, std::string("You: ") + clan_rank_name(mine), rank_ink(mine), ui::Align::Right, true);
    int online = 0;
    for (const auto& m : c.members) online += m.online;
    struct Fact {
        const char* name;
        std::string value;
        VanU32 ink;
    };
    const std::string battles = c.wins + c.losses + c.draws == 0
                                    ? std::string("None fought yet: meet another clan in a Clan War channel")
                                    : eng::str::format("%u won, %u lost, %u drawn   Clan Point %s   Clan Rank %u", c.wins, c.losses, c.draws, eng::str::thousands(c.points).c_str(),
                                                       unsigned(c.rank));
    const Fact facts[] = {{"Owner", c.master, kGold},
                          {"Founded", c.founded, kInk},
                          {"Members", eng::str::format("%zu / %d, %d online", c.members.size(), kClanMaxMembers, online), kInk},
                          {"Joining", c.open ? "Open: any soldier may join at once" : "Closed: by application or invitation", c.open ? kOnline : kWarn},
                          {"Battles", battles, c.wins + c.losses + c.draws ? kGold : kDim}};
    for (int i = 0; i < 5; ++i) {
        const float y = Y0 + 60 + 16.5f * float(i);
        ui::text_at(x0 + 114, y, x0 + 184, y + 18, facts[i].name, kLabel, ui::Align::Left, true);
        ui::text_at(x0 + 184, y, x1 - 160, y + 18, facts[i].value, facts[i].ink, ui::Align::Left, true);
    }
    // The Owner's and Co-Owners': the clan's own emblem, and whether it is open.
    if (clan_may(mine, ClanPower::Emblem) && ui::text_button(660, x1 - 150, Y0 + 36, x1, Y0 + 58, c.mark.own() ? "Change emblem" : "Make an emblem", true,
                                                             "The emblem maker: the clan's own mark, from shapes you place, size, turn and colour")) {
        open_emblem_maker(app, c.mark, true);
        ui::dialog_close();
    }
    if (clan_may(mine, ClanPower::Joining) &&
        ui::text_button(661, x1 - 150, Y0 + 62, x1, Y0 + 84, c.open ? "Close the clan" : "Open the clan", true,
                        c.open ? "Closed: soldiers apply and an officer answers, or are asked in"
                               : "Open: any soldier may join at once (those waiting on an answer are let in)"))
        s.clan_action(proto::ClanOp::SetOpen, {}, c.open ? "closed" : "open");

    // ── The notice (the Owner and Co-Owners write it) ──
    const bool may_write = clan_may(mine, ClanPower::Notice);
    ui::sprite_at("clan_notice_title", 0, 1, x0, Y0 + 147, x0 + 58, Y0 + 164);
    if (st.clan_notice_editing && may_write) {
        const bool enter = ui::edit_at(650, x0 + 64, Y0 + 144, x1 - 136, Y0 + 167, st.clan_notice_edit, int(kClanNoticeMax), "a line for the members");
        if (ui::text_button(651, x1 - 130, Y0 + 144, x1 - 68, Y0 + 167, "Save") || enter) {
            s.clan_action(proto::ClanOp::SetNotice, {}, st.clan_notice_edit);
            st.clan_notice_editing = false;
        }
        if (ui::text_button(652, x1 - 62, Y0 + 144, x1, Y0 + 167, "Cancel")) st.clan_notice_editing = false;
    } else {
        ui::well(x0 + 64, Y0 + 144, may_write ? x1 - 68 : x1, Y0 + 167);
        ui::text_at(x0 + 70, Y0 + 144, (may_write ? x1 - 68 : x1) - 6, Y0 + 167, c.notice.empty() ? "No notice." : c.notice, c.notice.empty() ? kDim : kInk,
                    ui::Align::Left, true);
        if (may_write && ui::text_button(653, x1 - 62, Y0 + 144, x1, Y0 + 167, "Edit", true, "Write the clan's notice")) {
            st.clan_notice_editing = true;
            st.clan_notice_edit = c.notice;
        }
    }

    // ── The tabs ──
    const bool answers = clan_may(mine, ClanPower::Answer);
    const std::string tabs[] = {eng::str::format("Members (%zu)", c.members.size()),
                                answers && !c.applicants.empty() ? eng::str::format("Applications (%zu)", c.applicants.size()) : std::string("Applications"),
                                "Log", "Other clans"};
    for (int t = 0; t < 4; ++t)
        if (ui::tab_button(600 + t, x0 + float(t) * 136, Y0 + 178, x0 + 132 + float(t) * 136, Y0 + 204, tabs[t], st.clan_tab == t)) st.clan_tab = t;
    ui::fill_at(X0 + 12, Y0 + 208, X1 - 12, Y0 + 209, kRule);
    const float y0 = Y0 + 216, y1 = Y1 - 60;
    if (st.clan_tab == 0) members_tab(app, c, mine, x0, y0, x1, y1);
    else if (st.clan_tab == 1) applications_tab(app, c, mine, x0, y0, x1, y1);
    else if (st.clan_tab == 2) log_tab(app, c, x0, y0, x1, y1);
    else (void)clans_list(app, x0, y0, x1, y1);

    // ── Leaving; the Owner's: disbanding ──
    const float BY = Y1 - 50;
    if (mine == ClanRank::Owner) {
        if (twice(app, 670, 2, {}, x0, BY + 8, x0 + 150, BY + 32, "Disband the clan", "Sure? Disband it", true,
                  "Every member is let go and the clan is gone. To leave it standing, hand it over first (Members, Make Owner)"))
            s.clan_action(proto::ClanOp::Disband, {});
    } else if (twice(app, 671, 4, {}, x0, BY + 8, x0 + 150, BY + 32, "Leave the clan", "Sure? Leave", true)) {
        s.leave_clan();
    }
    if (ui::kit_button(672, "close_1", X1 - 91, BY, X1 - 18, BY + 41)) {
        st.clan_open = false;
        ui::dialog_close();
    }
}

}  // namespace

// ── The emblem maker ───────────────────────────────────────────────────────────
// A clan's own emblem is made here and nowhere else: shapes off the palette, each placed, sized
// and turned by hand on the canvas (or by the sliders), in a colour of the maker's choosing.
// Nothing is uploaded; what is kept and sent is the shapes' numbers (Rules.hpp EmblemLayer).

namespace {

struct Swatch {
    u8 r, g, b;
};
constexpr Swatch kSwatches[16] = {{255, 255, 255}, {200, 200, 196}, {120, 120, 116}, {20, 20, 18},   {220, 40, 36},  {128, 22, 22},
                                  {236, 146, 38},  {224, 196, 110}, {244, 226, 80},  {202, 228, 80},  {60, 160, 60},  {40, 130, 120},
                                  {90, 200, 236},  {50, 100, 210},  {26, 44, 96},    {140, 70, 190}};

EmblemLayer make_layer(EmblemShape shape, int x, int y, int w, int h, const Swatch& c, u8 style = 0) {
    EmblemLayer l;
    l.shape = u8(shape), l.x = u8(x), l.y = u8(y), l.w = u8(w), l.h = u8(h);
    l.r = c.r, l.g = c.g, l.b = c.b, l.style = style;
    return l;
}

// An emblem to start from: a ground, a rim and a device, in colours that go together.
ClanMark random_emblem(u32 seed) {
    auto next = [&] { return seed = seed * 1664525u + 1013904223u, seed >> 8; };
    static const EmblemShape grounds[] = {EmblemShape::Shield, EmblemShape::Circle, EmblemShape::Hexagon, EmblemShape::Diamond, EmblemShape::RoundedSquare,
                                          EmblemShape::Octagon};
    static const EmblemShape devices[] = {EmblemShape::Star,    EmblemShape::Bolt,  EmblemShape::Crown,     EmblemShape::Snowflake, EmblemShape::Arrow,
                                          EmblemShape::Cross,   EmblemShape::Chevron, EmblemShape::Crescent, EmblemShape::Triangle, EmblemShape::Star4,
                                          EmblemShape::Gear,    EmblemShape::Drop,  EmblemShape::Heart,     EmblemShape::Burst,     EmblemShape::Skull,
                                          EmblemShape::Flame,   EmblemShape::Knife, EmblemShape::Crosshair, EmblemShape::Helmet,    EmblemShape::Explosion,
                                          EmblemShape::Rifle,   EmblemShape::Medal, EmblemShape::Moon};
    static const Swatch dark[] = {{26, 44, 96}, {128, 22, 22}, {20, 20, 18}, {40, 130, 120}, {60, 60, 58}, {70, 40, 110}};
    static const Swatch light[] = {{224, 196, 110}, {255, 255, 255}, {244, 226, 80}, {202, 228, 80}, {90, 200, 236}, {236, 146, 38}};
    const EmblemShape ground = grounds[next() % std::size(grounds)];
    const Swatch d = dark[next() % std::size(dark)], l = light[next() % std::size(light)];
    ClanMark m;
    m.layers.push_back(make_layer(ground, 100, 100, 190, 190, d));
    m.layers.push_back(make_layer(ground, 100, 100, 172, 172, l, u8(1 | (1 << 1))));   // its rim, as an outline
    const EmblemShape device = devices[next() % std::size(devices)];
    m.layers.push_back(make_layer(device, 100, ground == EmblemShape::Shield ? 92 : 100, 96, 96, l));
    return m;
}

void emblem_slider(int key, float x0, float y, float x1, const char* name, float& v, float lo, float hi, const char* format, bool enabled) {
    ui::text_at(x0, y, x0 + 60, y + 18, name, enabled ? kLabel : kDim, ui::Align::Left, true);
    (void)ui::trackbar(key, x0 + 62, y, x1, y + 18, v, lo, hi, format, enabled);
}

// "#RRGGBB" (the # may be left off) to a colour; false when it is not one.
bool parse_hex(std::string_view s, u8& r, u8& g, u8& b) {
    s = eng::str::trim(s);
    if (!s.empty() && s[0] == '#') s.remove_prefix(1);
    if (s.size() != 6) return false;
    u32 v = 0;
    for (char c : s) {
        const int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (n < 0) return false;
        v = v << 4 | u32(n);
    }
    r = u8(v >> 16), g = u8(v >> 8), b = u8(v);
    return true;
}

}  // namespace

void open_emblem_maker(App& app, const ClanMark& from, bool for_clan) {
    ScreenState& st = app.state();
    st.emblem = from;
    if (!st.emblem.own()) st.emblem.layers = random_emblem(u32(app.now() * 1000.0)).layers;
    st.emblem_sel = int(st.emblem.layers.size()) - 1;
    st.emblem_drag = 0;
    st.emblem_undo.clear();
    st.emblem_redo.clear();
    st.emblem_editing = false;
    st.emblem_for_clan = for_clan;
    st.emblem_open = true;
}

void emblem_modal(App& app) {
    ScreenState& st = app.state();
    constexpr float X0 = 122, Y0 = 92, X1 = 902, Y1 = 676;
    bool open = true;
    if (!ui::dialog_begin("Emblem", X0, Y0, X1, Y1, "EMBLEM MAKER", &open)) {
        st.emblem_open = false;
        return;
    }
    std::vector<EmblemLayer>& layers = st.emblem.layers;
    // What it was as the frame began: a change made in it is one step Undo takes back (a drag
    // or a slider held is one step however many frames it runs).
    const std::vector<EmblemLayer> before = layers;
    bool stepped = false;   // an undo or a redo: not itself a step
    st.emblem_sel = std::clamp(st.emblem_sel, -1, int(layers.size()) - 1);
    EmblemLayer* sel = st.emblem_sel >= 0 ? &layers[size_t(st.emblem_sel)] : nullptr;
    VanDrawList* dl = VanGui::GetWindowDrawList();
    VanGuiIO& io = VanGui::GetIO();

    auto undo = [&] {
        if (st.emblem_undo.empty()) return;
        st.emblem_redo.push_back(layers);
        layers = std::move(st.emblem_undo.back());
        st.emblem_undo.pop_back();
        stepped = true;
    };
    auto redo = [&] {
        if (st.emblem_redo.empty()) return;
        st.emblem_undo.push_back(layers);
        layers = std::move(st.emblem_redo.back());
        st.emblem_redo.pop_back();
        stepped = true;
    };
    auto reselect = [&] {
        st.emblem_sel = std::clamp(st.emblem_sel, -1, int(layers.size()) - 1);
        sel = st.emblem_sel >= 0 ? &layers[size_t(st.emblem_sel)] : nullptr;
    };

    // The keys: Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z), Delete, the arrows nudge (Shift: five).
    if (!io.WantTextInput && st.emblem_drag == 0) {
        if (io.KeyCtrl && VanGui::IsKeyPressed(VanGuiKey_Z, false)) io.KeyShift ? redo() : undo();
        else if (io.KeyCtrl && VanGui::IsKeyPressed(VanGuiKey_Y, false)) redo();
        reselect();
        if (sel && VanGui::IsKeyPressed(VanGuiKey_Delete, false)) {
            layers.erase(layers.begin() + st.emblem_sel);
            st.emblem_sel = std::min(st.emblem_sel, int(layers.size()) - 1);
            reselect();
        }
        if (sel) {
            const int step = io.KeyShift ? 5 : 1;
            auto nudge = [&](u8& v, int by) { v = u8(std::clamp(int(v) + by, 0, 200)); };
            if (VanGui::IsKeyPressed(VanGuiKey_LeftArrow)) nudge(sel->x, -step);
            if (VanGui::IsKeyPressed(VanGuiKey_RightArrow)) nudge(sel->x, step);
            if (VanGui::IsKeyPressed(VanGuiKey_UpArrow)) nudge(sel->y, -step);
            if (VanGui::IsKeyPressed(VanGuiKey_DownArrow)) nudge(sel->y, step);
        }
    }

    // ── The canvas: a square on the screen, whatever shape the page is stretched to ──
    constexpr float CH = 300;                                  // its height, page units
    const float CW = CH * ui::pgy(1) / std::max(0.01f, ui::pgx(1));
    const float CX = X0 + 18 + (300 - CW) * 0.5f, CY = Y0 + 56;
    ui::heading(X0 + 18, Y0 + 34, X0 + 318, "EMBLEM");
    ui::well(CX - 1, CY - 1, CX + CW + 1, CY + CH + 1);
    const VanVec2 ca = ui::pg(CX, CY), cb = ui::pg(CX + CW, CY + CH);
    st.emblem_canvas[0] = ca.x, st.emblem_canvas[1] = ca.y, st.emblem_canvas[2] = cb.x, st.emblem_canvas[3] = cb.y;
    // A dark ground in two tones (what is bare shows through wherever the emblem is worn), and guides.
    const float cell = (cb.x - ca.x) / 16.0f;
    dl->AddRectFilled(ca, cb, VAN_COL32(34, 34, 31, 255));
    for (int gy = 0; gy < 16; ++gy)
        for (int gx = (gy & 1); gx < 16; gx += 2)
            dl->AddRectFilled({ca.x + cell * float(gx), ca.y + cell * float(gy)}, {ca.x + cell * float(gx + 1), ca.y + cell * float(gy + 1)}, VAN_COL32(42, 42, 38, 255));
    const VanVec2 mid{(ca.x + cb.x) * 0.5f, (ca.y + cb.y) * 0.5f};
    dl->AddLine({mid.x, ca.y}, {mid.x, cb.y}, VAN_COL32(255, 255, 255, 26));
    dl->AddLine({ca.x, mid.y}, {cb.x, mid.y}, VAN_COL32(255, 255, 255, 26));
    ui::draw_emblem(dl, st.emblem, ca, cb);
    // Emblem units (0..200) to pixels and back.
    auto px = [&](const VanVec2& e) { return VanVec2{ca.x + e.x / 200.0f * (cb.x - ca.x), ca.y + e.y / 200.0f * (cb.y - ca.y)}; };
    const VanVec2 mp = io.MousePos;
    const float ex = (mp.x - ca.x) / std::max(1.0f, cb.x - ca.x) * 200.0f, ey = (mp.y - ca.y) / std::max(1.0f, cb.y - ca.y) * 200.0f;
    const bool on_canvas = ex >= -14 && ex <= 214 && ey >= -14 && ey <= 214;

    // The shape in hand: its box, a handle at each corner to size it, one above it to turn it.
    VanVec2 corner[4]{}, turner{};
    if (sel) {
        const float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; ++k) corner[k] = ui::emblem_from_layer(*sel, su[k], sv[k]);
        const VanVec2 top = ui::emblem_from_layer(*sel, 0, -1), centre{float(sel->x), float(sel->y)};
        const float len = std::max(1.0f, std::sqrt((top.x - centre.x) * (top.x - centre.x) + (top.y - centre.y) * (top.y - centre.y)));
        turner = {top.x + (top.x - centre.x) / len * 16.0f, top.y + (top.y - centre.y) / len * 16.0f};
    }
    auto near = [&](const VanVec2& e, float r) { return (e.x - ex) * (e.x - ex) + (e.y - ey) * (e.y - ey) <= r * r; };

    if (st.emblem_drag == 0 && io.MouseClicked[0] && on_canvas) {
        if (sel && near(turner, 8)) {
            st.emblem_drag = 3;
        } else if (sel && (near(corner[0], 7) || near(corner[1], 7) || near(corner[2], 7) || near(corner[3], 7))) {
            st.emblem_drag = 2;
        } else {
            // The shape under the pointer, the topmost first.
            int hit = -1;
            for (int k = int(layers.size()) - 1; k >= 0 && hit < 0; --k) {
                const VanVec2 q = ui::emblem_to_layer(layers[size_t(k)], ex, ey);
                if (std::fabs(q.x) <= 1.0f && std::fabs(q.y) <= 1.0f) hit = k;
            }
            st.emblem_sel = hit;
            sel = hit >= 0 ? &layers[size_t(hit)] : nullptr;
            if (sel) st.emblem_drag = 1, st.emblem_grab_x = float(sel->x) - ex, st.emblem_grab_y = float(sel->y) - ey;
        }
    }
    if (st.emblem_drag != 0 && (!io.MouseDown[0] || !sel)) st.emblem_drag = 0;
    if (sel && st.emblem_drag == 1) {
        sel->x = u8(std::clamp(std::lround(ex + st.emblem_grab_x), 0l, 200l));
        sel->y = u8(std::clamp(std::lround(ey + st.emblem_grab_y), 0l, 200l));
    } else if (sel && st.emblem_drag == 2) {
        // In the shape's own turned frame, the pointer is its corner. Shift keeps its proportions.
        const float a = float(sel->turn) * 3.14159265f / 180.0f, dx = ex - float(sel->x), dy = ey - float(sel->y);
        float w = std::fabs(dx * std::cos(a) + dy * std::sin(a)) * 2.0f, h = std::fabs(-dx * std::sin(a) + dy * std::cos(a)) * 2.0f;
        if (io.KeyShift && sel->h > 0) {
            const float ratio = float(sel->w) / float(sel->h);
            if (w / std::max(h, 1.0f) > ratio) h = w / ratio;
            else w = h * ratio;
        }
        sel->w = u8(std::clamp(std::lround(w), 2l, 250l));
        sel->h = u8(std::clamp(std::lround(h), 2l, 250l));
    } else if (sel && st.emblem_drag == 3) {
        // The handle follows the pointer round the shape's middle. Shift turns by fifteen degrees.
        float deg = std::atan2(ey - float(sel->y), ex - float(sel->x)) * 180.0f / 3.14159265f + 90.0f;
        if (io.KeyShift) deg = std::round(deg / 15.0f) * 15.0f;
        sel->turn = u16((int(std::lround(deg)) % 360 + 360) % 360);
    }
    // The wheel turns the shape in hand, five degrees a notch.
    if (sel && on_canvas && io.MouseWheel != 0 && st.emblem_drag == 0) sel->turn = u16((int(sel->turn) + int(io.MouseWheel * 5.0f) + 720) % 360);
    if (sel) {
        const VanU32 line = VAN_COL32(202, 228, 80, 220);
        const VanVec2 p[4] = {px(corner[0]), px(corner[1]), px(corner[2]), px(corner[3])};
        dl->AddPolyline(p, 4, line, VanDrawFlags_Closed, 1.0f);
        for (const VanVec2& c : p) dl->AddRectFilled({c.x - 4, c.y - 4}, {c.x + 4, c.y + 4}, line);
        const VanVec2 top = px(ui::emblem_from_layer(*sel, 0, -1)), t = px(turner);
        dl->AddLine(top, t, line, 1.0f);
        dl->AddCircleFilled(t, 5.0f, VAN_COL32(34, 34, 31, 255), 16);
        dl->AddCircle(t, 5.0f, line, 16, 1.5f);
    }
    ui::text_at(X0 + 18, CY + CH + 4, X0 + 318, CY + CH + 19, "Drag a shape to move it, a corner to size it, the ring to turn it.", kDim, ui::Align::Left, false, 10.0f);
    ui::text_at(X0 + 18, CY + CH + 17, X0 + 318, CY + CH + 32, "Arrows nudge it, Delete takes it off, Ctrl+Z undoes.", kDim, ui::Align::Left, false, 10.0f);

    // As it is worn: on a card, in a list, beside a name.
    const float wy = CY + CH + 40;
    ui::text_at(X0 + 18, wy + 22, X0 + 100, wy + 40, "As it is worn", kLabel, ui::Align::Left, true);
    float wx = X0 + 110;
    for (float size : {64.0f, 32.0f, 16.0f}) {
        ui::well(wx, wy + 32 - size * 0.5f, wx + size + 4, wy + 36 + size * 0.5f);
        clan_mark(app, st.emblem, wx + 2, wy + 34 - size * 0.5f, wx + size + 2, wy + 34 + size * 0.5f);
        wx += size + 16;
    }

    // ── The palette: shapes, or pictures; a click puts one on the emblem ──
    const float SX0 = X0 + 334, SX1 = X0 + 548;
    if (ui::tab_button(518, SX0, Y0 + 32, SX0 + 104, Y0 + 52, "Shapes", st.emblem_palette == 0)) st.emblem_palette = 0;
    if (ui::tab_button(519, SX0 + 108, Y0 + 32, SX1, Y0 + 52, "Pictures", st.emblem_palette == 1)) st.emblem_palette = 1;
    const int first_shape = st.emblem_palette == 0 ? 0 : kEmblemFirstPicture;
    const int shapes = st.emblem_palette == 0 ? kEmblemFirstPicture : int(EmblemShape::Count) - kEmblemFirstPicture;
    const int added = ui::tile_grid(520 + st.emblem_palette * 1000, SX0, Y0 + 56, SX1, Y0 + 290, shapes, 34, -1, [&](int i, float x0, float y0, float x1, float y1) {
        EmblemLayer tile;
        tile.shape = u8(first_shape + i), tile.w = tile.h = 150;
        tile.r = 214, tile.g = 214, tile.b = 208;
        ui::draw_emblem_layer(dl, tile, ui::pg((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), std::min(ui::pgx(x1 - x0), ui::pgy(y1 - y0)) * 0.5f);
        if (VanGui::IsMouseHoveringRect(ui::pg(x0, y0), ui::pg(x1, y1))) VanGui::SetTooltip("%s", emblem_shape_name(tile.shape));
    });
    if (added >= 0) {
        if (layers.size() >= kEmblemLayers) {
            ui::toast(ui::Toast::Info, "An emblem holds %d shapes: take one off to add another.", int(kEmblemLayers));
        } else {
            EmblemLayer l;
            l.shape = u8(first_shape + added), l.w = l.h = 90;
            if (sel) l.r = sel->r, l.g = sel->g, l.b = sel->b;   // in the colour last in hand
            layers.push_back(l);
            st.emblem_sel = int(layers.size()) - 1;
            sel = &layers.back();
        }
    }
    ui::text_at(SX0, Y0 + 294, SX1, Y0 + 309, "A click adds it to the emblem; point at one for its name.", kDim, ui::Align::Left, false, 10.0f);

    // ── The emblem's shapes, the last on top ──
    const float LX0 = X0 + 564, LX1 = X1 - 18;
    ui::heading(LX0, Y0 + 34, LX1, eng::str::format("ON THE EMBLEM  (%zu of %d)", layers.size(), int(kEmblemLayers)));
    const int clicked = ui::rows(521, LX0, Y0 + 56, LX1, Y0 + 262, int(layers.size()), 22, int(layers.size()) - 1 - st.emblem_sel,
                                 [&](int i, float x0, float y0, float x1, float y1, bool, bool) {
                                     // Listed top shape first, as they lie on the emblem.
                                     const EmblemLayer& l = layers[layers.size() - 1 - size_t(i)];
                                     EmblemLayer icon = l;
                                     icon.x = icon.y = 100, icon.turn = l.turn, icon.a = 255;
                                     const float most = float(std::max(l.w, l.h));
                                     icon.w = u8(float(l.w) / most * 170.0f), icon.h = u8(float(l.h) / most * 170.0f);
                                     ui::draw_emblem_layer(dl, icon, ui::pg(x0 + 13, (y0 + y1) * 0.5f), ui::pgy(9));
                                     ui::text_at(x0 + 30, y0, x1 - 4, y1, emblem_shape_name(l.shape), kInk, ui::Align::Left, true);
                                 });
    if (clicked >= 0) st.emblem_sel = int(layers.size()) - 1 - clicked, sel = &layers[size_t(st.emblem_sel)];
    {
        const float by = Y0 + 268, bw = (LX1 - LX0 - 9) / 4.0f;
        const int at = st.emblem_sel, n = int(layers.size());
        if (ui::text_button(530, LX0, by, LX0 + bw, by + 22, "Raise", sel && at < n - 1, "Over the shape above it"))
            std::swap(layers[size_t(at)], layers[size_t(at + 1)]), ++st.emblem_sel;
        if (ui::text_button(531, LX0 + bw + 3, by, LX0 + bw * 2 + 3, by + 22, "Lower", sel && at > 0, "Under the shape below it"))
            std::swap(layers[size_t(at)], layers[size_t(at - 1)]), --st.emblem_sel;
        if (ui::text_button(532, LX0 + bw * 2 + 6, by, LX0 + bw * 3 + 6, by + 22, "Copy", sel && layers.size() < kEmblemLayers, "Another of it, a little to the side")) {
            EmblemLayer copy = layers[size_t(at)];
            copy.x = u8(std::min(200, int(copy.x) + 10)), copy.y = u8(std::min(200, int(copy.y) + 10));
            layers.insert(layers.begin() + at + 1, copy);
            st.emblem_sel = at + 1;
        }
        if (ui::text_button(533, LX0 + bw * 3 + 9, by, LX1, by + 22, "Remove", sel != nullptr, "Take the shape off the emblem (Delete)")) {
            layers.erase(layers.begin() + at);
            st.emblem_sel = std::min(at, int(layers.size()) - 1);
        }
        reselect();
    }

    // ── The shape in hand: where, how big, which way up, how solid ──
    const float PY = Y0 + 318;
    ui::heading(SX0, PY, LX1, sel ? eng::str::format("THE %s", eng::str::upper(emblem_shape_name(sel->shape)).c_str()) : std::string("NO SHAPE IN HAND"));
    EmblemLayer blank;
    EmblemLayer& l = sel ? *sel : blank;
    const bool has = sel != nullptr;
    float y = PY + 22;
    auto number = [&](int key, const char* name, float lo, float hi, const char* format, auto get, auto set) {
        float v = float(get());
        emblem_slider(key, SX0, y, SX1, name, v, lo, hi, format, has);
        if (has) set(v);
        y += 22;
    };
    number(540, "Across", 0, 200, "%.0f", [&] { return l.x; }, [&](float v) { l.x = u8(std::lround(v)); });
    number(541, "Down", 0, 200, "%.0f", [&] { return l.y; }, [&](float v) { l.y = u8(std::lround(v)); });
    number(542, "Width", 2, 250, "%.0f", [&] { return l.w; }, [&](float v) { l.w = u8(std::lround(v)); });
    number(543, "Height", 2, 250, "%.0f", [&] { return l.h; }, [&](float v) { l.h = u8(std::lround(v)); });
    number(544, "Turn", 0, 359, "%.0f deg", [&] { return l.turn; }, [&](float v) { l.turn = u16(std::lround(v)) % 360; });
    {
        bool outline = l.outline(), mirrored = l.mirrored();
        float weight = float(l.weight());
        if (ui::check(545, SX0, y + 2, "Outline only", outline, has, "Its edge alone, not filled in") && has) l.style = u8((l.style & ~1) | (outline ? 1 : 0));
        if (ui::check(546, SX0 + 110, y + 2, "Mirrored", mirrored, has, "Flipped left to right") && has) l.style = u8((l.style & ~16) | (mirrored ? 16 : 0));
        y += 24;
        const bool lined = outline || l.shape == u8(EmblemShape::Snowflake) || l.shape >= kEmblemFirstPicture;
        emblem_slider(547, SX0, y, SX1, "Weight", weight, 0, 7, "%.0f", has && lined);
        if (has) l.style = u8((l.style & ~14) | (int(std::lround(weight)) << 1));
        y += 22;
    }
    number(548, "Opacity", 16, 255, "%.0f", [&] { return l.a; }, [&](float v) { l.a = u8(std::lround(v)); });

    // Its colour: a swatch, the picker (hue down the bar, shade across the square), or a code.
    float cy = PY + 22;
    for (int k = 0; k < 16; ++k) {
        const Swatch& sw = kSwatches[k];
        const float sx = LX0 + 25.0f * float(k % 8), sy = cy + 22.0f * float(k / 8);
        const bool chosen = has && l.r == sw.r && l.g == sw.g && l.b == sw.b;
        bool hovered = false;
        if (ui::region(550 + k, sx, sy, sx + 21, sy + 18, &hovered) && has) l.r = sw.r, l.g = sw.g, l.b = sw.b, ui::click_sound();
        dl->AddRectFilled(ui::pg(sx, sy), ui::pg(sx + 21, sy + 18), VAN_COL32(sw.r, sw.g, sw.b, has ? 255 : 90));
        dl->AddRect(ui::pg(sx, sy), ui::pg(sx + 21, sy + 18), chosen ? VAN_COL32(255, 255, 255, 255) : hovered && has ? VAN_COL32(202, 228, 80, 255) : VAN_COL32(0, 0, 0, 255),
                    0.0f, 0, chosen ? 2.0f : 1.0f);
    }
    cy += 50;
    {
        constexpr float kPickerH = 112;   // page units
        const float h_px = ui::pgy(kPickerH);
        const VanGuiStyle& style = VanGui::GetStyle();
        const float w_px = h_px + VanGui::GetFrameHeight() + style.ItemInnerSpacing.x;
        VanGui::SetCursorScreenPos(ui::pg(LX0, cy));
        VanGui::PushID("emblem_colour");
        VanGui::BeginDisabled(!has);
        float rgb[3] = {float(l.r) / 255.0f, float(l.g) / 255.0f, float(l.b) / 255.0f};
        VanGui::SetNextItemWidth(w_px);
        if (VanGui::ColorPicker3("##picker", rgb,
                                 VanGuiColorEditFlags_PickerHueBar | VanGuiColorEditFlags_NoSidePreview | VanGuiColorEditFlags_NoInputs | VanGuiColorEditFlags_NoLabel |
                                     VanGuiColorEditFlags_NoAlpha | VanGuiColorEditFlags_NoTooltip | VanGuiColorEditFlags_NoDragDrop) &&
            has)
            l.r = u8(std::lround(rgb[0] * 255.0f)), l.g = u8(std::lround(rgb[1] * 255.0f)), l.b = u8(std::lround(rgb[2] * 255.0f));
        VanGui::EndDisabled();
        VanGui::PopID();

        // Beside it: the colour large, and its code to read or type.
        const float hx = LX0 + w_px / std::max(0.01f, ui::pgx(1)) + 10;
        dl->AddRectFilled(ui::pg(hx, cy), ui::pg(LX1, cy + 40), VAN_COL32(l.r, l.g, l.b, has ? 255 : 60));
        dl->AddRect(ui::pg(hx, cy), ui::pg(LX1, cy + 40), VAN_COL32(0, 0, 0, 255));
        const u32 rgb_now = u32(l.r) << 16 | u32(l.g) << 8 | l.b;
        if (st.emblem_hex_rgb != rgb_now || st.emblem_hex.empty()) st.emblem_hex = eng::str::format("#%06X", rgb_now), st.emblem_hex_rgb = rgb_now;
        ui::text_at(hx, cy + 48, LX1, cy + 64, "Colour code", has ? kLabel : kDim, ui::Align::Left, true, 11.0f);
        (void)ui::edit_at(549, hx, cy + 66, LX1, cy + 85, st.emblem_hex, 7, "#RRGGBB");
        u8 r, g, b;
        if (has && parse_hex(st.emblem_hex, r, g, b) && (u32(r) << 16 | u32(g) << 8 | b) != rgb_now) {
            l.r = r, l.g = g, l.b = b;
            st.emblem_hex_rgb = u32(r) << 16 | u32(g) << 8 | b;
        }
    }

    // ── Start over, a surprise, undo, redo; Confirm, Cancel ──
    const float BY = Y1 - 50;
    if (ui::text_button(580, X0 + 18, BY + 8, X0 + 110, BY + 32, "Start over", !layers.empty(), "Take every shape off")) layers.clear(), st.emblem_sel = -1;
    if (ui::text_button(581, X0 + 116, BY + 8, X0 + 212, BY + 32, "Surprise me", true, "An emblem to start from")) {
        layers = random_emblem(u32(app.now() * 1000.0) ^ u32(layers.size() * 7919u)).layers;
        st.emblem_sel = int(layers.size()) - 1;
    }
    if (ui::text_button(582, X0 + 218, BY + 8, X0 + 290, BY + 32, "Undo", !st.emblem_undo.empty(), "Take the last change back (Ctrl+Z)")) undo();
    if (ui::text_button(583, X0 + 296, BY + 8, X0 + 368, BY + 32, "Redo", !st.emblem_redo.empty(), "Put it back again (Ctrl+Y)")) redo();
    if (layers.empty()) ui::text_at(X0 + 378, BY + 8, X1 - 190, BY + 32, "With no shapes, the clan wears its Soldier Front mark.", kDim, ui::Align::Left, true, 11.0f);
    const bool confirm = ui::kit_button(590, "confirm_1", X1 - 170, BY, X1 - 97, BY + 41);
    const bool cancel = ui::kit_button(591, "cancel_1", X1 - 87, BY, X1 - 14, BY + 41);

    // A change this frame is a step to undo; held changes (a drag, a slider) make one between them.
    if (layers != before && !stepped) {
        if (!st.emblem_editing) {
            st.emblem_undo.push_back(before);
            if (st.emblem_undo.size() > 64) st.emblem_undo.erase(st.emblem_undo.begin());
            st.emblem_redo.clear();
        }
        st.emblem_editing = true;
    }
    if (!io.MouseDown[0]) st.emblem_editing = false;
    reselect();

    if (confirm) {
        sanitize_mark(st.emblem);
        if (st.emblem_for_clan) app.session().set_clan_mark(st.emblem);
        else st.clan_mark.layers = st.emblem.layers;
    }
    if (confirm || cancel || !open) {
        st.emblem_open = false;
        st.emblem_drag = 0;
        if (open) ui::dialog_close();
    }
    ui::dialog_end();
}

void clan_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    constexpr float X0 = 142, Y0 = 92, X1 = 882, Y1 = 676;
    const bool member = s.clan && s.clan->member;
    if (!ui::dialog_begin("Clan", X0, Y0, X1, Y1, member ? "CLAN" : "CLANS", &st.clan_open)) {
        st.clan_open = false;
        return;
    }
    if (!s.clan) {
        ui::text_at(X0, Y0 + 200, X1, Y0 + 230, "Asking the server about your clan...", kDim, ui::Align::Center, true);
    } else if (member) {
        if (st.clan_tab > 3) st.clan_tab = 0;   // founded, or let in, a moment ago
        my_clan(app, *s.clan, X0, Y0, X1, Y1);
    } else {
        if (st.clan_tab < 3) st.clan_tab = 3;   // no clan: its tabs are the clans and founding one
        no_clan(app, *s.clan, X0, Y0, X1, Y1);
    }
    // (The emblem maker takes this dialog's place and hands it back: clan_open stays set.)
    ui::dialog_end();
}

void clan_invite_modal(App& app) {
    Session& s = app.session();
    if (s.clan_invites.empty()) return;
    const proto::ClanInvited inv = s.clan_invites.front();
    constexpr float X0 = 302, Y0 = 280, X1 = 722, Y1 = 470;
    bool open = true;
    if (!ui::dialog_begin("Clan invitation", X0, Y0, X1, Y1, "CLAN INVITATION", &open)) return;
    ui::well(X0 + 18, Y0 + 38, X0 + 102, Y0 + 122);
    clan_mark(app, inv.mark, X0 + 20, Y0 + 40, X0 + 100, Y0 + 120);
    ui::text_at(X0 + 116, Y0 + 40, X1 - 18, Y0 + 62, inv.clan, kGold, ui::Align::Left, true, 16.0f);
    ui::text_at(X0 + 116, Y0 + 68, X1 - 18, Y0 + 86, eng::str::format("%s asks you to join.", inv.from.c_str()), kInk, ui::Align::Left, true);
    ui::text_at(X0 + 116, Y0 + 90, X1 - 18, Y0 + 108, eng::str::format("%u member%s", unsigned(inv.members), inv.members == 1 ? "" : "s"), kDim,
                ui::Align::Left, true);
    const float BY = Y1 - 44, cx = (X0 + X1) * 0.5f;
    int answer = -1;
    if (ui::kit_button(400, "yes_1", cx - 88, BY, cx - 5, BY + 29)) answer = 1;
    if (ui::kit_button(401, "no_1", cx + 5, BY, cx + 88, BY + 29) || !open) answer = 0;
    if (answer >= 0) {
        s.clan_answer(inv.clan, answer == 1);
        s.clan_invites.pop_front();
        if (open) ui::dialog_close();
    }
    ui::dialog_end();
}

}  // namespace lsf
