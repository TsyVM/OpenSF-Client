// SPDX-License-Identifier: MIT
#include "sf1/data/archive.hpp"
#include "sf1/data/mrg.hpp"
#include "path_util.hpp"
#include <fstream>
#include <system_error>

namespace sf1::data {

std::string normalize_entry_name(std::string_view name) {
    std::string key(name);
    for (auto& c : key) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return key;
}

namespace {

bool ends_with_component(std::string_view key, std::string_view suffix) noexcept {
    return key.size() > suffix.size() && key.ends_with(suffix) && key[key.size() - suffix.size() - 1] == '/';
}

}  // namespace

Result<Archive> Archive::open(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return err(Error::IoError);

    std::ifstream f(path, std::ios::binary);
    if (!f) return err(Error::IoError);

    std::byte head[4]{};
    if (!f.read(reinterpret_cast<char*>(head), 4)) return err(Error::ShortRead);
    auto header = sff::read_header(head, size);
    if (!header) return err(header.error());

    std::vector<std::byte> table(static_cast<std::size_t>(header->data_base));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size())))
        return err(Error::ShortRead);
    auto entries = sff::read_table(table, size);
    if (!entries) return err(entries.error());

    Archive a;
    a.path_ = path;
    auto ext = normalize_entry_name(detail::utf8(path.extension()));
    a.kind_ = ext == ".mrg" ? ArchiveKind::Mrg : ArchiveKind::Sff;
    a.file_size_ = size;
    a.entries_ = std::move(*entries);
    a.keys_.reserve(a.entries_.size());
    for (std::uint32_t i = 0; i < a.entries_.size(); ++i) {
        a.keys_.push_back(normalize_entry_name(a.entries_[i].name));
        auto [it, inserted] = a.index_.emplace(a.keys_.back(), i);
        if (!inserted) it->second = kAmbiguous;
    }
    return a;
}

Result<std::size_t> Archive::find(std::string_view name) const noexcept {
    auto it = index_.find(normalize_entry_name(name));
    if (it == index_.end()) return err(Error::EntryNotFound);
    if (it->second == kAmbiguous) return err(Error::AmbiguousEntry);
    return std::size_t{it->second};
}

Result<std::size_t> Archive::resolve(std::string_view name) const noexcept {
    const auto key = normalize_entry_name(name);
    if (key.empty()) return err(Error::EntryNotFound);
    if (auto it = index_.find(key); it != index_.end()) {
        if (it->second == kAmbiguous) return err(Error::AmbiguousEntry);
        return std::size_t{it->second};
    }
    std::size_t found = 0, hits = 0;
    for (const auto& [k, index] : index_) {
        if (!ends_with_component(k, key)) continue;
        if (++hits > 1 || index == kAmbiguous) return err(Error::AmbiguousEntry);
        found = index;
    }
    if (hits == 0) return err(Error::EntryNotFound);
    return found;
}

Result<std::vector<std::byte>> Archive::read(std::size_t index) const noexcept {
    if (index >= entries_.size()) return err(Error::EntryNotFound);
    const auto& e = entries_[index];
    std::ifstream f(path_, std::ios::binary);
    if (!f) return err(Error::IoError);
    std::vector<std::byte> out(e.size);
    f.seekg(static_cast<std::streamoff>(e.offset));
    if (!f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())))
        return err(Error::ShortRead);
    if (kind_ == ArchiveKind::Mrg) mrg::decode(out);
    return out;
}

Result<std::vector<std::byte>> Archive::read(std::string_view name) const noexcept {
    auto index = find(name);
    if (!index) return err(index.error());
    return read(*index);
}

}  // namespace sf1::data
