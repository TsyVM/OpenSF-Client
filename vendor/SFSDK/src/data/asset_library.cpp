// SPDX-License-Identifier: MIT
#include "sf1/data/asset_library.hpp"
#include "path_util.hpp"
#include <algorithm>
#include <system_error>

namespace sf1::data {

namespace {

constexpr std::uint32_t kAmbiguous = 0xFFFFFFFF;

bool ends_with_component(std::string_view key, std::string_view suffix) noexcept {
    return key.size() > suffix.size() && key.ends_with(suffix) && key[key.size() - suffix.size() - 1] == '/';
}

// "texture/sf_m_wall/cr_wall_004.jpg" -> "texture/sf_m_wall/cr_wall_004".
// A dot after the last slash only; "level1/a.b/c" keeps its whole tail.
std::string_view without_extension(std::string_view key) noexcept {
    const auto dot = key.rfind('.');
    if (dot == std::string_view::npos) return key;
    if (key.find('/', dot) != std::string_view::npos) return key;
    return key.substr(0, dot);
}

bool is_sff(const std::filesystem::path& p) {
    return normalize_entry_name(detail::utf8(p.extension())) == ".sff";
}

std::vector<std::filesystem::path> sff_files(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && is_sff(it->path())) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return normalize_entry_name(detail::utf8(a.filename())) < normalize_entry_name(detail::utf8(b.filename()));
    });
    return out;
}

}  // namespace

std::vector<std::filesystem::path> sibling_archives(const std::filesystem::path& primary) {
    std::error_code ec;
    const auto self = std::filesystem::weakly_canonical(primary, ec);
    auto all = sff_files(primary.parent_path());
    std::vector<std::filesystem::path> out;
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        std::error_code ec2;
        if (std::filesystem::equivalent(*it, self, ec2)) continue;
        out.push_back(*it);
    }
    return out;
}

Result<AssetLibrary> AssetLibrary::open(const std::filesystem::path& primary) noexcept {
    std::vector<std::filesystem::path> order{primary};
    for (auto& p : sibling_archives(primary)) order.push_back(std::move(p));
    return open(order);
}

Result<AssetLibrary> AssetLibrary::open(std::span<const std::filesystem::path> archives) noexcept {
    if (archives.empty()) return err(Error::EntryNotFound);
    AssetLibrary lib;
    for (std::size_t i = 0; i < archives.size(); ++i) {
        auto a = Archive::open(archives[i]);
        if (!a) {
            if (i == 0) return err(a.error());
            lib.skipped_.emplace_back(archives[i], a.error());
            continue;
        }
        lib.archives_.push_back(std::move(*a));
    }
    if (auto ok = lib.index_all(); !ok) return err(ok.error());
    return lib;
}

Result<void> AssetLibrary::index_all() noexcept {
    merged_.clear();
    stems_.clear();
    for (std::uint32_t ai = 0; ai < archives_.size(); ++ai) {
        const auto& a = archives_[ai];
        for (std::uint32_t ei = 0; ei < a.size(); ++ei) {
            const auto& k = a.key(ei);
            if (merged_.contains(k)) continue;
            auto exact = a.find(k);
            const AssetLocation where{ai, exact ? static_cast<std::uint32_t>(*exact) : kAmbiguous};
            merged_.emplace(k, where);
            // Highest-priority archive wins a stem, exactly as it wins a key.
            stems_.emplace(std::string(without_extension(k)), where);
        }
    }
    return {};
}

Result<AssetLocation> AssetLibrary::find(std::string_view name) const noexcept {
    auto it = merged_.find(normalize_entry_name(name));
    if (it == merged_.end()) return err(Error::EntryNotFound);
    if (it->second.entry == kAmbiguous) return err(Error::AmbiguousEntry);
    return it->second;
}

Result<AssetLocation> AssetLibrary::resolve(std::string_view name) const noexcept {
    const auto key = normalize_entry_name(name);
    if (key.empty()) return err(Error::EntryNotFound);
    if (auto it = merged_.find(key); it != merged_.end()) {
        if (it->second.entry == kAmbiguous) return err(Error::AmbiguousEntry);
        return it->second;
    }
    AssetLocation found{};
    std::size_t hits = 0;
    for (const auto& [k, where] : merged_) {
        if (!ends_with_component(k, key)) continue;
        if (++hits > 1 || where.entry == kAmbiguous) return err(Error::AmbiguousEntry);
        found = where;
    }
    if (hits == 0) return err(Error::EntryNotFound);
    return found;
}

Result<AssetLocation> AssetLibrary::resolve_any_extension(std::string_view name) const noexcept {
    if (auto exact = resolve(name)) return exact;

    const auto key = normalize_entry_name(name);
    const std::string stem(without_extension(key));
    if (stem.empty()) return err(Error::EntryNotFound);

    if (auto it = stems_.find(stem); it != stems_.end()) {
        if (it->second.entry == kAmbiguous) return err(Error::AmbiguousEntry);
        return it->second;
    }

    AssetLocation found{};
    std::size_t hits = 0;
    for (const auto& [k, where] : stems_) {
        if (!ends_with_component(k, stem)) continue;
        if (++hits > 1 || where.entry == kAmbiguous) return err(Error::AmbiguousEntry);
        found = where;
    }
    if (hits == 0) return err(Error::EntryNotFound);
    return found;
}

std::vector<std::pair<std::string, AssetLocation>> AssetLibrary::keys(std::string_view prefix,
                                                                     std::string_view suffix) const {
    const std::string p = normalize_entry_name(prefix);
    const std::string s = normalize_entry_name(suffix);
    std::vector<std::pair<std::string, AssetLocation>> out;
    for (const auto& [k, where] : merged_) {
        if (where.entry == kAmbiguous || !k.starts_with(p) || !k.ends_with(s)) continue;
        out.emplace_back(k, where);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

Result<std::vector<std::byte>> AssetLibrary::read(AssetLocation where) const noexcept {
    if (where.archive >= archives_.size()) return err(Error::EntryNotFound);
    return archives_[where.archive].read(where.entry);
}

const std::string& AssetLibrary::key(AssetLocation where) const noexcept {
    return archives_[where.archive].key(where.entry);
}

const sff::Entry& AssetLibrary::entry(AssetLocation where) const noexcept {
    return archives_[where.archive].entries()[where.entry];
}

std::string map_label(std::string_view entry_key) {
    auto key = normalize_entry_name(entry_key);
    std::string_view stem = key;
    if (auto slash = stem.rfind('/'); slash != std::string_view::npos) stem.remove_prefix(slash + 1);
    if (auto dot = stem.rfind('.'); dot != std::string_view::npos && dot != 0) stem = stem.substr(0, dot);
    if (stem.starts_with("sf_m_")) stem.remove_prefix(5);

    std::string label(stem);
    bool word_start = true;
    for (auto& c : label) {
        if (c == '_') c = ' ';
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (letter && word_start && c >= 'a') c = static_cast<char>(c - 'a' + 'A');
        word_start = !letter;
    }
    return label;
}

std::vector<MapListing> discover_maps(const std::filesystem::path& folder) {
    std::vector<MapListing> rows;
    for (const auto& path : sff_files(folder)) {
        auto a = Archive::open(path);
        if (!a) continue;
        std::vector<std::string> keys;
        for (std::size_t i = 0; i < a->size(); ++i)
            if (a->key(i).ends_with(".map")) keys.push_back(a->key(i));
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (auto& k : keys) rows.push_back(MapListing{path, k, map_label(k)});
    }
    return rows;
}

}  // namespace sf1::data
