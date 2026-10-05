// SPDX-License-Identifier: MIT
#include "sf1/data/worldscript.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace sf1::data::script {

namespace {

// A tag scanner, not an XML parser: the shipped files are machine-written, flat,
// and have no namespaces, CDATA, entities or comments. Anything it does not
// recognise it skips, so an unexpected element can never fail a load.
struct Tag {
    std::string_view name;
    std::string_view attributes;
    bool             closing = false;      // </name>
    bool             self_closing = false; // <name .../>
};

bool name_is(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool starts_with_ci(std::string_view a, std::string_view prefix) noexcept {
    return a.size() >= prefix.size() && name_is(a.substr(0, prefix.size()), prefix);
}

// Advances `pos` to just past the next tag and describes it. False at end of input.
bool next_tag(std::string_view text, std::size_t& pos, Tag& out) noexcept {
    const auto open = text.find('<', pos);
    if (open == std::string_view::npos) return false;
    const auto close = text.find('>', open + 1);
    if (close == std::string_view::npos) return false;

    std::string_view body = text.substr(open + 1, close - open - 1);
    pos = close + 1;

    out = {};
    if (!body.empty() && (body.front() == '?' || body.front() == '!')) {
        out.name = {};
        return true;   // declaration or doctype: nothing to do, keep scanning
    }
    if (!body.empty() && body.front() == '/') {
        out.closing = true;
        body.remove_prefix(1);
    }
    if (!body.empty() && body.back() == '/') {
        out.self_closing = true;
        body.remove_suffix(1);
    }

    std::size_t i = 0;
    while (i < body.size() && !std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    out.name = body.substr(0, i);
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    out.attributes = body.substr(i);
    return true;
}

// The value of `key="..."` in an attribute run, or an empty view.
std::string_view attribute(std::string_view attributes, std::string_view key) noexcept {
    std::size_t i = 0;
    while (i < attributes.size()) {
        while (i < attributes.size() && std::isspace(static_cast<unsigned char>(attributes[i]))) ++i;
        const std::size_t name_start = i;
        while (i < attributes.size() && attributes[i] != '=' && !std::isspace(static_cast<unsigned char>(attributes[i])))
            ++i;
        const std::string_view name = attributes.substr(name_start, i - name_start);
        while (i < attributes.size() && std::isspace(static_cast<unsigned char>(attributes[i]))) ++i;
        if (i >= attributes.size() || attributes[i] != '=') {
            if (name.empty()) break;
            continue;
        }
        ++i;
        while (i < attributes.size() && std::isspace(static_cast<unsigned char>(attributes[i]))) ++i;
        if (i >= attributes.size()) break;
        const char quote = attributes[i];
        if (quote != '"' && quote != '\'') break;
        ++i;
        const std::size_t value_start = i;
        while (i < attributes.size() && attributes[i] != quote) ++i;
        const std::string_view value = attributes.substr(value_start, i - value_start);
        if (i < attributes.size()) ++i;
        if (name_is(name, key)) return value;
    }
    return {};
}

float number(std::string_view text, float fallback = 0.0f) noexcept {
    if (text.empty()) return fallback;
    // from_chars for float is not available everywhere the SDK builds; the values
    // here are short plain decimals, so strtof over a small stack copy is enough.
    char buffer[64];
    const std::size_t n = std::min(text.size(), sizeof buffer - 1);
    std::copy_n(text.data(), n, buffer);
    buffer[n] = '\0';
    char* end = nullptr;
    const float value = std::strtof(buffer, &end);
    return end == buffer ? fallback : value;
}

int integer(std::string_view text, int fallback = 0) noexcept {
    if (text.empty()) return fallback;
    int value = fallback;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    if (std::from_chars(first, last, value).ec != std::errc{}) return static_cast<int>(number(text, static_cast<float>(fallback)));
    return value;
}

std::string text_of(std::string_view text, std::size_t from, std::string_view closing) noexcept {
    const auto end = text.find('<', from);
    if (end == std::string_view::npos) return {};
    (void)closing;
    std::string_view body = text.substr(from, end - from);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front()))) body.remove_prefix(1);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back()))) body.remove_suffix(1);
    return std::string(body);
}

std::array<float, 3> xyz(std::string_view attributes, const char* x, const char* y, const char* z) noexcept {
    return {number(attribute(attributes, x)), number(attribute(attributes, y)), number(attribute(attributes, z))};
}

Spawn read_spawn(std::string_view a) {
    Spawn s;
    s.sector = std::string(attribute(a, "SectorName"));
    s.position = xyz(a, "X", "Y", "Z");
    s.angle = number(attribute(a, "Angle"));
    return s;
}

ClanMark read_mark(std::string_view a) {
    ClanMark m;
    m.sector = std::string(attribute(a, "SectorName"));
    m.position = xyz(a, "X", "Y", "Z");
    return m;
}

}  // namespace

std::string_view WorldScript::label_for(std::string_view sector) const noexcept {
    for (const auto& l : sector_labels)
        if (name_is(l.sector, sector)) return l.english.empty() ? l.localized : l.english;
    return {};
}

Result<WorldScript> read(std::string_view text) noexcept {
    WorldScript out;
    out.size = text.size();

    if (text.find("<SFWorldScript") == std::string_view::npos) return err(Error::BadMagic);

    // Which <Spawn*> or <ClanMark*> block the scanner is inside, if any.
    enum class Block { None, Personal, Red, Blue, Npc, Ammobox, MarkRed, MarkBlue, MarkWater, ZmZombie, ZmHuman, Target };
    Block block = Block::None;
    int ppl_group = 0;

    std::size_t pos = 0;
    Tag tag;
    while (next_tag(text, pos, tag)) {
        if (tag.name.empty()) continue;

        if (tag.closing) {
            if (starts_with_ci(tag.name, "Spawn") || starts_with_ci(tag.name, "ClanMark") ||
                name_is(tag.name, "WaterMark") || name_is(tag.name, "NpcSpawn") ||
                name_is(tag.name, "AmmoboxSpawn"))
                block = Block::None;
            if (starts_with_ci(tag.name, "PPLObjectInfo")) ppl_group = 0;
            continue;
        }

        // Scalars -------------------------------------------------------------
        if (name_is(tag.name, "ObjectiveType")) { out.objective = text_of(text, pos, tag.name); continue; }
        if (name_is(tag.name, "WorldFileName")) { out.world_file = text_of(text, pos, tag.name); continue; }
        if (name_is(tag.name, "LoadImage"))     { out.load_image = text_of(text, pos, tag.name); continue; }

        // Block openers -------------------------------------------------------
        if (name_is(tag.name, "SpawnPersonal"))   { block = tag.self_closing ? Block::None : Block::Personal; continue; }
        if (name_is(tag.name, "SpawnTeam_Red"))   { block = tag.self_closing ? Block::None : Block::Red; continue; }
        if (name_is(tag.name, "SpawnTeam_Blue"))  { block = tag.self_closing ? Block::None : Block::Blue; continue; }
        if (name_is(tag.name, "NpcSpawn"))        { block = tag.self_closing ? Block::None : Block::Npc; continue; }
        if (name_is(tag.name, "AmmoboxSpawn"))    { block = tag.self_closing ? Block::None : Block::Ammobox; continue; }
        if (name_is(tag.name, "ClanMark_Red"))    { block = tag.self_closing ? Block::None : Block::MarkRed; continue; }
        if (name_is(tag.name, "ClanMark_Blue"))   { block = tag.self_closing ? Block::None : Block::MarkBlue; continue; }
        if (name_is(tag.name, "WaterMark"))       { block = tag.self_closing ? Block::None : Block::MarkWater; continue; }
        if (name_is(tag.name, "SpawnZM2_Zombie")) { block = tag.self_closing ? Block::None : Block::ZmZombie; continue; }
        if (name_is(tag.name, "SpawnZM2_Human"))  { block = tag.self_closing ? Block::None : Block::ZmHuman; continue; }
        if (name_is(tag.name, "SpawnTarget"))     { block = tag.self_closing ? Block::None : Block::Target; continue; }
        if (starts_with_ci(tag.name, "PPLObjectInfo")) {
            const auto underscore = tag.name.rfind('_');
            ppl_group = underscore == std::string_view::npos ? 0 : integer(tag.name.substr(underscore + 1));
            continue;
        }

        // Rows ----------------------------------------------------------------
        if (name_is(tag.name, "SpawnData") && block == Block::Target) {
            Target t;
            t.a = xyz(tag.attributes, "X1", "Y1", "Z1");
            t.b = xyz(tag.attributes, "X2", "Y2", "Z2");
            t.angle = number(attribute(tag.attributes, "Angle"));
            out.targets.push_back(t);
            continue;
        }
        if (name_is(tag.name, "NpcSpawnPos"))     { out.npc_spawn.push_back(read_spawn(tag.attributes)); continue; }
        if (name_is(tag.name, "AmmoboxSpawnPos")) { out.ammobox_spawn.push_back(read_spawn(tag.attributes)); continue; }
        if (name_is(tag.name, "EvacuationData")) {
            Evacuation e;
            e.team = std::string(attribute(tag.attributes, "Team"));
            e.position = xyz(tag.attributes, "PosX", "PosY", "PosZ");
            e.radius = number(attribute(tag.attributes, "Radius"));
            out.evacuations.push_back(std::move(e));
            continue;
        }
        if (name_is(tag.name, "WaterMarkPos")) {
            Location l;
            l.name = std::string(attribute(tag.attributes, "Location"));
            l.position = xyz(tag.attributes, "X", "Y", "Z");
            l.radius = number(attribute(tag.attributes, "Radius"));
            out.locations.push_back(std::move(l));
            continue;
        }
        if (name_is(tag.name, "SpawnData")) {
            Spawn s = read_spawn(tag.attributes);
            switch (block) {
                case Block::Personal: out.spawn_personal.push_back(std::move(s)); break;
                case Block::Red:      out.spawn_red.push_back(std::move(s)); break;
                case Block::Blue:     out.spawn_blue.push_back(std::move(s)); break;
                case Block::Npc:      out.npc_spawn.push_back(std::move(s)); break;
                case Block::Ammobox:  out.ammobox_spawn.push_back(std::move(s)); break;
                case Block::ZmZombie: out.zm2_zombie.push_back(std::move(s)); break;
                case Block::ZmHuman:  out.zm2_human.push_back(std::move(s)); break;
                default: break;
            }
            continue;
        }
        if (starts_with_ci(tag.name, "ClanMarkData_Red"))  { out.clan_marks_red.push_back(read_mark(tag.attributes)); continue; }
        if (starts_with_ci(tag.name, "ClanMarkData_Blue")) { out.clan_marks_blue.push_back(read_mark(tag.attributes)); continue; }
        if (starts_with_ci(tag.name, "WaterMarkData"))     { out.water_marks.push_back(read_mark(tag.attributes)); continue; }

        if (name_is(tag.name, "MissionObjectData")) {
            MissionObject m;
            m.type = std::string(attribute(tag.attributes, "Type"));
            m.file = std::string(attribute(tag.attributes, "FileName"));
            m.sector = std::string(attribute(tag.attributes, "SectorName"));
            m.position = xyz(tag.attributes, "PosX", "PosY", "PosZ");
            m.angles = xyz(tag.attributes, "AngleX", "AngleY", "AngleZ");
            out.mission_objects.push_back(std::move(m));
            continue;
        }
        if (starts_with_ci(tag.name, "PPLObjectData")) {
            PplObject p;
            p.group = ppl_group;
            p.file = std::string(attribute(tag.attributes, "FileName"));
            p.sector = std::string(attribute(tag.attributes, "SectorName"));
            p.position = xyz(tag.attributes, "PosX", "PosY", "PosZ");
            p.angles = xyz(tag.attributes, "AngleX", "AngleY", "AngleZ");
            out.ppl_objects.push_back(std::move(p));
            continue;
        }
        if (name_is(tag.name, "SoundData")) {
            SoundEmitter s;
            s.file = std::string(attribute(tag.attributes, "FileName"));
            s.sector = std::string(attribute(tag.attributes, "SectorName"));
            s.position = xyz(tag.attributes, "PosX", "PosY", "PosZ");
            s.volume = number(attribute(tag.attributes, "Volume"));
            s.effect_volume = number(attribute(tag.attributes, "EffectVolume"));
            s.min_distance = number(attribute(tag.attributes, "MinDist"));
            s.max_distance = number(attribute(tag.attributes, "MaxDist"));
            s.loop_type = integer(attribute(tag.attributes, "LoopType"));
            s.delay_time = integer(attribute(tag.attributes, "DelayTime"));
            s.third = integer(attribute(tag.attributes, "Third"));
            out.sounds.push_back(std::move(s));
            continue;
        }
        if (name_is(tag.name, "SoundEffectData")) {
            SectorEffect e;
            e.sector = std::string(attribute(tag.attributes, "SectorName"));
            e.code = number(attribute(tag.attributes, "Code"));
            e.volume = number(attribute(tag.attributes, "Volume"));
            out.sector_effects.push_back(std::move(e));
            continue;
        }
        if (name_is(tag.name, "SectorInfoData")) {
            SectorLabel l;
            l.sector = std::string(attribute(tag.attributes, "SectorName"));
            l.english = std::string(attribute(tag.attributes, "EngInfo"));
            l.localized = std::string(attribute(tag.attributes, "LocalizeInfo"));
            out.sector_labels.push_back(std::move(l));
            continue;
        }
        if (name_is(tag.name, "CCTVData") || name_is(tag.name, "CCTVTopView")) {
            Camera c;
            c.position = xyz(tag.attributes, "PosX", "PosY", "PosZ");
            c.angle_y = number(attribute(tag.attributes, "AngleY"));
            c.angle_z = number(attribute(tag.attributes, "AngleZ"));
            c.top_view = name_is(tag.name, "CCTVTopView");
            out.cameras.push_back(c);
            continue;
        }
    }

    return out;
}

Result<WorldScript> read(std::span<const std::byte> bytes) noexcept {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // Skip a UTF-8 byte-order mark if the exporter left one.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    return read(text);
}

}  // namespace sf1::data::script
