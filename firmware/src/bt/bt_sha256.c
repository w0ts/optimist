/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* SHA-256 and HMAC-SHA-256 (bt_sha256.h), from FIPS PUB 180-4 §4.2.2, §5.3.3, §6.2 and RFC 2104. */
#include "bt_sha256.h"

static const uint32_t bt_sha_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t bt_ror(uint32_t x, uint32_t n) { return x >> n | x << (32u - n); }

static void bt_sha_block(struct bt_sha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2, i;
    for (i = 0; i < 16u; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 64u; i++) {
        uint32_t s0 = bt_ror(w[i - 15], 7) ^ bt_ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        uint32_t s1 = bt_ror(w[i - 2], 17) ^ bt_ror(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (i = 0; i < 64u; i++) {
        t1 = h + (bt_ror(e, 6) ^ bt_ror(e, 11) ^ bt_ror(e, 25)) + ((e & f) ^ (~e & g)) + bt_sha_k[i] + w[i];
        t2 = (bt_ror(a, 2) ^ bt_ror(a, 13) ^ bt_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

void bt_sha256_init(struct bt_sha256 *s)
{
    static const uint32_t h0[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint32_t i;
    for (i = 0; i < 8u; i++)
        s->h[i] = h0[i];
    s->nbuf = 0;
    s->len_lo = s->len_hi = 0;
}

void bt_sha256_add(struct bt_sha256 *s, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        s->buf[s->nbuf++] = p[i];
        if (s->nbuf == 64u) {
            bt_sha_block(s, s->buf);
            s->nbuf = 0;
        }
    }
    s->len_lo += n;
    if (s->len_lo < n)
        s->len_hi++;
}

void bt_sha256_done(struct bt_sha256 *s, uint8_t out[32])
{
    uint32_t hi = s->len_hi << 3 | s->len_lo >> 29, lo = s->len_lo << 3, i;
    uint8_t pad = 0x80, z = 0, len[8];
    bt_sha256_add(s, &pad, 1);
    while (s->nbuf != 56u)
        bt_sha256_add(s, &z, 1);
    for (i = 0; i < 4u; i++) {
        len[i] = (uint8_t)(hi >> (24 - 8 * i));
        len[4 + i] = (uint8_t)(lo >> (24 - 8 * i));
    }
    bt_sha256_add(s, len, 8);
    for (i = 0; i < 32u; i++)
        out[i] = (uint8_t)(s->h[i / 4u] >> (24 - 8 * (i % 4u)));
}

void bt_hmac_sha256(const uint8_t *key, uint32_t klen, const uint8_t *const *p, const uint32_t *n, uint32_t parts,
                    uint8_t out[32])
{
    struct bt_sha256 s;
    uint8_t pad[64], inner[32];
    uint32_t i;
    for (i = 0; i < 64u; i++)
        pad[i] = (uint8_t)((i < klen ? key[i] : 0) ^ 0x36u);
    bt_sha256_init(&s);
    bt_sha256_add(&s, pad, 64);
    for (i = 0; i < parts; i++)
        bt_sha256_add(&s, p[i], n[i]);
    bt_sha256_done(&s, inner);
    for (i = 0; i < 64u; i++)
        pad[i] ^= 0x36u ^ 0x5Cu;
    bt_sha256_init(&s);
    bt_sha256_add(&s, pad, 64);
    bt_sha256_add(&s, inner, 32);
    bt_sha256_done(&s, out);
}
