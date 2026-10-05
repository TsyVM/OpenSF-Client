#pragma once

#include "Engine/Core/Types.hpp"

#include <string_view>

namespace eng {

constexpr u64 fnv1a64(std::string_view s) {
    u64 h = 0xcbf29ce484222325ull;
    for (char c : s) {
        h ^= u8(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

constexpr u32 fnv1a32(std::string_view s) {
    u32 h = 0x811c9dc5u;
    for (char c : s) {
        h ^= u8(c);
        h *= 0x01000193u;
    }
    return h;
}

// Deterministic PRNG shared by client prediction and the server (weapon spread).
struct Rng {
    u64 state;
    explicit constexpr Rng(u64 seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    u32 next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return u32((state * 0x2545F4914F6CDD1Dull) >> 32);
    }
    float unit() { return float(next() & 0xFFFFFF) / float(0x1000000); }   // [0, 1)
    float signed_unit() { return unit() * 2.0f - 1.0f; }                   // [-1, 1)
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

}  // namespace eng
