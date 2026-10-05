// SPDX-License-Identifier: MIT
#include "sf1/data/mrg.hpp"

namespace sf1::data::mrg {

void decode(std::span<std::byte> payload) noexcept {
    for (auto& b : payload) b ^= kXorKey;
}

Result<std::vector<std::pair<Entry, std::vector<std::byte>>>>
extract_all(std::span<const std::byte> bytes) noexcept {
    auto out = sff::extract_all(bytes);
    if (!out) return err(out.error());
    for (auto& [entry, payload] : *out) decode(payload);
    return out;
}

}  // namespace sf1::data::mrg
