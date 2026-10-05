#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eng::crypto {

using Sha256 = std::array<u8, 32>;
using Sha512 = std::array<u8, 64>;

Sha256 sha256(std::span<const u8> data);
Sha256 sha256(std::string_view text);
Sha512 sha512(std::span<const u8> data);
// Cryptographically random bytes from the OS.
void random_bytes(std::span<u8> out);

std::string to_hex(std::span<const u8> bytes);
bool from_hex(std::string_view hex, std::span<u8> out);
// RFC 4648 base64; `url`: the URL-safe alphabet without padding. Reading takes either alphabet,
// padded or not; false on anything else.
std::string base64(std::span<const u8> bytes, bool url = false);
bool from_base64(std::string_view text, std::vector<u8>& out);

// Constant-time comparison.
bool equal(std::span<const u8> a, std::span<const u8> b);

// Ed25519 (RFC 8032; Ed25519.cpp): a key is its 32-byte seed, kept secret, and the public half made
// from it. The same signatures as libsodium's crypto_sign_detached.
using Ed25519Seed = std::array<u8, 32>;
using Ed25519Public = std::array<u8, 32>;
using Ed25519Signature = std::array<u8, 64>;
Ed25519Public ed25519_public(const Ed25519Seed& seed);
Ed25519Signature ed25519_sign(std::span<const u8> message, const Ed25519Seed& seed, const Ed25519Public& pub);
bool ed25519_verify(std::span<const u8> message, const Ed25519Signature& sig, const Ed25519Public& pub);

}  // namespace eng::crypto
