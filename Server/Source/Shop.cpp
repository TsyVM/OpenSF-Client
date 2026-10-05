// This server's shop (Docs/UniversalServerDeploy.md §6.3 to §6.5): the catalog its Owner, Admins and
// Game Masters keep (shop.cfg, F9), every sale made with the player's own approval from TVAS (MN-2)
// and sent to TVAS, which takes the SP or Coins. A base item is TV's to price (never under its
// price: SH-1 to SH-5) and lands in the universal record (SH-6); one of this server's own is any
// price the owner chose (SH-8) and stays here (D12). This server never adds SP or Coins to anyone
// (MN-1). On an official server this shop is TV's own, and it is pushed to TVAS when edited.
//
// TV's own services -- mending, selling back, coins, TV's capsules and Duffle Bags and Supply Crates,
// gifts opened -- are the game's with TVAS (CP-2, SH-7, SH-10).
#include "Server.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Registry.hpp"
#include "Game/TvasJson.hpp"

#include <algorithm>
#include <ctime>

namespace lsfs {

using namespace lsf::proto;
using eng::json::Value;

namespace {

std::string for_how_long(ShopKind kind, u16 id, u16 days) {
    if (kind == ShopKind::Item)
        if (const ItemDef* d = item(id); d && d->one_use()) return {};
    return days ? eng::str::format(" for %u day%s", unsigned(days), days == 1 ? "" : "s") : std::string(" for good");
}

// A ware's saved code: a pack's x:<pack>:<name>, else empty (a base ware).
std::string custom_code(ShopKind kind, u16 id) {
    switch (kind) {
        case ShopKind::Weapon:
            if (id >= kFirstPackWeapon && id != kNoWeapon)
                if (const WeaponDef* w = weapon(id)) return w->code;
            break;
        case ShopKind::Force:
            if (id >= kFirstPackForce && id < kNoForce)
                if (const PackForce* f = registry::force(u8(id))) return f->code;
            break;
        case ShopKind::Item:
            if (id >= kFirstPackItem)
                if (const PackItem* i = registry::item(id)) return i->code;
            break;
        default: break;
    }
    return {};
}

}  // namespace

void Server::load_shop() {
    shop_file_ = options_.shop;
    if (shop_file_.empty()) shop_file_ = options_.accounts_dir.parent_path() / "shop.cfg";
    shop_ = default_shop();
    sanitize(shop_);
    const auto text = eng::fs::read_text_file(shop_file_);
    if (!text) {
        LOG_INFO("Shop: %s does not exist yet; the game's own catalog is written there", eng::str::narrow(shop_file_.wstring()).c_str());
        save_shop();
        return;
    }
    std::string why;
    ShopConfig c;
    if (!shop_from_text(*text, c, &why)) {
        LOG_ERROR("Shop: %s: %s (the game's own catalog is used; the file is left as it is)", eng::str::narrow(shop_file_.wstring()).c_str(), why.c_str());
        shop_file_.clear();
        return;
    }
    shop_ = std::move(c);
    LOG_INFO("Shop: %zu things changed from the catalog, %zu capsules", shop_.entries.size(), shop_.capsules.size());
}

bool Server::save_shop() {
    if (shop_file_.empty()) return true;
    if (!eng::fs::write_text_file(shop_file_, shop_text(shop_))) {
        LOG_ERROR("Shop: cannot write %s", eng::str::narrow(shop_file_.wstring()).c_str());
        return false;
    }
    return true;
}

// SH-1, SH-2, SH-4, SH-5: on a community server, a base item sells only for the lengths TV sells it
// for, never below TV's price, and only while TV sells it at all. Its capsules hold only its own
// items (CP-1), numbered from kCustomCapsuleFirst so the game asks this server for them.
void Server::lift_to_floor() {
    if (tier_ == u32(Tier::Official) || tv_shop_.entries.empty() && tv_shop_rev_ == 0) return;
    bool changed = false;
    std::vector<ShopEntry> kept;
    for (ShopEntry e : shop_.entries) {
        const ShopKind kind = ShopKind(e.kind);
        if (!custom_code(kind, e.id).empty()) {
            kept.push_back(std::move(e));
            continue;
        }
        const ShopLine tv = shop_line(tv_shop_, kind, e.id);
        if (!tv.listed) {
            if (e.listed) changed = true;
            e.listed = false;
        }
        std::vector<ShopOffer> offers;
        for (ShopOffer o : e.offers) {
            const auto it = std::find_if(tv.offers.begin(), tv.offers.end(), [&](const ShopOffer& t) { return t.days == o.days; });
            if (it == tv.offers.end()) {
                changed = true;
                continue;
            }
            if (o.price < it->price) o.price = it->price, changed = true;
            offers.push_back(o);
        }
        e.offers = std::move(offers);
        kept.push_back(std::move(e));
    }
    shop_.entries = std::move(kept);
    for (CapsuleDef& c : shop_.capsules) {
        const size_t before = c.prizes.size();
        std::erase_if(c.prizes, [](const CapsulePrize& p) { return p.kind != u8(CapsulePrizeKind::Weapon) || p.id < kFirstPackWeapon; });
        if (c.id < kCustomCapsuleFirst) c.id = u16(c.id + kCustomCapsuleFirst), changed = true;
        changed |= c.prizes.size() != before;
    }
    std::erase_if(shop_.capsules, [](const CapsuleDef& c) { return c.prizes.empty(); });
    shop_.bags.clear();   // Duffle Bags with universal prizes are TV's alone (CP-2)
    if (changed) {
        save_shop();
        LOG_INFO("Shop: prices under Team Vanilla's lifted, lengths TV does not sell taken off (SH-1, SH-2)");
        for (auto& [id, p] : peers_)
            if (p.signed_in()) send_shop(p);
    }
}

void Server::send_shop(Peer& p) {
    ShopState m;
    m.config = shop_;
    send_msg(p.id, m);
}

int Server::shop_roll() {
    const int fixed = shop_roll_.load();
    if (fixed >= 0) return std::min(fixed, 999);
    return int(std::uniform_int_distribution<u32>(0, 999)(shop_rng_));
}

void Server::handle_shop(Peer& p, Msg kind, std::span<const u8> data) {
    switch (kind) {
        case Msg::Buy: {
            Buy m;
            if (decode(data, m)) buy(p, m);
            break;
        }
        case Msg::SendGift: {
            SendGift m;
            if (decode(data, m)) send_gift(p, m);
            break;
        }
        case Msg::Request: {
            Request m;
            if (!decode(data, m) || m.op >= u8(RequestOp::Count)) break;
            switch (RequestOp(m.op)) {
                case RequestOp::Shop: send_shop(p); break;
                case RequestOp::ReplayList: replay_list(p); break;
                case RequestOp::ReplayGet: replay_get(p, m.value); break;
                case RequestOp::Capsule:
                    if (m.a >= kCustomCapsuleFirst) custom_capsule(p, m);
                    else notice(p.id, NoticeKind::Info, "Team Vanilla's capsules are turned through Team Vanilla.");
                    break;
                case RequestOp::Sell:
                    if (!custom_code(ShopKind(m.b), m.a).empty()) custom_sell(p, m);
                    else notice(p.id, NoticeKind::Info, "Base items are sold back through Team Vanilla, at its price.");
                    break;
                default:
                    notice(p.id, NoticeKind::Info, "That is one of Team Vanilla's own services: your game asks Team Vanilla for it.");
                    break;
            }
            break;
        }
        case Msg::StaffShopSave: {
            StaffShopSave m;
            if (!decode(data, m)) break;
            Account& a = *p.account;
            if (!a.game_master()) return staff_result(p.id, false, "Only the Owner, Server Admins and Game Masters change the shop.");
            sanitize(m.config);
            const bool marquee = m.config.marquee != shop_.marquee;
            shop_ = std::move(m.config);
            if (tier_ == u32(Tier::Official)) {
                // TV's own shop: every server's floor (SH-5), and TV's capsules and services' odds.
                Value body;
                body["config"] = lsf::tvjson::shop_config(shop_);
                tvas_.post("/v1/server/tvshop", body, [](const TvasReply& r) {
                    if (!r.ok()) LOG_ERROR("TVAS: the TV shop was not taken: %s", r.error.c_str());
                });
            } else {
                // Duffle Bags are TV's alone (CP-2): TVAS sells only the official server's, so a bag
                // kept here would be shown and never sold. Cleared even before TV's shop is known.
                shop_.bags.clear();
                lift_to_floor();
            }
            save_shop();
            staff_logged(a, nullptr, "shop.save", "", eng::str::format("%zu entries, %zu capsules", shop_.entries.size(), shop_.capsules.size()));
            for (auto& [id, other] : peers_)
                if (other.signed_in()) send_shop(other);
            staff_result(p.id, true, marquee ? "The shop is saved, and the line along the bottom is changed for everyone." : "The shop is saved, and in use now.");
            break;
        }
        default: break;
    }
}

void Server::buy(Peer& p, const Buy& m) {
    Account& a = *p.account;
    auto refuse = [&](std::string text) {
        ShopResult res;
        res.text = std::move(text);
        send_msg(p.id, res);
    };
    if (m.kind >= u8(ShopKind::Count)) return;
    const ShopKind kind = ShopKind(m.kind);
    const ShopLine line = shop_line(shop_, kind, m.item);
    if (!line.exists || !line.listed || m.offer >= line.offers.size()) return refuse(kind == ShopKind::Force ? "That force is not for sale." : "That item is not for sale.");
    const ShopOffer offer = line.offers[m.offer];
    // MN-2: the player approved this price; a sale at any other is refused here and at TVAS.
    if (m.price != offer.price) return refuse("The price changed: look again before you buy.");
    if (m.approval.empty()) return refuse("Every sale needs your own approval from Team Vanilla.");
    const std::string name = ware_name(kind, m.item);
    const std::string code = custom_code(kind, m.item);
    Value sale;
    sale["account"] = a.id;
    sale["approval"] = m.approval;
    sale["amount"] = offer.price;
    sale["currency"] = 0;
    sale["days"] = unsigned(offer.days);
    if (!code.empty()) {
        sale["kind"] = "custom";
        sale["item"] = code;
    } else {
        sale["kind"] = "ware";
        sale["ware"] = unsigned(kind);
        sale["item"] = lsf::tvjson::ware_key(kind, m.item);
    }
    const u32 peer_id = p.id;
    const u64 account = a.id;
    tvas_.post("/v1/server/sale", sale, [this, peer_id, account, kind, item = m.item, code, offer, name](const TvasReply& r) {
        Peer* pp = peer(peer_id);
        ShopResult res;
        if (!r.ok()) {
            res.text = r.down() ? "Team Vanilla's services are down: nothing was bought." : r.error;
            if (pp) send_msg(peer_id, res);
            return;
        }
        Account* acc = accounts_.find(account);
        if (acc && !code.empty()) {
            // This server's own item (D12): kept here, by its saved code (NM-7).
            const u64 now = u64(std::time(nullptr));
            auto it = std::find_if(acc->custom.begin(), acc->custom.end(), [&](const Account::Custom& c) { return c.code == code; });
            if (it == acc->custom.end()) acc->custom.push_back({code, offer.days ? now + u64(offer.days) * 86400 : 0, u8(kind)});
            else if (it->expires) it->expires = offer.days ? std::max(it->expires, now) + u64(offer.days) * 86400 : 0;
            if (kind == ShopKind::Weapon)
                if (const WeaponDef* w = weapon(item)) acc->override_slots[equip_cell(acc->loadout, *w)] = code;
            accounts_.mark_dirty(account);
        }
        apply_profile(r.body["me"]);
        res.ok = true;
        res.text = kind == ShopKind::Force ? "Joined " + name + for_how_long(kind, item, offer.days) + "." : "Bought " + name + for_how_long(kind, item, offer.days) + ".";
        if (pp) {
            send_msg(peer_id, res);
            if (Room* room = find_room(pp->room)) send_room_state(*room);
        }
    });
}

void Server::send_gift(Peer& p, const SendGift& m) {
    Account& a = *p.account;
    auto refuse = [&](std::string text) {
        ShopResult res;
        res.text = std::move(text);
        send_msg(p.id, res);
    };
    if (m.kind >= u8(ShopKind::Count)) return;
    const ShopKind kind = ShopKind(m.kind);
    const ShopLine line = shop_line(shop_, kind, m.item);
    if (!line.exists || !line.listed || m.offer >= line.offers.size()) return refuse("That item is not for sale.");
    if (!line.giftable) return refuse(ware_name(kind, m.item) + " cannot be sent as a gift.");
    const ShopOffer offer = line.offers[m.offer];
    const u32 price = gift_price(shop_, offer.price);
    if (m.price != price) return refuse("The price changed: look again before you send it.");
    const std::string code = custom_code(kind, m.item);
    const std::string to = eng::str::sanitize_line(m.to, kNameMax);
    Value sale;
    sale["account"] = a.id;
    sale["approval"] = m.approval;
    sale["amount"] = price;
    sale["currency"] = 0;
    sale["days"] = unsigned(offer.days);
    sale["to"] = to;
    if (!code.empty()) {
        sale["kind"] = "custom_gift";
        sale["item"] = code;
    } else {
        sale["kind"] = "gift";
        sale["ware"] = unsigned(kind);
        sale["item"] = lsf::tvjson::ware_key(kind, m.item);
    }
    const u32 peer_id = p.id;
    tvas_.post("/v1/server/sale", sale, [this, peer_id, code, kind, offer, to, name = ware_name(kind, m.item)](const TvasReply& r) {
        ShopResult res;
        if (!r.ok()) {
            res.text = r.down() ? "Team Vanilla's services are down: nothing was sent." : r.error;
            if (peer(peer_id)) send_msg(peer_id, res);
            return;
        }
        if (!code.empty()) {
            // SH-9: a custom gift stays on this server, kept for the friend by their account id.
            const u64 friend_id = r.body["to"].as_uint();
            const u64 now = u64(std::time(nullptr));
            accounts_.grant_custom(friend_id, {code, offer.days ? now + u64(offer.days) * 86400 : 0, u8(kind)});
            if (Account* them = accounts_.find(friend_id))
                if (Peer* tp = peer_of(them)) {
                    notice(tp->id, NoticeKind::Good, "You received a gift on this server: " + name + ".");
                    fetch_profile(tp->id, friend_id, false);
                }
        }
        apply_profile(r.body["me"]);
        res.ok = true;
        res.text = r.body["text"].str("You've successfully sent the gift: " + name + " to " + to + ".");
        if (peer(peer_id)) send_msg(peer_id, res);
    });
}

// A turn of one of this server's own capsules (CP-1, CP-3, CP-4): paid with the player's approved
// coins at TVAS, a prize of this server's own drawn here by the odds it shows.
void Server::custom_capsule(Peer& p, const Request& m) {
    Account& a = *p.account;
    CapsuleResult res;
    res.capsule = m.a;
    const CapsuleDef* cap = shop_.capsule(m.a);
    if (!cap || !cap->listed || cap->prizes.empty()) {
        res.text = "That capsule is not in the machine.";
        send_msg(p.id, res);
        return;
    }
    Value sale;
    sale["account"] = a.id;
    sale["approval"] = m.approval;
    sale["amount"] = unsigned(cap->coins);
    sale["currency"] = 1;
    sale["kind"] = "custom_capsule";
    sale["item"] = eng::str::format("capsule:%u", unsigned(cap->id));
    sale["days"] = 0;
    const u32 peer_id = p.id;
    const u64 account = a.id;
    const CapsuleDef def = *cap;
    tvas_.post("/v1/server/sale", sale, [this, peer_id, account, def](const TvasReply& r) {
        CapsuleResult out;
        out.capsule = def.id;
        if (!r.ok()) {
            out.text = r.down() ? "Team Vanilla's services are down: the capsule was not turned." : r.error;
            if (peer(peer_id)) send_msg(peer_id, out);
            return;
        }
        u32 total = 0;
        for (const CapsulePrize& pr : def.prizes) total += pr.weight;
        u32 roll = u32(u64(shop_roll()) * std::max(1u, total) / 1000u);
        size_t k = 0;
        while (k + 1 < def.prizes.size() && roll >= def.prizes[k].weight) roll -= def.prizes[k].weight, ++k;
        const CapsulePrize prize = def.prizes[k];
        Account* acc = accounts_.find(account);
        const WeaponDef* w = weapon(prize.id);
        if (acc && w && !w->code.empty()) {
            const u64 now = u64(std::time(nullptr));
            auto it = std::find_if(acc->custom.begin(), acc->custom.end(), [&](const Account::Custom& c) { return c.code == w->code; });
            if (it == acc->custom.end()) acc->custom.push_back({w->code, prize.days ? now + u64(prize.days) * 86400 : 0, u8(ShopKind::Weapon)});
            else if (it->expires) it->expires = prize.days ? std::max(it->expires, now) + u64(prize.days) * 86400 : 0;
            accounts_.mark_dirty(account);
            // CP-4: each draw is kept with the sale, so staff can check a complaint.
            if (Peer* pp = peer(peer_id); pp && pp->account) staff_logged(*pp->account, nullptr, "capsule.draw", "", def.name + ": " + capsule_prize_text(prize));
        }
        apply_profile(r.body["me"]);
        out.ok = true;
        out.prize = prize;
        out.text = capsule_prize_text(prize);
        if (peer(peer_id)) send_msg(peer_id, out);
    });
}

// One of this server's own items sold back (SH-8): never more than the player paid for it here.
void Server::custom_sell(Peer& p, const Request& m) {
    Account& a = *p.account;
    const ShopKind kind = ShopKind(m.b);
    const std::string code = custom_code(kind, m.a);
    auto it = std::find_if(a.custom.begin(), a.custom.end(), [&](const Account::Custom& c) { return c.code == code; });
    ServiceResult res;
    res.op = m.op;
    res.weapon = kind == ShopKind::Weapon ? m.a : kNoWeapon;
    if (code.empty() || it == a.custom.end()) {
        res.text = "You do not own that here.";
        send_msg(p.id, res);
        return;
    }
    const u32 value = resale_price(for_good_price(shop_, kind, m.a));
    Value body;
    body["account"] = a.id;
    body["item"] = code;
    body["amount"] = value;
    const u32 peer_id = p.id;
    const u64 account = a.id;
    tvas_.post("/v1/server/sellback", body, [this, peer_id, account, code, res](const TvasReply& r) mutable {
        if (!r.ok()) {
            res.text = r.down() ? "Team Vanilla's services are down: nothing was sold." : r.error;
            if (peer(peer_id)) send_msg(peer_id, res);
            return;
        }
        if (Account* acc = accounts_.find(account)) {
            std::erase_if(acc->custom, [&](const Account::Custom& c) { return c.code == code; });
            for (std::string& s : acc->override_slots)
                if (s == code) s.clear();
            if (acc->override_force == code) acc->override_force.clear();
            std::erase(acc->custom_parts, code);
            accounts_.mark_dirty(account);
        }
        apply_profile(r.body["me"]);
        res.ok = true;
        res.text = eng::str::format("Sold for %s SP.", eng::str::thousands(r.body["paid"].as_uint()).c_str());
        if (peer(peer_id)) send_msg(peer_id, res);
    });
}

}  // namespace lsfs
