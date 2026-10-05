// SPDX-License-Identifier: MIT
// src/data/geometry_common.hpp — shared pieces of the .map and .cft grammars.
#pragma once

#include "binary_reader.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace sf1::data::detail {

// `count` finite float3 values.
[[nodiscard]] inline Result<std::vector<std::array<float, 3>>> read_float3_array(BinaryReader& r, std::uint32_t count) noexcept {
    auto raw = r.take_array(count, 12);
    if (!raw) return err(raw.error());
    auto out = copy_pod<std::array<float, 3>>(*raw);
    for (const auto& v : out)
        if (!all_finite(v.data(), 3)) return err(Error::NonFinite);
    return out;
}

// u32 count, then that many finite float3 values.
[[nodiscard]] inline Result<std::vector<std::array<float, 3>>> read_float3s(BinaryReader& r, std::uint32_t cap) noexcept {
    auto count = r.count(cap);
    if (!count) return err(count.error());
    return read_float3_array(r, *count);
}

// u32 index count (a multiple of 3, ≤ 10,000,000), then u16 indices below `vertex_count`.
[[nodiscard]] inline Result<std::vector<std::uint16_t>> read_indices(BinaryReader& r, std::size_t vertex_count) noexcept {
    auto count = r.count(10000000);
    if (!count) return err(count.error());
    if (*count % 3 != 0) return err(Error::Malformed);
    auto raw = r.take_array(*count, 2);
    if (!raw) return err(raw.error());
    auto out = copy_pod<std::uint16_t>(*raw);
    for (auto i : out)
        if (i >= vertex_count) return err(Error::IndexOutOfRange);
    return out;
}

}  // namespace sf1::data::detail
