// Global chat, a room's invitations, and a friend's room after a server switch.
//
// Friends, presence, blocks, whispers and mail are universal and the game's with TVAS directly
// (Docs/UniversalServerDeploy.md SO-1, SO-2): no whisper, mail, friend or block list ever passes
// this server. What it keeps of blocks is only which soldiers here must not share a room (BL-4).
// Global chat stays this server's own (§14.6), and blocks apply to it.
#include "Server.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <ctime>

namespace lsfs {

using namespace lsf::proto;

namespace {
constexpr double kGlobalGap = 3.0;
u64 unix_now() { return u64(std::time(nullptr)); }
}  // namespace

void Server::handle_social(Peer& p, Msg kind, std::span<const u8> data) {
    switch (kind) {
        case Msg::ChatPrefs: {
            ChatPrefs m;
            if (decode(data, m)) p.global_chat = m.global, p.room_invites = m.invites;
            break;
        }
        case Msg::RoomInvite: {
            RoomInvite m;
            if (decode(data, m)) room_invite(p, m);
            break;
        }
        case Msg::RoomInviteAnswer: {
            RoomInviteAnswer m;
            if (decode(data, m)) room_invite_answer(p, m);
            break;
        }
        default: break;
    }
}

// Where a soldier is, as their friends see it (SO-4): told to TVAS when it changes.
std::string Server::presence_of(const Peer& p) {
    const ChannelDef* ch = channel(p.channel);
    if (!ch) return "Choosing a channel";
    Room* r = find_room(p.room);
    if (!r) return "Lobby, " + ch->name;
    // 14.3: "Room 3: Owls scrim (waiting, 4/16)", "Playing crossroad (Team Deathmatch), Room 3".
    if (r->phase == RoomPhase::Waiting)
        return eng::str::format("Room %u: %s (waiting, %zu/%d), %s", unsigned(r->id), r->settings.title.c_str(), r->seats.size(), int(r->settings.max_players), ch->name.c_str());
    return eng::str::format("Playing %s (%s), Room %u, %s", r->settings.map.c_str(), mode_name(r->settings.mode), unsigned(r->id), ch->name.c_str());
}

// A soldier on this server asked into your room (FR-4: one on another server is asked through TVAS).
void Server::room_invite(Peer& p, const RoomInvite& m) {
    Account& me = *p.account;
    auto refuse = [&](const std::string& why) { notice(p.id, NoticeKind::Warning, why); };
    Room* r = find_room(p.room);
    if (!r) return refuse("You invite soldiers from a room: make one, or join one, first.");
    const std::string name(eng::str::trim(m.code_name));
    Account* them = accounts_.find_code_name(name);
    Peer* tp = peer_of(them);
    // BL-1, BL-3: a soldier who blocked you is, to you, nobody here.
    if (!them || !tp || !tp->named() || (them->blocks_with(me.id) && !me.staff())) return refuse(name + " is not on this server: invite them through your friends list.");
    if (them == &me) return;
    const std::string& who = them->code_name;
    if (now_ - p.last_invite < 1.0) return refuse("One invitation a second.");
    p.last_invite = now_;
    if (Room* theirs = find_room(tp->room)) return refuse(who + (theirs == r ? " is in this room already." : theirs->phase != RoomPhase::Waiting ? " is in a match." : " is in a room already."));
    if (!tp->room_invites) return refuse(who + " is not taking invitations.");
    if (int(r->seats.size()) >= r->settings.max_players) return refuse("The room is full.");
    if (r->phase != RoomPhase::Waiting && !r->settings.free_join) return refuse("The game is under way and takes nobody mid-game: invite them when it is over.");
    if (auto it = r->barred.find(them->id); it != r->barred.end() && it->second > now_) return refuse(who + " was removed from this room and cannot come back yet.");
    const ChannelDef* ch = channel(r->channel);
    if (ch && tp->channel != r->channel && !them->staff()) {
        std::string why;
        const float kd = them->deaths ? float(them->kills) / float(them->deaths) : float(them->kills);
        if (!channel_allows(*ch, rank_for_xp(them->xp), kd, &why)) return refuse(who + " cannot enter this channel (" + ch->name + ").");
    }
    Room::Invitation& inv = r->invited[them->id];
    if (inv.until - now_ > 50.0 && inv.from == me.code_name) return refuse("You invited " + who + " a moment ago.");
    inv = {now_ + 60.0, me.code_name};
    RoomInvited out;
    out.room = r->id;
    out.from = me.code_name;
    out.title = r->settings.title;
    out.mode = u8(r->settings.mode);
    out.map = r->settings.map;
    out.players = u8(r->seats.size());
    out.max_players = r->settings.max_players;
    out.channel = r->channel;
    out.channel_name = ch ? ch->name : std::string();
    out.playing = r->phase != RoomPhase::Waiting;
    send_msg(tp->id, out);
    notice(p.id, NoticeKind::Info, "You invited " + who + " to the room.");
}

void Server::room_invite_answer(Peer& p, const RoomInviteAnswer& m) {
    Account& me = *p.account;
    Room* r = find_room(m.room);
    const auto it = r ? r->invited.find(me.id) : std::map<u64, Room::Invitation>::iterator{};
    if (!r || it == r->invited.end() || it->second.until < now_) {
        if (m.accept) notice(p.id, NoticeKind::Warning, r ? "That invitation has run out." : "That room is gone.");
        if (r && it != r->invited.end()) r->invited.erase(it);
        return;
    }
    const std::string from = it->second.from;
    r->invited.erase(it);
    Peer* fp = peer_of(accounts_.find_code_name(from));
    if (!m.accept) {
        if (fp && fp->room == r->id) notice(fp->id, NoticeKind::Info, me.code_name + " turned down your invitation.");
        return;
    }
    if (p.room) return notice(p.id, NoticeKind::Warning, "Leave your room first.");
    if (p.channel != r->channel) {
        join_channel(p, r->channel);
        if (p.channel != r->channel) return;
    }
    std::string why;
    if (!join_room(p, *r, false, &why)) return notice(p.id, NoticeKind::Bad, why);
    if (fp && fp->room == r->id) notice(fp->id, NoticeKind::Good, me.code_name + " came on your invitation.");
}

// FR-2: after switching servers to join a friend (or on an invitation), into their room when it
// still takes them; otherwise the channel list, told why.
void Server::follow_intent(Peer& p) {
    const u64 friend_id = p.ticket.intent_friend;
    if (!friend_id) return;
    Account* f = accounts_.find(friend_id);
    Peer* fp = peer_of(f);
    if (!fp || !fp->named()) return notice(p.id, NoticeKind::Info, "Your friend is no longer on this server.");
    Room* r = find_room(fp->room);
    if (!r) {
        if (fp->channel) join_channel(p, fp->channel);
        return notice(p.id, NoticeKind::Info, f->code_name + " is not in a room now.");
    }
    if (p.channel != r->channel) {
        join_channel(p, r->channel);
        if (p.channel != r->channel) return;
    }
    std::string why;
    if (!join_room(p, *r, false, &why)) notice(p.id, NoticeKind::Warning, "Could not join " + f->code_name + "'s room: " + why);
}

void Server::global_chat(Peer& p, ChatLine line, double now) {
    Account& me = *p.account;
    const u64 t = unix_now();
    if (me.global_muted_until > t)
        return notice(p.id, NoticeKind::Warning,
                      eng::str::format("You are off global chat for another %llu minute(s).", (unsigned long long)std::max<u64>(1, (me.global_muted_until - t + 59) / 60)));
    if (!p.global_chat) return notice(p.id, NoticeKind::Info, "Global chat is off: turn it on beside the chat box, or in Options, to talk there.");
    if (now - p.last_global < kGlobalGap) return notice(p.id, NoticeKind::Warning, "Global chat takes a line every three seconds.");
    p.last_global = now;
    line.clan = me.clan;
    LOG_INFO("Global: %s: %s", me.code_name.c_str(), line.text.c_str());
    const auto msg = encode(line);
    // §14.4: a block stops global lines too, either way.
    for (auto& [id, other] : peers_)
        if (other.named() && (other.global_chat || &other == &p) && !(other.account->blocks_with(me.id) && !me.staff())) send(id, msg);
}

}  // namespace lsfs
