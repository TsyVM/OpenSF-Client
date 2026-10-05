#include "Game/Pack.hpp"

#include "Engine/Core/ByteStream.hpp"
#include "Engine/Core/Strings.hpp"
#include "Game/Movement.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace lsf::pack {

using eng::json::Value;

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// A u16-length-prefixed string, bounded by what is left (SC-1).
bool read_str(eng::ByteReader& r, std::string& out, size_t max) {
    const u16 n = r.u16();
    if (!r.ok() || n > max || n > r.remaining()) return false;
    out.resize(n);
    if (n) r.raw(out.data(), n);
    return r.ok();
}

void write_str(eng::ByteWriter& w, std::string_view s) {
    w.u16(u16(s.size()));
    w.bytes(std::span<const u8>(reinterpret_cast<const u8*>(s.data()), s.size()));
}

// The grips the arm clips exist for (Rules.hpp WeaponDef::grip).
bool grip_ok(int g) { return (g >= 1 && g <= 7) || g == 9 || g == 10; }

Slot slot_of(WeaponClass c) {
    switch (c) {
        case WeaponClass::Pistol:
        case WeaponClass::Shotgun: return Slot::Secondary;
        case WeaponClass::Knife: return Slot::Melee;
        case WeaponClass::Grenade: return Slot::Throw;
        default: return Slot::Primary;
    }
}

// Every clip the animation code plays on a soldier (§11.5): the legs', and each grip's upper body.
const char* const kRequiredClips[] = {"l_us", "l_ud", "l_ds", "l_urf_01", "l_urb_01", "l_uwf_01", "l_dwf_01", "l_uj_00", "l_uj_01"};

float num(const Value& v, const char* k, float fallback) {
    const double d = v[k].as_double(fallback);
    return std::isfinite(d) ? float(d) : fallback;
}

}  // namespace

const char* library_name(Library l) {
    static const char* const kNames[] = {"area", "weapon", "force", "sound", "effect", "lobby", "menu"};
    return u8(l) < u8(Library::Count) ? kNames[u8(l)] : "?";
}

bool id_ok(std::string_view id) {
    if (id.size() < 3 || id.size() > 24) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool key_ok(std::string_view key, std::string_view pack_id, std::string* why) {
    if (key.empty() || key.size() > kMaxKey) return fail(why, "a key that is empty or too long");
    const std::string prefix = "x/" + std::string(pack_id) + "/";
    if (!key.starts_with(prefix) || key.size() == prefix.size()) return fail(why, "a key outside x/" + std::string(pack_id) + "/ (NM-1): " + std::string(key));
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || c == '/'))
            return fail(why, "a key with a character it may not have (SC-3): " + std::string(key));
    if (key.find("..") != std::string_view::npos || key.find("//") != std::string_view::npos || key.back() == '/' || key.find(':') != std::string_view::npos)
        return fail(why, "a key that tries to leave its folder (SC-3): " + std::string(key));
    return true;
}

std::string_view leaf(std::string_view key) {
    const size_t slash = key.rfind('/');
    return slash == std::string_view::npos ? key : key.substr(slash + 1);
}

std::string sha256_hex(std::span<const u8> bytes) { return eng::crypto::to_hex(eng::crypto::sha256(bytes)); }

bool read(std::span<const u8> file, Pack& out, std::string* why, bool check_files) {
    out = Pack{};
    if (file.size() > kMaxPackBytes) return fail(why, "a pack over 50 MB (DL-6)");
    if (file.size() < 24 || std::memcmp(file.data(), kMagic, 8) != 0) return fail(why, "not a pack (it is no LSFPACK1 file)");
    eng::ByteReader r(file.data() + 8, file.size() - 8);
    out.format = r.u32();
    const u32 header_bytes = r.u32();
    if (!r.ok() || out.format != kPackFormat) return fail(why, "a pack of another format (" + std::to_string(out.format) + ")");
    if (header_bytes > file.size() || header_bytes < 24) return fail(why, "a pack whose table runs past its end");
    if (!read_str(r, out.id, 24) || !id_ok(out.id)) return fail(why, "a pack id that breaks the rules (NM-9): [a-z0-9_], 3 to 24");
    out.version = r.u32();
    {
        const u32 dn = r.u32();
        if (!r.ok() || dn > kMaxDefinitions || dn > r.remaining()) return fail(why, "a pack whose definitions run past its end");
        out.definitions.resize(dn);
        if (dn) r.raw(out.definitions.data(), dn);
    }
    const u32 n = r.u32();
    if (!r.ok() || n > kMaxEntries) return fail(why, "a pack with more files than one may hold (SC-2)");
    std::set<std::string> keys;
    for (u32 i = 0; i < n; ++i) {
        Entry e;
        const u8 lib = r.u8();
        if (!read_str(r, e.key, kMaxKey) || lib >= u8(Library::Count)) return fail(why, "a pack whose table is broken");
        e.library = Library(lib);
        e.offset = r.u64();
        e.size = r.u32();
        r.raw(e.sha.data(), e.sha.size());
        if (!r.ok()) return fail(why, "a pack whose table is broken");
        if (!key_ok(e.key, out.id, why)) return false;
        if (!keys.insert(std::string(library_name(e.library)) + ":" + e.key).second) return fail(why, "a pack with the same file twice: " + e.key);
        // SC-1: every file inside the file, after the table.
        if (e.offset < header_bytes || e.offset > file.size() || e.size > file.size() - e.offset) return fail(why, "a file that runs past the pack's end: " + e.key);
        if (check_files) {
            const auto got = eng::crypto::sha256(file.subspan(size_t(e.offset), e.size));
            if (!eng::crypto::equal(got, e.sha)) return fail(why, "a file that is not what its table says (its SHA-256): " + e.key);
        }
        out.entries.push_back(std::move(e));
    }
    if (8 + r.tell() > header_bytes) return fail(why, "a pack whose table is longer than it says");
    return true;
}

std::span<const u8> file_data(std::span<const u8> file, const Entry& e) {
    if (e.offset > file.size() || e.size > file.size() - e.offset) return {};
    return file.subspan(size_t(e.offset), e.size);
}

std::vector<u8> write(const Pack& p, const std::vector<std::vector<u8>>& files) {
    // The table first (its size settles the files' offsets), then the files.
    auto table = [&](u64 first) {
        eng::ByteWriter w(1024);
        write_str(w, p.id);
        w.u32(p.version);
        w.u32(u32(p.definitions.size()));
        w.bytes(std::span<const u8>(reinterpret_cast<const u8*>(p.definitions.data()), p.definitions.size()));
        w.u32(u32(p.entries.size()));
        u64 at = first;
        for (size_t i = 0; i < p.entries.size(); ++i) {
            const Entry& e = p.entries[i];
            w.u8(u8(e.library));
            write_str(w, e.key);
            w.u64(at);
            w.u32(u32(files[i].size()));
            const auto sha = eng::crypto::sha256(files[i]);
            w.bytes(sha);
            at += files[i].size();
        }
        return w.take();
    };
    const size_t fixed = 8 + 4 + 4;
    std::vector<u8> t = table(0);
    const u64 first = fixed + t.size();
    t = table(first);
    eng::ByteWriter out(size_t(first) + 1024);
    out.bytes(std::span<const u8>(reinterpret_cast<const u8*>(kMagic), 8));
    out.u32(p.format);
    out.u32(u32(first));
    out.bytes(t);
    std::vector<u8> bytes = out.take();
    for (const auto& f : files) bytes.insert(bytes.end(), f.begin(), f.end());
    return bytes;
}

// ── PK-1, SC-2: what kinds of file a pack holds ────────────────────────────────

namespace {

bool is_one_of(std::string_view ext, std::initializer_list<const char*> list) {
    return std::any_of(list.begin(), list.end(), [&](const char* e) { return ext == e; });
}

bool is_picture(std::string_view ext) { return is_one_of(ext, {".dds", ".tga", ".jpg", ".png", ".bmp"}); }

// The kinds each library's readers know: Soldier Front's own data formats, pictures and sounds.
// Nothing here is run: a script, a shader or a program has no extension on these lists.
bool kind_ok(Library l, std::string_view ext) {
    if (is_picture(ext)) return l != Library::Sound;
    switch (l) {
        case Library::Area: return is_one_of(ext, {".map", ".msf", ".xml", ".cft", ".env", ".wld", ".val", ".osf", ".db", ".particle"});
        case Library::Weapon: return is_one_of(ext, {".lma", ".sfc", ".sfm", ".fpd"});
        case Library::Force: return is_one_of(ext, {".lma", ".fxa", ".fxm", ".lmf", ".sfc", ".sfm", ".csv"});
        case Library::Sound: return is_one_of(ext, {".wav", ".mp3"});   // what the game decodes (Engine/Audio/Pcm)
        default: return false;   // effect, lobby, menu: pictures only
    }
}

u32 le16(std::span<const u8> d, size_t at) { return at + 2 <= d.size() ? u32(d[at]) | (u32(d[at + 1]) << 8) : 0; }
u32 le32(std::span<const u8> d, size_t at) { return at + 4 <= d.size() ? le16(d, at) | (le16(d, at + 2) << 16) : 0; }
u32 be16(std::span<const u8> d, size_t at) { return at + 2 <= d.size() ? (u32(d[at]) << 8) | u32(d[at + 1]) : 0; }
u32 be32(std::span<const u8> d, size_t at) { return at + 4 <= d.size() ? (be16(d, at) << 16) | be16(d, at + 2) : 0; }

// A picture's size from its header, before anything decodes it. False when it is not one of its kind.
bool picture_size(std::string_view ext, std::span<const u8> d, u32& w, u32& h) {
    w = h = 0;
    if (ext == ".dds") {
        if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) return false;
        h = le32(d, 12), w = le32(d, 16);
    } else if (ext == ".png") {
        static const u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (d.size() < 24 || std::memcmp(d.data(), kSig, 8) != 0) return false;
        w = be32(d, 16), h = be32(d, 20);
    } else if (ext == ".bmp") {
        if (d.size() < 26 || d[0] != 'B' || d[1] != 'M') return false;
        w = le32(d, 18);
        const i32 sh = i32(le32(d, 22));
        h = u32(sh < 0 ? -i64(sh) : i64(sh));
    } else if (ext == ".tga") {
        if (d.size() < 18) return false;
        w = le16(d, 12), h = le16(d, 14);
    } else if (ext == ".jpg") {
        if (d.size() < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
        // The frame header (SOF0..SOF15, less the tables that share the range) carries the size.
        size_t at = 2;
        while (at + 9 <= d.size()) {
            if (d[at] != 0xFF) return false;
            const u8 marker = d[at + 1];
            if (marker == 0xFF) {
                ++at;
                continue;
            }
            const u32 len = be16(d, at + 2);
            if (len < 2) return false;
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                h = be16(d, at + 5), w = be16(d, at + 7);
                break;
            }
            at += 2 + size_t(len);
        }
    }
    return w > 0 && h > 0;
}

}  // namespace

bool is_picture_name(std::string_view l) {
    const size_t dot = l.rfind('.');
    return dot != std::string_view::npos && is_picture(l.substr(dot));
}

bool check_files(const Pack& p, std::span<const u8> file, std::string* why, u32 max_texture) {
    for (const Entry& e : p.entries) {
        const std::string_view l = leaf(e.key);
        const size_t dot = l.rfind('.');
        const std::string_view ext = dot == std::string_view::npos ? std::string_view() : l.substr(dot);
        if (!kind_ok(e.library, ext))
            return fail(why, std::string(l) + " is not a kind of file the " + library_name(e.library) + " library holds (PK-1: packs are data only)");
        const std::span<const u8> d = file_data(file, e);
        if (d.size() != e.size) return fail(why, "a file that runs past the pack's end: " + e.key);
        if (is_picture(ext)) {
            u32 w = 0, h = 0;
            if (!picture_size(ext, d, w, h)) return fail(why, std::string(l) + " is not the picture its name says (SC-1)");
            if (w > max_texture || h > max_texture)
                return fail(why, std::string(l) + eng::str::format(" is %u x %u: a picture is at most %u a side (SC-2)", w, h, max_texture));
        } else if (e.library == Library::Sound && e.size > kMaxSoundBytes) {
            return fail(why, std::string(l) + " is over 8 MB: too long a sound (SC-2)");
        }
    }
    return true;
}

// ── §11.5: what a pack may add ────────────────────────────────────────────────

bool validate(const Pack& p, const Value& defs, const BaseLookup& base, std::set<std::string>& taken, std::string* why) {
    if (!defs.is_object()) return fail(why, "the pack's definitions are not readable");
    // NM-2: no file takes a name the client's own content finds by leaf (that lookup would turn
    // ambiguous and find nothing), nor one another pack has.
    for (const Entry& e : p.entries) {
        const std::string l(leaf(e.key));
        if (!base) continue;
        // A map's lightmaps (level1/sector_NNlightingmap) are named by the map format and only ever
        // read from their own map's folder: the same names in every map, and free.
        if (e.library == Library::Area && l.starts_with("sector_") && l.find("lightingmap.") != std::string::npos && e.key.ends_with("/level1/" + l)) continue;
        const std::string in = std::string(library_name(e.library)) + ":";
        const bool picture = is_picture_name(l);
        const BaseNames b = base(e.library, l);
        if (b.same == 1 || (picture && b.stem == 1)) return fail(why, "a file named like one of Soldier Front's own (NM-2): " + l);
        if (b.same == 0 && !taken.insert(in + l).second) return fail(why, "two files with the same name in one library (NM-2): " + l);
        // A picture is found under whatever extension it has: one of a name, whatever follows the dot.
        if (picture && b.stem == 0 && !taken.insert(in + "picture:" + l.substr(0, l.rfind('.'))).second)
            return fail(why, "two pictures with the same name in one library (NM-2): " + l);
    }
    auto in_library = [&](Library lib, std::string_view folder) {
        const std::string under = std::string(folder) + "/";
        return std::any_of(p.entries.begin(), p.entries.end(), [&](const Entry& e) { return e.library == lib && e.key.starts_with(under); });
    };
    auto has_entry = [&](Library lib, std::string_view key) {
        return std::any_of(p.entries.begin(), p.entries.end(), [&](const Entry& e) { return e.library == lib && e.key == key; });
    };
    auto name_ok = [](std::string_view n) {
        return !n.empty() && n.size() <= 40 && std::all_of(n.begin(), n.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    };
    const std::string mine = "x/" + p.id + "/";
    if (defs["weapons"].size() > 256 || defs["forces"].size() > 64 || defs["parts"].size() > 512 || defs["maps"].size() > 64)
        return fail(why, "more weapons, characters, parts or maps than one pack may add");
    std::set<std::string> names;
    const MovementDef move;
    for (const Value& w : defs["weapons"].elements()) {
        const std::string n = w["name"].str();
        if (!name_ok(n)) return fail(why, "a weapon's name breaks the rules: " + n);
        if (!names.insert("w:" + n).second) return fail(why, "two weapons called " + n);
        const int klass = int(w["class"].as_int(-1));
        if (klass < 0 || klass >= int(WeaponClass::Count)) return fail(why, n + ": no such weapon class");
        if (w.has("slot") && int(w["slot"].as_int()) != int(slot_of(WeaponClass(klass)))) return fail(why, n + ": its slot does not match its class (a pistol is a sidearm)");
        const int pellets = int(w["pellets"].as_int(1));
        if (pellets < 1 || pellets > 16) return fail(why, n + ": pellets 1 to 16 (Shoot carries 16 hits at most)");
        const float rpm = num(w, "rpm", 600);
        if (rpm < 1 || rpm > 1200) return fail(why, n + ": rpm 1 to 1,200");
        const i64 mag = w["magazine"].as_int(30), reserve = w["reserve"].as_int(90);
        if (mag < 0 || mag > 300 || reserve < 0 || reserve > 999) return fail(why, n + ": magazine 0 to 300, reserve 0 to 999");
        const float damage = num(w, "damage", 30), blast = num(w, "blast_radius", 0);
        if (damage < 0 || damage > 500 || blast < 0 || blast > 2000) return fail(why, n + ": damage 0 to 500, blast radius 0 to 2,000 cm");
        const int grip = int(w["grip"].as_int(2));
        if (!grip_ok(grip)) return fail(why, n + ": no arm clips exist for grip " + std::to_string(grip));
        // §11.5: the fastest force, the best parts and Super Speed together stay under kMaxSpeed.
        const float speed = num(w, "move_speed", 0.95f);
        if (speed <= 0 || move.run_speed * speed * 1.04f * 1.15f * 1.3f >= 1600.0f) return fail(why, n + ": move speed too high (the server would refuse such movement)");
        const i64 price = w["price"].as_int();
        if (price < 1 || price > 10000000) return fail(why, n + ": a price of 1 to 10,000,000 SP (SH-8)");
        // Its model: the pack's own gun folder, or a base gun's by reference (PK-8).
        const std::string model = w["model"].str();
        if (model.starts_with("base:")) {
            if (!weapon_by_model(model.substr(5))) return fail(why, n + ": no base weapon is called " + model.substr(5) + " (PK-8)");
        } else if (!model.starts_with(mine + "bhw/") || model.find('/', mine.size() + 4) != std::string::npos || !in_library(Library::Weapon, model)) {
            return fail(why, n + ": its model is not in the pack (a gun's folder is x/" + p.id + "/bhw/<folder>): " + model);
        }
        // The base gun its sounds, pictures and kill mark are taken from.
        if (!weapon_by_model(w["like"].str())) return fail(why, n + ": `like` names no base weapon: " + w["like"].str());
    }
    for (const Value& f : defs["forces"].elements()) {
        const std::string n = f["name"].str();
        if (!name_ok(n)) return fail(why, "a character's name breaks the rules: " + n);
        if (!names.insert("f:" + n).second) return fail(why, "two characters called " + n);
        const float speed = num(f, "speed", 1.0f);
        if (speed < 0.8f || speed > 1.04f) return fail(why, n + ": run speed 0.8 to 1.04 (the fastest base force's)");
        for (const char* k : {"upper_defense", "lower_defense", "avoid_headshot"})
            if (num(f, k, 0) < 0 || num(f, k, 0) > 0.05f) return fail(why, n + ": " + k + " 0 to 0.05");
        if (f["bones"].as_int(0) > i64(kMaxBones) || f["bones"].as_int(0) <= 0) return fail(why, n + ": its skeleton has more than 128 bones, or none");
        // PK-6: a character is only a look: near the hitboxes' size.
        const float height = num(f, "height", 0), width = num(f, "width", 0);
        if (height < kMinForceHeight || height > kMaxForceHeight || width < kMinForceWidth || width > kMaxForceWidth)
            return fail(why, n + ": much bigger or smaller than every soldier's hitboxes (PK-6)");
        std::set<std::string> clips;
        for (const Value& c : f["clips"].elements()) clips.insert(c.str());
        for (const char* c : kRequiredClips)
            if (!clips.contains(c)) return fail(why, n + ": it cannot play the " + c + " clip (every clip the animation code plays: §11.5)");
        const std::string model = f["model"].str();
        if (!model.starts_with(mine + "sf_c_") || model.find('/', mine.size()) != std::string::npos || !in_library(Library::Force, model))
            return fail(why, n + ": its model is not in the pack (a character's folder is x/" + p.id + "/sf_c_<name>): " + model);
        if (!force_by_model(f["art"].str())) return fail(why, n + ": `art` names no base force: " + f["art"].str());
        const i64 price = f["price"].as_int();
        if (price < 1 || price > 10000000) return fail(why, n + ": a price of 1 to 10,000,000 SP (SH-8)");
    }
    std::set<std::string> own_forces;
    for (const Value& f : defs["forces"].elements()) own_forces.insert("x:" + p.id + ":" + f["name"].str());
    for (const Value& part : defs["parts"].elements()) {
        const std::string n = part["name"].str();
        if (!name_ok(n)) return fail(why, "a part's name breaks the rules: " + n);
        if (!names.insert("p:" + n).second) return fail(why, "two parts called " + n);
        // PK-6a, PK-6b: a pack character wears only its own pack's parts; a new part may be made for a base character.
        const Value& owner = part["force"];
        const bool base_force = owner.is_number() && owner.as_int(-1) >= 0 && force(u8(std::min<i64>(owner.as_int(-1), 255))) && owner.as_int(-1) < i64(kFirstPackForce);
        if (!base_force && !own_forces.contains(owner.str())) return fail(why, n + ": a part fits one of this pack's characters, or a base character");
        const i64 price = part["price"].as_int();
        if (price < 1 || price > 10000000) return fail(why, n + ": a price of 1 to 10,000,000 SP (SH-8)");
        if (num(part, "speed", 0) > 1.0f || num(part, "speed", 0) < 0) return fail(why, n + ": speed 0 to 1, as base parts");
        // Its piece: a model of the pack's, by its file's name.
        const std::string model = part["model"].str();
        if (!name_ok(model)) return fail(why, n + ": its model is a file's name with no folder and no extension");
        const bool there = std::any_of(p.entries.begin(), p.entries.end(), [&](const Entry& e) {
            const std::string_view l = leaf(e.key);
            return e.library == Library::Force && (l == model + ".lma" || l == model + ".fxa");
        });
        if (!there) return fail(why, n + ": its model is not in the pack: " + model);
    }
    for (const Value& m : defs["maps"].elements()) {
        const std::string n = m["name"].str();
        if (!name_ok(n) || n.find('-') != std::string::npos) return fail(why, "a map's name breaks the rules: " + n);
        if (!names.insert("m:" + n).second) return fail(why, "two maps called " + n);
        const std::string id = "x-" + p.id + "-" + n;
        if (id.size() > 32) return fail(why, id + ": a map's id is at most 32 characters (NM-8)");
        const std::string folder = mine + "ground/sf_m_" + n;
        if (m["folder"].str() != folder || !has_entry(Library::Area, folder + "/sf_m_" + n + ".map"))
            return fail(why, n + ": its files are not in the pack (a map is x/" + p.id + "/ground/sf_m_" + n + "/sf_m_" + n + ".map)");
    }
    return true;
}

Value weapon_json(const WeaponDef& d) {
    Value w;
    w["class"] = int(d.klass);
    w["price"] = d.price;
    w["damage"] = d.damage, w["head"] = d.head_multiplier, w["leg"] = d.leg_multiplier;
    w["range"] = d.range, w["falloff_start"] = d.falloff_start, w["falloff_min"] = d.falloff_min;
    w["rpm"] = d.rpm, w["automatic"] = d.automatic;
    w["magazine"] = d.magazine, w["reserve"] = d.reserve;
    w["reload"] = d.reload_time, w["draw"] = d.draw_time;
    Value sp;
    sp["stand"] = d.spread_stand, sp["crouch"] = d.spread_crouch, sp["move"] = d.spread_move, sp["move_rate"] = d.spread_move_rate;
    sp["per_shot"] = d.spread_per_shot, sp["max"] = d.spread_max, sp["recover"] = d.spread_recover;
    sp["run"] = d.run_spread, sp["air"] = d.air_spread, sp["walk"] = d.walk_spread;
    w["spread"] = std::move(sp);
    Value rc;
    rc["up"] = d.recoil_up, rc["up_max"] = d.recoil_up_max, rc["side"] = d.recoil_side, rc["recover"] = d.recoil_recover, rc["side_max"] = d.recoil_side_max;
    w["recoil"] = std::move(rc);
    w["move_speed"] = d.move_speed;
    w["grip"] = d.grip;
    w["carried"] = d.carried;
    Value sc;
    sc["zoom"] = d.scoped ? d.zoom : 0.0f, sc["image"] = d.scope_image, sc["move"] = d.scope_move;
    w["scope"] = std::move(sc);
    w["pellets"] = d.pellets;
    w["grenade"] = int(d.grenade);
    w["fuse"] = d.fuse, w["blast_radius"] = d.blast_radius, w["melee_range"] = d.melee_range;
    return w;
}

// ── The manifest and the registry ──────────────────────────────────────────────

Value manifest(const std::vector<std::pair<Pack, std::string>>& packs, const std::vector<u32>& sizes) {
    Value m;
    m["format"] = kPackFormat;
    Value list = Value::array(), weapons = Value::array(), forces = Value::array(), parts = Value::array(), maps = Value::array();
    u32 next_weapon = kFirstPackWeapon, next_force = kFirstPackForce, next_item = kFirstPackItem;
    for (size_t i = 0; i < packs.size(); ++i) {
        const Pack& p = packs[i].first;
        Value e;
        e["id"] = p.id;
        e["version"] = p.version;
        e["sha256"] = packs[i].second;
        e["size"] = sizes[i];
        list.push(std::move(e));
        Value defs;
        eng::json::parse(p.definitions, defs);
        for (const Value& w : defs["weapons"].elements()) {
            Value d = w;
            d["id"] = next_weapon++;
            d["code"] = "x:" + p.id + ":" + w["name"].str();
            d["pack"] = p.id;
            weapons.push(std::move(d));
        }
        for (const Value& f : defs["forces"].elements()) {
            Value d = f;
            d["id"] = next_force++;
            d["code"] = "x:" + p.id + ":" + f["name"].str();
            d["pack"] = p.id;
            forces.push(std::move(d));
        }
        for (const Value& part : defs["parts"].elements()) {
            Value d = part;
            d["id"] = next_item++;
            d["code"] = "x:" + p.id + ":" + part["name"].str();
            d["pack"] = p.id;
            parts.push(std::move(d));
        }
        for (const Value& map : defs["maps"].elements()) {
            Value d = map;
            d["id"] = "x-" + p.id + "-" + map["name"].str();
            d["pack"] = p.id;
            maps.push(std::move(d));
        }
    }
    m["packs"] = std::move(list);
    m["weapons"] = std::move(weapons);
    m["forces"] = std::move(forces);
    m["parts"] = std::move(parts);
    m["maps"] = std::move(maps);
    return m;
}

bool registry_from_manifest(const Value& m, SessionContent& out, std::string* why) {
    out = SessionContent{};
    if (!m.is_object() || m["format"].as_uint() != kPackFormat) return fail(why, "a manifest of another format");
    const auto& ws = m["weapons"].elements();
    const auto& fs = m["forces"].elements();
    const auto& ps = m["parts"].elements();
    if (ws.size() > size_t(0xFFFE - kFirstPackWeapon) || fs.size() > size_t(kNoForce - kFirstPackForce) || ps.size() > size_t(0xFFFE - kFirstPackItem))
        return fail(why, "a manifest adding more than the numbers allow (NM-4..NM-6)");
    for (const Value& w : ws) {
        const u64 id = w["id"].as_uint();
        if (id < kFirstPackWeapon || id >= kNoWeapon) return fail(why, "a pack weapon with a base weapon's number (NM-4)");
        const int klass = int(w["class"].as_int(-1));
        if (klass < 0 || klass >= int(WeaponClass::Count)) return fail(why, "a pack weapon of no class");
        const int pellets = int(w["pellets"].as_int(1));
        const float rpm = num(w, "rpm", 600), dmg = num(w, "damage", 30), blast = num(w, "blast_radius", 0), speed = num(w, "move_speed", 0.95f);
        if (pellets < 1 || pellets > 16 || rpm < 1 || rpm > 1200 || dmg < 0 || dmg > 500 || blast < 0 || blast > 2000 || speed <= 0 || speed > 1.5f || !grip_ok(int(w["grip"].as_int(2))))
            return fail(why, "a pack weapon out of bounds (§11.5): " + w["code"].str());
        WeaponDef d;
        d.id = u16(id);
        d.code = w["code"].str();
        // PK-8: "base:<id>" is a base gun's model by reference; else the pack's own folder.
        d.model = w["model"].str();
        if (d.model.starts_with("base:")) d.model.erase(0, 5);
        const WeaponDef* like = weapon_by_model(w["like"].str());
        if (!like) return fail(why, "a pack weapon like no base weapon: " + w["code"].str());
        d.like = like->model;
        d.name = eng::str::sanitize_line(w["title"].str(w["name"].str()), 32);
        d.klass = WeaponClass(klass);
        d.slot = slot_of(d.klass);
        d.price = u32(std::max<i64>(1, w["price"].as_int(1)));
        d.shop = true;
        d.damage = dmg, d.head_multiplier = num(w, "head", 4), d.leg_multiplier = num(w, "leg", 0.75f);
        d.range = num(w, "range", 9000), d.falloff_start = num(w, "falloff_start", 3000), d.falloff_min = num(w, "falloff_min", 0.7f);
        d.rpm = rpm, d.automatic = w["automatic"].as_bool(true);
        d.magazine = u16(std::clamp<i64>(w["magazine"].as_int(30), 0, 300)), d.reserve = u16(std::clamp<i64>(w["reserve"].as_int(90), 0, 999));
        d.reload_time = std::clamp(num(w, "reload", 2.4f), 0.1f, 10.0f), d.draw_time = std::clamp(num(w, "draw", 0.55f), 0.05f, 5.0f);
        const Value& sp = w["spread"];
        d.spread_stand = num(sp, "stand", 0.32f), d.spread_crouch = num(sp, "crouch", 0.2f), d.spread_move = num(sp, "move", 2.4f);
        d.spread_move_rate = num(sp, "move_rate", 2.5f), d.spread_per_shot = num(sp, "per_shot", 0.18f), d.spread_max = num(sp, "max", 3.3f), d.spread_recover = num(sp, "recover", 7);
        d.run_spread = num(sp, "run", 1.5f), d.air_spread = num(sp, "air", 2.0f), d.walk_spread = num(sp, "walk", 1.0f);
        const Value& rc = w["recoil"];
        d.recoil_up = num(rc, "up", 0.38f), d.recoil_up_max = num(rc, "up_max", 5.5f), d.recoil_side = num(rc, "side", 0.22f), d.recoil_recover = num(rc, "recover", 14);
        d.recoil_side_max = num(rc, "side_max", 2.5f);
        d.move_speed = speed;
        d.grip = u8(w["grip"].as_int(2));
        d.carried = w.has("carried") ? w["carried"].str() : like->carried;
        const Value& sc = w["scope"];
        d.scoped = num(sc, "zoom", 0) > 0, d.zoom = num(sc, "zoom", 0), d.scope_image = sc["image"].str(), d.scope_move = num(sc, "move", 1.5f);
        d.pellets = u8(pellets);
        d.grenade = GrenadeKind(std::clamp<i64>(w["grenade"].as_int(0), 0, 4));
        d.fuse = num(w, "fuse", 0), d.blast_radius = blast, d.melee_range = num(w, "melee_range", 0);
        out.weapons.push_back(std::move(d));
    }
    std::sort(out.weapons.begin(), out.weapons.end(), [](const WeaponDef& a, const WeaponDef& b) { return a.id < b.id; });
    for (const Value& f : fs) {
        const u64 id = f["id"].as_uint();
        if (id < kFirstPackForce || id >= kNoForce) return fail(why, "a pack character with a base force's number (NM-5)");
        PackForce pf;
        pf.pack = f["pack"].str();
        pf.code = f["code"].str();
        pf.model = f["model"].str();
        pf.name = eng::str::sanitize_line(f["title"].str(f["name"].str()), 32);
        pf.nation = eng::str::sanitize_line(f["nation"].str(), 24);
        const ForceDef* art = force_by_model(f["art"].str());
        if (!art) return fail(why, "a pack character with no base force's art: " + pf.code);
        pf.art = art->model;
        pf.def.id = u8(id);
        pf.def.price = u32(std::max<i64>(1, f["price"].as_int(1)));
        pf.def.speed = std::clamp(num(f, "speed", 1.0f), 0.8f, 1.04f);
        pf.def.upper_defense = std::clamp(num(f, "upper_defense", 0), 0.0f, 0.05f);
        pf.def.lower_defense = std::clamp(num(f, "lower_defense", 0), 0.0f, 0.05f);
        pf.def.avoid_headshot = std::clamp(num(f, "avoid_headshot", 0), 0.0f, 0.05f);
        out.forces.push_back(std::move(pf));
    }
    for (const Value& part : ps) {
        const u64 id = part["id"].as_uint();
        if (id < kFirstPackItem || id >= 0xFFFF) return fail(why, "a pack part with a base item's number (NM-6)");
        PackItem pi;
        pi.pack = part["pack"].str();
        pi.code = part["code"].str();
        pi.name = eng::str::sanitize_line(part["title"].str(part["name"].str()), 32);
        pi.info = eng::str::sanitize_line(part["info"].str(), 120);
        pi.tab = eng::str::sanitize_line(part["tab"].str("Accessory"), 16);
        pi.model = part["model"].str();
        pi.picture = part["picture"].str();
        pi.def = ItemDef{};
        pi.def.id = u16(id);
        pi.def.kind = ItemKind::Part;
        const Value& owner = part["force"];
        if (owner.is_number()) pi.def.force = u8(std::clamp<i64>(owner.as_int(0), 0, kFirstPackForce - 1));
        else
            for (const PackForce& f : out.forces)
                if (f.code == owner.str()) pi.def.force = f.def.id;
        pi.def.slot = i16(std::clamp<i64>(part["slot"].as_int(0), 0, 64));
        pi.def.speed = std::clamp(num(part, "speed", 0), 0.0f, 1.0f);
        pi.def.offers[0] = {0, u32(std::max<i64>(1, part["price"].as_int(1)))};
        out.items.push_back(std::move(pi));
    }
    for (const Value& map : m["maps"].elements()) {
        PackMap pm;
        pm.id = map["id"].str();
        if (pm.id.size() > 32 || !pm.id.starts_with("x-")) return fail(why, "a pack map id that breaks NM-8: " + pm.id);
        pm.pack = map["pack"].str();
        pm.title = eng::str::sanitize_line(map["title"].str(pm.id), 40);
        pm.folder = map["folder"].str();
        pm.night = map["night"].as_bool();
        out.maps.push_back(std::move(pm));
    }
    return true;
}

}  // namespace lsf::pack
