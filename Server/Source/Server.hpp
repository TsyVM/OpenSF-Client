// The Soldier Front Legacy server: one process doing what the original's eight did, over
// Engine/Net's reliable UDP, for soldiers who are Team Vanilla accounts
// (Docs/UniversalServerDeploy.md):
//
//   joining    Hello -> ServerInfo -> Join (a ticket TVAS signed, checked here: ID-4) -> the
//              soldier's universal record from TVAS -> the gate (§9) -> the manifest and the packs
//              (§11) -> Ready -> Welcome and the channel list
//   channels   channels.cfg, the Owner's to edit (§10), with their rank and K/D gates
//   lobby      a channel's rooms and users, and its chat
//   rooms      the waiting room: teams, seats, ready, the host's settings, start
//   matches    authority over what clients report, and the match report TVAS pays by (§6.6)
//   TVAS       heartbeats (§8.2), where each soldier is (SO-4), sales with the player's approval
//              (§6.3), staff commands (EN-3), and, on an official server, reachability checks (SL-2)
//
// It runs as LegacySFServer.exe (legacysf-server on Linux), or inside the game: a player's own
// "This PC" (SL-9), unlisted, no progress (D41), on a thread of its own either way.
#pragma once

#include "ServerOnly.hpp"

#include "Accounts.hpp"
#include "Content.hpp"
#include "Maps.hpp"
#include "TvasLink.hpp"

#include "Engine/Net/NetHost.hpp"
#include "Game/Protocol.hpp"
#include "Game/Shop.hpp"
#include "Game/Tvas.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lsfs {

class Match;

struct ServerOptions {
    // Who this server is (§8.1, §12.3): its id at TVAS, its own key, and where TVAS is (TV-15).
    std::string tvas = lsf::tvas::kTvasAddress;
    u64 server_id = 0;
    lsf::tvas::ServerKey key;
    // DS-1: a dedicated server refuses to start without an identity; a test may run without TVAS.
    bool require_identity = true;
    std::string name = "Soldier Front Legacy";
    std::string motd = "Welcome to Soldier Front Legacy!";
    // ML-8: the F9 panel's edit, kept where the server reads it at start (server.cfg): unset, it
    // lasts until the server stops.
    std::function<void(const std::string&)> on_motd;
    std::string region;
    bool listed = true;                 // false: a private server (registered, unlisted: SL-8)
    // A player's own "This PC" (SL-9): loopback, unlisted, no match reports (D41).
    bool no_progress = false;
    bool loopback_only = false;
    // Network (§12.3, D38): which address of this machine to listen on, the port, and where
    // players reach it (empty: whatever TVAS sees the heartbeat come from, SL-2a).
    std::string bind_address;
    u16 port = kDefaultPort;
    std::string public_address;
    u16 public_port = 0;
    u32 max_players = 512;
    u32 reserved_slots = 4;            // GT-6: kept for staff when full
    // The gate (§9): ranks inclusive, the real rank never the Fake Rank Mark's (GT-2).
    int min_rank = 0, max_rank = 74;
    float min_kd = 0;
    std::string password;              // a private server's
    // Files (§12.1): this server's own data by TV account id, its staff, its channels, its shop.
    std::filesystem::path accounts_dir = "accounts";
    std::filesystem::path staff_file = "staff.cfg";
    std::filesystem::path channels_file = "channels.cfg";
    std::filesystem::path shop;        // empty: shop.cfg beside the accounts folder
    lsf::u32 test_evil = 0;            // tests only: Content.hpp's Evil (a hostile server, SC-5)
    std::filesystem::path packs_dir;   // built packs (lsfpack): served to players (§11.7); empty: none
    // Downloads (§11.7, DL-1): the whole upload for packs, and how many download at once.
    u32 upload_kbps = 4000;
    u32 max_downloaders = 4;
    // Every match is recorded here for staff (Game/Replay.hpp); empty: none are. Kept 7 days
    // (DS-8, D47), and longer only while its report still waits for TVAS.
    std::filesystem::path recordings = "recordings";
    u32 recording_days = 7;
    // The Soldier Front client's data folder (D49: the server's own data/): the maps' collision for
    // bots and for telling each soldier only what they could see.
    std::filesystem::path data;
    bool require_data = false;         // DS-1: a dedicated server will not start without it
    int clan_battle_min = kClanBattleMin;
    float host_start_seconds = 30.0f;
    double heartbeat_seconds = 30.0;   // §8.2
    double reach_seconds = 60.0;       // SL-2: how often an official server checks others' addresses for TVAS
    // Anti-cheat's thresholds (Match::review_outliers).
    u32 outlier_min_kills = 10;
    float outlier_headshot_share = 0.8f;
    float outlier_accuracy = 0.85f;
    u32 outlier_flicks = 3;
    u32 outlier_teleports = 6;
    u32 outlier_blocked = 4;
};

// How far a connection has come (§5.1).
enum class JoinStep : u8 { Hello, Info, Ticket, Profile, Content, Ready };

struct Peer {
    u32 id = 0;
    JoinStep step = JoinStep::Hello;
    Account* account = nullptr;
    u8 platform = 0;
    u32 build = 0, caps = 0;
    lsf::tvas::Ticket ticket;           // what TVAS signed for this join
    double profile_asked = 0;
    u8 channel = 0;       // 0: none
    u16 room = 0;         // 0: none
    double last_chat = 0;
    int chat_burst = 0;
    u32 ip = 0;
    double connected_at = 0;
    u32 last_match = 0;
    double vote_called_at = -1000;
    bool global_chat = true;
    bool room_invites = true;
    double last_invite = -1000;
    double last_global = -1000;
    double last_refresh = -1000;
    std::string presence;             // where they were when TVAS was last told (SO-4)
    bool presence_sent = false;
    // A pack on its way (§11.7): which, from where, and its place in the queue (DL-1).
    struct Download {
        std::string sha256;
        u32 offset = 0;
        double last_chunk = 0;
    };
    std::optional<Download> download;
    double waiting_since = 0;          // in the download queue since
    struct ReplaySend {
        u32 match = 0;
        std::vector<u8> bytes;
        size_t sent = 0;
    };
    std::shared_ptr<ReplaySend> replay_out;
    bool signed_in() const { return account != nullptr && step == JoinStep::Ready; }
    bool named() const { return signed_in() && !account->code_name.empty(); }
};

struct Seat {
    u32 peer = 0;
    Team team = Team::Red;
    u8 slot = 0;
    proto::SlotState state = proto::SlotState::Wait;
};

struct Room {
    u16 id = 0;
    u8 channel = 0;
    RoomSettings settings;
    u32 host = 0;
    proto::RoomPhase phase = proto::RoomPhase::Waiting;
    std::vector<Seat> seats;
    std::unique_ptr<Match> match;
    double all_ready_since = 0;
    u32 all_ready_host = 0;
    bool start_warned = false;
    struct Vote {
        u32 target = 0, caller = 0;
        Team side = Team::None;
        std::set<u32> yes, no;
        double ends = 0;
    };
    std::optional<Vote> vote;
    // Accounts kept out of this room until a time (a vote passed, or the host removed them), by id.
    std::map<u64, double> barred;
    struct Invitation {
        double until = 0;
        std::string from;
    };
    std::map<u64, Invitation> invited;
    bool closing = false;              // its channel was deleted: it plays its match out, then goes (CH-5)
    Seat* seat_of(u32 peer);
    int count(Team t) const;
};

class Server {
public:
    Server();
    ~Server();

    // False, and `why`, when it cannot start (DS-1: never "starts anyway").
    bool start(const ServerOptions& options, std::string* why = nullptr);
    void stop();
    void tick(double now);
    bool running() const { return net_.running(); }
    u16 port() const { return net_.port(); }

    bool start_thread(const ServerOptions& options, std::string* why = nullptr);
    void stop_thread();
    void set_clock_offset(i64 seconds) { clock_offset_ = seconds; }
    void set_shop_roll(int roll) { shop_roll_ = roll; }
    void test_bots_before(u32 session, float distance);
    void test_bots_pace(u32 session, u8 pace);   // Match::test_bots_pace, in that soldier's match
    const ShopConfig& shop() const { return shop_; }
    u16 start_bot_room(RoomSettings settings);
    bool room_playing(u16 id);
    // Tests: wait for every request to TVAS to be answered.
    void drain_tvas(double seconds) { tvas_.drain(seconds); }

    void send(u32 peer, std::vector<u8> msg, bool reliable = true) { net_.send(peer, std::move(msg), reliable); }
    Peer* peer(u32 id);
    void broadcast_room(Room& room, const std::vector<u8>& msg, bool reliable = true, u32 except = 0);
    void match_over(Room& room);
    Accounts& accounts() { return accounts_; }
    TvasLink& tvas() { return tvas_; }
    u64 reward_now() const;
    MapCache& maps() { return maps_; }
    void notice(u32 peer, proto::NoticeKind kind, std::string text);
    void staff_notice(const std::string& text);
    double now() const { return now_; }
    const ServerOptions& options() const { return options_; }
    const ServerContent& content() const { return content_; }
    // TV's running events, as the last heartbeat said (the Tab board's marks; the pay is TVAS's).
    u16 events_sp_pct() const { return events_sp_pct_; }
    u16 events_xp_pct() const { return events_xp_pct_; }
    // A soldier's universal record read again (a match's pay, a sale): their profile and inventory
    // sent on.
    void apply_profile(const eng::json::Value& me);
    void send_profile(Peer& p);
    void send_inventory(Peer& p);
    Peer* peer_of(const Account* a);

    u32 new_match_id();
    std::filesystem::path recording_path(u32 match) const;
    struct RecordingInfo {
        u32 match = 0;
        u64 started = 0;
        std::string map;
        u8 mode = 0;
        u32 seconds = 0;
        u32 bytes = 0;
        bool live = false;
        std::vector<u64> accounts;
        std::vector<std::string> names;
        std::vector<u8> teams;
        bool finished = false;
        u8 winner = u8(Team::None);
        u64 winner_account = 0;
        bool report_waiting = false;   // DS-8: kept until TVAS has the match's report
    };
    void recording_update(const RecordingInfo& info);
    void recording_reported(u32 match);

private:
    template <class T>
    void send_msg(u32 peer, const T& m, bool reliable = true) {
        net_.send(peer, proto::encode(m), reliable);
    }
    void disconnect(u32 peer, const std::string& why);

    void on_message(u32 peer, std::span<const u8> data, double now);
    void on_disconnect(u32 peer);
    void on_foreign(const eng::net::Address& from, std::span<const u8> datagram);

    // Joining (§5.1, ID-4, §9).
    void send_server_info(Peer& p);
    void handle_join(Peer& p, const proto::Join& m);
    void fetch_profile(u32 peer_id, u64 account, bool joining);
    void finish_join(Peer& p);
    std::string gate_refusal(const Account& a) const;
    void welcome(Peer& p);
    std::unordered_map<std::string, u64> nonces_;   // ID-4: tickets seen, until they run out
    void prune_nonces();

    void send_channels(Peer& p);
    void join_channel(Peer& p, u8 id);
    void leave_channel(Peer& p);
    void send_lobby(Peer& p);
    void broadcast_lobby_user(Peer& p);
    void broadcast_room_summary(Room& r);
    proto::RoomSummary summary(const Room& r) const;
    proto::LobbyUserInfo lobby_info(const Peer& p) const;
    const ChannelDef* channel(u8 id) const;

    Room* create_room(Peer& p, RoomSettings s);
    bool join_room(Peer& p, Room& r, bool observer, std::string* why);
    void leave_room(Peer& p, std::string reason);
    void send_room_state(Room& r);
    void remove_room(Room& r);
    Room* find_room(u16 id);
    bool seat(Room& r, Seat& s, Team want);
    void start_match(Room& r, const std::string& map);
    void chat(Peer& p, const proto::Chat& m, double now);

    // Channels (§10): channels.cfg, its editor, a deleted one's lobby told (CH-5).
    void load_channels();
    bool save_channels();
    void channel_edit(Peer& p, const proto::ChannelEdit& m);
    void channels_changed();
    u32 channels_version_ = 1;
    // What the whole server plays (games.cfg beside channels.cfg): its Game Masters' Games tab.
    void load_games();
    bool save_games();
    void send_games(Peer& p);
    void games_edit(Peer& p, const proto::GamesEdit& m);
    std::filesystem::path games_file() const;
    lsf::ServerGames games_;

    // TVAS (§8.2, SO-4, SL-2, EN-3).
    void heartbeat(bool now_please = false);
    void on_heartbeat(const TvasReply& r);
    void run_command(const eng::json::Value& cmd, eng::json::Value& done);
    void tick_presence();
    void reach_checks();
    double last_beat_ = -1000, last_reach_ = -1000;
    bool beat_in_flight_ = false;
    eng::json::Value done_commands_ = eng::json::Value::array();
    eng::json::Value staff_log_ = eng::json::Value::array();
    std::vector<std::string> dropped_items_;   // MN-7: items it sold that it no longer has
    u16 events_sp_pct_ = 100, events_xp_pct_ = 100;
    u32 tier_ = 0;
    bool listed_now_ = false;
    std::string not_listed_why_;
    // SL-2: the list queries this (official) server sent for TVAS, waiting for their answers.
    struct ReachJob {
        u64 server_id = 0;
        eng::net::Address to;
        double sent = 0;
        bool answered = false;
    };
    std::vector<ReachJob> reach_jobs_;
    double list_query_reset_ = 0;
    std::unordered_map<u32, int> list_queries_;   // SL-6: queries per address this second

  public:
    // RL-4: every staff action, for the next heartbeat.
    void staff_logged(const Account& actor, const Account* target, const std::string& action, const std::string& why, const std::string& detail = {});

  private:
    // Staff, reports and votes (Staff.cpp): this server's own (§7.2).
    void handle_staff(Peer& p, proto::Msg kind, std::span<const u8> data);
    void staff_result(u32 peer, bool ok, std::string text);
    std::string ban_refusal(Account& a);
    bool muted(Account& a);
    u32 match_between(const Peer& reporter, const Account& target) const;
    void call_vote(Peer& p, u32 target);
    void cast_vote(Peer& p, bool yes);
    void settle_vote(Room& r, bool timed_out);
    void send_vote(Room& r, const std::string& result = {});
    void tick_votes();
    void tick_rooms();
    bool clan_war_channel(u8 channel) const;
    void clan_room_changed(Room& r);
    void pump_replays();
    void load_recordings();
    void prune_recordings();

    // The shop (Shop.cpp): this server's shop, every sale with the player's approval through TVAS.
    void load_shop();
    bool save_shop();
    void send_shop(Peer& p);
    void handle_shop(Peer& p, proto::Msg kind, std::span<const u8> data);
    void buy(Peer& p, const proto::Buy& m);
    void send_gift(Peer& p, const proto::SendGift& m);
    void custom_capsule(Peer& p, const proto::Request& m);
    void custom_sell(Peer& p, const proto::Request& m);
    void lift_to_floor();              // SH-1, SH-5: base prices below TV's are lifted
    int shop_roll();
    void replay_list(Peer& p);
    void replay_get(Peer& p, u32 match);
    bool send_recording(Peer& p, u32 match, std::string* why);
    ShopConfig shop_;
    ShopConfig tv_shop_;               // TV's own (the floors), from TVAS
    u32 tv_shop_rev_ = 0;
    std::filesystem::path shop_file_;
    std::mt19937 shop_rng_{std::random_device{}()};
    std::atomic<int> shop_roll_{-1};

    // Global chat, presence and room invitations (Social.cpp). Friends, whispers, mail and clans
    // are the game's business with TVAS (SO-1, SO-2).
    void handle_social(Peer& p, proto::Msg kind, std::span<const u8> data);
    std::string presence_of(const Peer& p);
    void global_chat(Peer& p, proto::ChatLine line, double now);
    void room_invite(Peer& p, const proto::RoomInvite& m);
    void room_invite_answer(Peer& p, const proto::RoomInviteAnswer& m);
    void follow_intent(Peer& p);       // FR-2: into a friend's room after a server switch

    // Packs (Content.hpp): the manifest and the downloads (§11.6, §11.7).
    void send_manifest(Peer& p);
    void content_request(Peer& p, const proto::ContentRequest& m);
    void pump_downloads(double now);
    ServerContent content_;
    double download_budget_ = 0, download_last_ = 0;   // DL-1: bytes the upload may still send

public:
    // TVAS's answer to a match's report: each soldier's pay (MatchRewards), their record read again.
    void match_reported(u32 match, const std::map<u64, u32>& sessions, const TvasReply& r);

private:
    ServerOptions options_;
    eng::net::NetServer net_;
    Accounts accounts_;
    TvasLink tvas_;
    std::atomic<i64> clock_offset_{0};
    std::vector<ChannelDef> channels_;
    std::unordered_map<u32, Peer> peers_;
    std::map<u16, std::unique_ptr<Room>> rooms_;
    double now_ = 0;
    double started_ = 0;
    double last_save_ = 0;
    MapCache maps_;
    u32 next_match_ = 1;
    std::map<u32, RecordingInfo> recordings_;

    std::thread thread_;
    std::atomic<bool> thread_run_{false};
    std::mutex mutex_;
    std::vector<std::pair<u32, float>> pending_bots_before_;
    std::vector<std::pair<u32, u8>> pending_bots_pace_;
};

// channels.cfg as the server writes it (§10.1): the Owner's editor saves through it, and a new
// server's folder starts with the original thirteen (--defaults).
bool write_channels_file(const std::filesystem::path& file, const std::vector<lsf::ChannelDef>& channels, lsf::u32 version);

}  // namespace lsfs
