/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). See updateverify.h.
 *
 * The Ed25519 arithmetic below follows TweetNaCl (public domain).
 */
#include "updateverify.h"

#include <cstring>
#include <sstream>

#ifndef JUBELLE_RELEASE_PUBLIC_KEY
// Set at build time (CMake: -DJUBELLE_RELEASE_PUBLIC_KEY=<64 hex digits>), from
// the public half of the key made with tools/jubelle_release/make_signing_key.py.
// Empty: this build refuses every update, because it can't verify one.
#define JUBELLE_RELEASE_PUBLIC_KEY ""
#endif

namespace muse::update::jubelle {
// ---------------------------------------------------------------------------
// SHA-256 and SHA-512 (FIPS 180-4)
// ---------------------------------------------------------------------------

static inline uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint64_t ror64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size)
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

    Bytes msg(data, data + size);
    const uint64_t bits = uint64_t(size) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(uint8_t(bits >> (8 * i)));
    }

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = uint32_t(msg[off + 4 * i]) << 24 | uint32_t(msg[off + 4 * i + 1]) << 16
                   | uint32_t(msg[off + 4 * i + 2]) << 8 | uint32_t(msg[off + 4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    std::array<uint8_t, 32> out {};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[4 * i + j] = uint8_t(h[i] >> (24 - 8 * j));
        }
    }
    return out;
}

std::array<uint8_t, 64> sha512(const uint8_t* data, size_t size)
{
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
        0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
        0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
        0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
        0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
        0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };
    uint64_t h[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                      0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };

    Bytes msg(data, data + size);
    const uint64_t bits = uint64_t(size) * 8;
    msg.push_back(0x80);
    while (msg.size() % 128 != 112) {
        msg.push_back(0);
    }
    for (int i = 0; i < 8; ++i) {
        msg.push_back(0);   // the high 64 bits of the 128-bit length
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(uint8_t(bits >> (8 * i)));
    }

    for (size_t off = 0; off < msg.size(); off += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            uint64_t v = 0;
            for (int j = 0; j < 8; ++j) {
                v = (v << 8) | msg[off + 8 * i + j];
            }
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i) {
            const uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const uint64_t S1 = ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint64_t S0 = ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39);
            const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    std::array<uint8_t, 64> out {};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            out[8 * i + j] = uint8_t(h[i] >> (56 - 8 * j));
        }
    }
    return out;
}

std::string toHex(const uint8_t* data, size_t size)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        s += digits[data[i] >> 4];
        s += digits[data[i] & 15];
    }
    return s;
}

bool fromHex(const std::string& hex, Bytes& out)
{
    auto val = [](char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    out.clear();
    if (hex.size() % 2) {
        return false;
    }
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            out.clear();
            return false;
        }
        out.push_back(uint8_t(hi * 16 + lo));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Ed25519 verification (after TweetNaCl)
// ---------------------------------------------------------------------------

namespace {
using i64 = int64_t;
using gf = i64[16];

const gf gf0 = { 0 };
const gf gf1 = { 1 };
const gf D = { 0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
               0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203 };
const gf D2 = { 0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406 };
const gf X = { 0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
               0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169 };
const gf Y = { 0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
               0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666 };
const gf I = { 0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
               0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83 };

void set25519(gf r, const gf a)
{
    for (int i = 0; i < 16; ++i) {
        r[i] = a[i];
    }
}

void car25519(gf o)
{
    for (int i = 0; i < 16; ++i) {
        o[i] += (i64(1) << 16);
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * (i64(1) << 16);
    }
}

void sel25519(gf p, gf q, int b)
{
    const i64 c = ~(i64(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(uint8_t* o, const gf n)
{
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
        o[2 * i] = uint8_t(t[i] & 0xff);
        o[2 * i + 1] = uint8_t(t[i] >> 8);
    }
}

int neq25519(const gf a, const gf b)
{
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return std::memcmp(c, d, 32) != 0;
}

uint8_t par25519(const gf a)
{
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

void unpack25519(gf o, const uint8_t* n)
{
    for (int i = 0; i < 16; ++i) {
        o[i] = n[2 * i] + (i64(n[2 * i + 1]) << 8);
    }
    o[15] &= 0x7fff;
}

void A(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] + b[i];
    }
}

void Z(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; ++i) {
        o[i] = a[i] - b[i];
    }
}

void M(gf o, const gf a, const gf b)
{
    i64 t[31] = { 0 };
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; ++i) {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; ++i) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

void S(gf o, const gf a)
{
    M(o, a, a);
}

void inv25519(gf o, const gf i)
{
    gf c;
    set25519(c, i);
    for (int a = 253; a >= 0; --a) {
        S(c, c);
        if (a != 2 && a != 4) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

void pow2523(gf o, const gf i)
{
    gf c;
    set25519(c, i);
    for (int a = 250; a >= 0; --a) {
        S(c, c);
        if (a != 1) {
            M(c, c, i);
        }
    }
    set25519(o, c);
}

void add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]);
    Z(t, q[1], q[0]);
    M(a, a, t);
    A(b, p[0], p[1]);
    A(t, q[0], q[1]);
    M(b, b, t);
    M(c, p[3], q[3]);
    M(c, c, D2);
    M(d, p[2], q[2]);
    A(d, d, d);
    Z(e, b, a);
    Z(f, d, c);
    A(g, d, c);
    A(h, b, a);
    M(p[0], e, f);
    M(p[1], h, g);
    M(p[2], g, f);
    M(p[3], e, h);
}

void cswap(gf p[4], gf q[4], uint8_t b)
{
    for (int i = 0; i < 4; ++i) {
        sel25519(p[i], q[i], b);
    }
}

void pack(uint8_t* r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= par25519(tx) << 7;
}

void scalarmult(gf p[4], gf q[4], const uint8_t* s)
{
    set25519(p[0], gf0);
    set25519(p[1], gf1);
    set25519(p[2], gf1);
    set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        add(q, p);
        add(p, p);
        cswap(p, q, b);
    }
}

void scalarbase(gf p[4], const uint8_t* s)
{
    gf q[4];
    set25519(q[0], X);
    set25519(q[1], Y);
    set25519(q[2], gf1);
    M(q[3], X, Y);
    scalarmult(p, q, s);
}

const i64 L[32] = { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };

void modL(uint8_t* r, i64 x[64])
{
    i64 carry;
    int j;
    for (int i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) {
        x[j] -= carry * L[j];
    }
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = uint8_t(x[i] & 255);
    }
}

void reduce(uint8_t* r)
{
    i64 x[64];
    for (int i = 0; i < 64; ++i) {
        x[i] = i64(r[i]);
    }
    for (int i = 0; i < 64; ++i) {
        r[i] = 0;
    }
    modL(r, x);
}

int unpackneg(gf r[4], const uint8_t p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);

    S(den2, den);
    S(den4, den2);
    M(den6, den4, den2);
    M(t, den6, num);
    M(t, t, den);

    pow2523(t, t);
    M(t, t, num);
    M(t, t, den);
    M(t, t, den);
    M(r[0], t, den);

    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) {
        M(r[0], r[0], I);
    }

    S(chk, r[0]);
    M(chk, chk, den);
    if (neq25519(chk, num)) {
        return -1;
    }

    if (par25519(r[0]) == (p[31] >> 7)) {
        Z(r[0], gf0, r[0]);
    }

    M(r[3], r[0], r[1]);
    return 0;
}

// The scalar half of a signature must be below the group order (RFC 8032 5.1.7)
bool scalarIsCanonical(const uint8_t s[32])
{
    for (int i = 31; i >= 0; --i) {
        if (s[i] < L[i]) {
            return true;
        }
        if (s[i] > L[i]) {
            return false;
        }
    }
    return false;   // equal to L
}
}

bool ed25519Verify(const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t publicKey[32])
{
    if (!signature || !publicKey || (!message && size)) {
        return false;
    }
    if (!scalarIsCanonical(signature + 32)) {
        return false;
    }
    gf p[4], q[4];
    if (unpackneg(q, publicKey)) {
        return false;
    }

    // h = SHA-512(R || A || M) mod L
    Bytes buf;
    buf.reserve(64 + size);
    buf.insert(buf.end(), signature, signature + 32);
    buf.insert(buf.end(), publicKey, publicKey + 32);
    if (size) {
        buf.insert(buf.end(), message, message + size);
    }
    std::array<uint8_t, 64> h = sha512(buf.data(), buf.size());
    reduce(h.data());

    // check [S]B = R + [h]A, i.e. [h](-A) + [S]B == R
    scalarmult(p, q, h.data());
    scalarbase(q, signature + 32);
    add(p, q);
    uint8_t t[32];
    pack(t, p);
    return std::memcmp(signature, t, 32) == 0;
}

// ---------------------------------------------------------------------------
// The release check
// ---------------------------------------------------------------------------

std::map<std::string, std::string> parseSha256Sums(const std::string& text)
{
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 66) {
            continue;
        }
        const std::string hash = line.substr(0, 64);
        Bytes check;
        if (!fromHex(hash, check)) {
            continue;
        }
        size_t nameStart = 64;
        while (nameStart < line.size() && (line[nameStart] == ' ' || line[nameStart] == '*')) {
            ++nameStart;   // "hash  name" or "hash *name" (binary mode)
        }
        if (nameStart == 64 || nameStart >= line.size()) {
            continue;
        }
        std::string lower;
        for (char c : hash) {
            lower += char(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c);
        }
        out[line.substr(nameStart)] = lower;
    }
    return out;
}

Verdict verifyInstaller(const Bytes& installer, const std::string& installerName, const std::string& sha256sums,
                        const Bytes& signature, const std::string& publicKeyHex)
{
    Bytes key;
    if (!fromHex(publicKeyHex, key) || key.size() != 32) {
        return Verdict::NoReleaseKey;
    }
    Bytes sig = signature;
    if (sig.size() != 64) {
        // a hex text file: trim and decode
        std::string text(signature.begin(), signature.end());
        std::string hex;
        for (char c : text) {
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                hex += c;
            }
        }
        if (!fromHex(hex, sig) || sig.size() != 64) {
            return Verdict::BadSignature;
        }
    }
    if (!ed25519Verify(sig.data(), reinterpret_cast<const uint8_t*>(sha256sums.data()), sha256sums.size(), key.data())) {
        return Verdict::BadSignature;
    }
    const std::map<std::string, std::string> sums = parseSha256Sums(sha256sums);
    auto it = sums.find(installerName);
    if (it == sums.end()) {
        return Verdict::NotListed;
    }
    const std::array<uint8_t, 32> digest = sha256(installer.data(), installer.size());
    return toHex(digest.data(), digest.size()) == it->second ? Verdict::Ok : Verdict::WrongChecksum;
}

std::string releasePublicKeyHex()
{
    return JUBELLE_RELEASE_PUBLIC_KEY;
}

int compareVersions(const std::string& a, const std::string& b)
{
    auto parts = [](const std::string& v) {
        std::vector<long long> out;
        long long cur = -1;
        for (char c : v) {
            if (c >= '0' && c <= '9') {
                cur = (cur < 0 ? 0 : cur) * 10 + (c - '0');
            } else {
                if (cur >= 0) {
                    out.push_back(cur);
                }
                cur = -1;
                if (c != '.' && c != 'v' && c != 'V') {
                    break;   // a suffix such as "-beta": not compared
                }
            }
        }
        if (cur >= 0) {
            out.push_back(cur);
        }
        return out;
    };
    const std::vector<long long> x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const long long xi = i < x.size() ? x[i] : 0, yi = i < y.size() ? y[i] : 0;
        if (xi != yi) {
            return xi < yi ? -1 : 1;
        }
    }
    return 0;
}
}
