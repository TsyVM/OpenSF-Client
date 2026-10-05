// What Team Vanilla's account service answers (JSON), as the game's own structures
// (Docs/UniversalServerDeploy.md §6, §14): friends, the inbox, a clan, the clans there are, an ID
// card, the day's rewards, what a box or a capsule held, what a try at a gun came to, a staff card.
// One place, so the game and the checks read TVAS alike. Nothing here trusts what it reads: every
// text is cut to its length and cleaned, every number to its range.
#pragma once

#include "Engine/Core/Json.hpp"
#include "Game/Protocol.hpp"

#include <optional>
#include <string>

namespace lsf::tvproto {

using Json = eng::json::Value;

// A weapon's saved code as this session's number (kNoWeapon: none, or one this game has not).
u16 weapon_of(const Json& code);

proto::FriendList friend_list(const Json& reply);
proto::Mailbox mailbox(const Json& reply);
// `invite`: an officer's invitation waiting (a soldier in no clan), when the answer carries one.
proto::ClanState clan_state(const Json& state, std::optional<proto::ClanInvited>* invite = nullptr);
proto::ClanInvited clan_invited(const Json& invite);
proto::ClanDirectory clan_directory(const Json& reply);
proto::IdCard id_card(const Json& reply, const std::string& asked_for);
proto::RewardsState rewards_state(const Json& reply);
proto::BoxOpened box_opened(const Json& reply, u8 box);
proto::CapsuleResult capsule_result(const Json& reply, u16 capsule);
// `op`: the RequestOp the answer is for; `weapon`: the gun asked about (kNoWeapon: none).
proto::ServiceResult service_result(const Json& reply, proto::RequestOp op, u16 weapon);
proto::StaffAccountCard staff_card(const Json& card);

// The words TVAS knows each request by.
const char* friend_op(proto::FriendOp op);
const char* mail_op(proto::MailOp op);
const char* clan_op(proto::ClanOp op);
const char* claim_kind(proto::ClaimKind kind);
const char* staff_op(proto::StaffOp op);
const char* account_field(proto::AccountField field);   // nullptr: no such field
// A staff edit's `text` as TVAS keeps it (NM-7): a force and an item by number, a weapon by its code.
std::string account_field_key(proto::AccountField field, const std::string& text);

}  // namespace lsf::tvproto
