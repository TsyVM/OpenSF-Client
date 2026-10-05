#include "Game/Ui/Kit.hpp"

#include "Game/Ui/Ui.hpp"

#include <vangui/misc/vangui_icons.h>
#include <vangui/misc/vangui_vector.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <unordered_map>

namespace lsf::ui {

namespace {

bool g_sharp = true;

// Measured off sourceNewAlpha03 (October 2026): a plate runs (48, 46, 31) to (57, 55, 37) top to
// bottom in a one-pixel frame (98, 100, 94); its word is (202, 202, 198), or the kit's lime
// (217, 246, 94) when lit. The sub tabs' plates are a shade lighter, the small plates flat.
constexpr VanU32 kPlateTop = VAN_COL32(48, 46, 31, 255), kPlateBottom = VAN_COL32(57, 55, 37, 255);
constexpr VanU32 kTabTop = VAN_COL32(59, 55, 38, 255), kTabBottom = VAN_COL32(68, 65, 45, 255);
constexpr VanU32 kSmallPlate = VAN_COL32(56, 54, 38, 255);
constexpr VanU32 kFrame = VAN_COL32(98, 100, 94, 255), kFrameHover = VAN_COL32(222, 222, 214, 255), kFrameLit = VAN_COL32(202, 228, 80, 255);
constexpr VanU32 kInk = VAN_COL32(202, 202, 198, 255), kInkHover = VAN_COL32(244, 244, 240, 255), kInkLit = VAN_COL32(217, 246, 94, 255);
constexpr VanU32 kOrange = VAN_COL32(232, 150, 30, 255), kOrangeHover = VAN_COL32(246, 196, 40, 255), kOrangeLit = VAN_COL32(250, 226, 60, 255);

enum class Kind : unsigned char {
    Nav,       // the top row's plates: four states (resting, hover, chosen, chosen and hovered)
    Button,    // three states: resting, hover, pressed
    Tab,       // two states: resting, chosen
    SubTab,    // the same, on the lighter plate
    Small,     // Buy / Sell / Use: resting, lit word, lit word and frame
    Toggle,    // two words, one a state (Accept / Block invites)
    Sound,     // the lobby's sound switch: a lamp and a word
    Back,      // the big return arrow
    Power,     // the big exit button
    Help,
    Close,     // a dialog's X
    Up, Down,  // a list's scroll arrows
    Left, Right,   // a selector's arrows
    Radio,
    Check,
    Thumb,     // a slider's
    Heading,   // a section's bead and word
    Title,     // a dialog's title
    State,     // READY / INVEN / PLAY / LOAD / WAIT beside a soldier's name
};

struct Spec {
    Kind kind;
    const char* label;
    const char* label2 = nullptr;   // a toggle's other word
    int icon = VanGui::VanIcon_None;
    VanU32 colour = 0;              // a heading's bead, a state's ink
};

const std::unordered_map<std::string_view, Spec>& specs() {
    using namespace VanGui;
    static const std::unordered_map<std::string_view, Spec> table = {
        // ── The nav row ──
        {"char_shop_1", {Kind::Nav, "Character\nShop"}},
        {"arms_shop_1", {Kind::Nav, "Weapon\nShop"}},
        {"item_shop_1", {Kind::Nav, "Item\nShop"}},
        {"inventory_1", {Kind::Nav, "Inventory"}},
        {"replay_1", {Kind::Nav, "Replay"}},
        {"replay_2", {Kind::Nav, "Replay"}},
        {"clan_wait_room_1", {Kind::Nav, "Clan Lobby"}},
        {"environment_1", {Kind::Nav, "Options"}},
        // ── Buttons, with the kit's little pictures as icons ──
        {"start_1", {Kind::Button, "Start", nullptr, VanIcon_Play}},
        {"invite_1", {Kind::Button, "Invite", nullptr, VanIcon_PersonPlus}},
        {"kickout_1", {Kind::Button, "Kick", nullptr, VanIcon_Block}},
        {"record_replay_1", {Kind::Button, "Save", nullptr, VanIcon_Save}},
        {"roominven_1", {Kind::Button, "Invent.", nullptr, VanIcon_Backpack}},
        {"ready_1", {Kind::Button, "Ready", nullptr, VanIcon_Ready}},
        {"join_1", {Kind::Button, "Enter", nullptr, VanIcon_DoorIn}},
        {"entrance_1", {Kind::Button, "Enter", nullptr, VanIcon_DoorIn}},
        {"update_1", {Kind::Button, "Refresh", nullptr, VanIcon_Refresh}},
        {"quickjoin_1", {Kind::Button, "Quick\nJoin", nullptr, VanIcon_Send}},
        {"play_replay_1", {Kind::Button, "Play", nullptr, VanIcon_Play}},
        {"del_replay_1", {Kind::Button, "Delete", nullptr, VanIcon_Cross}},
        {"ladder_confirm_1", {Kind::Button, "Request"}},
        {"shop_buy_1", {Kind::Button, "Buy", nullptr, VanIcon_Cart}},
        {"inv_reset_1", {Kind::Button, "Return", nullptr, VanIcon_Undo}},
        {"make_room_1", {Kind::Button, "New\nRoom", nullptr, VanIcon_HousePlus}},
        {"custom_join_1", {Kind::Button, "Customized\nEnter", nullptr, VanIcon_Lock}},
        {"spectate_join_1", {Kind::Button, "Observer", nullptr, VanIcon_Spectate}},
        {"ladder_application_1", {Kind::Button, "Request\nLadder", nullptr, VanIcon_Flag}},
        {"research_1", {Kind::Button, "Search", nullptr, VanIcon_Search}},
        {"repair_inRoom_1", {Kind::Button, "Repair", nullptr, VanIcon_Wrench}},
        {"recharge_1", {Kind::Button, "Recharge"}},
        {"close_1", {Kind::Button, "Close"}},
        {"baseValue_1", {Kind::Button, "Default"}},
        {"confirm_1", {Kind::Button, "Confirm"}},
        {"cancel_1", {Kind::Button, "Cancel"}},
        {"s_confirm_1", {Kind::Button, "Confirm"}},
        {"invite_ok_1", {Kind::Button, "Invite"}},
        {"invite_cancel_1", {Kind::Button, "Cancel"}},
        {"yes_1", {Kind::Button, "Yes"}},
        {"no_1", {Kind::Button, "No"}},
        {"newbie_check_btn_1", {Kind::Button, "Duplicates"}},
        {"newbie_gameend_btn_1", {Kind::Button, "Exit"}},
        {"btn_replay_memo_modification", {Kind::Button, "Change"}},
        {"btn_messenger_invite", {Kind::Button, "Invite"}},
        {"btn_messenger", {Kind::Button, "Messenger"}},
        {"invite_clan", {Kind::Button, "Invite"}},
        {"invite_clan_off", {Kind::Button, "Delete"}},
        {"addfriend", {Kind::Button, "Invite"}},
        {"deletefriend", {Kind::Button, "Delete"}},
        {"btn_list_refresh", {Kind::Button, "Refresh", nullptr, VanIcon_Refresh}},
        {"btn_download", {Kind::Button, "Download", nullptr, VanIcon_Download}},
        {"capsule_coin_charge_btn", {Kind::Button, "Buy Coins"}},
        // ── Tabs ──
        {"tab_character_1", {Kind::Tab, "Character"}},
        {"tab_weapon_1", {Kind::Tab, "Weapon"}},
        {"tab_event_1", {Kind::Tab, "Event"}},
        {"tab_gift_1", {Kind::Tab, "Gift"}},
        {"tab_item_1", {Kind::Tab, "Game Item"}},
        {"tab_spray_1", {Kind::Tab, "Spray item"}},
        {"capsule_weapon_tap", {Kind::Tab, "Weapon"}},
        {"capsule_equip_tap", {Kind::Tab, "Gear"}},
        {"tab_weaponbomb_1", {Kind::Tab, "Bombs"}},
        {"tab_change_item_1", {Kind::Tab, "Transform\nCharacter"}},
        {"tab_recent_file", {Kind::Tab, "Recent File"}},
        {"tab_replay_sfleague", {Kind::Tab, "SF League"}},
        {"tab_replay_clan", {Kind::SubTab, "Clan"}},
        {"tab_replay_etc", {Kind::SubTab, "Other"}},
        {"tab_messenger_clanlist", {Kind::SubTab, "Clan\nMember"}},
        {"tab_messenger_friendlist", {Kind::SubTab, "Friend"}},
        {"system", {Kind::Tab, "System"}},
        {"controls", {Kind::Tab, "Controls"}},
        {"macro", {Kind::Tab, "Macro"}},
        {"method_key", {Kind::Tab, "Basic\nControls"}},
        {"method_radio", {Kind::Tab, "Radio\nMessage"}},
        {"idcard_tab_total", {Kind::Tab, "Info"}},
        {"idcard_tab_equip", {Kind::Tab, "Equip"}},
        {"idcard_tab_weapon", {Kind::Tab, "Weapon"}},
        {"tab_sub_char_1", {Kind::SubTab, "Character"}},
        {"tab_sub_head_1", {Kind::SubTab, "Head"}},
        {"tab_sub_face_1", {Kind::SubTab, "Face"}},
        {"tab_sub_chest_1", {Kind::SubTab, "Torso"}},
        {"tab_sub_arm_1", {Kind::SubTab, "Arms"}},
        {"tab_sub_leg_1", {Kind::SubTab, "Legs"}},
        {"tab_sub_foot_1", {Kind::SubTab, "Feet"}},
        {"tab_sub_accessory_1", {Kind::SubTab, "Accessory"}},
        {"tab_sub_event_1", {Kind::SubTab, "Event"}},
        {"tab_sub_primary_1", {Kind::SubTab, "Primary-\nRifle"}},
        {"tab_sub_sniper_1", {Kind::SubTab, "Primary-\nSnipe"}},
        {"tab_sub_mg_1", {Kind::SubTab, "Primary-\nMachine Gun"}},
        {"tab_sub_secondary_1", {Kind::SubTab, "Secondary"}},
        {"tab_sub_throw_1", {Kind::SubTab, "Throwing"}},
        {"tab_sub_knife_1", {Kind::SubTab, "Melee"}},
        {"tab_sub_primary_only_1", {Kind::SubTab, "Primary"}},
        {"userall_1", {Kind::SubTab, "All"}},
        {"servertab_all_1", {Kind::SubTab, "All"}},
        {"userwait_1", {Kind::SubTab, "Standby"}},
        {"roominfo_1", {Kind::SubTab, "Info."}},
        {"friend_on_off_1", {Kind::SubTab, "Friend"}},
        {"servertab_normal_1", {Kind::SubTab, "Normal"}},
        {"servertab_level_1", {Kind::SubTab, "Rank"}},
        {"servertab_clan_1", {Kind::SubTab, "Clan\nLobby"}},
        {"servertab_convention_1", {Kind::SubTab, "Tourney"}},
        {"servertab_biscuit_1", {Kind::SubTab, "PC Caf\xC3\xA9"}},
        {"servertab_ladder_1", {Kind::SubTab, "Ladder"}},
        {"chattab_normal_1", {Kind::SubTab, "All"}},
        {"chattab_custom_1", {Kind::SubTab, "Player1"}},
        {"chattab_custom2_1", {Kind::SubTab, "Player2"}},
        // ── The cards' small plates ──
        {"buy_1", {Kind::Small, "Buy"}},
        {"gift_1", {Kind::Small, "Gift"}},
        {"sell_1", {Kind::Small, "Sell"}},
        {"enchant_btn_s_resell", {Kind::Small, "Sell"}},
        {"using_1", {Kind::Small, "Use"}},
        {"repair_1", {Kind::Small, "Repair"}},
        {"enchant_btn_s_repair", {Kind::Small, "Repair"}},
        {"view_1", {Kind::Small, "View"}},
        {"enchant_btn_s_details", {Kind::Small, "View"}},
        // ── Switches ──
        {"no_invitation_1", {Kind::Toggle, "Accept\nInvites", "Block\nInvites"}},
        {"sound_on_off_1", {Kind::Sound, "SOUND"}},
        // ── Fixed controls ──
        {"out_1", {Kind::Back, ""}},
        {"exit_1", {Kind::Power, ""}},
        {"help_1", {Kind::Help, "Help"}},
        {"X_1", {Kind::Close, ""}},
        {"scroll_up_button_1", {Kind::Up, ""}},
        {"scroll_down_button_1", {Kind::Down, ""}},
        {"m_left_1", {Kind::Left, ""}},
        {"m_right_1", {Kind::Right, ""}},
        // The room's and the shops' green arrows.
        {"g_left_1", {Kind::Left, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"g_right_1", {Kind::Right, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"s_left_1", {Kind::Left, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"s_right_1", {Kind::Right, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"gs_left_1", {Kind::Left, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"gs_right_1", {Kind::Right, "", nullptr, VanIcon_None, VAN_COL32(98, 118, 50, 255)}},
        {"radio_btn_1", {Kind::Radio, ""}},
        {"RadioButton", {Kind::Radio, ""}},
        {"checkbox", {Kind::Check, ""}},
        {"trackbar", {Kind::Thumb, ""}},
        // ── Words ──
        {"title_option", {Kind::Title, "USER SETTING"}},
        {"clan_notice_title", {Kind::Title, "Notice"}},
        {"graphic_text", {Kind::Heading, "GRAPHICS", nullptr, VanIcon_None, VAN_COL32(112, 196, 60, 255)}},
        {"advanced_text", {Kind::Heading, "ADVANCED", nullptr, VanIcon_None, VAN_COL32(232, 150, 30, 255)}},
        {"sound_text", {Kind::Heading, "SOUND", nullptr, VanIcon_None, VAN_COL32(214, 60, 190, 255)}},
        {"weapon_text", {Kind::Heading, "WEAPON", nullptr, VanIcon_None, VAN_COL32(232, 150, 30, 255)}},
        {"view_text", {Kind::Heading, "VIEW", nullptr, VanIcon_None, VAN_COL32(214, 60, 190, 255)}},
        {"move_text", {Kind::Heading, "MOVE", nullptr, VanIcon_None, VAN_COL32(112, 196, 60, 255)}},
        {"macro_text", {Kind::Heading, "MACRO CHAT", nullptr, VanIcon_None, VAN_COL32(112, 196, 60, 255)}},
        {"state_ready_1", {Kind::State, "READY", nullptr, VanIcon_None, VAN_COL32(255, 122, 84, 255)}},
        {"state_inven_1", {Kind::State, "INVEN", nullptr, VanIcon_None, VAN_COL32(207, 231, 85, 255)}},
        {"state_play_1", {Kind::State, "PLAY", nullptr, VanIcon_None, VAN_COL32(142, 192, 239, 255)}},
        {"state_load_1", {Kind::State, "LOAD", nullptr, VanIcon_None, VAN_COL32(232, 190, 110, 255)}},
        {"state_wait_1", {Kind::State, "WAIT", nullptr, VanIcon_None, VAN_COL32(164, 204, 170, 255)}},
    };
    return table;
}

VanU32 faded(VanU32 c, float alpha) {
    const VanU32 a = VanU32(float((c >> VAN_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
    return (c & ~VAN_COL32_A_MASK) | (a << VAN_COL32_A_SHIFT);
}

// One UTF-8 character's length in bytes.
int char_bytes(unsigned char lead) { return lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1; }

// Lettering sizes below are in page units of the letters' own em, as the kit's were measured
// (11.5 on a plate 41 high). The interface draws a face by its line height, and Malgun Gothic's
// line is a third taller than its em; the caps' middle sits 0.547 of the line down from its top.
constexpr float kLine = 1.33f, kCapMid = 0.547f, kPitch = 0.80f;

float tracking(float size_px) { return size_px * 0.042f; }

float line_width(VanFont* f, float size, std::string_view s) {
    float w = 0;
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        const int len = std::min<int>(char_bytes(static_cast<unsigned char>(s[i])), int(s.size() - i));
        w += f->CalcTextSizeA(size, FLT_MAX, 0.0f, s.data() + i, s.data() + i + len).x;
        i += size_t(len);
        ++n;
    }
    return w + tracking(size) * float(std::max(0, n - 1));
}

// One line from its left edge: the kit's dark outline under every letter first, then the letters.
void draw_line(VanDrawList* dl, VanFont* f, float size, float x, float y, std::string_view s, VanU32 ink) {
    const float o = std::max(1.0f, std::floor(size / 13.0f + 0.5f));
    const VanU32 dark = faded(VAN_COL32(0, 0, 0, 150), float((ink >> VAN_COL32_A_SHIFT) & 0xFF) / 255.0f);
    const float track = tracking(size);
    for (int pass = 0; pass < 2; ++pass) {
        float cx = std::floor(x);
        for (size_t i = 0; i < s.size();) {
            const int len = std::min<int>(char_bytes(static_cast<unsigned char>(s[i])), int(s.size() - i));
            const char* p = s.data() + i;
            if (pass == 0) {
                for (const VanVec2 d : {VanVec2(-o, 0), VanVec2(o, 0), VanVec2(0, -o), VanVec2(0, o), VanVec2(o, o)})
                    dl->AddText(f, size, {cx + d.x, std::floor(y) + d.y}, dark, p, p + len);
            } else {
                dl->AddText(f, size, {cx, std::floor(y)}, ink, p, p + len);
            }
            cx += f->CalcTextSizeA(size, FLT_MAX, 0.0f, p, p + len).x + track;
            i += size_t(len);
        }
    }
}

// The plate: its gradient in its frame, `w` pixels thick.
void plate(VanDrawList* dl, VanVec2 a, VanVec2 b, VanU32 top, VanU32 bottom, VanU32 frame, float w, float alpha) {
    a = {std::floor(a.x), std::floor(a.y)};
    b = {std::floor(b.x), std::floor(b.y)};
    dl->AddRectFilledMultiColor(a, b, faded(top, alpha), faded(top, alpha), faded(bottom, alpha), faded(bottom, alpha));
    const float h = w * 0.5f;
    dl->AddRect({a.x + h, a.y + h}, {b.x - h, b.y - h}, faded(frame, alpha), 0, 0, w);
}

float line_px(float unit) { return std::max(1.0f, std::floor(unit + 0.25f)); }

// A filled triangle pointing along (dx, dy), in a rect.
void triangle(VanDrawList* dl, const VanVec2& a, const VanVec2& b, int dx, int dy, VanU32 col) {
    const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    const float hw = (b.x - a.x) * 0.5f, hh = (b.y - a.y) * 0.5f;
    VanVec2 p[3];
    if (dx != 0) {
        p[0] = {mx + float(dx) * hw * 0.8f, my};
        p[1] = {mx - float(dx) * hw * 0.8f, my - hh * 0.8f};
        p[2] = {mx - float(dx) * hw * 0.8f, my + hh * 0.8f};
    } else {
        p[0] = {mx, my + float(dy) * hh * 0.62f};
        p[1] = {mx - hw * 0.72f, my - float(dy) * hh * 0.5f};
        p[2] = {mx + hw * 0.72f, my - float(dy) * hh * 0.5f};
    }
    dl->AddTriangleFilled(p[0], p[1], p[2], col);
    dl->AddTriangle(p[0], p[1], p[2], faded(VAN_COL32(0, 0, 0, 90), float((col >> VAN_COL32_A_SHIFT) & 0xFF) / 255.0f), 1.0f);
}

void icon(VanDrawList* dl, int id, const VanVec2& c, float size, VanU32 ink) {
    VanGui::VanIconParams p;
    p.Thickness = std::max(1.2f, size / 9.5f);
    p.Color = faded(VAN_COL32(0, 0, 0, 150), float((ink >> VAN_COL32_A_SHIFT) & 0xFF) / 255.0f);
    const float o = std::max(1.0f, std::floor(size / 14.0f + 0.5f));
    VanGui::DrawIconEx(dl, id, {c.x + o, c.y + o}, size, p);
    p.Color = ink;
    VanGui::DrawIconEx(dl, id, c, size, p);
}

// A word (one or two lines) and, when there is one, its icon to the left: centred in a..b.
void words(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float unit, std::string_view label, int icon_id, VanU32 ink, float size_units) {
    VanFont* f = font_kit();
    const float w = b.x - a.x, h = b.y - a.y;
    const size_t nl = label.find('\n');
    const std::string_view l0 = label.substr(0, nl), l1 = nl == std::string_view::npos ? std::string_view{} : label.substr(nl + 1);
    float size = size_units * unit * kLine * (l1.empty() ? 1.0f : 0.95f);
    const float icon_size = icon_id != VanGui::VanIcon_None ? std::min(h * 0.54f, 22.0f * unit) : 0.0f;
    const float gap = icon_size > 0 ? 3.0f * unit : 0.0f;
    const float room = w - 5.0f * unit - icon_size - gap;
    float tw = std::max(line_width(f, size, l0), l1.empty() ? 0.0f : line_width(f, size, l1));
    if (tw > room && tw > 1) {
        size *= std::max(0.62f, room / tw);
        tw = std::max(line_width(f, size, l0), l1.empty() ? 0.0f : line_width(f, size, l1));
    }
    const float total = icon_size + gap + tw;
    float x = a.x + (w - total) * 0.5f;
    if (icon_size > 0) {
        icon(dl, icon_id, {x + icon_size * 0.5f, a.y + h * 0.5f}, icon_size, ink);
        x += icon_size + gap;
    }
    const float lh = size * kPitch, cy = a.y + h * 0.5f;
    if (l1.empty()) {
        draw_line(dl, f, size, x + (tw - line_width(f, size, l0)) * 0.5f, cy - size * kCapMid, l0, ink);
    } else {
        const float y0 = cy - size * kCapMid - lh * 0.5f;
        draw_line(dl, f, size, x + (tw - line_width(f, size, l0)) * 0.5f, y0, l0, ink);
        draw_line(dl, f, size, x + (tw - line_width(f, size, l1)) * 0.5f, y0 + lh, l1, ink);
    }
}

}  // namespace

bool kit_sharp() { return g_sharp; }
void set_kit_sharp(bool on) { g_sharp = on; }

void kit_label(VanDrawList* dl, std::string_view text, const VanVec2& centre, float size_px, VanU32 ink, float fit_px) {
    VanFont* f = font_kit();
    const size_t nl = text.find('\n');
    const std::string_view l0 = text.substr(0, nl), l1 = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    float size = size_px;
    float tw = std::max(line_width(f, size, l0), l1.empty() ? 0.0f : line_width(f, size, l1));
    if (fit_px > 0 && tw > fit_px) size *= std::max(0.6f, fit_px / tw);
    const float lh = size * kPitch;
    if (l1.empty()) {
        draw_line(dl, f, size, centre.x - line_width(f, size, l0) * 0.5f, centre.y - size * kCapMid, l0, ink);
    } else {
        const float y0 = centre.y - size * kCapMid - lh * 0.5f;
        draw_line(dl, f, size, centre.x - line_width(f, size, l0) * 0.5f, y0, l0, ink);
        draw_line(dl, f, size, centre.x - line_width(f, size, l1) * 0.5f, y0 + lh, l1, ink);
    }
}

float kit_label_left(VanDrawList* dl, std::string_view text, const VanVec2& left, float size_px, VanU32 ink) {
    VanFont* f = font_kit();
    draw_line(dl, f, size_px, left.x, left.y - size_px * kCapMid, text, ink);
    return line_width(f, size_px, text);
}

void kit_plate(VanDrawList* dl, const VanVec2& a, const VanVec2& b, float unit, std::string_view label, bool chosen, bool hovered, bool pressed) {
    const float w = line_px(unit);
    const bool lit = chosen || pressed;
    plate(dl, a, b, kTabTop, kTabBottom, lit ? kFrameLit : hovered ? kFrameHover : kFrame, lit ? w * 1.5f : w, 1.0f);
    words(dl, a, b, unit, label, VanGui::VanIcon_None, lit ? kInkLit : hovered ? kInkHover : kInk, 11.0f);
}

bool kit_draw(VanDrawList* dl, std::string_view sprite, int state, int states, const VanVec2& a0, const VanVec2& b0, VanU32 tint, float unit) {
    if (!g_sharp || !dl) return false;
    const auto& table = specs();
    const auto it = table.find(sprite);
    if (it == table.end()) return false;
    const Spec& sp = it->second;
    const float alpha = float((tint >> VAN_COL32_A_SHIFT) & 0xFF) / 255.0f;
    const float w = line_px(unit);
    // The kit's plates sit a pixel inside their sprites.
    const VanVec2 a{a0.x + unit * 0.5f, a0.y + unit * 0.5f}, b{b0.x - unit * 0.5f, b0.y - unit * 0.5f};
    const VanVec2 c{(a0.x + b0.x) * 0.5f, (a0.y + b0.y) * 0.5f};
    const float h = b0.y - a0.y;
    state = std::clamp(state, 0, std::max(0, states - 1));

    switch (sp.kind) {
        case Kind::Nav: {
            const bool chosen = state >= 2, hover = state == 1 || state == 3;
            plate(dl, a, b, kPlateTop, kPlateBottom, chosen && hover ? kFrameLit : hover ? kFrameHover : kFrame, w, alpha);
            words(dl, a, b, unit, sp.label, sp.icon, faded(chosen ? kInkLit : hover ? kInkHover : kInk, alpha), 11.5f);
            return true;
        }
        case Kind::Button: {
            const bool hover = state == 1, pressed = state >= 2;
            plate(dl, a, b, kPlateTop, kPlateBottom, pressed ? kFrameLit : hover ? kFrameHover : kFrame, w, alpha);
            words(dl, a, b, unit, sp.label, sp.icon, faded(pressed ? kInkLit : hover ? kInkHover : kInk, alpha), h < 34.0f * unit ? 10.5f : 11.5f);
            return true;
        }
        case Kind::Tab:
        case Kind::SubTab: {
            // A tab asked for in its chosen look but faded is a hover (a two-state tab has none of its own).
            const bool chosen = state >= 1 && alpha > 0.8f, hover = state >= 1 && !chosen;
            const float fade = hover ? 1.0f : alpha;
            const bool sub = sp.kind == Kind::SubTab;
            plate(dl, a, b, sub ? kTabTop : kPlateTop, sub ? kTabBottom : kPlateBottom, chosen ? kFrameLit : hover ? kFrameHover : kFrame, chosen ? w * 1.5f : w, fade);
            words(dl, a, b, unit, sp.label, sp.icon, faded(chosen ? kInkLit : hover ? kInkHover : kInk, fade), sub ? 10.5f : 11.5f);
            return true;
        }
        case Kind::Small: {
            plate(dl, a, b, kSmallPlate, kSmallPlate, state >= 2 ? kFrameLit : VAN_COL32(60, 61, 59, 255), w, alpha);
            words(dl, a, b, unit, sp.label, sp.icon, faded(state >= 1 ? kInkLit : kInk, alpha), 9.6f);
            return true;
        }
        case Kind::Toggle: {
            // The first word is the lit state (Accept), the second the unlit (Block).
            const bool second = state >= 1;
            plate(dl, a, b, kTabTop, kTabBottom, kFrame, w, alpha);
            words(dl, a, b, unit, second && sp.label2 ? sp.label2 : sp.label, sp.icon, faded(second ? kInk : kInkLit, alpha), 9.8f);
            return true;
        }
        case Kind::Sound: {
            const bool off = state >= 1;
            plate(dl, a, b, kTabTop, kTabBottom, kFrame, w, alpha);
            const VanU32 ink = faded(off ? kInk : kInkLit, alpha);
            const float r = h * 0.14f;
            const float size = 9.6f * unit * kLine;
            const float tw = line_width(font_kit(), size, sp.label);
            const float x = c.x - (tw + r * 2 + 3 * unit) * 0.5f;
            dl->AddCircleFilled({x + r, c.y}, r, ink, 20);
            dl->AddCircle({x + r, c.y}, r, faded(VAN_COL32(0, 0, 0, 160), alpha), 20, w);
            draw_line(dl, font_kit(), size, x + r * 2 + 3 * unit, c.y - size * kCapMid, sp.label, ink);
            return true;
        }
        case Kind::Back:
        case Kind::Power: {
            const bool hover = state == 1, pressed = state >= 2;
            const VanU32 top = pressed ? VAN_COL32(96, 104, 60, 255) : VAN_COL32(54, 52, 47, 255), bottom = pressed ? VAN_COL32(84, 88, 58, 255) : VAN_COL32(84, 78, 62, 255);
            plate(dl, a, b, top, bottom, pressed ? kOrangeLit : kOrange, w * 2.0f, alpha);
            const VanU32 ink = faded(pressed ? VAN_COL32(247, 253, 220, 255) : hover ? kInkLit : VAN_COL32(210, 210, 209, 255), alpha);
            const float s = std::min(b.x - a.x, b.y - a.y);
            if (sp.kind == Kind::Power) {
                icon(dl, VanGui::VanIcon_Power, c, s * 0.56f, ink);
            } else {
                // The return arrow: down the right side, then left to its head.
                const float u = s * 0.11f, t = std::max(2.0f, s * 0.115f);
                const VanVec2 p[3] = {{c.x + 2.4f * u, c.y - 2.1f * u}, {c.x + 2.4f * u, c.y + 1.1f * u}, {c.x - 1.0f * u, c.y + 1.1f * u}};
                const VanU32 dark = faded(VAN_COL32(0, 0, 0, 150), alpha);
                for (int pass = 0; pass < 2; ++pass) {
                    const float o = pass == 0 ? std::max(1.0f, t * 0.3f) : 0.0f;
                    const VanU32 col = pass == 0 ? dark : ink;
                    VanVec2 q[3] = {{p[0].x + o, p[0].y + o}, {p[1].x + o, p[1].y + o}, {p[2].x + o, p[2].y + o}};
                    dl->AddPolyline(q, 3, col, 0, t);
                    dl->AddTriangleFilled({c.x - 3.3f * u + o, c.y + 1.1f * u + o}, {c.x - 0.6f * u + o, c.y - 0.9f * u + o}, {c.x - 0.6f * u + o, c.y + 3.1f * u + o}, col);
                }
            }
            return true;
        }
        case Kind::Help: {
            const bool pressed = state >= 2;
            plate(dl, a, b, VAN_COL32(42, 42, 42, 255), VAN_COL32(42, 42, 42, 255), pressed ? kOrangeLit : kOrange, w * 2.0f, alpha);
            words(dl, a, b, unit, sp.label, sp.icon, faded(state >= 1 ? kInkLit : kInk, alpha), 11.0f);
            return true;
        }
        case Kind::Close: {
            const bool hover = state == 1, pressed = state >= 2;
            plate(dl, a, b, kPlateTop, kPlateBottom, pressed ? kFrameLit : hover ? kFrameHover : VAN_COL32(70, 75, 61, 255), w, alpha);
            const float r = std::min(b.x - a.x, b.y - a.y) * 0.24f, t = std::max(1.5f, 1.7f * unit);
            const VanU32 ink = faded(pressed ? kInkLit : hover ? kInkHover : kInk, alpha);
            dl->AddLine({c.x - r, c.y - r}, {c.x + r, c.y + r}, ink, t);
            dl->AddLine({c.x + r, c.y - r}, {c.x - r, c.y + r}, ink, t);
            return true;
        }
        case Kind::Up:
        case Kind::Down:
            triangle(dl, a, b, 0, sp.kind == Kind::Up ? -1 : 1, faded(state >= 1 ? kOrangeHover : kOrange, alpha));
            return true;
        case Kind::Left:
        case Kind::Right: {
            // The kit's orange arrows, or the green ones (which light paler).
            const bool green = sp.colour != 0;
            const VanU32 rest = green ? sp.colour : kOrange, hover = green ? VAN_COL32(150, 172, 70, 255) : kOrangeHover,
                         lit = green ? VAN_COL32(196, 212, 124, 255) : kOrangeLit;
            triangle(dl, a0, b0, sp.kind == Kind::Left ? -1 : 1, 0, faded(state >= 2 ? lit : state == 1 ? hover : rest, alpha));
            return true;
        }
        case Kind::Radio: {
            const float r = std::min(b0.x - a0.x, b0.y - a0.y) * 0.34f;
            dl->AddCircleFilled(c, r, faded(VAN_COL32(18, 18, 16, 255), alpha), 28);
            dl->AddCircle(c, r, faded(VAN_COL32(150, 152, 146, 255), alpha), 28, std::max(1.0f, 1.3f * unit));
            if (state >= 1) dl->AddCircleFilled(c, r * 0.5f, faded(kInkLit, alpha), 20);
            return true;
        }
        case Kind::Check: {
            const VanVec2 ia{std::floor(a0.x), std::floor(a0.y)}, ib{std::floor(b0.x), std::floor(b0.y)};
            dl->AddRectFilled(ia, ib, faded(VAN_COL32(18, 18, 16, 255), alpha));
            dl->AddRect({ia.x + w * 0.5f, ia.y + w * 0.5f}, {ib.x - w * 0.5f, ib.y - w * 0.5f}, faded(VAN_COL32(170, 172, 164, 255), alpha), 0, 0, w);
            if (state >= 1) {
                const float s = ib.x - ia.x;
                const VanVec2 p[3] = {{ia.x + s * 0.22f, ia.y + s * 0.52f}, {ia.x + s * 0.43f, ia.y + s * 0.73f}, {ia.x + s * 0.80f, ia.y + s * 0.27f}};
                dl->AddPolyline(p, 3, faded(VAN_COL32(244, 244, 240, 255), alpha), 0, std::max(1.5f, s * 0.14f));
            }
            return true;
        }
        case Kind::Thumb: {
            const float r = (b0.x - a0.x) * 0.3f;
            dl->AddRectFilled(a0, b0, faded(VAN_COL32(22, 22, 20, 255), alpha), r);
            dl->AddRectFilledMultiColor({a0.x + w, a0.y + w}, {b0.x - w, b0.y - w}, tint, tint, faded(VAN_COL32(150, 150, 146, 255), alpha), faded(VAN_COL32(150, 150, 146, 255), alpha));
            return true;
        }
        case Kind::Heading: {
            // The kit's bead (a ball with a highlight) and its word in cream.
            const float r = h * 0.40f;
            const VanVec2 bc{a0.x + r + unit, c.y};
            dl->AddCircleFilled(bc, r + w, faded(VAN_COL32(10, 10, 8, 255), alpha), 24);
            dl->AddCircleFilled(bc, r, faded(sp.colour, alpha), 24);
            dl->AddCircleFilled({bc.x - r * 0.28f, bc.y - r * 0.32f}, r * 0.34f, faded(VAN_COL32(255, 255, 255, 150), alpha), 14);
            draw_line(dl, font_kit(), 11.5f * unit * kLine, bc.x + r + 4.0f * unit, c.y - 11.5f * unit * kLine * kCapMid, sp.label,
                      faded(VAN_COL32(250, 240, 180, 255), alpha));
            return true;
        }
        case Kind::Title:
            draw_line(dl, font_kit(), 11.0f * unit * kLine, a0.x, c.y - 11.0f * unit * kLine * kCapMid, sp.label, faded(VAN_COL32(244, 244, 240, 255), alpha));
            return true;
        case Kind::State: {
            // Small capitals, the first letter a size up, in the state's own colour.
            VanFont* f = font_kit();
            const float big = 13.0f * unit * kLine, small = 10.0f * unit * kLine;
            const std::string_view s = sp.label;
            const float w0 = line_width(f, big, s.substr(0, 1)), w1 = line_width(f, small, s.substr(1));
            const float x = c.x - (w0 + w1) * 0.5f;
            const VanU32 ink = faded(sp.colour, alpha);
            // Both sizes stand on one baseline (0.82 of the line down).
            draw_line(dl, f, big, x, c.y - big * kCapMid, s.substr(0, 1), ink);
            draw_line(dl, f, small, x + w0 + tracking(small), c.y - big * kCapMid + (big - small) * 0.82f, s.substr(1), ink);
            return true;
        }
    }
    return false;
}

}  // namespace lsf::ui
