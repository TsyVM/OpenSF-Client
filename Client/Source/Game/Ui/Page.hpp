// The original client's lobby pages, drawn the way it drew them: every element of a page script
// (lobby Page*.txt, Docs/Research.md §1.1) at its own rect on the 1024x768 grid, in the lobby's
// own art, the whole page stretched over the window as the client stretched it on a wide screen.
//
// A page draws in two layers. draw_static() puts down everything the script alone decides, in
// script order: pictures (*IMAGE), outlines (*LINEFRAME), labels, the marquee, and every button,
// tab and selector in its resting state. The screen then calls the interactive pieces by the
// element's *ID — button(), tabs(), list(), edit(), ... — which draw their live state over that
// and report what the player did. Lists, text boxes and team panels are only ever drawn that way:
// the script gives their rect and columns, the game fills them.
//
// What the scripts leave to the engine is styled after period screenshots of the English client:
// outlines grey rather than the scripts' black, Tahoma lettering, 21-pixel list rows under a
// darker header, the olive selection band, orange scroll arrows.
#pragma once

#include "Game/Ui/Atlas.hpp"
#include "Game/Ui/Ui.hpp"
#include "SF/UiData.hpp"

#include <vangui/vangui.h>

#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lsf::ui {

inline constexpr float kPageW = 1024.0f, kPageH = 768.0f;

// ── The frame every page is drawn in ───────────────────────────────────────────
// Page units to pixels: x by width/1024, y by height/768.
VanVec2 pg(float x, float y);
float pgx(float w);
float pgy(float h);
// Opens the full-window host the pages' controls live in; pair with page_end().
void page_begin(const char* id);
void page_end();

// ── A page ─────────────────────────────────────────────────────────────────────
class Page {
public:
    // The newest copy of `name` ("PageServer"). Its layout is the one the client shipped last;
    // its strings are not always English (lobbydata71 and later came from the Japanese service),
    // so the screens name their columns and labels themselves.
    bool load(const sf::Data& data, std::string_view name);
    bool loaded() const { return loaded_; }
    const sf::PageNode& root() const { return root_; }
    const sf::PageNode* find(int id, std::string_view kind = {}) const { return root_.find_id(id, kind); }
    // The rect of an element, in page units.
    bool rect(int id, float& x0, float& y0, float& x1, float& y1, std::string_view kind = {}) const;

private:
    sf::PageNode root_;
    bool loaded_ = false;
};

// ── Drawing ────────────────────────────────────────────────────────────────────
// Everything the script decides on its own, in script order, on the background list. Elements
// whose id is in `skip` are left out (a picture the screen replaces, a button it hides).
void draw_static(const Page& page, std::initializer_list<int> skip = {});

// A button (*BUTTON) by id: its sprite's live state over the resting one. `sprite` swaps the art
// (the host's Start for Ready). Returns true on a click.
bool button(const Page& page, int id, bool enabled = true, const char* sprite = nullptr, const char* tooltip = nullptr, bool latched = false);
// A tab row (*TABCTRL / *BTNTABCTRL): draws `selected`'s selected state and returns the id of a
// tab clicked this frame, or -1. `enabled` may disable single tabs. `labels` (by tab, in script
// order) renames tabs whose words are baked into their sprites: the kit's plate is drawn afresh
// in its own colours with the new word on it.
int tabs(const Page& page, int ctrl_id, int selected, const std::function<bool(int)>& enabled = {}, std::span<const std::string> labels = {});
// A two-state *TAB used as a switch (Accept/Block invites, sound on/off). Returns true on a click.
bool toggle(const Page& page, int id, bool second_state);
// A ◀ label ▶ selector (*HORIZBAR). Returns -1 or +1 when an arrow is clicked, else 0.
int selector(const Page& page, int id, std::string_view label, bool enabled = true);
// A meter (*VALUEBAR), `fraction` full.
void meter(const Page& page, int id, float fraction, VanU32 tint = 0xFFFFFFFF);
// The kit's wear bar (durability_bar_1 over value_bottom_1) anywhere on the page grid: a card's.
void meter_at(float x0, float y0, float x1, float y1, float fraction, VanU32 tint = 0xFFFFFFFF);
// A picture into an *IMAGE's rect (a map, a portrait), and text into a *TEXT's.
void picture(const Page& page, int id, const Picture& pic, bool keep_aspect = false);
void text(const Page& page, int id, std::string_view s, VanU32 colour = 0xFFFFFFFF, Align align = Align::Left, bool bold = true);
// Several lines, wrapped to the *TEXT's width (a mission's description).
void paragraph(const Page& page, int id, std::string_view s, VanU32 colour);
// Text anywhere on the page grid (the bottom strip, a tile's caption).
void text_at(float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour, Align align = Align::Left, bool bold = true,
             float size = 12.0f);
// Loose pieces on the page grid, for what the engine drew itself (the room's team panels).
void page_background(std::string_view sprite = "default_bottom");
// Text wrapped to the box's width (as many lines as fit; the rest is clipped).
void text_wrapped(float x0, float y0, float x1, float y1, std::string_view s, VanU32 colour, float size = 12.0f);
// What a control says about itself when the pointer is on it: a tooltip, or, while a sink is set
// (a dialog with a line for it: the options), the sink's text instead. nullptr: tooltips again.
void tip(const char* text);
void set_tip_sink(std::string* sink);   // under everything, the whole page
// A code name in its Colored Codename colour (Game/Items.hpp kNameColours; 0 plain), glowing, in a rect.
void name_text_at(float x0, float y0, float x1, float y1, std::string_view name, eng::u8 colour, Align align = Align::Left, float size = 12.0f);
// The ink a name colour draws in, and the same glow at a pixel point (the HUD's and lists' own text).
VanU32 name_ink(eng::u8 colour);
void glow_text(VanDrawList* dl, VanFont* font, float px_size, VanVec2 at, std::string_view s, eng::u8 colour, VanU32 plain);
void sprite_at(std::string_view name, int state, int states, float x0, float y0, float x1, float y1, VanU32 tint = 0xFFFFFFFF);
void picture_at(const Picture& pic, float x0, float y0, float x1, float y1, VanU32 tint = 0xFFFFFFFF, bool keep_aspect = false);
void fill_at(float x0, float y0, float x1, float y1, VanU32 colour);
// A clickable area (`id` scopes it). Returns true on a click.
bool region(int id, float x0, float y0, float x1, float y1, bool* hovered = nullptr);
// The *FLOWTEXT marquee, with `message` in place of the script's own when given.
void marquee(const Page& page, int id, std::string_view message = {});
// An edit box (*EDIT). Returns true when Enter was pressed in it.
bool edit(const Page& page, int id, std::string& value, const char* hint = nullptr, bool password = false);
// A drop-down (*COMBOBOX; *STYLE up opens it upward). Returns true when the choice changed.
bool combo(const Page& page, int id, int& selected, std::span<const std::string> items);
// A scrolling text box (*TEXT with a *SCROLLID): the chat log.
struct Line {
    std::string text;
    VanU32 colour = 0xFFFFFFFF;
};
void text_log(const Page& page, int id, std::span<const Line> lines);

// A list (*LISTBOX) with the script's columns. `titles` replaces the column titles when given.
struct Cell {
    std::string text;
    VanU32 colour = 0xFFE6E6E6;
    Align align = Align::Center;
    bool bold = false;
    Picture icon;      // drawn at the cell's left (a rank badge, a flag)
    SpriteRef sprite;  // or a sprite, the same way
};
struct Row {
    std::vector<Cell> cells;
    int key = 0;       // the caller's id for the row (a room number, a channel id)
};
struct ListResult {
    bool clicked = false;      // a row was clicked this frame
    bool activated = false;    // ... twice (join, enter)
    bool context = false;      // right-clicked (the list itself when `key` < 0)
    int key = -1;
};
ListResult list(const Page& page, int id, std::span<const Row> rows, int selected_key, std::span<const std::string> titles = {});

// The shops' item grid (*BTNON3BTNCTRL): the kit's 203x144 cards (item_bottom_1: normal, hover,
// chosen) three across, scrolled by the orange arrows outside its right edge. `fill` puts each
// visible card's contents on; returns the index of a card clicked this frame, or -1.
struct Card {
    float x0, y0, x1, y1;
    int index;
    bool hovered, selected;
};
int card_grid(const Page& page, int id, int count, int selected, const std::function<void(const Card&)>& fill);
// One of a card's small buttons (buy_1, view_1, using_1, sell_1: 43x23, three states), its top-left
// at (x, y) on the page grid. Returns true on a click.
bool card_button(int key, std::string_view sprite, float x, float y, bool enabled = true, const char* tooltip = nullptr);

// ── Dialogs in the kit's own pieces ────────────────────────────────────────────
// The client opened its dialogs (Make Room, the options, a clan's card) over the page, drawn in the
// same kit: a grey body in the outline the pages use, a title band, the X in its corner, the
// three-state plates (Confirm, Cancel, Close, Default) along the bottom. Everything below is on
// the page grid, so a dialog stretches with the page it sits on.
//
// dialog_begin() opens a modal the size of the window (the page beneath dims and takes no input)
// and draws the frame at (x0, y0)-(x1, y1). Call it every frame while the dialog is up, after
// VanGui::OpenPopup(id) the first time; when it returns true, draw the contents and call
// dialog_end(). `*open` goes false on the X or Esc (the popup is closed for you).
bool dialog_begin(const char* id, float x0, float y0, float x1, float y1, std::string_view title, bool* open = nullptr);
void dialog_end();
// Closes the dialog being drawn (after Confirm or Cancel).
void dialog_close();
// The next dialog_begin() leaves Esc alone this frame (it belongs to something inside the dialog:
// a key being set).
void dialog_keep_on_escape();
// A plate of the kit (confirm_1, cancel_1, close_1, baseValue_1: 73x41, three states), anywhere.
bool kit_button(int key, std::string_view sprite, float x0, float y0, float x1, float y1, bool enabled = true, const char* tooltip = nullptr,
                int states = 3);
// A section's heading: a lime bead, the words, a keyline running on to x1.
void heading(float x0, float y, float x1, std::string_view label);
// A sunken well (black under a grey outline): behind lists, pictures, text boxes.
void well(float x0, float y0, float x1, float y1);
// A radio button (radio_btn_1) with its label to its right. Returns true when clicked.
bool radio(int key, float x, float y, std::string_view label, bool on, bool enabled = true, const char* tooltip = nullptr);
// A check box (the kit's `checkbox`) with its label. Flips `value` and returns true when clicked.
bool check(int key, float x, float y, std::string_view label, bool& value, bool enabled = true, const char* tooltip = nullptr);
// ◀ label ▶ over a bg_gray_1 plate, with the room page's orange arrows. -1 / +1 when clicked.
int arrows(int key, float x0, float y0, float x1, float y1, std::string_view label, bool enabled = true);
// A slider: the kit's trackbar thumb on a groove, the part up to it lit. Returns true while it
// changes; `format` (printf, one float) writes the value to the right of the groove.
bool trackbar(int key, float x0, float y0, float x1, float y1, float& value, float lo, float hi, const char* format = nullptr, bool enabled = true);
// An edit box anywhere (edit() is this at an *EDIT's rect). Returns true when Enter was pressed.
bool edit_at(int key, float x0, float y0, float x1, float y1, std::string& value, int limit, const char* hint = nullptr, bool password = false,
             bool enabled = true, VanU32 back = VAN_COL32(0, 0, 0, 170), VanU32 ink = 0xFFFFFFFF);
// A scrolling list of single lines in a well, the olive band on `selected`. Returns the index
// clicked this frame (-1: none); `activated` is set on a double click. The list scrolls itself
// to `selected` when it changes from outside (the map's arrows).
int pick_list(int key, float x0, float y0, float x1, float y1, std::span<const std::string> items, int selected, bool* activated = nullptr);
// Rows of the caller's own drawing in a well, `row_h` high, scrolled like a list; `draw` fills row i
// in its rect (hovered, chosen). Returns the row clicked, or -1.
int rows(int key, float x0, float y0, float x1, float y1, int count, float row_h, int selected,
         const std::function<void(int i, float x0, float y0, float x1, float y1, bool hovered, bool chosen)>& draw);
// Square tiles `tile` wide in a well, as many across as fit, scrolled the same way (the clan mark
// builder's pieces). Returns the tile clicked, or -1.
int tile_grid(int key, float x0, float y0, float x1, float y1, int count, float tile, int selected,
              const std::function<void(int i, float x0, float y0, float x1, float y1)>& draw);
// A plate with a word on it, where the kit has no sprite for the word (the 2010 kit's tab plate).
bool text_button(int key, float x0, float y0, float x1, float y1, std::string_view label, bool enabled = true, const char* tooltip = nullptr);
// The same plate as a tab: lit while `chosen`. Returns true when clicked.
bool tab_button(int key, float x0, float y0, float x1, float y1, std::string_view label, bool chosen);
// What is drawn until clip_end() shows only inside this rect (a list scrolled by hand).
void clip_begin(float x0, float y0, float x1, float y1);
void clip_end();
// The pointer on the page grid, and the wheel's turn this frame.
VanVec2 pointer();
float wheel();

}  // namespace lsf::ui
