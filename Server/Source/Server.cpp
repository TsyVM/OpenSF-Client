#include "Server.hpp"

#include "Match.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"
#include "Game/Registry.hpp"
#include "Game/TvasJson.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <ctime>

namespace lsfs {

namespace {
u64 wall_now() { return u64(std::time(nullptr)); }

constexpr double kTicketSkew = 60.0;       // ID-4: TVAS's clock and ours may differ this much

// SL-3's words: what a server's staff write for everyone (the message of the day, a channel's
// name) may not pass for Team Vanilla's own, however it is spaced or dotted.
bool claims_official(std::string_view text) {
    std::string flat = eng::str::lower(text);
    std::erase_if(flat, [](char c) { return c == ' ' || c == '_' || c == '-' || c == '.'; });
    for (const char* w : {"official", "teamvanilla", "sflegacy"})
        if (flat.find(w) != std::string::npos) return true;
    return false;
}
}  // namespace

using namespace lsf::proto;
using eng::json::Value;

Seat* Room::seat_of(u32 peer) {
    for (Seat& s : seats)
        if (s.peer == peer) return &s;
    return nullptr;
}

int Room::count(Team t) const {
    int n = 0;
    for (const Seat& s : seats) n += s.team == t;
    return n;
}

Server::Server() = default;
Server::~Server() { stop_thread(); }

bool Server::start(const ServerOptions& options, std::string* why) {
    auto refuse = [&](std::string text) {
        LOG_ERROR("Server: %s", text.c_str());
        if (why) *why = std::move(text);
        return false;
    };
    options_ = options;
    // DS-1: no identity, no server. A test (or "This PC" before it has its id) says it needs none.
    if (options_.require_identity && (!options_.server_id || !options_.key.valid))
        return refuse("This server has no identity: run it once with --register <code> (from the UCP) to make its key and get its id.");
    if (!options_.tvas.empty()) {
        std::string bad;
        if (!eng::net::url_allowed(options_.tvas, &bad)) return refuse("Team Vanilla's address in server.cfg is not usable: " + bad);
    }
    if (options.loopback_only) eng::net::set_loopback_only(true);
    accounts_.open(options_.accounts_dir, options_.staff_file);
    load_shop();
    load_channels();
    load_games();
    load_recordings();
    if (!maps_.open(options_.data) && options_.require_data)
        return refuse(eng::str::narrow(options_.data.wstring()) +
                      " is not a Soldier Front data folder: copy Soldier Front's data folder into this server's folder, or name it as client_data "
                      "in server.cfg.");
    // §11: the packs, checked before anyone can download them (PK-4); a broken one stops the start.
    std::string pack_why;
    if (!content_.load(options_.packs_dir, maps_, &pack_why, options_.test_evil))
        return refuse("A pack failed its checks: " + pack_why);
    eng::net::NetLimits limits;
    limits.peers_per_address = options.loopback_only ? 64 : 16;
    net_.set_limits(limits);
    u32 bind = 0;
    if (!options_.bind_address.empty()) {
        auto a = eng::net::Address::resolve(options_.bind_address + ":0", 0);
        if (!a) return refuse("bind_address " + options_.bind_address + " is not an address of this machine.");
        bind = a->ip;
    }
    if (!net_.start(options.port, options.max_players + options.reserved_slots, kProtocolVersion, bind))
        return refuse(eng::str::format("Cannot open UDP port %u (another server running?)", unsigned(options.port)));
    net_.on_foreign = [this](const eng::net::Address& from, std::span<const u8> d) { on_foreign(from, d); };
    started_ = eng::time::now();
    if (options_.server_id && options_.key.valid) {
        tvas_.configure(options_.tvas, options_.server_id, options_.key, options_.recordings.empty() ? std::filesystem::path("reports") : options_.recordings / "pending");
        // A test's TVAS on this machine: its keys are trusted for this run (Game/Tvas.hpp).
        if (lsf::tvas::loopback(options_.tvas)) {
            eng::net::HttpRequest r;
            r.url = lsf::tvas::url(options_.tvas, "/v1/status");
            r.timeout = 5;
            const auto res = eng::net::http_request(r);
            Value st;
            if (res.ok() && eng::json::parse(res.body, st)) lsf::tvas::trust_test_keys(options_.tvas, st["keys"]);
        }
        heartbeat(true);
    }
    LOG_INFO("Server \"%s\" (id %llu) on UDP %u: %zu channels, %zu packs (%s)", options.name.c_str(), (unsigned long long)options_.server_id, unsigned(net_.port()),
             channels_.size(), content_.packs().size(), options_.no_progress ? "This PC: no progress" : options_.listed ? "listed" : "private");
    return true;
}

void Server::stop() {
    for (auto& [id, r] : rooms_) r->match.reset();
    rooms_.clear();
    if (tvas_.enabled() && !peers_.empty()) {
        Value list = Value::array();
        for (auto& [id, p] : peers_)
            if (p.account) {
                Value e;
                e["account"] = p.account->id;
                e["gone"] = true;
                list.push(std::move(e));
            }
        Value body;
        body["players"] = std::move(list);
        tvas_.post("/v1/server/presence", body, {});
        tvas_.drain(3.0);
    }
    peers_.clear();
    net_.stop();
    accounts_.save();
    content_.unload(maps_);
}

bool Server::start_thread(const ServerOptions& options, std::string* why) {
    if (thread_run_) return true;
    if (!start(options, why)) return false;
    thread_run_ = true;
    thread_ = std::thread([this] {
        while (thread_run_) {
            tick(eng::time::now());
            eng::time::sleep_precise(1.0 / 120.0);
        }
    });
    return true;
}

void Server::stop_thread() {
    if (!thread_run_) return;
    thread_run_ = false;
    if (thread_.joinable()) thread_.join();
    stop();
}

Peer* Server::peer(u32 id) {
    auto it = peers_.find(id);
    return it == peers_.end() ? nullptr : &it->second;
}

Peer* Server::peer_of(const Account* a) {
    if (!a) return nullptr;
    for (auto& [id, p] : peers_)
        if (p.account == a) return &p;
    return nullptr;
}

u64 Server::reward_now() const { return u64(i64(wall_now()) + clock_offset_.load()); }

void Server::notice(u32 peer_id, NoticeKind kind, std::string text) {
    Notice n;
    n.kind = u8(kind);
    n.text = std::move(text);
    send_msg(peer_id, n);
}

void Server::test_bots_before(u32 session, float distance) {
    std::lock_guard lock(mutex_);
    pending_bots_before_.emplace_back(session, distance);
}

void Server::test_bots_pace(u32 session, u8 pace) {
    std::lock_guard lock(mutex_);
    pending_bots_pace_.emplace_back(session, pace);
}

void Server::tick(double now) {
    now_ = now;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [session, distance] : pending_bots_before_)
            if (Peer* p = peer(session); p && p->room)
                if (Room* r = find_room(p->room); r && r->match) r->match->test_bots_before(session, distance);
        pending_bots_before_.clear();
        for (const auto& [session, pace] : pending_bots_pace_)
            if (Peer* p = peer(session); p && p->room)
                if (Room* r = find_room(p->room); r && r->match) r->match->test_bots_pace(pace);
        pending_bots_pace_.clear();
    }
    std::vector<eng::net::NetEvent> events;
    net_.poll(now, events);
    for (auto& e : events) {
        switch (e.type) {
            case eng::net::NetEvent::Type::Connected:
                peers_[e.peer].id = e.peer;
                peers_[e.peer].ip = net_.address(e.peer).ip;
                peers_[e.peer].connected_at = now;
                break;
            case eng::net::NetEvent::Type::Disconnected:
                on_disconnect(e.peer);
                break;
            case eng::net::NetEvent::Type::Message:
                on_message(e.peer, e.data, now);
                break;
        }
    }
    tvas_.poll();
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        Room& r = *it->second;
        ++it;
        if (!r.match) continue;
        r.match->tick(now);
        if (r.match->done()) {
            r.match.reset();
            match_over(r);
        }
    }
    tick_votes();
    tick_rooms();
    pump_replays();
    pump_downloads(now);
    // A connection that never finished joining is let go.
    std::vector<u32> stale;
    for (auto& [id, p] : peers_)
        if (p.step != JoinStep::Ready && now - p.connected_at > 300.0 && !p.download) stale.push_back(id);
    for (u32 id : stale) disconnect(id, "Joining took too long.");
    if (tvas_.enabled()) {
        if (now - last_beat_ >= options_.heartbeat_seconds) heartbeat();
        tick_presence();
        if (tier_ == u32(Tier::Official) && now - last_reach_ >= options_.reach_seconds) reach_checks();
        tvas_.retry_reports(now);
    }
    net_.flush(now);
    if (now - last_save_ > 20.0) {
        accounts_.save();
        prune_nonces();
        prune_recordings();
        last_save_ = now;
    }
}

bool Server::clan_war_channel(u8 id) const {
    const ChannelDef* c = channel(id);
    return c && c->kind == ChannelKind::ClanWar;
}

const ChannelDef* Server::channel(u8 id) const {
    for (const ChannelDef& c : channels_)
        if (c.id == id) return &c;
    return nullptr;
}

void Server::clan_room_changed(Room& r) {
    if (!r.settings.clan_battle || r.phase != RoomPhase::Waiting) return;
    if (r.count(Team::Blue) == 0) r.settings.blue_clan.clear();
    if (r.count(Team::Red) == 0 && r.count(Team::Blue) > 0) {
        r.settings.red_clan = r.settings.blue_clan;
        r.settings.blue_clan.clear();
        for (Seat& s : r.seats)
            if (s.team == Team::Blue) s.team = Team::Red;
    }
}

void Server::tick_rooms() {
    std::vector<u32> out;
    for (auto& [id, r] : rooms_) {
        int others = 0, ready = 0;
        if (r->phase == RoomPhase::Waiting)
            for (const Seat& s : r->seats)
                if (s.peer != r->host && s.team != Team::Observer) ++others, ready += s.state == SlotState::Ready;
        if (others == 0 || ready != others) {
            r->all_ready_since = 0;
            continue;
        }
        if (r->all_ready_since == 0 || r->all_ready_host != r->host) {
            r->all_ready_since = now_;
            r->all_ready_host = r->host;
            r->start_warned = false;
            notice(r->host, NoticeKind::Info, eng::str::format("Everyone is ready: start the game within %.0f seconds.", double(options_.host_start_seconds)));
            continue;
        }
        const double left = double(options_.host_start_seconds) - (now_ - r->all_ready_since);
        if (left <= 10.0 && !r->start_warned && options_.host_start_seconds > 12.0f) {
            r->start_warned = true;
            notice(r->host, NoticeKind::Warning, "Ten seconds to start the game, or the room is handed on.");
        }
        if (left <= 0) out.push_back(r->host);
    }
    const std::string why = eng::str::format("You have been kicked for failing to start the game within %.0f seconds after all Users were ready.",
                                             double(options_.host_start_seconds));
    for (u32 host : out)
        if (Peer* p = peer(host)) leave_room(*p, why);
}

void Server::on_disconnect(u32 id) {
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    Peer& p = it->second;
    if (p.room) leave_room(p, "");
    if (p.channel) leave_channel(p);
    Account* gone = p.account;
    if (gone && tvas_.enabled()) {
        Value e;
        e["account"] = gone->id;
        e["gone"] = true;
        Value body;
        body["players"].push(std::move(e));
        tvas_.post("/v1/server/presence", body, {});
    }
    peers_.erase(it);
    if (gone) {
        LOG_INFO("Server: %s (#%llu) left", gone->code_name.c_str(), (unsigned long long)gone->id);
        bool still = false;
        for (auto& [oid, o] : peers_) still |= o.account == gone;
        if (!still) accounts_.unload(gone->id);
    }
}

void Server::disconnect(u32 peer_id, const std::string& why) {
    Refused r;
    r.reason = why;
    send_msg(peer_id, r);
    net_.flush(now_);
    net_.disconnect(peer_id, why);
}

// ── Joining (§5.1) ─────────────────────────────────────────────────────────────

void Server::send_server_info(Peer& p) {
    ServerInfo i;
    i.server_id = options_.server_id;
    i.name = options_.name;
    i.motd = options_.motd;
    i.tier = u8(tier_);
    i.no_progress = options_.no_progress;
    i.min_rank = u8(std::clamp(options_.min_rank, 0, 255));
    i.max_rank = options_.max_rank >= rank_count() - 1 ? 0xFF : u8(options_.max_rank);
    i.min_kd = options_.min_kd;
    i.manifest = content_.manifest_hash();
    i.pack_bytes = u32(std::min<u64>(content_.total_bytes(), 0xFFFFFFFFull));
    i.build = kBuildNumber;
    u16 n = 0;
    for (auto& [id, o] : peers_) n += o.signed_in();
    i.players = n;
    i.slots = u16(std::min<u32>(options_.max_players, 0xFFFF));
    send_msg(p.id, i);
    p.step = JoinStep::Info;
}

void Server::handle_join(Peer& p, const Join& m) {
    lsf::tvas::Ticket t;
    std::string why;
    if (!lsf::tvas::read_ticket(m.ticket, t, &why)) return disconnect(p.id, "That ticket is not Team Vanilla's (" + why + ").");
    // ID-4: for this server only, not run out, never seen before.
    const double now = double(wall_now());
    if (t.server != options_.server_id) return disconnect(p.id, "That ticket is for another server.");
    if (double(t.expires) + kTicketSkew < now) return disconnect(p.id, "That ticket has run out: join again.");
    if (double(t.issued) > now + kTicketSkew) return disconnect(p.id, "That ticket is from the future: is this server's clock right?");
    if (nonces_.contains(t.nonce)) return disconnect(p.id, "That ticket has been used already.");
    nonces_[t.nonce] = t.expires;
    // A private server's password (§12.3). SFLegacy Staff pass it (§7.2); its own staff are known
    // only once their record is read, so they type it like anyone.
    if (!options_.password.empty() && m.password != options_.password && t.global_role < u8(proto::GlobalRole::Moderator))
        return disconnect(p.id, m.password.empty() ? "This server is private: it needs its password." : "That is not this server's password.");
    p.ticket = t;
    p.step = JoinStep::Ticket;
    // One connection per soldier (the original's CheckOverlap): the newer one wins.
    std::vector<u32> older;
    for (auto& [oid, other] : peers_)
        if (oid != p.id && ((other.account && other.account->id == t.account) || (other.step >= JoinStep::Ticket && other.ticket.account == t.account))) older.push_back(oid);
    for (u32 oid : older) {
        notice(oid, NoticeKind::Bad, "Your account joined this server somewhere else.");
        net_.disconnect(oid, "signed in elsewhere");
    }
    if (!tvas_.enabled()) return disconnect(p.id, "This server cannot reach Team Vanilla's services: try again shortly.");
    fetch_profile(p.id, t.account, true);
}

void Server::fetch_profile(u32 peer_id, u64 account, bool joining) {
    if (Peer* p = peer(peer_id)) p->profile_asked = now_;
    Value body;
    body["account"] = account;
    tvas_.post("/v1/server/profile", body, [this, peer_id, account, joining](const TvasReply& r) {
        Peer* p = peer(peer_id);
        if (!p) return;
        if (!r.ok()) {
            if (joining) return disconnect(peer_id, r.down() ? "Team Vanilla's services are down; try again shortly." : r.error);
            return;
        }
        if (r.body["id"].as_uint() != account) return;
        Account& a = accounts_.apply_profile(r.body);
        // EN-4: SFLegacy Staff carry their role in the ticket too; the higher wins until TVAS says otherwise.
        if (joining && p->ticket.global_role > u8(a.global_role)) a.global_role = proto::GlobalRole(p->ticket.global_role);
        if (r.body["banned"].as_bool() && joining) return disconnect(peer_id, "This account is banned by SFLegacy Staff.");
        p->account = &a;
        if (joining) return finish_join(*p);
        send_profile(*p);
        send_inventory(*p);
        if (Room* room = find_room(p->room)) send_room_state(*room);
        if (p->channel) broadcast_lobby_user(*p);
    });
}

std::string Server::gate_refusal(const Account& a) const {
    // GT-3: SFLegacy Staff pass every gate, a server's own staff pass its own.
    if (a.sflegacy() || a.server_role >= proto::Role::Moderator) return {};
    const int rank = rank_for_xp(a.xp);   // GT-2: the real rank, never the Fake Rank Mark
    if (rank < options_.min_rank) return eng::str::format("This server is for soldiers of %s and above.", rank_name(options_.min_rank));
    if (rank > options_.max_rank) return eng::str::format("This server is for soldiers up to %s.", rank_name(options_.max_rank));
    const float kd = a.deaths ? float(a.kills) / float(a.deaths) : float(a.kills);
    if (options_.min_kd > 0 && kd < options_.min_kd) return eng::str::format("This server needs a K/D of %.1f or more.", double(options_.min_kd));
    return {};
}

void Server::finish_join(Peer& p) {
    Account& a = *p.account;
    if (std::string banned = ban_refusal(a); !banned.empty() && !a.sflegacy()) return disconnect(p.id, banned);
    if (std::string gate = gate_refusal(a); !gate.empty()) return disconnect(p.id, gate);
    // GT-6: a full server keeps its last few seats for staff.
    u32 players = 0;
    for (auto& [id, o] : peers_) players += o.signed_in();
    if (players >= options_.max_players && !a.staff()) return disconnect(p.id, "This server is full.");
    p.step = JoinStep::Content;
    send_manifest(p);
}

void Server::welcome(Peer& p) {
    p.step = JoinStep::Ready;
    Account& a = *p.account;
    Welcome w;
    w.session = p.id;
    w.server_name = options_.name;
    w.motd = options_.motd;
    send_msg(p.id, w);
    send_shop(p);
    send_profile(p);
    send_inventory(p);
    send_channels(p);
    p.presence.clear();
    p.presence_sent = false;
    LOG_INFO("Server: %s (#%llu, %s) joined from %s", a.code_name.c_str(), (unsigned long long)a.id, global_role_name(a.global_role), platform_name(p.platform));
    follow_intent(p);
}

void Server::prune_nonces() {
    const double now = double(wall_now());
    for (auto it = nonces_.begin(); it != nonces_.end();)
        if (double(it->second) + kTicketSkew * 2 < now) it = nonces_.erase(it);
        else ++it;
}

// ── Messages ───────────────────────────────────────────────────────────────────

void Server::on_message(u32 id, std::span<const u8> data, double now) {
    Peer* pp = peer(id);
    if (!pp) {
        peers_[id].id = id;
        pp = peer(id);
    }
    Peer& p = *pp;
    const Msg kind = peek_id(data);

    if (kind == Msg::Hello) {
        Hello m;
        if (!decode(data, m)) return;
        if (m.version != kProtocolVersion) {
            // DS-6: whose copy is behind, never "a mismatch".
            Refused r;
            r.reason = m.version < kProtocolVersion ? "Your game is older than this server's: update the game." : "This server is out of date: it cannot be joined until its owner updates it.";
            send_msg(id, r);
            net_.disconnect(id, "version");
            return;
        }
        p.platform = m.platform, p.build = m.build, p.caps = m.caps;
        send_server_info(p);
        return;
    }
    if (p.step == JoinStep::Hello) return;
    if (kind == Msg::Join) {
        Join m;
        if (decode(data, m) && p.step == JoinStep::Info) handle_join(p, m);
        return;
    }
    if (p.step == JoinStep::Content) {
        if (kind == Msg::ContentRequest) {
            ContentRequest m;
            if (decode(data, m)) content_request(p, m);
        } else if (kind == Msg::Ready) {
            Ready m;
            if (!decode(data, m)) return;
            if (m.manifest != content_.manifest_hash()) return disconnect(id, "Your game mounted another set of packs than this server's.");
            welcome(p);
        }
        return;
    }
    if (!p.signed_in()) return;
    Account& a = *p.account;

    switch (kind) {
        case Msg::ListChannels:
            send_channels(p);
            break;
        case Msg::JoinChannel: {
            JoinChannel m;
            if (decode(data, m)) join_channel(p, m.id);
            break;
        }
        case Msg::LeaveChannel:
            if (p.room) leave_room(p, "");
            leave_channel(p);
            send_channels(p);
            break;
        case Msg::Chat: {
            Chat m;
            if (decode(data, m)) chat(p, m, now);
            break;
        }
        case Msg::Refresh: {
            Refresh m;
            // One a second from a soldier's game; a test on this machine asks as often as it checks.
            if (!decode(data, m) || (now - p.last_refresh < 1.0 && !options_.loopback_only)) break;
            p.last_refresh = now;
            if (m.rev == 0 || m.rev > a.rev) fetch_profile(id, a.id, false);
            break;
        }
        case Msg::CreateRoom: {
            CreateRoom m;
            if (!decode(data, m) || !p.channel || p.room) break;
            const ChannelDef* ch = channel(p.channel);
            m.settings.clan_battle = clan_war_channel(p.channel) && !a.clan.empty();
            m.settings.red_clan = m.settings.clan_battle ? a.clan : std::string();
            m.settings.blue_clan.clear();
            if (clan_war_channel(p.channel) && !m.settings.clan_battle) {
                notice(id, NoticeKind::Warning, "You are not a member of a clan.");
                break;
            }
            if (!sanitize(m.settings)) {
                notice(id, NoticeKind::Bad, "Those room settings are not valid.");
                break;
            }
            // §10.1: the channel's game types and maps, and the server's (its Games tab).
            if (!games_.takes_mode(m.settings.mode)) {
                notice(id, NoticeKind::Warning, std::string(mode_name(m.settings.mode)) + " is switched off on this server.");
                break;
            }
            if (ch && !ch->takes_mode(m.settings.mode)) {
                notice(id, NoticeKind::Warning, std::string(mode_name(m.settings.mode)) + " is not played in this channel.");
                break;
            }
            if (ch && !ch->maps.empty() && std::find(ch->maps.begin(), ch->maps.end(), eng::str::lower(m.settings.map)) == ch->maps.end()) m.settings.map = ch->maps.front();
            if (!games_.takes_map(m.settings.map)) m.settings.map = std::string(kAllRandom);
            if (!m.settings.password.empty() && !a.game_master()) {
                m.settings.password.clear();
                notice(id, NoticeKind::Warning, "Only staff can lock a room with a password; your room is open.");
            }
            create_room(p, m.settings);
            break;
        }
        case Msg::JoinRoom: {
            JoinRoom m;
            if (!decode(data, m) || !p.channel || p.room) break;
            Room* r = find_room(m.id);
            if (!r || r->channel != p.channel) {
                notice(id, NoticeKind::Bad, "That room is gone.");
                break;
            }
            if (!r->settings.password.empty() && r->settings.password != m.password && !a.sflegacy()) {
                notice(id, NoticeKind::Bad, "Wrong room password.");
                break;
            }
            std::string why;
            if (!join_room(p, *r, m.observer, &why)) notice(id, NoticeKind::Bad, why);
            break;
        }
        case Msg::QuickJoin: {
            if (!p.channel || p.room) break;
            Room* best = nullptr;
            for (auto& [rid, r] : rooms_) {
                if (r->channel != p.channel || r->closing || !r->settings.password.empty() || int(r->seats.size()) >= r->settings.max_players) continue;
                if (r->phase != RoomPhase::Waiting && !r->settings.free_join) continue;
                // BL-2: Quick Join skips a room with someone either way blocked.
                bool apart = false;
                for (const Seat& s : r->seats)
                    if (Peer* o = peer(s.peer); o && o->account && (a.blocks_with(o->account->id) || o->account->blocks_with(a.id))) apart = true;
                if (apart) continue;
                if (!best || r->seats.size() > best->seats.size()) best = r.get();
            }
            std::string why;
            if (!best) notice(id, NoticeKind::Info, "No open room to join right now: make one!");
            else if (!join_room(p, *best, false, &why)) notice(id, NoticeKind::Bad, why);
            break;
        }
        case Msg::LeaveRoom:
            if (p.room) leave_room(p, "");
            break;
        case Msg::ChangeRoom: {
            ChangeRoom m;
            Room* r = find_room(p.room);
            if (!decode(data, m) || !r || r->host != id || r->phase != RoomPhase::Waiting) break;
            m.settings.clan_battle = r->settings.clan_battle;
            m.settings.red_clan = r->settings.red_clan, m.settings.blue_clan = r->settings.blue_clan;
            if (!sanitize(m.settings)) break;
            if (!games_.takes_mode(m.settings.mode) && m.settings.mode != r->settings.mode) {
                notice(id, NoticeKind::Warning, std::string(mode_name(m.settings.mode)) + " is switched off on this server.");
                break;
            }
            if (const ChannelDef* ch = channel(r->channel); ch && !ch->takes_mode(m.settings.mode)) {
                notice(id, NoticeKind::Warning, std::string(mode_name(m.settings.mode)) + " is not played in this channel.");
                break;
            }
            if (!games_.takes_map(m.settings.map) && m.settings.map != r->settings.map) {
                notice(id, NoticeKind::Warning, "That map is switched off on this server.");
                break;
            }
            m.settings.password = r->settings.password;
            if (m.settings.max_players < r->seats.size()) m.settings.max_players = u8(r->seats.size());
            const bool teams_changed = mode_info(m.settings.mode).teams != mode_info(r->settings.mode).teams;
            r->settings = m.settings;
            if (teams_changed)
                for (Seat& s : r->seats) seat(*r, s, mode_info(r->settings.mode).teams ? (s.team == Team::None ? Team::Red : s.team) : Team::None);
            for (Seat& s : r->seats)
                if (s.peer != r->host) s.state = SlotState::Wait;
            send_room_state(*r);
            broadcast_room_summary(*r);
            break;
        }
        case Msg::SetTeam: {
            SetTeam m;
            Room* r = find_room(p.room);
            if (!decode(data, m) || !r || r->phase != RoomPhase::Waiting) break;
            Seat* s = r->seat_of(id);
            if (!s) break;
            const Team want = Team(std::min<u8>(m.team, u8(Team::Observer)));
            if (want == Team::Observer && !r->settings.observers) break;
            if (!mode_info(r->settings.mode).teams && want != Team::None && want != Team::Observer) break;
            if (r->settings.clan_battle && want != Team::Observer) {
                const bool red = eng::str::iequals(a.clan, r->settings.red_clan), blue = eng::str::iequals(a.clan, r->settings.blue_clan);
                if ((want == Team::Red && !red) || (want == Team::Blue && !blue) || a.clan.empty()) {
                    notice(id, NoticeKind::Warning, "In a Clan Battle you stand with your own clan.");
                    break;
                }
            }
            if (seat(*r, *s, want)) send_room_state(*r);
            break;
        }
        case Msg::SetReady: {
            SetReady m;
            Room* r = find_room(p.room);
            if (!decode(data, m) || !r || r->phase != RoomPhase::Waiting) break;
            if (Seat* s = r->seat_of(id); s && id != r->host) {
                s->state = m.ready ? SlotState::Ready : SlotState::Wait;
                send_room_state(*r);
            }
            break;
        }
        case Msg::SetLoadout: {
            // The slots filled with this server's own weapons (D16); base slots are the universal
            // loadout's, chosen at TVAS. A pack character worn here likewise.
            SetLoadout m;
            if (!decode(data, m)) break;
            for (size_t slot = 0; slot < kLoadoutSlots; ++slot) {
                const u16 w = m.weapons[slot];
                if (w >= kFirstPackWeapon && w != kNoWeapon) {
                    const WeaponDef* def = weapon(w);
                    if (def && fits_cell(def->slot, slot) && a.owns_weapon(w)) a.override_slots[slot] = def->code;
                } else {
                    a.override_slots[slot].clear();
                }
            }
            if (m.force >= kFirstPackForce && m.force != kNoForce) {
                if (const PackForce* f = registry::force(m.force); f && a.owns_force(m.force)) a.override_force = f->code;
            } else {
                a.override_force.clear();
            }
            a.apply_overrides();
            accounts_.mark_dirty(a.id);
            send_profile(p);
            if (Room* r = find_room(p.room)) send_room_state(*r);
            break;
        }
        case Msg::Equip: {
            // A pack part (PK-6a/b): this server's own. Base parts are worn at TVAS.
            Equip m;
            if (!decode(data, m) || m.kind != u8(ShopKind::Item) || m.item < kFirstPackItem) break;
            const PackItem* d = registry::item(m.item);
            if (!d || !a.find_item(m.item)) break;
            auto& parts = a.custom_parts;
            if (std::erase(parts, d->code) == 0) {
                std::erase_if(parts, [&](const std::string& code) {
                    const PackItem* o = registry::item_by_code(code);
                    return o && o->def.force == d->def.force && o->def.slot == d->def.slot;
                });
                parts.push_back(d->code);
            }
            std::erase_if(a.equipped, [](u16 i) { return i >= kFirstPackItem; });
            for (const std::string& code : parts)
                if (const PackItem* i = registry::item_by_code(code)) a.equipped.push_back(i->def.id);
            accounts_.mark_dirty(a.id);
            send_inventory(p);
            if (Room* r = find_room(p.room)) send_room_state(*r);
            break;
        }
        case Msg::KickPlayer: {
            KickPlayer m;
            Room* r = find_room(p.room);
            if (!decode(data, m) || !r || r->host != id || m.player == id) break;
            if (Peer* victim = peer(m.player); victim && victim->room == r->id) {
                // RL-2: nobody but their own puts SFLegacy Staff out.
                if (victim->account && victim->account->sflegacy() && !a.sflegacy()) break;
                if (victim->account) r->barred[victim->account->id] = now_ + 600.0;
                leave_room(*victim, "The host removed you from the room.");
            }
            break;
        }
        case Msg::StartMatch: {
            Room* r = find_room(p.room);
            if (!r || r->host != id || r->phase != RoomPhase::Waiting || r->closing) break;
            const ModeInfo& mi = mode_info(r->settings.mode);
            bool all_ready = true;
            for (const Seat& s : r->seats)
                if (s.peer != r->host && s.team != Team::Observer && s.state != SlotState::Ready) all_ready = false;
            if (!all_ready) {
                notice(id, NoticeKind::Warning, "Everyone must be READY before the game can start.");
                break;
            }
            if (r->settings.clan_battle) {
                const int red = r->count(Team::Red), blue = r->count(Team::Blue);
                if (r->settings.blue_clan.empty() || blue == 0) {
                    notice(id, NoticeKind::Warning, "A Clan Battle needs another clan on the blue side.");
                    break;
                }
                if (red != blue) {
                    notice(id, NoticeKind::Warning, "Unbalanced clan.");
                    break;
                }
                if (red < options_.clan_battle_min) {
                    notice(id, NoticeKind::Warning, eng::str::format("%d members per each teams are required for the team battle or the clan battle.", options_.clan_battle_min));
                    break;
                }
            }
            if (mi.teams && r->settings.mode != Mode::Training && !(r->settings.mode == Mode::Horror || r->settings.mode == Mode::Horror2) && r->settings.bots == 0 &&
                (r->count(Team::Red) == 0 || r->count(Team::Blue) == 0) && r->seats.size() > 1) {
                notice(id, NoticeKind::Warning, "Both teams need at least one soldier.");
                break;
            }
            // A game type switched off since the room was made is not started; a map switched off is drawn again.
            if (!games_.takes_mode(r->settings.mode)) {
                notice(id, NoticeKind::Warning, std::string(mode_name(r->settings.mode)) + " is switched off on this server: pick another game type.");
                break;
            }
            eng::Rng pick{u64(now_ * 1000.0) ^ (u64(r->id) << 32)};
            const ChannelDef* ch = channel(r->channel);
            static const std::vector<std::string> kEveryMap;
            std::string map = maps_.pick_map(r->settings.mode, r->settings.map, pick, games_, ch ? ch->maps : kEveryMap);
            if (map.empty()) {
                notice(id, NoticeKind::Warning, std::string("No map here plays ") + mode_name(r->settings.mode) + ".");
                break;
            }
            start_match(*r, map);
            break;
        }
        case Msg::ChannelEdit: {
            ChannelEdit m;
            if (decode(data, m)) channel_edit(p, m);
            break;
        }
        case Msg::GamesEdit: {
            GamesEdit m;
            if (decode(data, m)) games_edit(p, m);
            break;
        }
        case Msg::MotdEdit: {
            // ML-8: the Owner and Server Admins edit it, through the server-name filter, 512 at most.
            MotdEdit m;
            if (!decode(data, m)) break;
            if (!a.admin()) return staff_result(id, false, "Only the Owner and Server Admins change the message of the day.");
            const std::string text = eng::str::sanitize_line(m.motd, 512);
            if (claims_official(text) && !a.sflegacy()) return staff_result(id, false, "The message of the day may not claim to be official.");
            options_.motd = text;
            if (options_.on_motd) options_.on_motd(text);
            staff_logged(a, nullptr, "motd", "", text);
            heartbeat(true);
            return staff_result(id, true, "The message of the day is changed.");
        }
        case Msg::ChatPrefs:
        case Msg::RoomInvite:
        case Msg::RoomInviteAnswer:
            handle_social(p, kind, data);
            break;
        case Msg::Buy:
        case Msg::SendGift:
        case Msg::Request:
        case Msg::StaffShopSave:
            handle_shop(p, kind, data);
            break;
        case Msg::ReportPlayer:
        case Msg::CallVote:
        case Msg::CastVote:
        case Msg::StaffListReports:
        case Msg::StaffResolveReport:
        case Msg::StaffAction:
        case Msg::StaffFindAccount:
        case Msg::StaffEditAccount:
        case Msg::StaffListRecordings:
        case Msg::StaffRequestReplay:
            handle_staff(p, kind, data);
            break;
        default:
            if (Room* r = find_room(p.room); r && r->match) r->match->on_message(id, data, now);
            break;
    }
}

void Server::apply_profile(const Value& me) {
    if (!me.is_object() || !me["id"].as_uint() || !accounts_.find(me["id"].as_uint())) return;   // only soldiers who are here
    Account& a = accounts_.apply_profile(me);
    if (Peer* p = peer_of(&a)) {
        send_profile(*p);
        send_inventory(*p);
    }
}

void Server::send_profile(Peer& p) {
    if (!p.account) return;
    send_msg(p.id, p.account->profile());
}

void Server::send_inventory(Peer& p) {
    if (!p.account) return;
    Account& a = *p.account;
    InventoryMsg inv;
    const u64 now = reward_now();
    auto left = [&](u64 expires) { return expires == 0 ? OwnedItem::kForGood : u32(std::min<u64>(expires > now ? expires - now : 0, 0xFFFFFFFEu)); };
    for (u16 w : a.weapons)
        if (weapon(w)) inv.weapons.push_back(w);
    for (const ForceDef& f : forces())
        if (a.owns_force(f.id)) inv.forces.push_back(f.id);
    for (u8 f : a.forces)
        if (f >= kFirstPackForce) inv.forces.push_back(f);
    for (const auto& [id, g] : a.guns) {
        const WeaponDef* w = weapon(id);
        if (!w || !a.owns_weapon(id)) continue;
        OwnedGun o;
        o.id = id;
        o.seconds_left = left(g.expires);
        o.durability = a.durability_of(id);
        inv.guns.push_back(o);
    }
    for (const Account::Custom& c : a.custom)
        if (const WeaponDef* w = registry::weapon_by_code(c.code); w && c.expires) {
            OwnedGun o;
            o.id = w->id;
            o.seconds_left = left(c.expires);
            inv.guns.push_back(o);
        }
    for (const auto& [id, until] : a.force_until) inv.force_time.push_back({u16(id), left(until), 0});
    for (const Account::Item& i : a.sprays) inv.sprays.push_back({i.id, left(i.expires), 0});
    inv.spray = a.spray;
    inv.coins = a.coins;
    for (const Account::Gift& g : a.gifts) inv.gifts.push_back({g.id, g.kind, g.item, g.days, g.from});
    inv.horror = a.horror_items;
    for (const auto& [id, n] : a.bags)
        if (n) inv.bags.push_back({id, OwnedItem::kForGood, n});
    for (const Account::Item& i : a.items) inv.items.push_back({i.id, i.expires == 0 ? OwnedItem::kForGood : left(i.expires), i.uses});
    inv.equipped = a.equipped;
    send_msg(p.id, inv);
}

// ── Channels (§10) ─────────────────────────────────────────────────────────────

namespace {
ChannelKind kind_from_text(std::string_view text) {
    const std::string k = eng::str::lower(eng::str::trim(text));
    for (int i = 0; i < int(ChannelKind::Count); ++i) {
        std::string name = eng::str::lower(channel_kind_name(ChannelKind(i)));
        std::string flat = name;
        std::erase_if(flat, [](char c) { return c == ' ' || c == '&'; });
        std::string kf = k;
        std::erase_if(kf, [](char c) { return c == ' ' || c == '&' || c == '_'; });
        if (k == name || kf == flat || (i == int(ChannelKind::Training) && kf == "training") || (i == int(ChannelKind::Officers) && kf == "officers") ||
            (i == int(ChannelKind::Sharpshooter) && kf == "sharpshooter") || (i == int(ChannelKind::Free) && kf == "free"))
            return ChannelKind(i);
    }
    return ChannelKind::Free;
}
}  // namespace

void Server::load_channels() {
    channels_.clear();
    auto text = eng::fs::read_text_file(options_.channels_file);
    eng::ConfigFile cfg;
    std::string why;
    if (text && eng::ConfigFile::parse(*text, cfg, &why)) {
        for (const eng::ConfigSection* s : cfg.all("channel")) {
            ChannelDef c;
            c.id = u8(std::clamp(s->get_int("id"), 0, 255));
            c.name = s->get_string("name");
            c.kind = kind_from_text(s->get_string("kind", "free"));
            c.min_rank = s->get_int("min_rank", 0);
            c.max_rank = s->get_int("max_rank", rank_count() - 1);
            c.min_kd = s->get_float("min_kd", 0);
            c.capacity = u16(std::clamp(s->get_int("capacity", 300), 1, 4096));
            for (std::string_view m : eng::str::split(s->get("modes"), ','))
                for (int k = 0; k < int(Mode::Count); ++k)
                    if (eng::str::iequals(eng::str::trim(m), mode_info(Mode(k)).short_name) || eng::str::iequals(eng::str::trim(m), mode_name(Mode(k)))) c.modes |= u16(1u << k);
            for (std::string_view m : eng::str::split(s->get("maps"), ',')) c.maps.emplace_back(eng::str::trim(m));
            c.hidden = s->get_bool("hidden", false);
            c.order = u8(std::clamp(s->get_int("order", c.id), 0, 255));
            std::string bad;
            if (!sanitize(c, options_.max_players, &bad)) {
                LOG_ERROR("Channels: [channel] %u in %s: %s (left out)", unsigned(c.id), eng::str::narrow(options_.channels_file.wstring()).c_str(), bad.c_str());
                continue;
            }
            if (std::any_of(channels_.begin(), channels_.end(), [&](const ChannelDef& o) { return o.id == c.id; })) continue;
            if (channels_.size() < size_t(kMaxChannels)) channels_.push_back(std::move(c));
        }
        channels_version_ = u32(std::max(1, cfg.root().get_int("version", 1)));
    }
    if (channels_.empty()) {
        // CH-3: a missing or broken channels.cfg falls back to the original thirteen, and says so.
        if (text) LOG_ERROR("Channels: %s has no usable channels%s: the original thirteen are used", eng::str::narrow(options_.channels_file.wstring()).c_str(),
                            why.empty() ? "" : (" (" + why + ")").c_str());
        else LOG_INFO("Channels: %s does not exist yet: the original thirteen are used and written there", eng::str::narrow(options_.channels_file.wstring()).c_str());
        channels_ = default_channels();
        for (ChannelDef& c : channels_) c.capacity = u16(std::min<u32>(c.capacity, options_.max_players));
        if (!text) save_channels();
    }
    std::stable_sort(channels_.begin(), channels_.end(), [](const ChannelDef& a, const ChannelDef& b) { return a.order < b.order; });
}

bool write_channels_file(const std::filesystem::path& file, const std::vector<ChannelDef>& channels, u32 version) {
    eng::ConfigFile cfg;
    cfg.section("").set("version", std::to_string(version));
    for (const ChannelDef& c : channels) {
        eng::ConfigSection s;
        s.name = "channel";
        s.set("id", std::to_string(c.id));
        s.set("name", c.name);
        s.set("kind", channel_kind_name(c.kind));
        s.set("min_rank", std::to_string(c.min_rank));
        s.set("max_rank", std::to_string(c.max_rank));
        if (c.min_kd > 0) s.set("min_kd", eng::str::format("%.2f", double(c.min_kd)));
        s.set("capacity", std::to_string(c.capacity));
        std::string modes;
        for (int k = 0; k < int(Mode::Count); ++k)
            if (c.modes & (1u << k)) modes += (modes.empty() ? "" : ",") + std::string(mode_info(Mode(k)).short_name);
        if (!modes.empty()) s.set("modes", modes);
        std::string maps;
        for (const std::string& m : c.maps) maps += (maps.empty() ? "" : ",") + m;
        if (!maps.empty()) s.set("maps", maps);
        if (c.hidden) s.set("hidden", "true");
        s.set("order", std::to_string(c.order));
        cfg.sections.push_back(std::move(s));
    }
    return eng::fs::write_text_file(file, "# This server's channels (Docs/UniversalServerDeploy.md §10.1). The Owner edits them in the game (F9, Channels).\n"
                                          "# kind: Free, Training, Officers, Sharpshooter, Clan War, Scrim, Event, Staff. Ranks are inclusive, 0 (Private) to 74.\n" +
                                              cfg.serialize());
}

bool Server::save_channels() {
    if (options_.channels_file.empty()) return true;
    return write_channels_file(options_.channels_file, channels_, channels_version_);
}

void Server::send_channels(Peer& p) {
    ChannelList list;
    list.server_name = options_.name;
    list.version = channels_version_;
    list.editable = p.account && p.account->admin();
    const int rank = p.account ? rank_for_xp(p.account->xp) : 0;
    const float kd = p.account && p.account->deaths ? float(p.account->kills) / float(p.account->deaths) : (p.account ? float(p.account->kills) : 0.0f);
    const bool staff = p.account && p.account->staff();
    for (const ChannelDef& c : channels_) {
        ChannelInfo ci;
        ci.id = c.id;
        ci.name = c.name;
        ci.kind = u8(c.kind);
        ci.limit = c.limit_text();
        ci.capacity = c.capacity;
        for (const auto& [id, other] : peers_) ci.players += other.channel == c.id;
        // GT-3: staff pass their own server's gates; a Staff channel is theirs alone.
        ci.allowed = (staff || channel_allows(c, rank, kd)) && (c.kind != ChannelKind::ClanWar || (p.account && !p.account->clan.empty())) &&
                     (c.kind != ChannelKind::Staff || staff);
        if (c.hidden && !ci.allowed && !list.editable) continue;
        ci.min_rank = u8(c.min_rank), ci.max_rank = u8(c.max_rank), ci.min_kd = c.min_kd, ci.modes = c.modes, ci.hidden = c.hidden, ci.order = c.order;
        if (list.editable) ci.maps = c.maps;
        list.channels.push_back(std::move(ci));
    }
    send_msg(p.id, list);
    send_games(p);
}

void Server::channels_changed() {
    ++channels_version_;
    save_channels();
    for (auto& [id, p] : peers_)
        if (p.signed_in()) send_channels(p);
}

void Server::channel_edit(Peer& p, const ChannelEdit& m) {
    Account& a = *p.account;
    if (!a.admin()) return staff_result(p.id, false, "Only the Owner and Server Admins edit channels.");
    // CH-6: an edit made on an older list is refused rather than written over another's.
    if (m.version != channels_version_) return staff_result(p.id, false, "The channels were changed by someone else meanwhile: look again and redo it.");
    const u8 id = m.channel.id;
    auto it = std::find_if(channels_.begin(), channels_.end(), [&](const ChannelDef& c) { return c.id == id; });
    switch (ChannelOp(m.op)) {
        case ChannelOp::Save: {
            ChannelDef c;
            c.id = id;
            c.name = m.channel.name;
            c.kind = ChannelKind(std::min<u8>(m.channel.kind, u8(ChannelKind::Count) - 1));
            c.min_rank = m.channel.min_rank, c.max_rank = m.channel.max_rank, c.min_kd = m.channel.min_kd;
            c.capacity = m.channel.capacity, c.modes = m.channel.modes, c.maps = m.channel.maps, c.hidden = m.channel.hidden;
            c.order = it != channels_.end() && m.channel.order == 0 ? it->order : m.channel.order;
            if (claims_official(c.name) && !a.sflegacy()) return staff_result(p.id, false, "A channel's name may not claim to be official (SL-3).");
            std::string why;
            if (!sanitize(c, options_.max_players, &why)) return staff_result(p.id, false, why);
            if (it == channels_.end()) {
                if (channels_.size() >= size_t(kMaxChannels)) return staff_result(p.id, false, "A server has at most 64 channels.");
                channels_.push_back(c);
            } else {
                // CH-4: soldiers now outside its gate stay until they leave; rooms keep playing.
                *it = c;
            }
            std::stable_sort(channels_.begin(), channels_.end(), [](const ChannelDef& x, const ChannelDef& y) { return x.order < y.order; });
            staff_logged(a, nullptr, "channel.save", "", c.name);
            channels_changed();
            return staff_result(p.id, true, "Channel " + c.name + " saved.");
        }
        case ChannelOp::Delete: {
            if (it == channels_.end()) return staff_result(p.id, false, "That channel is gone.");
            if (channels_.size() <= 1) return staff_result(p.id, false, "A server keeps at least one channel.");
            const std::string name = it->name;
            channels_.erase(it);
            // CH-5: its lobby is told and sent back to the list; its rooms play their matches out.
            for (auto& [rid, r] : rooms_)
                if (r->channel == id) r->closing = true;
            for (auto& [pid, o] : peers_) {
                if (o.channel != id) continue;
                if (o.room)
                    if (Room* r = find_room(o.room); r && r->match) continue;   // in a match: out when it is over
                if (o.room) leave_room(o, "The channel was closed.");
                o.channel = 0;
                notice(pid, NoticeKind::Warning, "The channel " + name + " was closed by the server's staff.");
            }
            std::vector<u16> empty;
            for (auto& [rid, r] : rooms_)
                if (r->channel == id && !r->match) empty.push_back(rid);
            for (u16 rid : empty)
                if (Room* r = find_room(rid)) remove_room(*r);
            staff_logged(a, nullptr, "channel.delete", "", name);
            channels_changed();
            return staff_result(p.id, true, "Channel " + name + " deleted.");
        }
        case ChannelOp::Move: {
            // `order`: the place it moves to, 1 the top. The list is numbered again from 1, so a
            // place always means the same thing whatever orders the file had.
            if (it == channels_.end()) return staff_result(p.id, false, "That channel is gone.");
            const ChannelDef moved = *it;
            channels_.erase(it);
            const size_t at = std::min<size_t>(m.channel.order ? size_t(m.channel.order) - 1 : 0, channels_.size());
            channels_.insert(channels_.begin() + std::ptrdiff_t(at), moved);
            for (size_t i = 0; i < channels_.size(); ++i) channels_[i].order = u8(i + 1);
            staff_logged(a, nullptr, "channel.move", "", eng::str::format("%s to %zu", moved.name.c_str(), at + 1));
            channels_changed();
            return staff_result(p.id, true, eng::str::format("%s is now number %zu.", moved.name.c_str(), at + 1));
        }
    }
}

// ── What the server plays ──────────────────────────────────────────────────────

std::filesystem::path Server::games_file() const {
    if (options_.channels_file.empty()) return {};
    return options_.channels_file.parent_path() / "games.cfg";
}

void Server::load_games() {
    games_ = {};
    const std::filesystem::path file = games_file();
    if (file.empty()) return;
    auto text = eng::fs::read_text_file(file);
    if (!text) return;
    eng::ConfigFile cfg;
    std::string why;
    if (!eng::ConfigFile::parse(*text, cfg, &why)) {
        LOG_ERROR("Games: %s: %s (every game type and map is played)", eng::str::narrow(file.wstring()).c_str(), why.c_str());
        return;
    }
    for (std::string_view m : eng::str::split(cfg.root().get("modes_off"), ','))
        for (int k = 0; k < int(Mode::Count); ++k)
            if (eng::str::iequals(eng::str::trim(m), mode_info(Mode(k)).short_name) || eng::str::iequals(eng::str::trim(m), mode_name(Mode(k)))) games_.modes_off |= u16(1u << k);
    for (std::string_view m : eng::str::split(cfg.root().get("maps_off"), ','))
        if (!eng::str::trim(m).empty()) games_.maps_off.emplace_back(eng::str::trim(m));
    sanitize(games_);
    if (games_.modes_off || !games_.maps_off.empty())
        LOG_INFO("Games: %d game type(s) and %zu map(s) switched off", std::popcount(unsigned(games_.modes_off)), games_.maps_off.size());
}

bool Server::save_games() {
    const std::filesystem::path file = games_file();
    if (file.empty()) return true;
    eng::ConfigFile cfg;
    std::string modes, maps;
    for (int k = 0; k < int(Mode::Count); ++k)
        if (!games_.takes_mode(Mode(k))) modes += (modes.empty() ? "" : ",") + std::string(mode_info(Mode(k)).short_name);
    for (const std::string& m : games_.maps_off) maps += (maps.empty() ? "" : ",") + m;
    cfg.section("").set("modes_off", modes);
    cfg.section("").set("maps_off", maps);
    return eng::fs::write_text_file(file, "# What this server plays: its Game Masters switch game types and maps off in the game (F9, Games).\n"
                                          "# modes_off: TB, TDM, SB, SNP, CTC, CPT, HOR, TRN, TS, OCC, HR2, PIR (or the full names). maps_off: level ids.\n" +
                                              cfg.serialize());
}

void Server::send_games(Peer& p) {
    GamesState m;
    m.games = games_;
    m.editable = p.account && p.account->game_master();
    send_msg(p.id, m);
}

void Server::games_edit(Peer& p, const GamesEdit& m) {
    Account& a = *p.account;
    if (!a.game_master()) return staff_result(p.id, false, "Only the server's Game Masters choose what it plays.");
    ServerGames g = m.games;
    sanitize(g);
    if (g.modes_off == u16((1u << unsigned(Mode::Count)) - 1)) return staff_result(p.id, false, "Leave at least one game type on.");
    if (g == games_) return staff_result(p.id, true, "Nothing changed.");
    games_ = std::move(g);
    if (!save_games()) LOG_ERROR("Games: %s cannot be written", eng::str::narrow(games_file().wstring()).c_str());
    std::string modes;
    for (int k = 0; k < int(Mode::Count); ++k)
        if (!games_.takes_mode(Mode(k))) modes += (modes.empty() ? "" : ", ") + std::string(mode_name(Mode(k)));
    staff_logged(a, nullptr, "games.save", "", eng::str::format("off: %s; %zu map(s)", modes.empty() ? "no game types" : modes.c_str(), games_.maps_off.size()));
    // Rooms already made keep their game; a new one, a change or a start is held to these.
    for (auto& [id, o] : peers_)
        if (o.signed_in()) send_games(o);
    return staff_result(p.id, true, "What this server plays is saved.");
}

proto::LobbyUserInfo Server::lobby_info(const Peer& p) const {
    LobbyUserInfo u;
    u.id = p.id;
    u.name = p.account ? p.account->code_name : "";
    u.xp = p.account ? p.account->shown_xp(reward_now()) : 0;
    u.room = p.room;
    u.clan = p.account ? p.account->clan : "";
    u.name_colour = p.account ? p.account->name_colour : 0;
    u.clan_colour = p.account ? p.account->clan_colour : 0;
    return u;
}

proto::RoomSummary Server::summary(const Room& r) const {
    RoomSummary s;
    s.id = r.id;
    s.title = r.settings.title;
    s.mode = u8(r.settings.mode);
    s.map = r.settings.map;
    auto hit = peers_.find(r.host);
    s.host = hit != peers_.end() && hit->second.account ? hit->second.account->code_name : "";
    s.players = u8(r.seats.size());
    s.max_players = r.settings.max_players;
    s.phase = u8(r.phase);
    s.locked = !r.settings.password.empty();
    s.free_join = r.settings.free_join;
    s.observers = r.settings.observers;
    s.time_of_day = u8(r.settings.time_of_day);
    s.no_snipers = r.settings.no_snipers;
    s.clan_battle = r.settings.clan_battle;
    s.red_clan = r.settings.red_clan, s.blue_clan = r.settings.blue_clan;
    return s;
}

void Server::join_channel(Peer& p, u8 id) {
    const ChannelDef* c = channel(id);
    if (!c) return;
    std::string why;
    const float kd = p.account->deaths ? float(p.account->kills) / float(p.account->deaths) : float(p.account->kills);
    const bool staff = p.account->staff();
    if (!staff && !channel_allows(*c, rank_for_xp(p.account->xp), kd, &why)) return notice(p.id, NoticeKind::Warning, why);
    if (c->kind == ChannelKind::ClanWar && p.account->clan.empty()) return notice(p.id, NoticeKind::Warning, "Only clan members are allowed to enter.");
    if (c->kind == ChannelKind::Staff && !staff) return notice(p.id, NoticeKind::Warning, "That channel is for the server's staff.");
    u32 inside = 0;
    for (auto& [oid, o] : peers_) inside += o.channel == id;
    if (inside >= c->capacity && !staff) return notice(p.id, NoticeKind::Warning, c->name + " is full.");
    if (p.room) leave_room(p, "");
    if (p.channel) leave_channel(p);
    p.channel = id;
    ChannelJoined j;
    j.id = id;
    j.name = c->name;
    send_msg(p.id, j);
    send_lobby(p);
    broadcast_lobby_user(p);
}

void Server::leave_channel(Peer& p) {
    if (!p.channel) return;
    const u8 was = p.channel;
    p.channel = 0;
    LobbyUserLeft left;
    left.id = p.id;
    for (auto& [id, other] : peers_)
        if (other.channel == was && id != p.id) send_msg(id, left);
}

void Server::send_lobby(Peer& p) {
    LobbyState st;
    for (auto& [id, r] : rooms_)
        if (r->channel == p.channel) st.rooms.push_back(summary(*r));
    for (auto& [id, other] : peers_) {
        if (other.channel != p.channel || !other.named()) continue;
        // BL-9: a soldier who blocked you is hidden from you in a lobby's list.
        if (p.account && other.account && other.account != p.account && other.account->blocks_with(p.account->id) && !p.account->staff()) continue;
        st.users.push_back(lobby_info(other));
    }
    send_msg(p.id, st);
}

void Server::broadcast_lobby_user(Peer& p) {
    LobbyUser u;
    u.user = lobby_info(p);
    for (auto& [id, other] : peers_) {
        if (other.channel != p.channel || id == p.id) continue;
        if (other.account && p.account && p.account->blocks_with(other.account->id) && !other.account->staff()) continue;
        send_msg(id, u);
    }
}

void Server::broadcast_room_summary(Room& r) {
    RoomSummaryMsg m;
    m.room = summary(r);
    for (auto& [id, other] : peers_)
        if (other.channel == r.channel) send_msg(id, m);
}

Room* Server::find_room(u16 id) {
    auto it = rooms_.find(id);
    return it == rooms_.end() ? nullptr : it->second.get();
}

bool Server::seat(Room& r, Seat& s, Team want) {
    auto taken = [&](Team t, u8 slot) {
        for (const Seat& o : r.seats)
            if (&o != &s && o.team == t && o.slot == slot) return true;
        return false;
    };
    const int per_team = want == Team::None ? kMaxRoomPlayers : (want == Team::Observer ? 8 : kTeamSlots);
    for (u8 slot = 0; slot < per_team; ++slot)
        if (!taken(want, slot)) {
            s.team = want;
            s.slot = slot;
            return true;
        }
    return false;
}

u16 Server::start_bot_room(RoomSettings s) {
    if (!sanitize(s)) return 0;
    eng::Rng pick{u64(now_ * 1000.0) ^ 0xB075ull};
    const std::string map = maps_.pick_map(s.mode, s.map, pick, games_);
    if (map.empty()) return 0;
    u16 id = 1;
    while (rooms_.contains(id)) ++id;
    auto r = std::make_unique<Room>();
    r->id = id;
    r->settings = std::move(s);
    Room* raw = r.get();
    rooms_[id] = std::move(r);
    start_match(*raw, map);
    return id;
}

bool Server::room_playing(u16 id) {
    Room* r = find_room(id);
    return r && r->match;
}

Room* Server::create_room(Peer& p, RoomSettings s) {
    u16 id = 1;
    while (rooms_.contains(id)) ++id;
    auto r = std::make_unique<Room>();
    r->id = id;
    r->channel = p.channel;
    r->settings = std::move(s);
    r->host = p.id;
    Room* raw = r.get();
    rooms_[id] = std::move(r);
    std::string why;
    join_room(p, *raw, false, &why);
    LOG_INFO("Server: %s made room %u \"%s\" (%s on %s)", p.account->code_name.c_str(), unsigned(id), raw->settings.title.c_str(), mode_name(raw->settings.mode),
             raw->settings.map.c_str());
    return raw;
}

bool Server::join_room(Peer& p, Room& r, bool observer, std::string* why) {
    Account* a = p.account;
    const bool staff_pass = a && a->sflegacy();
    if (a && !staff_pass)
        if (auto it = r.barred.find(a->id); it != r.barred.end() && it->second > now_) {
            *why = eng::str::format("You were removed from that room: you can join it again in %.0f minute(s).", std::ceil((it->second - now_) / 60.0));
            return false;
        }
    if (r.closing) {
        *why = "That room is closing.";
        return false;
    }
    // BL-2, BL-7: two soldiers with a block between them never share a room, either way -- but two of
    // the same clan in a Clan Battle room. A refusal reads like a full room's (BL-3).
    if (a && !a->staff()) {
        for (const Seat& s : r.seats) {
            Peer* o = peer(s.peer);
            if (!o || !o->account || o->account == a) continue;
            const bool blocked = a->blocks_with(o->account->id) || o->account->blocks_with(a->id);
            const bool clanmates = r.settings.clan_battle && !a->clan.empty() && eng::str::iequals(a->clan, o->account->clan);
            if (blocked && !clanmates && !o->account->staff()) {
                *why = "That room is full.";
                return false;
            }
        }
    }
    if (int(r.seats.size()) >= r.settings.max_players && !observer && !staff_pass) {
        *why = "That room is full.";
        return false;
    }
    if (r.phase != RoomPhase::Waiting && !r.settings.free_join && !staff_pass) {
        *why = "That game is in progress and the host does not allow joining mid-game.";
        return false;
    }
    Seat s;
    s.peer = p.id;
    const bool teams = mode_info(r.settings.mode).teams;
    Team want = Team::None;
    if (observer && r.settings.observers) want = Team::Observer;
    else if (teams) want = r.count(Team::Red) <= r.count(Team::Blue) ? Team::Red : Team::Blue;
    bool blue_claimed = false;
    if (r.settings.clan_battle && want != Team::Observer) {
        const std::string mine = a ? a->clan : std::string();
        if (mine.empty()) {
            *why = "You are not a member of a clan.";
            return false;
        }
        if (eng::str::iequals(mine, r.settings.red_clan)) want = Team::Red;
        else if (r.settings.blue_clan.empty()) want = Team::Blue, blue_claimed = true;
        else if (eng::str::iequals(mine, r.settings.blue_clan)) want = Team::Blue;
        else {
            *why = "That room is " + r.settings.red_clan + " against " + r.settings.blue_clan + ".";
            return false;
        }
        if (r.count(want) >= r.settings.max_players / 2) {
            *why = "Your clan's side of that room is full.";
            return false;
        }
    }
    r.seats.push_back(s);
    if (!seat(r, r.seats.back(), want)) {
        r.seats.pop_back();
        *why = "No free seat in that room.";
        return false;
    }
    if (blue_claimed) r.settings.blue_clan = a->clan;
    p.room = r.id;
    send_room_state(r);
    broadcast_room_summary(r);
    broadcast_lobby_user(p);
    if (r.match && r.phase == RoomPhase::Playing) r.match->join(p.id);
    return true;
}

void Server::leave_room(Peer& p, std::string reason) {
    Room* r = find_room(p.room);
    p.room = 0;
    RoomLeft left;
    left.reason = std::move(reason);
    send_msg(p.id, left);
    broadcast_lobby_user(p);
    if (!r) return;
    if (r->match) r->match->leave(p.id, left.reason.empty());
    std::erase_if(r->seats, [&](const Seat& s) { return s.peer == p.id; });
    clan_room_changed(*r);
    if (r->vote) settle_vote(*r, false);
    if (r->seats.empty() && !r->match) {
        remove_room(*r);
        return;
    }
    if (r->seats.empty()) return;
    if (r->host == p.id) {
        r->host = r->seats.front().peer;
        r->seats.front().state = SlotState::Wait;
        if (Peer* h = peer(r->host)) notice(h->id, NoticeKind::Info, "You are the room's host now.");
    }
    send_room_state(*r);
    broadcast_room_summary(*r);
}

void Server::remove_room(Room& r) {
    const u16 id = r.id;
    const u8 ch = r.channel;
    rooms_.erase(id);
    RoomRemoved m;
    m.id = id;
    for (auto& [pid, other] : peers_)
        if (other.channel == ch) send_msg(pid, m);
}

void Server::send_room_state(Room& r) {
    RoomState st;
    st.id = r.id;
    st.settings = r.settings;
    st.settings.password.clear();
    st.phase = u8(r.phase);
    for (const Seat& s : r.seats) {
        Peer* p = peer(s.peer);
        if (!p || !p->account) continue;
        RoomMember m;
        m.id = s.peer;
        m.name = p->account->code_name;
        m.xp = p->account->shown_xp(reward_now());
        m.team = u8(s.team);
        m.slot = s.slot;
        m.state = u8(s.state);
        m.host = s.peer == r.host;
        m.force = p->account->force;
        m.loadout = p->account->loadout;
        m.clan = p->account->clan;
        m.clan_mark = p->account->clan_mark;
        if (const auto* stats = net_.stats(s.peer)) m.ping = u16(stats->rtt * 1000.0f);
        m.name_colour = p->account->name_colour, m.clan_colour = p->account->clan_colour;
        m.parts = p->account->worn(p->account->force, reward_now());
        st.members.push_back(std::move(m));
    }
    const bool teams = mode_info(r.settings.mode).teams;
    const int bots = std::min<int>(r.settings.bots, std::max(0, int(r.settings.max_players) - int(r.seats.size())));
    for (int k = 0; k < bots; ++k) {
        RoomMember m;
        m.id = kBotIdBase | u32(k + 1);
        m.name = eng::str::format("[BOT] %s %d", bot_skill_name(BotSkill(r.settings.bot_skill)), k + 1);
        int red = 0, blue = 0;
        for (const RoomMember& o : st.members) red += o.team == u8(Team::Red), blue += o.team == u8(Team::Blue);
        m.team = u8(teams ? (red <= blue ? Team::Red : Team::Blue) : Team::None);
        u8 slot = 0;
        for (bool taken = true; taken;) {
            taken = false;
            for (const RoomMember& o : st.members) taken |= o.team == m.team && o.slot == slot;
            if (taken) ++slot;
        }
        m.slot = slot;
        m.state = u8(SlotState::Ready);
        st.members.push_back(std::move(m));
    }
    const auto msg = encode(st);
    for (const Seat& s : r.seats) send(s.peer, msg);
}

void Server::broadcast_room(Room& r, const std::vector<u8>& msg, bool reliable, u32 except) {
    for (const Seat& s : r.seats)
        if (s.peer != except) send(s.peer, msg, reliable);
}

void Server::start_match(Room& r, const std::string& map) {
    r.phase = RoomPhase::Loading;
    for (Seat& s : r.seats) s.state = SlotState::Loading;
    r.match = std::make_unique<Match>(*this, r, map);
    r.match->begin_loading(now_);
    send_room_state(r);
    broadcast_room_summary(r);
    LOG_INFO("Server: room %u starts %s on %s with %zu players", unsigned(r.id), mode_name(r.settings.mode), map.c_str(), r.seats.size());
}

void Server::match_over(Room& r) {
    r.phase = RoomPhase::Waiting;
    for (Seat& s : r.seats) s.state = SlotState::Wait;
    // CH-5: a room whose channel was deleted closes once its match is over.
    if (r.closing || r.seats.empty()) {
        const u16 rid = r.id;
        for (const Seat& s : std::vector<Seat>(r.seats))
            if (Peer* o = peer(s.peer)) {
                leave_room(*o, "The channel was closed.");
                o->channel = 0;
                send_channels(*o);
            }
        if (Room* left = find_room(rid)) remove_room(*left);
        return;
    }
    send_room_state(r);
    broadcast_room_summary(r);
    for (const Seat& s : r.seats)
        if (Peer* p = peer(s.peer)) send_profile(*p);
}

void Server::chat(Peer& p, const Chat& m, double now) {
    if (now - p.last_chat > 3.0) p.chat_burst = 0;
    if (++p.chat_burst > 6 && now - p.last_chat < 1.0) return notice(p.id, NoticeKind::Warning, "Slow down: you are chatting too fast.");
    p.last_chat = now;
    const std::string text = eng::str::sanitize_line(m.text, kChatMax);
    if (text.empty()) return;
    if (muted(*p.account)) {
        const u64 until = std::max(p.account->server_muted_until, p.account->muted_until);
        return notice(p.id, NoticeKind::Warning, "You are muted for another " + eng::str::format("%llu", (unsigned long long)std::max<u64>(1, (until - wall_now() + 59) / 60)) + " minute(s).");
    }
    ChatLine line;
    line.scope = m.scope;
    line.from = p.account->code_name;
    line.text = text;
    line.role = u8(p.account->effective_role());
    const ChatScope scope = ChatScope(m.scope);
    // Whispers and clan chat are the game's with TVAS (SO-1): a server never carries them.
    if (scope == ChatScope::Whisper || scope == ChatScope::Clan) return notice(p.id, NoticeKind::Info, "Whispers and clan chat go through Team Vanilla, not this server.");
    if (scope == ChatScope::Global) return global_chat(p, std::move(line), now);
    Room* r = find_room(p.room);
    if (r) {
        const Seat* me = r->seat_of(p.id);
        line.team = me ? u8(me->team) : u8(Team::None);
        const auto msg = encode(line);
        for (const Seat& s : r->seats) {
            if (scope == ChatScope::Team && me && s.team != me->team) continue;
            send(s.peer, msg);
        }
        return;
    }
    if (!p.channel) return;
    line.scope = u8(ChatScope::Lobby);
    const auto msg = encode(line);
    for (auto& [id, other] : peers_)
        if (other.channel == p.channel && other.room == 0) send(id, msg);
}

// ── TVAS: heartbeat, presence, staff commands, reachability ────────────────────

void Server::staff_logged(const Account& actor, const Account* target, const std::string& action, const std::string& why, const std::string& detail) {
    LOG_INFO("staff: %s (#%llu) %s %s%s%s", actor.code_name.c_str(), (unsigned long long)actor.id, action.c_str(), target ? target->code_name.c_str() : "",
             why.empty() ? "" : (" -- " + why).c_str(), detail.empty() ? "" : (" (" + detail + ")").c_str());
    Value e;
    e["actor"] = actor.id;
    if (target) e["target"] = target->id;
    e["action"] = action;
    e["why"] = why;
    e["detail"] = detail;
    e["at"] = wall_now();
    if (staff_log_.size() < 500) staff_log_.push(std::move(e));
}

void Server::heartbeat(bool now_please) {
    if (!tvas_.enabled() || (beat_in_flight_ && !now_please)) return;
    last_beat_ = now_;
    beat_in_flight_ = true;
    Value b;
    b["build"] = kBuildNumber;
    b["protocol"] = kProtocolVersion;
    b["name"] = options_.name;
    b["motd"] = options_.motd;
    b["region"] = options_.region;
    b["port"] = unsigned(net_.port());
    if (!options_.public_address.empty()) b["public_address"] = options_.public_address;
    if (options_.public_port) b["public_port"] = unsigned(options_.public_port);
    u32 players = 0;
    for (auto& [id, p] : peers_) players += p.signed_in();
    b["players"] = players;
    b["slots"] = options_.max_players;
    b["channels"] = unsigned(channels_.size());
    b["min_rank"] = options_.min_rank;
    b["locked"] = !options_.password.empty();
    b["listed"] = options_.listed && !options_.no_progress;
    b["manifest"] = content_.manifest_hash();
    b["pack_bytes"] = content_.total_bytes();
    Value packs = Value::array();
    for (const std::string& s : content_.heartbeat_list()) packs.push(s);
    b["packs"] = std::move(packs);
    b["uptime"] = u64(std::max(0.0, now_ - started_));
    b["staff_log"] = staff_log_;
    b["done"] = done_commands_;
    // MN-7, PK-10: everything this server's packs add now, by saved code: what it sold and no longer
    // has is refunded to those who paid.
    Value items = Value::array();
    for (const WeaponDef& w : registry::content().weapons) items.push(w.code);
    for (const PackForce& f : registry::content().forces) items.push(f.code);
    for (const PackItem& i : registry::content().items) items.push(i.code);
    b["items"] = std::move(items);
    Value dropped = Value::array();
    for (const std::string& s : dropped_items_) dropped.push(s);
    b["dropped_items"] = std::move(dropped);
    staff_log_ = Value::array();
    done_commands_ = Value::array();
    dropped_items_.clear();
    tvas_.post("/v1/server/heartbeat", b, [this](const TvasReply& r) { on_heartbeat(r); });
}

void Server::on_heartbeat(const TvasReply& r) {
    beat_in_flight_ = false;
    if (!r.ok()) {
        LOG_ERROR("TVAS: heartbeat: %s", r.error.c_str());
        return;
    }
    const u32 tier = u32(r.body["tier"].as_uint());
    if (tier != tier_) LOG_INFO("TVAS: this server is %s", tier == 2 ? "Official" : tier == 1 ? "Verified" : "Unverified");
    tier_ = tier;
    const bool listed = r.body["listed"].as_bool();
    const std::string why = r.body["not_listed_why"].str();
    if (listed != listed_now_ || why != not_listed_why_) {
        if (listed) LOG_INFO("TVAS: listed at %s:%u", r.body["address"].str().c_str(), unsigned(r.body["port"].as_uint()));
        else if (!why.empty()) LOG_INFO("TVAS: not listed: %s", why.c_str());
    }
    listed_now_ = listed, not_listed_why_ = why;
    if (const u64 owner = r.body["owner"].as_uint(); owner && owner != accounts_.owner()) {
        accounts_.set_owner(owner);
        for (auto& [id, p] : peers_)
            if (p.account) p.account->server_role = accounts_.role_of(p.account->id);
    }
    events_sp_pct_ = u16(r.body["events"]["sp_pct"].as_uint(100));
    events_xp_pct_ = u16(r.body["events"]["xp_pct"].as_uint(100));
    // EN-3: SFLegacy Staff's commands, done and reported with the next beat.
    for (const Value& cmd : r.body["commands"].elements()) {
        Value done;
        done["id"] = cmd["id"].as_uint();
        run_command(cmd, done);
        done_commands_.push(std::move(done));
    }
    // SH-5: TV's prices changed: the floors are read again.
    if (const u32 rev = u32(r.body["tv_shop_rev"].as_uint()); rev != tv_shop_rev_ || tv_shop_.entries.empty()) {
        tvas_.get("/v1/server/tvshop", [this, rev](const TvasReply& s) {
            if (!s.ok()) return;
            ShopConfig c;
            if (!lsf::tvjson::shop_config(s.body["config"], c)) return;
            tv_shop_ = std::move(c);
            tv_shop_rev_ = rev;
            lift_to_floor();
        });
    }
}

void Server::run_command(const Value& cmd, Value& done) {
    const std::string op = cmd["op"].str();
    const u64 account = cmd["account"].as_uint();
    const std::string text = cmd["text"].str(cmd["why"].str());
    LOG_INFO("TVAS: SFLegacy Staff command: %s %llu %s", op.c_str(), (unsigned long long)account, text.c_str());
    if (op == "kick") {
        for (auto& [id, p] : peers_)
            if (p.account && p.account->id == account) {
                disconnect(id, text.empty() ? "Removed by SFLegacy Staff." : "Removed by SFLegacy Staff: " + text);
                done["result"] = "kicked";
                return;
            }
        done["result"] = "not here";
        return;
    }
    if (op == "broadcast") {
        for (auto& [id, p] : peers_)
            if (p.signed_in()) notice(id, NoticeKind::Warning, "SFLegacy Staff: " + text);
        done["result"] = "said";
        return;
    }
    if (op == "close_room" || op == "end_match") {
        Room* r = find_room(u16(cmd["room"].as_uint()));
        if (!r) {
            done["result"] = "no such room";
            return;
        }
        if (op == "end_match") {
            r->match.reset();
            match_over(*r);
            done["result"] = "ended";
            return;
        }
        const u16 rid = r->id;
        for (const Seat& s : std::vector<Seat>(r->seats))
            if (Peer* o = peer(s.peer)) leave_room(*o, "The room was closed by SFLegacy Staff.");
        if (Room* left = find_room(rid)) remove_room(*left);
        done["result"] = "closed";
        return;
    }
    if (op == "unlist") {
        options_.listed = false;
        done["result"] = "unlisted";
        heartbeat(true);
        return;
    }
    done["result"] = "unknown command";
}

void Server::tick_presence() {
    // Only what changed is told, so looking often costs nothing; a friend sees a move within a moment.
    static double last = 0;
    if (now_ - last < 0.5) return;
    last = now_;
    Value list = Value::array();
    for (auto& [id, p] : peers_) {
        if (!p.named()) continue;
        std::string where = presence_of(p);
        if (where == p.presence && p.presence_sent) continue;
        p.presence = where;
        p.presence_sent = true;
        Value e;
        e["account"] = p.account->id;
        e["where"] = where;
        e["channel"] = unsigned(p.channel);
        e["room"] = unsigned(p.room);
        bool joinable = false;
        if (Room* r = find_room(p.room)) joinable = r->phase == RoomPhase::Waiting && r->settings.password.empty() && int(r->seats.size()) < r->settings.max_players;
        e["joinable"] = joinable;
        list.push(std::move(e));
    }
    if (list.size() == 0) return;
    Value body;
    body["players"] = std::move(list);
    tvas_.post("/v1/server/presence", body, {});
}

// The answer to a list query (§8.3): our state, kAnswerSize bytes, only to a query at least that
// size (SL-5) and a few a second from one address (SL-6).
void Server::on_foreign(const eng::net::Address& from, std::span<const u8> d) {
    if (d.size() < 12) return;
    eng::ByteReader r(d.data(), d.size());
    const u32 magic = r.u32();
    const u64 nonce = r.u64();
    if (magic == kAnswerMagic) {
        // An answer to one of our own reachability checks (SL-2).
        for (ReachJob& j : reach_jobs_)
            if (!j.answered && j.to.ip == from.ip && j.to.port == from.port && nonce == (u64(j.server_id) * 2654435761ull ^ 0x5EEDull)) j.answered = true;
        return;
    }
    if (magic != kQueryMagic || d.size() < kAnswerSize) return;
    if (now_ - list_query_reset_ > 1.0) list_queries_.clear(), list_query_reset_ = now_;
    if (++list_queries_[from.ip] > 5) return;
    eng::ByteWriter w(kAnswerSize);
    w.u32(kAnswerMagic);
    w.u64(nonce);
    w.u32(kProtocolVersion);
    w.u32(kBuildNumber);
    u16 players = 0;
    for (auto& [id, p] : peers_) players += p.signed_in();
    w.u16(players);
    w.u16(u16(std::min<u32>(options_.max_players, 0xFFFF)));
    w.u8(u8(options_.min_rank));
    w.u32(u32(std::min<u64>(content_.total_bytes(), 0xFFFFFFFFull)));
    w.boolean(!options_.password.empty());
    const Room* busiest = nullptr;
    for (auto& [id, room] : rooms_)
        if (!busiest || room->seats.size() > busiest->seats.size()) busiest = room.get();
    w.string(busiest ? std::string_view(busiest->settings.map).substr(0, 32) : std::string_view());
    w.string(std::string_view(options_.name).substr(0, 48));
    w.string(std::string_view(content_.manifest_hash()).substr(0, 64));
    std::vector<u8> out = w.take();
    out.resize(kAnswerSize, 0);
    net_.send_unconnected(from, out);
}

// TVAS's answer to a match's report (PR-3): each soldier's pay and what came with it, told to
// whoever is still here, and their record read again. A TVAS that is down keeps nothing from
// anyone: the report waits on disk and is sent again (PR-7).
void Server::match_reported(u32 match, const std::map<u64, u32>& sessions, const TvasReply& r) {
    MatchRewards out;
    if (r.ok()) {
        recording_reported(match);
        for (const Value& w : r.body["rewards"].elements()) {
            const u64 account = w["account"].as_uint();
            auto it = sessions.find(account);
            if (it == sessions.end()) continue;
            Reward rw;
            rw.id = it->second;
            rw.xp = u32(w["xp"].as_uint()), rw.sp = u32(w["sp"].as_uint());
            rw.ranked_up = w["ranked_up"].as_bool();
            rw.bags = u8(std::min<u64>(w["bags"].as_uint(), 255));
            rw.dropped = u8(w["dropped"].as_uint(kNoBox));
            rw.sp_pct = u16(w["sp_pct"].as_uint(100)), rw.xp_pct = u16(w["xp_pct"].as_uint(100));
            rw.won = w["won"].as_bool();
            rw.special = u16(w["special"].as_uint());
            rw.set = u8(w["set"].as_uint());
            rw.clan_points = u16(w["clan_points"].as_uint());
            rw.xp_capped = w["xp_capped"].as_bool();
            rw.levels_left = u8(std::min<u64>(w["levels_left"].as_uint(25), 255));
            if (!w["forfeit"].as_bool()) out.rewards.push_back(rw);
            Peer* p = peer(it->second);
            if (!p || !p->account || p->account->id != account) continue;
            // A gun worn down to where the lobby warns, or broken (Game/Wear.hpp).
            for (const Value& g : w["worn"].elements()) {
                const unsigned left = unsigned(g["left"].as_uint());
                if (left == 0) notice(p->id, NoticeKind::Warning, "Your " + g["name"].str() + " is broken: it stays behind until it is mended (Inventory).");
                else notice(p->id, NoticeKind::Warning, eng::str::format("Your %s is wearing out: %u%% left. Mend it in your inventory.", g["name"].str().c_str(), left));
            }
            if (rw.bags)
                notice(p->id, NoticeKind::Good, eng::str::format("Promoted! %u Duffle Bag%s in your inventory's Gift tab.", unsigned(rw.bags), rw.bags == 1 ? "" : "s"));
            for (const Value& q : w["quests_done"].elements()) notice(p->id, NoticeKind::Good, "Quest done: " + q.str() + ". Collect it under Rewards.");
            if (rw.dropped != kNoBox) notice(p->id, NoticeKind::Good, std::string("You found a ") + box_info(rw.dropped).name + "! It is in your inventory's Gift tab.");
            if (rw.xp_capped) notice(p->id, NoticeKind::Info, "Daily experience limit reached: SP still counts; resets at 00:00 UTC.");
            fetch_profile(p->id, account, false);
        }
    } else if (r.down()) {
        out.note = "Team Vanilla's services are down: this match is kept and counted as soon as they are back.";
    } else {
        out.note = "Team Vanilla refused this match's report: " + r.error;
        recording_reported(match);
        LOG_ERROR("TVAS: match %u's report refused: %s", match, r.error.c_str());
    }
    for (const auto& [account, sid] : sessions)
        if (Peer* p = peer(sid); p && p->account && p->account->id == account) send_msg(sid, out);
}

void Server::reach_checks() {
    last_reach_ = now_;
    // Whatever was sent last time is told to TVAS now: answered or not.
    if (!reach_jobs_.empty()) {
        Value results = Value::array();
        for (const ReachJob& j : reach_jobs_) {
            Value e;
            e["server_id"] = j.server_id;
            e["ok"] = j.answered;
            results.push(std::move(e));
        }
        Value body;
        body["results"] = std::move(results);
        tvas_.post("/v1/server/reach-results", body, {});
        reach_jobs_.clear();
    }
    tvas_.get("/v1/server/reach-jobs", [this](const TvasReply& r) {
        if (!r.ok()) return;
        for (const Value& j : r.body["jobs"].elements()) {
            auto addr = eng::net::Address::resolve(j["address"].str() + ":" + std::to_string(j["port"].as_uint()), kDefaultPort);
            if (!addr) continue;
            ReachJob job;
            job.server_id = j["server_id"].as_uint();
            job.to = *addr;
            job.sent = now_;
            eng::ByteWriter w(kAnswerSize);
            w.u32(kQueryMagic);
            w.u64(u64(job.server_id) * 2654435761ull ^ 0x5EEDull);
            std::vector<u8> q = w.take();
            q.resize(kAnswerSize, 0);
            for (int k = 0; k < 3; ++k) net_.send_unconnected(job.to, q);
            reach_jobs_.push_back(job);
        }
        // Their answers are told in five seconds (sooner on a test's quick clock).
        if (!reach_jobs_.empty()) last_reach_ = now_ - std::max(0.0, options_.reach_seconds - std::min(5.0, options_.reach_seconds * 0.5));
    });
}

}  // namespace lsfs
