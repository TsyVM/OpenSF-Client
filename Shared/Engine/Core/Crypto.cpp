#include "Engine/Core/Crypto.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstring>
#include <random>

namespace eng::crypto {

#ifdef _WIN32

Sha256 sha256(std::span<const u8> data) {
    Sha256 out{};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()), ULONG(data.size()), out.data(), ULONG(out.size()));
    return out;
}

void random_bytes(std::span<u8> out) {
    BCryptGenRandom(nullptr, out.data(), ULONG(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

#else

// FIPS 180-4 SHA-256, for the platforms without a system hash to call.
namespace {
constexpr u32 kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
void block(u32 h[8], const u8* p) {
    u32 w[64];
    for (int i = 0; i < 16; ++i) w[i] = (u32(p[i * 4]) << 24) | (u32(p[i * 4 + 1]) << 16) | (u32(p[i * 4 + 2]) << 8) | u32(p[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const u32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const u32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}
}  // namespace

Sha256 sha256(std::span<const u8> data) {
    u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= data.size(); i += 64) block(h, data.data() + i);
    u8 tail[128] = {};
    const size_t rest = data.size() - i;
    std::memcpy(tail, data.data() + i, rest);
    tail[rest] = 0x80;
    const size_t total = rest + 9 <= 64 ? 64 : 128;
    const u64 bits = u64(data.size()) * 8;
    for (int k = 0; k < 8; ++k) tail[total - 1 - k] = u8(bits >> (8 * k));
    block(h, tail);
    if (total == 128) block(h, tail + 64);
    Sha256 out{};
    for (int k = 0; k < 8; ++k) {
        out[size_t(k) * 4] = u8(h[k] >> 24);
        out[size_t(k) * 4 + 1] = u8(h[k] >> 16);
        out[size_t(k) * 4 + 2] = u8(h[k] >> 8);
        out[size_t(k) * 4 + 3] = u8(h[k]);
    }
    return out;
}

void random_bytes(std::span<u8> out) {
    const int fd = open("/dev/urandom", O_RDONLY);
    size_t got = 0;
    if (fd >= 0) {
        while (got < out.size()) {
            const ssize_t n = read(fd, out.data() + got, out.size() - got);
            if (n <= 0) break;
            got += size_t(n);
        }
        close(fd);
    }
    if (got < out.size()) {
        std::random_device rd;
        for (size_t k = got; k < out.size(); ++k) out[k] = u8(rd());
    }
}

#endif

Sha256 sha256(std::string_view text) { return sha256(std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())); }

std::string to_hex(std::span<const u8> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (u8 b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

bool from_hex(std::string_view hex, std::span<u8> out) {
    if (hex.size() != out.size() * 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = nib(hex[i * 2]), lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = u8((hi << 4) | lo);
    }
    return true;
}

bool equal(std::span<const u8> a, std::span<const u8> b) {
    if (a.size() != b.size()) return false;
    u8 diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= u8(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace eng::crypto
