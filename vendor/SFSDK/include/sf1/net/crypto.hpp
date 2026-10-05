// SPDX-License-Identifier: MIT
// sf1/net/crypto.hpp — declaration for the packet-body transform.
//
// The transform is Open (⏳ Encyclopedia 16). Every function here returns
// Error::CryptoNotDerived until the seed and routine are captured.
#pragma once
#include "../result.hpp"
#include <cstdint>
#include <span>

namespace sf1::net::crypto {

[[nodiscard]] Result<void> apply(std::span<std::uint8_t> body, std::uint32_t seed) noexcept;

}
