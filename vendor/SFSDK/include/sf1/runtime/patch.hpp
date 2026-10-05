// SPDX-License-Identifier: MIT
// sf1/runtime/patch.hpp — RAII byte patch. Restores on destruction.
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf1::runtime {

class Patch {
public:
    Patch() = default;
    Patch(const Patch&) = delete;
    Patch& operator=(const Patch&) = delete;
    Patch(Patch&&) noexcept;
    Patch& operator=(Patch&&) noexcept;
    ~Patch();

    // Overwrite `len` bytes at `addr` with `bytes`. Original bytes are saved.
    [[nodiscard]] static Result<Patch> install(void* addr, std::span<const std::uint8_t> bytes) noexcept;

    // NOP the range [addr, addr+len). Convenience over install().
    [[nodiscard]] static Result<Patch> nop(void* addr, std::size_t len) noexcept;

    void revert() noexcept;  // called by dtor

private:
    void*                       addr_ = nullptr;
    std::vector<std::uint8_t>   saved_;
    bool                        installed_ = false;
};

}  // namespace sf1::runtime
