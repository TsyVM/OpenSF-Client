// The soldiers this server knows (Docs/UniversalServerDeploy.md §6, §12.2). Nobody's account lives
// here any more: a soldier is a Team Vanilla account, and everything universal -- rank, XP, SP,
// Coins, stats, the base things owned and worn, the clan -- is TVAS's, read when they join (and
// again when their game says it changed: proto::Refresh) and never written by this server (PR-1).
//
// What is this server's own is kept in accounts/<TV account id>.cfg (§12.2): its custom items and
// characters owned here (D12), the slots filled with them (D16), its own bans and mutes. Server
// roles are in staff.cfg (§7), by account id. Everything is keyed by the TV account id (ID-7).
#pragma once

#include "ServerOnly.hpp"

#include "Game/Events.hpp"
#include "Game/Wear.hpp"
#include "Game/Items.hpp"
#include "Game/Protocol.hpp"
#include "Game/Shop.hpp"

#include "Engine/Core/Json.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lsfs {

using namespace lsf;

struct Account {
    u64 id = 0;                  // the TV account (ID-7)
    std::string code_name;
    u32 rev = 0;                 // the universal record's version this copy was read at (ID-3)
    proto::GlobalRole global_role = proto::GlobalRole::Player;   // from TVAS and the ticket (EN-4)
    proto::Role server_role = proto::Role::Player;               // this server's (staff.cfg; the Owner's from TVAS)
    // Universal, as TVAS last said (§6.1).
    u32 xp = 0, sp = 0, coins = 0;
    u32 kills = 0, deaths = 0, wins = 0, losses = 0, headshots = 0, matches = 0;
    u32 forfeits = 0, team_kills = 0, missions = 0, attended = 0;
    u32 rounds = 0, survived = 0;
    u64 shots = 0, hits = 0;
    std::string profile_text;
    u8 force = 0;
    Loadout loadout = starter_loadout();   // universal, with this server's overrides on top (D16)
    Loadout universal_loadout = starter_loadout();
    std::vector<u16> weapons;    // owned: base (universal) and this server's own (D12)
    std::vector<u8> forces;
    u8 sp_charge = 0;
    std::string clan;            // the clan's name (universal: §14.1)
    u64 clan_id = 0;
    ClanMark clan_mark;
    ClanRank clan_rank = ClanRank::Recruit;
    u64 muted_until = 0;         // a global mute (SFLegacy Staff), from TVAS
    // This server's own (§6.2): its bans and mutes.
    static constexpr u64 kForever = ~u64(0);
    u64 server_muted_until = 0, banned_until = 0;
    std::string ban_reason;
    u64 global_muted_until = 0;  // off this server's global chat until then
    std::vector<u64> blocks;     // soldiers this one blocks or is blocked by (BL-4: only to keep rooms apart)
    bool blocks_with(u64 other) const;

    bool staff() const { return server_role >= proto::Role::Moderator || global_role >= proto::GlobalRole::Moderator; }
    bool game_master() const { return server_role >= proto::Role::GameMaster || global_role >= proto::GlobalRole::GameMaster; }
    bool admin() const { return server_role >= proto::Role::Admin || global_role >= proto::GlobalRole::GameMaster; }
    bool sflegacy() const { return global_role >= proto::GlobalRole::Moderator; }
    // What this soldier may do here, as one role for the client's panel (Profile::role).
    proto::Role effective_role() const;
    // SFLegacy Staff carry the staff variants (the admin-only weapons) on every server.
    bool carries_staff_variants() const { return global_role >= proto::GlobalRole::GameMaster; }

    struct Gift {
        u32 id = 0;
        u8 kind = 0;
        u16 item = 0;
        u16 days = 0;
        std::string from;
    };
    std::vector<Gift> gifts;
    std::array<u16, kHorrorItems> horror_items{};
    std::map<u16, u16> bags;
    std::array<u16, kBoxKinds> boxes{};

    struct Item {
        u16 id = 0;
        u64 expires = 0;
        u16 uses = 0;
    };
    std::vector<Item> items;
    std::vector<u16> equipped;
    u8 name_colour = 0, clan_colour = 0;
    u8 fake_rank = 0xFF;

    struct Gun {
        u64 expires = 0;
        u8 durability = kDurabilityFull;
        bool plain() const { return expires == 0 && durability >= kDurabilityFull; }
    };
    std::map<u16, Gun> guns;
    std::map<u8, u64> force_until;
    std::vector<Item> sprays;
    u16 spray = kNoSpray;

    // This server's own items owned here (D12): a pack weapon, character or part by its saved code
    // (x:<pack>:<name>, NM-7), until when (0 for good). Read into `weapons`, `forces`, `items` with
    // this session's numbers when the registry has them; one whose pack is gone is kept, unshown.
    struct Custom {
        std::string code;
        u64 expires = 0;
        u8 kind = 0;             // ShopKind
    };
    std::vector<Custom> custom;
    // The slots filled here with this server's own weapons, by saved code (D16); a missing one falls
    // back to the universal loadout's (§6.2).
    std::array<std::string, kLoadoutSlots> override_slots;
    std::string override_force;  // a pack character worn here (x:<pack>:<name>), empty: the universal force
    std::vector<std::string> custom_parts;   // pack parts worn here

    u8 durability_of(u16 weapon) const;
    bool broken(u16 weapon) const { return durability_of(weapon) == 0; }
    u64 weapon_until(u16 id) const;
    bool owns_spray(u16 id, u64 now) const;
    bool owns_weapon(u16 id) const;
    bool owns_force(u8 id) const;
    proto::Profile profile() const;
    const Item* find_item(u16 id) const;
    bool has_boost(Boost b, u64 now) const;
    std::vector<u16> worn(u8 f, u64 now) const;
    u32 shown_xp(u64 now) const;
    // The loadout as carried here: the universal one with this server's own weapons on top (§6.2).
    void apply_overrides();
};

// A complaint about a soldier, for this server's staff (Staff.cpp): reporter and target by TV
// account id (0: the server's own flags, "System").
struct Report {
    u32 id = 0;
    u64 reporter = 0, target = 0;
    std::string reporter_name, target_name;   // code names as they were then
    proto::ReportReason reason = proto::ReportReason::Other;
    proto::ReportState state = proto::ReportState::Open;
    u64 time = 0;
    std::string note;
    u32 match = 0;
};

class Accounts {
public:
    // `dir`: accounts/ (one file a soldier); `staff_file`: staff.cfg. Reports live in dir/reports.cfg.
    bool open(const std::filesystem::path& dir, const std::filesystem::path& staff_file);
    // Writes whatever changed.
    bool save();
    void mark_dirty(u64 id);
    void mark_dirty() { reports_dirty_ = true; }

    Account* find(u64 id);
    Account* find_code_name(std::string_view code_name);
    // "#123" or a code name, among the soldiers this server knows: those signed in, and those it
    // keeps a record of (a ban, a mute, something of its own they own) who are away. One who is
    // away is read from their file: this server's own data only, nothing universal.
    Account* find_token(std::string_view token);
    // The soldier as TVAS describes them (its /v1/server/profile answer): made, or brought up to date,
    // with this server's own data read from accounts/<id>.cfg the first time.
    Account& apply_profile(const eng::json::Value& me);
    // Forget a soldier who left (their local data is saved first).
    void unload(u64 id);
    // One of this server's own things for a soldier who may not be here (a gift: SH-9).
    void grant_custom(u64 id, const Account::Custom& c);
    const std::unordered_map<u64, std::unique_ptr<Account>>& all() const { return accounts_; }
    size_t size() const { return accounts_.size(); }

    // staff.cfg (§7): this server's roles by account id. The Owner is the registration's (RL-6).
    proto::Role role_of(u64 id) const;
    void set_role(u64 id, proto::Role role, const std::string& code_name);
    void set_owner(u64 id) { owner_ = id; }
    u64 owner() const { return owner_; }
    const std::map<u64, std::pair<proto::Role, std::string>>& staff() const { return roles_; }

    // Reports, oldest first.
    const std::vector<Report>& reports() const { return reports_; }
    Report* report(u32 id);
    Report& add_report(const Account* reporter, const Account& target, proto::ReportReason reason, std::string note, u32 match);
    bool has_open_report(u64 reporter, u64 target) const;

private:
    void load_local(Account& a);
    bool save_local(const Account& a);
    bool save_staff();
    std::filesystem::path dir_, staff_file_;
    std::unordered_map<u64, std::unique_ptr<Account>> accounts_;
    std::map<u64, std::string> known_;   // every soldier with a file here, by id: the code name it was saved under
    std::vector<u64> dirty_;
    std::map<u64, std::pair<proto::Role, std::string>> roles_;
    u64 owner_ = 0;
    std::vector<Report> reports_;
    u32 next_report_ = 1;
    bool reports_dirty_ = false;
    bool staff_dirty_ = false;
};

}  // namespace lsfs
