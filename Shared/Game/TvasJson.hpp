// The game's own structures as JSON, the way Team Vanilla's account service (TVAS) keeps and sends
// them (Docs/UniversalServerDeploy.md §4.1): the shop, the rewards and events, clan marks, owned
// things. One place, so the game, the server and tvasdata (which hands TVAS the rules) all write
// and read them alike.
//
// Wares are named the way accounts keep them (NM-7): a weapon by its item code ("A013"), a force,
// an item and a spray by their fixed numbers, pack content by its x:<pack>:<name> code.
#pragma once

#include "Engine/Core/Json.hpp"
#include "Game/Events.hpp"
#include "Game/Rules.hpp"
#include "Game/Shop.hpp"

#include <string>

namespace lsf::tvjson {

using eng::json::Value;

// A ware's key in TVAS's records: a weapon's code, else the number as text.
std::string ware_key(ShopKind kind, u16 id);
bool ware_from_key(ShopKind kind, std::string_view key, u16& id);

Value shop_config(const ShopConfig& c);
bool shop_config(const Value& v, ShopConfig& out);

Value rewards_config(const RewardsConfig& c);
bool rewards_config(const Value& v, RewardsConfig& out);
Value loot_table(const LootTable& t);
LootTable loot_table(const Value& v);
Value quest(const QuestDef& q);
QuestDef quest(const Value& v);
Value event(const EventDef& e);
EventDef event(const Value& v);

Value mark(const ClanMark& m);
ClanMark mark(const Value& v);

}  // namespace lsf::tvjson
