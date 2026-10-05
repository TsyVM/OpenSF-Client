// SPDX-License-Identifier: MIT
// sf1/data/sff.hpp — reader for the .sff shipped-asset container.
//
// Verified 2026-09-15 against every archive in the installed client (371 .sff and
// 103 .mrg; the table accounts for every byte of every file, 0 mismatches):
//
//   u32 count
//   count × 136-byte entries:
//       char name[128]    NUL-terminated; bytes after the NUL are padding (0xCC)
//       u32  offset       relative to the end of the table, 4 + 136 × count
//       u32  size
//   payloads
//
// Names use backslashes and are cp949 bytes. .sff payloads are stored as-is;
// .mrg shares this table and XORs its payloads (see mrg.hpp). Names are not
// unique: 11 entries in the client differ from another only by letter case.
// File-backed access with name lookup lives in archive.hpp.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sf1::data::sff {

inline constexpr std::size_t   kEntrySize  = 136;
inline constexpr std::size_t   kNameWidth  = 128;
inline constexpr std::uint32_t kMaxEntries = 100000;   // largest shipped table: 2,269

struct Header {
    std::uint32_t count = 0;       // number of entries
    std::uint64_t data_base = 0;   // absolute offset of the first payload byte, 4 + 136 × count
};

struct Entry {
    std::string   name;            // as stored: backslashes, original case
    std::uint64_t offset = 0;      // absolute byte offset into the file
    std::uint32_t size = 0;        // payload size in bytes
};

[[nodiscard]] constexpr std::uint64_t table_end(std::uint32_t count) noexcept {
    return 4 + std::uint64_t{count} * kEntrySize;
}

// Read the count and check the table fits in a file of `file_size` bytes.
// `bytes` needs only the first 4 bytes of the file.
[[nodiscard]] Result<Header> read_header(std::span<const std::byte> bytes, std::uint64_t file_size) noexcept;
[[nodiscard]] inline Result<Header> read_header(std::span<const std::byte> bytes) noexcept {
    return read_header(bytes, bytes.size());
}

// Parse the table. `table` must hold at least the first table_end(count) bytes of
// the file; every entry is checked to lie inside `file_size`.
[[nodiscard]] Result<std::vector<Entry>> read_table(std::span<const std::byte> table, std::uint64_t file_size) noexcept;

// Parse the table of a whole file held in memory.
[[nodiscard]] Result<std::vector<Entry>> read_entries(std::span<const std::byte> bytes) noexcept;

// Every entry with a copy of its payload, as stored.
[[nodiscard]] Result<std::vector<std::pair<Entry, std::vector<std::byte>>>>
extract_all(std::span<const std::byte> bytes) noexcept;

// ── Writing ─────────────────────────────────────────────────────────────────
// The shipped archives store payloads back to back in table order (all 7,457
// entries of the 46 area archives), and most pad names with 0xCC after the NUL;
// a few carry stack garbage there instead, which `name_slot` can carry across.
struct NewEntry {
    std::string                 name;        // as the client expects it: backslashes, cp949
    std::span<const std::byte>  payload;     // stored as-is (an .sff never encodes)
    std::span<const std::byte>  name_slot;   // optional: the original 128-byte slot, kept verbatim
};

// A complete .sff holding `entries` in the order given. Fails when a name does not
// fit its slot (127 bytes and a NUL), there are too many entries, or the payloads
// would push an offset past 4 GB.
[[nodiscard]] Result<std::vector<std::byte>> write(std::span<const NewEntry> entries);

}  // namespace sf1::data::sff
