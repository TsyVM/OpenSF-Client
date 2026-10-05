#include "Accounts.hpp"

#include "Engine/Core/Config.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Registry.hpp"
#include "Game/TvasJson.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>

namespace lsfs {

namespace {

u64 to_u64(std::string_view s) {
    unsigned long long v = 0;
    s = eng::str::trim(s);
    std::from_chars(s.data(), s.data() + s.size(), v);
    return u64(v);
}

u16 clamp16(u64 v) { return u16(std::min<u64>(v, 0xFFFF)); }

}  // namespace

bool Account::blocks_with(u64 other) const { return std::find(blocks.begin(), blocks.end(), other) != blocks.end(); }

proto::Role Account::effective_role() const {
    // SFLegacy Staff outrank everyone on every server (D4): a Moderator acts as a server Moderator,
    // a Game Master or Admin as this server's Admin at least.
    proto::Role r = server_role;
    if (global_role >= proto::GlobalRole::GameMaster) r = std::max(r, proto::Role::Admin);
    else if (global_role >= proto::GlobalRole::Moderator) r = std::max(r, proto::Role::Moderator);
    return r;
}

bool Account::owns_weapon(u16 id) const {
    const WeaponDef* w = weapon(id);
    if (!w) return false;
    if (w->admin_only) return carries_staff_variants();
    if (w->price == 0 && w->klass == WeaponClass::Knife) return true;
    return std::find(weapons.begin(), weapons.end(), id) != weapons.end();
}

bool Account::owns_force(u8 id) const {
    const ForceDef* f = lsf::force(id);
    if (f && f->price == 0) return true;
    return std::find(forces.begin(), forces.end(), id) != forces.end();
}

proto::Profile Account::profile() const {
    proto::Profile p;
    p.account_id = id;
    p.code_name = code_name;
    p.xp = xp, p.sp = sp;
    p.kills = kills, p.deaths = deaths, p.wins = wins, p.losses = losses, p.headshots = headshots, p.matches = matches;
    p.force = force;
    p.loadout = loadout;
    p.sp_charge = sp_charge;
    p.clan = clan;
    p.clan_mark = clan_mark;
    p.role = u8(effective_role());
    p.server_role = u8(server_role);
    p.global_role = u8(global_role);
    p.rev = rev;
    p.name_colour = name_colour, p.clan_colour = clan_colour, p.fake_rank = fake_rank;
    p.forfeits = forfeits, p.team_kills = team_kills, p.missions = missions, p.attended = attended;
    p.rounds = rounds, p.survived = survived, p.shots = shots, p.hits = hits;
    return p;
}

const Account::Item* Account::find_item(u16 id) const {
    for (const Item& i : items)
        if (i.id == id) return &i;
    return nullptr;
}

bool Account::has_boost(Boost b, u64 now) const {
    for (const Item& i : items) {
        const ItemDef* d = item(i.id);
        if (!d || d->kind != ItemKind::Boost || d->one_use()) continue;
        if (i.expires && i.expires <= now) continue;
        if (boost_includes(d->boost, b)) return true;
    }
    return false;
}

u8 Account::durability_of(u16 weapon_id) const {
    const WeaponDef* w = weapon(weapon_id);
    auto it = guns.find(weapon_id);
    if (!w || !lsf::wears(*w) || it == guns.end() || !owns_weapon(weapon_id)) return kDurabilityFull;
    return std::min(it->second.durability, kDurabilityFull);
}

u64 Account::weapon_until(u16 id) const {
    if (!owns_weapon(id)) return 0;
    auto it = guns.find(id);
    return it == guns.end() || it->second.expires == 0 ? ~u64(0) : it->second.expires;
}

bool Account::owns_spray(u16 id, u64 now) const {
    for (const Item& i : sprays)
        if (i.id == id) return i.expires == 0 || i.expires > now;
    return false;
}

std::vector<u16> Account::worn(u8 f, u64 now) const {
    std::vector<u16> out;
    for (u16 id : equipped) {
        const ItemDef* d = item(id);
        if (!d && id >= kFirstPackItem)
            if (const PackItem* pi = registry::item(id)) d = &pi->def;
        const Item* own = find_item(id);
        if (d && own && d->kind == ItemKind::Part && d->force == f && (!own->expires || own->expires > now)) out.push_back(id);
    }
    return out;
}

u32 Account::shown_xp(u64 now) const {
    if (fake_rank != 0xFF && has_boost(Boost::FakeRank, now)) return rank_xp(fake_rank);
    return xp;
}

void Account::apply_overrides() {
    loadout = universal_loadout;
    const u64 now = u64(std::time(nullptr));
    for (size_t s = 0; s < kLoadoutSlots; ++s) {
        if (override_slots[s].empty()) continue;
        const WeaponDef* w = registry::weapon_by_code(override_slots[s]);
        // A slot whose custom item is gone (sold back, run out, its pack dropped) falls back (§6.2).
        bool owned = false;
        for (const Custom& c : custom)
            if (c.code == override_slots[s] && (c.expires == 0 || c.expires > now)) owned = true;
        if (w && owned && fits_cell(w->slot, s)) loadout[s] = w->id;
    }
    if (!override_force.empty())
        if (const PackForce* pf = registry::force_by_code(override_force)) force = pf->def.id;
}

// ── The store ──────────────────────────────────────────────────────────────────

bool Accounts::open(const std::filesystem::path& dir, const std::filesystem::path& staff_file) {
    dir_ = dir;
    staff_file_ = staff_file;
    std::error_code ec;
    if (!dir_.empty()) std::filesystem::create_directories(dir_, ec);
    roles_.clear();
    if (auto text = eng::fs::read_text_file(staff_file_)) {
        eng::ConfigFile cfg;
        if (eng::ConfigFile::parse(*text, cfg)) {
            for (const eng::ConfigSection* s : cfg.all("staff")) {
                const u64 id = to_u64(s->get("account"));
                const std::string r = eng::str::lower(s->get_string("role"));
                const proto::Role role = r == "admin" ? proto::Role::Admin : r == "game_master" ? proto::Role::GameMaster : r == "moderator" ? proto::Role::Moderator : proto::Role::Player;
                if (id && role != proto::Role::Player) roles_[id] = {role, s->get_string("code_name")};
            }
        }
    }
    reports_.clear();
    next_report_ = 1;
    if (auto text = eng::fs::read_text_file(dir_ / "reports.cfg")) {
        eng::ConfigFile cfg;
        if (eng::ConfigFile::parse(*text, cfg)) {
            for (const eng::ConfigSection* s : cfg.all("report")) {
                Report r;
                r.id = u32(s->get_int("id"));
                if (!r.id) continue;
                r.reporter = to_u64(s->get("reporter"));
                r.target = to_u64(s->get("target"));
                r.reporter_name = s->get_string("reporter_name");
                r.target_name = s->get_string("target_name");
                r.reason = proto::ReportReason(std::clamp(s->get_int("reason"), 0, int(proto::ReportReason::Count) - 1));
                r.state = proto::ReportState(std::clamp(s->get_int("state"), 0, 2));
                r.time = to_u64(s->get("time"));
                r.note = s->get_string("note");
                r.match = u32(s->get_int("match"));
                next_report_ = std::max(next_report_, r.id + 1);
                reports_.push_back(std::move(r));
            }
        }
    }
    // The soldiers this server keeps something of, so staff can reach one who is away (a ban lifted).
    known_.clear();
    if (!dir_.empty())
        for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
            const std::string stem = eng::str::narrow(entry.path().stem().wstring());
            if (entry.path().extension() != ".cfg" || stem.empty() || stem.find_first_not_of("0123456789") != std::string::npos) continue;
            auto text = eng::fs::read_text_file(entry.path());
            eng::ConfigFile cfg;
            if (text && eng::ConfigFile::parse(*text, cfg)) known_[to_u64(stem)] = cfg.root().get_string("code_name");
        }
    LOG_INFO("Accounts: %zu server staff, %zu reports, %zu soldiers with a record here (%s)", roles_.size(), reports_.size(), known_.size(),
             eng::str::narrow(dir_.wstring()).c_str());
    return true;
}

void Accounts::mark_dirty(u64 id) {
    if (std::find(dirty_.begin(), dirty_.end(), id) == dirty_.end()) dirty_.push_back(id);
}

bool Accounts::save() {
    bool ok = true;
    for (u64 id : dirty_)
        if (const Account* a = find(id)) ok &= save_local(*a);
    dirty_.clear();
    if (staff_dirty_) ok &= save_staff();
    if (reports_dirty_ && !dir_.empty()) {
        eng::ConfigFile cfg;
        for (const Report& r : reports_) {
            eng::ConfigSection s;
            s.name = "report";
            s.set("id", std::to_string(r.id));
            if (r.reporter) s.set("reporter", std::to_string(r.reporter)), s.set("reporter_name", r.reporter_name);
            s.set("target", std::to_string(r.target));
            s.set("target_name", r.target_name);
            s.set("reason", std::to_string(int(r.reason)));
            s.set("state", std::to_string(int(r.state)));
            s.set("time", std::to_string(r.time));
            if (!r.note.empty()) s.set("note", r.note);
            if (r.match) s.set("match", std::to_string(r.match));
            cfg.sections.push_back(std::move(s));
        }
        ok &= eng::fs::write_text_file(dir_ / "reports.cfg", "# This server's reports (written by the server)\n" + cfg.serialize());
        reports_dirty_ = false;
    }
    return ok;
}

Account* Accounts::find(u64 id) {
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : it->second.get();
}

Account* Accounts::find_code_name(std::string_view code_name) {
    for (auto& [id, a] : accounts_)
        if (eng::str::iequals(a->code_name, eng::str::trim(code_name))) return a.get();
    return nullptr;
}

Account* Accounts::find_token(std::string_view token) {
    token = eng::str::trim(token);
    if (Account* here = token.starts_with("#") ? find(to_u64(token.substr(1))) : find_code_name(token)) return here;
    // Away, with a record here: this server's own data for them, read from their file.
    u64 id = 0;
    if (token.starts_with("#")) {
        const u64 want = to_u64(token.substr(1));
        if (known_.contains(want)) id = want;
    } else {
        for (const auto& [kid, name] : known_)
            if (eng::str::iequals(name, token)) id = kid;
    }
    if (!id) return nullptr;
    auto& slot = accounts_[id];
    slot = std::make_unique<Account>();
    slot->id = id;
    slot->code_name = known_[id];
    load_local(*slot);
    slot->server_role = id == owner_ ? proto::Role::Owner : role_of(id);
    return slot.get();
}

Account& Accounts::apply_profile(const eng::json::Value& me) {
    const u64 id = me["id"].as_uint();
    auto& slot = accounts_[id];
    const bool fresh = !slot;
    if (fresh) slot = std::make_unique<Account>(), slot->id = id;
    Account& a = *slot;
    a.code_name = me["code_name"].str();
    a.rev = u32(me["rev"].as_uint());
    a.global_role = proto::GlobalRole(std::min<u64>(me["global_role"].as_uint(), 3));
    a.xp = u32(me["xp"].as_uint());
    a.sp = u32(std::min<u64>(me["sp"].as_uint(), 0xFFFFFFFFull));
    a.coins = u32(me["coins"].as_uint());
    a.sp_charge = u8(std::min<u64>(me["sp_charge"].as_uint(), 100));
    a.muted_until = me["muted_until"].as_uint();
    const auto& st = me["stats"];
    a.kills = u32(st["kills"].as_uint()), a.deaths = u32(st["deaths"].as_uint()), a.wins = u32(st["wins"].as_uint()), a.losses = u32(st["losses"].as_uint());
    a.headshots = u32(st["headshots"].as_uint()), a.matches = u32(st["matches"].as_uint()), a.forfeits = u32(st["forfeits"].as_uint());
    a.team_kills = u32(st["team_kills"].as_uint()), a.missions = u32(st["missions"].as_uint()), a.attended = u32(st["attended"].as_uint());
    a.rounds = u32(st["rounds"].as_uint()), a.survived = u32(st["survived"].as_uint()), a.shots = st["shots"].as_uint(), a.hits = st["hits"].as_uint();
    const auto& clan = me["clan"];
    a.clan = clan.is_object() ? clan["name"].str() : std::string();
    a.clan_id = clan.is_object() ? clan["id"].as_uint() : 0;
    a.clan_mark = clan.is_object() ? tvjson::mark(clan["mark"]) : ClanMark{};
    a.clan_rank = ClanRank(std::min<u64>(clan.is_object() ? clan["rank"].as_uint() : 0, u64(ClanRank::Owner)));
    a.blocks.clear();
    for (const auto& b : me["blocks"].elements()) a.blocks.push_back(b.as_uint());
    // The universal record (TVAS/app/lib/record.php): weapons by code, the rest by number.
    const auto& r = me["record"];
    a.weapons.clear();
    a.guns.clear();
    for (const auto& [code, g] : r["weapons"].items()) {
        const WeaponDef* w = weapon_by_code(code);
        if (!w) continue;
        a.weapons.push_back(w->id);
        Account::Gun gun;
        gun.expires = g["exp"].as_uint();
        gun.durability = u8(std::min<u64>(g["dur"].as_uint(kDurabilityFull), kDurabilityFull));
        if (!gun.plain()) a.guns[w->id] = gun;
    }
    a.forces.clear();
    a.force_until.clear();
    for (const auto& [fid, exp] : r["forces"].items()) {
        int f = 0;
        if (!eng::str::parse_int(fid, f) || f < 0 || f >= int(kFirstPackForce)) continue;
        a.forces.push_back(u8(f));
        if (exp.as_uint()) a.force_until[u8(f)] = exp.as_uint();
    }
    a.force = u8(std::min<u64>(r["force"].as_uint(), 255));
    // Six cells; a TVAS that keeps four leaves the last two throwables' empty.
    for (size_t s = 0; s < kLoadoutSlots; ++s) {
        const std::string code = r["loadout"][s].str();
        a.universal_loadout[s] = code.empty() ? kNoWeapon : weapon_from_saved(code);
    }
    sanitize_loadout(a.universal_loadout);
    a.items.clear();
    for (const auto& i : r["items"].elements()) a.items.push_back({clamp16(i["id"].as_uint()), i["exp"].as_uint(), clamp16(i["uses"].as_uint())});
    a.equipped.clear();
    for (const auto& e : r["equipped"].elements()) a.equipped.push_back(clamp16(e.as_uint()));
    a.sprays.clear();
    for (const auto& s : r["sprays"].elements()) a.sprays.push_back({clamp16(s["id"].as_uint()), s["exp"].as_uint(), 0});
    a.spray = clamp16(r["spray"].as_uint());
    for (size_t k = 0; k < a.horror_items.size(); ++k) a.horror_items[k] = clamp16(r["horror"][k].as_uint());
    for (size_t k = 0; k < a.boxes.size(); ++k) a.boxes[k] = clamp16(r["boxes"][k].as_uint());
    a.bags.clear();
    for (const auto& [bid, n] : r["bags"].items()) {
        int b = 0;
        if (eng::str::parse_int(bid, b) && b > 0 && n.as_uint()) a.bags[u16(b)] = clamp16(n.as_uint());
    }
    a.gifts.clear();
    for (const auto& g : r["gifts"].elements()) {
        Account::Gift gift;
        gift.id = u32(g["id"].as_uint());
        gift.kind = u8(g["kind"].as_uint());
        if (gift.kind >= u8(ShopKind::Count) || !tvjson::ware_from_key(ShopKind(gift.kind), g["item"].is_string() ? g["item"].str() : std::to_string(g["item"].as_uint()), gift.item))
            continue;
        gift.days = clamp16(g["days"].as_uint());
        gift.from = g["from"].str();
        a.gifts.push_back(gift);
    }
    a.name_colour = u8(std::min<u64>(r["name_colour"].as_uint(), 255));
    a.clan_colour = u8(std::min<u64>(r["clan_colour"].as_uint(), 255));
    a.fake_rank = u8(std::min<u64>(r["fake_rank"].as_uint(0xFF), 255));
    a.profile_text = r["card"].str();
    if (fresh) load_local(a);
    // This server's own things, with this session's numbers (NM-7).
    const u64 now = u64(std::time(nullptr));
    for (const Account::Custom& c : a.custom) {
        if (c.expires && c.expires <= now) continue;
        if (const WeaponDef* w = registry::weapon_by_code(c.code)) a.weapons.push_back(w->id);
        else if (const PackForce* f = registry::force_by_code(c.code)) a.forces.push_back(f->def.id);
        else if (const PackItem* i = registry::item_by_code(c.code)) a.items.push_back({i->def.id, c.expires, 0});
    }
    for (const std::string& code : a.custom_parts)
        if (const PackItem* i = registry::item_by_code(code)) a.equipped.push_back(i->def.id);
    a.server_role = id == owner_ ? proto::Role::Owner : role_of(id);
    a.apply_overrides();
    return a;
}

void Accounts::unload(u64 id) {
    if (Account* a = find(id)) {
        if (std::find(dirty_.begin(), dirty_.end(), id) != dirty_.end()) save_local(*a);
        std::erase(dirty_, id);
    }
    accounts_.erase(id);
}

void Accounts::grant_custom(u64 id, const Account::Custom& c) {
    if (Account* a = find(id)) {
        a->custom.push_back(c);
        mark_dirty(id);
        return;
    }
    Account held;
    held.id = id;
    load_local(held);
    held.custom.push_back(c);
    save_local(held);
}

// accounts/<id>.cfg: [account] with this server's own data only.
void Accounts::load_local(Account& a) {
    if (dir_.empty()) return;
    auto text = eng::fs::read_text_file(dir_ / (std::to_string(a.id) + ".cfg"));
    if (!text) return;
    eng::ConfigFile cfg;
    if (!eng::ConfigFile::parse(*text, cfg)) {
        LOG_ERROR("Accounts: %llu.cfg could not be read; it is left as it is", (unsigned long long)a.id);
        return;
    }
    const eng::ConfigSection& s = cfg.root();
    a.server_muted_until = to_u64(s.get("muted_until"));
    a.global_muted_until = to_u64(s.get("global_muted_until"));
    a.banned_until = s.get_string("banned_until") == "forever" ? Account::kForever : to_u64(s.get("banned_until"));
    a.ban_reason = s.get_string("ban_reason");
    for (std::string_view part : eng::str::split(s.get("custom"), ',')) {
        const auto f = eng::str::split(eng::str::trim(part), '|');
        if (f.size() != 3 || !is_pack_code(f[0])) continue;
        a.custom.push_back({std::string(f[0]), to_u64(f[1]), u8(std::min<u64>(to_u64(f[2]), 3))});
    }
    const auto slots = eng::str::split(s.get("overrides"), ',', false);
    for (size_t i = 0; i < kLoadoutSlots && i < slots.size(); ++i) a.override_slots[i] = std::string(eng::str::trim(slots[i]));
    a.override_force = s.get_string("force");
    for (std::string_view part : eng::str::split(s.get("parts"), ',')) a.custom_parts.emplace_back(eng::str::trim(part));
}

bool Accounts::save_local(const Account& a) {
    if (dir_.empty()) return true;
    known_[a.id] = a.code_name;
    eng::ConfigSection s;
    s.set("code_name", a.code_name);   // for whoever reads the file; the id is the key
    if (a.server_muted_until) s.set("muted_until", std::to_string(a.server_muted_until));
    if (a.global_muted_until) s.set("global_muted_until", std::to_string(a.global_muted_until));
    if (a.banned_until) s.set("banned_until", a.banned_until == Account::kForever ? std::string("forever") : std::to_string(a.banned_until));
    if (!a.ban_reason.empty()) s.set("ban_reason", a.ban_reason);
    std::string list;
    for (const Account::Custom& c : a.custom) list += (list.empty() ? "" : ",") + c.code + "|" + std::to_string(c.expires) + "|" + std::to_string(c.kind);
    if (!list.empty()) s.set("custom", list);
    std::string overrides;
    for (size_t i = 0; i < kLoadoutSlots; ++i) overrides += (i ? "," : "") + a.override_slots[i];
    if (std::any_of(a.override_slots.begin(), a.override_slots.end(), [](const std::string& x) { return !x.empty(); })) s.set("overrides", overrides);
    if (!a.override_force.empty()) s.set("force", a.override_force);
    std::string parts;
    for (const std::string& p : a.custom_parts) parts += (parts.empty() ? "" : ",") + p;
    if (!parts.empty()) s.set("parts", parts);
    eng::ConfigFile cfg;
    cfg.sections.push_back(std::move(s));
    return eng::fs::write_text_file(dir_ / (std::to_string(a.id) + ".cfg"),
                                    "# This server's own data for one Team Vanilla account (rank, SP and everything universal are TVAS's)\n" + cfg.serialize());
}

proto::Role Accounts::role_of(u64 id) const {
    if (id && id == owner_) return proto::Role::Owner;
    auto it = roles_.find(id);
    return it == roles_.end() ? proto::Role::Player : it->second.first;
}

void Accounts::set_role(u64 id, proto::Role role, const std::string& code_name) {
    if (role == proto::Role::Player || role == proto::Role::Owner) roles_.erase(id);
    else roles_[id] = {role, code_name};
    staff_dirty_ = true;
    if (Account* a = find(id)) a->server_role = role_of(id);
    save_staff();
}

bool Accounts::save_staff() {
    staff_dirty_ = false;
    if (staff_file_.empty()) return true;
    eng::ConfigFile cfg;
    for (const auto& [id, rc] : roles_) {
        eng::ConfigSection s;
        s.name = "staff";
        s.set("account", std::to_string(id));
        s.set("code_name", rc.second);
        s.set("role", rc.first == proto::Role::Admin ? "admin" : rc.first == proto::Role::GameMaster ? "game_master" : "moderator");
        cfg.sections.push_back(std::move(s));
    }
    return eng::fs::write_text_file(staff_file_, "# This server's staff by Team Vanilla account id (the Owner is the account that registered the server)\n"
                                                 "# role = admin | game_master | moderator\n" + cfg.serialize());
}

Report* Accounts::report(u32 id) {
    for (Report& r : reports_)
        if (r.id == id) return &r;
    return nullptr;
}

Report& Accounts::add_report(const Account* reporter, const Account& target, proto::ReportReason reason, std::string note, u32 match) {
    Report r;
    r.id = next_report_++;
    r.reporter = reporter ? reporter->id : 0;
    r.reporter_name = reporter ? reporter->code_name : std::string();
    r.target = target.id;
    r.target_name = target.code_name;
    r.reason = reason;
    r.time = u64(std::time(nullptr));
    r.note = std::move(note);
    r.match = match;
    reports_.push_back(std::move(r));
    while (reports_.size() > 2000) {
        auto closed = std::find_if(reports_.begin(), reports_.end(), [](const Report& x) { return x.state != proto::ReportState::Open; });
        reports_.erase(closed != reports_.end() ? closed : reports_.begin());
    }
    reports_dirty_ = true;
    return reports_.back();
}

bool Accounts::has_open_report(u64 reporter, u64 target) const {
    for (const Report& r : reports_)
        if (r.state == proto::ReportState::Open && r.reporter == reporter && r.target == target) return true;
    return false;
}

}  // namespace lsfs
