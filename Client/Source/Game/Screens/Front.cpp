// Boot, signing in to Team Vanilla (the server select itself is drawn from PageServer,
// Screens/Pages.cpp), the first sign-in's code name, and joining a server: the ticket, the server's
// packs on their way, a switch to a friend's server (Docs/UniversalServerDeploy.md §5, §11.7, §14.3).
#include "Game/Screens/Screens.hpp"

#include "Game/Ui/Ui.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Platform/Keys.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_loading.h>

#include <algorithm>

namespace lsf {

using namespace ui;

// ── Boot ───────────────────────────────────────────────────────────────────────

void draw_boot(App& app) {
    const Palette& p = pal();
    // The client's own splash while the archives open (sf.jpg: 1024x768).
    ui::backdrop(app.atlas().picture(sf::Pack::Lobby, "sf.jpg"), 0.15f);
    VanDrawList* dl = VanGui::GetForegroundDrawList();
    const float f = follow("bootbar", app.boot_progress(), 0.35f);
    panel(dl, 500, 760, 600, 70);
    text(dl, font_heading(), 18, 520, 770, col(p.gold_bright), "SOLDIER FRONT LEGACY");
    text(dl, font_body(), 14, 1080, 772, col(p.text_dim), app.boot_status(), Align::Right);
    progress(dl, f, 520, 800, 560, 14);
    spinner("##bootspin", 1060, 780, 9, p.gold);
    if (app.data_ready() && app.atlas().ready() && app.screen_time() > 1.0) app.go(Screen::Servers);
}

// ── Signing in: a Team Vanilla account, by username and password (ID-11) ───────

void sign_in_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Palette& p = pal();
    bool open = true;
    if (!begin_modal("Sign in", st.registering ? "CREATE AN ACCOUNT" : "SIGN IN", 480, st.registering ? 500 : 440, &open)) {
        st.login_open = false;
        return;
    }
    VanGui::PushTextWrapPos(0);
    VanGui::TextColored(p.text_dim, st.registering ? "One Team Vanilla account for every server. Your username is private: other soldiers only ever see your code name."
                                                   : "Your Team Vanilla account: one account for every server.");
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(8)});
    VanGui::TextUnformatted("Username");
    input("##acct", st.registering ? "3 to 16 letters, digits or _" : "username", st.account, 448);
    VanGui::TextUnformatted("Password");
    const bool enter = input("##pass", st.registering ? "at least 8 characters" : "password", st.password, 448, true, VanGuiInputTextFlags_EnterReturnsTrue);
    if (st.registering) {
        VanGui::TextUnformatted("Password again");
        input("##pass2", "the same password", st.password2, 448, true);
    }
    (void)VanGui::Checkbox(eng::str::format("Keep me signed in on %s", ui::this_device()).c_str(), &app.settings().remember);
    VanGui::Dummy({0, px(6)});
    VanGui::PushTextWrapPos(0);
    if (!st.login_error.empty()) VanGui::TextColored(p.bad, "%s", st.login_error.c_str());
    else if (s.tv.status_known && s.tv.down) VanGui::TextColored(p.bad, "Team Vanilla's services are down; try again shortly.");
    else if (st.registering && !s.tv.registration_open) VanGui::TextColored(p.bad, "Making new accounts is closed for now.");
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(6)});
    if (s.tv.busy || st.signing_in) {
        VanGui::Spinner("##signin", px(12), px(2.5f), col(p.gold));
        VanGui::SameLine();
        VanGui::TextColored(p.text_dim, st.registering ? "Creating your account..." : "Signing in...");
    } else {
        const bool ok_fields = st.account.size() >= 3 && (st.registering ? st.password.size() >= 8 && st.password == st.password2 : !st.password.empty());
        if (button(st.registering ? "Create account" : "Sign in", 220, 44, Style::Primary, ok_fields) || (enter && ok_fields)) app.sign_in(st.registering);
        VanGui::SameLine();
        if (button(st.registering ? "I have an account" : "Create an account", 220, 44, Style::Ghost)) {
            st.registering = !st.registering;
            st.login_error.clear();
        }
        if (st.registering && !st.password.empty() && st.password.size() < 8) VanGui::TextColored(p.text_dim, "A password is at least 8 characters.");
        else if (st.registering && !st.password2.empty() && st.password != st.password2) VanGui::TextColored(p.text_dim, "The two passwords are not the same.");
    }
    if (!st.registering) {
        // ID-12: no email reset and no recovery code: a member of Team Vanilla restores it.
        VanGui::Dummy({0, px(8)});
        VanGui::PushTextWrapPos(0);
        VanGui::TextColored(p.text_dim, "Forgot your password? %s", s.tv.forgot_password.empty() ? "Contact Team Vanilla." : s.tv.forgot_password.c_str());
        VanGui::PopTextWrapPos();
    }
    end_modal();
    if (!open) st.login_open = false;
}

// A staff reset's temporary password must be changed before anything else (ID-12).
void change_password_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Palette& p = pal();
    if (!VanGui::IsPopupOpen("Change password")) open_modal("Change password");
    if (!begin_modal("Change password", "CHOOSE A NEW PASSWORD", 480, 400)) return;
    VanGui::PushTextWrapPos(0);
    VanGui::TextUnformatted("Your password was reset by SFLegacy Staff. Choose a new one to go on.");
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(8)});
    VanGui::TextUnformatted("The temporary password");
    input("##old", "the one you were given", st.password, 448, true);
    VanGui::TextUnformatted("New password");
    input("##new", "at least 8 characters", st.new_password, 448, true);
    VanGui::TextUnformatted("New password again");
    const bool enter = input("##new2", "the same password", st.new_password2, 448, true, VanGuiInputTextFlags_EnterReturnsTrue);
    VanGui::Dummy({0, px(6)});
    if (!st.login_error.empty()) {
        VanGui::PushTextWrapPos(0);
        VanGui::TextColored(p.bad, "%s", st.login_error.c_str());
        VanGui::PopTextWrapPos();
    }
    const bool ok = !st.password.empty() && st.new_password.size() >= 8 && st.new_password == st.new_password2;
    if (button("Change it", 220, 44, Style::Primary, ok) || (enter && ok)) {
        st.login_error.clear();
        s.tv_change_password(st.password, st.new_password);
    }
    VanGui::SameLine();
    if (button("Sign out", 220, 44, Style::Ghost)) {
        st.change_password_open = false;
        s.tv_sign_out();
        VanGui::CloseCurrentPopup();
    }
    end_modal();
}

// ── Joining a server (§5.1): what is happening, the packs' size asked about, their download ──

namespace {
std::string megabytes(u64 bytes) {
    if (bytes < (1u << 20)) return eng::str::format("%.0f KB", double(bytes) / 1024.0);
    return eng::str::format("%.1f MB", double(bytes) / double(1u << 20));
}
}  // namespace

void join_modal(App& app) {
    ScreenState& st = app.state();
    Session& s = app.session();
    const Palette& p = pal();
    const Session::JoinStage stage = s.join_stage();
    // Only once there is something to say or to ask: a quick join shows its toast alone.
    const bool show = s.content || (st.joining && app.now() - st.join_started > 1.5);
    if (!show) return;
    if (!VanGui::IsPopupOpen("Joining")) open_modal("Joining");
    if (!begin_modal("Joining", "JOINING A SERVER", 520, 300)) return;
    const std::string name = s.server_info ? s.server_info->name : app.server_row().name;
    VanGui::TextColored(p.gold_bright, "%s", name.c_str());
    VanGui::Dummy({0, px(8)});
    VanGui::PushTextWrapPos(0);
    bool cancel = false;
    if (s.content && s.content->ask) {
        // §11.7: the size shown and a yes asked before anything over the threshold comes down.
        VanGui::Text("This server adds its own weapons, maps or characters: %s to download (%zu pack%s). They are kept, so the next join is quick.",
                     megabytes(s.content->need_bytes).c_str(), s.content->packs.size(), s.content->packs.size() == 1 ? "" : "s");
        VanGui::PopTextWrapPos();
        VanGui::Dummy({0, px(14)});
        if (button("Download", 230, 44, Style::Primary)) s.content_accept();
        VanGui::SameLine();
        if (button("Not now", 230, 44, Style::Ghost)) {
            s.content_decline();
            VanGui::CloseCurrentPopup();
        }
        end_modal();
        return;
    }
    const char* what = "Connecting...";
    switch (stage) {
        case Session::JoinStage::Ticket: what = "Asking Team Vanilla for your ticket..."; break;
        case Session::JoinStage::Joining: what = "The server is checking your ticket..."; break;
        case Session::JoinStage::Manifest: what = "Reading what this server adds..."; break;
        case Session::JoinStage::Downloading: what = "Downloading this server's packs..."; break;
        case Session::JoinStage::Mounting: what = "Checking and loading the packs..."; break;
        case Session::JoinStage::Entering: what = "Entering..."; break;
        default: break;
    }
    VanGui::Spinner("##joinspin", px(12), px(2.5f), col(p.gold));
    VanGui::SameLine();
    VanGui::TextUnformatted(what);
    if (s.content && stage == Session::JoinStage::Downloading) {
        const Session::Content& c = *s.content;
        VanGui::Dummy({0, px(8)});
        if (c.queue) {
            // DL-1: the server sends to a few players at once; the rest wait their turn.
            VanGui::TextColored(p.text_dim, "Waiting for a download slot: you are number %u in the queue.", unsigned(c.queue));
        } else {
            const float f = c.need_bytes ? float(double(c.got_bytes) / double(c.need_bytes)) : 1.0f;
            VanGui::ProgressBar(std::clamp(f, 0.0f, 1.0f), {px(488), px(16)}, "");
            size_t at = 0, of = 0;
            for (size_t i = 0; i < c.packs.size(); ++i) {
                if (c.packs[i].cached && i >= c.current) continue;
                ++of;
                if (i <= c.current) at = of;
            }
            VanGui::TextColored(p.text_dim, "%s of %s", megabytes(c.got_bytes).c_str(), megabytes(c.need_bytes).c_str());
            (void)at;
        }
    }
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(14)});
    // DL-4: cancelling disconnects cleanly; what finished stays, what did not is dropped.
    if (button("Cancel", 200, 40, Style::Ghost)) cancel = true;
    end_modal();
    if (cancel) {
        s.disconnect("cancelled");
        st.joining = false;
        if (st.join_toast) resolve(st.join_toast, false, "Cancelled."), st.join_toast = 0;
    }
}

// A private server's password (§12.3), asked before joining.
void server_password_modal(App& app) {
    ScreenState& st = app.state();
    if (!VanGui::IsPopupOpen("Server password")) open_modal("Server password");
    bool open = true;
    if (!begin_modal("Server password", "A PRIVATE SERVER", 460, 230, &open)) {
        st.server_password_open = false;
        return;
    }
    VanGui::Text("%s needs its password.", app.server_row().name.c_str());
    VanGui::Dummy({0, px(6)});
    const bool enter = input("##srvpass", "the server's password", st.server_password, 428, true, VanGuiInputTextFlags_EnterReturnsTrue);
    VanGui::Dummy({0, px(8)});
    if (button("Join", 200, 42, Style::Primary, !st.server_password.empty()) || (enter && !st.server_password.empty())) {
        const App::ServerRow row = app.server_row();
        const std::string password = st.server_password;
        st.server_password.clear();
        st.server_password_open = false;
        VanGui::CloseCurrentPopup();
        end_modal();
        app.connect(row, password);
        return;
    }
    VanGui::SameLine();
    if (button("Cancel", 200, 42)) {
        st.server_password_open = false;
        VanGui::CloseCurrentPopup();
    }
    end_modal();
    if (!open) st.server_password_open = false;
}

// A friend on another server joined, or their room's invitation taken (D24, FR-2, FR-4): the game
// asks first, then leaves this server and joins theirs like any other (its gates, its download).
void switch_modal(App& app) {
    Session& s = app.session();
    if (!s.switch_request) return;
    const Palette& p = pal();
    const Session::Switch sw = *s.switch_request;
    if (!VanGui::IsPopupOpen("Switch server")) open_modal("Switch server");
    bool open = true;
    if (!begin_modal("Switch server", sw.invitation ? "AN INVITATION" : "JOIN A FRIEND", 520, 300, &open)) {
        s.switch_request.reset();
        return;
    }
    VanGui::PushTextWrapPos(0);
    const std::string here = s.signed_in() ? s.server_name : std::string();
    if (sw.invitation) VanGui::Text("%s asks you into their room on %s.", sw.friend_name.c_str(), sw.server.name.c_str());
    if (here.empty()) VanGui::Text("Join %s on %s?", sw.friend_name.c_str(), sw.server.name.c_str());
    else VanGui::Text("Leave %s and join %s on %s?", here.c_str(), sw.friend_name.c_str(), sw.server.name.c_str());
    // FR-2: leaving a match counts as a forfeit, as it always has.
    if (app.screen() == Screen::Match || app.screen() == Screen::Loading)
        VanGui::TextColored(p.bad, "You are in a match: leaving it now counts as a forfeit.");
    if (sw.server.pack_bytes) VanGui::TextColored(p.text_dim, "That server has its own packs (%s): you are asked before anything is downloaded.", megabytes(sw.server.pack_bytes).c_str());
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(12)});
    if (button(sw.invitation ? "Accept" : "Join", 230, 44, Style::Primary)) {
        s.switch_request.reset();
        VanGui::CloseCurrentPopup();
        end_modal();
        app.switch_server(sw.server);
        return;
    }
    VanGui::SameLine();
    if (button(sw.invitation ? "Decline" : "Stay", 230, 44, Style::Ghost)) {
        s.switch_request.reset();
        VanGui::CloseCurrentPopup();
    }
    end_modal();
    if (!open) s.switch_request.reset();
}

// ── The first sign-in: a code name and a force (ID-8: chosen once, at Team Vanilla) ──

void draw_code_name(App& app) {
    ScreenState& st = app.state();
    const Palette& p = pal();
    backdrop(app, 0.45f);
    VanDrawList* dl = VanGui::GetBackgroundDrawList();
    sprite(dl, app.atlas().sprite("sf_mark_1"), 14, 7, 258, 64);
    const float y = 120 + slide("codename.panel", 40);
    panel(dl, 300, y, 1000, 640, "CREATE YOUR SOLDIER");
    begin_area("##codename", 320, y + 44, 960, 580);
    VanGui::PushTextWrapPos(px(940));
    const bool rename = app.session().tv.free_rename && !app.session().tv.code_name.empty();
    VanGui::TextColored(p.gold_bright, rename ? "Choose a new code name." : "Welcome, recruit.");
    if (rename)
        VanGui::Text("Another soldier already goes by %s on Team Vanilla, so this one name change is free.", app.session().tv.code_name.c_str());
    else
        VanGui::TextUnformatted("Choose the code name everyone will know you by, on every server, and the force you start in. ARTC is free for every new soldier; "
                                "the other forces are sold in the Character Shop.");
    VanGui::PopTextWrapPos();
    VanGui::Dummy({0, px(10)});
    VanGui::TextUnformatted("Code name");
    input("##cn", "2 to 16 letters and digits", st.code_name, 400);
    VanGui::Dummy({0, px(10)});
    VanGui::TextUnformatted("Starting force");
    const auto all = forces();
    for (size_t i = 0; i < all.size(); ++i) {
        const ForceDef& f = all[i];
        const bool free = f.price == 0;
        if (i % 4) VanGui::SameLine();
        VanGui::PushID(int(i));
        const std::string label = free ? std::string(f.name) : std::string(f.name) + "\n" + std::to_string(f.price) + " SP";
        if (button(label.c_str(), 225, 56, st.force_sel == int(i) ? Style::Primary : Style::Normal, free)) st.force_sel = int(i);
        VanGui::PopID();
    }
    VanGui::Dummy({0, px(14)});
    const ForceDef& chosen = all[size_t(std::clamp(st.force_sel, 0, int(all.size()) - 1))];
    VanGui::TextColored(p.lime, "%s", chosen.name);
    VanGui::SameLine();
    VanGui::TextDisabled("(%s)  speed %.2f  upper armour %.0f%%  lower armour %.0f%%", chosen.nation, double(chosen.speed),
                         double(chosen.upper_defense * 100), double(chosen.lower_defense * 100));
    VanGui::Dummy({0, px(18)});
    const bool ok = st.code_name.size() >= 2;
    if (button("Enlist", 260, 48, Style::Primary, ok)) app.session().set_code_name(st.code_name, chosen.id);
    VanGui::SameLine();
    if (button("Sign out", 200, 48, Style::Ghost)) {
        app.session().tv_sign_out();
        app.go(Screen::Servers);
    }
    end_area();
    bottom_bar(app, "New soldier");
}

}  // namespace lsf
