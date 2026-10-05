// SPDX-License-Identifier: MIT
// sf1/net/opcodes.hpp — opcode identifiers.
#pragma once
#include "../result.hpp"
#include <cstdint>
#include <string_view>

namespace sf1::net {

// Opcode integer width is Open (⏳ Encyclopedia 15). Alias defaults to u16;
// change to u32 once confirmed and re-run the extractor.
using Opcode = std::uint16_t;

struct OpcodeInfo {
    Opcode           value;
    std::string_view name;      // e.g. "AUTH_LOGIN_REQ"
    std::string_view direction; // "C2S" | "S2C"
};

[[nodiscard]] Result<OpcodeInfo> opcode_info(Opcode) noexcept;
[[nodiscard]] std::size_t        opcode_count() noexcept;

}  // namespace sf1::net
