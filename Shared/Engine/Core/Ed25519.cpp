// SHA-512, Ed25519 (RFC 8032) and base64, for Team Vanilla's account service: join tickets and the
// server list are signed by TVAS, a server's heartbeats and match reports by the server's own key
// (Docs/UniversalServerDeploy.md §5, §8). The curve arithmetic is TweetNaCl's (public domain):
// small and constant-time, which is what a signature a few times a minute needs. The same keys and
// signatures as libsodium's crypto_sign (PHP's sodium_crypto_sign_*), which is what TVAS uses.
#include "Engine/Core/Crypto.hpp"

#include <cstring>

namespace eng::crypto {

// ── SHA-512 (FIPS 180-4) ───────────────────────────────────────────────────────

namespace {

constexpr u64 kK512[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};

u64 rotr64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

void block512(u64 h[8], const u8* p) {
    u64 w[80];
    for (int i = 0; i < 16; ++i) {
        u64 v = 0;
        for (int k = 0; k < 8; ++k) v = (v << 8) | p[i * 8 + k];
        w[i] = v;
    }
    for (int i = 16; i < 80; ++i) {
        const u64 s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const u64 s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u64 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; ++i) {
        const u64 t1 = hh + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + kK512[i] + w[i];
        const u64 t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

}  // namespace

Sha512 sha512(std::span<const u8> data) {
    u64 h[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    size_t i = 0;
    for (; i + 128 <= data.size(); i += 128) block512(h, data.data() + i);
    u8 tail[256] = {};
    const size_t rest = data.size() - i;
    if (rest) std::memcpy(tail, data.data() + i, rest);
    tail[rest] = 0x80;
    const size_t total = rest + 17 <= 128 ? 128 : 256;
    const u64 bits = u64(data.size()) * 8;   // the length's top 64 bits stay zero
    for (int k = 0; k < 8; ++k) tail[total - 1 - k] = u8(bits >> (8 * k));
    block512(h, tail);
    if (total == 256) block512(h, tail + 128);
    Sha512 out{};
    for (int k = 0; k < 8; ++k)
        for (int b = 0; b < 8; ++b) out[size_t(k) * 8 + size_t(b)] = u8(h[k] >> (56 - 8 * b));
    return out;
}

// ── Ed25519 (TweetNaCl's field and group arithmetic) ───────────────────────────

namespace {

using gf = i64[16];

constexpr gf gf0 = {0};
constexpr gf gf1 = {1};
constexpr gf kD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
constexpr gf kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
constexpr gf kX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr gf kY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr gf kI = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
// The group's order, little-endian.
constexpr i64 kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void set25519(gf r, const gf a) {
    for (int i = 0; i < 16; ++i) r[i] = a[i];
}

void car25519(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += i64(1) << 16;
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

void sel25519(gf p, gf q, int b) {
    const i64 c = ~(i64(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(u8* o, const gf n) {
    gf m, t;
    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = int((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = u8(t[i] & 0xff);
        o[2 * i + 1] = u8(t[i] >> 8);
    }
}

bool neq25519(const gf a, const gf b) {
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return !equal(std::span<const u8>(c, 32), std::span<const u8>(d, 32));
}

u8 par25519(const gf a) {
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

void unpack25519(gf o, const u8* n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (i64(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void fadd(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void fsub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}
void fmul(gf o, const gf a, const gf b) {
    i64 t[31] = {};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o);
    car25519(o);
}
void fsq(gf o, const gf a) { fmul(o, a, a); }

void inv25519(gf o, const gf i) {
    gf c;
    set25519(c, i);
    for (int a = 253; a >= 0; --a) {
        fsq(c, c);
        if (a != 2 && a != 4) fmul(c, c, i);
    }
    set25519(o, c);
}

void pow2523(gf o, const gf i) {
    gf c;
    set25519(c, i);
    for (int a = 250; a >= 0; --a) {
        fsq(c, c);
        if (a != 1) fmul(c, c, i);
    }
    set25519(o, c);
}

void padd(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    fsub(a, p[1], p[0]);
    fsub(t, q[1], q[0]);
    fmul(a, a, t);
    fadd(b, p[0], p[1]);
    fadd(t, q[0], q[1]);
    fmul(b, b, t);
    fmul(c, p[3], q[3]);
    fmul(c, c, kD2);
    fmul(d, p[2], q[2]);
    fadd(d, d, d);
    fsub(e, b, a);
    fsub(f, d, c);
    fadd(g, d, c);
    fadd(h, b, a);
    fmul(p[0], e, f);
    fmul(p[1], h, g);
    fmul(p[2], g, f);
    fmul(p[3], e, h);
}

void cswap(gf p[4], gf q[4], u8 b) {
    for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b);
}

void pack_point(u8* r, gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    fmul(tx, p[0], zi);
    fmul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= u8(par25519(tx) << 7);
}

void scalarmult(gf p[4], gf q[4], const u8* s) {
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const u8 b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        padd(q, p);
        padd(p, p);
        cswap(p, q, b);
    }
}

void scalarbase(gf p[4], const u8* s) {
    gf q[4];
    set25519(q[0], kX);
    set25519(q[1], kY);
    set25519(q[2], gf1);
    fmul(q[3], kX, kY);
    scalarmult(p, q, s);
}

void modL(u8* r, i64 x[64]) {
    for (int i = 63; i >= 32; --i) {
        i64 carry = 0;
        int j = i - 32;
        for (; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    i64 carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * kL[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) x[j] -= carry * kL[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = u8(x[i] & 255);
    }
}

void reduce64(u8* r) {
    i64 x[64];
    for (int i = 0; i < 64; ++i) x[i] = i64(r[i]);
    for (int i = 0; i < 64; ++i) r[i] = 0;
    modL(r, x);
}

int unpackneg(gf r[4], const u8 p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    fsq(num, r[1]);
    fmul(den, num, kD);
    fsub(num, num, r[2]);
    fadd(den, r[2], den);
    fsq(den2, den);
    fsq(den4, den2);
    fmul(den6, den4, den2);
    fmul(t, den6, num);
    fmul(t, t, den);
    pow2523(t, t);
    fmul(t, t, num);
    fmul(t, t, den);
    fmul(t, t, den);
    fmul(r[0], t, den);
    fsq(chk, r[0]);
    fmul(chk, chk, den);
    if (neq25519(chk, num)) fmul(r[0], r[0], kI);
    fsq(chk, r[0]);
    fmul(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    if (par25519(r[0]) == (p[31] >> 7)) fsub(r[0], gf0, r[0]);
    fmul(r[3], r[0], r[1]);
    return 0;
}

// S < L: a signature with another encoding of the same S is refused, as libsodium refuses it.
bool canonical_scalar(const u8* s) {
    for (int i = 31; i >= 0; --i) {
        if (i64(s[i]) < kL[i]) return true;
        if (i64(s[i]) > kL[i]) return false;
    }
    return false;
}

void expand_seed(const Ed25519Seed& seed, u8 d[64]) {
    const Sha512 h = sha512(seed);
    std::memcpy(d, h.data(), 64);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
}

}  // namespace

Ed25519Public ed25519_public(const Ed25519Seed& seed) {
    u8 d[64];
    expand_seed(seed, d);
    gf p[4];
    scalarbase(p, d);
    Ed25519Public out{};
    pack_point(out.data(), p);
    return out;
}

Ed25519Signature ed25519_sign(std::span<const u8> message, const Ed25519Seed& seed, const Ed25519Public& pub) {
    u8 d[64];
    expand_seed(seed, d);
    // r = H(prefix || M), R = rB, h = H(R || A || M), S = r + h a (mod L).
    std::vector<u8> buf(64 + message.size());
    std::memcpy(buf.data() + 32, d + 32, 32);
    if (!message.empty()) std::memcpy(buf.data() + 64, message.data(), message.size());
    Sha512 rh = sha512(std::span<const u8>(buf.data() + 32, 32 + message.size()));
    u8 r[64];
    std::memcpy(r, rh.data(), 64);
    reduce64(r);
    gf p[4];
    scalarbase(p, r);
    Ed25519Signature sig{};
    pack_point(sig.data(), p);
    std::memcpy(buf.data(), sig.data(), 32);
    std::memcpy(buf.data() + 32, pub.data(), 32);
    Sha512 hh = sha512(buf);
    u8 h[64];
    std::memcpy(h, hh.data(), 64);
    reduce64(h);
    i64 x[64] = {};
    for (int i = 0; i < 32; ++i) x[i] = i64(r[i]);
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += i64(h[i]) * i64(d[j]);
    modL(sig.data() + 32, x);
    return sig;
}

bool ed25519_verify(std::span<const u8> message, const Ed25519Signature& sig, const Ed25519Public& pub) {
    if (!canonical_scalar(sig.data() + 32)) return false;
    gf q[4], p[4];
    if (unpackneg(q, pub.data())) return false;
    std::vector<u8> buf(64 + message.size());
    std::memcpy(buf.data(), sig.data(), 32);
    std::memcpy(buf.data() + 32, pub.data(), 32);
    if (!message.empty()) std::memcpy(buf.data() + 64, message.data(), message.size());
    Sha512 hh = sha512(buf);
    u8 h[64];
    std::memcpy(h, hh.data(), 64);
    reduce64(h);
    scalarmult(p, q, h);
    scalarbase(q, sig.data() + 32);
    padd(p, q);
    u8 t[32];
    pack_point(t, p);
    return equal(std::span<const u8>(sig.data(), 32), std::span<const u8>(t, 32));
}

// ── base64 ─────────────────────────────────────────────────────────────────────

std::string base64(std::span<const u8> bytes, bool url) {
    const char* digits = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                             : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const u32 v = (u32(bytes[i]) << 16) | (u32(bytes[i + 1]) << 8) | u32(bytes[i + 2]);
        out.push_back(digits[(v >> 18) & 63]);
        out.push_back(digits[(v >> 12) & 63]);
        out.push_back(digits[(v >> 6) & 63]);
        out.push_back(digits[v & 63]);
    }
    if (const size_t rest = bytes.size() - i; rest) {
        const u32 v = (u32(bytes[i]) << 16) | (rest == 2 ? u32(bytes[i + 1]) << 8 : 0);
        out.push_back(digits[(v >> 18) & 63]);
        out.push_back(digits[(v >> 12) & 63]);
        if (rest == 2) out.push_back(digits[(v >> 6) & 63]);
        if (!url) out.append(rest == 2 ? "=" : "==");
    }
    return out;
}

bool from_base64(std::string_view text, std::vector<u8>& out) {
    out.clear();
    out.reserve(text.size() * 3 / 4);
    u32 acc = 0;
    int bits = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else return false;
        acc = (acc << 6) | u32(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(u8((acc >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace eng::crypto
