#include "Game/Tvas.hpp"

#include "Engine/Core/Strings.hpp"

#include <mutex>

namespace lsf::tvas {

namespace {

// TVAS's public keys as the builds carry them (bin/keygen.php printed them on the hosting,
// 2026-10-03). A build trusts no ticket but those these sign (and a test TVAS's on loopback).
constexpr const char* kBuiltInKeys[4] = {
    nullptr,
    "e82d17dbd76e8fda3a73e700e0f697f91ce4f486fe5a2ec481ed9e6c7cf8fba2",   // key 1: the one TVAS signs with
    "36d1677fa9bdd884627af39fd9fdd67228df5e28b05c808ca983726b67319417",   // key 2: the spare, its seed kept offline (ID-5)
    nullptr,
};

std::mutex g_mutex;
Keys g_test;
bool g_test_set = false;

Keys built_in() {
    Keys k;
    for (int i = 0; i < 4; ++i) {
        if (!kBuiltInKeys[i] || std::string_view(kBuiltInKeys[i]).size() != 64) continue;
        eng::crypto::Ed25519Public pub{};
        if (eng::crypto::from_hex(kBuiltInKeys[i], pub)) k.slot[size_t(i)] = pub;
    }
    return k;
}

std::span<const eng::u8> bytes_of(std::string_view s) { return {reinterpret_cast<const eng::u8*>(s.data()), s.size()}; }

}  // namespace

std::string url(std::string_view base, std::string_view path) {
    std::string out(base);
    while (!out.empty() && out.back() == '/') out.pop_back();
    if (path.empty() || path.front() != '/') out.push_back('/');
    out += path;
    return out;
}

bool loopback(std::string_view address) {
    std::string_view a = address;
    if (a.starts_with("https://")) a.remove_prefix(8);
    else if (a.starts_with("http://")) a.remove_prefix(7);
    const size_t end = a.find_first_of(":/");
    std::string_view host = a.substr(0, end);
    if (host.starts_with("[")) host = a.substr(1, a.find(']') - 1);
    return host == "127.0.0.1" || host == "::1" || eng::str::iequals(host, "localhost") || host.starts_with("127.");
}

Keys keys() {
    static const Keys k = built_in();
    std::lock_guard lock(g_mutex);
    if (!g_test_set) return k;
    Keys merged = k;
    for (size_t i = 0; i < merged.slot.size(); ++i)
        if (g_test.slot[i]) merged.slot[i] = g_test.slot[i];
    return merged;
}

bool trust_test_keys(std::string_view address, const eng::json::Value& status_keys) {
    if (!loopback(address)) return false;
    Keys k;
    for (const auto& [id, hex] : status_keys.items()) {
        int kid = 0;
        if (!eng::str::parse_int(id, kid) || kid < 0 || kid >= 4) continue;
        eng::crypto::Ed25519Public pub{};
        if (eng::crypto::from_hex(hex.str(), pub)) k.slot[size_t(kid)] = pub;
    }
    std::lock_guard lock(g_mutex);
    g_test = k;
    g_test_set = true;
    return true;
}

void forget_test_keys() {
    std::lock_guard lock(g_mutex);
    g_test = {};
    g_test_set = false;
}

bool read_ticket(std::string_view token, Ticket& out, std::string* why) {
    auto no = [&](const char* text) {
        if (why) *why = text;
        return false;
    };
    const size_t a = token.find('.');
    const size_t b = a == std::string_view::npos ? a : token.find('.', a + 1);
    if (a == std::string_view::npos || b == std::string_view::npos) return no("not a ticket");
    int kid = 0;
    if (!eng::str::parse_int(token.substr(a + 1, b - a - 1), kid) || kid < 0 || kid >= 4) return no("not a ticket");
    std::vector<eng::u8> payload, sig;
    if (!eng::crypto::from_base64(token.substr(0, a), payload) || !eng::crypto::from_base64(token.substr(b + 1), sig) || sig.size() != 64) return no("not a ticket");
    const Keys k = keys();
    if (!k.slot[size_t(kid)]) return no("a ticket signed with a key this build does not know");
    eng::crypto::Ed25519Signature s{};
    std::copy(sig.begin(), sig.end(), s.begin());
    if (!eng::crypto::ed25519_verify(payload, s, *k.slot[size_t(kid)])) return no("a ticket whose signature does not check out");
    eng::json::Value v;
    if (!eng::json::parse(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()), v) || v["v"].as_int() != 1) return no("a ticket this build cannot read");
    out.account = v["aid"].as_uint();
    out.code_name = v["cn"].str();
    out.xp = u32(v["xp"].as_uint());
    out.rank = int(v["rank"].as_int());
    out.global_role = u8(std::min<u64>(v["grole"].as_uint(), 3));
    out.server = v["sid"].as_uint();
    out.issued = v["iat"].as_uint();
    out.expires = v["exp"].as_uint();
    out.nonce = v["nonce"].str();
    out.rev = u32(v["rev"].as_uint());
    out.muted_until = v["gmute"].as_uint();
    if (const auto& in = v["intent"]; in.is_object()) {
        out.intent_friend = in["friend"].as_uint();
        out.intent_channel = u8(in["channel"].as_uint());
        out.intent_room = u32(in["room"].as_uint());
    }
    if (!out.account || !out.server || out.nonce.size() < 16 || out.code_name.empty()) return no("a ticket with parts missing");
    return true;
}

std::string make_token(const eng::json::Value& payload, const eng::crypto::Ed25519Seed& seed, int kid) {
    const std::string body = payload.dump();
    const auto pub = eng::crypto::ed25519_public(seed);
    const auto sig = eng::crypto::ed25519_sign(bytes_of(body), seed, pub);
    return eng::crypto::base64(bytes_of(body), true) + "." + std::to_string(kid) + "." + eng::crypto::base64(sig, true);
}

bool reply_signed(std::string_view body, std::string_view header) {
    const size_t dot = header.find('.');
    int kid = 0;
    if (dot == std::string_view::npos || !eng::str::parse_int(header.substr(0, dot), kid) || kid < 0 || kid >= 4) return false;
    std::vector<eng::u8> sig;
    if (!eng::crypto::from_base64(header.substr(dot + 1), sig) || sig.size() != 64) return false;
    const Keys k = keys();
    if (!k.slot[size_t(kid)]) return false;
    eng::crypto::Ed25519Signature s{};
    std::copy(sig.begin(), sig.end(), s.begin());
    return eng::crypto::ed25519_verify(bytes_of(body), s, *k.slot[size_t(kid)]);
}

ServerKey make_server_key() {
    ServerKey k;
    eng::crypto::random_bytes(k.seed);
    k.pub = eng::crypto::ed25519_public(k.seed);
    k.valid = true;
    return k;
}

bool read_server_key(std::string_view hex, ServerKey& out) {
    hex = eng::str::trim(hex);
    if (hex.size() != 64 || !eng::crypto::from_hex(hex, out.seed)) return false;
    out.pub = eng::crypto::ed25519_public(out.seed);
    out.valid = true;
    return true;
}

std::string server_key_text(const ServerKey& k) { return eng::crypto::to_hex(k.seed); }

std::vector<std::pair<std::string, std::string>> sign_request(u64 server_id, const ServerKey& key, std::string_view method, std::string_view path,
                                                              std::string_view body, u64 unix_time) {
    eng::u8 n[16];
    eng::crypto::random_bytes(n);
    const std::string nonce = eng::crypto::to_hex(n);
    const std::string time = std::to_string(unix_time);
    const std::string message =
        std::string(method) + "\n" + std::string(path) + "\n" + time + "\n" + nonce + "\n" + eng::crypto::to_hex(eng::crypto::sha256(body));
    const auto sig = eng::crypto::ed25519_sign(bytes_of(message), key.seed, key.pub);
    return {{"X-TVAS-Server", std::to_string(server_id)}, {"X-TVAS-Time", time}, {"X-TVAS-Nonce", nonce}, {"X-TVAS-Sig", eng::crypto::base64(sig)}};
}

std::string path_of(std::string_view full) {
    std::string_view a = full;
    if (a.starts_with("https://")) a.remove_prefix(8);
    else if (a.starts_with("http://")) a.remove_prefix(7);
    const size_t slash = a.find('/');
    return slash == std::string_view::npos ? std::string("/") : std::string(a.substr(slash));
}

}  // namespace lsf::tvas
