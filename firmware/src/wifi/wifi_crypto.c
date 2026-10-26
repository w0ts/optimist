/* SPDX-License-Identifier: GPL-3.0-only */
/* WPA2-PSK crypto (wifi_crypto.h). Small over fast: run a few times per join, never per frame. */
#include "wifi_crypto.h"

/* ---- SHA-1 (FIPS 180-4) */
static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80], a, b, c, d, e, t, i;
    for (i = 0; i < 16u; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 80u; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (i = 0; i < 80u; i++) {
        uint32_t f, k;
        if (i < 20u)
            f = (b & c) | (~b & d), k = 0x5A827999u;
        else if (i < 40u)
            f = b ^ c ^ d, k = 0x6ED9EBA1u;
        else if (i < 60u)
            f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
        else
            f = b ^ c ^ d, k = 0xCA62C1D6u;
        t = rol(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rol(b, 30), b = a, a = t;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
}

void wc_sha1_init(struct wc_sha1 *s)
{
    s->h[0] = 0x67452301u, s->h[1] = 0xEFCDAB89u, s->h[2] = 0x98BADCFEu, s->h[3] = 0x10325476u, s->h[4] = 0xC3D2E1F0u;
    s->n = 0;
    s->bn = 0;
}

void wc_sha1_add(struct wc_sha1 *s, const uint8_t *p, uint32_t n)
{
    s->n += n;
    while (n--) {
        s->b[s->bn++] = *p++;
        if (s->bn == 64u) {
            sha1_block(s->h, s->b);
            s->bn = 0;
        }
    }
}

void wc_sha1_done(struct wc_sha1 *s, uint8_t out[20])
{
    uint64_t bits = s->n * 8u;
    uint8_t pad = 0x80;
    uint32_t i;
    wc_sha1_add(s, &pad, 1);
    pad = 0;
    while (s->bn != 56u)
        wc_sha1_add(s, &pad, 1);
    for (i = 0; i < 8u; i++) {
        uint8_t c = (uint8_t)(bits >> (56 - 8 * i));
        wc_sha1_add(s, &c, 1);
    }
    for (i = 0; i < 20u; i++)
        out[i] = (uint8_t)(s->h[i / 4u] >> (24 - 8 * (i % 4u)));
}

/* ---- HMAC-SHA1 (RFC 2104) */
static void hmac_start(struct wc_sha1 *s, const uint8_t *key, uint32_t kn, uint8_t x)
{
    uint8_t k[64], d[20];
    uint32_t i;
    if (kn > 64u) {
        wc_sha1_init(s);
        wc_sha1_add(s, key, kn);
        wc_sha1_done(s, d);
        key = d;
        kn = 20;
    }
    for (i = 0; i < 64u; i++)
        k[i] = (uint8_t)((i < kn ? key[i] : 0u) ^ x);
    wc_sha1_init(s);
    wc_sha1_add(s, k, 64);
}

void wc_hmac_sha1(const uint8_t *key, uint32_t kn, const uint8_t *p, uint32_t n, uint8_t out[20])
{
    struct wc_sha1 s;
    uint8_t in[20];
    hmac_start(&s, key, kn, 0x36);
    wc_sha1_add(&s, p, n);
    wc_sha1_done(&s, in);
    hmac_start(&s, key, kn, 0x5C);
    wc_sha1_add(&s, in, 20);
    wc_sha1_done(&s, out);
}

/* ---- the PMK (PBKDF2, RFC 8018, 4096 rounds, two blocks of 20 for 32 bytes) */
void wc_pmk(const char *pass, const uint8_t *ssid, uint32_t ssid_n, uint8_t pmk[32])
{
    uint8_t u[20], t[20], salt[32 + 4];
    uint32_t pn = 0, blk, i, j;
    while (pass[pn])
        pn++;
    for (i = 0; i < ssid_n && i < 32u; i++)
        salt[i] = ssid[i];
    for (blk = 1; blk <= 2u; blk++) {
        salt[i] = 0, salt[i + 1] = 0, salt[i + 2] = 0, salt[i + 3] = (uint8_t)blk;
        wc_hmac_sha1((const uint8_t *)pass, pn, salt, i + 4u, u);
        for (j = 0; j < 20u; j++)
            t[j] = u[j];
        for (j = 1; j < 4096u; j++) {
            uint32_t k;
            wc_hmac_sha1((const uint8_t *)pass, pn, u, 20, u);
            for (k = 0; k < 20u; k++)
                t[k] ^= u[k];
        }
        for (j = 0; j < 20u && (blk - 1u) * 20u + j < 32u; j++)
            pmk[(blk - 1u) * 20u + j] = t[j];
    }
}

/* ---- the PRF (§12.7.1.2): HMAC-SHA1(K, A || 0 || B || i) for i = 0, 1, ... */
void wc_prf(const uint8_t *k, uint32_t kn, const char *label, const uint8_t *b, uint32_t bn, uint8_t *out, uint32_t out_n)
{
    uint8_t m[128], d[20];
    uint32_t ln = 0, n, i, done = 0;
    while (label[ln])
        ln++;
    for (i = 0; i < ln; i++)
        m[i] = (uint8_t)label[i];
    m[ln] = 0;
    for (i = 0; i < bn; i++)
        m[ln + 1u + i] = b[i];
    n = ln + 1u + bn;
    for (i = 0; done < out_n; i++) {
        uint32_t j;
        m[n] = (uint8_t)i;
        wc_hmac_sha1(k, kn, m, n + 1u, d);
        for (j = 0; j < 20u && done < out_n; j++)
            out[done++] = d[j];
    }
}

/* ---- AES-128 (FIPS-197) */
static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d,
    0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb,
    0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
    0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9,
    0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static uint8_t xt(uint8_t x) { return (uint8_t)(x << 1 ^ (x & 0x80u ? 0x1Bu : 0u)); }
static uint8_t mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1u)
            r ^= a;
        a = xt(a);
        b >>= 1;
    }
    return r;
}
static uint8_t inv_sbox(uint8_t y)                 /* (the inverse S-box by search: a few unwraps per join) */
{
    uint32_t i;
    for (i = 0; i < 256u; i++)
        if (SBOX[i] == y)
            return (uint8_t)i;
    return 0;
}

static void expand(const uint8_t key[16], uint8_t rk[176])
{
    uint8_t rc = 1;
    uint32_t i;
    for (i = 0; i < 16u; i++)
        rk[i] = key[i];
    for (i = 16; i < 176u; i += 4) {
        uint8_t t0 = rk[i - 4], t1 = rk[i - 3], t2 = rk[i - 2], t3 = rk[i - 1];
        if (i % 16u == 0) {
            uint8_t s = t0;
            t0 = (uint8_t)(SBOX[t1] ^ rc), t1 = SBOX[t2], t2 = SBOX[t3], t3 = SBOX[s];
            rc = xt(rc);
        }
        rk[i] = rk[i - 16] ^ t0, rk[i + 1] = rk[i - 15] ^ t1, rk[i + 2] = rk[i - 14] ^ t2, rk[i + 3] = rk[i - 13] ^ t3;
    }
}

void wc_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176], s[16], t[16];
    uint32_t r, i, c;
    expand(key, rk);
    for (i = 0; i < 16u; i++)
        s[i] = in[i] ^ rk[i];
    for (r = 1; r <= 10u; r++) {
        for (i = 0; i < 16u; i++)                  /* SubBytes + ShiftRows (column-major state) */
            t[i] = SBOX[s[(i + 4u * (i % 4u)) % 16u]];
        if (r < 10u)
            for (c = 0; c < 4u; c++) {             /* MixColumns */
                uint8_t *p = t + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                p[0] = (uint8_t)(xt(a0) ^ xt(a1) ^ a1 ^ a2 ^ a3);
                p[1] = (uint8_t)(a0 ^ xt(a1) ^ xt(a2) ^ a2 ^ a3);
                p[2] = (uint8_t)(a0 ^ a1 ^ xt(a2) ^ xt(a3) ^ a3);
                p[3] = (uint8_t)(xt(a0) ^ a0 ^ a1 ^ a2 ^ xt(a3));
            }
        for (i = 0; i < 16u; i++)
            s[i] = t[i] ^ rk[16u * r + i];
    }
    for (i = 0; i < 16u; i++)
        out[i] = s[i];
}

void wc_aes128_decrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176], s[16], t[16];
    uint32_t r, i, c;
    expand(key, rk);
    for (i = 0; i < 16u; i++)
        s[i] = in[i] ^ rk[160 + i];
    for (r = 9;; r--) {
        for (i = 0; i < 16u; i++)                  /* InvShiftRows + InvSubBytes */
            t[(i + 4u * (i % 4u)) % 16u] = inv_sbox(s[i]);
        for (i = 0; i < 16u; i++)
            s[i] = t[i] ^ rk[16u * r + i];
        if (!r)
            break;
        for (c = 0; c < 4u; c++) {                 /* InvMixColumns */
            uint8_t *p = s + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
            p[0] = (uint8_t)(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
            p[1] = (uint8_t)(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
            p[2] = (uint8_t)(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
            p[3] = (uint8_t)(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
        }
    }
    for (i = 0; i < 16u; i++)
        out[i] = s[i];
}

/* ---- RFC 3394 key unwrap */
int wc_aes_unwrap(const uint8_t kek[16], const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint8_t a[8], b[16];
    uint32_t blocks = n / 8u - 1u, i, j, k;
    if (n % 8u || n < 24u)
        return 0;
    for (i = 0; i < 8u; i++)
        a[i] = in[i];
    for (i = 0; i < blocks * 8u; i++)
        out[i] = in[8 + i];
    for (j = 6; j-- > 0;)
        for (i = blocks; i >= 1u; i--) {
            uint32_t t = blocks * j + i;
            for (k = 0; k < 8u; k++)
                b[k] = a[k];
            b[7] ^= (uint8_t)t, b[6] ^= (uint8_t)(t >> 8), b[5] ^= (uint8_t)(t >> 16), b[4] ^= (uint8_t)(t >> 24);
            for (k = 0; k < 8u; k++)
                b[8 + k] = out[8u * (i - 1u) + k];
            wc_aes128_decrypt(kek, b, b);
            for (k = 0; k < 8u; k++) {
                a[k] = b[k];
                out[8u * (i - 1u) + k] = b[8 + k];
            }
        }
    for (i = 0; i < 8u; i++)
        if (a[i] != 0xA6u)
            return 0;
    return 1;
}
