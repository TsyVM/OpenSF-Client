// Soldier Front textures onto the card: RGBA8 with its mips, or a DDS's DXT blocks as they are.
#pragma once

#include "Engine/Render/Device.hpp"
#include "SF/Image.hpp"

namespace lsf {

inline eng::TextureRef upload_texture(eng::Device& device, const sf::Texture& t) {
    if (t.mips.empty() || t.width == 0 || t.height == 0) return nullptr;
    eng::TextureDesc d;
    d.width = int(t.width);
    d.height = int(t.height);
    d.mips = int(t.mips.size());
    switch (t.kind) {
        case sf::Texture::Kind::Bc1: d.format = eng::Format::BC1; break;
        case sf::Texture::Kind::Bc2: d.format = eng::Format::BC2; break;
        case sf::Texture::Kind::Bc3: d.format = eng::Format::BC3; break;
        default: d.format = eng::Format::RGBA8; break;
    }
    std::vector<eng::TextureData> init(t.mips.size());
    for (size_t m = 0; m < t.mips.size(); ++m) init[m] = {t.mips[m].data(), t.row_pitch(m)};
    return device.create_texture(d, init);
}

inline eng::TextureRef upload_image(eng::Device& device, const eng::Image& img, bool mips = true) {
    return upload_texture(device, sf::texture_from_image(img, mips));
}

}  // namespace lsf
