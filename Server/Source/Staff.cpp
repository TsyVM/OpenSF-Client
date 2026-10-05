// Reports, staff and votes, on this server's terms (Docs/UniversalServerDeploy.md §7).
//
// A server's own staff -- the Owner (the account that registered it: RL-6), Server Admins, Game
// Masters and Moderators (staff.cfg) -- act on this server's own data only: its kicks, mutes and
// bans, its rooms, its shop, its channels, its own items held by soldiers here (§7.2). Rank, XP, SP,
// what is owned universally and code names are TVAS's, changed by SFLegacy Staff from their own
// game, never through a server (RL-1, PR-1). SFLegacy Staff outrank everyone here (D4): nobody here
// acts on them (RL-2), and nobody names anyone to their own tier or above (RL-3).
//
// Every action is logged, and goes to TVAS with the next heartbeat (RL-4).
#include "Server.hpp"

#include "Match.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Registry.hpp"
#include "Game/Replay.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <fstream>
#include <iterator>

namespace lsfs {

using namespace lsf::proto;

namespace {

u64 unix_now() { return u64(std::time(nullptr)); }

constexpr double kVoteSeconds = 30.0;
constexpr double kVoteCooldown = 120.0;
constexpr double kBarSeconds = 600.0;

std::string minutes_text(u64 seconds) {
    if (seconds >= 86400) return eng::str::format("%llu day(s)", (unsigned long long)(seconds / 86400));
    if (seconds >= 3600) return eng::str::format("%llu hour(s)", (unsigned long long)(seconds / 3600));
    return eng::str::format("%llu minute(s)", (unsigned long long)std::max<u64>(1, seconds / 60));
}

// RL-2, RL-3: who may act on whom here.
bool may_act_on(const Account& actor, const Account& target, std::string& why) {
    if (&actor == &target) {
        why = "You cannot use that on your own account.";
        return false;
    }
    if (target.sflegacy() && !actor.sflegacy()) {
        why = target.code_name + " is SFLegacy Staff: nobody on a server acts on them.";
        return false;
    }
    if (target.sflegacy() && actor.sflegacy() && target.global_role >= actor.global_role) {
        why = target.code_name + " is SFLegacy Staff of your tier or above.";
        return false;
    }
    if (target.effective_role() >= actor.effective_role() && !actor.sflegacy()) {
        why = eng::str::format("%s is a %s here: they outrank you or share your rank.", target.code_name.c_str(), role_name(target.effective_role()));
        return false;
    }
    return true;
}

}  // namespace

std::string Server::ban_refusal(Account& a) {
    if (a.banned_until == 0) return {};
    const u64 now = unix_now();
    if (a.banned_until != Account::kForever && a.banned_until <= now) {
        a.banned_until = 0;
        a.ban_reason.clear();
        accounts_.mark_dirty(a.id);
        return {};
    }
    const std::string when = a.banned_until == Account::kForever ? std::string("for good") : "for another " + minutes_text(a.banned_until - now);
    return a.ban_reason.empty() ? "You are banned from this server " + when + "." : "You are banned from this server " + when + ": " + a.ban_reason;
}

bool Server::muted(Account& a) {
    const u64 now = unix_now();
    // A global mute (SFLegacy Staff: TVAS) or this server's own.
    if (a.muted_until > now) return true;
    if (a.server_muted_until == 0) return false;
    if (a.server_muted_until > now) return true;
    a.server_muted_until = 0;
    accounts_.mark_dirty(a.id);
    return false;
}

void Server::staff_notice(const std::string& text) {
    for (auto& [id, other] : peers_)
        if (other.account && other.account->staff() && other.signed_in()) notice(id, NoticeKind::Info, text);
}

void Server::staff_result(u32 peer_id, bool ok, std::string text) {
    StaffResult r;
    r.ok = ok;
    r.text = std::move(text);
    send_msg(peer_id, r);
}

u32 Server::match_between(const Peer& reporter, const Account& target) const {
    if (!reporter.last_match) return 0;
    auto it = recordings_.find(reporter.last_match);
    if (it == recordings_.end()) return 0;
    const auto& acc = it->second.accounts;
    return std::find(acc.begin(), acc.end(), target.id) != acc.end() ? reporter.last_match : 0;
}

void Server::handle_staff(Peer& p, Msg kind, std::span<const u8> data) {
    Account& me = *p.account;
    const u64 now = unix_now();
    auto card_for = [&](const Account& a) {
        StaffAccountCard c;
        c.account = "#" + std::to_string(a.id);
        c.code_name = a.code_name;
        c.clan = a.clan;
        c.role = u8(a.effective_role());
        c.xp = a.xp, c.sp = a.sp;
        c.kills = a.kills, c.deaths = a.deaths, c.wins = a.wins, c.losses = a.losses, c.matches = a.matches;
        c.muted_until = std::max(a.server_muted_until, a.muted_until) > now ? std::max(a.server_muted_until, a.muted_until) : 0;
        c.global_muted_until = a.global_muted_until > now ? a.global_muted_until : 0;
        c.banned_until = a.banned_until == Account::kForever ? StaffAccountCard::kForever : (a.banned_until > now ? a.banned_until : 0);
        c.ban_reason = a.ban_reason;
        c.online = peer_of(&a) != nullptr;
        c.weapons = u16(a.weapons.size());
        c.forces = u16(a.forces.size());
        c.items = u16(a.items.size());
        return c;
    };

    switch (kind) {
        case Msg::ReportPlayer: {
            ReportPlayer m;
            if (!decode(data, m)) return;
            Account* target = nullptr;
            if (m.player)
                if (Peer* o = peer(m.player)) target = o->account;
            if (!target && !m.code_name.empty()) target = accounts_.find_code_name(eng::str::trim(m.code_name));
            if (!target) return staff_result(p.id, false, "That soldier could not be found on this server.");
            if (target == &me) return staff_result(p.id, false, "You cannot report yourself.");
            if (m.reason >= u8(ReportReason::Count)) return staff_result(p.id, false, "Pick a reason.");
            if (accounts_.has_open_report(me.id, target->id)) return staff_result(p.id, false, "You already have a report open against " + target->code_name + ".");
            const u32 match = match_between(p, *target);
            const std::string note = eng::str::sanitize_line(m.note, kReportNoteMax);
            accounts_.add_report(&me, *target, ReportReason(m.reason), note, match);
            LOG_INFO("report: %s (#%llu) reported %s (#%llu) for %s", me.code_name.c_str(), (unsigned long long)me.id, target->code_name.c_str(),
                     (unsigned long long)target->id, report_reason_name(ReportReason(m.reason)));
            // §14.6: on an official or verified server, SFLegacy Staff see it too.
            if (tier_ >= u32(Tier::Verified)) {
                eng::json::Value body;
                body["reporter"] = me.id;
                body["target"] = target->id;
                body["reason"] = unsigned(m.reason);
                body["note"] = note;
                tvas_.post("/v1/server/report-player", body, {});
            }
            for (auto& [id, other] : peers_)
                if (other.account && other.account->staff() && &other != &p)
                    notice(id, NoticeKind::Info, "New report: " + target->code_name + " (" + report_reason_name(ReportReason(m.reason)) + "). F9 to review.");
            return staff_result(p.id, true, "Reported " + target->code_name + ". Thank you: staff will review it.");
        }
        case Msg::CallVote: {
            CallVote m;
            if (decode(data, m)) call_vote(p, m.target);
            return;
        }
        case Msg::CastVote: {
            CastVote m;
            if (decode(data, m)) cast_vote(p, m.yes);
            return;
        }
        default:
            break;
    }

    if (!me.staff()) return;

    switch (kind) {
        case Msg::StaffListReports: {
            StaffListReports m;
            if (!decode(data, m)) return;
            StaffReportList out;
            const auto& all = accounts_.reports();
            for (auto it = all.rbegin(); it != all.rend() && out.reports.size() < 200; ++it) {
                const Report& r = *it;
                if (m.open_only && r.state != ReportState::Open) continue;
                ReportEntry e;
                e.id = r.id;
                e.reporter = r.reporter ? r.reporter_name + " (#" + std::to_string(r.reporter) + ")" : std::string("System");
                e.target = "#" + std::to_string(r.target);
                e.target_code = r.target_name;
                e.reason = u8(r.reason);
                e.state = u8(r.state);
                e.time = r.time;
                e.note = r.note;
                e.match = recordings_.contains(r.match) ? r.match : 0;
                out.reports.push_back(std::move(e));
            }
            send_msg(p.id, out);
            return;
        }
        case Msg::StaffResolveReport: {
            StaffResolveReport m;
            if (!decode(data, m)) return;
            Report* r = accounts_.report(m.report);
            if (!r) return staff_result(p.id, false, "That report is gone.");
            if (m.state != u8(ReportState::Actioned) && m.state != u8(ReportState::Dismissed)) return staff_result(p.id, false, "A report can only be actioned or dismissed.");
            r->state = ReportState(m.state);
            accounts_.mark_dirty();
            staff_logged(me, nullptr, r->state == ReportState::Actioned ? "report.actioned" : "report.dismissed", "", eng::str::format("#%u", r->id));
            return staff_result(p.id, true, r->state == ReportState::Actioned ? "Report marked actioned." : "Report dismissed.");
        }
        case Msg::StaffFindAccount: {
            StaffFindAccount m;
            if (!decode(data, m)) return;
            const std::string want = eng::str::lower(eng::str::trim(m.name));
            if (want.empty()) return staff_result(p.id, false, "Type a code name (or #id).");
            Account* hit = accounts_.find_token(want);
            if (!hit) {
                int n = 0;
                for (auto& [id, a] : accounts_.all())
                    if (eng::str::lower(a->code_name).rfind(want, 0) == 0) hit = a.get(), ++n;
                if (n > 1) return staff_result(p.id, false, "\"" + m.name + "\" matches more than one soldier: type more of it.");
            }
            if (!hit) return staff_result(p.id, false, "No soldier here matching \"" + m.name + "\".");
            send_msg(p.id, card_for(*hit));
            return;
        }
        case Msg::StaffAction: {
            StaffAction m;
            if (!decode(data, m) || m.op > u8(StaffOp::GlobalUnmute)) return;
            const StaffOp op = StaffOp(m.op);
            Account* target = accounts_.find_token(m.account);
            if (!target) return staff_result(p.id, false, "No soldier here called " + m.account + ".");
            std::string why;
            if (!may_act_on(me, *target, why)) return staff_result(p.id, false, why);
            const std::string reason = eng::str::sanitize_line(m.reason, kStaffReasonMax);
            Peer* online = peer_of(target);
            switch (op) {
                case StaffOp::Kick:
                    if (!online) return staff_result(p.id, false, target->code_name + " is not on this server.");
                    staff_logged(me, target, "kick", reason);
                    disconnect(online->id, reason.empty() ? "You were removed from the server by its staff." : "Removed by the server's staff: " + reason);
                    return staff_result(p.id, true, "Kicked " + target->code_name + ".");
                case StaffOp::Mute: {
                    const u32 seconds = m.seconds ? m.seconds : 600;
                    target->server_muted_until = now + seconds;
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "mute", reason, eng::str::format("%u s", seconds));
                    if (online) notice(online->id, NoticeKind::Bad, "You have been muted on this server for " + minutes_text(seconds) + (reason.empty() ? std::string(".") : ": " + reason));
                    return staff_result(p.id, true, "Muted " + target->code_name + " for " + minutes_text(seconds) + ".");
                }
                case StaffOp::Unmute:
                    target->server_muted_until = 0;
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "unmute", reason);
                    if (online) notice(online->id, NoticeKind::Good, "You may chat again.");
                    return staff_result(p.id, true, "Unmuted " + target->code_name + ".");
                case StaffOp::GlobalMute: {
                    const u32 seconds = m.seconds ? m.seconds : 3600;
                    target->global_muted_until = now + seconds;
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "global_chat_mute", reason, eng::str::format("%u s", seconds));
                    if (online) notice(online->id, NoticeKind::Bad, "You are off this server's global chat for " + minutes_text(seconds) + (reason.empty() ? std::string(".") : ": " + reason));
                    return staff_result(p.id, true, "Took " + target->code_name + " off global chat for " + minutes_text(seconds) + ".");
                }
                case StaffOp::GlobalUnmute:
                    target->global_muted_until = 0;
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "global_chat_unmute", reason);
                    if (online) notice(online->id, NoticeKind::Good, "You may talk on global chat again.");
                    return staff_result(p.id, true, target->code_name + " may talk on global chat again.");
                case StaffOp::Ban:
                    if (!me.game_master()) return staff_result(p.id, false, "Moderators kick and mute; a ban is for Game Masters and above.");
                    target->banned_until = m.seconds ? now + m.seconds : Account::kForever;
                    target->ban_reason = reason;
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "ban", reason, m.seconds ? eng::str::format("%u s", m.seconds) : "for good");
                    if (online) disconnect(online->id, ban_refusal(*target));
                    return staff_result(p.id, true, m.seconds ? "Banned " + target->code_name + " from this server for " + minutes_text(m.seconds) + "." : "Banned " + target->code_name + " from this server for good.");
                case StaffOp::Unban:
                    if (!me.game_master()) return staff_result(p.id, false, "Only Game Masters and above lift a ban.");
                    target->banned_until = 0;
                    target->ban_reason.clear();
                    accounts_.mark_dirty(target->id);
                    staff_logged(me, target, "unban", reason);
                    return staff_result(p.id, true, "Unbanned " + target->code_name + ".");
                case StaffOp::SetRole: {
                    // §7.2: the Owner names Admins and below; an Admin names below Admin (RL-3). The
                    // Owner is the registration's alone (RL-6); SFLegacy roles are TVAS's.
                    if (!me.admin()) return staff_result(p.id, false, "Only the Owner and Server Admins name this server's staff.");
                    if (m.role >= u8(Role::Owner)) return staff_result(p.id, false, "The Owner is the account that registered the server.");
                    const Role want = Role(m.role);
                    if (want >= me.effective_role() && !me.sflegacy()) return staff_result(p.id, false, "You can name staff only below your own rank.");
                    if (want == target->server_role) return staff_result(p.id, false, target->code_name + " is already a " + role_name(want) + " here.");
                    accounts_.set_role(target->id, want, target->code_name);
                    staff_logged(me, target, "set_role", reason, role_name(want));
                    if (online) send_profile(*online);
                    return staff_result(p.id, true, target->code_name + " is now a " + role_name(want) + " on this server.");
                }
            }
            return;
        }
        case Msg::StaffEditAccount: {
            StaffEditAccount m;
            if (!decode(data, m)) return;
            if (!me.game_master()) return staff_result(p.id, false, "Only Game Masters and above edit a soldier's data here.");
            Account* target = accounts_.find_token(m.account);
            if (!target) return staff_result(p.id, false, "No soldier here called " + m.account + ".");
            if (target != &me) {
                std::string why;
                if (!may_act_on(me, *target, why)) return staff_result(p.id, false, why);
            }
            const std::string text(eng::str::trim(m.text));
            const AccountField field = AccountField(m.field);
            // PR-1, RL-1: everything universal is TVAS's, changed by SFLegacy Staff at TVAS.
            const bool server_field = field == AccountField::GrantWeapon || field == AccountField::RevokeWeapon || field == AccountField::GrantForce ||
                                      field == AccountField::RevokeForce || field == AccountField::GrantItem || field == AccountField::RevokeItem ||
                                      field == AccountField::ClearLoadout;
            if (!server_field || (field != AccountField::ClearLoadout && !is_pack_code(text)))
                return staff_result(p.id, false, "That is universal: only SFLegacy Staff change it, at Team Vanilla. Here you give and take this server's own items (x:<pack>:<name>).");
            std::string what;
            const bool grant = field == AccountField::GrantWeapon || field == AccountField::GrantForce || field == AccountField::GrantItem;
            const u8 ware = field == AccountField::GrantWeapon || field == AccountField::RevokeWeapon ? u8(ShopKind::Weapon)
                            : field == AccountField::GrantForce || field == AccountField::RevokeForce ? u8(ShopKind::Force)
                                                                                                      : u8(ShopKind::Item);
            if (field == AccountField::ClearLoadout) {
                target->override_slots = {};
                target->override_force.clear();
                target->custom_parts.clear();
                what = "this server's loadout overrides cleared";
            } else if (grant) {
                if (!registry::weapon_by_code(text) && !registry::force_by_code(text) && !registry::item_by_code(text))
                    return staff_result(p.id, false, "This server has no item " + text + ".");
                target->custom.push_back({text, m.value ? now + u64(m.value) * 86400 : 0, ware});
                what = "granted " + text + (m.value ? eng::str::format(" for %u days", m.value) : std::string(" for good"));
            } else {
                const size_t before = target->custom.size();
                std::erase_if(target->custom, [&](const Account::Custom& c) { return c.code == text; });
                if (before == target->custom.size()) return staff_result(p.id, false, target->code_name + " does not own " + text + " here.");
                what = "revoked " + text;
            }
            accounts_.mark_dirty(target->id);
            staff_logged(me, target, "edit", "", what);
            if (Peer* online = peer_of(target)) {
                fetch_profile(online->id, target->id, false);
                if (online != &p) notice(online->id, NoticeKind::Info, "This server's staff changed your items here.");
            }
            send_msg(p.id, card_for(*target));
            return staff_result(p.id, true, what);
        }
        case Msg::StaffListRecordings: {
            StaffListRecordings m;
            if (!decode(data, m)) return;
            StaffRecordingList out;
            std::map<u32, u16> reported;
            for (const Report& r : accounts_.reports())
                if (r.match) ++reported[r.match];
            for (auto it = recordings_.rbegin(); it != recordings_.rend() && out.recordings.size() < 200; ++it) {
                const RecordingInfo& i = it->second;
                RecordingEntry e;
                e.match = i.match;
                e.started = i.started;
                e.map = i.map;
                e.mode = i.mode;
                e.seconds = u16(std::min<u32>(i.seconds, 65535));
                e.players = u8(std::min<size_t>(i.names.size(), 255));
                e.bytes = i.bytes;
                e.reports = reported[i.match];
                e.live = i.live;
                for (const std::string& n : i.names) e.names += (e.names.empty() ? "" : ", ") + n;
                out.recordings.push_back(std::move(e));
            }
            send_msg(p.id, out);
            return;
        }
        case Msg::StaffRequestReplay: {
            StaffRequestReplay m;
            if (!decode(data, m)) return;
            std::string why;
            if (!send_recording(p, m.match, &why)) {
                ReplayChunk none;
                none.match = m.match;
                none.error = why;
                send_msg(p.id, none);
                return;
            }
            staff_logged(me, nullptr, "watch", "", eng::str::format("match %u", m.match));
            return;
        }
        default:
            return;
    }
}

bool Server::send_recording(Peer& p, u32 match, std::string* why) {
    const std::filesystem::path path = recording_path(match);
    std::ifstream in(path, std::ios::binary);
    if (!match || path.empty() || !in) {
        *why = path.empty() ? "This server does not record matches." : eng::str::format("No recording of match %u is kept.", match);
        return false;
    }
    auto out = std::make_shared<Peer::ReplaySend>();
    out->match = match;
    out->bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out->bytes.empty() || out->bytes.size() > (256u << 20)) {
        *why = "That recording cannot be sent.";
        return false;
    }
    p.replay_out = std::move(out);
    return true;
}

namespace {
int seat_in(const std::vector<u64>& accounts, u64 account) {
    for (size_t i = 0; i < accounts.size(); ++i)
        if (accounts[i] == account) return int(i);
    return -1;
}
}  // namespace

void Server::replay_list(Peer& p) {
    ReplayList out;
    const u64 me = p.account->id;
    for (auto it = recordings_.rbegin(); it != recordings_.rend() && out.replays.size() < 100; ++it) {
        const RecordingInfo& i = it->second;
        const int seat = seat_in(i.accounts, me);
        if (seat < 0 || i.live) continue;
        ReplayEntry e;
        e.match = i.match;
        e.started = i.started;
        e.map = i.map;
        e.mode = i.mode;
        e.seconds = u16(std::min<u32>(i.seconds, 65535));
        e.players = u8(std::min<size_t>(i.names.size(), 255));
        e.bytes = i.bytes;
        e.team = size_t(seat) < i.teams.size() ? i.teams[size_t(seat)] : u8(Team::None);
        const bool sides = e.team == u8(Team::Red) || e.team == u8(Team::Blue);
        if (!i.finished) e.result = u8(ReplayResult::Unknown);
        else if (sides) e.result = u8(i.winner == u8(Team::None) ? ReplayResult::Draw : i.winner == e.team ? ReplayResult::Won : ReplayResult::Lost);
        else if (e.team == u8(Team::None)) e.result = u8(i.winner_account == me ? ReplayResult::Won : ReplayResult::Lost);
        out.replays.push_back(std::move(e));
    }
    send_msg(p.id, out);
}

void Server::replay_get(Peer& p, u32 match) {
    ReplayChunk none;
    none.match = match;
    auto it = recordings_.find(match);
    if (it == recordings_.end() || seat_in(it->second.accounts, p.account->id) < 0) none.error = "You did not play in that match, or its recording is no longer kept.";
    else if (it->second.live) none.error = "That match is still being played: its recording is yours once it is over.";
    else if (std::string why; !send_recording(p, match, &why)) none.error = why;
    if (!none.error.empty()) send_msg(p.id, none);
}

// ── Votes ─────────────────────────────────────────────────────────────────────

void Server::call_vote(Peer& p, u32 target_id) {
    Room* r = find_room(p.room);
    if (!r) return notice(p.id, NoticeKind::Warning, "You are not in a room.");
    if (r->vote) return notice(p.id, NoticeKind::Warning, "A vote is already running in this room.");
    if (now_ - p.vote_called_at < kVoteCooldown)
        return notice(p.id, NoticeKind::Warning, eng::str::format("You can call another vote in %.0f seconds.", kVoteCooldown - (now_ - p.vote_called_at)));
    const Seat* me = r->seat_of(p.id);
    const Seat* them = r->seat_of(target_id);
    Peer* t = peer(target_id);
    if (!me || !them || !t || !t->account || target_id == p.id) return notice(p.id, NoticeKind::Warning, "Pick a soldier in your room.");
    if (me->team == Team::Observer) return notice(p.id, NoticeKind::Warning, "Observers do not vote.");
    const bool teams = mode_info(r->settings.mode).teams;
    if (teams && them->team != me->team) return notice(p.id, NoticeKind::Warning, "Only your own side can be voted out.");
    if (t->account->staff()) return notice(p.id, NoticeKind::Warning, "Staff cannot be voted out: report them instead.");
    int voters = 0;
    for (const Seat& s : r->seats)
        if (s.peer != target_id && s.team != Team::Observer && (!teams || s.team == me->team)) ++voters;
    if (voters < 2) return notice(p.id, NoticeKind::Warning, "Not enough soldiers on your side to hold a vote.");
    Room::Vote v;
    v.target = target_id;
    v.caller = p.id;
    v.side = teams ? me->team : Team::None;
    v.yes.insert(p.id);
    v.ends = now_ + kVoteSeconds;
    r->vote = v;
    p.vote_called_at = now_;
    settle_vote(*r, false);
    if (r->vote) send_vote(*r);
}

void Server::cast_vote(Peer& p, bool yes) {
    Room* r = find_room(p.room);
    if (!r || !r->vote || p.id == r->vote->target) return;
    const Seat* me = r->seat_of(p.id);
    if (!me || me->team == Team::Observer || (r->vote->side != Team::None && me->team != r->vote->side)) return;
    if (r->vote->yes.contains(p.id) || r->vote->no.contains(p.id)) return;
    (yes ? r->vote->yes : r->vote->no).insert(p.id);
    settle_vote(*r, false);
    if (r->vote) send_vote(*r);
}

void Server::settle_vote(Room& r, bool timed_out) {
    if (!r.vote) return;
    Room::Vote& v = *r.vote;
    int voters = 0;
    for (const Seat& s : r.seats)
        if (s.peer != v.target && s.team != Team::Observer && (v.side == Team::None || s.team == v.side)) ++voters;
    const int needed = voters / 2 + 1;
    Peer* t = peer(v.target);
    if (!t || !t->account || !r.seat_of(v.target)) {
        send_vote(r, "The vote ended: they left.");
        r.vote.reset();
        return;
    }
    if (int(v.yes.size()) >= needed) {
        const std::string name = t->account->code_name;
        r.barred[t->account->id] = now_ + kBarSeconds;
        send_vote(r, name + " was voted out of the room.");
        r.vote.reset();
        leave_room(*t, "Your side voted you out of the room (for 10 minutes).");
        return;
    }
    if (timed_out || int(v.no.size()) > voters - needed) {
        send_vote(r, "The vote to remove " + t->account->code_name + " failed.");
        r.vote.reset();
    }
}

void Server::send_vote(Room& r, const std::string& result) {
    if (!r.vote) return;
    const Room::Vote& v = *r.vote;
    int voters = 0;
    for (const Seat& s : r.seats)
        if (s.peer != v.target && s.team != Team::Observer && (v.side == Team::None || s.team == v.side)) ++voters;
    VoteState st;
    st.active = result.empty();
    st.target = v.target;
    if (Peer* t = peer(v.target); t && t->account) st.target_name = t->account->code_name;
    if (Peer* c = peer(v.caller); c && c->account) st.caller_name = c->account->code_name;
    st.yes = u8(v.yes.size()), st.no = u8(v.no.size());
    st.voters = u8(voters), st.needed = u8(voters / 2 + 1);
    st.seconds_left = float(std::max(0.0, v.ends - now_));
    st.result = result;
    for (const Seat& s : r.seats) {
        if (s.team == Team::Observer || (v.side != Team::None && s.team != v.side)) continue;
        st.can_vote = st.active && s.peer != v.target && !v.yes.contains(s.peer) && !v.no.contains(s.peer);
        send_msg(s.peer, st);
    }
}

void Server::tick_votes() {
    for (auto& [id, r] : rooms_) {
        if (r->vote && now_ >= r->vote->ends) settle_vote(*r, true);
        std::erase_if(r->barred, [&](const auto& kv) { return kv.second <= now_; });
    }
}

// ── Recordings (DS-8: kept 7 days, longer only while their match's report waits for TVAS) ──

u32 Server::new_match_id() { return next_match_++; }

std::filesystem::path Server::recording_path(u32 match) const {
    if (options_.recordings.empty()) return {};
    return options_.recordings / (std::to_string(match) + replay::kExtension);
}

void Server::recording_update(const RecordingInfo& info) {
    recordings_[info.match] = info;
    if (options_.recordings.empty()) return;
    eng::ConfigFile cfg;
    eng::ConfigSection s;
    s.name = "recording";
    s.set("match", std::to_string(info.match));
    s.set("started", std::to_string(info.started));
    s.set("map", info.map);
    s.set("mode", std::to_string(info.mode));
    s.set("seconds", std::to_string(info.seconds));
    s.set("bytes", std::to_string(info.bytes));
    std::string acc, names;
    for (u64 a : info.accounts) acc += (acc.empty() ? "" : ",") + std::to_string(a);
    for (const auto& n : info.names) names += (names.empty() ? "" : ",") + n;
    s.set("accounts", acc);
    s.set("names", names);
    std::string teams;
    for (u8 t : info.teams) teams += (teams.empty() ? "" : ",") + std::to_string(unsigned(t));
    s.set("teams", teams);
    if (info.finished) {
        s.set("finished", "true");
        s.set("winner", std::to_string(unsigned(info.winner)));
        if (info.winner_account) s.set("winner_account", std::to_string(info.winner_account));
    }
    if (info.report_waiting) s.set("report_waiting", "true");
    cfg.sections.push_back(std::move(s));
    eng::fs::write_text_file(options_.recordings / (std::to_string(info.match) + ".info"), cfg.serialize());
}

void Server::recording_reported(u32 match) {
    auto it = recordings_.find(match);
    if (it == recordings_.end() || !it->second.report_waiting) return;
    it->second.report_waiting = false;
    recording_update(it->second);
}

void Server::load_recordings() {
    recordings_.clear();
    if (options_.recordings.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(options_.recordings, ec);
    for (const auto& e : std::filesystem::directory_iterator(options_.recordings, ec)) {
        if (e.path().extension() != ".info") continue;
        auto text = eng::fs::read_text_file(e.path());
        eng::ConfigFile cfg;
        if (!text || !eng::ConfigFile::parse(*text, cfg, nullptr)) continue;
        const eng::ConfigSection* s = cfg.find("recording");
        if (!s) continue;
        RecordingInfo i;
        i.match = u32(s->get_int("match"));
        if (!i.match || !std::filesystem::exists(recording_path(i.match), ec)) continue;
        i.started = u64(std::atoll(s->get_string("started").c_str()));
        i.map = s->get_string("map");
        i.mode = u8(s->get_int("mode"));
        i.seconds = u32(s->get_int("seconds"));
        i.bytes = u32(s->get_int("bytes"));
        for (auto part : eng::str::split(s->get("accounts"), ',')) i.accounts.push_back(u64(std::strtoull(std::string(eng::str::trim(part)).c_str(), nullptr, 10)));
        for (auto part : eng::str::split(s->get("names"), ',')) i.names.emplace_back(eng::str::trim(part));
        for (auto part : eng::str::split(s->get("teams"), ',')) i.teams.push_back(u8(std::clamp(std::atoi(std::string(eng::str::trim(part)).c_str()), 0, 3)));
        i.finished = s->get_bool("finished");
        i.winner = u8(std::clamp(s->get_int("winner", int(Team::None)), 0, 3));
        i.winner_account = u64(std::strtoull(s->get_string("winner_account").c_str(), nullptr, 10));
        i.report_waiting = s->get_bool("report_waiting");
        next_match_ = std::max(next_match_, i.match + 1);
        recordings_[i.match] = std::move(i);
    }
    LOG_INFO("Recordings: %zu kept in %s", recordings_.size(), eng::str::narrow(options_.recordings.wstring()).c_str());
}

// DS-8: a recording goes 7 days after its match ended, a reported one too; the only longer stay is
// one whose report has not yet reached TVAS (PR-7).
void Server::prune_recordings() {
    if (options_.recordings.empty()) return;
    const u64 cutoff = unix_now() - u64(options_.recording_days) * 86400;
    for (auto it = recordings_.begin(); it != recordings_.end();) {
        const RecordingInfo& i = it->second;
        if (i.live || i.report_waiting || i.started + i.seconds >= cutoff) {
            ++it;
            continue;
        }
        std::error_code ec;
        std::filesystem::remove(recording_path(it->first), ec);
        std::filesystem::remove(options_.recordings / (std::to_string(it->first) + ".info"), ec);
        it = recordings_.erase(it);
    }
}

void Server::pump_replays() {
    for (auto& [id, p] : peers_) {
        if (!p.replay_out) continue;
        Peer::ReplaySend& r = *p.replay_out;
        for (int k = 0; k < 4 && r.sent < r.bytes.size() && net_.pending_reliable(id) < 8; ++k) {
            ReplayChunk c;
            c.match = r.match;
            c.total = u32(r.bytes.size());
            c.offset = u32(r.sent);
            const size_t n = std::min(kReplayChunk, r.bytes.size() - r.sent);
            c.bytes.assign(r.bytes.begin() + std::ptrdiff_t(r.sent), r.bytes.begin() + std::ptrdiff_t(r.sent + n));
            r.sent += n;
            send_msg(id, c);
        }
        if (r.sent >= r.bytes.size()) p.replay_out.reset();
    }
}

}  // namespace lsfs
