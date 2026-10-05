// The original client's kill effects, read from its own table (data/menu inf/killeffect/
// killimage.txt, 2009): which pictures a kill of yours puts at the top of the screen, where, how
// big, for how long and with what sound.
//
// The table has two halves. An effect (<HEADSHOT> ... <END>) names an icon and a text by number
// and a sound; an image ([HEADSHOT_IMAGE] ... [END]) gives a number its place on the screen (X_POS,
// Y_POS: 0..1, the picture's middle), its size in pixels of the 1024x768 screen, how long it shows
// and fades, how it plays (FADE_STATE: 2 shown and faded out, 7 its pictures a tenth of a second
// each and then held) and its pictures. [BASIC] holds what an image does not say for itself.
#pragma once

#include "SF/Data.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lsf {

struct KillImage {
    float x = 0.5f, y = 0.28f;      // the middle, as a share of the screen
    float w = 94, h = 94;           // pixels of a 768-line screen
    float show = 1.8f, fade_out = 0.1f;
    int state = 2;                  // FADE_STATE
    std::vector<std::string> textures;   // archive keys ("inf/killeffect/icon_headshot1.tga")
};

struct KillEffect {
    std::string name;               // "HEADSHOT", "Z_DOUBLE", "P_NORMAL"
    int image = -1, text = -1;      // KillImage numbers (-1: none)
    int sound_kind = -1, sound = -1;
};

class KillEffects {
public:
    // Reads the table; where it is missing (or does not read), the handful the game cannot do
    // without is made up from the art's own names.
    void load(const sf::Data& data);
    bool loaded() const { return loaded_; }
    const KillEffect* find(std::string_view name) const;
    const KillImage* image(int index) const;
    size_t effects() const { return effects_.size(); }
    size_t images() const { return images_.size(); }

private:
    bool parse(std::string_view text, const sf::Data& data);
    std::vector<KillEffect> effects_;
    std::vector<KillImage> images_;   // by INDEX
    bool loaded_ = false;
};

}  // namespace lsf
