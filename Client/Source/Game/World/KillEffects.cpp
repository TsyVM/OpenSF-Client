#include "Game/World/KillEffects.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsf {

namespace {

// "Inf\KillEffect\icon_headshot1.tga" -> "inf/killeffect/icon_headshot1.tga": the archive's keys
// are lower case with forward slashes. (Some of the undead's are named with a space, in the table
// and in the archive alike: "zombie/icon_human kill.tga".)
std::string texture_key(std::string_view path) {
    std::string key = eng::str::lower(eng::str::trim(path));
    for (char& c : key)
        if (c == '\\') c = '/';
    return key;
}

std::string_view strip_quotes(std::string_view v) {
    v = eng::str::trim(v);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

}  // namespace

bool KillEffects::parse(std::string_view text, const sf::Data& data) {
    KillImage basic;
    enum class In { None, Effect, Image, Basic } in = In::None;
    KillEffect effect;
    KillImage image;
    int index = -1;
    bool comment = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = eng::str::trim(text.substr(at, end - at));
        at = end + 1;
        // Comments: /* ... */ over lines of their own (the explanations are Korean), and // lines.
        if (comment) {
            if (line.find("*/") != std::string_view::npos) comment = false;
            continue;
        }
        if (line.starts_with("/*")) {
            comment = line.find("*/") == std::string_view::npos;
            continue;
        }
        if (line.empty() || line.starts_with("//")) continue;
        if (line.front() == '<' && line.back() == '>') {
            const std::string_view name = line.substr(1, line.size() - 2);
            if (name == "END") {
                if (in == In::Effect) effects_.push_back(effect);
                in = In::None;
            } else {
                in = In::Effect;
                effect = KillEffect{};
                effect.name = std::string(name);
            }
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            const std::string_view name = line.substr(1, line.size() - 2);
            if (name == "END") {
                if (in == In::Basic) basic = image;
                if (in == In::Image && index >= 0 && index < 512) {
                    if (size_t(index) >= images_.size()) images_.resize(size_t(index) + 1);
                    images_[size_t(index)] = image;
                }
                in = In::None;
            } else {
                in = name == "BASIC" ? In::Basic : In::Image;
                image = basic;
                image.textures.clear();
                index = -1;
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || in == In::None) continue;
        const std::string_view key = eng::str::trim(line.substr(0, eq));
        const std::string value(strip_quotes(line.substr(eq + 1)));
        const float number = float(std::atof(value.c_str()));
        if (in == In::Effect) {
            if (key == "IMAGE_INDEX") effect.image = int(number);
            else if (key == "TEXT_INDEX") effect.text = int(number);
            else if (key == "KILL_SND_KIND") effect.sound_kind = int(number);
            else if (key == "KILL_SOUND") effect.sound = int(number);
            else if (key == "IMAGE_COUNT" && int(number) <= 0) effect.image = -1;
            else if (key == "TEXT_COUNT" && int(number) <= 0) effect.text = -1;
        } else {
            if (key == "INDEX") index = int(number);
            else if (key == "X_POS") image.x = number;
            else if (key == "Y_POS") image.y = number;
            else if (key == "WIDTH") image.w = number;
            else if (key == "HEIGHT") image.h = number;
            else if (key == "SHOW_TIME") image.show = number;
            else if (key == "FADE_OUT_TIME") image.fade_out = number;
            else if (key == "FADE_STATE") image.state = int(number);
            else if (key == "TEXTURE") {
                // As the table names it; else with its spaces as underscores (a copy renamed so).
                std::string tex = texture_key(value);
                if (!data.resolve(sf::Pack::Menu, tex)) std::replace(tex.begin(), tex.end(), ' ', '_');
                if (data.resolve(sf::Pack::Menu, tex)) image.textures.push_back(tex);
                else LOG_WARN("Kill effects: %s is named by the table and not in the archive", tex.c_str());
            }
        }
    }
    // IMAGE_COUNT / TEXT_COUNT of 0 were read before or after their INDEX: an effect whose count
    // is 0 has no picture whatever its index says (COMMONKILL_CENTERIMAGE's 0 is HEADSHOT's icon).
    return !effects_.empty() && !images_.empty();
}

void KillEffects::load(const sf::Data& data) {
    loaded_ = true;
    effects_.clear();
    images_.clear();
    if (auto bytes = data.read(sf::Pack::Menu, "inf/killeffect/killimage.txt")) {
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (parse(text, data)) {
            LOG_INFO("Kill effects: %zu effects, %zu pictures from the client's killimage.txt", effects_.size(), images_.size());
            return;
        }
    }
    // No table: the classic seven, by the art's names, in the table's own places and sizes.
    LOG_WARN("Kill effects: killimage.txt did not read; using the built-in set");
    effects_.clear();
    images_.clear();
    struct Basic {
        const char* name;
        const char* icon;
        int frames;
        float w, h;
        const char* text;
    };
    static const Basic kBasics[] = {{"HEADSHOT", "icon_headshot", 5, 100, 100, "text_headshot"},
                                    {"KNIFEKILL", "icon_knifekill", 0, 94, 94, "text_knifekill"},
                                    {"GRENADEKILL", "icon_grenadekill", 5, 100, 100, "text_grenadekill"},
                                    {"DOUBLEKILL", "icon_killeffectcenter", 0, 176, 94, "text_killeffectcenter"},
                                    {"MULTIKILL", "icon_multikille", 0, 264, 94, "text_multikille"},
                                    {"SPECIALFORCE", "icon_specialforce", 0, 352, 94, "text_specialforce"},
                                    {"REVENGEKILL_CENTERIMAGE", "icon_revengekill", 0, 94, 94, "text_revengekill"}};
    for (const Basic& b : kBasics) {
        KillImage icon;
        icon.w = b.w, icon.h = b.h;
        icon.state = b.frames ? 7 : 2;
        const std::string stem = std::string("inf/killeffect/") + b.icon;
        if (b.frames)
            for (int f = 1; f <= b.frames; ++f) icon.textures.push_back(stem + std::to_string(f) + ".tga");
        else icon.textures.push_back(stem + ".tga");
        KillImage text;
        text.y = 0.18f, text.w = 200, text.h = 40;
        text.textures.push_back(std::string("inf/killeffect/") + b.text + ".tga");
        KillEffect e;
        e.name = b.name;
        e.image = int(images_.size());
        images_.push_back(icon);
        e.text = int(images_.size());
        images_.push_back(text);
        effects_.push_back(e);
    }
}

const KillEffect* KillEffects::find(std::string_view name) const {
    for (const KillEffect& e : effects_)
        if (e.name == name) return &e;
    return nullptr;
}

const KillImage* KillEffects::image(int index) const { return index >= 0 && size_t(index) < images_.size() ? &images_[size_t(index)] : nullptr; }

}  // namespace lsf
