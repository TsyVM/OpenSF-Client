// Team Vanilla's side of the session (Docs/UniversalServerDeploy.md §4.1, §5, §6, §14): the account,
// the server list, tickets, and everything universal. TVAS answers in JSON; what it says is turned
// into the game's own structures here, so the screens read them as they always did.
#include "Game/Net/Session.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Tvas.hpp"
#include "Game/TvasJson.hpp"
#include "Game/TvasProto.hpp"

#include <algorithm>
#include <ctime>

namespace lsf {

using namespace proto;

namespace {

u32 u32_of(const eng::json::Value& v, u32 fallback = 0) { return u32(std::min<u64>(v.as_uint(fallback), 0xFFFFFFFFull)); }
u16 u16_of(const eng::json::Value& v, u16 fallback = 0) { return u16(std::min<u64>(v.as_uint(fallback), 0xFFFFull)); }
u8 u8_of(const eng::json::Value& v, u8 fallback = 0) { return u8(std::min<u64>(v.as_uint(fallback), 0xFFull)); }

const char* platform_text() {
#if defined(__ANDROID__)
    return "android";
#elif defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

ListedServer listed(const eng::json::Value& s) {
    ListedServer o;
    o.id = s["id"].as_uint();
    o.name = eng::str::sanitize_line(s["name"].str(), 48);
    o.region = eng::str::sanitize_line(s["region"].str(), 24);
    o.description = eng::str::sanitize_line(s["description"].str(), 200);
    o.owner = eng::str::sanitize_line(s["owner"].str(), kNameMax);
    o.motd = s["motd"].str();
    o.address = s["address"].str();
    o.port = u16_of(s["port"], kDefaultPort);
    o.tier = u8_of(s["tier"]);
    o.official = s["official"].as_bool();
    o.locked = s["locked"].as_bool();
    o.players = u16_of(s["players"]), o.slots = u16_of(s["slots"]);
    o.min_rank = u8_of(s["min_rank"]);
    o.manifest = s["manifest"].str();
    o.pack_bytes = u32_of(s["pack_bytes"]);
    o.build = u32_of(s["build"]);
    return o;
}

using tvproto::weapon_of;

// A pack thing's saved code (NM-7), empty for base content.
std::string custom_code(ShopKind kind, u16 id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (id >= kFirstPackWeapon && id != kNoWeapon)
                if (const WeaponDef* w = weapon(id)) return w->code;
            break;
        case ShopKind::Force:
            if (id >= kFirstPackForce && id < kNoForce)
                if (const PackForce* f = registry::force(u8(id))) return f->code;
            break;
        case ShopKind::Item:
            if (id >= kFirstPackItem)
                if (const PackItem* i = registry::item(id)) return i->code;
            break;
        default: break;
    }
    return {};
}

}  // namespace

// ── The account ────────────────────────────────────────────────────────────────

void Session::set_tvas(const std::string& address, std::filesystem::path token_file) {
    tvas_.configure(address);
    token_file_ = std::move(token_file);
}

u64 Session::tv_now() const { return u64(eng::i64(std::time(nullptr)) + tv.clock_skew); }
u64 Session::server_now(double) const { return tv_now(); }

void Session::tv_apply_status(const Json& s) {
    tv.status_known = true;
    tv.down = false;
    tv.registration_open = s["registration_open"].as_bool(true);
    tv.min_build = u32_of(s["min_build"]);
    tv.forgot_password = s["forgot_password"].str();
    tv.news = s["news"].str();
    if (s["time"].as_uint()) tv.clock_skew = eng::i64(s["time"].as_uint()) - eng::i64(std::time(nullptr));
    const Json& iv = s["poll_intervals"];
    if (iv.is_object()) intervals_ = {std::clamp(iv["chat"].as_double(3), 1.0, 60.0), std::clamp(iv["menu"].as_double(6), 2.0, 120.0), std::clamp(iv["match"].as_double(15), 5.0, 300.0)};
    // A test's own TVAS on this machine says which keys it signs with (never any other address).
    if (tvas::loopback(tvas_.address())) tvas::trust_test_keys(tvas_.address(), s["keys"]);
}

void Session::tv_status() {
    tvas_.get("/v1/status", [this](const TvReply& r) {
        tv.status_known = true;
        tv.down = !r.ok();
        if (r.ok()) tv_apply_status(r.body);
    });
}

void Session::tv_post(const std::string& path, const Json& body, std::function<void(const TvReply&)> done) {
    tvas_.post(path, body, [this, done = std::move(done)](const TvReply& r) {
        if (r.code == "signed_out") tv_lost();
        if (done) done(r);
    });
}

void Session::tv_lost() {
    if (!tv.signed_in) return;
    tvas_.set_token({});
    forget_session_token(token_file_);
    tv.signed_in = false;
    tv_error = "Your sign-in has run out: sign in again.";
    ev_tv_signed_out = true;
    if (state_ != State::Offline) {
        disconnect("signed out");
        disconnect_reason = tv_error;
        ev_disconnected = true;
    }
}

void Session::tv_apply_me(const Json& me) {
    if (!me.is_object() || !me["id"].as_uint()) return;
    tv.account = me["id"].as_uint();
    tv.code_name = me["code_name"].str();
    tv.free_rename = me["free_rename"].as_bool();
    tv.global_role = u8_of(me["global_role"]);
    tv.xp = u32_of(me["xp"]), tv.sp = u32_of(me["sp"]), tv.coins = u32_of(me["coins"]);
    tv.must_change = me["must_change"].as_bool(tv.must_change);
    if (me.has("presence")) tv.presence = u8_of(me["presence"]);
    if (me.has("messages_from")) tv.messages_from = u8_of(me["messages_from"]);
    if (me.has("invites")) tv.invites = me["invites"].as_bool(true);
    const Json& lim = me["xp_limit"];
    if (lim.is_object()) {
        tv.levels_left = u8_of(lim["levels_left"], 25);
        tv.xp_capped = lim["reached"].as_bool();
        tv.xp_resets = lim["resets"].as_uint();
    }
    if (me["now"].as_uint()) tv.clock_skew = eng::i64(me["now"].as_uint()) - eng::i64(std::time(nullptr));
    const u32 rev = u32_of(me["rev"]);
    if (rev >= tv.rev) tv.rev = rev;
    want_refresh(tv.rev);
}

void Session::want_refresh(u32 rev) {
    if (rev > refresh_rev_) refresh_rev_ = rev;
}

void Session::changed(const Json& reply) {
    if (reply["me"].is_object()) tv_apply_me(reply["me"]);
}

void Session::tv_signed(const Json& signin, bool remember) {
    tvas_.set_token(signin["token"].str());
    tv.signed_in = true;
    tv.username = signin["username"].str();
    tv.must_change = signin["must_change"].as_bool();
    tv.rev = 0;
    tv_apply_me(signin["me"]);
    if (remember) save_session_token(token_file_, tvas_.token());
    else forget_session_token(token_file_);
    cursor_ = 0;
    first_poll_ = true;
    last_poll_ = -1000;
    ev_tv_signed_in = true;
    friends.reset(), mailbox.reset(), clan.reset(), rewards.reset();
    request_friends();
    request_mail();
    request_clan();
}

void Session::tv_sign_in(const std::string& username, const std::string& password, bool create, bool remember) {
    if (tv.busy) return;
    tv.busy = true;
    tv_error.clear();
    Json b;
    b["username"] = std::string(eng::str::trim(username));
    b["password"] = password;   // to TVAS only, over HTTPS (ID-1); never kept, never logged
    b["platform"] = platform_text();
    tvas_.post(create ? "/v1/account/create" : "/v1/session", b, [this, remember](const TvReply& r) {
        tv.busy = false;
        tv.down = r.down();
        if (!r.ok()) {
            tv_error = r.text();
            ev_tv_refused = true;
            return;
        }
        tv_signed(r.body, remember);
    }, 30);
}

bool Session::tv_resume() {
    const std::string token = load_session_token(token_file_);
    if (token.empty()) return false;
    tvas_.set_token(token);
    tv.busy = true;
    tvas_.get("/v1/me", [this](const TvReply& r) {
        tv.busy = false;
        if (!r.ok()) {
            tv.down = r.down();
            // A sign-in that has run out is forgotten; one TVAS could not answer for is kept for next time.
            if (!r.down()) forget_session_token(token_file_);
            tvas_.set_token({});
            return;
        }
        tv.signed_in = true;
        tv.rev = 0;
        tv_apply_me(r.body);
        cursor_ = 0;
        first_poll_ = true;
        last_poll_ = -1000;
        ev_tv_signed_in = true;
        request_friends();
        request_mail();
        request_clan();
    });
    return true;
}

// The game closing: friends see this soldier away at once, not a minute on. The sign-in is kept.
void Session::tv_away() {
    if (!tv.signed_in || tvas_.token().empty()) return;
    Json b;
    b["away"] = true;
    tvas_.post("/v1/poll", b, {}, 3);
    tvas_.drain(2.0);
}

void Session::tv_sign_out() {
    if (state_ != State::Offline) disconnect("signed out");
    tv_away();
    if (!tvas_.token().empty()) tvas_.post("/v1/session/end", Json(), {});
    forget_session_token(token_file_);
    const Tv was = tv;
    tv = Tv{};
    tv.status_known = was.status_known, tv.down = was.down, tv.registration_open = was.registration_open, tv.min_build = was.min_build;
    tv.forgot_password = was.forgot_password, tv.news = was.news, tv.clock_skew = was.clock_skew;
    friends.reset(), mailbox.reset(), clan.reset(), clan_invites.clear(), rewards.reset(), id_card.reset(), switch_request.reset();
    std::erase_if(servers, [](const ListedServer& o) { return !o.direct; });
    tvas_.set_token({});   // the request that ends it already carries it
    ev_tv_signed_out = true;
}

void Session::tv_change_password(const std::string& old_password, const std::string& new_password) {
    Json b;
    b["old"] = old_password;
    b["new"] = new_password;
    tv_post("/v1/account/password", b, [this](const TvReply& r) {
        if (!r.ok()) {
            tv_error = r.text();
            ev_tv_refused = true;
            return;
        }
        tv.must_change = false;
        ev_tv_password = true;
    });
}

void Session::set_code_name(const std::string& name, u8 force) {
    Json b;
    b["name"] = name;
    b["force"] = unsigned(force);
    tv_post("/v1/account/codename", b, [this](const TvReply& r) {
        if (!r.ok()) {
            notices.push_back({NoticeKind::Bad, r.text()});
            return;
        }
        tv_apply_me(r.body);
        ev_named = true;
    });
}

void Session::set_social(PresenceMode presence, MessagesFrom from) {
    tv.presence = u8(presence), tv.messages_from = u8(from);
    Json b;
    b["presence"] = unsigned(presence);
    b["messages_from"] = unsigned(from);
    tv_post("/v1/social/settings", b, {});
}

void Session::set_room_invites(bool on) {
    room_invites_ = on;
    if (signed_in()) send(ChatPrefs{global_chat_, room_invites_});
    if (tv.signed_in && tv.invites != on) {
        tv.invites = on;
        Json b;
        b["invites"] = on;
        tv_post("/v1/social/settings", b, {});
    }
}

// ── The list (§8) ──────────────────────────────────────────────────────────────

void Session::request_servers(double now) {
    if (servers_loading) return;
    servers_loading = true;
    servers_error.clear();
    servers_at = now;
    auto ask = [this] {
        tvas_.get("/v1/servers", [this](const TvReply& r) {
            servers_loading = false;
            if (!r.ok()) {
                servers_error = r.text();
                return;
            }
            // SL-4: every entry TVAS serves is signed; a list that is not is not shown at all.
            if (!tvas::reply_signed(r.raw, r.signature)) {
                servers_error = "The server list did not check out as Team Vanilla's.";
                return;
            }
            std::vector<ListedServer> out;
            for (const Json& s : r.body["servers"].elements()) {
                ListedServer o = listed(s);
                if (!o.id || o.address.empty()) continue;
                // What a server answered a moment ago is kept until it answers again.
                for (const ListedServer& old : servers)
                    if (old.id == o.id) o.ping_ms = old.ping_ms, o.answered = old.answered, o.map = old.map;
                out.push_back(std::move(o));
            }
            // D5: Team Vanilla's own first, whatever the order they came in; the player's own
            // (by address: SL-8) after the list.
            std::stable_sort(out.begin(), out.end(), [](const ListedServer& a, const ListedServer& b) { return a.official > b.official; });
            for (const ListedServer& old : servers)
                if (old.direct) out.push_back(old);
            servers = std::move(out);
            if (r.body["time"].as_uint()) tv.clock_skew = eng::i64(r.body["time"].as_uint()) - eng::i64(std::time(nullptr));
            ping_servers(now_);
        });
    };
    if (tv.status_known && !tv.down) return ask();
    tvas_.get("/v1/status", [this, ask](const TvReply& r) {
        tv.status_known = true;
        tv.down = !r.ok();
        if (!r.ok()) {
            servers_loading = false;
            servers_error = r.text();
            return;
        }
        tv_apply_status(r.body);
        ask();
    });
}

// ── Tickets (§5.1, ID-3) ───────────────────────────────────────────────────────

void Session::ask_ticket() {
    if (!server_info) return;
    Json b;
    b["server"] = server_info->server_id;
    const u64 server = server_info->server_id;
    tv_post("/v1/ticket", b, [this, server](const TvReply& r) {
        if (stage_ != JoinStage::Ticket || !server_info || server_info->server_id != server) return;   // the join was given up meanwhile
        if (!r.ok()) return fail_join(r.text());
        blocked_packs_.clear();
        for (const Json& h : r.body["blocked_packs"].elements()) blocked_packs_.push_back(eng::str::lower(h.str()));
        Join j;
        j.ticket = r.body["ticket"].str();
        j.password = join_password_;
        stage_ = JoinStage::Joining;
        send(j);
    });
}

// ── News (TV-8) ────────────────────────────────────────────────────────────────

void Session::tv_poll(double now) {
    if (!tv.signed_in || poll_in_flight_) return;
    if (now - last_poll_ < intervals_[size_t(pace_)]) return;
    last_poll_ = now;
    poll_in_flight_ = true;
    Json b;
    b["cursor"] = cursor_;
    if (first_poll_) b["first"] = true;   // a session's first: nothing older than now is news
    tv_post("/v1/poll", b, [this](const TvReply& r) {
        poll_in_flight_ = false;
        tv.down = r.down();
        if (!r.ok()) return;
        first_poll_ = false;
        cursor_ = r.body["cursor"].as_uint(cursor_);
        const Json& iv = r.body["intervals"];
        if (iv.is_object()) intervals_ = {std::clamp(iv["chat"].as_double(3), 1.0, 60.0), std::clamp(iv["menu"].as_double(6), 2.0, 120.0), std::clamp(iv["match"].as_double(15), 5.0, 300.0)};
        if (r.body["now"].as_uint()) tv.clock_skew = eng::i64(r.body["now"].as_uint()) - eng::i64(std::time(nullptr));
        for (const Json& e : r.body["events"].elements()) tv_event(e["kind"].str(), e["data"], now_);
        const u32 rev = u32_of(r.body["rev"]);
        if (rev > tv.rev) {
            // The record changed somewhere else (a match's pay, a gift, staff): read it again.
            tv.rev = rev;
            want_refresh(rev);
            tvas_.get("/v1/me", [this](const TvReply& me) {
                if (me.ok()) tv_apply_me(me.body);
            });
        }
    });
}

void Session::tv_event(const std::string& kind, const Json& d, double now) {
    if (kind == "whisper" || kind == "clan_chat") {
        ChatLine l;
        l.scope = u8(kind == "whisper" ? ChatScope::Whisper : ChatScope::Clan);
        l.from = eng::str::sanitize_line(d["from"].str(), kNameMax);
        l.text = eng::str::sanitize_line(d["text"].str(), kChatMax);
        l.role = d["role"].as_uint() ? u8(Role::Moderator) : u8(0);
        if (kind == "clan_chat" && clan) l.clan = clan->name;
        add_chat(std::move(l), now);
    } else if (kind == "mail") {
        request_mail();
        notices.push_back({NoticeKind::Info, d["system"].as_bool() ? std::string("Team Vanilla left you a message: see your inbox.")
                                                                   : eng::str::sanitize_line(d["from"].str(), kNameMax) + " left you a message: see your inbox."});
    } else if (kind == "friends") {
        request_friends();
    } else if (kind == "notice") {
        notices.push_back({NoticeKind(std::min<u64>(d["kind"].as_uint(), 3)), eng::str::sanitize_line(d["text"].str(), 300)});
    } else if (kind == "clan") {
        request_clan();
    } else if (kind == "clan_invite") {
        ClanInvited inv = tvproto::clan_invited(d);
        std::erase_if(clan_invites, [&](const ClanInvited& o) { return o.clan == inv.clan; });
        clan_invites.push_back(std::move(inv));
    } else if (kind == "room_invite") {
        // FR-4: a room on another server asks you in; saying yes means switching servers.
        if (!room_invites_ || d["until"].as_uint() < tv_now()) return;
        Switch s;
        s.server = listed(d["server"]);
        s.friend_name = eng::str::sanitize_line(d["from"].str(), kNameMax);
        s.channel = u8_of(d["channel"]);
        s.room = u16_of(d["room"]);
        s.invitation = true;
        if (server_info && server_info->server_id == s.server.id) return;   // this server's own invitations come from it
        switch_request = std::move(s);
    } else if (kind == "gift") {
        notices.push_back({NoticeKind::Good, eng::str::sanitize_line(d["from"].str(), kNameMax) + " sent you a gift: " + eng::str::sanitize_line(d["name"].str(), 48) +
                                                 ". It waits in your inventory's Gift tab."});
    }
    // "refresh": the poll's own `rev` says so.
}

// ── Chat that is Team Vanilla's (whispers, clan chat: §14.5, §14.6) ────────────

void Session::chat(ChatScope scope, const std::string& text, const std::string& to) {
    if (scope == ChatScope::Whisper) {
        // ML-1: a whisper goes through TVAS, to any server (no game server ever sees one: SO-2).
        Json b;
        b["to"] = to;
        b["text"] = text;
        tv_post("/v1/whisper", b, [this, to, text](const TvReply& r) {
            const bool ok = r.ok() && r.body["ok"].as_bool();
            // Echoed to its sender whether it was heard or left in the inbox (the notice says which).
            if (ok) {
                ChatLine l;
                l.scope = u8(ChatScope::Whisper);
                l.from = tv.code_name;
                l.to = r.body["to"].str(to);
                l.text = eng::str::sanitize_line(text, kChatMax);
                add_chat(std::move(l), now_);
            }
            const std::string said = r.ok() ? r.body["text"].str() : r.text();
            if (!said.empty()) notices.push_back({ok ? NoticeKind::Info : NoticeKind::Warning, said});
        });
        return;
    }
    if (scope == ChatScope::Clan) {
        Json b;
        b["op"] = "chat";
        b["text"] = text;
        tv_post("/v1/clan", b, [this](const TvReply& r) {
            const std::string said = r.ok() ? (r.body["ok"].as_bool() ? std::string() : r.body["text"].str()) : r.text();
            if (!said.empty()) notices.push_back({NoticeKind::Warning, said});
        });
        last_poll_ = now_ - intervals_[size_t(pace_)] + 0.5;   // TV-8: your own line shows straight away
        return;
    }
    Chat m;
    m.scope = u8(scope);
    m.text = text;
    m.to = to;
    send(m);
}

// ── Friends, blocks and mail (§14.2 to §14.5) ──────────────────────────────────

void Session::apply_friends(const Json& list) { friends = tvproto::friend_list(list); }

void Session::request_friends() {
    if (!tv.signed_in) return;
    tvas_.get("/v1/friends", [this](const TvReply& r) {
        if (r.ok()) apply_friends(r.body);
    });
}

void Session::friend_action(FriendOp op, const std::string& code_name) {
    Json b;
    b["op"] = tvproto::friend_op(op);
    b["code_name"] = code_name;
    tv_post("/v1/friends", b, [this](const TvReply& r) {
        if (!r.ok()) {
            notices.push_back({NoticeKind::Warning, r.text()});
            return;
        }
        apply_friends(r.body);
        const std::string said = r.body["text"].str();
        if (!said.empty()) notices.push_back({r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
    });
}

void Session::apply_mailbox(const Json& box) { mailbox = tvproto::mailbox(box); }

void Session::request_mail() {
    if (!tv.signed_in) return;
    tvas_.get("/v1/mail", [this](const TvReply& r) {
        if (r.ok()) apply_mailbox(r.body);
    });
}

void Session::mail_action(MailOp op, u32 id) {
    Json b;
    b["op"] = tvproto::mail_op(op);
    b["id"] = id;
    // Shown at once; TVAS's answer is the truth.
    if (mailbox)
        for (MailItem& m : mailbox->items)
            if (op == MailOp::ReadAll || (m.id == id && op == MailOp::Read)) m.read = true;
    tv_post("/v1/mail", b, [this](const TvReply& r) {
        if (!r.ok()) {
            notices.push_back({NoticeKind::Warning, r.text()});
            return;
        }
        apply_mailbox(r.body);
        const std::string said = r.body["text"].str();
        if (!said.empty()) notices.push_back({r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
    });
}

void Session::send_mail(const std::string& to, const std::string& text) {
    Json b;
    b["to"] = to;
    b["text"] = text;
    tv_post("/v1/mail/send", b, [this](const TvReply& r) {
        const std::string said = r.ok() ? r.body["text"].str() : r.text();
        if (!said.empty()) notices.push_back({r.ok() && r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
    });
}

void Session::join_friend(const std::string& code_name) {
    Json b;
    b["code_name"] = code_name;
    tv_post("/v1/join-friend", b, [this](const TvReply& r) {
        if (!r.ok() || !r.body["ok"].as_bool()) {
            notices.push_back({NoticeKind::Warning, r.ok() ? r.body["text"].str("That friend cannot be joined now.") : r.text()});
            return;
        }
        Switch s;
        s.server = listed(r.body["server"]);
        s.friend_name = eng::str::sanitize_line(r.body["friend"].str(), kNameMax);
        s.channel = u8_of(r.body["channel"]);
        s.room = u16_of(r.body["room"]);
        if (state_ == State::SignedIn && server_info && server_info->server_id == s.server.id) {
            // The same server: straight to their room, as always.
            if (s.channel && s.channel != channel) join_channel(s.channel);
            if (s.room) join_room(s.room);
            return;
        }
        switch_request = std::move(s);   // FR-2: the app asks first, then switches
    });
}

void Session::room_invite(const std::string& code_name) {
    // A friend on another server is asked through TVAS (FR-4); anyone here, by the server as always.
    const FriendEntry* there = nullptr;
    if (friends && server_info)
        for (const FriendEntry& f : friends->entries)
            if (f.state == u8(FriendState::Friend) && eng::str::iequals(f.name, code_name) && f.online && f.server && f.server != server_info->server_id) there = &f;
    if (!there || !room) {
        RoomInvite m;
        m.code_name = code_name;
        send(m);
        return;
    }
    Json b;
    b["code_name"] = code_name;
    b["server"] = server_info->server_id;
    b["room"] = unsigned(room->id);
    b["channel"] = unsigned(channel);
    b["title"] = room->settings.title;
    b["mode"] = unsigned(room->settings.mode);
    b["map"] = room->settings.map;
    b["players"] = unsigned(room->members.size());
    b["max_players"] = unsigned(room->settings.max_players);
    tv_post("/v1/invite", b, [this](const TvReply& r) {
        const std::string said = r.ok() ? r.body["text"].str() : r.text();
        if (!said.empty()) notices.push_back({r.ok() && r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
    });
}

// ── Clans (§14.1: one clan system, in TVAS) ────────────────────────────────────

void Session::apply_clan(const Json& s) {
    if (!s.is_object()) return;
    std::optional<ClanInvited> invite;
    ClanState c = tvproto::clan_state(s, &invite);
    if (c.member) clan_invites.clear();
    if (invite) {
        std::erase_if(clan_invites, [&](const ClanInvited& o) { return o.clan == invite->clan; });
        clan_invites.push_back(std::move(*invite));
    }
    clan = std::move(c);
}

void Session::request_clan() {
    if (!tv.signed_in) return;
    tvas_.get("/v1/clan", [this](const TvReply& r) {
        if (r.ok()) apply_clan(r.body);
    });
}

void Session::clan_post(Json body) {
    tv_post("/v1/clan", body, [this](const TvReply& r) {
        if (!r.ok()) {
            notices.push_back({NoticeKind::Warning, r.text()});
            return;
        }
        apply_clan(r.body["state"]);
        const std::string said = r.body["text"].str();
        if (!said.empty()) notices.push_back({r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
        // A clan joined, left or renamed is on the soldier's badge: the record is read again.
        tvas_.get("/v1/me", [this](const TvReply& me) {
            if (me.ok()) tv_apply_me(me.body);
        });
    });
}

void Session::create_clan(const std::string& name, const std::string& notice, const ClanMark& mark) {
    Json b;
    b["op"] = "create";
    b["name"] = name;
    b["text"] = notice;
    b["mark"] = tvjson::mark(mark);
    clan_post(std::move(b));
}

void Session::set_clan_mark(const ClanMark& mark) {
    Json b;
    b["op"] = "mark";
    b["mark"] = tvjson::mark(mark);
    clan_post(std::move(b));
}

void Session::leave_clan() {
    Json b;
    b["op"] = "leave";
    clan_post(std::move(b));
}

void Session::clan_invite(const std::string& code_name) {
    Json b;
    b["op"] = "invite";
    b["code_name"] = code_name;
    clan_post(std::move(b));
}

void Session::clan_answer(const std::string& clan_name, bool accept) {
    std::erase_if(clan_invites, [&](const ClanInvited& o) { return o.clan == clan_name; });
    Json b;
    b["op"] = "answer";
    b["clan"] = clan_name;
    b["accept"] = accept;
    clan_post(std::move(b));
}

void Session::clan_kick(const std::string& code_name) {
    Json b;
    b["op"] = "kick";
    b["code_name"] = code_name;
    clan_post(std::move(b));
}

void Session::clan_action(ClanOp op, const std::string& code_name, const std::string& text) {
    Json b;
    b["op"] = tvproto::clan_op(op);
    b["code_name"] = code_name;
    b["text"] = text;
    clan_post(std::move(b));
}

void Session::browse_clans() {
    tvas_.get("/v1/clans", [this](const TvReply& r) {
        if (r.ok()) clan_directory = tvproto::clan_directory(r.body);
    });
}

// ── The ID card ────────────────────────────────────────────────────────────────

void Session::request_id_card(const std::string& code_name) {
    Json b;
    b["name"] = code_name;
    tv_post("/v1/idcard", b, [this, code_name](const TvReply& r) { id_card = tvproto::id_card(r.ok() ? r.body : Json(), code_name); });
}

void Session::set_card_message(const std::string& text) {
    Json b;
    b["text"] = text;
    tv_post("/v1/card", b, [this](const TvReply& r) {
        const std::string said = r.ok() ? r.body["text"].str() : r.text();
        if (!said.empty()) notices.push_back({r.ok() && r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
        if (r.ok()) changed(r.body), request_id_card(tv.code_name);
    });
}

// ── Rewards and events (Team Vanilla's: MN-5) ──────────────────────────────────

void Session::request_rewards() {
    if (!tv.signed_in) return;
    tvas_.get("/v1/rewards", [this](const TvReply& r) {
        if (!r.ok()) return;
        rewards = tvproto::rewards_state(r.body);
        rewards_at = now_;
        if (r.body["now"].as_uint()) tv.clock_skew = eng::i64(r.body["now"].as_uint()) - eng::i64(std::time(nullptr));
    });
}

void Session::claim_reward(ClaimKind kind, u8 index) {
    Json b;
    b["kind"] = tvproto::claim_kind(kind);
    b["index"] = unsigned(index);
    tv_post("/v1/rewards/claim", b, [this](const TvReply& r) {
        const std::string said = r.ok() ? r.body["text"].str() : r.text();
        if (!said.empty()) notices.push_back({r.ok() && r.body["ok"].as_bool() ? NoticeKind::Good : NoticeKind::Warning, said});
        if (!r.ok()) return;
        changed(r.body);
        request_rewards();
    });
}

void Session::open_box(u8 box) {
    Json b;
    b["box"] = unsigned(box);
    tv_post("/v1/rewards/box", b, [this, box](const TvReply& r) {
        BoxOpened o = tvproto::box_opened(r.ok() ? r.body : Json(), box);
        if (!r.ok()) {
            o.text = r.text();
        } else {
            changed(r.body);
            request_rewards();
        }
        box_opened = std::move(o);
    });
}

void Session::staff_request_rewards() {
    tvas_.get("/v1/staff/rewards", [this](const TvReply& r) {
        if (!r.ok()) {
            staff_results.push_back({false, r.text()});
            return;
        }
        StaffRewards s;
        if (!tvjson::rewards_config(r.body["config"], s.config)) return;
        s.now = r.body["now"].as_uint();
        staff_rewards = std::move(s);
        staff_rewards_at = now_;
    });
}

void Session::staff_save_rewards(const RewardsConfig& config) {
    Json b;
    b["config"] = tvjson::rewards_config(config);
    tv_post("/v1/staff/rewards", b, [this](const TvReply& r) {
        if (!r.ok()) {
            staff_results.push_back({false, r.text()});
            return;
        }
        StaffRewards s;
        if (tvjson::rewards_config(r.body["config"], s.config)) {
            s.now = r.body["now"].as_uint();
            staff_rewards = std::move(s);
            staff_rewards_at = now_;
        }
        staff_results.push_back({true, "The rewards and events are saved, on every server."});
        request_rewards();
    });
}

// ── Sales: the player's own approval first (MN-2) ──────────────────────────────

void Session::approve_then(const Json& what, u32 amount, u8 currency, std::function<void(const std::string&)> then, std::function<void(std::string)> refuse) {
    if (!server_info || state_ != State::SignedIn) return refuse("You are not on a server.");
    Json b = what;
    b["server"] = server_info->server_id;
    b["amount"] = amount;
    b["currency"] = unsigned(currency);
    tv_post("/v1/approval", b, [then = std::move(then), refuse = std::move(refuse)](const TvReply& r) {
        if (!r.ok()) return refuse(r.text());
        then(r.body["approval"].str());
    });
}

void Session::buy(ShopKind kind, u16 item, u8 offer) {
    const ShopLine line = shop_line(kind, item);
    if (!line.exists || offer >= line.offers.size()) {
        shop_results.push_back({false, "That is not for sale."});
        return;
    }
    const ShopOffer o = line.offers[offer];
    const std::string code = custom_code(kind, item);
    Json what;
    what["days"] = unsigned(o.days);
    if (!code.empty()) {
        what["kind"] = "custom";
        what["item"] = code;
    } else {
        what["kind"] = "ware";
        what["ware"] = unsigned(kind);
        what["item"] = tvjson::ware_key(kind, item);
    }
    approve_then(
        what, o.price, 0,
        [this, kind, item, offer, price = o.price](const std::string& approval) {
            Buy m;
            m.kind = u8(kind), m.item = item, m.offer = offer, m.approval = approval, m.price = price;
            send(m);
        },
        [this](std::string why) { shop_results.push_back({false, std::move(why)}); });
}

void Session::send_gift(ShopKind kind, u16 item, u8 offer, const std::string& to) {
    const ShopLine line = shop_line(kind, item);
    if (!line.exists || offer >= line.offers.size()) {
        shop_results.push_back({false, "That is not for sale."});
        return;
    }
    const ShopOffer o = line.offers[offer];
    const u32 price = gift_price(shop_config(), o.price);
    const std::string code = custom_code(kind, item);
    const std::string who = eng::str::sanitize_line(to, kNameMax);
    Json what;
    what["days"] = unsigned(o.days);
    what["to"] = who;
    if (!code.empty()) {
        what["kind"] = "custom_gift";
        what["item"] = code;
    } else {
        what["kind"] = "gift";
        what["ware"] = unsigned(kind);
        what["item"] = tvjson::ware_key(kind, item);
    }
    approve_then(
        what, price, 0,
        [this, kind, item, offer, who, price](const std::string& approval) {
            SendGift m;
            m.kind = u8(kind), m.item = item, m.offer = offer, m.to = who, m.approval = approval, m.price = price;
            send(m);
        },
        [this](std::string why) { shop_results.push_back({false, std::move(why)}); });
}

// ── Team Vanilla's own shop services (§6.3, §6.5: asked of TVAS, never of a server) ──

void Session::repair(u16 weapon_id) {
    const WeaponDef* w = weapon(weapon_id);
    if (!w || w->id >= kFirstPackWeapon) {
        ServiceResult g;
        g.op = u8(RequestOp::Repair);
        g.weapon = weapon_id;
        g.text = w ? "Team Vanilla mends only its own guns: this server's are its own." : "You do not own that weapon.";
        service_results.push_back(std::move(g));
        return;
    }
    Json b;
    b["weapon"] = w->code;
    simple_service(RequestOp::Repair, "/v1/shop/repair", b);
}

// A service whose answer is a line of text (and the record, read again).
void Session::simple_service(RequestOp op, const std::string& path, const Json& body) {
    tv_post(path, body, [this, op](const TvReply& r) {
        ServiceResult g = tvproto::service_result(r.ok() ? r.body : Json(), op, kNoWeapon);
        if (!r.ok()) g.text = r.text();
        service_results.push_back(std::move(g));
        if (r.ok()) changed(r.body);
    });
}

void Session::sell(ShopKind kind, u16 id) {
    if (!custom_code(kind, id).empty()) {
        // This server's own thing (D12): sold back to the server it lives on (SH-8).
        send(Request{u8(RequestOp::Sell), id, u16(kind)});
        return;
    }
    Json b;
    b["ware"] = unsigned(kind);
    b["item"] = tvjson::ware_key(kind, id);
    simple_service(RequestOp::Sell, "/v1/shop/sell", b);   // SH-7: at the official price, everywhere
}

void Session::set_spray(u16 spray_id) {
    Json b;
    b["id"] = unsigned(spray_id);
    spray = spray_id;
    simple_service(RequestOp::SetSpray, "/v1/spray", b);
}

void Session::buy_coins(u32 n) {
    Json b;
    b["n"] = n;
    simple_service(RequestOp::BuyCoins, "/v1/shop/coins", b);   // SH-10: only at TV's rate
}

void Session::buy_horror_item(HorrorItem item, u32 how_many) {
    Json b;
    b["item"] = unsigned(item);
    b["n"] = how_many;
    simple_service(RequestOp::BuyHorrorItem, "/v1/shop/horror", b);
}

void Session::supply_crate(u16 days) {
    Json b;
    b["days"] = unsigned(days);
    simple_service(RequestOp::SupplyCrate, "/v1/shop/crate", b);
}

void Session::refresh_tv_shop() {
    if (!tv.signed_in || now_ - tv_shop_asked_ < 60.0) return;
    tv_shop_asked_ = now_;
    tvas_.get("/v1/tvshop", [this](const TvReply& r) {
        if (!r.ok()) return;
        ShopConfig c;
        if (tvjson::shop_config(r.body["config"], c)) tv_shop = std::move(c);
    });
}

const std::vector<CapsuleDef>& Session::bags_on_sale() const {
    // Until TVAS answers: an official server's own shop is TV's; any other's bags are not sold.
    static const std::vector<CapsuleDef> none;
    if (tv_shop) return tv_shop->bags;
    return on_official_server() ? shop_config().bags : none;
}

const CapsuleDef* Session::bag_def(u16 id) const {
    for (const CapsuleDef& b : bags_on_sale())
        if (b.id == id) return &b;
    return nullptr;
}

std::vector<const CapsuleDef*> Session::machine_capsules() const {
    std::vector<const CapsuleDef*> out;
    const bool tv_here = !tv_shop && on_official_server();
    if (tv_shop)
        for (const CapsuleDef& c : tv_shop->capsules)
            if (c.id < kCustomCapsuleFirst) out.push_back(&c);
    for (const CapsuleDef& c : shop_config().capsules)
        if (c.id >= kCustomCapsuleFirst || tv_here) out.push_back(&c);
    return out;
}

const CapsuleDef* Session::machine_capsule(u16 id) const {
    for (const CapsuleDef* c : machine_capsules())
        if (c->id == id) return c;
    return nullptr;
}

void Session::buy_bag(u16 bag, u32 how_many) {
    Json b;
    b["id"] = unsigned(bag);
    b["n"] = how_many;
    simple_service(RequestOp::BuyBag, "/v1/shop/bag/buy", b);
}

void Session::open_gift(u32 gift) {
    Json b;
    b["id"] = gift;
    simple_service(RequestOp::OpenGift, "/v1/gift/open", b);
}

namespace {
CapsuleResult capsule_result(u16 id, const TvReply& r) {
    CapsuleResult c = tvproto::capsule_result(r.ok() ? r.body : eng::json::Value(), id);
    if (!r.ok()) c.text = r.text();
    return c;
}
}  // namespace

void Session::turn_capsule(u16 capsule) {
    if (capsule >= kCustomCapsuleFirst) {
        // CP-1, CP-3: a server's own capsule, paid in Coins with the player's approval.
        const CapsuleDef* cap = shop_config().capsule(capsule);
        if (!cap) {
            CapsuleResult c;
            c.capsule = capsule;
            c.text = "That capsule is not in the machine.";
            capsule_results.push_back(std::move(c));
            return;
        }
        Json what;
        what["kind"] = "custom_capsule";
        what["item"] = eng::str::format("capsule:%u", unsigned(cap->id));
        what["days"] = 0;
        approve_then(
            what, cap->coins, 1,
            [this, capsule](const std::string& approval) {
                Request m;
                m.op = u8(RequestOp::Capsule);
                m.a = capsule;
                m.approval = approval;
                send(m);
            },
            [this, capsule](std::string why) {
                CapsuleResult c;
                c.capsule = capsule;
                c.text = std::move(why);
                capsule_results.push_back(std::move(c));
            });
        return;
    }
    Json b;
    b["id"] = unsigned(capsule);
    tv_post("/v1/shop/capsule", b, [this, capsule](const TvReply& r) {   // CP-2: universal prizes only from TV's own
        capsule_results.push_back(capsule_result(capsule, r));
        if (r.ok()) changed(r.body);
    });
}

void Session::open_bag(u16 bag) {
    Json b;
    b["id"] = unsigned(bag);
    tv_post("/v1/shop/bag/open", b, [this, bag](const TvReply& r) {
        capsule_results.push_back(capsule_result(bag, r));
        if (r.ok()) changed(r.body);
    });
}

void Session::equip_item(u16 item_id) {
    if (item_id >= kFirstPackItem) {
        // A pack part (PK-6a/b): this server's own, worn here.
        Equip m;
        m.kind = u8(ShopKind::Item);
        m.item = item_id;
        send(m);
        return;
    }
    // Shown at once; TVAS's answer is the truth.
    if (std::erase(equipped, item_id) == 0) equipped.push_back(item_id);
    Json b;
    b["item"] = unsigned(item_id);
    tv_post("/v1/item/equip", b, [this](const TvReply& r) {
        if (!r.ok() || !r.body["ok"].as_bool()) notices.push_back({NoticeKind::Warning, r.ok() ? r.body["text"].str() : r.text()});
        if (r.ok()) changed(r.body);
    });
}

void Session::use_item(u16 item_id, u8 value, const std::string& text) {
    Json b;
    b["item"] = unsigned(item_id);
    b["value"] = unsigned(value);
    b["text"] = text;
    tv_post("/v1/item/use", b, [this](const TvReply& r) {
        shop_results.push_back({r.ok() && r.body["ok"].as_bool(), r.ok() ? eng::str::sanitize_line(r.body["text"].str(), 160) : r.text()});
        if (r.ok()) changed(r.body);
    });
}

void Session::recharge_sp() {
    tv_post("/v1/recharge", Json(), [this](const TvReply& r) {
        shop_results.push_back({r.ok() && r.body["ok"].as_bool(), r.ok() ? eng::str::sanitize_line(r.body["text"].str(), 160) : r.text()});
        if (r.ok()) changed(r.body);
    });
}

void Session::set_loadout(u8 force_id, const Loadout& weapons) {
    // Shown at once.
    profile.force = force_id;
    profile.loadout = weapons;
    // D16: the universal loadout is TVAS's; a slot filled with this server's own gun (and a pack
    // character worn here) is the server's, laid over it. The universal slot under one is left as it is.
    Json b;
    if (force_id < kFirstPackForce) b["force"] = unsigned(force_id);
    Json slots = Json::array();
    for (u16 w : weapons) {
        if (w == kNoWeapon) slots.push(std::string());
        else if (w >= kFirstPackWeapon) slots.push(Json());
        else if (const WeaponDef* d = weapon(w)) slots.push(d->code);
        else slots.push(Json());
    }
    b["weapons"] = std::move(slots);
    tv_post("/v1/loadout", b, [this, force_id, weapons](const TvReply& r) {
        if (r.ok()) changed(r.body);
        // A throwable asked for that the account service did not keep: one that keeps only the
        // first throwable's cell (TVAS before the kit of six) says so plainly, not "equipped".
        if (r.ok())
            for (size_t c = kFirstThrowCell + 1; c < kLoadoutSlots; ++c)
                if (weapons[c] != kNoWeapon && weapons[c] < kFirstPackWeapon && !in_kit(profile.loadout, weapons[c])) {
                    shop_results.push_back({false, "The account service kept only one throwable: it needs its update to carry three."});
                    break;
                }
        if (state_ != State::SignedIn) return;
        SetLoadout m;
        m.force = force_id;
        m.weapons = weapons;
        send(m);
        if (refresh_rev_) {
            refresh_sent_ = now_;
            Refresh f;
            f.rev = refresh_rev_;
            send(f);
        }
    });
}

// ── SFLegacy Staff (RL-1: global actions go from the game to TVAS, never through a server) ──

namespace {
StaffAccountCard card_of(const eng::json::Value& c) { return tvproto::staff_card(c); }
}  // namespace

void Session::global_staff_find(const std::string& name) {
    Json b;
    b["q"] = name;
    tv_post("/v1/staff/find", b, [this](const TvReply& r) {
        if (!r.ok()) {
            staff_results.push_back({false, r.text()});
            return;
        }
        const Json& list = r.body["results"];
        if (list.size() == 0) {
            staff_card.reset();
            staff_results.push_back({false, "No account by that name."});
            return;
        }
        staff_card = card_of(list[0]);
        if (list.size() > 1) {
            std::string names;
            for (const Json& c : list.elements()) names += (names.empty() ? "" : ", ") + c["code_name"].str(c["username"].str());
            staff_results.push_back({true, eng::str::format("%zu accounts start so: %s", list.size(), names.c_str())});
        }
    });
}

void Session::global_staff_action(const std::string& account, StaffOp op, u32 seconds, const std::string& reason, u8 role) {
    Json b;
    b["account"] = account;
    b["op"] = tvproto::staff_op(op);
    b["seconds"] = seconds;
    b["reason"] = reason;
    b["role"] = unsigned(role);
    tv_post("/v1/staff/action", b, [this](const TvReply& r) {
        staff_results.push_back({r.ok(), r.ok() ? r.body["text"].str() : r.text()});
        if (r.ok() && r.body["card"].is_object()) staff_card = card_of(r.body["card"]);
    });
}

void Session::global_staff_edit(const std::string& account, AccountField field, u32 value, const std::string& text) {
    const char* name = tvproto::account_field(field);
    if (!name) return;
    Json b;
    b["account"] = account;
    b["field"] = name;
    b["value"] = value;
    b["text"] = tvproto::account_field_key(field, text);
    tv_post("/v1/staff/edit", b, [this](const TvReply& r) {
        staff_results.push_back({r.ok(), r.ok() ? r.body["text"].str() : r.text()});
        if (r.ok() && r.body["card"].is_object()) staff_card = card_of(r.body["card"]);
    });
}

void Session::global_staff_reset_password(const std::string& account, const std::string& checked) {
    Json b;
    b["account"] = account;
    b["checked"] = checked;
    tv_post("/v1/staff/password", b, [this](const TvReply& r) {
        if (!r.ok()) {
            staff_results.push_back({false, r.text()});
            return;
        }
        // ID-12: shown to the staff member once, to pass on privately.
        staff_results.push_back({true, r.body["text"].str() + " Temporary password: " + r.body["temporary_password"].str()});
    });
}

void Session::global_staff_servers() {
    tvas_.get("/v1/staff/servers", [this](const TvReply& r) {
        if (!r.ok()) {
            staff_results.push_back({false, r.text()});
            return;
        }
        std::vector<StaffServer> out;
        for (const Json& s : r.body["servers"].elements()) {
            StaffServer o;
            o.id = s["id"].as_uint();
            o.name = s["name"].str();
            o.owner = s["owner"].str();
            o.address = s["address"].str() + ":" + std::to_string(s["port"].as_uint());
            o.note = s["reach_note"].str();
            o.tier = u8_of(s["tier"]), o.state = u8_of(s["state"]);
            o.players = u16_of(s["players"]);
            o.last_beat = s["last_beat"].as_uint();
            o.listed = s["reachable"].as_int() > 0 || o.tier == u8(Tier::Official);
            out.push_back(std::move(o));
        }
        staff_servers = std::move(out);
    });
}

void Session::global_staff_server(u64 server, const std::string& op, const std::string& reason, u32 value) {
    Json b;
    b["server"] = server;
    b["op"] = op;
    b["reason"] = reason;
    b["tier"] = value;
    b["on"] = value != 0;
    tv_post("/v1/staff/server", b, [this](const TvReply& r) {
        staff_results.push_back({r.ok(), r.ok() ? r.body["text"].str("Done.") : r.text()});
        if (r.ok()) global_staff_servers();
    });
}

void Session::global_staff_command(u64 server, const std::string& cmd, const std::string& text, u64 account, u16 room) {
    Json b;
    b["server"] = server;
    b["op"] = "command";
    b["cmd"] = cmd;
    b["text"] = text;
    b["account"] = account;
    b["room"] = unsigned(room);
    tv_post("/v1/staff/server", b, [this](const TvReply& r) { staff_results.push_back({r.ok(), r.ok() ? r.body["text"].str("Sent.") : r.text()}); });
}

}  // namespace lsf
