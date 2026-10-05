#include "SF/Data.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>

namespace sf {

namespace stdfs = std::filesystem;

const char* pack_folder(Pack pack) {
    switch (pack) {
        case Pack::Area: return "area";
        case Pack::Weapon: return "weapon";
        case Pack::Force: return "force";
        case Pack::Lobby: return "lobby";
        case Pack::Menu: return "menu";
        case Pack::Effect: return "effect";
        case Pack::Sound: return "sound";
        case Pack::Clan: return "clan";
        case Pack::Scr: return "scr";
        default: return "?";
    }
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return out;
}

std::string parent_key(std::string_view key) {
    const auto slash = key.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(key.substr(0, slash));
}

std::string leaf_of(std::string_view key) {
    const auto slash = key.rfind('/');
    return std::string(slash == std::string_view::npos ? key : key.substr(slash + 1));
}

std::string stem_of(std::string_view key) {
    std::string leaf = leaf_of(key);
    if (const auto dot = leaf.rfind('.'); dot != std::string::npos) leaf.resize(dot);
    return leaf;
}

int archive_number(const stdfs::path& file) {
    const std::string name = file.stem().string();
    size_t end = name.size();
    while (end > 0 && !std::isdigit((unsigned char)name[end - 1])) --end;
    size_t begin = end;
    while (begin > 0 && std::isdigit((unsigned char)name[begin - 1])) --begin;
    return begin < end ? std::atoi(name.substr(begin, end - begin).c_str()) : 0;
}

std::string cp949_to_utf8(std::string_view text) {
    bool ascii = true;
    for (unsigned char c : text)
        if (c >= 0x80) {
            ascii = false;
            break;
        }
    if (ascii) return std::string(text);
#ifdef _WIN32
    const int wn = MultiByteToWideChar(949, 0, text.data(), int(text.size()), nullptr, 0);
    if (wn > 0) {
        std::wstring w(size_t(wn), L'\0');
        MultiByteToWideChar(949, 0, text.data(), int(text.size()), w.data(), wn);
        return eng::str::narrow(w);
    }
#endif
    // No code page tables here (Android): the client's text is English apart from comments,
    // so each double-byte character becomes one '?'.
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            out.push_back(char(c));
        } else {
            out.push_back('?');
            if (i + 1 < text.size()) ++i;
        }
    }
    return out;
}

std::string cp949_to_utf8(std::span<const std::byte> bytes) {
    return cp949_to_utf8(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// ── Packs' own names (NM-3) ────────────────────────────────────────────────────

namespace {
thread_local std::string t_pack_scope;
}  // namespace

std::string pack_prefix(std::string_view key) {
    if (!key.starts_with("x/")) return {};
    const size_t slash = key.find('/', 2);
    if (slash == std::string_view::npos || slash == 2) return {};
    return std::string(key.substr(0, slash + 1));
}

PackScope::PackScope(std::string_view key) : before_(t_pack_scope) { t_pack_scope = pack_prefix(lower(key)); }
PackScope::~PackScope() { t_pack_scope = std::move(before_); }
const std::string& PackScope::prefix() { return t_pack_scope; }

// ── Data ───────────────────────────────────────────────────────────────────────

bool Data::is_client_data(const stdfs::path& dir) {
    std::error_code ec;
    return !dir.empty() && stdfs::is_directory(dir / "area", ec) && stdfs::is_directory(dir / "lobby", ec);
}

bool Data::open(const stdfs::path& client_data) {
    if (!is_client_data(client_data)) return false;
    std::lock_guard lock(mutex_);
    std::error_code ec;
    root_ = stdfs::absolute(client_data, ec);
    for (auto& l : libs_) l.reset();
    tried_.fill(false);
    return true;
}

void Data::add_layer(const stdfs::path& data) {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    layers_.push_back(stdfs::absolute(data, ec));
    for (auto& l : libs_) l.reset();
    tried_.fill(false);
}

void Data::set_session(const stdfs::path& data) {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    session_ = data.empty() ? stdfs::path() : stdfs::absolute(data, ec);
    for (auto& l : libs_) l.reset();
    tried_.fill(false);
}

namespace {

// A folder's archives, highest patch number first; .sff before .mrg at the same number (none
// share one today).
std::vector<stdfs::path> archives_in(const stdfs::path& folder) {
    std::vector<stdfs::path> files;
    std::error_code ec;
    for (stdfs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext == ".sff" || ext == ".mrg") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const stdfs::path& a, const stdfs::path& b) {
        const int na = archive_number(a), nb = archive_number(b);
        if (na != nb) return na > nb;
        return lower(a.filename().string()) > lower(b.filename().string());
    });
    return files;
}

}  // namespace

std::shared_ptr<const AssetLibrary> Data::library(Pack pack) const {
    const size_t i = size_t(pack);
    if (i >= libs_.size() || root_.empty()) return nullptr;
    std::lock_guard lock(mutex_);
    if (tried_[i]) return libs_[i];
    tried_[i] = true;
    const stdfs::path folder = root_ / pack_folder(pack);
    std::vector<stdfs::path> files = archives_in(folder);
    for (const stdfs::path& layer : layers_)
        for (stdfs::path& f : archives_in(layer / pack_folder(pack))) files.push_back(std::move(f));
    // The session's last: nothing of the client's own moves when it comes or goes.
    if (!session_.empty())
        for (stdfs::path& f : archives_in(session_ / pack_folder(pack))) files.push_back(std::move(f));
    if (files.empty()) {
        LOG_WARN("SF: no archives in %s", eng::str::narrow(folder.wstring()).c_str());
        return nullptr;
    }
    auto lib = AssetLibrary::open(files);
    if (!lib) {
        LOG_ERROR("SF: %s: %s", pack_folder(pack), std::string(sf1::describe(lib.error())).c_str());
        return nullptr;
    }
    for (const auto& [path, error] : lib->skipped())
        LOG_WARN("SF: skipped %s: %s", eng::str::narrow(path.filename().wstring()).c_str(), std::string(sf1::describe(error)).c_str());
    size_t entries = 0;
    for (const auto& a : lib->archives()) entries += a.size();
    LOG_INFO("SF: %-6s %3zu archives, %6zu entries", pack_folder(pack), lib->archives().size(), entries);
    libs_[i] = std::make_shared<const AssetLibrary>(std::move(*lib));
    session_first_[i] = u32(libs_[i]->archives().size());
    if (!session_.empty()) {
        const stdfs::path mine = session_ / pack_folder(pack);
        for (u32 a = 0; a < libs_[i]->archives().size(); ++a)
            if (libs_[i]->archives()[a].path().parent_path() == mine) {
                session_first_[i] = a;
                break;
            }
    }
    return libs_[i];
}

bool Data::in_session(Pack pack, AssetLocation where) const {
    const size_t i = size_t(pack);
    if (i >= libs_.size()) return false;
    std::lock_guard lock(mutex_);
    return !session_.empty() && where.archive >= session_first_[i];
}

bool Data::allowed(Pack pack, std::string_view asked, AssetLocation where) const {
    if (!in_session(pack, where)) return true;
    const std::string found = key(pack, where);
    // By its whole key, or from that pack's own content.
    if (lower(asked) == found) return true;
    const std::string& scope = PackScope::prefix();
    return !scope.empty() && found.starts_with(scope);
}

std::optional<AssetLocation> Data::resolve(Pack pack, std::string_view key) const {
    const auto lib = library(pack);
    if (!lib) return std::nullopt;
    if (auto r = lib->resolve(key)) {
        if (!allowed(pack, key, *r)) return std::nullopt;
        return *r;
    }
    if (session_.empty()) return std::nullopt;
    // Not found, or found twice -- perhaps only because a pack has a file of the name. A pack's
    // files do not count for anyone else (NM-3); for the pack's own content, its own file is the one.
    auto hits = lib->keys("", "/" + sf1::data::normalize_entry_name(key));
    if (hits.size() < 2) return std::nullopt;
    const std::string& scope = PackScope::prefix();
    const AssetLocation* own = nullptr;
    const AssetLocation* base = nullptr;
    size_t owns = 0, bases = 0;
    for (const auto& [k, where] : hits) {
        if (!in_session(pack, where)) ++bases, base = &where;
        else if (!scope.empty() && k.starts_with(scope)) ++owns, own = &where;
    }
    if (owns == 1) return *own;
    if (owns == 0 && bases == 1) return *base;
    return std::nullopt;
}

std::optional<AssetLocation> Data::resolve_any_extension(Pack pack, std::string_view key) const {
    const auto lib = library(pack);
    if (!lib) return std::nullopt;
    auto r = lib->resolve_any_extension(key);
    if (!r || !allowed(pack, key, *r)) return std::nullopt;
    return *r;
}

std::optional<AssetLocation> Data::find_leaf(Pack pack, std::string_view leaf, std::string_view prefer_prefix) const {
    const auto lib = library(pack);
    if (!lib) return std::nullopt;
    auto hits = lib->keys("", "/" + lower(leaf));
    // NM-3: a pack's files are found only from that pack's own content.
    if (!session_.empty()) std::erase_if(hits, [&](const auto& hit) { return !allowed(pack, leaf, hit.second); });
    if (hits.empty()) {
        // A key with no folder at all ("page1.txt" in the lobby archives).
        if (auto r = lib->find(lower(leaf))) return *r;
        return std::nullopt;
    }
    if (!prefer_prefix.empty()) {
        const std::string pre = lower(prefer_prefix);
        for (const auto& hit : hits)
            if (hit.first.starts_with(pre)) return hit.second;
    }
    return hits.front().second;
}

std::vector<std::pair<std::string, AssetLocation>> Data::keys(Pack pack, std::string_view prefix, std::string_view suffix) const {
    const auto lib = library(pack);
    if (!lib) return {};
    return lib->keys(prefix, suffix);
}

std::optional<std::vector<std::byte>> Data::read(Pack pack, AssetLocation where) const {
    const auto lib = library(pack);
    if (!lib) return std::nullopt;
    auto bytes = lib->read(where);
    if (!bytes) return std::nullopt;
    return std::move(*bytes);
}

std::optional<std::vector<std::byte>> Data::read(Pack pack, std::string_view key) const {
    auto where = resolve(pack, key);
    if (!where) return std::nullopt;
    return read(pack, *where);
}

std::string Data::key(Pack pack, AssetLocation where) const {
    const auto lib = library(pack);
    if (!lib || where.archive >= lib->archives().size() || where.entry >= lib->archives()[where.archive].size()) return {};
    return lib->key(where);
}

stdfs::path Data::archive_path(Pack pack, AssetLocation where) const {
    const auto lib = library(pack);
    if (!lib || where.archive >= lib->archives().size()) return {};
    return lib->archives()[where.archive].path();
}

}  // namespace sf
