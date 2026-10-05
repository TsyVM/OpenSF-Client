// The game's two connections, and everything they have said.
//
// Team Vanilla (TVAS, Docs/UniversalServerDeploy.md §4.1, §5): the soldier's account. Signing in,
// the server list, tickets, and everything universal -- friends, whispers, mail, clans, the day's
// rewards, TV's own shop services -- go straight there over HTTPS and never through a game server
// (ID-1, SO-1, SO-2).
//
// A game server (official or community): the channel list, a channel's lobby (rooms and users), the
// room, the chat, the match. Joining one is Hello -> ServerInfo -> a ticket from TVAS -> Join -> the
// server's manifest and its packs -> Ready -> Welcome (§5.1). The screens read what is kept here;
// the match's messages queue up for the world (World/GameWorld) to take.
#pragma once

#include "Engine/Net/NetHost.hpp"
#include "Game/Items.hpp"
#include "Game/Net/TvasClient.hpp"
#include "Game/Protocol.hpp"
#include "Game/Registry.hpp"

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lsf {

// A server on Team Vanilla's list (§8.4), or one of the player's own by address (SL-8).
struct ListedServer {
    u64 id = 0;                // 0: known only by its address until it says who it is
    std::string name, region, description, owner, motd;
    std::string address;
    u16 port = kDefaultPort;
    u8 tier = 0;               // proto::Tier
    bool official = false;
    bool locked = false;
    bool local = false;        // "This PC" (SL-9)
    bool direct = false;       // the player's own entry, by address
    u16 players = 0, slots = 0;
    u8 min_rank = 0;
    std::string manifest;      // hex SHA-256 of its manifest (empty: no packs)
    u32 pack_bytes = 0;
    u32 build = 0;
    // What the server itself answered (§8.3): -1 until it has.
    int ping_ms = -1;
    bool answered = false;
    std::string map;           // its busiest room's
    std::string endpoint() const { return address + ":" + std::to_string(unsigned(port)); }
};

// How often TVAS is asked for news (TV-8): the app says which it is.
enum class PollPace : u8 { Chat, Menu, Match };

// Who sees where you are (D25), and who may whisper or mail you (D28).
enum class PresenceMode : u8 { Show = 0, OnlineOnly = 1, Offline = 2 };
enum class MessagesFrom : u8 { Anyone = 0, FriendsAndClan = 1, Friends = 2 };

class Session {
public:
    enum class State { Offline, Connecting, Connected, SignedIn, Failed };
    // How far a join has come, for the sign that says so.
    enum class JoinStage : u8 { None, Hello, Ticket, Joining, Manifest, Consent, Downloading, Mounting, Entering };

    Session();
    ~Session();

    // ── Team Vanilla ────────────────────────────────────────────────────────────
    // TVAS's address (TV-15: a setting), and where the saved sign-in is kept (ID-9).
    void set_tvas(const std::string& address, std::filesystem::path token_file);
    TvasClient& tvas() { return tvas_; }
    // What TVAS says of itself (its sign-in notes, the lowest build, whether it is up at all).
    void tv_status();
    // Username and password, to TVAS only (ID-1, ID-11). `create`: a new account.
    void tv_sign_in(const std::string& username, const std::string& password, bool create, bool remember);
    // The sign-in kept from last time, tried at start ("remember me").
    bool tv_resume();
    void tv_sign_out();
    void tv_away();
    void tv_change_password(const std::string& old_password, const std::string& new_password);
    struct Tv {
        bool signed_in = false;
        bool busy = false;             // a sign-in is on its way
        u64 account = 0;
        std::string username;          // private: shown to nobody else (ID-11)
        std::string code_name;         // empty until one is chosen
        bool free_rename = false;      // an import's clash: a new code name, free (§5.4)
        bool must_change = false;      // a staff reset's temporary password (ID-12)
        u8 global_role = 0;            // proto::GlobalRole
        u32 xp = 0, sp = 0, coins = 0;
        u32 rev = 0;                   // the universal record's version, as TVAS last said
        // The day's XP limit (PR-13).
        u8 levels_left = 25;
        bool xp_capped = false;
        u64 xp_resets = 0;
        u8 presence = 0;               // PresenceMode
        u8 messages_from = 0;          // MessagesFrom
        bool invites = true;
        // TVAS itself.
        bool status_known = false;
        bool down = false;
        bool registration_open = true;
        u32 min_build = 0;
        std::string forgot_password;   // ID-12: where to ask
        std::string news;
        eng::i64 clock_skew = 0;            // TVAS's clock minus this machine's (PR-8)
    } tv;
    bool ev_tv_signed_in = false, ev_tv_refused = false, ev_tv_signed_out = false, ev_tv_password = false;
    std::string tv_error;
    // TVAS's clock now (UTC seconds).
    u64 tv_now() const;
    int rank() const { return rank_for_xp(tv.xp); }

    // The first sign-in's code name and starting force (ID-8), chosen at TVAS.
    void set_code_name(const std::string& name, u8 force);
    bool ev_named = false;

    // The server list (§8): TVAS's signed list (SL-4), official first (D5), then each asked directly
    // for its players and ping (§8.3).
    void request_servers(double now);
    void ping_servers(double now);
    std::vector<ListedServer> servers;
    bool servers_loading = false;
    std::string servers_error;
    double servers_at = -1000;
    // A server of the player's own, by address, asked the same way.
    void ping(ListedServer& s, double now);

    void set_poll_pace(PollPace pace) { pace_ = pace; }
    void set_social(PresenceMode presence, MessagesFrom from);

    // ── A game server ───────────────────────────────────────────────────────────
    // `server`: who it should turn out to be (0: whoever answers, a direct connect: SL-8).
    bool connect(const std::string& address, double now, u64 server = 0, const std::string& password = {});
    void disconnect(std::string_view reason = "left");
    void poll(double now);
    void flush(double now);

    State state() const { return state_; }
    JoinStage join_stage() const { return stage_; }
    bool signed_in() const { return state_ == State::SignedIn; }
    int ping_ms() const;
    const std::string& address() const { return address_; }
    std::optional<proto::ServerInfo> server_info;   // who the server said it is

    // A server's packs on their way (§11.7): what is needed, what has come, and the player's say.
    struct Content {
        struct Need {
            std::string id, sha256;
            u32 size = 0;
            bool cached = false;
        };
        std::vector<Need> packs;
        u64 need_bytes = 0, got_bytes = 0;
        bool ask = false;              // waiting for the player's yes (over the threshold)
        size_t current = 0;
        u32 offset = 0;
        u16 queue = 0;                 // DL-1: place in the server's queue (0: downloading)
        int retries = 0;
        double last_chunk = 0;
    };
    std::optional<Content> content;
    void content_accept();
    void content_decline();
    // Where packs are cached (§11.8), how much may be kept, and over how much to ask first.
    std::filesystem::path cache_dir;
    u64 cache_cap = 512ull << 20;
    u64 ask_over = 5ull << 20;
    double download_timeout = 30.0;   // DL-5: nothing arriving for this long
    // The app's: mounts the session's packs at a safe point (MT-2) and sets the registry (MT-4).
    // With no packs it clears what the last server left (MT-3).
    std::function<bool(const std::vector<std::filesystem::path>& packs, const eng::json::Value& manifest, const std::string& manifest_hash, std::string* why)> mount;

    // ── Requests ────────────────────────────────────────────────────────────────
    void list_channels();
    void join_channel(u8 id);
    void leave_channel();
    void chat(proto::ChatScope scope, const std::string& text, const std::string& to = {});
    void create_room(const RoomSettings& s);
    void join_room(u16 id, const std::string& password = {}, bool observer = false);
    void quick_join();
    void leave_room();
    void change_room(const RoomSettings& s);
    void set_team(Team t);
    void set_ready(bool ready);
    void set_loadout(u8 force, const Loadout& weapons);
    void kick(u32 player);
    void start_match();
    void load_done();
    void leave_match();
    // A sale: the player's own approval from TVAS first (MN-2), then the server's sale with it.
    void buy(proto::ShopKind kind, u16 item, u8 offer = 0);
    // Wear or take off a character part.
    void equip_item(u16 item);
    // An Item Shop item that asks for something (Game/Items.hpp, proto::UseItem).
    void use_item(u16 item, u8 value, const std::string& text = {});
    void recharge_sp();
    void request_clan();
    void create_clan(const std::string& name, const std::string& notice, const ClanMark& mark);
    void set_clan_mark(const ClanMark& mark);   // the Owner or a Co-Owner: the clan's mark
    void leave_clan();
    void clan_invite(const std::string& code_name);
    void clan_answer(const std::string& clan, bool accept);
    void clan_kick(const std::string& code_name);
    // Ranks, the notice, applications, handing over, disbanding (proto::ClanOp).
    void clan_action(proto::ClanOp op, const std::string& code_name, const std::string& text = {});
    void browse_clans();
    // Friends and the inbox (TVAS: §14.2).
    void request_friends();
    void friend_action(proto::FriendOp op, const std::string& code_name);
    void request_mail();
    void mail_action(proto::MailOp op, u32 id = 0);
    void send_mail(const std::string& to, const std::string& text);
    // A friend on another server joined (D24, FR-2): TVAS says where; the app asks, then switches.
    void join_friend(const std::string& code_name);
    struct Switch {
        ListedServer server;
        std::string friend_name;
        u8 channel = 0;
        u16 room = 0;
        bool invitation = false;       // FR-4: a room's invitation rather than a Join
    };
    std::optional<Switch> switch_request;
    // Global chat heard or not: told to the server now and again at every sign-in.
    void set_global_chat(bool on);
    bool global_chat() const { return global_chat_; }
    // A room's invitations: taken or not (told like global chat), one sent from your room by
    // code name, and yours answered.
    void set_room_invites(bool on);
    bool takes_room_invites() const { return room_invites_; }
    void room_invite(const std::string& code_name);
    void room_invite_answer(u16 room, bool accept);
    // Reports, votes and this server's staff (proto Staff*): the panel's requests.
    void report(u32 player, const std::string& code_name, proto::ReportReason reason, const std::string& note);
    void call_vote(u32 target);
    void cast_vote(bool yes);
    void staff_list_reports(bool open_only);
    void staff_resolve(u32 report, proto::ReportState state);
    void staff_action(const std::string& account, proto::StaffOp op, u32 seconds, const std::string& reason, proto::Role role = proto::Role::Player);
    void staff_find(const std::string& name);
    void staff_edit(const std::string& account, proto::AccountField field, u32 value, const std::string& text);
    void staff_list_recordings();
    void request_replay(u32 match);
    // SFLegacy Staff's own (RL-1): global bans, mutes and edits go from the game to TVAS, never
    // through a server. Answers come back as staff_results and staff_card like a server's.
    void global_staff_find(const std::string& name);
    void global_staff_action(const std::string& account, proto::StaffOp op, u32 seconds, const std::string& reason, u8 role = 0);
    void global_staff_edit(const std::string& account, proto::AccountField field, u32 value, const std::string& text);
    void global_staff_reset_password(const std::string& account, const std::string& checked);
    void global_staff_servers();
    void global_staff_server(u64 server, const std::string& op, const std::string& reason, u32 value = 0);
    // EN-3: a command the server collects with its next heartbeat (broadcast, unlist, kick, close_room, end_match).
    void global_staff_command(u64 server, const std::string& cmd, const std::string& text, u64 account = 0, u16 room = 0);
    struct StaffServer {
        u64 id = 0;
        std::string name, owner, address, note;
        u8 tier = 0, state = 0;
        u16 players = 0;
        u64 last_beat = 0;
        bool listed = false;
    };
    std::optional<std::vector<StaffServer>> staff_servers;
    // The Owner's and Server Admins' (§10.2, ML-8).
    void channel_edit(proto::ChannelOp op, const proto::ChannelInfo& channel);
    void motd_edit(const std::string& motd);
    void games_edit(const ServerGames& games);   // the server's Game Masters (GamesEdit)
    // A soldier's ID card by code name (into `id_card`), and your own card's line.
    void request_id_card(const std::string& code_name);
    void set_card_message(const std::string& text);
    // Rewards and events (Game/Events.hpp): Team Vanilla's, the same on every server (MN-5).
    void request_rewards();
    void claim_reward(proto::ClaimKind kind, u8 index = 0);
    void open_box(u8 box);
    void staff_request_rewards();
    void staff_save_rewards(const RewardsConfig& config);
    // The shop (Game/Shop.hpp, Server/Shop.cpp): sent at sign-in and whenever its staff change it;
    // asked for again here. The spray carried, capsule coins and a turn of a capsule: Team
    // Vanilla's own services, asked of TVAS; a server's own capsules, of the server.
    void request_shop();
    // A gun mended to full, and a gun, a force or a part sold back (Game/Wear.hpp).
    // Something from the shop bought for a friend (a code name on your list), and a gift opened.
    void send_gift(proto::ShopKind kind, u16 item, u8 offer, const std::string& to);
    void open_gift(u32 gift);
    // Horror Mode's items bought (Game/Rules.hpp HorrorItem), and a Supply Crate bought and opened (7 or 30 days).
    void buy_horror_item(HorrorItem item, u32 how_many = 1);
    void supply_crate(u16 days);
    // The shop's Duffle Bags (ShopConfig::bags): bought into the Gift tab, and one opened.
    void buy_bag(u16 bag, u32 how_many = 1);
    void open_bag(u16 bag);
    void repair(u16 weapon);
    void sell(proto::ShopKind kind, u16 id);
    void set_spray(u16 spray_id);
    void buy_coins(u32 n);
    void turn_capsule(u16 capsule);
    void staff_save_shop(const ShopConfig& config);
    // Your own matches' recordings: the list, and one of them (into `replay`, as staff's come).
    void list_replays();
    void get_replay(u32 match);
    template <class T>
    void send(const T& m, bool reliable = true) {
        client_.send(proto::encode(m), reliable);
    }

    // ── What the server said ────────────────────────────────────────────────────
    u32 session_id = 0;
    std::string server_name, motd;
    std::string refused;                     // why the last join failed
    bool has_profile = false;
    proto::Profile profile;
    std::vector<u16> owned_weapons;
    std::vector<u16> owned_forces;
    // Items (Game/Items.hpp): what is owned, with the seconds it had left when the list came
    // (`items_at`, the session's clock), and the parts worn.
    std::vector<proto::OwnedItem> owned_items;
    double items_at = 0;
    std::vector<u16> equipped;
    std::vector<proto::ChannelInfo> channels;
    u32 channels_version = 0;
    bool channels_editable = false;
    // What the server plays (its Game Masters' Games tab): game types and maps switched off.
    lsf::ServerGames games;
    bool games_editable = false;
    // The game types a room in the joined channel may be made or changed to: the channel's, less
    // the server's switched off.
    u16 modes_allowed() const;
    u8 channel = 0;
    std::string channel_name;
    std::map<u16, proto::RoomSummary> rooms;
    std::map<u32, proto::LobbyUserInfo> users;
    std::optional<proto::RoomState> room;
    struct ChatEntry {
        proto::ChatLine line;
        double time = 0;
    };
    std::deque<ChatEntry> lobby_chat, room_chat;
    std::optional<proto::MatchLoad> match_load;   // set when a match is to load; the app clears it
    std::deque<std::vector<u8>> match_events;     // in-match messages, in order, for the world
    std::optional<proto::MatchOver> match_over;
    // TVAS's word on a match's pay, when it came after the results were shown (PR-3, PR-7).
    std::optional<proto::MatchRewards> match_rewards;

    // One-shot events for the app's screen flow (cleared by whoever acts on them).
    bool ev_welcomed = false, ev_refused = false, ev_channels = false, ev_channel_joined = false, ev_room_joined = false,
         ev_room_left = false, ev_disconnected = false;
    std::string room_left_reason, disconnect_reason;
    struct NoticeEntry {
        proto::NoticeKind kind;
        std::string text;
    };
    std::deque<NoticeEntry> notices;
    std::deque<proto::ShopResult> shop_results;
    std::optional<proto::ClanState> clan;        // your clan as TVAS last told it (member = false: none)
    std::deque<proto::ClanInvited> clan_invites; // waiting for a yes or a no
    std::optional<proto::ClanDirectory> clan_directory;   // the clans there are (browse_clans)
    // Your friends, the requests and whom you block, where each friend is; your inbox.
    std::optional<proto::FriendList> friends;
    std::optional<proto::Mailbox> mailbox;
    std::string last_whisper_from;               // who whispered you last (/r answers them)
    std::deque<proto::RoomInvited> room_invites; // rooms you are asked into, waiting for a yes or a no
    int friend_requests() const;                 // asking you, waiting
    int unread_mail() const;
    // Staff: what the panel asked for, as it came back.
    std::deque<proto::StaffResult> staff_results;
    std::optional<proto::StaffReportList> staff_reports;
    std::optional<proto::StaffAccountCard> staff_card;
    std::optional<proto::StaffRecordingList> staff_recordings;
    // A match recording on its way (ReplayChunk): done once every byte is in, an error if it cannot come.
    struct ReplayDownload {
        u32 match = 0;
        u32 total = 0;
        u32 got = 0;
        std::vector<u8> bytes;
        bool done = false;
        std::string error;
    };
    std::optional<ReplayDownload> replay;
    // The vote running on your side (Esc -> Kick a player), and when it was told.
    std::optional<proto::VoteState> vote;
    double vote_at = 0;
    // The day's rewards, your boxes and the events, and the app's clock when they came (TVAS's
    // `now` then: countdowns run from it). The last box opened, until it is shown.
    std::optional<proto::RewardsState> rewards;
    double rewards_at = 0;
    std::optional<proto::BoxOpened> box_opened;
    // SFLegacy Staff's copy of the rewards and events, to edit.
    std::optional<proto::StaffRewards> staff_rewards;
    double staff_rewards_at = 0;
    // The shop as the server keeps it (nullopt until it has said), and what you have of it: the
    // guns rented or worked on, the forces rented, the sprays, the one carried, capsule coins. The
    // seconds left count down from `items_at` like the items'.
    std::optional<ShopConfig> shop;
    std::vector<proto::OwnedGun> owned_guns;
    std::vector<proto::OwnedItem> force_time, owned_sprays;
    u16 spray = kNoSpray;
    u32 coins = 0;
    std::vector<proto::GiftInfo> gifts;   // friends' gifts waiting in the Gift tab
    std::array<u16, kHorrorItems> horror_items{};   // Horror Mode's items: how many of each you carry
    std::vector<proto::OwnedItem> bags;             // the shop's Duffle Bags bought, unopened (uses: how many)
    // What a request to the shop came to (mending, selling back, coins, a gift), until the app
    // shows it as a toast.
    std::deque<proto::ServiceResult> service_results;
    // The ID card last asked for (nullopt until TVAS answers).
    std::optional<proto::IdCard> id_card;
    std::deque<proto::CapsuleResult> capsule_results;
    // The matches you played that the server still keeps, and when the list came.
    std::optional<proto::ReplayList> replays;
    double replays_at = 0;
    // The shop's catalog: the server's, or the game's own until it comes.
    const ShopConfig& shop_config() const;
    ShopLine shop_line(ShopKind kind, u16 id) const { return lsf::shop_line(shop_config(), kind, id); }
    // Team Vanilla's own shop as TVAS has it (/v1/tvshop): its Duffle Bags, its capsules and its coin
    // price are what TVAS sells, on whichever server you are. Only the official server's Game Masters
    // change them (its shop is pushed to TVAS); another server's copy of them is never sold.
    // `refresh_tv_shop` asks again when the copy here is older than a minute.
    std::optional<ShopConfig> tv_shop;
    void refresh_tv_shop();
    bool on_official_server() const { return server_info && server_info->tier == u8(proto::Tier::Official); }
    // The Duffle Bags TVAS sells, and one of them (null: TVAS does not know it).
    const std::vector<CapsuleDef>& bags_on_sale() const;
    const CapsuleDef* bag_def(u16 id) const;
    // The capsule machine: TV's capsules (TVAS turns them) and this server's own (kCustomCapsuleFirst
    // and up, turned through the server), and what a coin costs (TV's price).
    std::vector<const CapsuleDef*> machine_capsules() const;
    const CapsuleDef* machine_capsule(u16 id) const;
    u32 coin_price() const { return tv_shop ? tv_shop->coin_price : shop_config().coin_price; }
    // What is left of a gun (kDurabilityFull for one that never wears), what mending it costs
    // and what selling it back brings, as the shop stands.
    u8 durability_of(u16 weapon_id) const;
    u32 repair_price(u16 weapon_id) const;
    u32 resale_of(proto::ShopKind kind, u16 id) const;
    // Seconds left on a weapon, a force, a spray (OwnedItem::kForGood for good, 0 not owned).
    u32 weapon_seconds_left(u16 id, double now) const;
    u32 force_seconds_left(u8 id, double now) const;
    u32 spray_seconds_left(u16 id, double now) const;

    // The clock rentals, days and events run by: TVAS's (PR-8), never a server's or this PC's.
    u64 server_now(double now) const;

    bool owns_weapon(u16 id) const;
    bool owns_force(u8 id) const;
    const proto::OwnedItem* owned_item(u16 id) const;
    // Seconds an item still has (OwnedItem::kForGood for good), counted down since the list came.
    u32 item_seconds_left(u16 id, double now) const;
    bool wears(u16 id) const;
    // The parts worn for `force`.
    std::vector<u16> worn(u8 force) const;
    // A boost bought and not run out (a Special Package counts for what it holds).
    bool has_boost(Boost b, double now) const;
    const proto::RoomMember* me_in_room() const;
    bool is_host() const;

private:
    void on_message(std::span<const u8> data, double now);
    void fail_join(std::string why);
    void add_chat(proto::ChatLine line, double now);

    // Team Vanilla (SessionTvas.cpp).
    void tv_signed(const eng::json::Value& signin, bool remember);
    void tv_apply_me(const eng::json::Value& me);
    void tv_apply_status(const eng::json::Value& status);
    void tv_lost();                        // TVAS says the sign-in has run out
    void tv_poll(double now);
    void tv_event(const std::string& kind, const eng::json::Value& data, double now);
    // After anything that changed the universal record: the server reads it again (proto::Refresh).
    void changed(const eng::json::Value& reply);
    void want_refresh(u32 rev);
    void ask_ticket();
    using Json = eng::json::Value;
    void tv_post(const std::string& path, const Json& body, std::function<void(const TvReply&)> done);
    void simple_service(proto::RequestOp op, const std::string& path, const Json& body);
    void clan_post(Json body);
    void apply_clan(const Json& state);
    void apply_friends(const Json& list);
    void apply_mailbox(const Json& box);
    void approve_then(const Json& what, u32 amount, u8 currency, std::function<void(const std::string& approval)> then, std::function<void(std::string why)> refused);

    // Packs (§11.6 to §11.9).
    void manifest_part(const proto::ManifestPart& m, double now);
    void manifest_ready(double now);
    void next_download(double now);
    void content_chunk(const proto::ContentChunk& m, double now);
    void content_done(double now);
    void prune_cache();

    TvasClient tvas_;
    std::filesystem::path token_file_;
    PollPace pace_ = PollPace::Menu;
    std::array<double, 3> intervals_{3.0, 6.0, 15.0};
    double last_poll_ = -1000, now_ = 0;
    double tv_shop_asked_ = -1000;   // refresh_tv_shop's last ask
    bool poll_in_flight_ = false;
    u64 cursor_ = 0;
    bool first_poll_ = true;
    u32 refresh_rev_ = 0;                  // the newest version the server should read
    double refresh_sent_ = -1000;
    std::vector<std::string> blocked_packs_;   // RT-3: hashes taken down, from the ticket's answer

    eng::net::NetClient client_;
    eng::net::UdpSocket query_socket_;
    struct Query {
        eng::net::Address to;
        u64 nonce = 0;
        double sent = 0;
        u64 server = 0;                    // the listed server's id, or 0 for a direct one
        std::string endpoint;
    };
    std::vector<Query> queries_;
    State state_ = State::Offline;
    JoinStage stage_ = JoinStage::None;
    std::string address_;
    u64 expect_server_ = 0;
    std::string join_password_;
    std::vector<std::vector<u8>> manifest_parts_;
    std::vector<u8> manifest_bytes_;
    Json manifest_;
    std::vector<u8> pack_buffer_;
    bool global_chat_ = true;
    bool room_invites_ = true;
};

}  // namespace lsf
