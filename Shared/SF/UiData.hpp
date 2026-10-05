// The lobby's own interface data (data/lobby/lobbydata*.mrg): sprite atlases, page layout
// scripts, and the tables the lobby reads (maps, ranks, notices). See Docs/Research.md §1.
//
//   source*.txt      name x0 y0 x1 y1        one sprite per line, into the sheet of the same
//                                           stem (.bmp without alpha, .tga with it)
//   Page*.txt        *FRAME { *COMMON { *ID *RECT } *CHILD { *IMAGE {...} *BUTTON {...} ... } }
//                    on a 1024x768 grid; a *SPLIT n button stacks n states down its sprite
//   MapName.txt      index  picture  "name"  "mission"  "attack text"  "defence text"  "TRAINING"
//   SF_ClassPoint.txt  [CLASS-n] NAME= CLASSID= MAXPOINT=
#pragma once

#include "SF/Data.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

// ── Atlases ────────────────────────────────────────────────────────────────────

struct Sprite {
    std::string name;
    int sheet = -1;          // index into UiAtlas::sheets()
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
};

struct Sheet {
    std::string name;        // "sourcenewalpha03"
    std::string text_key;    // its sprite table
    std::string image_key;   // its picture, empty when the archives do not ship one
    int generation = 0;      // newer kits win a name several sheets define
};

class UiAtlas {
public:
    // Every source*.txt the lobby archives carry, and its picture.
    bool load(const Data& data);
    const std::vector<Sheet>& sheets() const { return sheets_; }
    const std::vector<Sprite>& sprites() const { return sprites_; }
    // By name (case-insensitive), from the newest kit that has it, or from `sheet` only.
    const Sprite* find(std::string_view name, std::string_view sheet = {}) const;

private:
    std::vector<Sheet> sheets_;
    std::vector<Sprite> sprites_;
    std::map<std::string, std::vector<int>> by_name_;   // lower-case name -> sprites, newest first
};

// ── Page scripts ───────────────────────────────────────────────────────────────

// One element of a page: its kind (FRAME, IMAGE, BUTTON, LISTBOX, ...), its properties as the
// script spells them (first token after the key; `values` keeps every token), and children.
struct PageNode {
    std::string kind;
    std::string comment;                                   // the // note beside it (UTF-8)
    std::map<std::string, std::vector<std::string>> props; // "ID" -> {"101"}, "RECT" -> {"30","234","549","680"}
    std::vector<PageNode> children;

    int id() const;
    bool rect(int& x0, int& y0, int& x1, int& y1) const;
    std::string text(std::string_view key) const;          // first value, quotes stripped
    int number(std::string_view key, int fallback = 0) const;
    // Depth-first search.
    const PageNode* find_id(int id, std::string_view kind = {}) const;
    void collect(std::string_view kind, std::vector<const PageNode*>& out) const;
};

// Parses one page script (cp949 or UTF-8 text).
std::optional<PageNode> parse_page(std::string_view text);
// "Page2" -> the newest Page2.txt the lobby ships, parsed.
std::optional<PageNode> load_page(const Data& data, std::string_view name);

// ── Tables ─────────────────────────────────────────────────────────────────────

struct MapInfo {
    int index = 0;
    std::string picture;       // "SF_M_Shanghai.jpg" (lobby archives)
    std::string name;          // "Shanghai"
    std::string mission;       // "Escape", "Take Back", "Destroy", "Dual", "Random", ...
    std::string attack_text;   // '|' already turned into '\n'
    std::string defence_text;
    bool training = false;
};
std::vector<MapInfo> load_map_names(const Data& data);

struct RankInfo {
    int id = 0;
    std::string name;          // "Private", "Staff_Sergeant" (underscores kept as shipped)
    int max_point = 0;         // XP at which the next rank begins
};
std::vector<RankInfo> load_ranks(const Data& data);

// A lobby text file, newest copy, as UTF-8 (Notice.txt, AdTextList.txt, ...).
std::optional<std::string> load_lobby_text(const Data& data, std::string_view name);

}  // namespace sf
