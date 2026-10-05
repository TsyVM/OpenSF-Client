#include "SF/SoundTables.hpp"

#include "SF/Tables.hpp"

#include "Engine/Core/Log.hpp"

#include <cctype>
#include <cstdlib>

namespace sf {

namespace {

// The value of attribute `name` in the tag text `tag`, or "".
std::string attribute(std::string_view tag, std::string_view name) {
    const std::string key = std::string(name) + "=\"";
    const size_t at = tag.find(key);
    if (at == std::string_view::npos) return {};
    const size_t start = at + key.size(), end = tag.find('"', start);
    return end == std::string_view::npos ? std::string() : std::string(tag.substr(start, end - start));
}

float to_float(const std::string& s, float fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end != s.c_str() ? v : fallback;
}

// A name the archives would hold it under: forward slashes, lower case.
std::string archive_key(std::string_view file) {
    std::string k = lower(file);
    for (char& c : k)
        if (c == '\\') c = '/';
    return k;
}

std::string squash(std::string_view s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

const char* SoundTables::material_name(u8 id) {
    switch (id) {
        case 0: return "Soil";
        case 1: return "Metal";
        case 2: return "Grass";
        case 3: return "Water";
        case 4: return "Rock";
        case 5: return "Wood";
        case 6: return "Snow";
        case 7: return "Mud";
        case 8: return "Concret";
        case 9: return "Glass";
        case 11: return "Fabric";
        case 12: return "Sand";
        default: return "Default";
    }
}

bool SoundTables::load(const Data& data, std::string* error) {
    sounds_.clear();
    weapons_.clear();
    by_code_.clear(), by_bhw_.clear(), by_name_.clear();

    // SFSound.xml: <Sound Name=... DistanceMin=... DistanceMax=... Volume=... LoopCount=... SoundType=...>
    //                <SoundOnMaterial MaterialName=...><SoundFileOnMaterial FileName=.../>...
    if (auto bytes = data.read(Pack::Sound, "sfsound.xml")) {
        const std::string xml = cp949_to_utf8(*bytes);
        for (size_t pos = xml.find("<Sound Name="); pos != std::string::npos; pos = xml.find("<Sound Name=", pos + 1)) {
            const size_t tag_end = xml.find('>', pos);
            const size_t close = xml.find("</Sound>", pos);
            if (tag_end == std::string::npos || close == std::string::npos) break;
            const std::string_view tag(xml.data() + pos, tag_end - pos);
            SoundDef def;
            def.name = attribute(tag, "Name");
            def.min = to_float(attribute(tag, "DistanceMin"), 0);
            def.max = to_float(attribute(tag, "DistanceMax"), 0);
            def.volume = to_float(attribute(tag, "Volume"), 1);
            def.positional = attribute(tag, "SoundType") != "2D";
            def.loop = attribute(tag, "LoopCount") == "0";
            const std::string_view body(xml.data() + tag_end, close - tag_end);
            for (size_t m = body.find("<SoundOnMaterial"); m != std::string_view::npos; m = body.find("<SoundOnMaterial", m + 1)) {
                const size_t mend = body.find("</SoundOnMaterial>", m);
                const std::string_view block = body.substr(m, mend == std::string_view::npos ? std::string_view::npos : mend - m);
                auto& list = def.files[lower(attribute(block, "MaterialName"))];
                for (size_t f = block.find("FileName="); f != std::string_view::npos; f = block.find("FileName=", f + 1))
                    if (std::string file = attribute(block.substr(f), "FileName"); !file.empty()) list.push_back(archive_key(file));
            }
            if (!def.name.empty()) sounds_[lower(def.name)] = std::move(def);
        }
    }
    if (sounds_.empty()) {
        if (error) *error = "SFSound.xml is missing or empty";
        return false;
    }

    // Weapon.kst.
    std::string err;
    if (auto sheet = load_kst(data, "weapon.kst", &err)) {
        const int c_code = sheet->column("CODE"), c_name = sheet->column("NAME"), c_bhw = sheet->column("BHW");
        auto sound_at = [&](size_t row, const char* prefix) {
            WeaponSound s;
            const std::string base(prefix);
            const std::string file = sheet->cell(row, sheet->column(base + "_SOUND_NAME"));
            if (file.empty()) return s;
            s.file = "weapon/" + archive_key(file);
            s.min = sheet->number(row, sheet->column(base + "_SND_MIN"), 5);
            s.max = sheet->number(row, sheet->column(base + "_SND_MAX"), 30);
            s.volume = sheet->number(row, sheet->column(base + "_SND_VOL"), 1);
            return s;
        };
        for (size_t r = 0; r < sheet->rows.size(); ++r) {
            WeaponSounds w;
            w.code = sheet->cell(r, c_code);
            w.name = sheet->cell(r, c_name);
            w.bhw = lower(sheet->cell(r, c_bhw));
            if (w.code.empty()) continue;
            w.number = std::atoi(w.code.c_str() + 1);
            w.fire = sound_at(r, "ATTACK");
            w.draw = sound_at(r, "APEAR");
            w.rolling = sound_at(r, "ROLLING");
            for (const char* stage : {"RELOAD", "RELOAD2", "RELOAD3", "RELOAD4", "RELOAD5"})
                if (WeaponSound s = sound_at(r, stage); s.valid()) w.reload.push_back(std::move(s));
            weapons_.push_back(std::move(w));
        }
        for (size_t i = 0; i < weapons_.size(); ++i) {
            const WeaponSounds& w = weapons_[i];
            by_code_.try_emplace(lower(w.code), i);
            auto keep_lowest = [&](std::map<std::string, size_t>& index, const std::string& key) {
                if (key.empty() || !w.fire.valid()) return;
                auto [it, fresh] = index.try_emplace(key, i);
                if (!fresh && weapons_[it->second].number > w.number) it->second = i;
            };
            keep_lowest(by_bhw_, w.bhw);
            keep_lowest(by_name_, squash(w.name));
        }
    } else {
        LOG_WARN("Sounds: Weapon.kst: %s", err.c_str());
    }

    LOG_INFO("Sounds: %zu named sounds, %zu weapon rows", sounds_.size(), weapons_.size());
    return true;
}

const SoundDef* SoundTables::sound(std::string_view name) const {
    auto it = sounds_.find(lower(name));
    return it == sounds_.end() ? nullptr : &it->second;
}

const std::vector<std::string>* SoundTables::files(std::string_view name, std::string_view key) const {
    const SoundDef* def = sound(name);
    if (!def) return nullptr;
    if (auto it = def->files.find(lower(key)); it != def->files.end() && !it->second.empty()) return &it->second;
    if (auto it = def->files.find("default"); it != def->files.end() && !it->second.empty()) return &it->second;
    return nullptr;
}

const WeaponSounds* SoundTables::weapon(std::string_view code, std::string_view model) const {
    const std::string bhw = "sf_a_" + lower(model) + ".sfc";
    if (auto it = by_code_.find(lower(code)); it != by_code_.end() && weapons_[it->second].bhw == bhw && weapons_[it->second].fire.valid())
        return &weapons_[it->second];
    if (auto it = by_bhw_.find(bhw); it != by_bhw_.end()) return &weapons_[it->second];
    // A variant's skin over a base gun: gold_scar_h fires as the SCAR-H.
    std::string base = lower(model);
    for (const char* prefix : {"gold_", "camo_", "alcad_", "engraving_", "limited_", "evl_"})
        if (base.starts_with(prefix)) base.erase(0, std::char_traits<char>::length(prefix));
    for (const std::string& guess : {"sf_a_" + base + ".sfc", "sf_a_evl_" + base + ".sfc"})
        if (auto it = by_bhw_.find(guess); it != by_bhw_.end()) return &weapons_[it->second];
    return nullptr;
}

const WeaponSounds* SoundTables::weapon_named(std::string_view name) const {
    auto it = by_name_.find(squash(name));
    return it == by_name_.end() ? nullptr : &weapons_[it->second];
}

}  // namespace sf
