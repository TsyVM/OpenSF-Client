// The lobby's sprite sheets on the card, and the pictures the menus show (map thumbnails,
// loading screens, rank badges), loaded the first time a screen asks for them.
//
// A sprite is one rect of a sheet (Shared/SF/UiData.hpp). A button sprite stacks its states
// down the rect: normal, hover, pressed (and disabled or selected, where a fourth is drawn), so
// `sprite(name, state, states)` cuts one state out.
#pragma once

#include "Engine/Render/Device.hpp"
#include "SF/ClanMarks.hpp"
#include "SF/Data.hpp"
#include "SF/UiData.hpp"

#include <vangui/vangui.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lsf::ui {

struct SpriteRef {
    eng::u64 tex = 0;          // VanGUI texture id
    VanVec2 uv0{0, 0}, uv1{1, 1};
    float w = 0, h = 0;        // pixels of the state
    bool valid() const { return tex != 0; }
};

struct Picture {
    eng::u64 tex = 0;
    float w = 0, h = 0;
    bool valid() const { return tex != 0; }
};

class Atlas {
public:
    // CPU side, on a loader thread: the tables and every sheet's pixels.
    bool prepare(const sf::Data& data);
    // GPU side, on the main thread: the sheets onto the card.
    bool upload(eng::Device& device);
    bool ready() const { return uploaded_; }

    SpriteRef sprite(std::string_view name, int state = 0, int states = 1, std::string_view sheet = {}) const;
    const sf::UiAtlas& table() const { return table_; }

    // A picture from an archive (decoded now, then kept). Invalid when the archive lacks it.
    // `crop_w`/`crop_h` keep only the top-left part (the lobby's backgrounds sit in the top
    // 1024x768 of a 1024x1024 sheet over a red filler).
    Picture picture(sf::Pack pack, std::string_view key, int crop_w = 0, int crop_h = 0);
    // The same for art the original drew additively on black (the HUD's damage arcs): black
    // becomes transparent and brightness becomes alpha.
    Picture glow_picture(sf::Pack pack, std::string_view key);
    // Pixels of our own (the radar's floorplan), uploaded under `id`, replacing what was there.
    Picture image_picture(const std::string& id, const eng::Image& img);
    // A picture of our own from a file (Client/Content/ui/...), kept the same way.
    Picture file_picture(const std::filesystem::path& file);
    // The lobby's picture of a map ("SF_M_Crossroad.jpg"), found by level id or MapName name.
    Picture map_picture(std::string_view level_id, std::string_view picture_hint = {});
    // The map's loading screen from the menu archives ("SF_L_Crossroad.jpg").
    Picture loading_picture(std::string_view load_image, std::string_view level_id);
    // The 32x32 rank badge (lobby Division_N.bmp, else the menu's inf/class icons).
    Picture rank_badge(int rank);
    // A piece of the clan mark builder (data/clan, SF/ClanMarks.hpp): 128x128, numbered from 1.
    void set_clan_marks(const sf::ClanMarks* marks) { clan_marks_ = marks; }
    Picture clan_piece(sf::MarkLayer layer, int index);

    void set_device(eng::Device* d, const sf::Data* data) { device_ = d, data_ = data; }
    void release_pictures();

private:
    struct SheetPixels {
        eng::Image image;
    };
    sf::UiAtlas table_;
    std::vector<SheetPixels> pixels_;
    std::vector<eng::TextureRef> sheets_;
    bool uploaded_ = false;
    eng::Device* device_ = nullptr;
    const sf::Data* data_ = nullptr;
    const sf::ClanMarks* clan_marks_ = nullptr;
    std::map<std::string, std::pair<eng::TextureRef, Picture>> pictures_;
};

}  // namespace lsf::ui
