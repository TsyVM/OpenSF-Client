// SPDX-License-Identifier: MIT
#include "sf1/data/env.hpp"
#include "binary_reader.hpp"

#include <cctype>
#include <cstring>

namespace sf1::data::env {

namespace {
constexpr std::uint32_t kMaxPlacements = 100000;
}

std::string_view object_stem(std::string_view source) noexcept {
    // "Drum_01_14_SECTOR" -> "Drum_01": drop a trailing "_SECTOR" and the
    // instance number in front of it.
    constexpr std::string_view kSector = "_SECTOR";
    if (source.size() <= kSector.size()) return source;

    auto tail = source.substr(source.size() - kSector.size());
    bool matches = tail.size() == kSector.size();
    for (std::size_t i = 0; matches && i < kSector.size(); ++i)
        matches = std::toupper(static_cast<unsigned char>(tail[i])) ==
                  std::toupper(static_cast<unsigned char>(kSector[i]));
    if (!matches) return source;

    std::string_view head = source.substr(0, source.size() - kSector.size());
    std::size_t end = head.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(head[end - 1]))) --end;
    if (end == head.size() || end == 0 || head[end - 1] != '_') return head;
    return head.substr(0, end - 1);
}

Result<Objects> read(std::span<const std::byte> bytes) noexcept {
    detail::BinaryReader r(bytes);
    Objects out;
    out.size = bytes.size();

    auto count = r.count(kMaxPlacements);
    if (!count) return err(count.error());

    // The shipped files end exactly on the last record; a leftover tail would mean
    // the record size is not what this reader believes it is.
    if (!r.can(static_cast<std::size_t>(*count) * kRecordSize)) return err(Error::ShortRead);
    if (r.remaining() != static_cast<std::size_t>(*count) * kRecordSize) return err(Error::TrailingBytes);

    out.placements.resize(*count);
    for (auto& p : out.placements) {
        p.offset = r.pos();

        auto instance = r.name(kNameWidth);
        if (!instance) return err(instance.error());
        auto source = r.name(kNameWidth);
        if (!source) return err(source.error());
        p.instance = std::move(*instance);
        p.source = std::move(*source);

        auto tail = r.take(kRecordSize - kNameWidth * 2);
        if (!tail) return err(tail.error());
        const auto* raw = tail->data();

        std::memcpy(p.position.data(), raw + 0, sizeof(float) * 3);
        std::memcpy(p.scale.data(), raw + 12, sizeof(float) * 3);
        // raw + 24 .. raw + 35 is the uninitialised span; nothing reads it.
        std::memcpy(p.world.data(), raw + 36, sizeof(float) * 16);

        if (!detail::all_finite(p.position.data(), 3) || !detail::all_finite(p.scale.data(), 3) ||
            !detail::all_finite(p.world.data(), 16))
            return err(Error::NonFinite);
    }
    return out;
}

}  // namespace sf1::data::env
