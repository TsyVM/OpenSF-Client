#include "Game/PackMount.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Strings.hpp"
#include "SF/Level.hpp"
#include "SF/Model.hpp"
#include "sf1/data/sff.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace lsf::pack {

using eng::json::Value;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::string stem_of_leaf(std::string_view leaf) {
    const size_t dot = leaf.rfind('.');
    return std::string(dot == std::string_view::npos ? leaf : leaf.substr(0, dot));
}

}  // namespace

sf::Pack data_pack(Library l) {
    switch (l) {
        case Library::Weapon: return sf::Pack::Weapon;
        case Library::Force: return sf::Pack::Force;
        case Library::Sound: return sf::Pack::Sound;
        case Library::Effect: return sf::Pack::Effect;
        case Library::Lobby: return sf::Pack::Lobby;
        case Library::Menu: return sf::Pack::Menu;
        default: return sf::Pack::Area;
    }
}

void BaseIndex::build(const sf::Data& data) {
    for (size_t i = 0; i < size_t(Library::Count); ++i) {
        same_[i].clear(), stem_[i].clear();
        const sf::Pack pack = data_pack(Library(i));
        for (const auto& [key, where] : data.keys(pack, "", "")) {
            if (data.in_session(pack, where)) continue;
            const std::string l = sf::leaf_of(key);
            ++same_[i][l];
            if (is_picture_name(l)) ++stem_[i][stem_of_leaf(l)];
        }
    }
    built_ = true;
}

BaseNames BaseIndex::names(Library l, std::string_view leaf) const {
    BaseNames out;
    const size_t i = size_t(l);
    if (i >= same_.size()) return out;
    const std::string name = sf::lower(leaf);
    if (auto it = same_[i].find(name); it != same_[i].end()) out.same = it->second;
    if (auto it = stem_[i].find(stem_of_leaf(name)); it != stem_[i].end()) out.stem = it->second;
    return out;
}

bool load_bytes(std::vector<u8> bytes, Loaded& out, std::string* why) {
    out = Loaded{};
    if (!read(bytes, out.pack, why)) return false;
    if (!check_files(out.pack, bytes, why)) return false;
    out.sha256 = sha256_hex(bytes);
    out.bytes = std::move(bytes);
    return true;
}

bool load_file(const std::filesystem::path& file, Loaded& out, std::string* why) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return fail(why, "it cannot be read");
    if (size > kMaxPackBytes) return fail(why, "a pack over 50 MB (DL-6)");
    auto bytes = eng::fs::read_file(file);
    if (!bytes) return fail(why, "it cannot be read");
    return load_bytes(std::move(*bytes), out, why);
}

bool check_set(const std::vector<Loaded>& packs, const BaseLookup& base, std::string* why) {
    u64 total = 0;
    std::set<std::string> ids, taken;
    for (const Loaded& l : packs) {
        const std::string& id = l.pack.id;
        total += l.bytes.size();
        // DL-6: 50 MB of packs, all together.
        if (total > kMaxPackBytes) return fail(why, "the packs come to more than 50 MB together (DL-6)");
        if (!ids.insert(id).second) return fail(why, "a second pack with the id " + id + " (NM-9)");
        Value defs;
        std::string bad;
        if (!eng::json::parse(l.pack.definitions, defs)) return fail(why, id + ": its definitions cannot be read");
        if (!validate(l.pack, defs, base, taken, &bad)) return fail(why, id + ": " + bad);
    }
    return true;
}

void remove_layer(const std::filesystem::path& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

bool write_layer(const std::filesystem::path& dir, const std::vector<Loaded>& packs, std::string* why) {
    if (dir.empty()) return fail(why, "there is nowhere to lay the packs out");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ec.clear();
    std::filesystem::create_directories(dir, ec);
    if (ec) return fail(why, "the packs' folder cannot be made");
    // One archive a pack and a library: x001.sff, x002.sff ... in the order the manifest lists them.
    for (size_t n = 0; n < packs.size(); ++n) {
        const Loaded& l = packs[n];
        for (size_t lib = 0; lib < size_t(Library::Count); ++lib) {
            std::vector<sf1::data::sff::NewEntry> entries;
            for (const Entry& e : l.pack.entries) {
                if (size_t(e.library) != lib) continue;
                const std::span<const u8> d = file_data(l.bytes, e);
                if (d.size() != e.size) return fail(why, l.pack.id + ": a file that runs past the pack's end");
                sf1::data::sff::NewEntry ne;
                // An archive's names are the client's own way round: backslashes (SC-3: the key was
                // checked when the pack was read, and is a name inside the archive, never a path).
                ne.name = e.key;
                std::replace(ne.name.begin(), ne.name.end(), '/', '\\');
                ne.payload = std::span<const std::byte>(reinterpret_cast<const std::byte*>(d.data()), d.size());
                entries.push_back(std::move(ne));
            }
            if (entries.empty()) continue;
            auto archive = sf1::data::sff::write(entries);
            if (!archive) return fail(why, l.pack.id + ": its files cannot be laid out: " + std::string(sf1::describe(archive.error())));
            const std::filesystem::path folder = dir / library_name(Library(lib));
            std::filesystem::create_directories(folder, ec);
            const std::filesystem::path file = folder / eng::str::format("x%03zu.sff", n + 1);
            if (!eng::fs::write_file(file, archive->data(), archive->size())) return fail(why, l.pack.id + ": its files cannot be written: is the disk full?");
        }
    }
    return true;
}

bool measure_force(const sf::Data& data, std::string_view folder, ForceMeasure& out, std::string* why) {
    out = ForceMeasure{};
    const std::string want = sf::lower(folder);
    for (const sf::ForceSet& s : sf::discover_forces(data)) {
        if (s.folder != want) continue;
        std::string bad;
        auto model = sf::load_force(data, s, "", &bad);
        if (!model) return fail(why, bad.empty() ? "its model cannot be read" : bad);
        out.bones = u32(model->bones.size());
        eng::Aabb box;
        for (const sf::ModelMesh& m : model->meshes)
            if (m.bounds.valid()) box.add(m.bounds);
        if (!box.valid()) return fail(why, "its model has no shape");
        out.height = box.max.y - box.min.y;
        out.width = std::max(box.max.x - box.min.x, box.max.z - box.min.z);
        if (!std::isfinite(out.height) || !std::isfinite(out.width)) return fail(why, "its model has no size that can be measured");
        for (const char* clip : {"l_us", "l_ud", "l_ds", "l_urf_01", "l_urb_01", "l_uwf_01", "l_dwf_01", "l_uj_00", "l_uj_01"})
            if (auto a = sf::load_character_motion(data, *model, clip); a && !a->tracks.empty()) out.clips.push_back(clip);
        return true;
    }
    return fail(why, "no character's pieces are in " + want + " (a rig sf_c_<name>_bone and its pieces)");
}

bool check_mounted(const sf::Data& data, const SessionContent& content, std::string* why) {
    // Guns: one of the pack's own is a folder the weapon reader makes a gun of.
    std::vector<sf::WeaponSet> guns;
    bool guns_read = false;
    for (const WeaponDef& w : content.weapons) {
        if (!w.model.starts_with("x/")) continue;   // a base gun's model, by reference (PK-8)
        if (!guns_read) guns = sf::discover_weapons(data), guns_read = true;
        const auto it = std::find_if(guns.begin(), guns.end(), [&](const sf::WeaponSet& s) { return s.id == w.model; });
        if (it == guns.end()) return fail(why, w.code + ": its folder holds no gun (sf_a_g_<name>.lma, its rig and hands)");
        std::string bad;
        if (!sf::load_weapon(data, *it, 0, &bad)) return fail(why, w.code + ": its model cannot be read: " + bad);
    }
    for (const PackForce& f : content.forces) {
        ForceMeasure m;
        std::string bad;
        if (!measure_force(data, f.model, m, &bad)) return fail(why, f.code + ": " + bad);
        if (m.bones == 0 || m.bones > kMaxBones) return fail(why, f.code + ": its skeleton has more than 128 bones, or none");
        if (m.height < kMinForceHeight || m.height > kMaxForceHeight || m.width < kMinForceWidth || m.width > kMaxForceWidth)
            return fail(why, f.code + eng::str::format(": %.0f cm tall and %.0f wide: much bigger or smaller than every soldier's hitboxes (PK-6)", double(m.height), double(m.width)));
        if (m.clips.size() < 9) return fail(why, f.code + ": its skeleton cannot play every clip the animation code plays (§11.5)");
    }
    if (!content.maps.empty()) {
        const auto levels = sf::list_levels(data);
        for (const PackMap& m : content.maps) {
            const auto it = std::find_if(levels.begin(), levels.end(), [&](const sf::LevelListing& l) { return l.id == m.id; });
            if (it == levels.end() || !it->key.starts_with(m.folder + "/")) return fail(why, m.id + ": its map is not among its pack's files");
            if (!it->playable) return fail(why, m.id + ": it has no world script (sf_m_<name>.xml): nothing could be played on it");
        }
    }
    return true;
}

bool mount_session(const std::vector<std::filesystem::path>& files, const Value& manifest, sf::Data& data, const BaseIndex& base, const std::filesystem::path& layer,
                   u32 max_texture, SessionContent& out, std::string* why) {
    out = SessionContent{};
    std::vector<Loaded> set;
    std::string bad;
    for (const std::filesystem::path& file : files) {
        Loaded l;
        if (!load_file(file, l, &bad)) return fail(why, "One of this server's packs is broken: " + bad);
        // PF-4: a phone's renderer takes smaller pictures than a desktop's.
        if (max_texture < kMaxTexture && !check_files(l.pack, l.bytes, &bad, max_texture)) return fail(why, "This server's packs are made for a desktop game: " + bad);
        set.push_back(std::move(l));
    }
    if (!check_set(set, base.built() ? base.lookup() : BaseLookup(), &bad)) return fail(why, "This server's packs break the rules: " + bad);
    std::vector<std::pair<Pack, std::string>> named;
    std::vector<u32> sizes;
    for (const Loaded& l : set) named.emplace_back(l.pack, l.sha256), sizes.push_back(u32(l.bytes.size()));
    const Value mine = pack::manifest(named, sizes);
    if (!same_json(mine, manifest)) return fail(why, "This server's manifest does not describe its packs.");
    if (!registry_from_manifest(mine, out, &bad)) return fail(why, "This server's packs break the rules: " + bad);
    bool ok = write_layer(layer, set, &bad);
    if (ok) {
        data.set_session(layer);
        ok = check_mounted(data, out, &bad);
    }
    if (!ok) {
        data.set_session({});
        remove_layer(layer);
        out = SessionContent{};
        return fail(why, "This server's packs cannot be used: " + bad);
    }
    return true;
}

bool same_json(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Value::Type::Null: return true;
        case Value::Type::Bool: return a.as_bool() == b.as_bool();
        case Value::Type::Number: return a.as_double() == b.as_double() && a.as_int() == b.as_int();
        case Value::Type::String: return a.string_ref() == b.string_ref();
        case Value::Type::Array: {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (!same_json(a[i], b[i])) return false;
            return true;
        }
        case Value::Type::Object: {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.items().size(); ++i)
                if (a.items()[i].first != b.items()[i].first || !same_json(a.items()[i].second, b.items()[i].second)) return false;
            return true;
        }
    }
    return false;
}

}  // namespace lsf::pack
