// SPDX-License-Identifier: MIT
// sf1/patch/client_fix.hpp — pre-canned client fixes.
#pragma once
#include "../runtime/patch.hpp"
#include <cstdint>

namespace sf1::patch {

// Verified @3c7699: 60 fps unlock ported from the NiHooks build.
[[nodiscard]] Result<runtime::Patch> unlock_framerate(std::uint32_t target_fps = 60) noexcept;

// Reasoned — not yet implemented.
[[nodiscard]] Result<runtime::Patch> widescreen(std::uint32_t w, std::uint32_t h) noexcept;

// Reasoned — not yet implemented.
[[nodiscard]] Result<runtime::Patch> fix_ping_display() noexcept;

}
