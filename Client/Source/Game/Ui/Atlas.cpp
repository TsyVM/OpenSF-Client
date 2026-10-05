#include "Game/Ui/Atlas.hpp"

#include "Game/Render/Upload.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"
#include "SF/Image.hpp"

#include <algorithm>
#include <cstring>
#include <span>

namespace lsf::ui {

using eng::u32;

bool Atlas::prepare(const sf::Data& data) {
    if (!table_.load(data)) return false;
    pixels_.assign(table_.sheets().size(), {});
    for (size_t i = 0; i < table_.sheets().size(); ++i) {
        const sf::Sheet& sheet = table_.sheets()[i];
        auto bytes = data.read(sf::Pack::Lobby, sheet.image_key);
        if (!bytes) continue;
        eng::Image img;
        std::string err;
        if (!sf::decode_image(*bytes, img, &err)) {
            LOG_WARN("UI: sheet %s: %s", sheet.name.c_str(), err.c_str());
            continue;
        }
        // The old .bmp sheets have no alpha: their filler is pure red or pure blue.
        if (sheet.image_key.ends_with(".bmp")) {
            sf::key_colour(img, 255, 0, 0, 12);
            sf::key_colour(img, 0, 0, 255, 12);
        }
        pixels_[i].image = std::move(img);
    }
    return true;
}

bool Atlas::upload(eng::Device& device) {
    device_ = &device;
    sheets_.assign(pixels_.size(), nullptr);
    for (size_t i = 0; i < pixels_.size(); ++i) {
        if (pixels_[i].image.rgba.empty()) continue;
        sheets_[i] = upload_image(device, pixels_[i].image, false);
        pixels_[i].image = {};   // the card has it now
    }
    uploaded_ = true;
    return true;
}

SpriteRef Atlas::sprite(std::string_view name, int state, int states, std::string_view sheet) const {
    SpriteRef out;
    const sf::Sprite* s = table_.find(name, sheet);
    if (!s || s->sheet < 0 || size_t(s->sheet) >= sheets_.size() || !sheets_[size_t(s->sheet)]) return out;
    const eng::Texture& t = *sheets_[size_t(s->sheet)];
    states = std::max(1, states);
    state = std::clamp(state, 0, states - 1);
    const float h = float(s->height()) / float(states);
    const float y0 = float(s->y0) + h * float(state);
    out.tex = t.ui_id();
    out.uv0 = {float(s->x0) / float(t.width()), y0 / float(t.height())};
    out.uv1 = {float(s->x1) / float(t.width()), (y0 + h) / float(t.height())};
    out.w = float(s->width());
    out.h = h;
    return out;
}

Picture Atlas::picture(sf::Pack pack, std::string_view key, int crop_w, int crop_h) {
    const std::string id = std::string(sf::pack_folder(pack)) + "|" + sf::lower(key) + (crop_w ? "|" + std::to_string(crop_w) + "x" + std::to_string(crop_h) : "");
    if (auto it = pictures_.find(id); it != pictures_.end()) return it->second.second;
    Picture pic;
    eng::TextureRef tex;
    if (device_ && data_) {
        std::string found = sf::lower(key);
        auto bytes = data_->read(pack, key);
        if (!bytes) {
            if (auto where = data_->resolve_any_extension(pack, key)) {
                bytes = data_->read(pack, *where);
                found = data_->key(pack, *where);
            }
        }
        eng::Image img;
        if (bytes && sf::decode_image(*bytes, img)) {
            // The .bmp pictures have no alpha either: they sit on pure blue filler.
            // The HUD's weapon icons (inf/weapon/w_*.bmp) use that blue inside the gun too, so every
            // blue pixel goes there.
            if (found.ends_with(".bmp") && found.find("inf/weapon/w_") != std::string::npos) {
                // A soft key with the spill taken out: how much bluer than its other channels a pixel
                // is says how much filler it is, so the gun's anti-aliased rim keeps no blue halo.
                for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
                    eng::u8* p = &img.rgba[i];
                    const int other = std::max(p[0], p[1]);
                    const int spill = int(p[2]) - other;
                    if (spill <= 16) continue;
                    p[3] = eng::u8(std::clamp(255 - (spill - 16) * 255 / 120, 0, 255));
                    p[2] = eng::u8(other);
                }
            }
            else if (found.ends_with(".bmp")) sf::key_colour_from_edges(img, 0, 0, 255);
            else if (img.width > 2 && img.height > 2) {
                // Some pictures ship as .tga with no alpha on the same blue filler (the character
                // parts' _LB pictures): keyed the same way when all four corners are that blue.
                auto blue = [&](u32 x, u32 y) {
                    const eng::u8* q = &img.rgba[(size_t(y) * img.width + x) * 4];
                    return q[3] == 255 && q[0] < 24 && q[1] < 24 && q[2] > 230;
                };
                if (blue(0, 0) && blue(img.width - 1, 0) && blue(0, img.height - 1) && blue(img.width - 1, img.height - 1))
                    sf::key_colour_from_edges(img, 0, 0, 255);
            }
            if (crop_w > 0 && crop_h > 0 && u32(crop_w) <= img.width && u32(crop_h) <= img.height) {
                eng::Image c;
                c.width = u32(crop_w);
                c.height = u32(crop_h);
                c.rgba.resize(size_t(crop_w) * crop_h * 4);
                for (int y = 0; y < crop_h; ++y)
                    std::memcpy(&c.rgba[size_t(y) * crop_w * 4], &img.rgba[size_t(y) * img.width * 4], size_t(crop_w) * 4);
                img = std::move(c);
            }
            tex = upload_image(*device_, img, true);
            if (tex) pic = {tex->ui_id(), float(img.width), float(img.height)};
        }
    }
    pictures_[id] = {tex, pic};
    return pic;
}

Picture Atlas::glow_picture(sf::Pack pack, std::string_view key) {
    const std::string id = std::string(sf::pack_folder(pack)) + "|glow|" + sf::lower(key);
    if (auto it = pictures_.find(id); it != pictures_.end()) return it->second.second;
    Picture pic;
    eng::TextureRef tex;
    eng::Image img;
    if (device_ && data_)
        if (auto bytes = data_->read(pack, key); bytes && sf::decode_image(*bytes, img)) {
            // Drawn additively in the original: black is nothing, and brightness is how much.
            for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
                eng::u8* p = &img.rgba[i];
                const int a = std::max({p[0], p[1], p[2]});
                if (a == 0) {
                    p[3] = 0;
                    continue;
                }
                for (int c = 0; c < 3; ++c) p[c] = eng::u8(std::min(255, int(p[c]) * 255 / a));
                p[3] = eng::u8(a);
            }
            tex = upload_image(*device_, img, true);
            if (tex) pic = {tex->ui_id(), float(img.width), float(img.height)};
        }
    pictures_[id] = {tex, pic};
    return pic;
}

Picture Atlas::image_picture(const std::string& id, const eng::Image& img) {
    const std::string key = "image|" + id;
    Picture pic;
    eng::TextureRef tex;
    if (device_ && img.width && img.height) {
        tex = upload_image(*device_, img, false);
        if (tex) pic = {tex->ui_id(), float(img.width), float(img.height)};
    }
    pictures_[key] = {tex, pic};
    return pic;
}

Picture Atlas::file_picture(const std::filesystem::path& file) {
    const std::string id = "file|" + eng::str::narrow(file.wstring());
    if (auto it = pictures_.find(id); it != pictures_.end()) return it->second.second;
    Picture pic;
    eng::TextureRef tex;
    if (device_) {
        eng::Image img;
        const auto bytes = eng::fs::read_file(file);
        if (bytes && sf::decode_image(std::as_bytes(std::span(*bytes)), img)) {
            tex = upload_image(*device_, img, true);
            if (tex) pic = {tex->ui_id(), float(img.width), float(img.height)};
        }
    }
    pictures_[id] = {tex, pic};
    return pic;
}

Picture Atlas::map_picture(std::string_view level_id, std::string_view hint) {
    if (!hint.empty())
        if (Picture p = picture(sf::Pack::Lobby, hint); p.valid()) return p;
    std::string id = sf::lower(level_id);
    static const std::pair<const char*, const char*> kAliases[] = {
        {"toy", "sf_m_toytower.jpg"}, {"artcenter", "sf_m_art center.jpg"}, {"nervegas", "sf_m_nervegas.jpg"},
        {"nervegashorror", "sf_m_nervegashorror.jpg"}, {"desertcamp", "sf_m_desertcamp.jpg"}, {"all", "sf_m_allrandom.jpg"},
    };
    for (const auto& [from, to] : kAliases)
        if (id == from)
            if (Picture p = picture(sf::Pack::Lobby, to); p.valid()) return p;
    return picture(sf::Pack::Lobby, "sf_m_" + id + ".jpg");
}

Picture Atlas::loading_picture(std::string_view load_image, std::string_view level_id) {
    if (data_ && !load_image.empty()) {
        if (auto where = data_->find_leaf(sf::Pack::Menu, load_image, "load/"))
            if (Picture p = picture(sf::Pack::Menu, data_->key(sf::Pack::Menu, *where)); p.valid()) return p;
    }
    if (data_) {
        const std::string guess = "sf_l_" + sf::lower(level_id) + ".jpg";
        if (auto where = data_->find_leaf(sf::Pack::Menu, guess, "load/"))
            if (Picture p = picture(sf::Pack::Menu, data_->key(sf::Pack::Menu, *where)); p.valid()) return p;
    }
    return {};
}

Picture Atlas::rank_badge(int rank) {
    // The insignia the lists show (a chevron for a Private): 16x16, one per rank, in the menu
    // archives. (The lobby's Division_N.bmp are unit patches, not ranks.)
    return picture(sf::Pack::Menu, eng::str::format("inf/class/%d.bmp", std::clamp(rank, 0, 74)));
}

Picture Atlas::clan_piece(sf::MarkLayer layer, int index) {
    const std::string id = eng::str::format("clan|%d|%d", int(layer), index);
    if (auto it = pictures_.find(id); it != pictures_.end()) return it->second.second;
    Picture pic;
    eng::TextureRef tex;
    if (device_ && clan_marks_)
        if (auto img = clan_marks_->image(layer, index)) {
            tex = upload_image(*device_, *img, true);
            if (tex) pic = {tex->ui_id(), float(img->width), float(img->height)};
        }
    pictures_[id] = {tex, pic};
    return pic;
}

void Atlas::release_pictures() { pictures_.clear(); }

}  // namespace lsf::ui
