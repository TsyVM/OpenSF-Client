// SPDX-License-Identifier: MIT
// sf1/data/archive.hpp — file-backed .sff / .mrg archive with name lookup.
//
// Opening reads only the table; payloads are read on demand, so an Archive is cheap
// to keep open and safe to read from several threads (every read opens the file).
//
// Names are looked up by key: backslashes become '/', ASCII letters are lower-cased.
// Two entries with the same key make that key ambiguous, and lookups refuse it with
// Error::AmbiguousEntry instead of picking one.
#pragma once

#include "sff.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sf1::data {

enum class ArchiveKind : std::uint8_t { Sff, Mrg };

// "Texture\\Wall01.JPG" -> "texture/wall01.jpg"
[[nodiscard]] std::string normalize_entry_name(std::string_view name);

class Archive {
public:
    // Kind comes from the extension: ".mrg" (any case) is Mrg, anything else Sff.
    [[nodiscard]] static Result<Archive> open(const std::filesystem::path& path) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] ArchiveKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::uint64_t file_size() const noexcept { return file_size_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::span<const sff::Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] const std::string& key(std::size_t index) const noexcept { return keys_[index]; }

    // Exact key match.
    [[nodiscard]] Result<std::size_t> find(std::string_view name) const noexcept;

    // Exact key match, otherwise the one entry whose key ends in "/<key>".
    [[nodiscard]] Result<std::size_t> resolve(std::string_view name) const noexcept;

    // The payload, decoded for .mrg.
    [[nodiscard]] Result<std::vector<std::byte>> read(std::size_t index) const noexcept;
    [[nodiscard]] Result<std::vector<std::byte>> read(std::string_view name) const noexcept;

private:
    static constexpr std::uint32_t kAmbiguous = 0xFFFFFFFF;

    std::filesystem::path                          path_;
    ArchiveKind                                    kind_ = ArchiveKind::Sff;
    std::uint64_t                                  file_size_ = 0;
    std::vector<sff::Entry>                        entries_;
    std::vector<std::string>                       keys_;
    std::unordered_map<std::string, std::uint32_t> index_;
};

}  // namespace sf1::data
