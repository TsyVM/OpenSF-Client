// Every message the client and the server exchange, over Engine/Net's reliable UDP.
//
// Each message is a struct with one `serialize(S&)` used for both directions, so a field can
// never be written in one order and read in another (TacticalFPS's pattern). On the wire a
// message is its one-byte id followed by its fields.
//
// The flow is the original's (Docs/Research.md §2), with Team Vanilla accounts in front of it
// (Docs/UniversalServerDeploy.md §5.1): Hello -> ServerInfo -> Join (a ticket TVAS signed) ->
// the server's manifest and its packs -> Ready -> Welcome, the channel list -> a channel's lobby
// (rooms, users, chat) -> a room (teams, map, ready) -> loading -> the match -> results -> back to
// the room. Passwords never come here (ID-1): the game signs in to TVAS over HTTPS. Friends,
// whispers, mail, clans and TV's own shop services are the game's business with TVAS too (SO-1).
#pragma once

#include "Engine/Core/ByteStream.hpp"
#include "Game/Events.hpp"
#include "Game/Wear.hpp"
#include "Game/Rules.hpp"
#include "Game/Shop.hpp"

#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace lsf::proto {

using eng::i16;
using eng::i8;
using eng::Vec3;

// Numbers are kept for ever: a message that is no longer sent keeps its number, unused (marked
// "retired"), so a newer build never reads an older one's message as something else.
enum class Msg : u8 {
    // client -> server
    Hello = 1,       // the protocol, the build, the platform; the server answers ServerInfo
    Login,           // retired: sign-in is TVAS's (ID-1)
    Register,        // retired
    SetCodeName,     // retired: the code name is chosen at TVAS
    ListChannels,
    JoinChannel,
    LeaveChannel,
    Chat,
    CreateRoom,
    JoinRoom,
    QuickJoin,
    LeaveRoom,
    ChangeRoom,      // host: the room's settings
    SetTeam,
    SetReady,
    SetLoadout,      // force and the four weapon slots
    KickPlayer,
    StartMatch,      // host
    LoadDone,
    Input,           // unreliable, every tick: where I am and what I am doing
    Shoot,           // a shot and what it hit (the server checks it)
    Throw,           // a grenade leaves the hand
    Radio,
    LeaveMatch,
    Buy,
    Equip,
    RechargeSp,
    ClanRequest,     // my clan, as it stands
    CreateClan,
    LeaveClan,
    ClanInvite,      // master: ask a soldier in
    ClanAnswer,      // yes or no to an invitation
    ClanKick,        // master: put a member out
    UseItem,         // an Item Shop item that asks for something: a new code name, a colour, a rank mark
    ReportPlayer,    // anyone: a soldier to staff's queue, with a reason
    CallVote,        // a vote to put a teammate out of the room
    CastVote,        // yes or no to the vote running on your side
    StaffListReports,
    StaffResolveReport,
    StaffAction,     // kick, mute, unmute, ban, unban, set rank
    StaffFindAccount,
    StaffEditAccount,   // Game Master: code name, SP, XP, rank, what is owned
    StaffListRecordings,
    StaffRequestReplay,
    ClanSetMark,     // master: the clan's mark (its own emblem, or Soldier Front's pieces)
    RewardsRequest,      // the day's rewards and the events, as they stand
    RewardsClaim,        // a sign-in, an hour of play, a quest, the day's quests all done
    OpenBox,             // a duffle bag or a gift box from the Event tab
    StaffRewardsRequest, // Game Master: the rewards and events to edit
    StaffRewardsSave,    // Game Master: all of them, edited
    UseSkill,            // an undead's skill (Game/Modes.hpp Skill)
    PickClass,           // Horror Mode 2: the undead's class for the next life
    FriendAction,        // ask, accept, decline, cancel, remove, block, unblock (Social.cpp)
    MailAction,          // read, delete: the inbox of messages left while you were away
    ChatPrefs,           // global chat heard or not
    ClanAction,          // ranks, the notice, applications, open or closed, handing over, disbanding
    ClanBrowse,          // the clans there are, to apply to one
    RoomInvite,          // from a room: ask a soldier in, by code name
    RoomInviteAnswer,    // yes or no to a room's invitation
    Request,             // the shop asked for; a spray, capsule coins, a capsule, mending, selling back; your matches' recordings
    Spray,               // in a match: your spray on the wall in front of you
    StaffShopSave,       // Game Master: the shop, edited (Game/Shop.hpp)

    // server -> client
    Welcome = 64,    // accepted; the profile follows
    Refused,         // why a Hello/Login/Register failed
    Profile,
    Notice,          // a line for a toast
    ChannelList,
    ChannelJoined,
    LobbyState,      // everyone in the channel's lobby and every room
    RoomSummaryMsg,  // one room changed (or appeared)
    RoomRemoved,
    LobbyUser,       // one lobby user changed (or arrived)
    LobbyUserLeft,
    ChatLine,
    RoomState,       // the room you are in, whole
    RoomLeft,
    MatchLoad,       // everyone load this map
    MatchBegin,
    Snapshot,        // unreliable: everyone's position and state
    Spawn,
    Damage,
    Kill,
    ShotFx,          // someone else fired (tracer, muzzle flash, sound)
    GrenadeFx,
    RoundStart,
    RoundEnd,
    Score,
    MatchOver,
    Inventory,
    ShopResult,
    ClanStateMsg,    // your clan (or that you have none)
    ClanInvited,     // an officer asks you in
    RadioFx,         // a teammate's radio line: say it in their voice
    StaffResult,     // what a staff action (or a report) came to
    StaffReportList,
    StaffAccountCard,
    StaffRecordingList,
    ReplayChunk,     // a piece of a match recording on its way to staff
    VoteState,       // the vote running on your side (or that it ended, and how)
    RewardsStateMsg,     // the day's rewards, your boxes, the events
    BoxOpened,           // what a box held
    StaffRewardsMsg,     // the rewards and events, for a Game Master to edit
    ModeStateMsg,        // the game type's state: objectives, roles, counts (Game/Modes.hpp)
    ModeEventMsg,        // something the game type did: a bomb set, an item taken, a soldier infected
    FriendListMsg,       // your friends (and requests, and whom you block), where each one is
    MailboxMsg,          // your inbox
    ClanDirectoryMsg,    // the clans there are
    RoomInvited,         // a soldier in a room asks you in
    ShopStateMsg,        // the shop as it stands: the catalog's changes, the prices, the capsules, the lobby's line
    ServiceResult,       // what a request to the shop came to (mending, selling back, coins, a gift opened)
    CapsuleResult,       // what a capsule held
    SprayFx,             // a soldier's spray on a wall
    ReplayListMsg,       // the matches you played that this server still keeps a recording of
    Rearm,               // a soldier's weapon slot changed mid-life: a gun put down or taken up
    IdCardMsg,           // a soldier's ID card: who they are, their record, what they wear and carry
    SpecialPointMsg,     // in a match: you did one of the round's challenges (Rules.hpp Special)
    HorrorItemUsed,      // in a Horror game: a soldier used an item (or his Rebirth went)
    CannonFx,            // Pirate Mode: a cannon fired: its ball, to be flown
    ServerInfo,          // who this server is and what it asks before anyone signs in: its id, gates, its packs' size
    ManifestPart,        // a piece of the server's manifest (§11.6): its packs and what they add, with this session's numbers
    ContentChunk,        // a piece of one of the server's packs (§11.7), or why it cannot be sent
    MatchRewards,        // TVAS's answer for a match's pay, when it came after MatchOver (PR-3, PR-7)
    GamesState,          // the game types and maps this server has switched off (Rules.hpp ServerGames)

    // client -> server, continued (the first block reached the server's at 64)
    DropWeapon = 160,    // in a match: the gun in a slot on the floor
    PickUpWeapon,        // in a match: the gun at your feet into its slot
    IdCardRequest,       // a soldier's ID card by code name; or your own card's line, written
    SendGift,            // something from the shop, bought for a friend: it waits in their Gift tab
    UseHorrorItem,       // in a Horror game: one of the items you carry (Game/Modes.hpp HorrorItem), used
    CannonUse,           // Pirate Mode: a cannon manned, left or fired
    Join,                // the ticket TVAS signed for this server (ID-3): who you are, here
    Ready,               // the manifest's packs are mounted: in to the channel list
    Refresh,             // your record changed at TVAS (a purchase, a clan, a loadout): read it again
    ContentRequest,      // one of the manifest's packs, from an offset (DL-2), or the download cancelled
    ChannelEdit,         // the Owner or a Server Admin: a channel made, changed, moved or deleted (§10.2)
    MotdEdit,            // the Owner or a Server Admin: the message of the day (ML-8)
    GamesEdit,           // a Game Master: the game types and maps switched off on this server
};

const char* message_name(Msg id);

// ── Serializers ────────────────────────────────────────────────────────────────

class Out {
public:
    static constexpr bool reading = false;
    explicit Out(eng::ByteWriter& w) : w_(w) {}
    void u8(eng::u8& v) { w_.u8(v); }
    void i8(eng::i8& v) { w_.i8(v); }
    void u16(eng::u16& v) { w_.u16(v); }
    void i16(eng::i16& v) { w_.i16(v); }
    void i32(eng::i32& v) { w_.i32(v); }
    void u32(eng::u32& v) { w_.u32(v); }
    void u64(eng::u64& v) { w_.u64(v); }
    void f32(float& v) { w_.f32(v); }
    void boolean(bool& v) { w_.boolean(v); }
    void varu(eng::u32& v) { w_.varu(v); }
    void vec3(Vec3& v) { w_.vec3(v); }
    void angle(float& v) { w_.angle16(v); }
    void str(std::string& s, size_t max) { w_.string(s.size() > max ? std::string_view(s).substr(0, max) : s); }
    template <typename T>
    void list(std::vector<T>& v, size_t max) {
        eng::u32 n = eng::u32(std::min(v.size(), max));
        w_.varu(n);
        for (eng::u32 i = 0; i < n; ++i) v[i].serialize(*this);
    }
    void words(std::vector<eng::u16>& v, size_t max) {
        eng::u32 n = eng::u32(std::min(v.size(), max));
        w_.varu(n);
        for (eng::u32 i = 0; i < n; ++i) w_.u16(v[i]);
    }
    template <typename T, size_t N>
    void array(std::array<T, N>& a) {
        for (auto& e : a) raw_one(e);
    }
    void blob(std::vector<eng::u8>& v, size_t max) {
        eng::u32 n = eng::u32(std::min(v.size(), max));
        w_.varu(n);
        w_.bytes(std::span<const eng::u8>(v.data(), n));
    }

private:
    void raw_one(eng::u16& v) { w_.u16(v); }
    void raw_one(eng::u8& v) { w_.u8(v); }
    eng::ByteWriter& w_;
};

class In {
public:
    static constexpr bool reading = true;
    explicit In(eng::ByteReader& r) : r_(r) {}
    void u8(eng::u8& v) { v = r_.u8(); }
    void i8(eng::i8& v) { v = r_.i8(); }
    void u16(eng::u16& v) { v = r_.u16(); }
    void i16(eng::i16& v) { v = r_.i16(); }
    void i32(eng::i32& v) { v = r_.i32(); }
    void u32(eng::u32& v) { v = r_.u32(); }
    void u64(eng::u64& v) { v = r_.u64(); }
    void f32(float& v) {
        v = r_.f32();
        if (!std::isfinite(v)) {
            v = 0;
            failed_ = true;
        }
    }
    void boolean(bool& v) { v = r_.u8() != 0; }
    void varu(eng::u32& v) { v = eng::u32(r_.varu()); }
    void vec3(Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void angle(float& v) {
        v = r_.angle16();
        if (v >= 180.0f) v -= 360.0f;
    }
    void str(std::string& s, size_t max) { s = r_.string(max); }
    template <typename T>
    void list(std::vector<T>& v, size_t max) {
        eng::u64 n = r_.varu();
        if (n > max || n > r_.remaining()) {
            failed_ = true;
            v.clear();
            return;
        }
        v.resize(size_t(n));
        for (auto& e : v) e.serialize(*this);
    }
    void words(std::vector<eng::u16>& v, size_t max) {
        eng::u64 n = r_.varu();
        if (n > max || n * 2 > r_.remaining()) {
            failed_ = true;
            v.clear();
            return;
        }
        v.resize(size_t(n));
        for (auto& e : v) e = r_.u16();
    }
    template <typename T, size_t N>
    void array(std::array<T, N>& a) {
        for (auto& e : a) raw_one(e);
    }
    void blob(std::vector<eng::u8>& v, size_t max) {
        eng::u64 n = r_.varu();
        if (n > max || n > r_.remaining()) {
            failed_ = true;
            v.clear();
            return;
        }
        v.resize(size_t(n));
        if (n) r_.raw(v.data(), size_t(n));
    }
    bool ok() const { return r_.ok() && !failed_; }

private:
    void raw_one(eng::u16& v) { v = r_.u16(); }
    void raw_one(eng::u8& v) { v = r_.u8(); }
    eng::ByteReader& r_;
    bool failed_ = false;
};

template <typename T>
std::vector<u8> encode(const T& message) {
    eng::ByteWriter w(64);
    w.u8(u8(T::kId));
    Out out(w);
    const_cast<T&>(message).serialize(out);
    return w.take();
}

inline Msg peek_id(std::span<const u8> data) { return data.empty() ? Msg(0) : Msg(data[0]); }

template <typename T>
bool decode(std::span<const u8> data, T& message) {
    if (data.empty() || data[0] != u8(T::kId)) return false;
    eng::ByteReader r(data.data() + 1, data.size() - 1);
    In in(r);
    message.serialize(in);
    return in.ok() && r.at_end();
}

inline constexpr size_t kNameMax = 16;
inline constexpr size_t kChatMax = 120;
inline constexpr size_t kTicketMax = 4096;

// The server list's query (§8.3), outside any connection: a datagram with this magic and a nonce,
// padded to the answer's size (SL-5: the answer is never bigger than the question), answered with
// kAnswerMagic, the nonce, the protocol, the build, players, slots, the lowest rank, the packs'
// size, whether it is locked, its busiest room's map, its name and its manifest's hash.
inline constexpr u32 kQueryMagic = 0x51465351;    // "QSFQ"
inline constexpr u32 kAnswerMagic = 0x41465351;   // "QSFA"
inline constexpr size_t kAnswerSize = 256;

// ── Joining a server (Docs/UniversalServerDeploy.md §5.1) ──────────────────────

// Values only ever go on the end (item 44 of §16): an older build reads the ones it knows.
enum class Platform : u8 { Windows = 0, Android = 1, MacOS = 2, Linux = 3 };
const char* platform_name(u8 platform);
// What a game can read, so a server can tell it the truth before it downloads anything.
enum Capabilities : u32 { kCapPacks = 1 << 0, kCapGles = 1 << 1, kCapD3D = 1 << 2 };

struct Hello {
    static constexpr Msg kId = Msg::Hello;
    u32 version = kProtocolVersion;
    u8 platform = 0;
    u32 build = kBuildNumber;
    u32 caps = 0;
    template <typename S> void serialize(S& s) { s.u32(version), s.u8(platform), s.u32(build), s.u32(caps); }
};

// A server's own tier on Team Vanilla's list (PR-5).
enum class Tier : u8 { Unverified = 0, Verified = 1, Official = 2 };

// The server, before anyone is signed in: who it is (so the game can ask TVAS for a ticket for it,
// and only for it: ID-4), what it asks of a soldier, and what joining it will download.
struct ServerInfo {
    static constexpr Msg kId = Msg::ServerInfo;
    u64 server_id = 0;
    std::string name;
    std::string motd;
    u8 tier = 0;                 // Tier
    bool no_progress = false;    // a player's own "This PC" (D41): no rank, XP, stats or SP here
    u8 min_rank = 0, max_rank = 0xFF;   // the gate (§9; 0xFF: none)
    float min_kd = 0;
    std::string manifest;        // hex SHA-256 of the manifest (empty: no packs)
    u32 pack_bytes = 0;          // the packs together (DL-6: at most 50 MB)
    u32 build = 0;
    u16 players = 0, slots = 0;
    template <typename S> void serialize(S& s) {
        s.u64(server_id), s.str(name, 64), s.str(motd, 512), s.u8(tier), s.boolean(no_progress), s.u8(min_rank), s.u8(max_rank), s.f32(min_kd);
        s.str(manifest, 64), s.u32(pack_bytes), s.u32(build), s.u16(players), s.u16(slots);
    }
};

// The ticket: TVAS's signed word that this account may join this one server, for a minute or two
// (ID-3). The server checks it itself (ID-4) and asks nothing else of the game about who it is.
struct Join {
    static constexpr Msg kId = Msg::Join;
    std::string ticket;
    std::string password;        // a private server's (server.cfg `password`), else empty
    template <typename S> void serialize(S& s) { s.str(ticket, kTicketMax), s.str(password, 32); }
};

// The manifest (§11.6), in pieces when it is over a message (PK-9): JSON, the packs and what they add.
inline constexpr size_t kManifestPiece = 48 * 1024;
struct ManifestPart {
    static constexpr Msg kId = Msg::ManifestPart;
    u16 index = 0, count = 1;
    std::vector<u8> bytes;
    template <typename S> void serialize(S& s) { s.u16(index), s.u16(count), s.blob(bytes, kManifestPiece); }
};

struct Ready {
    static constexpr Msg kId = Msg::Ready;
    std::string manifest;        // the manifest's hash, as mounted
    template <typename S> void serialize(S& s) { s.str(manifest, 64); }
};

// Your record changed at TVAS: the server reads it again when its copy is older than `rev`.
struct Refresh {
    static constexpr Msg kId = Msg::Refresh;
    u32 rev = 0;
    template <typename S> void serialize(S& s) { s.u32(rev); }
};

// A pack from the server itself (D43), in pieces paced like recordings (§11.7, DL-1, DL-2).
inline constexpr size_t kContentChunk = 24 * 1024;
struct ContentRequest {
    static constexpr Msg kId = Msg::ContentRequest;
    std::string sha256;
    u32 offset = 0;
    bool cancel = false;
    template <typename S> void serialize(S& s) { s.str(sha256, 64), s.u32(offset), s.boolean(cancel); }
};

struct ContentChunk {
    static constexpr Msg kId = Msg::ContentChunk;
    std::string sha256;
    u32 total = 0, offset = 0;
    std::vector<u8> bytes;
    std::string error;           // set (and nothing else) when it cannot be sent
    u16 queue = 0;               // DL-1: your place in the queue of players waiting to download (0: downloading)
    template <typename S> void serialize(S& s) { s.str(sha256, 64), s.u32(total), s.u32(offset), s.blob(bytes, kContentChunk), s.str(error, 200), s.u16(queue); }
};

struct Welcome {
    static constexpr Msg kId = Msg::Welcome;
    u32 session = 0;
    std::string server_name;
    std::string motd;
    template <typename S> void serialize(S& s) { s.u32(session), s.str(server_name, 64), s.str(motd, 512); }
};

struct Refused {
    static constexpr Msg kId = Msg::Refused;
    std::string reason;
    template <typename S> void serialize(S& s) { s.str(reason, 200); }
};

// The player's own record: what the waiting room's "my info" box shows. Rank, XP, SP, the record
// and what is worn are universal (TVAS's, §6.1); what this server adds (its own items, the slots
// filled with them) is the server's (§6.2). `role` is what this soldier may do here: the higher of
// their role on this server and their SFLegacy role.
struct Profile {
    static constexpr Msg kId = Msg::Profile;
    u64 account_id = 0;      // the TV account (ID-7): permanent, whatever the code name
    std::string code_name;
    u32 xp = 0;
    u32 sp = 0;
    u32 kills = 0, deaths = 0, wins = 0, losses = 0, headshots = 0, matches = 0;
    u8 force = 0;
    Loadout loadout = kNoLoadout;
    u8 sp_charge = 0;        // 0..100, the recharge bar
    std::string clan;
    ClanMark clan_mark;
    u8 role = 0;             // Role: a Moderator or Game Master is shown the staff panel (F9); a
                             // Game Master may also lock a room and carries the staff variants
    u8 name_colour = 0, clan_colour = 0;
    u8 fake_rank = 0xFF;     // the rank a Fake Rank Mark shows, 0xFF none
    // The rest of the record the ID card shows (Server/Accounts.hpp): games left before their end,
    // teammates killed, objectives done, days signed in; rounds played and lived through; shots
    // fired and those that hit.
    u32 forfeits = 0, team_kills = 0, missions = 0, attended = 0;
    u32 rounds = 0, survived = 0;
    u64 shots = 0, hits = 0;
    u8 server_role = 0;      // Role on this server
    u8 global_role = 0;      // GlobalRole
    u32 rev = 0;             // the universal record's version this was made from (ID-3)
    template <typename S> void serialize(S& s) {
        s.u64(account_id);
        s.str(code_name, kNameMax);
        s.u32(xp), s.u32(sp);
        s.u32(kills), s.u32(deaths), s.u32(wins), s.u32(losses), s.u32(headshots), s.u32(matches);
        s.u8(force);
        s.array(loadout);
        s.u8(sp_charge);
        s.str(clan, 24);
        clan_mark.serialize(s);
        s.u8(role);
        s.u8(name_colour), s.u8(clan_colour), s.u8(fake_rank);
        s.u32(forfeits), s.u32(team_kills), s.u32(missions), s.u32(attended), s.u32(rounds), s.u32(survived), s.u64(shots), s.u64(hits);
        s.u8(server_role), s.u8(global_role), s.u32(rev);
    }
    int rank() const { return rank_for_xp(xp); }
    bool staff() const { return role >= 1; }
    bool game_master() const { return role >= 2; }
    float kd() const { return deaths ? float(kills) / float(deaths) : float(kills); }
};

enum class NoticeKind : u8 { Info = 0, Good = 1, Warning = 2, Bad = 3 };

struct Notice {
    static constexpr Msg kId = Msg::Notice;
    u8 kind = 0;
    std::string text;
    template <typename S> void serialize(S& s) { s.u8(kind), s.str(text, 300); }
};

// ── Channels and the lobby ─────────────────────────────────────────────────────

struct ListChannels {
    static constexpr Msg kId = Msg::ListChannels;
    template <typename S> void serialize(S&) {}
};

struct ChannelInfo {
    u8 id = 0;
    std::string name;
    u8 kind = 0;
    std::string limit;
    u16 players = 0, capacity = 0;
    bool allowed = true;
    // The channel as channels.cfg keeps it (§10.1), for the list's gates and the Owner's editor.
    u8 min_rank = 0, max_rank = 74;
    float min_kd = 0;
    u16 modes = 0;
    bool hidden = false;
    u8 order = 0;
    std::vector<std::string> maps;
    template <typename S> void serialize(S& s) {
        s.u8(id), s.str(name, 48), s.u8(kind), s.str(limit, 32), s.u16(players), s.u16(capacity), s.boolean(allowed);
        s.u8(min_rank), s.u8(max_rank), s.f32(min_kd), s.u16(modes), s.boolean(hidden), s.u8(order);
        u32 n = u32(maps.size());
        s.varu(n);
        if constexpr (S::reading) maps.resize(std::min<u32>(n, 64));
        for (std::string& m : maps) s.str(m, 32);
    }
};

struct ChannelList {
    static constexpr Msg kId = Msg::ChannelList;
    std::string server_name;
    std::vector<ChannelInfo> channels;
    u32 version = 0;           // channels.cfg's version, which an edit carries back (CH-6)
    bool editable = false;     // you may edit them (the Owner, a Server Admin, SFLegacy Staff)
    template <typename S> void serialize(S& s) { s.str(server_name, 64), s.list(channels, size_t(kMaxChannels)), s.u32(version), s.boolean(editable); }
};

// The Owner's editor (§10.2): a channel made or changed (Save), deleted, or moved in the list.
enum class ChannelOp : u8 { Save = 0, Delete, Move };
struct ChannelEdit {
    static constexpr Msg kId = Msg::ChannelEdit;
    u8 op = 0;
    ChannelInfo channel;       // Save: all of it; Delete and Move: its id (Move: `order` the new place)
    u32 version = 0;           // the list's version this edit was made on: a stale one is refused (CH-6)
    template <typename S> void serialize(S& s) { s.u8(op), channel.serialize(s), s.u32(version); }
};

struct MotdEdit {
    static constexpr Msg kId = Msg::MotdEdit;
    std::string motd;
    template <typename S> void serialize(S& s) { s.str(motd, 512); }
};

// What this server plays (its Game Masters' Games tab): sent with the channel list, and to everyone
// whenever it changes. `editable`: you may change it (the Owner, Admins and Game Masters).
struct GamesState {
    static constexpr Msg kId = Msg::GamesState;
    ServerGames games;
    bool editable = false;
    template <typename S> void serialize(S& s) { games.serialize(s), s.boolean(editable); }
};

struct GamesEdit {
    static constexpr Msg kId = Msg::GamesEdit;
    ServerGames games;
    template <typename S> void serialize(S& s) { games.serialize(s); }
};

struct JoinChannel {
    static constexpr Msg kId = Msg::JoinChannel;
    u8 id = 0;
    template <typename S> void serialize(S& s) { s.u8(id); }
};

struct LeaveChannel {
    static constexpr Msg kId = Msg::LeaveChannel;
    template <typename S> void serialize(S&) {}
};

struct ChannelJoined {
    static constexpr Msg kId = Msg::ChannelJoined;
    u8 id = 0;
    std::string name;
    template <typename S> void serialize(S& s) { s.u8(id), s.str(name, 48); }
};

enum class RoomPhase : u8 { Waiting = 0, Loading = 1, Playing = 2 };

struct RoomSummary {
    u16 id = 0;
    std::string title;
    u8 mode = 0;
    std::string map;
    std::string host;
    u8 players = 0, max_players = 0;
    u8 phase = 0;
    bool locked = false;       // has a password
    bool free_join = true;
    bool observers = true;
    u8 time_of_day = 0;        // TimeOfDay
    bool no_snipers = false;   // the game type's "(no sniper)" variant
    bool clan_battle = false;  // a Clan War channel's room: the two clans (blue's empty until one has come)
    std::string red_clan, blue_clan;
    template <typename S> void serialize(S& s) {
        s.u16(id), s.str(title, 32), s.u8(mode), s.str(map, 32), s.str(host, kNameMax);
        s.u8(players), s.u8(max_players), s.u8(phase), s.boolean(locked), s.boolean(free_join), s.boolean(observers), s.u8(time_of_day);
        s.boolean(no_snipers), s.boolean(clan_battle), s.str(red_clan, 24), s.str(blue_clan, 24);
    }
};

struct LobbyUserInfo {
    u32 id = 0;
    std::string name;
    u32 xp = 0;                // what the rank mark shows (a Fake Rank Mark's, when one is worn)
    u16 room = 0;              // 0: in the lobby itself
    std::string clan;
    u8 name_colour = 0;        // Colored Codename: kNameColours index, 0 plain
    u8 clan_colour = 0;
    template <typename S> void serialize(S& s) { s.u32(id), s.str(name, kNameMax), s.u32(xp), s.u16(room), s.str(clan, 24), s.u8(name_colour), s.u8(clan_colour); }
};

struct LobbyState {
    static constexpr Msg kId = Msg::LobbyState;
    std::vector<RoomSummary> rooms;
    std::vector<LobbyUserInfo> users;
    template <typename S> void serialize(S& s) { s.list(rooms, 512), s.list(users, 1024); }
};

struct RoomSummaryMsg {
    static constexpr Msg kId = Msg::RoomSummaryMsg;
    RoomSummary room;
    template <typename S> void serialize(S& s) { room.serialize(s); }
};

struct RoomRemoved {
    static constexpr Msg kId = Msg::RoomRemoved;
    u16 id = 0;
    template <typename S> void serialize(S& s) { s.u16(id); }
};

struct LobbyUser {
    static constexpr Msg kId = Msg::LobbyUser;
    LobbyUserInfo user;
    template <typename S> void serialize(S& s) { user.serialize(s); }
};

struct LobbyUserLeft {
    static constexpr Msg kId = Msg::LobbyUserLeft;
    u32 id = 0;
    template <typename S> void serialize(S& s) { s.u32(id); }
};

// Lobby: the channel's lobby; Room: the waiting room; All and Team: in a match. Whisper: one soldier,
// wherever they are (left in their inbox when they are away); Clan: every member signed in, wherever
// they are; Global: everyone signed in who has it on, lobbies, rooms and matches alike (staff can take
// a soldier off it: StaffOp::GlobalMute).
enum class ChatScope : u8 { Lobby = 0, Room = 1, All = 2, Team = 3, Whisper = 4, System = 5, Clan = 6, Global = 7 };

struct Chat {
    static constexpr Msg kId = Msg::Chat;
    u8 scope = 0;
    std::string to;            // whisper target
    std::string text;
    template <typename S> void serialize(S& s) { s.u8(scope), s.str(to, kNameMax), s.str(text, kChatMax); }
};

struct ChatLine {
    static constexpr Msg kId = Msg::ChatLine;
    u8 scope = 0;
    std::string from;
    u8 team = u8(Team::None);
    std::string text;
    std::string to;            // a whisper of yours: whom it went to (`from` is then you)
    std::string clan;          // the speaker's clan, on global lines
    u8 role = 0;               // the speaker's Role: staff are marked
    template <typename S> void serialize(S& s) {
        s.u8(scope), s.str(from, kNameMax), s.u8(team), s.str(text, kChatMax + 40), s.str(to, kNameMax), s.str(clan, 24), s.u8(role);
    }
};

struct ChatPrefs {
    static constexpr Msg kId = Msg::ChatPrefs;
    bool global = true;        // hear (and speak on) global chat
    bool invites = true;       // take invitations to rooms (the lobby's Accept Invites plate)
    template <typename S> void serialize(S& s) { s.boolean(global), s.boolean(invites); }
};

// ── Friends and messages (Server/Social.cpp) ───────────────────────────────────
//
// Friends are mutual: one asks, the other accepts. A soldier blocked cannot ask you, whisper you,
// leave you a message or be heard by you on global chat. A whisper to someone away is left in their
// inbox (kMailKeep, oldest read ones go first).

inline constexpr size_t kFriendsMax = 100;
inline constexpr size_t kMailKeep = 50;
inline constexpr size_t kMailTextMax = 600;
enum class FriendOp : u8 { Add = 0, Accept, Decline, Cancel, Remove, Block, Unblock };
enum class FriendState : u8 { Friend = 0, Incoming, Outgoing, Blocked };

struct FriendAction {
    static constexpr Msg kId = Msg::FriendAction;
    u8 op = 0;
    std::string code_name;
    template <typename S> void serialize(S& s) { s.u8(op), s.str(code_name, kNameMax); }
};

struct FriendEntry {
    std::string name;          // code name
    u8 state = 0;              // FriendState
    bool online = false;
    u32 xp = 0;
    std::string clan;
    std::string where;         // "Lobby, Free Channel", "Room 3: Night fight (waiting)", "Playing Crossroad"
    u8 channel = 0;            // where to find them: their channel and room (0: none)
    u16 room = 0;
    bool joinable = false;     // a waiting room with a seat, open
    u64 last_seen = 0;         // unix seconds, when away
    u64 server = 0;            // the server they are on (§14.3), 0: none, or not shown (D25)
    std::string server_name;
    template <typename S> void serialize(S& s) {
        s.str(name, kNameMax), s.u8(state), s.boolean(online), s.u32(xp), s.str(clan, 24), s.str(where, 96), s.u8(channel), s.u16(room), s.boolean(joinable),
            s.u64(last_seen), s.u64(server), s.str(server_name, 64);
    }
};

struct FriendList {
    static constexpr Msg kId = Msg::FriendListMsg;
    std::vector<FriendEntry> entries;
    template <typename S> void serialize(S& s) { s.list(entries, kFriendsMax * 3); }
};

// Save keeps a message past its 7 days (ML-2); Report sends it to SFLegacy Staff (ML-6).
enum class MailOp : u8 { Read = 0, Delete, ReadAll, DeleteRead, Save, Unsave, Report };

struct MailAction {
    static constexpr Msg kId = Msg::MailAction;
    u8 op = 0;
    u32 id = 0;
    template <typename S> void serialize(S& s) { s.u8(op), s.u32(id); }
};

struct MailItem {
    u32 id = 0;
    std::string from;          // code name
    u64 time = 0;              // unix seconds
    std::string text;
    bool read = false;
    bool saved = false;        // kept until deleted (ML-2)
    bool system = false;       // Team Vanilla's own: a refund, a warning, staff (ML-4)
    u64 expires = 0;           // unix seconds it is deleted at (0: saved)
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(from, kNameMax), s.u64(time), s.str(text, kMailTextMax), s.boolean(read), s.boolean(saved), s.boolean(system), s.u64(expires);
    }
};

struct Mailbox {
    static constexpr Msg kId = Msg::MailboxMsg;
    std::vector<MailItem> items;   // newest first
    u64 now = 0;                   // TVAS's clock when it was read (ML-3: "deleted in 2 days")
    u16 saved_max = 50, unsaved_max = 30;   // ML-5
    template <typename S> void serialize(S& s) { s.list(items, 256), s.u64(now), s.u16(saved_max), s.u16(unsaved_max); }
};

// ── Rooms ──────────────────────────────────────────────────────────────────────

struct CreateRoom {
    static constexpr Msg kId = Msg::CreateRoom;
    RoomSettings settings;
    template <typename S> void serialize(S& s) { settings.serialize(s); }
};

struct JoinRoom {
    static constexpr Msg kId = Msg::JoinRoom;
    u16 id = 0;
    std::string password;
    bool observer = false;
    template <typename S> void serialize(S& s) { s.u16(id), s.str(password, 16), s.boolean(observer); }
};

struct QuickJoin {
    static constexpr Msg kId = Msg::QuickJoin;
    template <typename S> void serialize(S&) {}
};

// A room's invitation. A soldier in a room asks another in by code name: a friend wherever they
// are, or anyone in the channel's lobby. The soldier asked is shown the room and answers; a yes
// takes them there (to its channel first, when they stand in another) and past its password.
// Nobody in a room or a match is asked, nor a soldier who blocks the asker or takes no
// invitations (ChatPrefs::invites). An invitation stands for a minute.
struct RoomInvite {
    static constexpr Msg kId = Msg::RoomInvite;
    std::string code_name;
    template <typename S> void serialize(S& s) { s.str(code_name, kNameMax); }
};

struct RoomInvited {
    static constexpr Msg kId = Msg::RoomInvited;
    u16 room = 0;
    std::string from;          // who asks
    std::string title;
    u8 mode = 0;
    std::string map;
    u8 players = 0, max_players = 0;
    u8 channel = 0;
    std::string channel_name;
    bool playing = false;      // its game is under way (it takes soldiers mid-game)
    template <typename S> void serialize(S& s) {
        s.u16(room), s.str(from, kNameMax), s.str(title, 32), s.u8(mode), s.str(map, 32), s.u8(players), s.u8(max_players), s.u8(channel), s.str(channel_name, 48),
            s.boolean(playing);
    }
};

struct RoomInviteAnswer {
    static constexpr Msg kId = Msg::RoomInviteAnswer;
    u16 room = 0;
    bool accept = false;
    template <typename S> void serialize(S& s) { s.u16(room), s.boolean(accept); }
};

struct LeaveRoom {
    static constexpr Msg kId = Msg::LeaveRoom;
    template <typename S> void serialize(S&) {}
};

struct ChangeRoom {
    static constexpr Msg kId = Msg::ChangeRoom;
    RoomSettings settings;
    template <typename S> void serialize(S& s) { settings.serialize(s); }
};

struct SetTeam {
    static constexpr Msg kId = Msg::SetTeam;
    u8 team = 0;
    template <typename S> void serialize(S& s) { s.u8(team); }
};

struct SetReady {
    static constexpr Msg kId = Msg::SetReady;
    bool ready = false;
    template <typename S> void serialize(S& s) { s.boolean(ready); }
};

struct SetLoadout {
    static constexpr Msg kId = Msg::SetLoadout;
    u8 force = 0;
    Loadout weapons = kNoLoadout;
    template <typename S> void serialize(S& s) { s.u8(force), s.array(weapons); }
};

struct KickPlayer {
    static constexpr Msg kId = Msg::KickPlayer;
    u32 player = 0;
    template <typename S> void serialize(S& s) { s.u32(player); }
};

struct StartMatch {
    static constexpr Msg kId = Msg::StartMatch;
    template <typename S> void serialize(S&) {}
};

// A seat's state, as the waiting room's slot plates show it (READY, INVEN, PLAY, LOAD, WAIT).
enum class SlotState : u8 { Wait = 0, Ready = 1, Inventory = 2, Loading = 3, Playing = 4 };

struct RoomMember {
    u32 id = 0;
    std::string name;
    u32 xp = 0;
    u8 team = 0;
    u8 slot = 0;               // seat within the team, 0..7
    u8 state = 0;              // SlotState
    bool host = false;
    u8 force = 0;
    Loadout loadout = kNoLoadout;
    u16 ping = 0;
    std::string clan;
    ClanMark clan_mark;
    u8 name_colour = 0, clan_colour = 0;
    std::vector<u16> parts;    // the character parts worn (Game/Items.hpp), for the soldier on show
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(name, kNameMax), s.u32(xp), s.u8(team), s.u8(slot), s.u8(state), s.boolean(host), s.u8(force);
        s.array(loadout);
        s.u16(ping), s.str(clan, 24);
        clan_mark.serialize(s);
        s.u8(name_colour), s.u8(clan_colour), s.words(parts, 32);
    }
};

struct RoomState {
    static constexpr Msg kId = Msg::RoomState;
    u16 id = 0;
    RoomSettings settings;
    u8 phase = 0;
    std::vector<RoomMember> members;
    template <typename S> void serialize(S& s) { s.u16(id), settings.serialize(s), s.u8(phase), s.list(members, kMaxRoomPlayers + 8); }
};

struct RoomLeft {
    static constexpr Msg kId = Msg::RoomLeft;
    std::string reason;        // empty when you left yourself
    template <typename S> void serialize(S& s) { s.str(reason, 120); }
};

// ── The match ──────────────────────────────────────────────────────────────────

struct MatchPlayer {
    u32 id = 0;
    std::string name;
    u8 team = 0;
    u8 force = 0;
    Loadout loadout = kNoLoadout;
    u32 xp = 0;
    u8 name_colour = 0;
    std::vector<u16> parts;    // worn: the soldier's model is his force's with these on
    u16 spray = kNoSpray;      // the spray he carries (Game/Shop.hpp)
    u16 marks = 0;             // what marks his row on the Tab board (Game/Items.hpp RowMark)
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(name, kNameMax), s.u8(team), s.u8(force), s.array(loadout), s.u32(xp), s.u8(name_colour), s.words(parts, 32);
        s.u16(spray), s.u16(marks);
    }
};

struct MatchLoad {
    static constexpr Msg kId = Msg::MatchLoad;
    RoomSettings settings;
    u32 seed = 0;
    u32 you = 0;
    std::vector<MatchPlayer> players;
    template <typename S> void serialize(S& s) { settings.serialize(s), s.u32(seed), s.u32(you), s.list(players, kMaxRoomPlayers + 8); }
};

struct LoadDone {
    static constexpr Msg kId = Msg::LoadDone;
    template <typename S> void serialize(S&) {}
};

struct MatchBegin {
    static constexpr Msg kId = Msg::MatchBegin;
    u32 tick = 0;
    template <typename S> void serialize(S& s) { s.u32(tick); }
};

enum Buttons : u16 {
    kButtonFire = 1 << 0,
    kButtonAim = 1 << 1,
    kButtonJump = 1 << 2,
    kButtonCrouch = 1 << 3,
    kButtonWalk = 1 << 4,
    kButtonReload = 1 << 5,
    kButtonUse = 1 << 6,
};

enum PlayerFlags : u16 {
    kFlagAlive = 1 << 0,
    kFlagCrouched = 1 << 1,
    kFlagOnGround = 1 << 2,
    kFlagReloading = 1 << 3,
    kFlagScoped = 1 << 4,
    kFlagFiring = 1 << 5,
    kFlagWalking = 1 << 6,
    // Sent instead of everything else about an enemy this client could not see or hear (the
    // server's sight cull): only that they are alive. Not drawn, not hit, not on the radar.
    kFlagHidden = 1 << 7,
    kFlagOnLadder = 1 << 8,   // climbing: the legs walk the rungs, the gun stays in hand
};

// The client's own movement, sent every tick. The rewrite trusts a client with its own position
// (as the original did) and the server keeps it honest: speed, teleport and wall checks.
struct Input {
    static constexpr Msg kId = Msg::Input;
    u32 tick = 0;
    Vec3 position;
    Vec3 velocity;
    float yaw = 0, pitch = 0;
    u16 flags = 0;
    u8 weapon_slot = 0;
    u16 buttons = 0;
    template <typename S> void serialize(S& s) {
        s.u32(tick), s.vec3(position), s.vec3(velocity), s.angle(yaw), s.angle(pitch), s.u16(flags), s.u8(weapon_slot), s.u16(buttons);
    }
};

enum class HitZone : u8 { None = 0, Head, Chest, Stomach, Arms, Legs };

struct ShotHit {
    u32 victim = 0;
    u8 zone = 0;
    Vec3 point;
    u8 wall = 0;               // cm of wall the shooter's game traced the bullet through (0: none)
    template <typename S> void serialize(S& s) { s.u32(victim), s.u8(zone), s.vec3(point), s.u8(wall); }
};

struct Shoot {
    static constexpr Msg kId = Msg::Shoot;
    u32 tick = 0;
    u16 weapon = kNoWeapon;
    Vec3 origin;
    Vec3 direction;
    Vec3 end;                  // where the bullet stopped (a wall, or range)
    std::vector<ShotHit> hits; // what the client's own trace met (pellets: several)
    template <typename S> void serialize(S& s) { s.u32(tick), s.u16(weapon), s.vec3(origin), s.vec3(direction), s.vec3(end), s.list(hits, 16); }
};

struct Throw {
    static constexpr Msg kId = Msg::Throw;
    u16 weapon = kNoWeapon;
    Vec3 origin;
    Vec3 velocity;
    template <typename S> void serialize(S& s) { s.u16(weapon), s.vec3(origin), s.vec3(velocity); }
};

struct Radio {
    static constexpr Msg kId = Msg::Radio;
    u8 group = 0;              // Z command, X general, C reply
    u8 line = 0;
    template <typename S> void serialize(S& s) { s.u8(group), s.u8(line); }
};

struct RadioFx {
    static constexpr Msg kId = Msg::RadioFx;
    u32 speaker = 0;
    u8 group = 0, line = 0;
    template <typename S> void serialize(S& s) { s.u32(speaker), s.u8(group), s.u8(line); }
};

struct LeaveMatch {
    static constexpr Msg kId = Msg::LeaveMatch;
    template <typename S> void serialize(S&) {}
};

struct PlayerSnap {
    u32 id = 0;
    Vec3 position;
    Vec3 velocity;
    float yaw = 0, pitch = 0;
    u16 flags = 0;
    u16 health = 0;            // a captain has 1,000, a host zombie 1,500
    u16 weapon = kNoWeapon;
    template <typename S> void serialize(S& s) {
        s.u32(id), s.vec3(position), s.vec3(velocity), s.angle(yaw), s.angle(pitch), s.u16(flags), s.u16(health), s.u16(weapon);
    }
};

struct Snapshot {
    static constexpr Msg kId = Msg::Snapshot;
    u32 tick = 0;
    float round_time = 0;      // seconds left
    std::vector<PlayerSnap> players;
    template <typename S> void serialize(S& s) { s.u32(tick), s.f32(round_time), s.list(players, kMaxRoomPlayers + 8); }
};

// Where a soldier appears: the n-th spawn point of their side's list in the map's world script
// (every client has the map, so the server need not). Single Battle uses the personal list.
struct Spawn {
    static constexpr Msg kId = Msg::Spawn;
    u32 player = 0;
    u8 team = 0;
    u8 index = 0;
    u16 health = kMaxHealth;
    u8 force = 0;
    Loadout loadout = kNoLoadout;
    // Horror: an infected soldier rises where he fell, not at a spawn point.
    bool here = false;
    Vec3 at;
    float yaw = 0;
    template <typename S> void serialize(S& s) {
        s.u32(player), s.u8(team), s.u8(index), s.u16(health), s.u8(force), s.array(loadout), s.boolean(here), s.vec3(at), s.angle(yaw);
    }
};

struct Damage {
    static constexpr Msg kId = Msg::Damage;
    u32 attacker = 0, victim = 0;
    u16 amount = 0;
    u16 health = 0;            // what the victim has left
    u8 zone = 0;
    Vec3 from;
    template <typename S> void serialize(S& s) { s.u32(attacker), s.u32(victim), s.u16(amount), s.u16(health), s.u8(zone), s.vec3(from); }
};

// How a kill was made, for the feed's marks and the killer's own (the original's kill effects,
// inf/killeffect/killimage.txt). Double: the second kill inside three seconds of the last; Multi the
// third; Special Force the fourth and on (the streak row: 2, 3 and 4 marks). Revenge: the one who
// killed you last. Wall: the bullet came through a wall (Game/Ballistics.hpp). Rage: Team Slayer's,
// made while in a rage. Captain: the one brought down was a captain. Bomb: the C4's own blast.
// Team: a teammate's grenade did it (no kill counted, a team kill on the thrower's record; Prevent
// Team Kill keeps a soldier from it). Fall: the ground did it (a fall from too high).
enum KillFlags : u16 {
    kKillHeadshot = 1, kKillGrenade = 2, kKillKnife = 4, kKillSuicide = 8, kKillDouble = 16, kKillMulti = 32, kKillSpecialForce = 64,
    kKillRevenge = 128, kKillWall = 256, kKillRage = 512, kKillCaptain = 1024, kKillBomb = 2048, kKillTeam = 4096, kKillFall = 8192
};

struct Kill {
    static constexpr Msg kId = Msg::Kill;
    u32 killer = 0, victim = 0;
    u16 weapon = kNoWeapon;
    u16 flags = 0;             // KillFlags
    u16 killer_health = 0;     // what the killer had left (the death screen; enemies' health is never sent otherwise)
    template <typename S> void serialize(S& s) { s.u32(killer), s.u32(victim), s.u16(weapon), s.u16(flags), s.u16(killer_health); }
};

struct ShotFx {
    static constexpr Msg kId = Msg::ShotFx;
    u32 shooter = 0;
    u16 weapon = kNoWeapon;
    Vec3 origin, end;
    template <typename S> void serialize(S& s) { s.u32(shooter), s.u16(weapon), s.vec3(origin), s.vec3(end); }
};

struct GrenadeFx {
    static constexpr Msg kId = Msg::GrenadeFx;
    u32 thrower = 0;
    u16 weapon = kNoWeapon;
    Vec3 origin, velocity;
    bool exploded = false;     // false: thrown (clients fly it), true: went off at origin
    template <typename S> void serialize(S& s) { s.u32(thrower), s.u16(weapon), s.vec3(origin), s.vec3(velocity), s.boolean(exploded); }
};

struct RoundStart {
    static constexpr Msg kId = Msg::RoundStart;
    u8 round = 0;
    float seconds = 0;
    template <typename S> void serialize(S& s) { s.u8(round), s.f32(seconds); }
};

enum class RoundReason : u8 { Elimination = 0, TimeUp, Objective, Surrender };

struct RoundEnd {
    static constexpr Msg kId = Msg::RoundEnd;
    u8 winner = u8(Team::None);
    u8 reason = 0;
    u8 red_wins = 0, blue_wins = 0;
    template <typename S> void serialize(S& s) { s.u8(winner), s.u8(reason), s.u8(red_wins), s.u8(blue_wins); }
};

struct ScoreRow {
    u32 id = 0;
    u16 kills = 0, deaths = 0, assists = 0;
    u16 score = 0;
    u16 ping = 0;
    std::array<u16, 3> extra{};   // the game type's own columns (ModeInfo::columns)
    u16 headshots = 0;            // Training's board counts them
    template <typename S> void serialize(S& s) { s.u32(id), s.u16(kills), s.u16(deaths), s.u16(assists), s.u16(score), s.u16(ping), s.array(extra), s.u16(headshots); }
};

struct Score {
    static constexpr Msg kId = Msg::Score;
    u16 red = 0, blue = 0;     // rounds won, or team points
    std::vector<ScoreRow> rows;
    template <typename S> void serialize(S& s) { s.u16(red), s.u16(blue), s.list(rows, kMaxRoomPlayers + 8); }
};

struct Reward {
    u32 id = 0;
    u32 xp = 0, sp = 0;
    bool ranked_up = false;
    u8 bags = 0;               // duffle bags the promotion brought (one a rank)
    u8 dropped = kNoBox;       // an event's box, dropped at the end
    u16 sp_pct = 100, xp_pct = 100;   // the events' share of it
    bool won = false;          // this soldier's game counts as won (Horror's sides change every round)
    u16 special = 0;           // of the SP, what the round's challenges paid (before any bonus)
    u8 set = 0;                // the set worn whole (Game/Items.hpp ItemSet): its rank points are in `xp`
    u16 clan_points = 0;       // a Clan Battle: what this soldier earned his clan
    // TVAS works the pay out (PR-3): `pending` until it has answered (MatchRewards follows); the
    // day's XP limit reached (PR-13) and the levels left today.
    bool pending = false;
    bool xp_capped = false;
    u8 levels_left = 25;
    template <typename S> void serialize(S& s) {
        s.u32(id), s.u32(xp), s.u32(sp), s.boolean(ranked_up), s.u8(bags), s.u8(dropped), s.u16(sp_pct), s.u16(xp_pct), s.boolean(won), s.u16(special), s.u8(set);
        s.u16(clan_points), s.boolean(pending), s.boolean(xp_capped), s.u8(levels_left);
    }
};

// The match's pay as TVAS worked it out, when it came after the results were shown.
struct MatchRewards {
    static constexpr Msg kId = Msg::MatchRewards;
    std::vector<Reward> rewards;
    std::string note;          // why there is none ("Team Vanilla's services are down: ...")
    template <typename S> void serialize(S& s) { s.list(rewards, kMaxRoomPlayers + 8), s.str(note, 200); }
};

// A Horror game's item used (Game/Modes.hpp HorrorItem): asked for, and told to everyone (`left`:
// how many of it the soldier still has).
struct UseHorrorItem {
    static constexpr Msg kId = Msg::UseHorrorItem;
    u8 item = 0;
    template <typename S> void serialize(S& s) { s.u8(item); }
};
struct HorrorItemUsed {
    static constexpr Msg kId = Msg::HorrorItemUsed;
    u32 player = 0;
    u8 item = 0;
    u16 left = 0;
    template <typename S> void serialize(S& s) { s.u32(player), s.u8(item), s.u16(left); }
};

// Pirate Mode's cannons (Game/Modes.hpp): one manned, left, or fired along `aim` (the server keeps
// it within what the mount turns); and a ball on its way, for everyone to fly as the server does
// (cannon_ball_at) until its burst comes (a GrenadeFx gone off).
enum class CannonOp : u8 { Man = 0, Leave, Fire };
struct CannonUse {
    static constexpr Msg kId = Msg::CannonUse;
    u8 op = 0;
    u8 index = 0;
    Vec3 aim;
    template <typename S> void serialize(S& s) { s.u8(op), s.u8(index), s.vec3(aim); }
};
struct CannonFx {
    static constexpr Msg kId = Msg::CannonFx;
    u8 index = 0;
    u32 by = 0;
    Vec3 origin, velocity;
    template <typename S> void serialize(S& s) { s.u8(index), s.u32(by), s.vec3(origin), s.vec3(velocity); }
};

// One of the round's challenges done (Rules.hpp Special), told to the soldier who did it.
struct SpecialPoint {
    static constexpr Msg kId = Msg::SpecialPointMsg;
    u32 player = 0;
    u8 kind = 0;
    u16 sp = 0;
    template <typename S> void serialize(S& s) { s.u32(player), s.u8(kind), s.u16(sp); }
};

struct MatchOver {
    static constexpr Msg kId = Msg::MatchOver;
    u8 winner = u8(Team::None);
    u32 winner_player = 0;     // Single Battle
    std::vector<ScoreRow> rows;
    std::vector<Reward> rewards;
    u32 match = 0;             // the recording's number (0: this server keeps none)
    bool no_progress = false;  // "This PC": nothing counted (D41)
    template <typename S> void serialize(S& s) {
        s.u8(winner), s.u32(winner_player), s.list(rows, kMaxRoomPlayers + 8), s.list(rewards, kMaxRoomPlayers + 8), s.u32(match), s.boolean(no_progress);
    }
};

// ── Game types (Game/Modes.hpp) ────────────────────────────────────────────────

// One objective as it stands: a bomb site, the bomb, a thing to take, a side's zone, a console or
// stronghold being taken, a supply box, the girl, the treasure, a magazine on the floor.
struct ObjectiveNow {
    u8 kind = 0;               // lsf::Objective
    u8 state = 0;              // the kind's own (Game/Modes.hpp)
    u8 team = u8(Team::None);  // whose (a zone's side, a console's owner)
    u8 index = 0;              // which of its kind (site A = 0, B = 1; the place's number)
    u32 who = 0;               // the carrier, the planter, the one taking it
    Vec3 at;
    float radius = 0;
    float progress = 0;        // 0..1: a plant, a defuse, a capture
    float timer = 0;           // seconds: the fuse left, the return, the next treasure
    template <typename S> void serialize(S& s) {
        s.u8(kind), s.u8(state), s.u8(team), s.u8(index), s.u32(who), s.vec3(at), s.f32(radius), s.f32(progress), s.f32(timer);
    }
};

// What the game made of a soldier: a captain, a host zombie, an undead and its class, escaped.
struct RoleNow {
    u32 player = 0;
    u8 role = 0;               // lsf::MatchRole
    u8 undead = 0;             // lsf::Undead: the body drawn
    u8 rank = 1;               // an undead's evolution (zrank_1..3)
    u8 flags = 0;              // kRole*
    u16 health_max = 0;
    float boost = 0;           // seconds left of a skill acting on him (speed, jump), or of rage
    template <typename S> void serialize(S& s) { s.u32(player), s.u8(role), s.u8(undead), s.u8(rank), s.u8(flags), s.u16(health_max), s.f32(boost); }
};
enum RoleFlags : u8 {
    kRoleSpeed = 1,            // Super Speed acting
    kRoleJump = 2,             // Super Jump acting
    kRoleRage = 4,             // Team Slayer's rage
    kRoleProtected = 8,        // just spawned: unbeatable
    kRoleSeen = 16,            // shown to everyone wherever he is (Captain Mode's captains; a Search)
    kRoleKiller = 32,          // Team Slayer: the one who killed you last (each client is told its own)
    kRolePicking = 64,         // Horror Mode 2: choosing a class
    kRoleCleansed = 128,       // a Blind Cleanse acting: fog and smoke do not blind him
};

struct ModeState {
    static constexpr Msg kId = Msg::ModeStateMsg;
    u8 phase = 0;              // the game type's own (Horror: 0 the wait, 1 turned)
    float phase_left = 0;      // seconds of it left
    std::array<u16, 4> counts{};   // escaped, needed to escape, humans standing, undead standing
    std::array<u16, 2> points{};   // red's and blue's (Pirate Mode's, Team Slayer's), else 0
    std::vector<ObjectiveNow> objectives;
    std::vector<RoleNow> roles;
    // Your own: skills' cooldowns (seconds left, in the order of your class's skills), your rage
    // gauge, your record against the one who killed you last.
    std::array<float, 5> cooldowns{};
    u8 rage = 0;
    u8 vs_mine = 0, vs_theirs = 0;
    template <typename S> void serialize(S& s) {
        s.u8(phase), s.f32(phase_left), s.array(counts), s.array(points), s.list(objectives, 96), s.list(roles, kMaxRoomPlayers + 8);
        for (float& c : cooldowns) s.f32(c);
        s.u8(rage), s.u8(vs_mine), s.u8(vs_theirs);
    }
};

struct ModeEvent {
    static constexpr Msg kId = Msg::ModeEventMsg;
    u8 kind = 0;               // lsf::ModeEventKind
    u32 who = 0;
    u32 other = 0;
    u8 team = u8(Team::None);
    u8 value = 0;              // a site's number, a skill, a class, a rank
    Vec3 at;
    template <typename S> void serialize(S& s) { s.u8(kind), s.u32(who), s.u32(other), s.u8(team), s.u8(value), s.vec3(at); }
};

struct UseSkill {
    static constexpr Msg kId = Msg::UseSkill;
    u8 skill = 0;              // lsf::Skill (one of your class's)
    Vec3 aim;                  // where you look (a throw's, a snatch's, a drilling's way)
    template <typename S> void serialize(S& s) { s.u8(skill), s.vec3(aim); }
};

struct PickClass {
    static constexpr Msg kId = Msg::PickClass;
    u8 undead = 0;             // lsf::Undead
    template <typename S> void serialize(S& s) { s.u8(undead); }
};

// A gun put down and taken up (config.cfg WEAPON_DROP, ACQUIRE_WEAPON). A primary or a sidearm only;
// what lies on the floor is an objective (Objective::Weapon: `who` the weapon's id) that goes in
// kWeaponLies, and a soldier's primary falls where he does. The rounds in it go with it: the client
// keeps a gun's ammunition, so it says what is left in one it puts down.
struct DropWeapon {
    static constexpr Msg kId = Msg::DropWeapon;
    u8 slot = 0;
    u16 clip = 0, reserve = 0;
    template <typename S> void serialize(S& s) { s.u8(slot), s.u16(clip), s.u16(reserve); }
};

// The gun at your feet taken: into its own slot, and the gun in that slot (if any) put down in its
// place, with what is left in it (`clip`, `reserve` by slot: primary, sidearm).
struct PickUpWeapon {
    static constexpr Msg kId = Msg::PickUpWeapon;
    std::array<u16, 2> clip{}, reserve{};
    template <typename S> void serialize(S& s) { s.array(clip), s.array(reserve); }
};

// A soldier's slot now holds this (kNoWeapon: emptied), with these rounds.
struct Rearm {
    static constexpr Msg kId = Msg::Rearm;
    u32 player = 0;
    u8 slot = 0;
    u16 weapon = kNoWeapon;
    u16 clip = 0, reserve = 0;
    template <typename S> void serialize(S& s) { s.u32(player), s.u8(slot), s.u16(weapon), s.u16(clip), s.u16(reserve); }
};

// ── The ID card (gametext 283; locale_string_table IDCARD_*) ───────────────────

// Any soldier's card, asked for by code name (on duty or off). With `set`, your own card's line is
// written instead (`message`), and your own card comes back.
inline constexpr int kCardMessageMax = 60;
struct IdCardRequest {
    static constexpr Msg kId = Msg::IdCardRequest;
    std::string name;
    bool set = false;
    std::string message;
    template <typename S> void serialize(S& s) { s.str(name, kNameMax), s.boolean(set), s.str(message, kCardMessageMax); }
};

struct IdCard {
    static constexpr Msg kId = Msg::IdCardMsg;
    bool found = false;
    std::string code_name;     // as asked for when nobody is called that
    std::string clan;
    ClanMark clan_mark;
    std::string message;       // the soldier's own line
    u32 xp = 0;                // what the rank mark shows (a Fake Rank Mark's, when one is worn)
    u8 name_colour = 0, clan_colour = 0;
    bool online = false;
    u16 room = 0;              // the room they are in (0: none)
    std::string where;         // where they are, as they let it be seen (D25)
    // The record (Server/Accounts.hpp).
    u32 kills = 0, deaths = 0, wins = 0, losses = 0, headshots = 0, matches = 0;
    u32 forfeits = 0, team_kills = 0, missions = 0, attended = 0, rounds = 0, survived = 0;
    u64 shots = 0, hits = 0;
    // What they wear and carry.
    u8 force = 0;
    Loadout loadout = kNoLoadout;
    std::vector<u16> parts;
    template <typename S> void serialize(S& s) {
        s.boolean(found), s.str(code_name, kNameMax), s.str(clan, 24);
        clan_mark.serialize(s);
        s.str(message, kCardMessageMax), s.u32(xp), s.u8(name_colour), s.u8(clan_colour), s.boolean(online), s.u16(room);
        s.u32(kills), s.u32(deaths), s.u32(wins), s.u32(losses), s.u32(headshots), s.u32(matches);
        s.u32(forfeits), s.u32(team_kills), s.u32(missions), s.u32(attended), s.u32(rounds), s.u32(survived), s.u64(shots), s.u64(hits);
        s.u8(force), s.array(loadout);
        s.words(parts, 32);
    }
};

// ── Shop ───────────────────────────────────────────────────────────────────────

// An item you own (Game/Items.hpp): how long it has left and, for a one-use item, how many.
struct OwnedItem {
    u16 id = 0;
    u32 seconds_left = 0;      // kForGood: never runs out
    u16 uses = 0;              // one-use items: how many are left
    static constexpr u32 kForGood = 0xFFFFFFFFu;
    template <typename S> void serialize(S& s) { s.u16(id), s.u32(seconds_left), s.u16(uses); }
};

// A weapon you own that is not a plain one owned for good: rented (how long it has left), or worn.
struct OwnedGun {
    u16 id = 0;
    u32 seconds_left = OwnedItem::kForGood;
    u8 durability = kDurabilityFull;   // what is left of it (Game/Wear.hpp): 0 broken
    template <typename S> void serialize(S& s) { s.u16(id), s.u32(seconds_left), s.u8(durability); }
};

// A gift a friend sent (gametext 496-520): what it is and for how long, waiting in the inventory's
// Gift tab until it is opened (RequestOp::OpenGift).
struct GiftInfo {
    u32 id = 0;
    u8 kind = 0;               // ShopKind
    u16 item = 0;
    u16 days = 0;              // 0: for good (or one use)
    std::string from;          // the sender's code name
    template <typename S> void serialize(S& s) { s.u32(id), s.u8(kind), s.u16(item), s.u16(days), s.str(from, kNameMax); }
};
inline constexpr int kGiftsMax = 30;   // waiting unopened; a friend's Gift tab holds no more

struct InventoryMsg {
    static constexpr Msg kId = Msg::Inventory;
    std::vector<u16> weapons;  // owned
    std::vector<u16> forces;   // u16 for the list helper; each < 256
    std::vector<OwnedItem> items;
    std::vector<u16> equipped; // character parts worn (each force's own)
    std::vector<OwnedGun> guns;         // the weapons that are rented or worked on
    std::vector<OwnedItem> force_time;  // the forces that are rented: how long each has left
    std::vector<OwnedItem> sprays;      // the sprays owned (Game/Shop.hpp)
    u16 spray = kNoSpray;               // the one carried into a match
    u32 coins = 0;                      // capsule coins
    std::vector<GiftInfo> gifts;        // gifts waiting to be opened
    std::array<u16, kHorrorItems> horror{};   // Horror Mode's items, how many of each (Game/Modes.hpp)
    std::vector<OwnedItem> bags;        // the shop's Duffle Bags bought and not yet opened (id: ShopConfig::bags; uses: how many)
    template <typename S> void serialize(S& s) {
        s.words(weapons, 1024), s.words(forces, 64), s.list(items, 2048), s.words(equipped, 256);
        s.list(guns, 1024), s.list(force_time, 64), s.list(sprays, 64), s.u16(spray), s.u32(coins);
        s.list(gifts, size_t(kGiftsMax)), s.array(horror), s.list(bags, 64);
    }
};

using lsf::ShopKind;   // Weapon, Force, Item, Spray (Game/Shop.hpp)

// Bought for a friend instead (gametext 498 "Send gift"): the same offer, paid by you, kept for
// `to` (a code name on your friends list). The answer is a ShopResult.
// Every sale carries the player's own approval from TVAS (MN-2): this amount, this thing, this
// server. The server hands it to TVAS with the sale; without it no SP or Coin moves.
inline constexpr size_t kApprovalMax = 40;

struct SendGift {
    static constexpr Msg kId = Msg::SendGift;
    u8 kind = 0;
    u16 item = 0;
    u8 offer = 0;
    std::string to;
    std::string approval;
    u32 price = 0;             // what the player approved: the server refuses a sale at any other price
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(item), s.u8(offer), s.str(to, kNameMax), s.str(approval, kApprovalMax), s.u32(price); }
};

struct Buy {
    static constexpr Msg kId = Msg::Buy;
    u8 kind = 0;
    u16 item = 0;
    u8 offer = 0;              // which of its offers, as the shop now has them (ShopState): how many days
    std::string approval;
    u32 price = 0;
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(item), s.u8(offer), s.str(approval, kApprovalMax), s.u32(price); }
};

// An item that wants something of you: a Codename Change (text), a Colored Codename or Clanname
// (value = the colour), a Fake Rank Mark (value = the rank, 0xFF to take it off), a record reset.
struct UseItem {
    static constexpr Msg kId = Msg::UseItem;
    u16 item = 0;
    u8 value = 0;
    std::string text;
    template <typename S> void serialize(S& s) { s.u16(item), s.u8(value), s.str(text, kNameMax); }
};

// Wear or take off a character part (kind ShopKind::Item); the server answers with the inventory.
struct Equip {
    static constexpr Msg kId = Msg::Equip;
    u8 kind = 0;
    u16 item = 0;
    template <typename S> void serialize(S& s) { s.u8(kind), s.u16(item); }
};

struct RechargeSp {
    static constexpr Msg kId = Msg::RechargeSp;
    template <typename S> void serialize(S&) {}
};

struct ShopResult {
    static constexpr Msg kId = Msg::ShopResult;
    bool ok = false;
    std::string text;
    template <typename S> void serialize(S& s) { s.boolean(ok), s.str(text, 160); }
};

// ── Clans ──────────────────────────────────────────────────────────────────────

struct ClanRequest {
    static constexpr Msg kId = Msg::ClanRequest;
    template <typename S> void serialize(S&) {}
};

struct CreateClan {
    static constexpr Msg kId = Msg::CreateClan;
    std::string name;
    std::string notice;        // a line for the members
    ClanMark mark;
    template <typename S> void serialize(S& s) { s.str(name, kClanNameMax), s.str(notice, kClanNoticeMax), mark.serialize(s); }
};

struct LeaveClan {
    static constexpr Msg kId = Msg::LeaveClan;
    template <typename S> void serialize(S&) {}
};

struct ClanInvite {
    static constexpr Msg kId = Msg::ClanInvite;
    std::string code_name;
    template <typename S> void serialize(S& s) { s.str(code_name, kNameMax); }
};

struct ClanAnswer {
    static constexpr Msg kId = Msg::ClanAnswer;
    std::string clan;
    bool accept = false;
    template <typename S> void serialize(S& s) { s.str(clan, kClanNameMax), s.boolean(accept); }
};

struct ClanKick {
    static constexpr Msg kId = Msg::ClanKick;
    std::string code_name;
    template <typename S> void serialize(S& s) { s.str(code_name, kNameMax); }
};

struct ClanSetMark {
    static constexpr Msg kId = Msg::ClanSetMark;
    ClanMark mark;
    template <typename S> void serialize(S& s) { mark.serialize(s); }
};

// What a member may do is settled by rank (Game/Rules.hpp clan_may).
//
// A clan is Open or Closed (SetOpen). An open one takes any soldier who asks, at once, as a
// Recruit; a closed one takes them by application (Apply), which its Lieutenants and above answer
// (Accept, Decline), or by an officer's invitation (ClanInvite). Apply to an open clan joins it.
enum class ClanOp : u8 { Promote = 0, Demote, Transfer, SetNotice, Apply, Accept, Decline, CancelApply, Disband, SetOpen };

struct ClanAction {
    static constexpr Msg kId = Msg::ClanAction;
    u8 op = 0;
    std::string code_name;     // the member or applicant it is done to
    std::string text;          // the notice; Apply: the clan's name; SetOpen: "open" or "closed"
    template <typename S> void serialize(S& s) { s.u8(op), s.str(code_name, kNameMax), s.str(text, kClanNoticeMax); }
};

struct ClanMember {
    std::string name;          // code name
    u32 xp = 0;
    bool online = false;
    u8 rank = 0;               // ClanRank
    u64 joined = 0;            // unix seconds (0: before it was kept)
    template <typename S> void serialize(S& s) { s.str(name, kNameMax), s.u32(xp), s.boolean(online), s.u8(rank), s.u64(joined); }
};

struct ClanApplicant {
    std::string name;          // code name
    u32 xp = 0;
    u64 time = 0;
    bool online = false;
    template <typename S> void serialize(S& s) { s.str(name, kNameMax), s.u32(xp), s.u64(time), s.boolean(online); }
};

struct ClanLogLine {
    u64 time = 0;
    std::string text;          // "Raven promoted Ash to Lieutenant"
    template <typename S> void serialize(S& s) { s.u64(time), s.str(text, 96); }
};

inline constexpr size_t kClanLogKeep = 40;
inline constexpr size_t kClanApplicantsMax = 30;

struct ClanState {
    static constexpr Msg kId = Msg::ClanStateMsg;
    bool member = false;       // false: no clan (the rest is empty, but `applied`)
    std::string name;
    std::string master;        // the owner's code name
    std::string notice;
    std::string founded;       // "2026-09-27"
    ClanMark mark;
    std::vector<ClanMember> members;
    u8 my_rank = 0;            // ClanRank
    std::vector<ClanApplicant> applicants;   // shown to those who may answer them
    std::vector<ClanLogLine> log;            // newest first
    std::string applied;       // no clan: the one you asked to join, waiting
    bool open = false;         // anyone may join at once (else: by application or invitation)
    // Its Clan Battles: won, lost, drawn; the clan points its soldiers earned; where that puts it
    // among the clans (1: the most).
    u32 wins = 0, losses = 0, draws = 0, points = 0;
    u16 rank = 0;
    template <typename S> void serialize(S& s) {
        s.boolean(member), s.str(name, kClanNameMax), s.str(master, kNameMax), s.str(notice, kClanNoticeMax), s.str(founded, 16);
        mark.serialize(s);
        s.list(members, size_t(kClanMaxMembers));
        s.u8(my_rank), s.list(applicants, kClanApplicantsMax), s.list(log, kClanLogKeep), s.str(applied, kClanNameMax);
        s.boolean(open);
        s.u32(wins), s.u32(losses), s.u32(draws), s.u32(points), s.u16(rank);
    }
};

struct ClanBrowse {
    static constexpr Msg kId = Msg::ClanBrowse;
    template <typename S> void serialize(S&) {}
};

struct ClanListing {
    std::string name, owner, founded;    // `owner`: their code name
    std::string notice;
    u16 members = 0;
    u16 online = 0;
    bool open = false;         // joined at once (else: an application, answered by its officers)
    ClanMark mark;
    u32 wins = 0, losses = 0, draws = 0, points = 0;   // its Clan Battles, and its clan points
    u16 rank = 0;
    template <typename S> void serialize(S& s) {
        s.str(name, kClanNameMax), s.str(owner, kNameMax), s.str(founded, 16), s.str(notice, kClanNoticeMax), s.u16(members), s.u16(online), s.boolean(open),
            mark.serialize(s);
        s.u32(wins), s.u32(losses), s.u32(draws), s.u32(points), s.u16(rank);
    }
};

struct ClanDirectory {
    static constexpr Msg kId = Msg::ClanDirectoryMsg;
    std::vector<ClanListing> clans;
    template <typename S> void serialize(S& s) { s.list(clans, 300); }
};

struct ClanInvited {
    static constexpr Msg kId = Msg::ClanInvited;
    std::string clan;
    std::string from;          // the officer who asks
    ClanMark mark;
    u16 members = 0;
    template <typename S> void serialize(S& s) { s.str(clan, kClanNameMax), s.str(from, kNameMax), mark.serialize(s), s.u16(members); }
};

// ── Reports, staff and votes ───────────────────────────────────────────────────
//
// Two ranks above a player (TacticalFPS's split, on what can be undone): a Moderator handles
// people -- the report queue, kick, mute, ban -- and a Game Master does that and can also reach
// into an account (its code name, SP, XP, what it owns). Every message is checked on the server
// against the sender's own account; the role a client is told is only for what it draws.
//
// A client is never told another player's account name: what it picks from is the room's, the
// match's or the lobby's list (session ids) or a code name. Account names reach staff only.

// A soldier's role on one server (§7.1): the Owner (set by the registration at TVAS: RL-6), the
// Admins, Game Masters and Moderators the Owner names (staff.cfg). Their power stops at their
// server's own data (D4).
enum class Role : u8 { Player = 0, Moderator = 1, GameMaster = 2, Admin = 3, Owner = 4 };
const char* role_name(Role r);
// SFLegacy Staff (§7.1): every server, and TVAS itself; carried in the ticket (EN-4), never a
// server's to give or take (RL-1, RL-2).
enum class GlobalRole : u8 { Player = 0, Moderator = 1, GameMaster = 2, Admin = 3 };
const char* global_role_name(GlobalRole r);

enum class ReportReason : u8 { Cheating = 0, Abuse, TeamKilling, Leaving, BadName, Other, Count };
const char* report_reason_name(ReportReason r);
enum class ReportState : u8 { Open = 0, Actioned, Dismissed };

inline constexpr size_t kReportNoteMax = 200;
inline constexpr size_t kStaffReasonMax = 120;

struct ReportPlayer {
    static constexpr Msg kId = Msg::ReportPlayer;
    u32 player = 0;            // a session id from the room, the match or the lobby; or 0 and...
    std::string code_name;     // ...a code name
    u8 reason = 0;
    std::string note;
    template <typename S> void serialize(S& s) { s.u32(player), s.str(code_name, kNameMax), s.u8(reason), s.str(note, kReportNoteMax); }
};

struct StaffListReports {
    static constexpr Msg kId = Msg::StaffListReports;
    bool open_only = true;
    template <typename S> void serialize(S& s) { s.boolean(open_only); }
};

struct StaffResolveReport {
    static constexpr Msg kId = Msg::StaffResolveReport;
    u32 report = 0;
    u8 state = 0;              // ReportState::Actioned or Dismissed
    template <typename S> void serialize(S& s) { s.u32(report), s.u8(state); }
};

// GlobalMute takes a soldier off global chat only (the rest of their chat stands); 0 seconds: an hour.
enum class StaffOp : u8 { Kick = 0, Mute, Unmute, Ban, Unban, SetRole, GlobalMute, GlobalUnmute };

// On this server only (§7.2). SFLegacy Staff's global bans, mutes and edits go from their game to
// TVAS directly (RL-1): a server never carries them.
struct StaffAction {
    static constexpr Msg kId = Msg::StaffAction;
    std::string account;       // the soldier: a code name, or "#<TV account id>" (from a card or a report)
    u8 op = 0;
    u32 seconds = 0;           // a mute's or a ban's length; 0: a mute of ten minutes, a ban for good
    u8 role = 0;               // SetRole
    std::string reason;
    template <typename S> void serialize(S& s) { s.str(account, 32), s.u8(op), s.u32(seconds), s.u8(role), s.str(reason, kStaffReasonMax); }
};

struct StaffFindAccount {
    static constexpr Msg kId = Msg::StaffFindAccount;
    std::string name;          // an account name or a code name, or the start of one
    template <typename S> void serialize(S& s) { s.str(name, kNameMax + 8); }
};

enum class AccountField : u8 {
    CodeName = 0, Sp, Xp, Rank, GrantWeapon, RevokeWeapon, GrantForce, RevokeForce, GrantItem, RevokeItem, ClearLoadout,
    GrantBox,                  // a duffle bag or a gift box (text: its key, value: how many)
    Coins,                     // capsule coins (value: how many they have)
    Durability,                // a gun's wear (text: its code, value: what is left of it, 0..100)
    Count
};

struct StaffEditAccount {
    static constexpr Msg kId = Msg::StaffEditAccount;
    std::string account;
    u8 field = 0;
    u32 value = 0;             // SP, XP, a rank; an item's days (0: for good); boxes
    std::string text;          // a code name; a weapon's code (A009), a force's name, an item's code, a box's key
    template <typename S> void serialize(S& s) { s.str(account, 32), s.u8(field), s.u32(value), s.str(text, kNameMax + 8); }
};

struct StaffListRecordings {
    static constexpr Msg kId = Msg::StaffListRecordings;
    template <typename S> void serialize(S&) {}
};

struct StaffRequestReplay {
    static constexpr Msg kId = Msg::StaffRequestReplay;
    u32 match = 0;
    template <typename S> void serialize(S& s) { s.u32(match); }
};

struct CallVote {
    static constexpr Msg kId = Msg::CallVote;
    u32 target = 0;            // a teammate's session id, from the room's list
    template <typename S> void serialize(S& s) { s.u32(target); }
};

struct CastVote {
    static constexpr Msg kId = Msg::CastVote;
    bool yes = false;
    template <typename S> void serialize(S& s) { s.boolean(yes); }
};

struct StaffResult {
    static constexpr Msg kId = Msg::StaffResult;
    bool ok = false;
    std::string text;
    template <typename S> void serialize(S& s) { s.boolean(ok), s.str(text, 300); }
};

struct ReportEntry {
    u32 id = 0;
    std::string reporter;      // "account (Code Name)", or "System" for the server's own flags
    std::string target;        // the account's sign-in name
    std::string target_code;   // its code name
    u8 reason = 0;
    u8 state = 0;
    u64 time = 0;              // unix seconds
    std::string note;
    u32 match = 0;             // the recording of the match they shared, 0 none
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(reporter, 64), s.str(target, 32), s.str(target_code, kNameMax), s.u8(reason), s.u8(state), s.u64(time);
        s.str(note, kReportNoteMax), s.u32(match);
    }
};

struct StaffReportList {
    static constexpr Msg kId = Msg::StaffReportList;
    std::vector<ReportEntry> reports;
    template <typename S> void serialize(S& s) { s.list(reports, 200); }
};

struct StaffAccountCard {
    static constexpr Msg kId = Msg::StaffAccountCard;
    static constexpr u64 kForever = ~u64(0);
    std::string account, code_name, clan;
    u8 role = 0;
    u32 xp = 0, sp = 0;
    u32 kills = 0, deaths = 0, wins = 0, losses = 0, matches = 0;
    u64 created = 0;
    u64 muted_until = 0, banned_until = 0;   // unix seconds; banned_until kForever: for good
    u64 global_muted_until = 0;              // off global chat until then
    std::string ban_reason;
    bool online = false;
    u16 weapons = 0, forces = 0, items = 0;
    template <typename S> void serialize(S& s) {
        s.str(account, 32), s.str(code_name, kNameMax), s.str(clan, 24), s.u8(role);
        s.u32(xp), s.u32(sp), s.u32(kills), s.u32(deaths), s.u32(wins), s.u32(losses), s.u32(matches);
        s.u64(created), s.u64(muted_until), s.u64(banned_until), s.str(ban_reason, kStaffReasonMax), s.boolean(online);
        s.u16(weapons), s.u16(forces), s.u16(items), s.u64(global_muted_until);
    }
};

struct RecordingEntry {
    u32 match = 0;
    u64 started = 0;           // unix seconds
    std::string map;           // map id
    u8 mode = 0;
    u16 seconds = 0;           // how long it ran (so far, when still being played)
    u8 players = 0;
    u32 bytes = 0;
    u16 reports = 0;           // reports pointing at it
    bool live = false;         // still being played
    std::string names;         // the code names in it, comma separated
    template <typename S> void serialize(S& s) {
        s.u32(match), s.u64(started), s.str(map, 48), s.u8(mode), s.u16(seconds), s.u8(players), s.u32(bytes), s.u16(reports);
        s.boolean(live), s.str(names, 400);
    }
};

struct StaffRecordingList {
    static constexpr Msg kId = Msg::StaffRecordingList;
    std::vector<RecordingEntry> recordings;
    template <typename S> void serialize(S& s) { s.list(recordings, 200); }
};

inline constexpr size_t kReplayChunk = 24 * 1024;

struct ReplayChunk {
    static constexpr Msg kId = Msg::ReplayChunk;
    u32 match = 0;
    u32 total = 0;             // the whole file's size
    u32 offset = 0;
    std::vector<u8> bytes;
    std::string error;         // set (and nothing else) when it cannot be sent
    template <typename S> void serialize(S& s) { s.u32(match), s.u32(total), s.u32(offset), s.blob(bytes, kReplayChunk), s.str(error, 200); }
};

struct VoteState {
    static constexpr Msg kId = Msg::VoteState;
    bool active = false;
    u32 target = 0;
    std::string target_name, caller_name;
    u8 yes = 0, no = 0, needed = 0, voters = 0;
    float seconds_left = 0;
    bool can_vote = false;     // you are on the side, not the target, and have not voted
    std::string result;        // when it ends: "Kicked X." / "The vote failed."
    template <typename S> void serialize(S& s) {
        s.boolean(active), s.u32(target), s.str(target_name, kNameMax), s.str(caller_name, kNameMax);
        s.u8(yes), s.u8(no), s.u8(needed), s.u8(voters), s.f32(seconds_left), s.boolean(can_vote), s.str(result, 120);
    }
};

// ── Rewards and events (Game/Events.hpp; Server/Rewards.cpp) ──────────────────

struct RewardsRequest {
    static constexpr Msg kId = Msg::RewardsRequest;
    template <typename S> void serialize(S&) {}
};

enum class ClaimKind : u8 { SignIn, PlayStep, Quest, AllQuests };
struct RewardsClaim {
    static constexpr Msg kId = Msg::RewardsClaim;
    u8 kind = 0;
    u8 index = 0;              // the play step's, or the quest's
    template <typename S> void serialize(S& s) { s.u8(kind), s.u8(index); }
};

struct OpenBox {
    static constexpr Msg kId = Msg::OpenBox;
    u8 box = 0;
    template <typename S> void serialize(S& s) { s.u8(box); }
};

// One of the day's quests: what it asks, how far along, whether it has paid.
struct QuestNow {
    QuestDef def;
    u32 progress = 0;
    bool claimed = false;
    u32 event = 0;             // the event it is for (0: the day's own)
    template <typename S> void serialize(S& s) { def.serialize(s), s.u32(progress), s.boolean(claimed), s.u32(event); }
};

// An event as a player sees it: running now, or the next to come.
struct EventNow {
    u32 id = 0;
    std::string name, banner;
    u8 theme = 0;
    u64 from = 0, to = 0;      // this run's, or the next one's
    bool running = false;
    u16 sp_pct = 100, xp_pct = 100;
    u8 drop_box = kNoBox;
    u16 drop_permille = 0;
    u32 sign_in_sp = 0;
    template <typename S> void serialize(S& s) {
        s.u32(id), s.str(name, 40), s.str(banner, 160), s.u8(theme), s.u64(from), s.u64(to), s.boolean(running);
        s.u16(sp_pct), s.u16(xp_pct), s.u8(drop_box), s.u16(drop_permille), s.u32(sign_in_sp);
    }
};

struct RewardsState {
    static constexpr Msg kId = Msg::RewardsStateMsg;
    u64 now = 0;               // the server's clock (UTC seconds)
    u64 day_ends = 0;          // when the day's quests and hours begin again
    u16 minutes = 0;           // played today (matches played to their end)
    std::vector<PlayStep> play;
    u8 play_claimed = 0;       // a bit a step
    std::vector<SignInDay> week;
    u8 week_at = 0;            // the day of the week signing in pays next (0..6)
    bool signed_today = false;
    std::vector<QuestNow> quests;
    u32 all_quests_sp = 0;
    u8 all_quests_box = kNoBox;
    bool all_claimed = false;
    std::array<u16, kBoxKinds> boxes{};
    std::vector<EventNow> events;   // running first, then the next few
    std::vector<LootTable> tables;  // what each box holds, and how often (shown before one is opened)
    template <typename S> void serialize(S& s) {
        s.u64(now), s.u64(day_ends), s.u16(minutes), s.list(play, 6), s.u8(play_claimed), s.list(week, 7), s.u8(week_at), s.boolean(signed_today);
        s.list(quests, 12), s.u32(all_quests_sp), s.u8(all_quests_box), s.boolean(all_claimed), s.array(boxes), s.list(events, 16), s.list(tables, kBoxKinds);
    }
};

struct BoxOpened {
    static constexpr Msg kId = Msg::BoxOpened;
    u8 box = 0;
    bool ok = false;
    u8 kind = 0;               // PrizeKind
    u32 amount = 0;            // SP, XP, days or boxes
    u16 item = 0;              // the boost, part or weapon given (or the box)
    std::string text;          // "Points X2 for 3 days", or why not
    template <typename S> void serialize(S& s) { s.u8(box), s.boolean(ok), s.u8(kind), s.u32(amount), s.u16(item), s.str(text, 160); }
};

struct StaffRewardsRequest {
    static constexpr Msg kId = Msg::StaffRewardsRequest;
    template <typename S> void serialize(S&) {}
};

struct StaffRewardsSave {
    static constexpr Msg kId = Msg::StaffRewardsSave;
    RewardsConfig config;
    template <typename S> void serialize(S& s) { config.serialize(s); }
};

struct StaffRewards {
    static constexpr Msg kId = Msg::StaffRewardsMsg;
    RewardsConfig config;
    u64 now = 0;
    template <typename S> void serialize(S& s) { config.serialize(s), s.u64(now); }
};

// ── The shop as a Game Master keeps it, capsules, sprays (Game/Shop.hpp, Game/Wear.hpp) ──

struct ShopState {
    static constexpr Msg kId = Msg::ShopStateMsg;
    ShopConfig config;
    template <typename S> void serialize(S& s) { config.serialize(s); }
};

// One message for the small things a soldier asks of the shop and of the recordings.
enum class RequestOp : u8 {
    Shop = 0,          // the shop as it stands
    SetSpray,          // a = the spray to carry (0: none)
    BuyCoins,          // value = how many capsule coins
    Capsule,           // a = the capsule: a turn of it
    ReplayList,        // the matches you played that are still kept
    ReplayGet,         // value = the match: its recording (ReplayChunk)
    Repair,            // a = the weapon: mended to full, for SP (Game/Wear.hpp)
    Sell,              // a = the thing, b = its ShopKind: sold back for what Game/Wear.hpp resale_price says
    OpenGift,          // value = the gift (GiftInfo::id): opened, and yours
    BuyHorrorItem,     // a = the HorrorItem, value = how many (1..10)
    SupplyCrate,       // a = 7 or 30: a Supply Crate bought and opened (Game/Shop.hpp)
    BuyBag,            // a = the Duffle Bag (ShopConfig::bags), value = how many (1..10): into the Gift tab
    OpenBag,           // a = the Duffle Bag: one opened (answered with a CapsuleResult: what it held)
    Count
};
// TV's own services (mending, selling back, coins, TV's capsules,
// Duffle Bags and Supply Crates, gifts opened, sprays, Horror items) are the game's with TVAS; a
// server answers only what is its own: its custom capsules (ids from kCustomCapsuleFirst, paid with
// an approval), its own items sold back, recordings.
inline constexpr u16 kCustomCapsuleFirst = 1000;
struct Request {
    static constexpr Msg kId = Msg::Request;
    u8 op = 0;
    u16 a = 0, b = 0;
    u32 value = 0;
    std::string approval;
    template <typename S> void serialize(S& s) { s.u8(op), s.u16(a), s.u16(b), s.u32(value), s.str(approval, kApprovalMax); }
};

// What a request to the shop came to: mending, selling back, capsule coins, a gift opened.
struct ServiceResult {
    static constexpr Msg kId = Msg::ServiceResult;
    u8 op = 0;                 // the RequestOp it answers
    bool ok = false;           // the request was taken (SP spent, where it costs any)
    u16 weapon = kNoWeapon;
    std::string text;
    template <typename S> void serialize(S& s) { s.u8(op), s.boolean(ok), s.u16(weapon), s.str(text, 160); }
};

struct CapsuleResult {
    static constexpr Msg kId = Msg::CapsuleResult;
    u16 capsule = 0;
    bool ok = false;
    CapsulePrize prize;        // what came out: its kind, the thing, its days; lo = the SP or the coins
    std::string text;          // "M4A1 for 7 days", or why not
    template <typename S> void serialize(S& s) { s.u16(capsule), s.boolean(ok), prize.serialize(s), s.str(text, 160); }
};

// In a match: the wall in front of you, where your own trace met it.
struct Spray {
    static constexpr Msg kId = Msg::Spray;
    Vec3 at, normal;
    template <typename S> void serialize(S& s) { s.vec3(at), s.vec3(normal); }
};

struct SprayFx {
    static constexpr Msg kId = Msg::SprayFx;
    u32 player = 0;
    u16 spray = kNoSpray;
    Vec3 at, normal;
    template <typename S> void serialize(S& s) { s.u32(player), s.u16(spray), s.vec3(at), s.vec3(normal); }
};

struct StaffShopSave {
    static constexpr Msg kId = Msg::StaffShopSave;
    ShopConfig config;
    template <typename S> void serialize(S& s) { config.serialize(s); }
};

// ── Your own matches' recordings ───────────────────────────────────────────────

enum class ReplayResult : u8 { Lost = 0, Won, Draw, Unknown };
struct ReplayEntry {
    u32 match = 0;
    u64 started = 0;           // unix seconds
    std::string map;
    u8 mode = 0;
    u16 seconds = 0;
    u8 players = 0;
    u32 bytes = 0;
    u8 team = u8(Team::None);  // the side you were on
    u8 result = u8(ReplayResult::Unknown);
    template <typename S> void serialize(S& s) {
        s.u32(match), s.u64(started), s.str(map, 48), s.u8(mode), s.u16(seconds), s.u8(players), s.u32(bytes), s.u8(team), s.u8(result);
    }
};

struct ReplayList {
    static constexpr Msg kId = Msg::ReplayListMsg;
    std::vector<ReplayEntry> replays;   // newest first
    template <typename S> void serialize(S& s) { s.list(replays, 100); }
};

}  // namespace lsf::proto
