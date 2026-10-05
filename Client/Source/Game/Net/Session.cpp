// The game server's side of the session: the connection, the join (§5.1), the packs on their way
// (§11.7), and what the server says once in. Team Vanilla's side is SessionTvas.cpp.
#include "Game/Net/Session.hpp"

#include "Engine/Core/Crypto.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Pack.hpp"

#include <algorithm>

namespace lsf {

using namespace proto;

namespace {
constexpr size_t kChatKeep = 200;
constexpr double kQueryTimeout = 3.0;

u8 this_platform() {
#if defined(__ANDROID__)
    return u8(Platform::Android);
#elif defined(__APPLE__)
    return u8(Platform::MacOS);
#elif defined(_WIN32)
    return u8(Platform::Windows);
#else
    return u8(Platform::Linux);
#endif
}

u32 this_caps() {
    u32 caps = kCapPacks;
#ifdef _WIN32
    caps |= kCapD3D | kCapGles;
#else
    caps |= kCapGles;
#endif
    return caps;
}
}  // namespace

Session::Session() = default;
Session::~Session() {
    disconnect("closed");
    tv_away();
}

bool Session::connect(const std::string& address, double now, u64 server, const std::string& password) {
    disconnect("reconnect");
    now_ = now;
    auto addr = eng::net::Address::resolve(address, kDefaultPort);
    if (!addr) {
        state_ = State::Failed;
        refused = "The server's address could not be found.";
        ev_refused = true;
        return false;
    }
    if (!tv.signed_in) {
        state_ = State::Failed;
        refused = "Sign in to Team Vanilla first.";
        ev_refused = true;
        return false;
    }
    address_ = address;
    expect_server_ = server;
    join_password_ = password;
    if (!client_.connect(*addr, kProtocolVersion, now)) {
        state_ = State::Failed;
        refused = "Could not open a connection.";
        ev_refused = true;
        return false;
    }
    state_ = State::Connecting;
    stage_ = JoinStage::Hello;
    Hello h;
    h.platform = this_platform();
    h.caps = this_caps();
    send(h);   // queued until the handshake completes
    return true;
}

u16 Session::modes_allowed() const {
    u16 mask = u16((1u << unsigned(Mode::Count)) - 1);
    for (const ChannelInfo& c : channels)
        if (c.id == channel && c.modes) mask &= c.modes;
    return u16(mask & ~games.modes_off);
}

void Session::disconnect(std::string_view reason) {
    if (state_ == State::Offline) return;
    // DL-4: a download cancelled goes cleanly; what finished stays cached, what did not is dropped.
    if (content && stage_ == JoinStage::Downloading && content->current < content->packs.size()) {
        ContentRequest c;
        c.sha256 = content->packs[content->current].sha256;
        c.cancel = true;
        send(c);
        client_.flush(now_);
    }
    client_.disconnect(reason);
    state_ = State::Offline;
    stage_ = JoinStage::None;
    has_profile = false;
    server_info.reset();
    content.reset();
    manifest_parts_.clear();
    manifest_bytes_.clear();
    manifest_ = Json();
    pack_buffer_.clear();
    channels.clear();
    games = {};
    games_editable = false;
    channel = 0;
    rooms.clear();
    users.clear();
    room.reset();
    match_load.reset();
    match_events.clear();
    match_rewards.reset();
    shop.reset();
    replays.reset();
    service_results.clear();
    capsule_results.clear();
    staff_reports.reset();
    staff_card.reset();
    staff_recordings.reset();
    room_invites.clear();
    vote.reset();
    refresh_rev_ = 0;
    // MT-3: leaving a server forgets everything its packs added.
    if (mount) mount({}, Json(), {}, nullptr);
}

void Session::fail_join(std::string why) {
    LOG_INFO("Session: the join failed: %s", why.c_str());
    refused = std::move(why);
    ev_refused = true;
    const std::string keep = refused;
    disconnect("join failed");
    refused = keep;
    state_ = State::Failed;
}

int Session::ping_ms() const {
    const auto* s = client_.stats();
    return s ? int(s->rtt * 1000.0f) : -1;
}

void Session::poll(double now) {
    now_ = now;
    tvas_.poll();
    tv_poll(now);

    // The list's answers (§8.3), whatever the connection is doing.
    if (query_socket_.is_open()) {
        u8 buffer[512];
        eng::net::Address from;
        for (int n = query_socket_.receive(from, buffer, sizeof buffer); n >= 0; n = query_socket_.receive(from, buffer, sizeof buffer)) {
            if (size_t(n) < 26 || size_t(n) > kAnswerSize) continue;   // SL-5: never bigger than the question
            eng::ByteReader r(buffer, size_t(n));
            if (r.u32() != kAnswerMagic) continue;
            const u64 nonce = r.u64();
            auto q = std::find_if(queries_.begin(), queries_.end(), [&](const Query& o) { return o.nonce == nonce && o.to == from; });
            if (q == queries_.end()) continue;
            const u32 protocol = r.u32();
            const u32 build = r.u32();
            const u16 players = r.u16(), slots = r.u16();
            const u8 min_rank = r.u8();
            const u32 pack_bytes = r.u32();
            const bool locked = r.u8() != 0;
            const std::string map = r.string(32), name = r.string(48), manifest_hash = r.string(64);
            if (!r.ok()) continue;
            (void)protocol;
            for (ListedServer& s : servers) {
                if (q->server ? s.id != q->server : s.endpoint() != q->endpoint) continue;
                s.answered = true;
                s.ping_ms = int(std::max(0.0, now - q->sent) * 1000.0);
                s.players = players, s.slots = slots, s.min_rank = min_rank, s.pack_bytes = pack_bytes, s.locked = locked, s.build = build;
                s.map = eng::str::sanitize_line(map, 32);
                s.manifest = manifest_hash;
                // A listed server's name is TVAS's (SL-3, SL-4); one known only by its address says its own.
                if (s.direct && !name.empty()) s.name = eng::str::sanitize_line(name, 48);
            }
            queries_.erase(q);
        }
        std::erase_if(queries_, [&](const Query& q) { return now - q.sent > kQueryTimeout; });
    }

    if (state_ == State::Offline || state_ == State::Failed) return;
    std::vector<eng::net::NetEvent> events;
    client_.poll(now, events);
    for (auto& e : events) {
        if (state_ == State::Offline || state_ == State::Failed) break;
        switch (e.type) {
            case eng::net::NetEvent::Type::Connected:
                if (state_ == State::Connecting) state_ = State::Connected;
                break;
            case eng::net::NetEvent::Type::Disconnected:
                if (state_ == State::SignedIn) {
                    const std::string why = e.reason;
                    disconnect("lost");
                    disconnect_reason = why;
                    ev_disconnected = true;
                } else if (!ev_refused) {
                    // DS-6: the handshake says whose copy is behind; anything else is no answer.
                    fail_join(state_ == State::Connecting ? (e.reason.empty() ? "The server did not answer." : "The server did not answer (" + e.reason + ").")
                                                          : (e.reason.empty() ? "The server closed the connection." : e.reason));
                } else {
                    const std::string keep = refused;
                    disconnect("refused");
                    refused = keep;
                    state_ = State::Failed;
                }
                break;
            case eng::net::NetEvent::Type::Message:
                on_message(e.data, now);
                break;
        }
    }

    // DL-5: a download with nothing arriving is asked for once more from where it stood, then given up.
    if (content && stage_ == JoinStage::Downloading && content->queue == 0 && now - content->last_chunk > download_timeout) {
        if (content->retries++ < 1) next_download(now);
        else fail_join("The server stopped sending its packs.");
    }

    // The server reads the universal record again once it is behind what TVAS last said. One a
    // second at most reaches it (its own limit), so a second change waits its turn here.
    if (state_ == State::SignedIn && refresh_rev_ && has_profile && profile.rev < refresh_rev_ && now - refresh_sent_ > 1.25) {
        refresh_sent_ = now;
        Refresh m;
        m.rev = refresh_rev_;
        send(m);
    }
}

void Session::flush(double now) {
    if (state_ != State::Offline && state_ != State::Failed) client_.flush(now);
}

// ── The list's query (§8.3) ────────────────────────────────────────────────────

void Session::ping(ListedServer& s, double now) {
    if (s.local) return;
    auto addr = eng::net::Address::resolve(s.endpoint(), kDefaultPort);
    if (!addr) return;
    if (!query_socket_.is_open() && !query_socket_.open(0)) return;
    Query q;
    q.to = *addr;
    eng::crypto::random_bytes(std::span<u8>(reinterpret_cast<u8*>(&q.nonce), sizeof q.nonce));
    q.sent = now;
    q.server = s.id;
    q.endpoint = s.endpoint();
    eng::ByteWriter w(kAnswerSize);
    w.u32(kQueryMagic);
    w.u64(q.nonce);
    std::vector<u8> out = w.take();
    out.resize(kAnswerSize, 0);   // SL-5: padded to the answer's size
    query_socket_.send(q.to, out.data(), out.size());
    queries_.push_back(std::move(q));
}

void Session::ping_servers(double now) {
    queries_.clear();
    for (ListedServer& s : servers) ping(s, now);
}

// ── Requests the server itself answers ─────────────────────────────────────────

void Session::list_channels() { send(ListChannels{}); }

void Session::join_channel(u8 id) {
    JoinChannel m;
    m.id = id;
    send(m);
}

void Session::leave_channel() {
    send(LeaveChannel{});
    channel = 0;
    rooms.clear();
    users.clear();
}

void Session::create_room(const RoomSettings& s) {
    CreateRoom m;
    m.settings = s;
    send(m);
}

void Session::join_room(u16 id, const std::string& password, bool observer) {
    JoinRoom m;
    m.id = id;
    m.password = password;
    m.observer = observer;
    send(m);
}

void Session::quick_join() { send(QuickJoin{}); }
void Session::leave_room() { send(LeaveRoom{}); }

void Session::change_room(const RoomSettings& s) {
    ChangeRoom m;
    m.settings = s;
    send(m);
}

void Session::set_team(Team t) {
    SetTeam m;
    m.team = u8(t);
    send(m);
}

void Session::set_ready(bool ready) {
    SetReady m;
    m.ready = ready;
    send(m);
}

void Session::kick(u32 player) {
    KickPlayer m;
    m.player = player;
    send(m);
}

void Session::start_match() { send(StartMatch{}); }
void Session::load_done() { send(LoadDone{}); }
void Session::leave_match() { send(LeaveMatch{}); }

void Session::set_global_chat(bool on) {
    global_chat_ = on;
    if (signed_in()) send(ChatPrefs{global_chat_, room_invites_});
}

void Session::room_invite_answer(u16 room_id, bool accept) {
    RoomInviteAnswer m;
    m.room = room_id;
    m.accept = accept;
    send(m);
}

int Session::friend_requests() const {
    int n = 0;
    if (friends)
        for (const FriendEntry& e : friends->entries) n += e.state == u8(FriendState::Incoming);
    return n;
}

int Session::unread_mail() const {
    int n = 0;
    if (mailbox)
        for (const MailItem& m : mailbox->items) n += !m.read;
    return n;
}

void Session::report(u32 player, const std::string& code_name, ReportReason reason, const std::string& note) {
    ReportPlayer m;
    m.player = player;
    m.code_name = code_name;
    m.reason = u8(reason);
    m.note = note;
    send(m);
}

void Session::call_vote(u32 target) {
    CallVote m;
    m.target = target;
    send(m);
}

void Session::cast_vote(bool yes) {
    CastVote m;
    m.yes = yes;
    send(m);
    if (vote) vote->can_vote = false;
}

void Session::staff_list_reports(bool open_only) {
    StaffListReports m;
    m.open_only = open_only;
    send(m);
}

void Session::staff_resolve(u32 report_id, ReportState state) {
    StaffResolveReport m;
    m.report = report_id;
    m.state = u8(state);
    send(m);
}

void Session::staff_action(const std::string& account, StaffOp op, u32 seconds, const std::string& reason, Role role) {
    StaffAction m;
    m.account = account;
    m.op = u8(op);
    m.seconds = seconds;
    m.reason = reason;
    m.role = u8(role);
    send(m);
}

void Session::staff_find(const std::string& name) {
    StaffFindAccount m;
    m.name = name;
    send(m);
}

void Session::staff_edit(const std::string& account, AccountField field, u32 value, const std::string& text) {
    StaffEditAccount m;
    m.account = account;
    m.field = u8(field);
    m.value = value;
    m.text = text;
    send(m);
}

void Session::staff_list_recordings() { send(StaffListRecordings{}); }

void Session::channel_edit(ChannelOp op, const ChannelInfo& info) {
    ChannelEdit m;
    m.op = u8(op);
    m.channel = info;
    m.version = channels_version;   // CH-6: the list this edit was made on
    send(m);
}

void Session::motd_edit(const std::string& text) {
    MotdEdit m;
    m.motd = text;
    send(m);
}

void Session::games_edit(const ServerGames& g) {
    GamesEdit m;
    m.games = g;
    send(m);
}

void Session::request_shop() { send(Request{u8(RequestOp::Shop)}); }
void Session::list_replays() { send(Request{u8(RequestOp::ReplayList)}); }

void Session::get_replay(u32 match) {
    replay = ReplayDownload{};
    replay->match = match;
    send(Request{u8(RequestOp::ReplayGet), 0, 0, match});
}

void Session::staff_save_shop(const ShopConfig& config) {
    StaffShopSave m;
    m.config = config;
    send(m);
}

void Session::request_replay(u32 match) {
    replay = ReplayDownload{};
    replay->match = match;
    StaffRequestReplay m;
    m.match = match;
    send(m);
}

// ── What is kept ───────────────────────────────────────────────────────────────

const ShopConfig& Session::shop_config() const {
    static const ShopConfig own = [] {
        ShopConfig c = default_shop();
        sanitize(c);
        return c;
    }();
    return shop ? *shop : own;
}

u8 Session::durability_of(u16 weapon_id) const {
    const WeaponDef* w = weapon(weapon_id);
    if (!w || !lsf::wears(*w)) return kDurabilityFull;
    for (const OwnedGun& g : owned_guns)
        if (g.id == weapon_id) return std::min(g.durability, kDurabilityFull);
    return kDurabilityFull;
}

u32 Session::repair_price(u16 weapon_id) const {
    return repair_cost(for_good_price(shop_config(), ShopKind::Weapon, weapon_id), durability_of(weapon_id));
}

u32 Session::resale_of(ShopKind kind, u16 id) const {
    if (kind == ShopKind::Weapon) {
        const WeaponDef* w = weapon(id);
        return w ? gun_resale(shop_config(), *w, durability_of(id)) : 0;
    }
    return resale_price(for_good_price(shop_config(), kind, id));
}

namespace {
u32 left_of(u32 seconds_left, double since) {
    if (seconds_left == OwnedItem::kForGood) return OwnedItem::kForGood;
    const double left = double(seconds_left) - since;
    return left > 0 ? u32(left) : 0;
}
}  // namespace

u32 Session::weapon_seconds_left(u16 id, double now) const {
    if (!owns_weapon(id)) return 0;
    for (const OwnedGun& g : owned_guns)
        if (g.id == id) return left_of(g.seconds_left, now - items_at);
    return OwnedItem::kForGood;
}

u32 Session::force_seconds_left(u8 id, double now) const {
    const ForceDef* f = force(id);
    if (f && f->price == 0) return OwnedItem::kForGood;
    if (!owns_force(id)) return 0;
    for (const OwnedItem& o : force_time)
        if (o.id == id) return left_of(o.seconds_left, now - items_at);
    return OwnedItem::kForGood;
}

u32 Session::spray_seconds_left(u16 id, double now) const {
    for (const OwnedItem& o : owned_sprays)
        if (o.id == id) return left_of(o.seconds_left, now - items_at);
    return 0;
}

bool Session::owns_weapon(u16 id) const { return std::find(owned_weapons.begin(), owned_weapons.end(), id) != owned_weapons.end(); }
bool Session::owns_force(u8 id) const { return std::find(owned_forces.begin(), owned_forces.end(), u16(id)) != owned_forces.end(); }

const OwnedItem* Session::owned_item(u16 id) const {
    for (const OwnedItem& o : owned_items)
        if (o.id == id) return &o;
    return nullptr;
}

u32 Session::item_seconds_left(u16 id, double now) const {
    const OwnedItem* o = owned_item(id);
    if (!o) return 0;
    if (o->seconds_left == OwnedItem::kForGood) return OwnedItem::kForGood;
    const double left = double(o->seconds_left) - (now - items_at);
    return left > 0 ? u32(left) : 0;
}

bool Session::wears(u16 id) const { return std::find(equipped.begin(), equipped.end(), id) != equipped.end(); }

std::vector<u16> Session::worn(u8 force) const {
    std::vector<u16> out;
    for (u16 id : equipped)
        if (const ItemDef* d = item(id); d && d->force == force && owned_item(id)) out.push_back(id);
    return out;
}

bool Session::has_boost(Boost b, double now) const {
    for (const OwnedItem& o : owned_items) {
        const ItemDef* d = item(o.id);
        if (!d || d->kind != ItemKind::Boost || d->one_use()) continue;
        if (item_seconds_left(o.id, now) == 0) continue;
        if (boost_includes(d->boost, b)) return true;
    }
    return false;
}

const RoomMember* Session::me_in_room() const {
    if (!room) return nullptr;
    for (const RoomMember& m : room->members)
        if (m.id == session_id) return &m;
    return nullptr;
}

bool Session::is_host() const {
    const RoomMember* me = me_in_room();
    return me && me->host;
}

void Session::add_chat(ChatLine line, double now) {
    if (line.scope == u8(ChatScope::Whisper) && line.to.empty() && line.from != profile.code_name) last_whisper_from = line.from;
    auto& list = room ? room_chat : lobby_chat;
    list.push_back({line, now});
    while (list.size() > kChatKeep) list.pop_front();
    // In a match the world shows it too.
    if (match_load || !match_events.empty()) match_events.push_back(encode(line));
}

// ── Packs (§11.6 to §11.8) ─────────────────────────────────────────────────────

void Session::manifest_part(const ManifestPart& m, double now) {
    if (stage_ != JoinStage::Joining && stage_ != JoinStage::Manifest) return;
    // PK-9: in pieces when it is over a message; never more than a manifest could be.
    if (m.count == 0 || m.count > 64 || m.index >= m.count) return fail_join("The server sent a manifest that cannot be read.");
    stage_ = JoinStage::Manifest;
    if (manifest_parts_.size() != m.count) manifest_parts_.assign(m.count, {});
    manifest_parts_[m.index] = m.bytes;
    if (m.index + 1 != m.count) return;
    manifest_bytes_.clear();
    for (size_t i = 0; i < manifest_parts_.size(); ++i) {
        if (manifest_parts_[i].empty() && manifest_parts_.size() > 1) return fail_join("The server's manifest came with a piece missing.");
        manifest_bytes_.insert(manifest_bytes_.end(), manifest_parts_[i].begin(), manifest_parts_[i].end());
    }
    manifest_parts_.clear();
    manifest_ready(now);
}

void Session::manifest_ready(double now) {
    const std::string expected = server_info ? server_info->manifest : std::string();
    if (manifest_bytes_.empty() || expected.empty()) {
        // No packs: only what the game itself has. Whatever the last server added goes (MT-3).
        if (!expected.empty()) return fail_join("The server said it has packs and sent no manifest.");
        manifest_bytes_.clear();
        std::string why;
        if (mount && !mount({}, Json(), {}, &why)) return fail_join(why);
        stage_ = JoinStage::Entering;
        send(Ready{});
        return;
    }
    const std::string hash = pack::sha256_hex(manifest_bytes_);
    if (hash != expected) return fail_join("The server's manifest is not the one it announced.");
    if (!eng::json::parse(std::string_view(reinterpret_cast<const char*>(manifest_bytes_.data()), manifest_bytes_.size()), manifest_))
        return fail_join("The server's manifest cannot be read.");
    Content c;
    u64 total = 0;
    for (const Json& p : manifest_["packs"].elements()) {
        Content::Need n;
        n.id = p["id"].str();
        n.sha256 = eng::str::lower(p["sha256"].str());
        n.size = u32(std::min<u64>(p["size"].as_uint(), 0xFFFFFFFFull));
        if (!pack::id_ok(n.id) || n.sha256.size() != 64 || n.sha256.find_first_not_of("0123456789abcdef") != std::string::npos || n.size == 0)
            return fail_join("The server's manifest names a pack that cannot be one.");
        // RT-3: a pack Team Vanilla took down stops everywhere at once.
        if (std::find(blocked_packs_.begin(), blocked_packs_.end(), n.sha256) != blocked_packs_.end())
            return fail_join("This server's pack \"" + n.id + "\" has been taken down by Team Vanilla: the server cannot be joined with it.");
        total += n.size;
        c.packs.push_back(std::move(n));
    }
    if (c.packs.empty() || c.packs.size() > 64) return fail_join("The server's manifest lists no packs it could have.");
    // DL-6: 50 MB a server, on every platform; never downloaded past it.
    if (total > pack::kMaxPackBytes) return fail_join("This server's packs are over the 50 MB limit: it cannot be joined.");
    if (cache_dir.empty()) return fail_join("This game has nowhere to keep a server's packs.");
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    for (Content::Need& n : c.packs) {
        // §11.8: named by its SHA-256; its hash checked again before it is used.
        const std::filesystem::path file = cache_dir / (n.sha256 + ".lsfpack");
        if (std::filesystem::file_size(file, ec) == n.size && !ec) {
            auto bytes = eng::fs::read_file(file);
            if (bytes && pack::sha256_hex(*bytes) == n.sha256) n.cached = true;
            else std::filesystem::remove(file, ec);
        }
        ec.clear();
        if (!n.cached) c.need_bytes += n.size;
    }
    content = std::move(c);
    if (content->need_bytes == 0) return content_done(now);
    if (content->need_bytes > ask_over) {
        // §11.7: the size shown and a yes asked before anything over the threshold comes down.
        content->ask = true;
        stage_ = JoinStage::Consent;
        return;
    }
    stage_ = JoinStage::Downloading;
    content->current = 0;
    next_download(now);
}

void Session::content_accept() {
    if (!content || stage_ != JoinStage::Consent) return;
    content->ask = false;
    stage_ = JoinStage::Downloading;
    content->current = 0;
    next_download(now_);
}

void Session::content_decline() {
    if (!content) return;
    const std::string keep = "You chose not to download this server's packs.";
    disconnect("download declined");
    refused = keep;
    ev_refused = true;
    state_ = State::Failed;
}

void Session::next_download(double now) {
    if (!content) return;
    while (content->current < content->packs.size() && content->packs[content->current].cached) ++content->current, pack_buffer_.clear();
    if (content->current >= content->packs.size()) return content_done(now);
    const Content::Need& n = content->packs[content->current];
    ContentRequest c;
    c.sha256 = n.sha256;
    c.offset = u32(pack_buffer_.size());   // DL-2: from where it stood
    content->offset = c.offset;
    content->last_chunk = now;
    content->queue = 0;
    send(c);
}

void Session::content_chunk(const ContentChunk& m, double now) {
    if (!content || stage_ != JoinStage::Downloading || content->current >= content->packs.size()) return;
    Content::Need& n = content->packs[content->current];
    if (m.sha256 != n.sha256) return;
    if (!m.error.empty()) return fail_join(eng::str::sanitize_line(m.error, 200));
    content->last_chunk = now;
    if (m.bytes.empty()) {
        content->queue = m.queue;   // DL-1: waiting, and where in the queue
        return;
    }
    content->queue = 0;
    if (m.total != n.size || m.offset != pack_buffer_.size() || size_t(m.offset) + m.bytes.size() > n.size) return;   // out of step: the timeout asks again
    if (pack_buffer_.capacity() < n.size) pack_buffer_.reserve(n.size);
    pack_buffer_.insert(pack_buffer_.end(), m.bytes.begin(), m.bytes.end());
    content->got_bytes += m.bytes.size();
    if (pack_buffer_.size() < n.size) return;
    // DL-3: a pack that fails its hash is fetched once more, then given up; it is never mounted.
    if (pack::sha256_hex(pack_buffer_) != n.sha256) {
        content->got_bytes -= std::min<u64>(content->got_bytes, pack_buffer_.size());
        pack_buffer_.clear();
        if (content->retries++ < 1) return next_download(now);
        return fail_join("The pack \"" + n.id + "\" did not arrive as the server's manifest describes it.");
    }
    // SC-3: the cache holds only <sha256>.lsfpack; a pack's own names are never paths on disk.
    const std::filesystem::path file = cache_dir / (n.sha256 + ".lsfpack");
    const std::filesystem::path part = cache_dir / (n.sha256 + ".part");
    std::error_code ec;
    if (!eng::fs::write_file(part, pack_buffer_.data(), pack_buffer_.size())) return fail_join("The pack \"" + n.id + "\" could not be saved: is the disk full?");
    std::filesystem::rename(part, file, ec);
    if (ec) {
        std::filesystem::remove(file, ec);
        std::filesystem::rename(part, file, ec);
        if (ec) return fail_join("The pack \"" + n.id + "\" could not be saved.");
    }
    n.cached = true;
    pack_buffer_.clear();
    content->retries = 0;
    ++content->current;
    next_download(now);
}

void Session::content_done(double) {
    if (!content) return;
    stage_ = JoinStage::Mounting;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const Content::Need& n : content->packs) {
        files.push_back(cache_dir / (n.sha256 + ".lsfpack"));
        std::filesystem::last_write_time(files.back(), std::filesystem::file_time_type::clock::now(), ec);   // §11.8: least recently used goes first
    }
    std::string why;
    const std::string hash = server_info ? server_info->manifest : std::string();
    if (!mount) return fail_join("This game cannot use a server's packs.");
    if (!mount(files, manifest_, hash, &why)) return fail_join(why.empty() ? "This server's packs could not be used." : why);
    prune_cache();
    stage_ = JoinStage::Entering;
    Ready r;
    r.manifest = hash;
    send(r);
}

// §11.8: the cache has a cap; the least recently used packs go first, never one in use.
void Session::prune_cache() {
    if (cache_dir.empty()) return;
    struct Kept {
        std::filesystem::path file;
        u64 size = 0;
        std::filesystem::file_time_type used;
    };
    std::vector<Kept> all;
    u64 total = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (e.path().extension() == ".part") {
            std::filesystem::remove(e.path(), ec);
            continue;
        }
        if (e.path().extension() != ".lsfpack") continue;
        Kept k{e.path(), u64(e.file_size(ec)), e.last_write_time(ec)};
        total += k.size;
        all.push_back(std::move(k));
    }
    std::sort(all.begin(), all.end(), [](const Kept& a, const Kept& b) { return a.used < b.used; });
    for (const Kept& k : all) {
        if (total <= cache_cap) break;
        bool in_use = false;
        if (content)
            for (const Content::Need& n : content->packs) in_use = in_use || k.file.stem().string() == n.sha256;
        if (in_use) continue;
        std::filesystem::remove(k.file, ec);
        total -= std::min(total, k.size);
    }
}

// ── What the server says ───────────────────────────────────────────────────────

void Session::on_message(std::span<const u8> data, double now) {
    switch (peek_id(data)) {
        case Msg::ServerInfo: {
            ServerInfo m;
            if (!decode(data, m) || stage_ != JoinStage::Hello) break;
            // SL-4: a listed server is the one TVAS listed, not whoever answers at its address.
            if (expect_server_ && m.server_id != expect_server_) return fail_join("That address answered as another server than the one listed.");
            if (!m.server_id) return fail_join("That server is not registered with Team Vanilla.");
            m.name = eng::str::sanitize_line(m.name, 64);
            server_info = m;
            server_name = m.name;
            motd = m.motd;
            stage_ = JoinStage::Ticket;
            ask_ticket();
            break;
        }
        case Msg::ManifestPart: {
            ManifestPart m;
            if (decode(data, m)) manifest_part(m, now);
            break;
        }
        case Msg::ContentChunk: {
            ContentChunk m;
            if (decode(data, m)) content_chunk(m, now);
            break;
        }
        case Msg::Welcome: {
            Welcome m;
            if (!decode(data, m)) break;
            session_id = m.session;
            server_name = m.server_name;
            motd = m.motd;
            state_ = State::SignedIn;
            stage_ = JoinStage::None;
            content.reset();
            ev_welcomed = true;
            send(ChatPrefs{global_chat_, room_invites_});
            // ML-8: the server's message of the day, to everyone joining it.
            if (!motd.empty()) {
                ChatLine l;
                l.scope = u8(ChatScope::System);
                l.text = server_name + ": " + motd;
                add_chat(std::move(l), now);
            }
            break;
        }
        case Msg::Refused: {
            Refused m;
            if (!decode(data, m)) break;
            refused = m.reason;
            ev_refused = true;
            break;
        }
        case Msg::Profile: {
            Profile m;
            if (decode(data, m)) {
                profile = m;
                has_profile = true;
            }
            break;
        }
        case Msg::Inventory: {
            InventoryMsg m;
            if (decode(data, m)) {
                owned_weapons = m.weapons;
                owned_forces = m.forces;
                owned_items = std::move(m.items);
                items_at = now;
                equipped = std::move(m.equipped);
                owned_guns = std::move(m.guns);
                force_time = std::move(m.force_time);
                owned_sprays = std::move(m.sprays);
                spray = m.spray;
                coins = m.coins;
                gifts = std::move(m.gifts);
                horror_items = m.horror;
                bags = std::move(m.bags);
            }
            break;
        }
        case Msg::ShopStateMsg: {
            ShopState m;
            if (decode(data, m)) shop = std::move(m.config);
            break;
        }
        case Msg::ServiceResult: {
            ServiceResult m;
            if (decode(data, m)) service_results.push_back(std::move(m));
            break;
        }
        case Msg::CapsuleResult: {
            CapsuleResult m;
            if (decode(data, m)) capsule_results.push_back(std::move(m));
            break;
        }
        case Msg::ReplayListMsg: {
            ReplayList m;
            if (decode(data, m)) replays = std::move(m), replays_at = now;
            break;
        }
        case Msg::Notice: {
            Notice m;
            if (decode(data, m)) notices.push_back({NoticeKind(m.kind), m.text});
            break;
        }
        case Msg::ShopResult: {
            ShopResult m;
            if (!decode(data, m)) break;
            // A sale on this server spent SP at Team Vanilla: its word on what is left is asked for
            // now, not at the next turn of the news.
            if (m.ok) last_poll_ = -1.0e9;
            shop_results.push_back(m);
            break;
        }
        case Msg::RoomInvited: {
            RoomInvited m;
            if (!decode(data, m)) break;
            // One invitation a room: a second from the same room takes the first one's place.
            std::erase_if(room_invites, [&](const RoomInvited& o) { return o.room == m.room; });
            room_invites.push_back(std::move(m));
            break;
        }
        case Msg::StaffResult: {
            StaffResult m;
            if (decode(data, m)) staff_results.push_back(std::move(m));
            break;
        }
        case Msg::StaffReportList: {
            StaffReportList m;
            if (decode(data, m)) staff_reports = std::move(m);
            break;
        }
        case Msg::StaffAccountCard: {
            StaffAccountCard m;
            if (decode(data, m)) staff_card = std::move(m);
            break;
        }
        case Msg::StaffRecordingList: {
            StaffRecordingList m;
            if (decode(data, m)) staff_recordings = std::move(m);
            break;
        }
        case Msg::ReplayChunk: {
            ReplayChunk m;
            if (!decode(data, m) || !replay || replay->match != m.match) break;
            if (!m.error.empty()) {
                replay->error = m.error;
                break;
            }
            if (m.total > (256u << 20) || size_t(m.offset) + m.bytes.size() > m.total) break;
            replay->total = m.total;
            if (replay->bytes.size() != m.total) replay->bytes.resize(m.total);
            std::copy(m.bytes.begin(), m.bytes.end(), replay->bytes.begin() + m.offset);
            replay->got += u32(m.bytes.size());
            replay->done = replay->got >= replay->total;
            break;
        }
        case Msg::VoteState: {
            VoteState m;
            if (!decode(data, m)) break;
            vote = std::move(m);
            vote_at = now;
            break;
        }
        case Msg::ChannelList: {
            ChannelList m;
            if (!decode(data, m)) break;
            server_name = m.server_name;
            channels = std::move(m.channels);
            channels_version = m.version;
            channels_editable = m.editable;
            ev_channels = true;
            break;
        }
        case Msg::GamesState: {
            GamesState m;
            if (!decode(data, m)) break;
            games = std::move(m.games);
            sanitize(games);
            games_editable = m.editable;
            break;
        }
        case Msg::ChannelJoined: {
            ChannelJoined m;
            if (!decode(data, m)) break;
            channel = m.id;
            channel_name = m.name;
            rooms.clear();
            users.clear();
            lobby_chat.clear();
            ev_channel_joined = true;
            break;
        }
        case Msg::LobbyState: {
            LobbyState m;
            if (!decode(data, m)) break;
            rooms.clear();
            users.clear();
            for (auto& r : m.rooms) rooms[r.id] = r;
            for (auto& u : m.users) users[u.id] = u;
            break;
        }
        case Msg::RoomSummaryMsg: {
            RoomSummaryMsg m;
            if (decode(data, m)) rooms[m.room.id] = m.room;
            break;
        }
        case Msg::RoomRemoved: {
            RoomRemoved m;
            if (decode(data, m)) rooms.erase(m.id);
            break;
        }
        case Msg::LobbyUser: {
            LobbyUser m;
            if (decode(data, m)) users[m.user.id] = m.user;
            break;
        }
        case Msg::LobbyUserLeft: {
            LobbyUserLeft m;
            if (decode(data, m)) users.erase(m.id);
            break;
        }
        case Msg::ChatLine: {
            ChatLine m;
            if (!decode(data, m)) break;
            if (m.scope == u8(ChatScope::Whisper) && m.to.empty() && m.from != profile.code_name) last_whisper_from = m.from;
            auto& list = room ? room_chat : lobby_chat;
            list.push_back({m, now});
            while (list.size() > kChatKeep) list.pop_front();
            if (match_load || !match_events.empty()) match_events.push_back(std::vector<u8>(data.begin(), data.end()));
            break;
        }
        case Msg::RoomState: {
            RoomState m;
            if (!decode(data, m)) break;
            const bool fresh = !room || room->id != m.id;
            room = std::move(m);
            if (fresh) {
                room_chat.clear();
                ev_room_joined = true;
            }
            break;
        }
        case Msg::RoomLeft: {
            RoomLeft m;
            if (!decode(data, m)) break;
            room.reset();
            room_left_reason = m.reason;
            ev_room_left = true;
            match_load.reset();
            match_events.clear();
            break;
        }
        case Msg::MatchLoad: {
            MatchLoad m;
            if (!decode(data, m)) break;
            match_load = std::move(m);
            match_events.clear();
            match_over.reset();
            match_rewards.reset();
            break;
        }
        case Msg::MatchOver: {
            MatchOver m;
            if (!decode(data, m)) break;
            match_over = m;
            match_events.push_back(std::vector<u8>(data.begin(), data.end()));
            break;
        }
        case Msg::MatchRewards: {
            // TVAS's word on the match's pay (PR-3): the results' own rows are brought up to date.
            MatchRewards m;
            if (!decode(data, m)) break;
            if (match_over)
                for (const Reward& r : m.rewards) {
                    bool found = false;
                    for (Reward& mine : match_over->rewards)
                        if (mine.id == r.id) mine = r, found = true;
                    if (!found) match_over->rewards.push_back(r);
                }
            for (const Reward& r : m.rewards)
                if (r.id == session_id) tv.levels_left = r.levels_left, tv.xp_capped = r.xp_capped;
            match_rewards = std::move(m);
            break;
        }
        default:
            // Everything else belongs to the match.
            match_events.push_back(std::vector<u8>(data.begin(), data.end()));
            if (match_events.size() > 4096) match_events.pop_front();
            break;
    }
}

}  // namespace lsf
