// The server select (PageServer), the channels (pagecommon + Page1) and the lobby (pagecommon +
// Page2), drawn from the client's own page scripts (Game/Ui/Page.hpp): every element where the
// script puts it, in the lobby's own art. What happens when one is used is ours.
#include "Game/Screens/Screens.hpp"

#include "Game/Render/ModelStage.hpp"
#include "Game/Ui/Page.hpp"
#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"

#include <vangui/vangui.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace lsf {

ui::Picture weapon_icon(App& app, const WeaponDef& w);

namespace {

using ui::Align;

constexpr VanU32 kWhite = VAN_COL32(236, 236, 232, 255);
constexpr VanU32 kSoft = VAN_COL32(200, 200, 194, 255);
constexpr VanU32 kDim = VAN_COL32(128, 128, 122, 255);
constexpr VanU32 kOn = VAN_COL32(96, 208, 72, 255);        // "On" in the room list
constexpr VanU32 kOff = VAN_COL32(236, 146, 38, 255);      // "Off"
constexpr VanU32 kLocation = VAN_COL32(126, 196, 88, 255); // the bottom strip's "where you are"
constexpr VanU32 kGold = VAN_COL32(224, 196, 110, 255);

// The pages these screens draw, by name.
struct Pages {
    const ui::Page& server;
    const ui::Page& channel;
    const ui::Page& lobby;
    const ui::Page& room;
};
}  // namespace

const ui::Page& lobby_page(App& app, const char* name);

namespace {
Pages pages(App& app) {
    return {lobby_page(app, "PageServer"), lobby_page(app, "Page1"), lobby_page(app, "Page2"), lobby_page(app, "Page3")};
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

}  // namespace

// ── What every page screen shares (Screens.hpp) ────────────────────────────────

const ui::Page& lobby_page(App& app, const char* name) {
    static std::map<std::string, ui::Page> loaded;
    auto [it, fresh] = loaded.try_emplace(name);
    if (!it->second.loaded() && app.data_ready()) it->second.load(app.data(), name);
    return it->second;
}

// The bottom strip the background paints: where you are on the left, the ping on the right.
void bottom_strip(App& app, std::string_view where) {
    ui::text_at(0, 748, 258, 768, where, kLocation, Align::Center, true, 11.0f);
    const Session& s = app.session();
    std::string right;
    if (s.signed_in()) right = eng::str::format("Ping %d ms", std::max(0, s.ping_ms()));
    if (app.settings().show_fps) right += eng::str::format("%s%.0f FPS  %s", right.empty() ? "" : "   ", double(app.fps()), eng::api_name(app.device().api()));
    ui::text_at(767, 748, 1020, 768, right, kDim, Align::Right, false, 11.0f);
}

std::string marquee_text(const Session& s) {
    // The running events' banners first, then the line a Game Master keeps (the house's own until
    // the server has said).
    const std::string line = s.shop_config().marquee;
    const std::string banner = events_banner(s);
    return banner.empty() ? line : banner + "          " + line;
}

// pagecommon: the background, the mark, the shops and options, help and the marquee.
Nav common_chrome(App& app, int current_nav) {
    const ui::Page& common = lobby_page(app, "pagecommon");
    ui::draw_static(common, {501});
    ui::marquee(common, 501, marquee_text(app.session()));
    Nav nav = Nav::None;
    switch (ui::tabs(common, 500, current_nav)) {
        case 0: nav = Nav::CharShop; break;
        case 1: nav = Nav::WeaponShop; break;
        case 2: nav = Nav::ItemShop; break;
        case 3: nav = Nav::Inventory; break;
        case 5: nav = Nav::Recordings; break;
        case 6: nav = Nav::Options; break;
        default: break;
    }
    // The Clan Lobby plate (the kit's clan_wait_room_1; the scripts dropped their tab 4 for it), in
    // a box of its own between the plates and Help.
    constexpr VanU32 kFrame = VAN_COL32(80, 80, 78, 255);
    ui::sprite_at("bg_gray_1", 0, 1, 843, 18, 931, 69);
    ui::fill_at(843, 18, 931, 19, kFrame), ui::fill_at(843, 68, 931, 69, kFrame);
    ui::fill_at(843, 18, 844, 69, kFrame), ui::fill_at(930, 18, 931, 69, kFrame);
    if (ui::kit_button(4, "clan_wait_room_1", 850, 23, 923, 64, true, "Clans: found one, or see yours", 4)) nav = Nav::Clan;
    if (ui::button(common, 502, true, nullptr, "How to play")) open_settings(app, 1, 0);
    // Rewards: the day's sign-in, hours and quests, the events (a dot when something waits to be
    // collected). Staff: their panel (reports, accounts, recordings; F9 from anywhere as well).
    // Staff see the two stacked in the one gap.
    const bool staff = app.session().signed_in() && app.session().profile.staff();
    // Rewards and Friends stacked in the gap (and Staff below them, for staff).
    const float ry0 = staff ? 18 : 19, ry1 = staff ? 34 : 41;
    if (app.session().signed_in() && ui::text_button(9001, 262, ry0, 330, ry1, "Rewards", true, "The day's sign-in, hours of play and quests, and the events")) open_rewards(app);
    if (app.session().signed_in()) (void)friends_plate(app, 9003, 262, staff ? 36 : 45, 330, staff ? 52 : 67);
    if (rewards_waiting(app.session())) {
        VanDrawList* dl = VanGui::GetWindowDrawList();
        const float pulse = 0.6f + 0.4f * std::sin(float(app.now()) * 4.0f);
        dl->AddCircleFilled(ui::pg(326, ry0 + 4), ui::pgy(4.5f), VAN_COL32(236, 200, 96, int(255 * pulse)), 16);
        dl->AddCircle(ui::pg(326, ry0 + 4), ui::pgy(4.5f), VAN_COL32(0, 0, 0, 200), 16);
    }
    if (staff && ui::text_button(9002, 262, 54, 330, 70, "Staff", true, "Reports, accounts and match recordings (F9)")) {
        app.state().staff_open = !app.state().staff_open;
        app.state().staff_asked = false;
    }
    return nav;
}

namespace {

std::vector<ui::Line> chat_lines(const Session& s, bool room) {
    std::vector<ui::Line> out;
    for (const auto& e : room ? s.room_chat : s.lobby_chat) out.push_back(chat_view(e.line));
    return out;
}

// A chat box on a page: the log, the scope combo and the line. The scopes are the page's own
// (`plain`, and Team in a room) and Whisper ("name text"), Clan and Global; any line may also
// start with a command (/w /r /c /g /t /a: Screens/Social.cpp). Global chat's switch sits in the
// log's top corner.
void chat_box(App& app, const ui::Page& page, int log_id, int edit_id, int combo_id, bool room) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const auto lines = chat_lines(s, room);
    ui::text_log(page, log_id, lines);
    static const std::string lobby_scopes[] = {"Normal", "Whisper", "Clan", "Global"};
    static const std::string room_scopes[] = {"Normal", "Whisper", "Clan", "Global", "Team"};
    const std::span<const std::string> scopes = room ? std::span<const std::string>(room_scopes) : std::span<const std::string>(lobby_scopes);
    st.chat_scope = std::clamp(st.chat_scope, 0, int(scopes.size()) - 1);
    (void)ui::combo(page, combo_id, st.chat_scope, scopes);
    const char* hint = st.chat_scope == 1 ? "name text" : st.chat_scope == 3 && !s.global_chat() ? "global chat is off" : nullptr;
    if (ui::edit(page, edit_id, st.chat_line, hint) && !st.chat_line.empty()) {
        const proto::ChatScope plain = room ? proto::ChatScope::Room : proto::ChatScope::Lobby;
        std::string line = st.chat_line;
        if (line[0] != '/') {
            if (st.chat_scope == 1) line = "/w " + line;
            else if (st.chat_scope == 2) line = "/c " + line;
            else if (st.chat_scope == 3) line = "/g " + line;
            else if (st.chat_scope == 4) line = "/t " + line;
        }
        if (send_chat(app, line, plain)) st.chat_line = st.chat_scope == 1 ? st.chat_line.substr(0, st.chat_line.find(' ') + 1) : std::string();
    }
    // Global chat on or off, there and then (Options keeps it).
    float x0, y0, x1, y1;
    if (page.rect(log_id, x0, y0, x1, y1)) {
        const bool on = s.global_chat();
        if (ui::text_button(9905 + (room ? 1 : 0), x1 - 112, y0 + 3, x1 - 22, y0 + 21, on ? "Global: On" : "Global: Off", true,
                            on ? "Global chat is heard here and in matches: click to turn it off" : "Global chat is off: click to hear it")) {
            app.settings().global_chat = !on;
            app.save_settings();
            s.set_global_chat(!on);
        }
    }
}

}  // namespace

void page_chat(App& app, const ui::Page& page, int log_id, int edit_id, int combo_id, bool room) { chat_box(app, page, log_id, edit_id, combo_id, room); }

namespace {

const char* condition(float fill) { return fill > 0.9f ? "Full" : fill > 0.6f ? "Busy" : fill > 0.3f ? "Normal" : "Smooth"; }
VanU32 condition_colour(float fill) { return fill > 0.9f ? VAN_COL32(230, 80, 60, 255) : fill > 0.6f ? kOff : kOn; }

}  // namespace

// ── Server select (PageServer) ─────────────────────────────────────────────────

void draw_servers(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Pages p = pages(app);
    ui::page_begin("##page_server");
    ui::draw_static(p.server, {501});
    ui::marquee(p.server, 501, s.tv.news.empty() ? marquee_text(s) : s.tv.news);

    // The list from Team Vanilla (§8.4): asked for on arriving, again every half a minute, and
    // each server asked itself for its players and ping (§8.3).
    if (!s.servers_loading && app.now() - s.servers_at > 30.0) s.request_servers(app.now());

    // All / Official / Community / Favourites (the page's four plates), and Recent beside them.
    static const std::string tab_names[] = {"All", "Official", "Community", "Favourites"};
    auto& fav = app.settings().favourites;
    auto& recent = app.settings().recent;
    const int my_rank = s.rank();
    const auto rows_src = app.servers();
    std::vector<ui::Row> rows;
    std::vector<int> shown;   // rows_src indices, in list order
    for (int i = 0; i < int(rows_src.size()); ++i) {
        const App::ServerRow& r = rows_src[size_t(i)];
        const bool is_fav = r.id && std::find(fav.begin(), fav.end(), r.id) != fav.end();
        const bool is_recent = r.id && std::find(recent.begin(), recent.end(), r.id) != recent.end();
        if (!r.local) {
            if (st.server_tab == 1 && !r.official) continue;
            if (st.server_tab == 2 && (r.official || r.direct)) continue;
            if (st.server_tab == 3 && !is_fav) continue;
            if (st.server_recent && !is_recent) continue;
            if (st.hide_full && r.slots && r.players >= r.slots) continue;
            if (st.hide_locked && r.locked) continue;
            if (st.hide_gated && r.min_rank > my_rank && s.tv.global_role == 0) continue;
            if (st.hide_downloads && r.pack_bytes > 0) continue;
        } else if (st.server_tab != 0 || st.server_recent) {
            continue;
        }
        // GT-5: the gate shown before connecting; greyed when the player is under it.
        const bool gated = !r.local && r.min_rank > my_rank && s.tv.global_role == 0;
        const float fill = r.slots ? float(r.players) / float(r.slots) : 0.0f;
        const bool live = r.local ? app.hosting() : r.answered;
        ui::Row row;
        row.key = i;
        // Condition: how full it is, in the colour of its ping (SL-7).
        const VanU32 ping_colour = !live ? kDim : r.ping_ms < 90 ? kOn : r.ping_ms < 180 ? kGold : kOff;
        row.cells.push_back({r.local ? (app.hosting() ? "Open" : "Ready") : live ? condition(fill) : "-", r.local ? kOn : ping_colour});
        std::string name = r.name;
        if (r.direct) name += "  (" + r.endpoint() + ")";
        // The tier's mark (PR-5), the padlock, the gate, what joining will download.
        std::string marks;
        if (r.official) marks += "  [Official]";
        else if (r.tier == u8(proto::Tier::Verified)) marks += "  [Verified]";
        if (r.locked) marks += "  [Locked]";
        if (r.min_rank > 0) marks += std::string("  ") + rank_name(r.min_rank) + "+";
        if (r.pack_bytes > 0) marks += eng::str::format("  %.1f MB", double(r.pack_bytes) / double(1u << 20));
        if (is_fav) marks += "  *";
        row.cells.push_back({name + marks, gated ? kDim : r.official ? kGold : kWhite, Align::Left, true});
        row.cells.push_back({r.local ? (app.hosting() ? "Hosting" : "-") : live || r.slots ? eng::str::format("%u / %u", unsigned(r.players), unsigned(r.slots)) : "-", gated ? kDim : kSoft});
        row.cells.push_back({r.local ? "0" : live ? std::to_string(r.ping_ms) : "-", ping_colour});
        rows.push_back(std::move(row));
        shown.push_back(i);
    }
    if (!shown.empty() && std::find(shown.begin(), shown.end(), st.server_sel) == shown.end()) st.server_sel = shown.front();
    static const std::string titles[] = {"Condition", "Server name", "Players", "Ping"};
    const ui::ListResult picked = ui::list(p.server, 101, rows, st.server_sel, titles);
    const App::ServerRow* chosen = st.server_sel >= 0 && st.server_sel < int(rows_src.size()) && !shown.empty() ? &rows_src[size_t(st.server_sel)] : nullptr;

    auto open_sign_in = [&] {
        st.login_open = true;
        st.login_error.clear();
        ui::open_modal("Sign in");
    };
    auto enter = [&] {
        if (st.joining || s.tv.busy) return;
        if (!s.tv.signed_in) return open_sign_in();
        if (s.tv.code_name.empty() || s.tv.free_rename) return app.go(Screen::CodeName);
        if (!chosen) return;
        // A private server asks for its password first (SFLegacy Staff pass it: §7.2).
        if (chosen->locked && s.tv.global_role == 0) {
            app.pick_server(*chosen);
            st.server_password_open = true;
            return;
        }
        app.connect(*chosen);
    };
    if (picked.clicked && picked.key >= 0) st.server_sel = picked.key;
    if (picked.activated && picked.key >= 0) {
        st.server_sel = picked.key;
        chosen = &rows_src[size_t(st.server_sel)];
        enter();
    }
    if (picked.context) {
        if (picked.key >= 0) st.server_sel = picked.key;
        VanGui::OpenPopup("##serverctx");
    }

    if (const int t = ui::tabs(p.server, 106, 107 + st.server_tab, {}, tab_names); t >= 0) st.server_tab = std::clamp(t - 107, 0, 3);
    const bool modal_up = st.login_open || st.joining || st.server_password_open || st.change_password_open || s.switch_request;
    if (ui::button(p.server, 114, !st.joining, nullptr, s.tv.signed_in ? "Join the server" : "Sign in") || (VanGui::IsKeyPressed(VanGuiKey_Enter) && !modal_up && !VanGui::GetIO().WantTextInput))
        enter();
    if (ui::button(p.server, 115, !s.servers_loading, nullptr, "Refresh the list")) s.request_servers(app.now());
    // The options, before signing in too (a screen size, a renderer, a controller): the script has
    // no plate for them on this page, so plates of the kit's beside Refresh.
    if (ui::text_button(9001, 211, 127, 284, 165, "Options", true, "Screen, graphics, sound and controls")) open_settings(app, -1, -1);
    if (ui::text_button(9002, 290, 127, 363, 165, st.server_recent ? "Recent *" : "Recent", true, "Only the servers you joined lately")) st.server_recent = !st.server_recent;
    if (s.tv.signed_in) {
        if (ui::text_button(9003, 369, 127, 462, 165, "Sign out", true, "Sign out of your Team Vanilla account")) s.tv_sign_out();
    } else if (ui::text_button(9003, 369, 127, 462, 165, "Sign in", !s.tv.busy, "Sign in to your Team Vanilla account")) {
        open_sign_in();
    }
    if (ui::button(p.server, 65535, true, nullptr, "Exit Soldier Front")) {
        st.confirm_quit = true;   // asked in handle_nav, outside this page's window
    }
    // Who is signed in, and what the list is doing.
    std::string status = s.tv.signed_in ? (s.tv.code_name.empty() ? "Signed in: choose your code name" : "Signed in as " + s.tv.code_name + "  (" + rank_name(my_rank) + ")")
                                        : s.tv.busy ? std::string("Signing in...") : std::string("Not signed in");
    if (s.servers_loading) status += "   |   Asking Team Vanilla for the list...";
    else if (!s.servers_error.empty()) status += "   |   " + s.servers_error;
    else if (s.tv.status_known && s.tv.down) status += "   |   Team Vanilla's services are down; try again shortly.";
    bottom_strip(app, status);

    // Right-click on the list: a favourite, the filters, servers of your own by address.
    static bool adding = false;
    if (VanGui::BeginPopup("##serverctx")) {
        if (chosen && chosen->id && !chosen->local) {
            const bool is_fav = std::find(fav.begin(), fav.end(), chosen->id) != fav.end();
            if (VanGui::MenuItem(is_fav ? "Remove from Favourites" : "Add to Favourites")) {
                if (is_fav) std::erase(fav, chosen->id);
                else if (fav.size() < 64) fav.push_back(chosen->id);
                app.save_settings();
            }
            VanGui::Separator();
        }
        if (chosen && chosen->direct && VanGui::MenuItem("Remove this server")) {
            auto& mine = app.settings().servers;
            std::erase_if(mine, [&](const ServerEntry& e) { return e.name == chosen->name; });
            std::erase_if(s.servers, [&](const ListedServer& o) { return o.direct && o.name == chosen->name; });
            app.save_settings();
        }
        (void)VanGui::MenuItem("Hide full servers", nullptr, &st.hide_full);
        (void)VanGui::MenuItem("Hide locked servers", nullptr, &st.hide_locked);
        (void)VanGui::MenuItem("Hide servers above my rank", nullptr, &st.hide_gated);
        (void)VanGui::MenuItem("Hide servers that need a download", nullptr, &st.hide_downloads);
        VanGui::Separator();
        if (VanGui::MenuItem("Add a server by address...")) adding = true;
        VanGui::EndPopup();
    }
    ui::page_end();

    if (adding) {
        ui::open_modal("Add a server");
        static std::string add_name, add_addr;
        bool open = true;
        if (ui::begin_modal("Add a server", "ADD A SERVER", 460, 250, &open)) {
            VanGui::TextUnformatted("Name");
            ui::input("##addname", "a name for it", add_name, 428);
            VanGui::TextUnformatted("Address");
            const bool entered = ui::input("##addaddr", "host or host:port", add_addr, 428, false, VanGuiInputTextFlags_EnterReturnsTrue);
            VanGui::Dummy({0, ui::px(8)});
            if ((ui::button("Add", 200, 42, ui::Style::Primary, !add_addr.empty()) || (entered && !add_addr.empty()))) {
                const ServerEntry e{add_name.empty() ? add_addr : add_name, add_addr};
                app.settings().servers.push_back(e);
                app.save_settings();
                app.add_direct_server(e);
                ui::toast(ui::Toast::Good, "Added %s to the server list.", e.name.c_str());
                add_name.clear(), add_addr.clear();
                adding = false;
                VanGui::CloseCurrentPopup();
            }
            VanGui::SameLine();
            if (ui::button("Cancel", 200, 42)) {
                adding = false;
                VanGui::CloseCurrentPopup();
            }
            ui::end_modal();
        }
        if (!open) adding = false;
    }
    handle_nav(app, Nav::None);
    // Signing in comes first: the window opens by itself until there is an account to play as.
    if (!s.tv.signed_in && !s.tv.busy && !st.login_open && !st.sign_in_dismissed && app.screen_time() > 0.6 && app.options().autotest.empty()) {
        st.login_open = true;
        st.sign_in_dismissed = true;   // once: closing it leaves the list to look at
    }
    if (s.tv.signed_in && st.change_password_open) {
        change_password_modal(app);
    } else if (st.login_open && !s.tv.signed_in) {
        if (!VanGui::IsPopupOpen("Sign in")) ui::open_modal("Sign in");
        sign_in_modal(app);
    } else if (st.server_password_open) {
        server_password_modal(app);
    } else if (st.joining) {
        join_modal(app);
    }
}

// ── Channels (pagecommon + Page1) ──────────────────────────────────────────────

void draw_channels(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Pages p = pages(app);
    ui::page_begin("##page_channel");
    const Nav nav = common_chrome(app);
    ui::draw_static(p.channel);

    // The channels, filtered by the search box.
    static std::string search;
    std::vector<ui::Row> rows;
    static const VanU32 kind_colour[] = {VAN_COL32(224, 221, 94, 255), VAN_COL32(236, 200, 120, 255), VAN_COL32(130, 200, 255, 255),
                                         VAN_COL32(236, 96, 72, 255), VAN_COL32(236, 170, 70, 255), kWhite};
    for (int i = 0; i < int(s.channels.size()); ++i) {
        const proto::ChannelInfo& c = s.channels[size_t(i)];
        if (!search.empty() && sf::lower(c.name).find(sf::lower(search)) == std::string::npos) continue;
        const float fill = c.capacity ? float(c.players) / float(c.capacity) : 0.0f;
        ui::Row row;
        row.key = i;
        row.cells.push_back({condition(fill), c.allowed ? condition_colour(fill) : kDim});
        row.cells.push_back({c.name, c.allowed ? kind_colour[std::min<u8>(c.kind, 5)] : kDim, Align::Left, true});
        row.cells.push_back({eng::str::format("%u / %u", unsigned(c.players), unsigned(c.capacity)), c.allowed ? kSoft : kDim});
        rows.push_back(std::move(row));
    }
    static const std::string titles[] = {"Condition", "Channel name", "Players"};
    const ui::ListResult picked = ui::list(p.channel, 1, rows, st.channel_sel, titles);
    auto usable = [&](int i) { return i >= 0 && i < int(s.channels.size()) && s.channels[size_t(i)].allowed; };
    if (picked.clicked && picked.key >= 0) st.channel_sel = picked.key;
    if (picked.key >= 0 && usable(picked.key)) {
        const proto::ChannelInfo& c = s.channels[size_t(picked.key)];
        if (VanGui::IsItemHovered() && !c.limit.empty()) VanGui::SetTooltip("%s", c.limit.c_str());
        if (picked.activated) s.join_channel(c.id);
    }
    if (s.channels.empty()) ui::text_at(40, 230, 454, 260, "Asking the server for its channels...", kDim, Align::Center, true);
    if (ui::button(p.channel, 11, usable(st.channel_sel), nullptr, "Enter the channel")) s.join_channel(s.channels[size_t(st.channel_sel)].id);
    if (ui::button(p.channel, 6, true, nullptr, "Refresh")) s.list_channels();
    (void)ui::edit(p.channel, 9, search);
    if (ui::button(p.channel, 10, true, nullptr, "Search")) {
        if (!rows.empty()) st.channel_sel = rows.front().key;
    }

    // NEW / HOT: weapons across the top, soldiers across the bottom.
    struct Tile {
        float x0, y0, x1, y1;
        bool hot;
    };
    const Tile tiles[] = {{490, 374, 743, 518, false}, {744, 374, 997, 518, true}, {490, 520, 743, 664, false}, {744, 520, 997, 664, true}};
    const char* guns[] = {"evl_scar_h", "ak74"};
    const u8 soldiers[] = {forces().back().id, forces()[std::min<size_t>(5, forces().size() - 1)].id};
    for (int i = 0; i < 4; ++i) {
        const Tile& t = tiles[i];
        std::string name, price;
        ui::Picture pic;
        if (i < 2) {
            if (const WeaponDef* w = weapon_by_model(guns[i])) {
                pic = weapon_icon(app, *w);
                name = w->name;
                price = eng::str::format("SP %u", w->price);
            }
        } else if (const ForceDef* f = force(soldiers[i - 2])) {
            pic = app.stage().photo(f->id);
            name = f->name;
            price = f->price ? eng::str::format("SP %u", f->price) : "Free";
        }
        VanDrawList* dl = VanGui::GetWindowDrawList();
        // The grid frame's middle is see-through; the pick sits on black inside it.
        dl->AddRectFilled(ui::pg(t.x0 + 9, t.y0 + 9), ui::pg(t.x1 - 9, t.y1 - 9), VAN_COL32(0, 0, 0, 170));
        if (pic.valid()) {
            const float bw = t.x1 - t.x0 - 24, bh = t.y1 - t.y0 - 44;
            const float s_ = std::min(bw / std::max(1.0f, pic.w), bh / std::max(1.0f, pic.h));
            const float w = pic.w * s_, h = pic.h * s_;
            const float cx = (t.x0 + t.x1) * 0.5f, cy = t.y0 + 12 + bh * 0.5f;
            dl->AddImage(VanTextureRef(VanTextureID(pic.tex)), ui::pg(cx - w * 0.5f, cy - h * 0.5f), ui::pg(cx + w * 0.5f, cy + h * 0.5f));
        }
        ui::text_at(t.x0 + 10, t.y1 - 28, t.x1 - 10, t.y1 - 12, name, kWhite, Align::Left, true);
        ui::text_at(t.x0 + 10, t.y1 - 28, t.x1 - 10, t.y1 - 12, price, kGold, Align::Right, true);
        if (ui::Atlas* a = ui::atlas()) {
            const ui::SpriteRef badge = a->sprite(t.hot ? "ico_hot_1" : "ico_new_1");
            if (badge.valid()) dl->AddImage(VanTextureRef(VanTextureID(badge.tex)), ui::pg(t.x0 + 6, t.y0 + 6), ui::pg(t.x0 + 6 + badge.w, t.y0 + 6 + badge.h), badge.uv0, badge.uv1);
        }
    }

    chat_box(app, p.channel, 13, 12, 14, false);
    if (ui::button(p.channel, 16, true, nullptr, "Back to the servers")) {
        s.disconnect("left");
        app.go(Screen::Servers);
    }
    if (ui::button(p.channel, 65535, true, nullptr, "Exit Soldier Front")) {
        st.confirm_quit = true;   // asked in handle_nav, outside this page's window
    }
    bottom_strip(app, "Channel Select");
    ui::page_end();
    handle_nav(app, nav);
}

// ── The lobby (pagecommon + Page2) ─────────────────────────────────────────────

void draw_lobby(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Pages p = pages(app);
    ui::page_begin("##page_lobby");
    const Nav nav = common_chrome(app);
    // The right column changes with its tab: the users, the room preview, the messenger.
    const bool info = st.user_tab == 2;
    if (info) ui::draw_static(p.lobby);
    else ui::draw_static(p.lobby, {43});

    auto try_join = [&](const proto::RoomSummary& r) {
        if (r.locked) {
            st.password_open = true;
            st.password_room = r.id;
            ui::open_modal("Room password");
        } else {
            s.join_room(r.id);
        }
    };

    // The rooms.
    static std::string search;
    std::vector<ui::Row> rows;
    for (const auto& [id, r] : s.rooms) {
        if (!search.empty() && sf::lower(r.title).find(sf::lower(search)) == std::string::npos && std::to_string(id) != search) continue;
        const bool waiting = r.phase == 0;
        const VanU32 base = waiting ? kSoft : kDim;
        ui::Row row;
        row.key = int(id);
        row.cells.push_back({std::to_string(id), base});
        row.cells.push_back({upper(game_type_name(Mode(std::min<u8>(r.mode, u8(Mode::Count) - 1)), r.no_snipers, r.clan_battle)), base});
        const std::string title = r.clan_battle ? r.red_clan + " vs " + (r.blue_clan.empty() ? std::string("any clan") : r.blue_clan) : r.title;
        row.cells.push_back({title, waiting ? kWhite : kDim, Align::Center, true});
        row.cells.push_back({r.host, waiting ? kWhite : kDim, Align::Left});
        row.cells.push_back({eng::str::format("%u/%u", unsigned(r.players), unsigned(r.max_players)), waiting ? kWhite : kDim});
        row.cells.push_back({r.locked ? "Locked" : "Open", base});
        row.cells.push_back({r.phase == 0 ? "WAITING" : r.phase == 1 ? "LOADING" : "GAMING", base});
        row.cells.push_back({r.free_join ? "On" : "Off", !waiting ? kDim : r.free_join ? kOn : kOff});
        rows.push_back(std::move(row));
    }
    static const std::string room_titles[] = {"No.", "Type", "Room name", "Host", "Players", "Remark", "Status", "Free Join"};
    const ui::ListResult picked = ui::list(p.lobby, 2, rows, st.room_sel, room_titles);
    if (picked.clicked && picked.key >= 0) {
        st.room_sel = picked.key;
        st.user_tab = 2;   // a room picked shows its preview, as the client did
    }
    if (picked.activated && picked.key >= 0)
        if (auto it = s.rooms.find(u16(picked.key)); it != s.rooms.end()) try_join(it->second);
    if (rows.empty()) ui::text_at(40, 230, 658, 260, "No rooms yet. Make one, and the others will find you here.", kDim, Align::Center, true);
    const proto::RoomSummary* selected = nullptr;
    if (auto it = s.rooms.find(u16(std::max(0, st.room_sel))); it != s.rooms.end()) selected = &it->second;

    if (ui::button(p.lobby, 20, true, nullptr, "Make a room")) {
        // The last room's game and map again, with a fresh name and the map's own hour.
        st.create.title = s.profile.code_name + "'s room";
        st.create.password.clear();
        // In a Clan War channel the room is a Clan Battle (the server says so too): its game types only.
        bool clan_channel = false;
        for (const proto::ChannelInfo& c : s.channels)
            if (c.id == s.channel) clan_channel = ChannelKind(c.kind) == ChannelKind::ClanWar;
        st.create.clan_battle = clan_channel;
        if (clan_channel && !clan_battle_mode(st.create.mode)) {
            st.create.mode = Mode::TeamBattle;
            st.create.goal = mode_info(st.create.mode).goal_default, st.create.minutes = mode_info(st.create.mode).minutes_default;
        }
        if (clan_channel) st.create.title = s.profile.clan + " at home";
        st.create.time_of_day = baked_time_of_day(st.create.map);
        st.create_hour_set = false;
        st.create_open = true;
    }
    if (ui::button(p.lobby, 19, selected != nullptr, nullptr, "Enter the room") && selected) try_join(*selected);
    if (ui::button(p.lobby, 28, true, nullptr, "Quick join")) s.quick_join();
    // Accept Invites / Block Invites: whether other soldiers' rooms may ask you in.
    if (ui::toggle(p.lobby, 40, !app.settings().room_invites)) {
        app.settings().room_invites = !app.settings().room_invites;
        app.save_settings();
        s.set_room_invites(app.settings().room_invites);
        ui::toast(ui::Toast::Info, app.settings().room_invites ? "Other soldiers may invite you to their rooms." : "Invitations to rooms are blocked.");
    }
    // The sound switch: the lobby's music, kept in the settings.
    if (ui::toggle(p.lobby, 41, !app.settings().lobby_music)) {
        app.settings().lobby_music = !app.settings().lobby_music;
        app.save_settings();
    }
    (void)ui::edit(p.lobby, 21, search);
    if (ui::button(p.lobby, 22, true, nullptr, "Search rooms") && !rows.empty()) st.room_sel = rows.front().key;

    // The right column.
    if (const int t = ui::tabs(p.lobby, 11, st.user_tab); t >= 0) st.user_tab = t;
    if (st.user_tab < 2) {
        std::vector<ui::Row> users;
        for (const auto& [id, u] : s.users) {
            if (st.user_tab == 1 && u.room != 0) continue;
            const int rank = rank_for_xp(u.xp);
            ui::Row row;
            row.key = int(id);
            // A Colored Codename shows in its colour.
            row.cells.push_back({u.name, u.name_colour ? ui::name_ink(u.name_colour) : id == s.session_id ? VAN_COL32(224, 221, 94, 255) : kWhite,
                                 Align::Left, true});
            ui::Cell level{"", kSoft};
            level.icon = app.atlas().rank_badge(rank);
            row.cells.push_back(std::move(level));
            row.cells.push_back({u.room ? eng::str::format("Room %u", unsigned(u.room)) : "Lobby", kSoft});
            users.push_back(std::move(row));
        }
        static const std::string user_titles[] = {"Codename", "Level", "Location"};
        // A right click on a name: whisper, befriend, ask into the clan, block, report. Twice: a whisper.
        const ui::ListResult who = ui::list(p.lobby, 14, users, -1, user_titles);
        if (who.key >= 0 && (who.context || who.activated))
            if (auto it = s.users.find(u32(who.key)); it != s.users.end()) {
                if (who.context) {
                    open_soldier_menu(app, it->first, it->second.name, it->second.clan);   // your own name: your ID card
                } else if (it->first != s.session_id) {
                    st.chat_scope = 1;
                    st.chat_line = it->second.name + " ";
                }
            }
    } else if (st.user_tab == 2) {
        if (selected) {
            ui::picture(p.lobby, 42, app.atlas().map_picture(selected->map));
            ui::text(p.lobby, 44, mode_name(Mode(std::min<u8>(selected->mode, u8(Mode::Count) - 1))), kWhite, Align::Center);
            ui::text(p.lobby, 45, eng::str::format("%u / %u", unsigned(selected->players), unsigned(selected->max_players)), kWhite, Align::Center);
            const bool night = selected->time_of_day == u8(TimeOfDay::Night);
            ui::text(p.lobby, 46, app.map_title(selected->map) + (night ? " (Night)" : ""), kWhite, Align::Center);
            ui::text(p.lobby, 47, selected->phase == 0 ? "WAITING" : "GAMING", selected->phase == 0 ? kOn : kOff, Align::Center);
        }
        static const std::string team_titles[] = {"Codename", "Level", "Kill/Death"};
        const bool teams = selected && mode_info(Mode(std::min<u8>(selected->mode, u8(Mode::Count) - 1))).teams;
        if (teams) {
            (void)ui::list(p.lobby, 57, {}, -1, team_titles);
            (void)ui::list(p.lobby, 58, {}, -1, team_titles);
        } else {
            (void)ui::list(p.lobby, 54, {}, -1, team_titles);
        }
    } else {
        // The Friend tab (the original's messenger): your friends and where they are.
        friend_panel(app, p.lobby, 59);
    }

    chat_box(app, p.lobby, 25, 27, 32, false);
    if (ui::button(p.lobby, 24, true, nullptr, "Back to the channels")) {
        s.leave_channel();
        app.go(Screen::Channels);
    }
    bottom_strip(app, s.channel_name);
    soldier_menu(app);
    ui::page_end();
    handle_nav(app, nav);
    if (st.create_open) make_room_modal(app);
    if (st.password_open) password_modal(app);
}

// ── The waiting room (Page3) ───────────────────────────────────────────────────

namespace {

// The flag a force's nation flies (the 2004 kit's flag sprites).
const char* flag_of(const char* nation) {
    const std::string_view n = nation ? nation : "";
    if (n == "USA") return "US";
    if (n == "Hong Kong") return "Hongkong";
    if (n == "Philippines") return "psu";
    if (n == "China") return "china";
    return nation;   // Korea, England, Germany, France, Russia
}

const char* state_sprite(u8 state) {
    switch (proto::SlotState(state)) {
        case proto::SlotState::Ready: return "state_ready_1";
        case proto::SlotState::Inventory: return "state_inven_1";
        case proto::SlotState::Playing: return "state_play_1";
        case proto::SlotState::Loading: return "state_load_1";
        default: return nullptr;
    }
}

// One side's *USERCTRL: the map behind in the side's colour, the column titles, eight seats. The
// client drew these itself; the look is the ijji-era client's (Nation / Level / Codename / Clan
// name, READY stamped over the flag) in the 2010 kit's pieces.
void team_panel(App& app, const proto::RoomState& room, const ui::Page& page, int ctrl_id, Team side, int first_seat, bool teams, u32& picked,
                bool can_move) {
    Session& s = app.session();
    float x0, y0, x1, y1;
    if (!page.rect(ctrl_id, x0, y0, x1, y1, "USERCTRL")) return;
    y0 = std::max(y0, 107.0f);   // the button box ends there; the seats' own frame starts below it
    const VanU32 tint = !teams ? VAN_COL32(170, 170, 170, 120) : side == Team::Red ? VAN_COL32(255, 120, 110, 120) : VAN_COL32(120, 150, 255, 120);
    ui::fill_at(x0, y0, x1, y1, VAN_COL32(0, 0, 0, 170));
    ui::picture_at(app.atlas().map_picture(room.settings.map), x0, y0, x1, y1, tint);
    const float head = 22;
    ui::fill_at(x0, y0, x1, y0 + head, VAN_COL32(34, 34, 32, 235));
    const float cols[] = {52, 46, 150};
    const char* titles[] = {"Nation", "Level", "Codename", "Clan name"};
    float cx = x0;
    for (int c = 0; c < 4; ++c) {
        const float w = c < 3 ? cols[c] : x1 - cx;
        ui::text_at(cx, y0, cx + w, y0 + head, titles[c], kWhite, Align::Center, true);
        cx += w;
    }
    const float row = (y1 - y0 - head) / 8.0f;
    // A Clan Battle: the side's clan, large and faint, across the panel's foot (blue's until a clan has come).
    if (room.settings.clan_battle && teams) {
        const std::string& clan = side == Team::Red ? room.settings.red_clan : room.settings.blue_clan;
        ui::text_at(x0, y1 - 44, x1 - 10, y1 - 6, clan.empty() ? std::string("Waiting for a clan") : clan,
                    clan.empty() ? VAN_COL32(255, 255, 255, 70) : side == Team::Red ? VAN_COL32(255, 170, 160, 150) : VAN_COL32(170, 190, 255, 150), Align::Right, true, clan.empty() ? 14.0f : 24.0f);
    }
    const char* bar = !teams ? "teambar_black_1" : side == Team::Red ? "teambar_red_1" : "teambar_blue_1";
    const proto::RoomMember* me = s.me_in_room();
    for (int seat = 0; seat < 8; ++seat) {
        const float ry0 = y0 + head + row * float(seat), ry1 = ry0 + row;
        const proto::RoomMember* m = nullptr;
        for (const auto& mm : room.members) {
            if (teams ? (Team(mm.team) == side && int(mm.slot) == seat) : (Team(mm.team) == Team::None && int(mm.slot) == first_seat + seat)) m = &mm;
        }
        bool hovered = false;
        const bool clicked = ui::region(ctrl_id * 100 + seat, x0, ry0, x1, ry1, &hovered);
        // A taken seat is underlined in the side's colour; an empty one is just the map.
        if (m) ui::sprite_at(bar, 0, 1, x0, ry1 - 7, x1, ry1 + 7, VAN_COL32(255, 255, 255, 190));
        if (!m) {
            if (hovered && can_move && teams && me && Team(me->team) != side) {
                ui::fill_at(x0, ry0, x1, ry1, VAN_COL32(255, 255, 255, 16));
                VanGui::SetTooltip("Move to %s", team_name(side));
            }
            if (clicked && can_move && teams && me && Team(me->team) != side) s.set_team(side);
            continue;
        }
        if (clicked) picked = m->id;
        // A right click on a soldier: their menu (whisper, befriend, ask into the clan, block, report).
        if (hovered && VanGui::IsMouseClicked(1)) {
            picked = m->id;
            open_soldier_menu(app, m->id, m->name, m->clan);
        }
        if (m->id == picked) ui::sprite_at("list_selected_1", 0, 1, x0, ry0 + 2, x1, ry1 - 2);
        else if (hovered) ui::fill_at(x0, ry0, x1, ry1, VAN_COL32(255, 255, 255, 16));
        const float mid = (ry0 + ry1) * 0.5f;
        const ForceDef* f = force(m->force);
        ui::sprite_at(flag_of(f ? f->nation : "Korea"), 0, 1, x0 + 8, mid - 10, x0 + 44, mid + 10);
        if (const char* st = state_sprite(m->state); st && !m->host) ui::sprite_at(st, 0, 1, x0 + 1, mid - 10, x0 + 51, mid + 11);
        ui::picture_at(app.atlas().rank_badge(rank_for_xp(m->xp)), x0 + cols[0] + 14, mid - 9, x0 + cols[0] + 32, mid + 9);
        float nx = x0 + cols[0] + cols[1];
        if (m->host) {
            ui::sprite_at("mark_captain", 0, 1, nx + 2, mid - 13, nx + 17, mid + 12);
            nx += 18;
        }
        if (m->name_colour) ui::name_text_at(nx, ry0, x0 + cols[0] + cols[1] + cols[2], ry1, m->name, m->name_colour, Align::Left);
        else ui::text_at(nx, ry0, x0 + cols[0] + cols[1] + cols[2], ry1, m->name, m->id == s.session_id ? VAN_COL32(224, 221, 94, 255) : kWhite, Align::Left,
                         true);
        if (!m->clan.empty()) {
            const float clan_x = x0 + cols[0] + cols[1] + cols[2];
            clan_mark(app, m->clan_mark, clan_x + 4, mid - 8, clan_x + 20, mid + 8);
            if (m->clan_colour) ui::name_text_at(clan_x + 22, ry0, x1 - 4, ry1, m->clan, m->clan_colour, Align::Left);
            else ui::text_at(clan_x + 22, ry0, x1 - 4, ry1, m->clan, kSoft, Align::Left, true);
        }
        if (hovered) VanGui::SetTooltip("%s  -  %u ms", m->name.c_str(), unsigned(m->ping));
    }
}

std::string win_label(const RoomSettings& r) {
    const ModeInfo& mi = mode_info(r.mode);
    if (mi.goal_max == 0) return "-";
    if (mi.rounds) return eng::str::format("%u WIN", unsigned(r.goal));
    return eng::str::format("%u %s", unsigned(r.goal), upper(mi.goal_label).c_str());
}

}  // namespace

void draw_room(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Pages p = pages(app);
    if (!s.room) {
        app.go(Screen::Lobby);
        return;
    }
    const proto::RoomState& room = *s.room;
    const proto::RoomMember* me = s.me_in_room();
    const bool host = me && me->host;
    const bool waiting = room.phase == 0;
    const bool can_edit = host && waiting;
    const ModeInfo& mi = mode_info(room.settings.mode);
    ui::page_begin("##page_room");
    ui::page_background();
    ui::draw_static(p.room, {26, 59});

    // ── Left: your soldier ──
    std::vector<u8> owned;
    for (const ForceDef& f : forces())
        if (f.price == 0 || s.owns_force(f.id)) owned.push_back(f.id);
    const u8 my_force = me ? me->force : s.profile.force;
    const ForceDef* mf = force(my_force);
    if (const int step = ui::selector(p.room, 42, mf ? mf->name : "?", waiting && owned.size() > 1); step != 0) {
        auto it = std::find(owned.begin(), owned.end(), my_force);
        int i = it == owned.end() ? 0 : int(it - owned.begin());
        i = (i + step + int(owned.size())) % int(owned.size());
        s.set_loadout(owned[size_t(i)], s.profile.loadout);
    }
    (void)ui::selector(p.room, 43, "Type-A", false);
    // The soldier himself, and what he carries down the right of the preview.
    const ui::Picture mine = app.stage().live(my_force, 10.0f * std::sin(float(app.now()) * 0.6f), s.worn(my_force));
    VanGui::GetWindowDrawList()->PushClipRect(ui::pg(6, 113), ui::pg(321, 599), true);
    if (mine.valid()) ui::picture_at(mine, -52, 116, 262, 596, 0xFFFFFFFF, true);
    VanGui::GetWindowDrawList()->PopClipRect();
    const float pics[][4] = {{182, 124, 314, 174}, {182, 223, 314, 273}, {182, 322, 314, 364}, {182, 372, 314, 414}};
    for (int k = 0; k < 3; ++k)
        if (const WeaponDef* w = weapon(s.profile.loadout[size_t(k)])) ui::picture_at(weapon_icon(app, *w), pics[k][0], pics[k][1], pics[k][2], pics[k][3], 0xFFFFFFFF, true);
    // The throwables share the last place, side by side.
    {
        std::vector<const WeaponDef*> thrown;
        for (size_t c = kFirstThrowCell; c < kLoadoutSlots; ++c)
            if (const WeaponDef* w = weapon(s.profile.loadout[c])) thrown.push_back(w);
        const float* r = pics[3];
        const float cw = (r[2] - r[0]) / float(std::max<size_t>(1, thrown.size()));
        for (size_t i = 0; i < thrown.size(); ++i) ui::picture_at(weapon_icon(app, *thrown[i]), r[0] + cw * float(i), r[1], r[0] + cw * float(i + 1), r[3], 0xFFFFFFFF, true);
    }
    // What is left of the primary and the sidearm (Game/Wear.hpp), and Repair under each.
    for (int k = 0; k < 2; ++k) {
        const WeaponDef* w = weapon(s.profile.loadout[size_t(k)]);
        const u8 wear = w ? s.durability_of(w->id) : kDurabilityFull;
        const VanU32 tint = wear == 0 ? VAN_COL32(236, 80, 64, 255) : wear <= kDurabilityLow ? VAN_COL32(236, 160, 48, 255) : 0xFFFFFFFF;
        ui::meter(p.room, 61 + k, float(wear) / float(kDurabilityFull), tint);
        const bool worn = w && wears(*w) && wear < kDurabilityFull;
        const std::string tip = !w || !wears(*w) ? std::string("This weapon never wears")
                                : !worn          ? std::string("It needs no mending")
                                                 : eng::str::format("%u%% left: mend it for SP %s", unsigned(wear), eng::str::thousands(s.repair_price(w->id)).c_str());
        if (ui::button(p.room, 63 + k, worn && waiting, nullptr, tip.c_str())) s.repair(w->id);
    }
    // The force's numbers.
    if (mf) {
        const struct {
            const char* label;
            float value;
            bool percent;
        } stats[] = {{"Moving speed", (mf->speed - 1.0f) * 10.0f, false},
                     {"Upper defense", mf->upper_defense * 100, true},
                     {"Legs defense", mf->lower_defense * 100, true},
                     {"Avoid headshot", mf->avoid_headshot * 100, true}};
        for (int i = 0; i < 4; ++i) {
            const float y = 438 + 20.0f * float(i);
            ui::text_at(182, y, 316, y + 18, stats[i].label, kSoft, Align::Left, true, 11.0f);
            const std::string v = stats[i].percent ? eng::str::format("+%.0f%%", double(stats[i].value)) : eng::str::format("+%.1f", double(stats[i].value));
            ui::text_at(182, y, 312, y + 18, v, stats[i].value > 0 ? kOn : kSoft, Align::Right, true, 11.0f);
        }
    }
    // My info.
    const int rank = s.profile.rank();
    ui::picture(p.room, 52, app.atlas().rank_badge(rank));
    ui::text_at(160, 608, 318, 626, s.profile.code_name, VAN_COL32(224, 221, 94, 255), Align::Left, true);
    {
        bool over = false;
        if (ui::region(9930, 100, 604, 318, 628, &over)) open_id_card(app, s.profile.code_name);
        if (over) ui::tip("Your ID card");
    }
    if (s.profile.clan.empty()) {
        ui::text_at(85, 626, 318, 644, "Clan : -", kWhite, Align::Left, true);
    } else {
        ui::text_at(85, 626, 318, 644, "Clan : " + s.profile.clan, kWhite, Align::Left, true);
        clan_mark(app, s.profile.clan_mark, 298, 627, 314, 643);
    }
    ui::text_at(85, 644, 205, 662, eng::str::format("Win : %u  Lose : %u", s.profile.wins, s.profile.losses), kWhite, Align::Left, true);
    ui::text_at(85, 662, 205, 680, eng::str::format("K/D : %.2f", double(s.profile.kd())), kWhite, Align::Left, true);
    ui::text_at(205, 644, 318, 662, rank_name(rank), kSoft, Align::Left, true);
    ui::text_at(205, 662, 318, 680, eng::str::format("SP : %u", s.profile.sp), kGold, Align::Left, true);
    ui::text_at(85, 680, 160, 698, "SP recharge :", kWhite, Align::Left, true);
    ui::meter(p.room, 60, float(s.profile.sp_charge) / 100.0f);

    // ── Right: the room ──
    const bool all_ready = [&] {
        for (const auto& m : room.members)
            if (!m.host && m.team != u8(Team::Observer) && m.state != u8(proto::SlotState::Ready)) return false;
        return true;
    }();
    if (host && waiting) {
        if (ui::button(p.room, 26, true, "start_1", all_ready ? "Start the game" : "Everyone must be READY")) s.start_match();
    } else if (!waiting) {
        if (ui::button(p.room, 26, room.settings.free_join, "start_1", "Join the game in progress")) s.start_match();
    } else {
        const bool ready = me && me->state == u8(proto::SlotState::Ready);
        if (ui::button(p.room, 26, true, "ready_1", ready ? "Not ready" : "Ready", ready)) s.set_ready(!ready);
    }
    if (ui::button(p.room, 16, true, nullptr, "Invite a soldier to the room")) {
        st.invite_open = true;
        st.invite_sel.clear();
    }
    static u32 picked = 0;
    const bool can_kick = host && waiting && picked != 0 && (!me || picked != me->id);
    if (ui::button(p.room, 44, can_kick, nullptr, "Kick the soldier picked")) s.kick(picked);
    // Record: every match you finish is kept on this PC (the server records them all; this keeps a copy).
    const char* here = ui::this_device();
    if (ui::button(p.room, 66, true, nullptr,
                   (st.record_match ? eng::str::format("Recording: each match you finish is kept on %s (Replay)", here)
                                    : eng::str::format("Keep each match you finish on %s", here)).c_str(),
                   st.record_match)) {
        st.record_match = !st.record_match;
        ui::toast(ui::Toast::Info, st.record_match ? "Each match you finish is kept on %s, under Replay." : "Matches are no longer kept on %s by themselves.", here);
    }
    if (ui::button(p.room, 69, waiting, nullptr, "Inventory")) {
        st.shop_tab = 2;
        st.shop_return = Screen::Room;
        app.go(Screen::Shop);
    }
    team_panel(app, room, p.room, 50, mi.teams ? Team::Red : Team::None, 0, mi.teams, picked, waiting);
    team_panel(app, room, p.room, 51, mi.teams ? Team::Blue : Team::None, 8, mi.teams, picked, waiting);

    // The mission.
    RoomSettings edit = room.settings;
    // The maps the room's game type is played on (All Random and Hot Random first).
    const std::vector<std::string> maps = app.maps_for(room.settings.mode);
    ui::picture(p.room, 40, app.atlas().map_picture(room.settings.map));
    if (const int step = ui::selector(p.room, 41, "", can_edit && !maps.empty()); step != 0) {
        int i = -1;
        for (int k = 0; k < int(maps.size()); ++k)
            if (maps[size_t(k)] == room.settings.map) i = k;
        i = i < 0 ? 0 : (i + step + int(maps.size())) % int(maps.size());
        edit.map = maps[size_t(i)];
        s.change_room(edit);
    }
    ui::text(p.room, 70, app.map_title(room.settings.map), kWhite, Align::Left);
    // The hour, on the map name's line: the host's to change like the five options.
    if (const int d = ui::arrows(9001, 690, 458, 795, 477, time_of_day_name(room.settings.time_of_day), can_edit); d != 0) {
        edit.time_of_day = TimeOfDay((int(room.settings.time_of_day) + d + int(TimeOfDay::Count)) % int(TimeOfDay::Count));
        s.change_room(edit);
    }
    // The map's own words (MapName.txt), the game type's where it has its own, else the mission's.
    const Mission mission = app.map_mission(room.settings.map);
    const bool defending = me && Team(me->team) == Team::Blue;
    std::string mission_text = defending ? mission_defence_text(mission) : mission_attack_text(mission);
    if (const sf::MapInfo* info = app.map_info(room.settings.map); info && room.settings.mode == Mode::TeamBattle)
        mission_text = defending ? info->defence_text : info->attack_text;
    else if (const char* brief = mode_brief(room.settings.mode))
        mission_text = brief;
    ui::paragraph(p.room, 27, mission_text, VAN_COL32(96, 150, 255, 255));

    // The room's five options.
    if (const int d = ui::selector(p.room, 45, game_type_name(room.settings), can_edit); d != 0) {
        step_game_type(edit, d, s.modes_allowed());
        // A map the new game type is not played on: its first (Horror's own, the Pirate Ship ...).
        const std::vector<std::string> now_maps = app.maps_for(edit.mode);
        if (std::find(now_maps.begin(), now_maps.end(), edit.map) == now_maps.end() && !now_maps.empty()) {
            size_t first = 0;
            while (first + 1 < now_maps.size() && is_random_map(now_maps[first])) ++first;
            edit.map = now_maps[first];
        }
        s.change_room(edit);
    }
    if (const int d = ui::selector(p.room, 46, win_label(room.settings), can_edit && mi.goal_max > 0); d != 0) {
        edit.goal = u8(std::clamp(int(room.settings.goal) + d * std::max<int>(1, mi.goal_step), int(mi.goal_min), int(mi.goal_max)));
        s.change_room(edit);
    }
    if (const int d = ui::selector(p.room, 47, eng::str::format("%u MIN", unsigned(room.settings.minutes)), can_edit); d != 0) {
        edit.minutes = u8(std::clamp(int(room.settings.minutes) + d, 1, 30));
        s.change_room(edit);
    }
    if (ui::selector(p.room, 48, room.settings.third_person ? "3rd Person ON" : "3rd Person OFF", can_edit) != 0) {
        edit.third_person = !room.settings.third_person;
        s.change_room(edit);
    }
    if (ui::selector(p.room, 75, room.settings.free_join ? "Free Join ON" : "Free Join OFF", can_edit) != 0) {
        edit.free_join = !room.settings.free_join;
        s.change_room(edit);
    }

    // The room's chat.
    chat_box(app, p.room, 11, 13, 14, true);
    if (ui::button(p.room, 2, true, nullptr, "Leave the room")) s.leave_room();
    ui::text(p.room, 65, eng::str::format("%03u  %s", unsigned(room.id), room.settings.title.c_str()), kWhite, Align::Left);
    ui::text_at(0, 748, 258, 768, eng::str::format("%s  Room : %u", s.channel_name.c_str(), unsigned(room.id)), kLocation, Align::Center, true, 11.0f);
    soldier_menu(app);
    ui::page_end();
    handle_nav(app, Nav::None);
}

}  // namespace lsf
