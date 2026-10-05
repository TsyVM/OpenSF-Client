#include "Game/TvasJson.hpp"

#include "Engine/Core/Strings.hpp"

#include <algorithm>

namespace lsf::tvjson {

namespace {

u32 u32_of(const Value& v, u32 fallback = 0) { return u32(std::min<u64>(v.as_uint(fallback), 0xFFFFFFFFull)); }
u16 u16_of(const Value& v, u16 fallback = 0) { return u16(std::min<u64>(v.as_uint(fallback), 0xFFFFull)); }
u8 u8_of(const Value& v, u8 fallback = 0) { return u8(std::min<u64>(v.as_uint(fallback), 0xFFull)); }

Value offer_list(const std::vector<ShopOffer>& offers) {
    Value a = Value::array();
    for (const ShopOffer& o : offers) {
        Value p = Value::array();
        p.push(unsigned(o.days));
        p.push(o.price);
        a.push(std::move(p));
    }
    return a;
}

std::vector<ShopOffer> offer_list(const Value& a) {
    std::vector<ShopOffer> out;
    for (const Value& p : a.elements())
        if (p.size() == 2 && out.size() < kShopOffers) out.push_back({u16_of(p[0]), u32_of(p[1])});
    return out;
}

Value capsule(const CapsuleDef& c) {
    Value v;
    v["id"] = unsigned(c.id);
    v["name"] = c.name;
    v["picture"] = c.picture;
    v["coins"] = unsigned(c.coins);
    v["price"] = c.price;
    v["listed"] = c.listed;
    Value prizes = Value::array();
    for (const CapsulePrize& p : c.prizes) {
        Value o;
        o["kind"] = unsigned(p.kind);
        if (p.kind == u8(CapsulePrizeKind::Weapon)) o["item"] = ware_key(ShopKind::Weapon, p.id);
        else o["item"] = unsigned(p.id);
        o["days"] = unsigned(p.days);
        o["lo"] = p.lo;
        o["hi"] = p.hi;
        o["weight"] = unsigned(p.weight);
        prizes.push(std::move(o));
    }
    v["prizes"] = std::move(prizes);
    return v;
}

CapsuleDef capsule(const Value& v) {
    CapsuleDef c;
    c.id = u16_of(v["id"]);
    c.name = v["name"].str();
    c.picture = v["picture"].str();
    c.coins = u8_of(v["coins"], 1);
    c.price = u32_of(v["price"]);
    c.listed = v["listed"].as_bool(true);
    for (const Value& o : v["prizes"].elements()) {
        CapsulePrize p;
        p.kind = u8_of(o["kind"]);
        if (p.kind == u8(CapsulePrizeKind::Weapon)) {
            if (!ware_from_key(ShopKind::Weapon, o["item"].str(), p.id)) continue;
        } else {
            p.id = u16_of(o["item"]);
        }
        p.days = u16_of(o["days"]);
        p.lo = u32_of(o["lo"]), p.hi = u32_of(o["hi"]);
        p.weight = u16_of(o["weight"], 1);
        c.prizes.push_back(p);
    }
    return c;
}

}  // namespace

std::string ware_key(ShopKind kind, u16 id) {
    if (kind == ShopKind::Weapon) {
        const WeaponDef* w = weapon(id);
        return w ? w->code : std::string();
    }
    return std::to_string(unsigned(id));
}

bool ware_from_key(ShopKind kind, std::string_view key, u16& id) {
    if (kind == ShopKind::Weapon) {
        const u16 w = weapon_from_saved(key);
        if (w == kNoWeapon) return false;
        id = w;
        return true;
    }
    int n = 0;
    if (!eng::str::parse_int(key, n) || n < 0 || n > 65535) return false;
    id = u16(n);
    return true;
}

Value shop_config(const ShopConfig& c) {
    Value v;
    Value entries = Value::array();
    for (const ShopEntry& e : c.entries) {
        const std::string key = ware_key(ShopKind(e.kind), e.id);
        if (key.empty()) continue;
        Value o;
        o["kind"] = unsigned(e.kind);
        o["item"] = key;
        o["listed"] = e.listed;
        o["giftable"] = e.giftable;
        o["offers"] = offer_list(e.offers);
        entries.push(std::move(o));
    }
    v["entries"] = std::move(entries);
    v["marquee"] = c.marquee;
    v["coin_price"] = c.coin_price;
    v["gift_discount"] = unsigned(c.gift_discount);
    Value caps = Value::array(), bags = Value::array();
    for (const CapsuleDef& cap : c.capsules) caps.push(capsule(cap));
    for (const CapsuleDef& bag : c.bags) bags.push(capsule(bag));
    v["capsules"] = std::move(caps);
    v["bags"] = std::move(bags);
    return v;
}

bool shop_config(const Value& v, ShopConfig& out) {
    if (!v.is_object()) return false;
    ShopConfig c = default_shop();
    c.entries.clear();
    for (const Value& o : v["entries"].elements()) {
        ShopEntry e;
        e.kind = u8_of(o["kind"]);
        if (e.kind >= u8(ShopKind::Count) || !ware_from_key(ShopKind(e.kind), o["item"].str(), e.id)) continue;
        e.listed = o["listed"].as_bool(true);
        e.giftable = o["giftable"].as_bool(true);
        e.offers = offer_list(o["offers"]);
        c.entries.push_back(std::move(e));
    }
    if (v.has("marquee")) c.marquee = v["marquee"].str();
    c.coin_price = u32_of(v["coin_price"], c.coin_price);
    c.gift_discount = u8_of(v["gift_discount"]);
    if (v.has("capsules")) {
        c.capsules.clear();
        for (const Value& cap : v["capsules"].elements()) c.capsules.push_back(capsule(cap));
    }
    c.bags.clear();
    for (const Value& bag : v["bags"].elements()) c.bags.push_back(capsule(bag));
    sanitize(c);
    out = std::move(c);
    return true;
}

Value loot_table(const LootTable& t) {
    Value v;
    v["box"] = unsigned(t.box);
    Value prizes = Value::array();
    for (const Prize& p : t.prizes) {
        Value o;
        o["kind"] = unsigned(p.kind);
        o["weight"] = unsigned(p.weight);
        o["lo"] = p.lo;
        o["hi"] = p.hi;
        o["match"] = p.match;
        prizes.push(std::move(o));
    }
    v["prizes"] = std::move(prizes);
    return v;
}

LootTable loot_table(const Value& v) {
    LootTable t;
    t.box = u8_of(v["box"]);
    for (const Value& o : v["prizes"].elements()) t.prizes.push_back({u8_of(o["kind"]), u16_of(o["weight"], 1), u32_of(o["lo"]), u32_of(o["hi"]), o["match"].str()});
    return t;
}

Value quest(const QuestDef& q) {
    Value v;
    v["id"] = unsigned(q.id);
    v["goal"] = unsigned(q.goal);
    v["target"] = unsigned(q.target);
    v["sp"] = q.sp;
    v["xp"] = q.xp;
    v["weight"] = unsigned(q.weight);
    v["text"] = q.text;
    v["shown"] = quest_text(q);
    return v;
}

QuestDef quest(const Value& v) {
    QuestDef q;
    q.id = u16_of(v["id"]);
    q.goal = u8_of(v["goal"]);
    q.target = u16_of(v["target"], 1);
    q.sp = u32_of(v["sp"]), q.xp = u32_of(v["xp"]);
    q.weight = u16_of(v["weight"], 10);
    q.text = v["text"].str();
    return q;
}

Value event(const EventDef& e) {
    Value v;
    v["id"] = e.id;
    v["name"] = e.name;
    v["banner"] = e.banner;
    v["theme"] = unsigned(e.theme);
    v["repeat"] = unsigned(e.repeat);
    v["start"] = e.start;
    v["end"] = e.end;
    v["sp_pct"] = unsigned(e.sp_pct);
    v["xp_pct"] = unsigned(e.xp_pct);
    v["drop_box"] = unsigned(e.drop_box);
    v["drop_permille"] = unsigned(e.drop_permille);
    v["sign_in_sp"] = e.sign_in_sp;
    Value qs = Value::array();
    for (const QuestDef& q : e.quests) qs.push(quest(q));
    v["quests"] = std::move(qs);
    v["enabled"] = e.enabled;
    return v;
}

EventDef event(const Value& v) {
    EventDef e;
    e.id = u32_of(v["id"]);
    e.name = v["name"].str();
    e.banner = v["banner"].str();
    e.theme = u8_of(v["theme"]);
    e.repeat = u8_of(v["repeat"]);
    e.start = v["start"].as_uint();
    e.end = v["end"].as_uint();
    e.sp_pct = u16_of(v["sp_pct"], 100);
    e.xp_pct = u16_of(v["xp_pct"], 100);
    e.drop_box = u8_of(v["drop_box"], kNoBox);
    e.drop_permille = u16_of(v["drop_permille"]);
    e.sign_in_sp = u32_of(v["sign_in_sp"]);
    for (const Value& q : v["quests"].elements()) e.quests.push_back(quest(q));
    e.enabled = v["enabled"].as_bool(true);
    return e;
}

Value rewards_config(const RewardsConfig& c) {
    Value v;
    Value boxes = Value::array();
    for (const LootTable& t : c.boxes) boxes.push(loot_table(t));
    v["boxes"] = std::move(boxes);
    Value play = Value::array();
    for (const PlayStep& s : c.play) {
        Value o;
        o["minutes"] = unsigned(s.minutes);
        o["sp"] = s.sp;
        o["box"] = unsigned(s.box);
        play.push(std::move(o));
    }
    v["play"] = std::move(play);
    Value week = Value::array();
    for (const SignInDay& d : c.week) {
        Value o;
        o["sp"] = d.sp;
        o["box"] = unsigned(d.box);
        week.push(std::move(o));
    }
    v["week"] = std::move(week);
    Value quests = Value::array();
    for (const QuestDef& q : c.quests) quests.push(quest(q));
    v["quests"] = std::move(quests);
    v["quests_a_day"] = unsigned(c.quests_a_day);
    v["all_quests_sp"] = c.all_quests_sp;
    v["all_quests_box"] = unsigned(c.all_quests_box);
    v["day_starts"] = unsigned(c.day_starts);
    Value events = Value::array();
    for (const EventDef& e : c.events) events.push(event(e));
    v["events"] = std::move(events);
    return v;
}

bool rewards_config(const Value& v, RewardsConfig& out) {
    if (!v.is_object()) return false;
    RewardsConfig c;
    for (const Value& t : v["boxes"].elements()) c.boxes.push_back(loot_table(t));
    for (const Value& s : v["play"].elements()) c.play.push_back({u16_of(s["minutes"], 60), u32_of(s["sp"]), u8_of(s["box"], kNoBox)});
    for (const Value& d : v["week"].elements()) c.week.push_back({u32_of(d["sp"]), u8_of(d["box"], kNoBox)});
    for (const Value& q : v["quests"].elements()) c.quests.push_back(quest(q));
    c.quests_a_day = u8_of(v["quests_a_day"], 3);
    c.all_quests_sp = u32_of(v["all_quests_sp"]);
    c.all_quests_box = u8_of(v["all_quests_box"], kNoBox);
    c.day_starts = u8_of(v["day_starts"]);
    for (const Value& e : v["events"].elements()) c.events.push_back(event(e));
    sanitize(c);
    out = std::move(c);
    return true;
}

Value mark(const ClanMark& m) {
    Value v;
    v["background"] = unsigned(m.background);
    v["frame"] = unsigned(m.frame);
    v["symbol"] = unsigned(m.symbol);
    v["emblem"] = emblem_text(m);
    return v;
}

ClanMark mark(const Value& v) {
    ClanMark m;
    if (!v.is_object()) return m;
    m.background = u8_of(v["background"], 1), m.frame = u8_of(v["frame"]), m.symbol = u8_of(v["symbol"], 1);
    emblem_from_text(v["emblem"].str(), m);
    sanitize_mark(m);
    return m;
}

}  // namespace lsf::tvjson
