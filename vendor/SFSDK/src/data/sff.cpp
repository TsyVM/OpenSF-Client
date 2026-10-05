// SPDX-License-Identifier: MIT
#include "sf1/data/sff.hpp"
#include "binary_reader.hpp"

#include <algorithm>
#include <cstring>

namespace sf1::data::sff {

Result<Header> read_header(std::span<const std::byte> bytes, std::uint64_t file_size) noexcept {
    detail::BinaryReader r(bytes);
    auto count = r.u32();
    if (!count) return err(count.error());
    if (*count > kMaxEntries) return err(Error::CountOutOfRange);
    if (table_end(*count) > file_size) return err(Error::HeaderOutOfRange);
    return Header{*count, table_end(*count)};
}

Result<std::vector<Entry>> read_table(std::span<const std::byte> table, std::uint64_t file_size) noexcept {
    auto header = read_header(table, file_size);
    if (!header) return err(header.error());
    if (table.size() < header->data_base) return err(Error::ShortRead);

    std::vector<Entry> entries;
    entries.reserve(header->count);
    detail::BinaryReader r(table.subspan(4));
    for (std::uint32_t i = 0; i < header->count; ++i) {
        auto name = r.name(kNameWidth);
        auto offset = r.u32();
        auto size = r.u32();
        if (!name || !offset || !size) return err(Error::ShortRead);
        const std::uint64_t begin = header->data_base + *offset;
        if (begin + *size > file_size) return err(Error::EntryOutOfRange);
        entries.push_back(Entry{std::move(*name), begin, *size});
    }
    return entries;
}

Result<std::vector<Entry>> read_entries(std::span<const std::byte> bytes) noexcept {
    return read_table(bytes, bytes.size());
}

Result<std::vector<std::pair<Entry, std::vector<std::byte>>>>
extract_all(std::span<const std::byte> bytes) noexcept {
    auto entries = read_entries(bytes);
    if (!entries) return err(entries.error());
    std::vector<std::pair<Entry, std::vector<std::byte>>> out;
    out.reserve(entries->size());
    for (auto& e : *entries) {
        auto payload = bytes.subspan(static_cast<std::size_t>(e.offset), e.size);
        out.emplace_back(std::move(e), std::vector<std::byte>(payload.begin(), payload.end()));
    }
    return out;
}

Result<std::vector<std::byte>> write(std::span<const NewEntry> entries) {
    if (entries.size() > kMaxEntries) return err(Error::CountOutOfRange);

    std::uint64_t payload_bytes = 0;
    for (const NewEntry& e : entries) {
        if (e.name.empty() || e.name.size() >= kNameWidth) return err(Error::Malformed);
        if (!e.name_slot.empty() && e.name_slot.size() != kNameWidth) return err(Error::Malformed);
        payload_bytes += e.payload.size();
    }
    const std::uint64_t base = table_end(static_cast<std::uint32_t>(entries.size()));
    if (payload_bytes > 0xFFFFFFFFull) return err(Error::EntryOutOfRange);

    std::vector<std::byte> out(static_cast<std::size_t>(base + payload_bytes));
    const auto count = static_cast<std::uint32_t>(entries.size());
    std::memcpy(out.data(), &count, 4);

    std::uint32_t offset = 0;
    std::size_t cursor = static_cast<std::size_t>(base);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const NewEntry& e = entries[i];
        std::byte* name = out.data() + 4 + i * kEntrySize;
        if (!e.name_slot.empty()) {
            std::memcpy(name, e.name_slot.data(), kNameWidth);
        } else {
            std::fill(name, name + kNameWidth, std::byte{0xCC});
        }
        std::memcpy(name, e.name.data(), e.name.size());
        name[e.name.size()] = std::byte{0};

        const auto size = static_cast<std::uint32_t>(e.payload.size());
        std::memcpy(name + kNameWidth, &offset, 4);
        std::memcpy(name + kNameWidth + 4, &size, 4);
        if (size) std::memcpy(out.data() + cursor, e.payload.data(), size);
        cursor += size;
        offset += size;
    }
    return out;
}

}  // namespace sf1::data::sff
