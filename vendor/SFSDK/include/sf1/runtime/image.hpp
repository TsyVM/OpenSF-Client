// SPDX-License-Identifier: MIT
// sf1/runtime/image.hpp — in-process PE image handle + rebasing + pattern scan.
//
// Header-only. Consumed by the hook layer and by anything that turns an address
// tagged @<build-hash> into a runtime pointer.
#pragma once

#include "../result.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace sf1::runtime {

class Image {
public:
    // The main module (SpecialForce.exe).
    [[nodiscard]] static Result<Image> current() noexcept;

    // A named DLL (e.g. "BVEPatches.dll").
    [[nodiscard]] static Result<Image> by_name(std::string_view module_name) noexcept;

    [[nodiscard]] std::uintptr_t base() const noexcept { return base_; }
    [[nodiscard]] std::size_t    size() const noexcept { return size_; }

    // Rebase a build-tagged address (recorded against a known preferred image base)
    // to a runtime pointer in this image.
    [[nodiscard]] void* rebase(std::uintptr_t preferred_va,
                               std::uintptr_t preferred_base = 0x00400000) const noexcept;

    // IDA-style pattern scan, wildcard `??`. Returns Error::PatternAmbiguous
    // if >1 hit. Backed by VanHooks' scanner when the adapter is enabled.
    [[nodiscard]] Result<void*> find_pattern(std::string_view pattern) const noexcept;

private:
    std::uintptr_t base_ = 0;
    std::size_t    size_ = 0;
};

}  // namespace sf1::runtime
