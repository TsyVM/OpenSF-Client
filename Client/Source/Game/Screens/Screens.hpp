// The front end's screens, one function each, drawn every frame by App::draw_screen, and what
// they remember between frames. Layouts follow the lobby's own page scripts (Docs/Research.md
// §1.1) on the 1600x900 stage.
#pragma once

#include "Game/App.hpp"
#include "Game/Protocol.hpp"

#include <array>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lsf {

namespace ui {
class Page;
struct Line;
}

struct ScreenState {
    // Servers and signing in.
    int server_sel = 0;
    bool login_open = false;
    bool registering = false;
    bool signing_in = false;
    int sign_in_toast = 0;
    std::string account, password, password2;
    std::string login_error;
    // Joining a server (5.1): on its way, its toast, a private server's password.
    bool joining = false;
    double join_started = 0;
    int join_toast = 0;
    bool server_recent = false;      // only the servers joined lately
    bool sign_in_dismissed = false;  // the sign-in window opened by itself once already
    bool server_password_open = false;
    std::string server_password;
    int server_tab = 0;        // All, Official, Community, Favourites, Recent (8.4)
    bool hide_full = false, hide_locked = false, hide_gated = false, hide_downloads = false;
    // A staff reset's temporary password must be changed (ID-12).
    bool change_password_open = false;
    std::string new_password, new_password2;
    // The first sign-in.
    std::string code_name;
    int force_sel = 0;
    // Channels.
    int channel_sel = 0;
    // Lobby.
    int room_sel = -1;         // room id
    int room_filter = 0;       // all, waiting, playing
    int user_tab = 0;          // all, waiting, room info
    bool create_open = false;
    RoomSettings create;
    bool create_hour_set = false;   // the host chose the hour (it no longer follows the map's bake)
    bool password_open = false;
    u16 password_room = 0;
    std::string join_password;
    // Chat (lobby and room).
    std::string chat_line;
    int chat_scope = 0;
    // Room.
    bool loadout_open = false;
    // Shop.
    int shop_tab = 0;          // 0 weapons, 1 characters, 2 inventory, 3 the Item Shop, 4 the capsule machine
    int shop_cat = 0;          // the weapon shop's tab: rifle, snipe, machine gun, secondary, melee, throwing
    int shop_sel = -1;         // the card picked: a weapon id, or a force id in the character pages
    int char_tab = 0;          // the character shop's tab: character, head, face, ... accessory
    int inv_tab = 1;           // the inventory's page: 0 characters, 1 weapons, 2 items, 3 gifts, 4 sprays
    int inv_sub = 0;           // its sub-tab
    int view_force = -1;       // the soldier in a shop's preview (-1: your own)
    int emblem_first = 0;      // the first force the emblem strip shows
    std::optional<std::pair<u8, Loadout>> inv_snapshot;   // the kit as the inventory found it, for Return
    float model_turn = 0;      // how far the shown soldier has been dragged round (degrees)
    std::string shop_search;
    // Items (Game/Items.hpp): the one being bought (its days picked) and the one being used.
    int item_sel = -1;         // the item card picked (Item Shop, the inventory's items and parts)
    int buy_item = -1;
    int buy_kind = 2;          // what buy_item is (ShopKind): the dialog sells all four kinds
    int buy_offer = 0;
    // Bought for a friend instead (gametext 498 "Send gift"): the dialog's switch and the friend picked.
    bool buy_gift = false;
    std::string buy_friend;
    int item_tab = 0;          // the Item Shop's grid: 0 game items, 1 sprays
    // Mending a gun or selling something back (Game/Wear.hpp): the thing asked about (-1: none),
    // what it is (ShopKind) and which of the two (0 mend, 1 sell).
    int deal_item = -1;
    int deal_kind = 0;
    int deal_op = 0;
    // The ID card (Screens/IdCard.cpp): whose is up, its tab (0 record, 1 equipment, 2 weapons),
    // and your own line while it is being written.
    bool card_open = false;
    std::string card_name;
    int card_tab = 0;
    bool card_editing = false;
    std::string card_message;
    // The capsule machine (Screens/Capsule.cpp): the capsule picked (its id), the grid's page, how
    // many coins Buy Coins buys.
    int capsule_sel = -1;
    int capsule_page = 0;
    int coin_buy = 1;
    // Your own recordings (Screens/Recordings.cpp, PageReplay): the match picked, its memo being
    // written, the list asked for.
    int replay_sel = -1;
    bool replay_memo_editing = false;
    std::string replay_memo;
    bool replays_asked = false;
    bool record_match = false;       // the room's Record plate: every match finished is kept on this PC
    int use_item = -1;
    int use_value = 0;
    std::string use_text;
    Screen shop_return = Screen::Channels;
    // Settings.
    bool settings_open = false;
    int settings_tab = 0;      // system, controls, macro
    int system_sub = 0;        // system: display, graphics, sound, game
    int settings_sub = 0;      // controls: the keys, the controller, the radio
    int binding = -1;          // the Action whose key the next press sets, -1 none
    int pad_binding = -1;      // ... whose controller button the next press sets
    double binding_since = 0;
    Settings edit;
    int resolution_asked = -1; // the aspect the resolution list was last made for (-1: not yet)
    int fidelity_taps = 0;     // see settings_modal
    bool credits_open = false;
    bool touch_arrange = false; // the options' arranging screen for the touch controls is up (in their place)
    int touch_picked = 0;       // the control picked there (TouchPart)
    double credits_since = 0;
    float credits_scroll = 0;  // page units rolled past
    bool credits_by_hand = false;
    bool restart_ask = false;  // a new renderer was confirmed: restart now, or later
    // Clans.
    bool clan_open = false;
    int clan_layer = 0;              // the mark builder's layer: background, frame, symbol
    ClanMark clan_mark{1, 0, 1};     // the mark being built
    // The emblem maker (Screens/Clan.cpp): the emblem being made, the shape in hand, and what
    // the pointer is doing to it on the canvas.
    bool emblem_open = false;
    bool emblem_for_clan = false;    // an Owner or Co-Owner changing the clan's (else: founding one)
    ClanMark emblem;
    int emblem_sel = -1;
    int emblem_drag = 0;             // 0 nothing, 1 moving, 2 sizing, 3 turning
    float emblem_grab_x = 0, emblem_grab_y = 0;   // moving: the shape's middle from the pointer
    float emblem_canvas[4]{};        // where the canvas was last drawn, in pixels (x0 y0 x1 y1)
    int emblem_palette = 0;          // 0 shapes, 1 pictures
    std::vector<std::vector<EmblemLayer>> emblem_undo, emblem_redo;
    bool emblem_editing = false;     // a change is under way (a drag, a slider held): one step to undo
    std::string emblem_hex;          // the colour code being typed
    u32 emblem_hex_rgb = 0;          // the colour it was written for
    std::string clan_name, clan_notice;
    std::string clan_member_sel;     // the member picked, by code name
    std::string clan_invite_name;    // an officer's "ask in" box
    // The staff panel (F9, Screens/Staff.cpp): reports, accounts, match recordings.
    bool staff_open = false;
    int staff_tab = 0;               // 0 reports, 1 accounts, 2 recordings, 3 events, 4 rewards, 5 shop, 6 channels, 7 servers
    // The Accounts tab's reach (RL-1): this server's own data, or Team Vanilla's (SFLegacy Staff:
    // every server, from the game to TVAS).
    bool staff_global = false;
    std::string staff_checked;       // ID-12: what was checked before a password reset
    // The Channels tab (§10.2): the channel picked (its id; 0 a new one), the copy being edited,
    // and the message of the day being written.
    int staff_channel = -1;
    proto::ChannelInfo staff_channel_edit;
    std::string staff_channel_maps;  // its maps as typed: "crossroad, x-mypack-harbour"
    bool staff_channel_loaded = false;
    std::string staff_motd;
    bool staff_motd_loaded = false;
    // The Games tab (the server's Game Masters): the switches being changed, a map looked for.
    lsf::ServerGames staff_games;
    bool staff_games_loaded = false;
    std::string staff_games_find;
    std::vector<std::string> staff_games_maps;   // every map, read when the tab opens
    // The Servers tab (SFLegacy Staff): the server picked, its tier being set, a line to broadcast.
    u64 staff_server_sel = 0;
    int staff_server_tier = 0;
    std::string staff_server_text;
    bool staff_open_only = true;
    int staff_report_sel = -1;       // report id
    int staff_recording_sel = -1;    // match id
    std::string staff_search, staff_reason, staff_value;
    int staff_mute = 1, staff_ban = 1, staff_field = 1, staff_role = 0;
    bool staff_asked = false;        // the open tab's list has been asked for
    // The panel's Events and Rewards (Screens/EventsEditor.cpp): the copy being edited, and where.
    std::optional<RewardsConfig> staff_cfg;
    // The panel's Shop (Screens/ShopEditor.cpp): a Game Master's copy of the shop, its page (0 the
    // catalog, 1 coins and gifts, 2 the capsules, 3 the line along the bottom, 4 Duffle Bags), the
    // catalog's kind and the thing picked, the capsule picked, the catalog's search.
    std::optional<ShopConfig> shop_cfg;
    bool shop_dirty = false;
    int shop_sub = 0, shop_kind = 0, shop_ware = -1, shop_capsule = 0;
    int shop_bag = 0;          // the Duffle Bag picked on the staff Shop's Duffle Bags page
    std::string shop_find;
    bool staff_cfg_dirty = false, staff_cfg_asked = false;
    int staff_event_sel = 0, staff_template = 1, staff_box_sel = 0, staff_rewards_sub = 0, staff_quest_page = 0;
    double staff_delete_armed = -10;
    // Reporting a soldier, and calling a vote on one (from the match's Esc menu).
    bool report_open = false;
    u32 report_player = 0;
    std::string report_name, report_note;
    int report_reason = 0;
    bool vote_open = false;
    u32 vote_target = 0;
    // Rewards (Screens/Rewards.cpp): the dialog and its tab; the inventory's Gift tab and a box's reveal.
    bool rewards_open = false;
    int rewards_tab = 0;             // 0 today, 1 quests, 2 events
    int gift_sel = -1;               // the Gift tab's pick: a box kind, or kGiftPick + a friend's gift's id
    double box_opened_at = -1;
    // Friends and messages (Screens/Social.cpp): the dialog, its tab, the friend picked, the
    // soldier being asked, the whisper being written, the message picked, a Remove or Block asked twice.
    bool social_open = false;
    int social_tab = 0;              // 0 friends, 1 requests, 2 blocked, 3 inbox, 4 Team Vanilla's own mail
    std::string friend_sel, friend_add, whisper_to, whisper_text;
    u32 mail_sel = 0;
    double social_armed = -10;
    std::string social_armed_for;
    int social_armed_what = 0;       // 1 remove, 2 block, 3 report a message
    // The menu a soldier's name opens (a right click in the lobby's list or on a room's seat):
    // whose it is, and that it was asked for this frame.
    u32 menu_id = 0;
    std::string menu_name, menu_clan;
    bool menu_asked = false;
    // Inviting soldiers to your room (the room's Invite plate): the dialog, the soldier picked,
    // a code name typed, and whom you asked a moment ago (shown as asked for a while).
    bool invite_open = false;
    std::string invite_sel, invite_name;
    std::map<std::string, double> invited_at;
    // Clans: the dialog's tab (0 members, 1 applications, 2 log, 3 the clans, 4 found one), the
    // notice being edited, the clan picked in the list and when the list was last asked for, and
    // what was asked once and must be asked again within three seconds to be done.
    int clan_tab = 0;
    bool clan_notice_editing = false;
    std::string clan_notice_edit;
    std::string clan_browse_sel;     // a clan's name
    double clan_browse_at = -100;
    double clan_armed = -10;
    int clan_armed_what = 0;         // 1 hand over, 2 disband, 3 remove, 4 leave
    std::string clan_armed_for;      // the member it is about
    // Results.
    double result_since = 0;
    // A confirmation dialog.
    bool confirm_quit = false;
};

// Chrome every signed-in screen shares: the SF mark, the player card and the nav plates across
// the top; the marquee, where you are and the ping along the bottom. Returns what was clicked.
enum class Nav { None, CharShop, WeaponShop, ItemShop, Inventory, Recordings, Clan, Options, Exit };
Nav top_bar(App& app, bool allow_shop = true);
// The top bar's slot for a screen's back plate (out_1, 59x44): between Options and the card.
inline constexpr float kBackPlateX = 1012, kBackPlateY = 21;
void bottom_bar(App& app, const char* where);
void backdrop(App& app, float darken = 0.35f);
// A chat panel with its scope combo and input line; `room` picks which log it shows.
void chat_panel(App& app, float x, float y, float w, float h, bool room);
void handle_nav(App& app, Nav nav);
// The options ("USER SETTING", Screens/Options.cpp), and the credits it opens (Screens/Credits.cpp).
void open_settings(App& app, int tab = 0, int sub = 0);
void settings_modal(App& app);
void credits_modal(App& app);
// The touch controls' arranging screen (Options, Controls, Touch, Arrange), up in the options' place.
void touch_arrange_modal(App& app);
// How many names stand under one of the credits' headings (tests).
int credits_names(const char* heading);
void restart_modal(App& app);
// Friends, messages and global chat (Screens/Social.cpp): how a chat line reads by its scope; a
// line sent from any chat box (its /w /r /c /g /t /a commands, else `plain`); "3 h ago"; what waits
// (requests and unread messages); the Friends dialog; the lobby's Friend tab in the list `list_id`
// of `page`; the header's Friends plate with its count.
ui::Line chat_view(const proto::ChatLine& line);
bool send_chat(App& app, const std::string& line, proto::ChatScope plain);
std::string ago(App& app, u64 unix_time);
int social_waiting(const Session& s);
void open_social(App& app, int tab = -1, const std::string& whisper_to = {});
void social_modal(App& app);
void friend_panel(App& app, const ui::Page& page, int list_id);
bool friends_plate(App& app, int key, float x0, float y0, float x1, float y1);
// Who sees where you are and who may write to you (D25, ML-7), kept with your account at TVAS: on
// one row, or `stacked` on two (Options). Returns the y under them.
float privacy_rows(App& app, float x0, float y, float x1, bool stacked = false);
// The menu a soldier's name opens: whisper, befriend, ask into the clan, block, report. A page
// asks for it where a name is right-clicked and draws it once, before its page_end().
void open_soldier_menu(App& app, u32 session_id, const std::string& name, const std::string& clan);
void soldier_menu(App& app);
// A soldier's ID card (Screens/IdCard.cpp): asked of the server by code name, yours or anyone's.
void open_id_card(App& app, const std::string& code_name);
void id_card_modal(App& app);
// A room's invitations: the room's Invite dialog (friends who are on and the lobby's soldiers, to
// pick from, or a code name), and the invitation another soldier's room sends you.
void room_invite_dialog(App& app);
void room_invited_modal(App& app);
// The lobby's page scripts by name ("PageWPShop"), loaded the first time (Screens/Pages.cpp).
const ui::Page& lobby_page(App& app, const char* name);
// pagecommon: the background, the mark, the shop / inventory / option plates (`current_nav` the
// one lit: 0 character shop, 1 weapon shop, 3 inventory), help and the marquee.
Nav common_chrome(App& app, int current_nav = -1);
// The bottom strip: where you are (green, left), the ping (right).
void bottom_strip(App& app, std::string_view where);
// The dialogs the page screens (Screens/Pages.cpp) open.
void sign_in_modal(App& app);
void change_password_modal(App& app);
void join_modal(App& app);
void server_password_modal(App& app);
void switch_modal(App& app);
// What a game type asks of you (Make Room's MISSION box, the waiting room's), nullptr for Team
// Battle, whose map says it.
const char* mode_brief(Mode m);
void make_room_modal(App& app);
void password_modal(App& app);
// Clans (Screens/Clan.cpp): the Clan Lobby plate's dialog, an officer's invitation, and a clan's
// mark (its three pieces, over each other) on the page grid.
void clan_modal(App& app);
// The emblem maker, opened from the clan's dialog (which comes back when it closes).
void open_emblem_maker(App& app, const ClanMark& from, bool for_clan);
void emblem_modal(App& app);
void clan_invite_modal(App& app);
void clan_mark(App& app, const ClanMark& mark, float x0, float y0, float x1, float y1);
// Rewards and events (Screens/Rewards.cpp): the Rewards plate's dialog (`tab` -1: as last left),
// whether anything waits to be collected (the plate's dot), the running events' banners (the
// marquee), and a box opened in the Gift tab.
void open_rewards(App& app, int tab = -1);
void rewards_modal(App& app);
bool rewards_waiting(const Session& s);
std::string events_banner(const Session& s);
void box_opened_modal(App& app);
// The staff panel's Events and Rewards tabs (Screens/EventsEditor.cpp): a Game Master's editor.
void staff_events_tab(App& app, float x0, float y0, float x1, float y1);
void staff_rewards_tab(App& app, float x0, float y0, float x1, float y1);
// ... and its Shop (Screens/ShopEditor.cpp): the catalog (on sale or not, for how long, for how
// much), the coins' price and the gift discount, the capsules, the line along the bottom, Duffle Bags.
void staff_shop_tab(App& app, float x0, float y0, float x1, float y1);
// The line that pans along the bottom of the lobby's pages: the running events' banners, then the
// shop's line (a Game Master's, Game/Shop.hpp).
std::string marquee_text(const Session& s);
// A page's chat box: its log, scope combo and line (Screens/Pages.cpp), on any page that has one.
void page_chat(App& app, const ui::Page& page, int log_id, int edit_id, int combo_id, bool room);
// What is left of something rented: "6 days left", "3 hours left", "For good" (kForGood).
std::string time_left(u32 seconds);
// A spray's picture: the lobby's can, else the spray itself (the effect archive's).
ui::Picture spray_picture(App& app, const SprayDef& s);
// The capsule machine (Screens/Capsule.cpp, the client's PageLottoShop): the Shop screen's tab 4.
void capsule_shop(App& app);
// A turn of the capsule picked, played out on the page (the machine shakes, the capsule opens).
// False when it cannot be turned (no coins, out of the machine, a turn still out).
bool capsule_turn(App& app);
// Your own matches' recordings (Screens/Recordings.cpp, PageReplay): the list, Play, a memo.
void draw_recordings(App& app);
// Every frame: a recording being downloaded for this PC, kept when it is in; the Record plate's.
void recordings_tick(App& app);
// Play's: a recording of yours watched (this PC's copy, else downloaded, kept and then watched).
// False when there is no such match in the list. And how many matches the list holds, kept here.
bool play_recording(App& app, u32 match);
int recordings_listed(App& app, int* kept_here = nullptr);
// Staff, reports and votes (Screens/Staff.cpp). The soldiers a report or a vote can pick from
// are the match's or the room's (session ids, never account names).
struct Pickable {
    u32 id = 0;
    std::string name;
    bool votable = false;
    std::string why_not;   // shown greyed beside a soldier who cannot be voted on
};
std::vector<Pickable> pickable_players(App& app);
void staff_modal(App& app);
void report_modal(App& app);
void vote_call_modal(App& app);
// The vote running on your side: a banner with Yes / No (F1 / F2), anywhere in a room or match.
void vote_banner(App& app);

void draw_boot(App& app);
void draw_servers(App& app);
void draw_code_name(App& app);
void draw_channels(App& app);
void draw_lobby(App& app);
void draw_room(App& app);
void draw_shop(App& app);
void draw_loading(App& app);
void draw_match(App& app);
void draw_result(App& app);
void draw_replay(App& app);

}  // namespace lsf
