#include "Game/TvasProto.hpp"

#include "Engine/Core/Strings.hpp"
#include "Game/Items.hpp"
#include "Game/TvasJson.hpp"

#include <algorithm>

namespace lsf::tvproto {

using namespace proto;

namespace {
u32 u32_of(const Json& v, u32 fallback = 0) { return u32(std::min<u64>(v.as_uint(fallback), 0xFFFFFFFFull)); }
u16 u16_of(const Json& v, u16 fallback = 0) { return u16(std::min<u64>(v.as_uint(fallback), 0xFFFFull)); }
u8 u8_of(const Json& v, u8 fallback = 0) { return u8(std::min<u64>(v.as_uint(fallback), 0xFFull)); }
std::string line(const Json& v, size_t max) { return eng::str::sanitize_line(v.str(), max); }
}  // namespace

u16 weapon_of(const Json& code) {
    const std::string c = code.str();
    return c.empty() ? kNoWeapon : weapon_from_saved(c);
}

FriendList friend_list(const Json& reply) {
    FriendList out;
    for (const Json& e : reply["entries"].elements()) {
        FriendEntry f;
        f.name = line(e["name"], kNameMax);
        f.state = u8_of(e["state"]);
        f.online = e["online"].as_bool();
        f.xp = u32_of(e["xp"]);
        f.clan = line(e["clan"], 24);
        f.where = line(e["where"], 96);
        f.channel = u8_of(e["channel"]);
        f.room = u16_of(e["room"]);
        f.joinable = e["joinable"].as_bool();
        f.last_seen = e["last_seen"].as_uint();
        f.server = e["server"].as_uint();
        f.server_name = line(e["server_name"], 48);
        out.entries.push_back(std::move(f));
    }
    return out;
}

Mailbox mailbox(const Json& reply) {
    Mailbox out;
    for (const Json& m : reply["items"].elements()) {
        MailItem i;
        i.id = u32_of(m["id"]);
        i.from = line(m["from"], 24);
        i.time = m["time"].as_uint();
        i.text = m["text"].str().substr(0, kMailTextMax);
        i.read = m["read"].as_bool();
        i.saved = m["saved"].as_bool();
        i.system = m["system"].as_bool();
        i.expires = m["expires"].as_uint();
        out.items.push_back(std::move(i));
    }
    out.now = reply["now"].as_uint();
    out.saved_max = u16_of(reply["saved_max"], 50);
    out.unsaved_max = u16_of(reply["unsaved_max"], 30);
    return out;
}

ClanInvited clan_invited(const Json& inv) {
    ClanInvited i;
    i.clan = line(inv["clan"], kClanNameMax);
    i.from = line(inv["from"], kNameMax);
    i.mark = tvjson::mark(inv["mark"]);
    i.members = u16_of(inv["members"]);
    return i;
}

ClanState clan_state(const Json& s, std::optional<ClanInvited>* invite) {
    ClanState c;
    c.member = s["member"].as_bool();
    c.applied = line(s["applied"], kClanNameMax);
    if (!c.member) {
        if (invite && s["invite"].is_object()) *invite = clan_invited(s["invite"]);
        return c;
    }
    c.name = line(s["name"], kClanNameMax);
    c.master = line(s["master"], kNameMax);
    c.notice = line(s["notice"], kClanNoticeMax);
    c.founded = line(s["founded"], 16);
    c.mark = tvjson::mark(s["mark"]);
    for (const Json& m : s["members"].elements()) c.members.push_back({line(m["name"], kNameMax), u32_of(m["xp"]), m["online"].as_bool(), u8_of(m["rank"]), m["joined"].as_uint()});
    c.my_rank = u8_of(s["my_rank"]);
    for (const Json& a : s["applicants"].elements()) c.applicants.push_back({line(a["name"], kNameMax), u32_of(a["xp"]), a["time"].as_uint(), a["online"].as_bool()});
    for (const Json& l : s["log"].elements()) c.log.push_back({l["time"].as_uint(), line(l["text"], 96)});
    c.open = s["open"].as_bool();
    c.wins = u32_of(s["wins"]), c.losses = u32_of(s["losses"]), c.draws = u32_of(s["draws"]), c.points = u32_of(s["points"]);
    c.rank = u16_of(s["rank"]);
    return c;
}

ClanDirectory clan_directory(const Json& reply) {
    ClanDirectory dir;
    for (const Json& c : reply["clans"].elements()) {
        ClanListing l;
        l.name = line(c["name"], kClanNameMax);
        l.owner = line(c["owner"], kNameMax);
        l.founded = line(c["founded"], 16);
        l.notice = line(c["notice"], kClanNoticeMax);
        l.members = u16_of(c["members"]), l.online = u16_of(c["online"]);
        l.open = c["open"].as_bool();
        l.mark = tvjson::mark(c["mark"]);
        l.wins = u32_of(c["wins"]), l.losses = u32_of(c["losses"]), l.draws = u32_of(c["draws"]), l.points = u32_of(c["points"]);
        l.rank = u16_of(c["rank"]);
        dir.clans.push_back(std::move(l));
    }
    return dir;
}

IdCard id_card(const Json& v, const std::string& asked_for) {
    IdCard c;
    c.found = v["found"].as_bool();
    c.code_name = eng::str::sanitize_line(v["code_name"].str(asked_for), kNameMax);
    if (!c.found) return c;
    c.clan = line(v["clan"], 24);
    c.clan_mark = tvjson::mark(v["clan_mark"]);
    c.message = line(v["message"], size_t(kCardMessageMax));
    c.xp = u32_of(v["xp"]);
    c.name_colour = u8_of(v["name_colour"]), c.clan_colour = u8_of(v["clan_colour"]);
    c.online = v["online"].as_bool();
    c.room = u16_of(v["room"]);
    c.where = line(v["where"], 96);
    const Json& s = v["stats"];
    c.kills = u32_of(s["kills"]), c.deaths = u32_of(s["deaths"]), c.wins = u32_of(s["wins"]), c.losses = u32_of(s["losses"]);
    c.headshots = u32_of(s["headshots"]), c.matches = u32_of(s["matches"]), c.forfeits = u32_of(s["forfeits"]), c.team_kills = u32_of(s["team_kills"]);
    c.missions = u32_of(s["missions"]), c.attended = u32_of(s["attended"]), c.rounds = u32_of(s["rounds"]), c.survived = u32_of(s["survived"]);
    c.shots = s["shots"].as_uint(), c.hits = s["hits"].as_uint();
    c.force = u8_of(v["force"]);
    for (size_t i = 0; i < kLoadoutSlots; ++i) c.loadout[i] = weapon_of(v["loadout"][i]);   // a TVAS of four leaves the last two empty
    for (const Json& p : v["parts"].elements()) c.parts.push_back(u16_of(p));
    return c;
}

RewardsState rewards_state(const Json& v) {
    RewardsState s;
    s.now = v["now"].as_uint();
    s.day_ends = v["day_ends"].as_uint();
    s.minutes = u16_of(v["minutes"]);
    for (const Json& p : v["play"].elements()) s.play.push_back({u16_of(p["minutes"], 60), u32_of(p["sp"]), u8_of(p["box"], kNoBox)});
    s.play_claimed = u8_of(v["play_claimed"]);
    for (const Json& d : v["week"].elements()) s.week.push_back({u32_of(d["sp"]), u8_of(d["box"], kNoBox)});
    s.week_at = u8_of(v["week_at"]);
    s.signed_today = v["signed_today"].as_bool();
    for (const Json& q : v["quests"].elements()) {
        QuestNow n;
        n.def = tvjson::quest(q["def"]);
        n.progress = u32_of(q["progress"]);
        n.claimed = q["claimed"].as_bool();
        n.event = u32_of(q["event"]);
        s.quests.push_back(std::move(n));
    }
    s.all_quests_sp = u32_of(v["all_quests_sp"]);
    s.all_quests_box = u8_of(v["all_quests_box"], kNoBox);
    s.all_claimed = v["all_claimed"].as_bool();
    for (size_t i = 0; i < s.boxes.size(); ++i) s.boxes[i] = u16_of(v["boxes"][i]);
    for (const Json& e : v["events"].elements()) {
        EventNow n;
        n.id = u32_of(e["id"]);
        n.name = line(e["name"], 40);
        n.banner = line(e["banner"], 160);
        n.theme = u8_of(e["theme"]);
        n.from = e["from"].as_uint(), n.to = e["to"].as_uint();
        n.running = e["running"].as_bool();
        n.sp_pct = u16_of(e["sp_pct"], 100), n.xp_pct = u16_of(e["xp_pct"], 100);
        n.drop_box = u8_of(e["drop_box"], kNoBox);
        n.drop_permille = u16_of(e["drop_permille"]);
        n.sign_in_sp = u32_of(e["sign_in_sp"]);
        s.events.push_back(std::move(n));
    }
    for (const Json& t : v["tables"].elements()) s.tables.push_back(tvjson::loot_table(t));
    return s;
}

BoxOpened box_opened(const Json& v, u8 box) {
    BoxOpened o;
    o.box = box;
    o.ok = v["ok"].as_bool();
    o.kind = u8_of(v["kind"]);
    o.amount = u32_of(v["amount"]);
    const Json& it = v["item"];
    o.item = it.is_string() ? weapon_of(it) : u16_of(it);
    o.text = line(v["text"], 160);
    return o;
}

CapsuleResult capsule_result(const Json& v, u16 capsule) {
    CapsuleResult c;
    c.capsule = capsule;
    c.ok = v["ok"].as_bool();
    c.text = line(v["text"], 160);
    const Json& p = v["prize"];
    if (p.is_object()) {
        c.prize.kind = u8_of(p["kind"]);
        c.prize.id = p["item"].is_string() ? weapon_of(p["item"]) : u16_of(p["item"]);
        c.prize.days = u16_of(p["days"]);
        c.prize.lo = u32_of(p["lo"]), c.prize.hi = u32_of(p["hi"]);
        c.prize.weight = u16_of(p["weight"], 1);
    }
    return c;
}

ServiceResult service_result(const Json& v, RequestOp op, u16 weapon_id) {
    ServiceResult g;
    g.op = u8(op);
    g.ok = v["ok"].as_bool();
    g.weapon = weapon_id;
    if (weapon_id == kNoWeapon && v["weapon"].is_string()) g.weapon = weapon_of(v["weapon"]);
    g.text = line(v["text"], 160);
    return g;
}

StaffAccountCard staff_card(const Json& c) {
    StaffAccountCard o;
    o.account = "#" + std::to_string(c["id"].as_uint()) + " " + c["username"].str();
    o.code_name = c["code_name"].str();
    o.clan = c["clan"].str();
    o.role = u8_of(c["global_role"]);
    o.xp = u32_of(c["xp"]), o.sp = u32_of(c["sp"]);
    const Json& s = c["stats"];
    o.kills = u32_of(s["kills"]), o.deaths = u32_of(s["deaths"]), o.wins = u32_of(s["wins"]), o.losses = u32_of(s["losses"]), o.matches = u32_of(s["matches"]);
    o.created = c["created"].as_uint();
    o.muted_until = c["muted_until"].as_uint();
    const eng::i64 banned = c["banned_until"].as_int();
    o.banned_until = banned < 0 ? StaffAccountCard::kForever : u64(banned);
    o.ban_reason = c["ban_reason"].str();
    o.online = c["online"].as_bool();
    o.weapons = u16(std::min<size_t>(c["weapons"].size(), 0xFFFF)), o.forces = u16(std::min<size_t>(c["forces"].size(), 0xFFFF)), o.items = u16_of(c["items"]);
    return o;
}

const char* friend_op(FriendOp op) {
    static const char* const ops[] = {"add", "accept", "decline", "cancel", "remove", "block", "unblock"};
    return ops[std::min<size_t>(size_t(op), 6)];
}

const char* mail_op(MailOp op) {
    static const char* const ops[] = {"read", "delete", "read_all", "delete_read", "save", "unsave", "report"};
    return ops[std::min<size_t>(size_t(op), 6)];
}

const char* clan_op(ClanOp op) {
    static const char* const ops[] = {"promote", "demote", "transfer", "notice", "apply", "accept", "decline", "cancel_apply", "disband", "set_open"};
    return ops[std::min<size_t>(size_t(op), 9)];
}

const char* claim_kind(ClaimKind kind) {
    static const char* const kinds[] = {"sign_in", "play", "quest", "all"};
    return kinds[std::min<size_t>(size_t(kind), 3)];
}

const char* staff_op(StaffOp op) {
    // A global mute is a mute everywhere: TVAS knows one kind.
    static const char* const ops[] = {"kick", "mute", "unmute", "ban", "unban", "set_role", "mute", "unmute"};
    return ops[std::min<size_t>(size_t(op), 7)];
}

const char* account_field(AccountField field) {
    static const char* const fields[] = {"code_name", "sp", "xp", "rank", "grant_weapon", "revoke_weapon", "grant_force", "revoke_force",
                                         "grant_item", "revoke_item", "clear_loadout", "grant_box", "coins", "durability"};
    return size_t(field) < std::size(fields) ? fields[size_t(field)] : nullptr;
}

std::string account_field_key(AccountField field, const std::string& text) {
    if (field == AccountField::GrantForce || field == AccountField::RevokeForce) {
        for (const ForceDef& f : forces())
            if (eng::str::iequals(f.name, text) || eng::str::iequals(f.model, text)) return std::to_string(unsigned(f.id));
    } else if (field == AccountField::GrantItem || field == AccountField::RevokeItem) {
        for (const ItemDef& d : items())
            if (eng::str::iequals(d.code, text) || eng::str::iequals(d.name, text)) return std::to_string(unsigned(d.id));
    } else if (field == AccountField::GrantWeapon || field == AccountField::RevokeWeapon || field == AccountField::Durability) {
        if (const WeaponDef* w = weapon(weapon_from_saved(text))) return w->code;
    }
    return text;
}

}  // namespace lsf::tvproto
