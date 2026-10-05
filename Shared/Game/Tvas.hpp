// What the game and the server both need of Team Vanilla's account service (TVAS,
// Docs/UniversalServerDeploy.md §4.1, §5): its address, its public keys, reading a join ticket it
// signed, checking a list it signed, and a server signing its own requests to it.
//
// TVAS is known only by its address and its interface (TV-11). The address is a setting with
// kTvasAddress as its default (TV-15); a test points at its own TVAS on loopback.
#pragma once

#include "Engine/Core/Crypto.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lsf::tvas {

using eng::u32;
using eng::u64;
using eng::u8;

inline constexpr const char* kTvasAddress = "https://api.teamvanilla.dev/";

// "https://api.teamvanilla.dev/" + "/v1/me" -> "https://api.teamvanilla.dev/v1/me".
std::string url(std::string_view base, std::string_view path);
// Whether an address is this machine (a test's TVAS): only then may plain http and keys TVAS itself
// hands out be used.
bool loopback(std::string_view address);

// TVAS's public keys (ID-5): two slots built into every build, so a leaked key is retired without
// stranding anyone. A test's own TVAS on loopback may add its keys (trust_test_keys).
struct Keys {
    std::array<std::optional<eng::crypto::Ed25519Public>, 4> slot;   // by key id (1, 2; 0 and 3 unused)
};
Keys keys();
// The keys a loopback TVAS says it signs with (its /v1/status "keys"), trusted for this run only.
// Refused (false) for any address that is not loopback.
bool trust_test_keys(std::string_view address, const eng::json::Value& status_keys);
void forget_test_keys();

// A join ticket (ID-3), as TVAS signed it.
struct Ticket {
    u64 account = 0;
    std::string code_name;
    u32 xp = 0;
    int rank = 0;
    u8 global_role = 0;
    u64 server = 0;
    u64 issued = 0, expires = 0;
    std::string nonce;
    u32 rev = 0;
    u64 muted_until = 0;
    // FR-2: "into <friend>'s room", when the join is a friend's or an invitation's.
    u64 intent_friend = 0;
    u8 intent_channel = 0;
    u32 intent_room = 0;
};
// Reads and checks a ticket's signature (nothing else: the server checks audience, time and nonce
// itself, ID-4). False and `why` when it is not TVAS's.
bool read_ticket(std::string_view token, Ticket& out, std::string* why = nullptr);
// A ticket's payload signed with `seed` as key `kid` (tests, and TVAS's own format checked in C++).
std::string make_token(const eng::json::Value& payload, const eng::crypto::Ed25519Seed& seed, int kid);

// SL-4: a reply TVAS signed (its X-TVAS-Sig header, "kid.base64", over the exact body).
bool reply_signed(std::string_view body, std::string_view sig_header);

// A server's own key (§8.1): the seed in server.key, hex.
struct ServerKey {
    eng::crypto::Ed25519Seed seed{};
    eng::crypto::Ed25519Public pub{};
    bool valid = false;
};
ServerKey make_server_key();
bool read_server_key(std::string_view hex_seed, ServerKey& out);
std::string server_key_text(const ServerKey& k);   // the seed as hex, for server.key
// The headers that sign one request to TVAS (§8.2): X-TVAS-Server, -Time, -Nonce, -Sig over
// "METHOD\nPATH\nTIME\nNONCE\nhex sha256(body)".
std::vector<std::pair<std::string, std::string>> sign_request(u64 server_id, const ServerKey& key, std::string_view method, std::string_view path,
                                                              std::string_view body, u64 unix_time);
// The path part of a full address ("https://x/v1/me?a" -> "/v1/me?a").
std::string path_of(std::string_view full_url);

}  // namespace lsf::tvas
