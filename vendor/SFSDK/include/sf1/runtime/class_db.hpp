// SPDX-License-Identifier: MIT
// sf1/runtime/class_db.hpp — generated class DB lookup surface.
//
// The table itself lives in class_db.inl, generated from data/classes.csv by
// tools/extract_classes.py. The file is intentionally empty until the class
// model is recovered from an unpacked dump (see Encyclopedia 13).
#pragma once

#include "../result.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sf1::runtime {

struct ClassInfo {
    std::string_view name;
    std::uintptr_t   vtable_rva;
    std::size_t      instance_size;
    std::string_view base_name;
    std::size_t      method_count;
};

[[nodiscard]] Result<ClassInfo> class_info(std::string_view name) noexcept;
[[nodiscard]] Result<ClassInfo> class_by_vtable(std::uintptr_t vtable_rva) noexcept;

// identify(obj) — vtable-shape guess for an unknown live pointer.
[[nodiscard]] Result<ClassInfo> identify(const void* obj) noexcept;

// Enumerate every class known to the DB.
[[nodiscard]] std::size_t class_count() noexcept;

}  // namespace sf1::runtime
