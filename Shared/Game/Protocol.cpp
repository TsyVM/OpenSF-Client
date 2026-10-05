#include "Game/Protocol.hpp"

#include "Engine/Core/Strings.hpp"

namespace lsf::proto {

const char* role_name(Role r) {
    switch (r) {
        case Role::Moderator: return "Moderator";
        case Role::GameMaster: return "Game Master";
        case Role::Admin: return "Server Admin";
        case Role::Owner: return "Owner";
        default: return "Player";
    }
}

const char* global_role_name(GlobalRole r) {
    switch (r) {
        case GlobalRole::Moderator: return "SFLegacy Moderator";
        case GlobalRole::GameMaster: return "SFLegacy Game Master";
        case GlobalRole::Admin: return "SFLegacy Admin";
        default: return "Player";
    }
}

const char* platform_name(u8 platform) {
    switch (Platform(platform)) {
        case Platform::Windows: return "Windows";
        case Platform::Android: return "Android";
        case Platform::MacOS: return "macOS";
        case Platform::Linux: return "Linux";
    }
    return "?";
}

const char* report_reason_name(ReportReason r) {
    switch (r) {
        case ReportReason::Cheating: return "Cheating";
        case ReportReason::Abuse: return "Abusive chat";
        case ReportReason::TeamKilling: return "Team killing / griefing";
        case ReportReason::Leaving: return "Leaving / idling";
        case ReportReason::BadName: return "Offensive name";
        default: return "Other";
    }
}

const char* message_name(Msg id) {
    switch (id) {
        case Msg::Hello: return "Hello";
        case Msg::Login: return "Login";
        case Msg::Register: return "Register";
        case Msg::SetCodeName: return "SetCodeName";
        case Msg::ListChannels: return "ListChannels";
        case Msg::JoinChannel: return "JoinChannel";
        case Msg::LeaveChannel: return "LeaveChannel";
        case Msg::Chat: return "Chat";
        case Msg::CreateRoom: return "CreateRoom";
        case Msg::JoinRoom: return "JoinRoom";
        case Msg::QuickJoin: return "QuickJoin";
        case Msg::LeaveRoom: return "LeaveRoom";
        case Msg::ChangeRoom: return "ChangeRoom";
        case Msg::SetTeam: return "SetTeam";
        case Msg::SetReady: return "SetReady";
        case Msg::SetLoadout: return "SetLoadout";
        case Msg::KickPlayer: return "KickPlayer";
        case Msg::StartMatch: return "StartMatch";
        case Msg::LoadDone: return "LoadDone";
        case Msg::Input: return "Input";
        case Msg::Shoot: return "Shoot";
        case Msg::Throw: return "Throw";
        case Msg::Radio: return "Radio";
        case Msg::LeaveMatch: return "LeaveMatch";
        case Msg::Buy: return "Buy";
        case Msg::Equip: return "Equip";
        case Msg::RechargeSp: return "RechargeSp";
        case Msg::ClanRequest: return "ClanRequest";
        case Msg::CreateClan: return "CreateClan";
        case Msg::LeaveClan: return "LeaveClan";
        case Msg::ClanInvite: return "ClanInvite";
        case Msg::ClanAnswer: return "ClanAnswer";
        case Msg::ClanKick: return "ClanKick";
        case Msg::ClanSetMark: return "ClanSetMark";
        case Msg::RewardsRequest: return "RewardsRequest";
        case Msg::RewardsClaim: return "RewardsClaim";
        case Msg::OpenBox: return "OpenBox";
        case Msg::StaffRewardsRequest: return "StaffRewardsRequest";
        case Msg::StaffRewardsSave: return "StaffRewardsSave";
        case Msg::UseSkill: return "UseSkill";
        case Msg::PickClass: return "PickClass";
        case Msg::FriendAction: return "FriendAction";
        case Msg::MailAction: return "MailAction";
        case Msg::ChatPrefs: return "ChatPrefs";
        case Msg::ClanAction: return "ClanAction";
        case Msg::ClanBrowse: return "ClanBrowse";
        case Msg::RoomInvite: return "RoomInvite";
        case Msg::RoomInviteAnswer: return "RoomInviteAnswer";
        case Msg::UseItem: return "UseItem";
        case Msg::ReportPlayer: return "ReportPlayer";
        case Msg::CallVote: return "CallVote";
        case Msg::CastVote: return "CastVote";
        case Msg::StaffListReports: return "StaffListReports";
        case Msg::StaffResolveReport: return "StaffResolveReport";
        case Msg::StaffAction: return "StaffAction";
        case Msg::StaffFindAccount: return "StaffFindAccount";
        case Msg::StaffEditAccount: return "StaffEditAccount";
        case Msg::StaffListRecordings: return "StaffListRecordings";
        case Msg::StaffRequestReplay: return "StaffRequestReplay";
        case Msg::StaffResult: return "StaffResult";
        case Msg::StaffReportList: return "StaffReportList";
        case Msg::StaffAccountCard: return "StaffAccountCard";
        case Msg::StaffRecordingList: return "StaffRecordingList";
        case Msg::ReplayChunk: return "ReplayChunk";
        case Msg::VoteState: return "VoteState";
        case Msg::RewardsStateMsg: return "RewardsState";
        case Msg::BoxOpened: return "BoxOpened";
        case Msg::StaffRewardsMsg: return "StaffRewards";
        case Msg::ModeStateMsg: return "ModeState";
        case Msg::ModeEventMsg: return "ModeEvent";
        case Msg::FriendListMsg: return "FriendList";
        case Msg::MailboxMsg: return "Mailbox";
        case Msg::ClanDirectoryMsg: return "ClanDirectory";
        case Msg::RoomInvited: return "RoomInvited";
        case Msg::Request: return "Request";
        case Msg::Spray: return "Spray";
        case Msg::StaffShopSave: return "StaffShopSave";
        case Msg::ShopStateMsg: return "ShopState";
        case Msg::ServiceResult: return "ServiceResult";
        case Msg::CapsuleResult: return "CapsuleResult";
        case Msg::SprayFx: return "SprayFx";
        case Msg::ReplayListMsg: return "ReplayList";
        case Msg::Rearm: return "Rearm";
        case Msg::IdCardMsg: return "IdCard";
        case Msg::SpecialPointMsg: return "SpecialPoint";
        case Msg::IdCardRequest: return "IdCardRequest";
        case Msg::SendGift: return "SendGift";
        case Msg::UseHorrorItem: return "UseHorrorItem";
        case Msg::HorrorItemUsed: return "HorrorItemUsed";
        case Msg::CannonUse: return "CannonUse";
        case Msg::CannonFx: return "CannonFx";
        case Msg::DropWeapon: return "DropWeapon";
        case Msg::PickUpWeapon: return "PickUpWeapon";
        case Msg::Welcome: return "Welcome";
        case Msg::Refused: return "Refused";
        case Msg::Profile: return "Profile";
        case Msg::Notice: return "Notice";
        case Msg::ChannelList: return "ChannelList";
        case Msg::ChannelJoined: return "ChannelJoined";
        case Msg::LobbyState: return "LobbyState";
        case Msg::RoomSummaryMsg: return "RoomSummary";
        case Msg::RoomRemoved: return "RoomRemoved";
        case Msg::LobbyUser: return "LobbyUser";
        case Msg::LobbyUserLeft: return "LobbyUserLeft";
        case Msg::ChatLine: return "ChatLine";
        case Msg::RoomState: return "RoomState";
        case Msg::RoomLeft: return "RoomLeft";
        case Msg::MatchLoad: return "MatchLoad";
        case Msg::MatchBegin: return "MatchBegin";
        case Msg::Snapshot: return "Snapshot";
        case Msg::Spawn: return "Spawn";
        case Msg::Damage: return "Damage";
        case Msg::Kill: return "Kill";
        case Msg::ShotFx: return "ShotFx";
        case Msg::GrenadeFx: return "GrenadeFx";
        case Msg::RoundStart: return "RoundStart";
        case Msg::RoundEnd: return "RoundEnd";
        case Msg::Score: return "Score";
        case Msg::MatchOver: return "MatchOver";
        case Msg::Inventory: return "Inventory";
        case Msg::ShopResult: return "ShopResult";
        case Msg::ClanStateMsg: return "ClanState";
        case Msg::ClanInvited: return "ClanInvited";
        case Msg::RadioFx: return "RadioFx";
        case Msg::ServerInfo: return "ServerInfo";
        case Msg::ManifestPart: return "ManifestPart";
        case Msg::ContentChunk: return "ContentChunk";
        case Msg::MatchRewards: return "MatchRewards";
        case Msg::GamesState: return "GamesState";
        case Msg::Join: return "Join";
        case Msg::Ready: return "Ready";
        case Msg::Refresh: return "Refresh";
        case Msg::ContentRequest: return "ContentRequest";
        case Msg::ChannelEdit: return "ChannelEdit";
        case Msg::MotdEdit: return "MotdEdit";
        case Msg::GamesEdit: return "GamesEdit";
    }
    return "?";
}

}  // namespace lsf::proto
