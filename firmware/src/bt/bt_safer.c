/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Legacy Bluetooth authentication, SAFER+ and Secure Simple Pairing (SSP) cryptography (bt_safer.h).
 * Implemented from Bluetooth Core Specification Vol 2 Part H §6 and §7; host-tested in tests/bt_crypto_test.c. */
#include "bt_safer.h"
#include "bt_sha256.h"

#define uECC_PLATFORM 0
#define uECC_WORD_SIZE 4
#define uECC_SUPPORTS_secp160r1 0
#define uECC_SUPPORTS_secp192r1 1
#define uECC_SUPPORTS_secp224r1 0
#define uECC_SUPPORTS_secp256r1 0
#define uECC_SUPPORTS_secp256k1 0
#define uECC_SUPPORT_COMPRESSED_POINT 0

#include "uECC.h"
#include "uECC.c"

/* SAFER+ substitution boxes: e[x] = 45^x mod 257 (with e[128]=0), l[x] = log_45(x) (with l[0]=128) */
static int s_safer_init = 0;
static uint8_t s_exp[256], s_log[256];

static void bts_init_tables(void)
{
    uint32_t val = 1;
    for (int i = 0; i < 256; i++) {
        s_exp[i] = (uint8_t)(val % 257 == 256 ? 0 : val % 257);
        val = (val * 45) % 257;
    }
    s_exp[128] = 0;
    for (int i = 0; i < 256; i++)
        s_log[s_exp[i]] = (uint8_t)i;
    s_log[0] = 128;
    s_safer_init = 1;
}

static uint8_t bts_rol(uint8_t b, int n)
{
    return (uint8_t)((b << n) | (b >> (8 - n)));
}

static void bts_pht(uint8_t *a, uint8_t *b)
{
    uint8_t a2 = (uint8_t)(2 * (*a) + (*b));
    uint8_t b2 = (uint8_t)((*a) + (*b));
    *a = a2;
    *b = b2;
}

static void bts_ilt(uint8_t b[16])
{
    static const uint8_t armenian[16] = {8, 11, 12, 15, 2, 1, 6, 5, 10, 9, 14, 13, 0, 7, 4, 3};
    uint8_t t[16];
    for (int step = 0; step < 3; step++) {
        for (int i = 0; i < 16; i += 2)
            bts_pht(&b[i], &b[i + 1]);
        for (int i = 0; i < 16; i++)
            t[i] = b[armenian[i]];
        for (int i = 0; i < 16; i++)
            b[i] = t[i];
    }
    for (int i = 0; i < 16; i += 2)
        bts_pht(&b[i], &b[i + 1]);
}

void bts_ar(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], int prime)
{
    static const uint8_t is_e[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    uint8_t k[17][16], karr[17], b[16], in_copy[16];
    uint8_t chk = 0;

    if (!s_safer_init)
        bts_init_tables();

    for (int i = 0; i < 16; i++) {
        karr[i] = key[i];
        chk ^= key[i];
        k[0][i] = key[i];
        b[i] = in[i];
        in_copy[i] = in[i];
    }
    karr[16] = chk;

    for (int p = 2; p <= 17; p++) {
        for (int i = 0; i < 17; i++)
            karr[i] = bts_rol(karr[i], 3);
        for (int j = 0; j < 16; j++) {
            uint8_t bias = s_exp[s_exp[(17 * p + j + 1) % 256]];
            k[p - 1][j] = (uint8_t)(karr[(j + p - 1) % 17] + bias);
        }
    }

    for (int r = 0; r < 8; r++) {
        if (prime && r == 2) {
            for (int i = 0; i < 16; i++) {
                if (is_e[i])
                    b[i] ^= in_copy[i];
                else
                    b[i] = (uint8_t)(b[i] + in_copy[i]);
            }
        }
        uint8_t *k2r = k[2 * r];
        uint8_t *k2r1 = k[2 * r + 1];
        for (int i = 0; i < 16; i++) {
            if (is_e[i])
                b[i] ^= k2r[i];
            else
                b[i] = (uint8_t)(b[i] + k2r[i]);
        }
        for (int i = 0; i < 16; i++)
            b[i] = is_e[i] ? s_exp[b[i]] : s_log[b[i]];
        for (int i = 0; i < 16; i++) {
            if (is_e[i])
                b[i] = (uint8_t)(b[i] + k2r1[i]);
            else
                b[i] ^= k2r1[i];
        }
        bts_ilt(b);
    }
    uint8_t *k17 = k[16];
    for (int i = 0; i < 16; i++) {
        if (is_e[i])
            b[i] ^= k17[i];
        else
            b[i] = (uint8_t)(b[i] + k17[i]);
        out[i] = b[i];
    }
}

static void bts_hash(const uint8_t key[16], const uint8_t i1[16], const uint8_t *i2, uint32_t len, uint8_t out[16])
{
    uint8_t ar[16], inp2[16], k_tilde[16];
    bts_ar(key, i1, ar, 0);
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t idx = i;
        while (idx >= len)
            idx -= len;
        inp2[i] = (uint8_t)((ar[i] ^ i1[i]) + i2[idx]);
    }

    k_tilde[0] = (uint8_t)(key[0] + 233);
    k_tilde[1] = key[1] ^ 229;
    k_tilde[2] = (uint8_t)(key[2] + 223);
    k_tilde[3] = key[3] ^ 193;
    k_tilde[4] = (uint8_t)(key[4] + 179);
    k_tilde[5] = key[5] ^ 167;
    k_tilde[6] = (uint8_t)(key[6] + 149);
    k_tilde[7] = key[7] ^ 131;
    k_tilde[8] = key[8] ^ 233;
    k_tilde[9] = (uint8_t)(key[9] + 229);
    k_tilde[10] = key[10] ^ 223;
    k_tilde[11] = (uint8_t)(key[11] + 193);
    k_tilde[12] = key[12] ^ 179;
    k_tilde[13] = (uint8_t)(key[13] + 167);
    k_tilde[14] = key[14] ^ 149;
    k_tilde[15] = (uint8_t)(key[15] + 131);
    bts_ar(k_tilde, inp2, out, 1);
}

void bts_e1(const uint8_t key[16], const uint8_t rand[16], const uint8_t addr[6], uint8_t sres[4], uint8_t aco[12])
{
    uint8_t out[16];
    bts_hash(key, rand, addr, 6, out);
    for (int i = 0; i < 4; i++)
        sres[i] = out[i];
    for (int i = 0; i < 12; i++)
        aco[i] = out[4 + i];
}

void bts_e21(const uint8_t rand[16], const uint8_t addr[6], uint8_t out[16])
{
    uint8_t x[16], y[16];
    for (int i = 0; i < 16; i++) {
        uint32_t idx = (uint32_t)i;
        while (idx >= 6)
            idx -= 6;
        x[i] = rand[i];
        y[i] = addr[idx];
    }
    x[15] ^= 6;
    bts_ar(x, y, out, 1);
}

void bts_e22(const uint8_t rand[16], const uint8_t *pin, uint32_t plen, uint8_t out[16])
{
    uint8_t x[16], y[16];
    for (int i = 0; i < 16; i++) {
        uint32_t idx = (uint32_t)i;
        while (idx >= plen)
            idx -= plen;
        x[i] = pin[idx];
        y[i] = rand[i];
    }
    y[15] ^= (uint8_t)plen;
    bts_ar(x, y, out, 1);
}

void bts_e3(const uint8_t key[16], const uint8_t rand[16], const uint8_t cof[12], uint8_t out[16])
{
    bts_hash(key, rand, cof, 12, out);
}

/* the encryption key size reduction (Core Vol 2 Part H 4.5): Kc'(x) = g2(x) (Kc(x) mod g1(x)), g1 = x^8L + BTS_G1,
 * g2 BTS_G2's 16 - L octets (top first), for L = 7..15 (7 the spec's least since the KNOB erratum). Kc[0] is the
 * polynomial's lowest octet, as the vendor controller has it (btctrler.a authentication.c Entrypt_Key_Length_Change:
 * the key turned round, then read top octet first); the Part G vectors (francozappa/knob), top octet first, turned round in
 * tests/bt_crypto_test.c. The Bose SoundLink Mini offers 7 octets at most (2026-10-23) */
static const uint16_t BTS_G1[9] = {0x95, 0x1b, 0x609, 0x215, 0x13b, 0xdd, 0x49d, 0x14f, 0xe7};
static const uint8_t BTS_G2[45] = {
    0xb3, 0xf7, 0xff, 0xfc, 0xe2, 0x79, 0xf3, 0xa0, 0x73, 0xa1, 0xab, 0x81, 0x5b, 0xc7, 0xec, 0x80, 0x25,
    0x02, 0xc9, 0x80, 0x11, 0xd8, 0xb0, 0x4d, 0x05, 0x8e, 0x24, 0xf9, 0xa4, 0xbb, 0x0c, 0xa7, 0x60, 0x24, 0xd7,
    0x1c, 0x9c, 0x26, 0xb9, 0x26, 0xd9, 0xe3, 0x43, 0x77, 0x89};
static uint32_t bts_bit(const uint8_t *v, uint32_t n, uint32_t i) { return (uint32_t)v[n - 1u - (i >> 3)] >> (i & 7u) & 1u; }
static uint32_t bts_kbit(const uint8_t *k, uint32_t i) { return (uint32_t)k[i >> 3] >> (i & 7u) & 1u; }   /* (a key: octet 0 the lowest) */
static void bts_kflip(uint8_t *k, uint32_t i) { k[i >> 3] ^= (uint8_t)(1u << (i & 7u)); }
void bts_kc_reduce(const uint8_t kc[16], uint32_t l, uint8_t out[16])
{
    uint8_t r[16];
    const uint8_t *g2 = BTS_G2;
    uint32_t d = 8u * l, n2 = 16u - l, i, j;
    for (i = 0; i < 16u; i++)
        r[i] = kc[i], out[i] = l < 7u || l > 15u ? kc[i] : 0;
    if (l < 7u || l > 15u)
        return;                                         /* (16: Kc' = Kc) */
    for (i = 7; i < l; i++)
        g2 += 16u - i;
    for (i = 127; i >= d; i--)                          /* Kc mod g1 */
        if (bts_kbit(r, i)) {
            bts_kflip(r, i);
            for (j = 0; j < 13u; j++)
                if (BTS_G1[l - 7u] >> j & 1u)
                    bts_kflip(r, i - d + j);
        }
    for (i = 0; i < d; i++)                             /* times g2 */
        if (bts_kbit(r, i))
            for (j = 0; j < 8u * n2 && i + j < 128u; j++)
                if (bts_bit(g2, n2, j))
                    bts_kflip(out, i + j);
}

/* SSP (Core Vol 2 Part H 7.7, P-192), every value in the spec's order (most significant octet first, as Part G 7's sample
 * data): the LMP layer turns the air order (least significant first) round. Checked against Part G 7.7.1-7.7.4 through
 * these very functions (tests/bt_crypto_test.c) */
static void bts_hmac16(const uint8_t *key, uint32_t klen, const uint8_t *const *p, const uint32_t *n, uint32_t np, uint8_t out[16])
{
    uint8_t h[32];
    uint32_t i;
    bt_hmac_sha256(key, klen, p, n, np, h);
    for (i = 0; i < 16u; i++)
        out[i] = h[i];                                  /* (the 128 most significant bits: / 2^128) */
}
/* f1(U, V, X, Z) = HMAC-SHA-256_X(U || V || Z) / 2^128: U, V the two public keys' x, X a nonce, Z 0 for Just Works */
void bts_ssp_f1(const uint8_t u[24], const uint8_t v[24], const uint8_t x[16], uint8_t z, uint8_t out[16])
{
    const uint8_t *p[3] = {u, v, &z};
    const uint32_t n[3] = {24, 24, 1};
    bts_hmac16(x, 16, p, n, 3, out);
}
/* g(U, V, X, Y) = SHA-256(U || V || X || Y) mod 2^32: the numeric comparison's six digits (not used by Just Works) */
uint32_t bts_ssp_g(const uint8_t u[24], const uint8_t v[24], const uint8_t x[16], const uint8_t y[16])
{
    struct bt_sha256 ctx;
    uint8_t h[32];
    bt_sha256_init(&ctx);
    bt_sha256_add(&ctx, u, 24);
    bt_sha256_add(&ctx, v, 24);
    bt_sha256_add(&ctx, x, 16);
    bt_sha256_add(&ctx, y, 16);
    bt_sha256_done(&ctx, h);
    return (uint32_t)h[28] << 24 | (uint32_t)h[29] << 16 | (uint32_t)h[30] << 8 | h[31];
}
/* f2(W, N1, N2, "btlk", A1, A2) = HMAC-SHA-256_W(N1 || N2 || "btlk" || A1 || A2) / 2^128: the link key; W the DHKey, N1 N2
 * the central's and the peripheral's nonces, A1 A2 their addresses */
void bts_ssp_f2(const uint8_t w[24], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[6], const uint8_t a2[6], uint8_t out[16])
{
    static const uint8_t KEYID[4] = {0x62, 0x74, 0x6c, 0x6b};   /* "btlk" */
    const uint8_t *p[5] = {n1, n2, KEYID, a1, a2};
    const uint32_t n[5] = {16, 16, 4, 6, 6};
    bts_hmac16(w, 24, p, n, 5, out);
}
/* f3(W, N1, N2, R, IOcap, A1, A2) = HMAC-SHA-256_W(N1 || N2 || R || IOcap || A1 || A2) / 2^128: the DHKey check (R 0 for
 * Just Works, IOcap the three octets of the IO capability exchange) */
void bts_ssp_f3(const uint8_t w[24], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16], const uint8_t iocap[3],
                const uint8_t a1[6], const uint8_t a2[6], uint8_t out[16])
{
    const uint8_t *p[6] = {n1, n2, r, iocap, a1, a2};
    const uint32_t n[6] = {16, 16, 16, 3, 6, 6};
    bts_hmac16(w, 24, p, n, 6, out);
}

void bts_ecc_make_key(uint8_t pub[64], uint8_t priv[32])
{
    uECC_Curve curve = uECC_secp192r1();
    uint8_t pub_be[48], priv_be[24];
    uECC_make_key(pub_be, priv_be, curve);
    for (int i = 0; i < 24; i++) {
        pub[i] = pub_be[23 - i];
        pub[32 + i] = pub_be[47 - i];
        priv[i] = priv_be[23 - i];
    }
    for (int i = 24; i < 32; i++) {
        pub[i] = 0;
        pub[32 + i] = 0;
        priv[i] = 0;
    }
}

void bts_ecc_shared_secret(const uint8_t pub[64], const uint8_t priv[32], uint8_t secret[32])
{
    uECC_Curve curve = uECC_secp192r1();
    uint8_t pub_be[48], priv_be[24], secret_be[24];
    for (int i = 0; i < 24; i++) {
        pub_be[i] = pub[23 - i];
        pub_be[24 + i] = pub[32 + 23 - i];
        priv_be[i] = priv[23 - i];
    }
    uECC_shared_secret(pub_be, priv_be, secret_be, curve);
    for (int i = 0; i < 24; i++)
        secret[i] = secret_be[23 - i];
    for (int i = 24; i < 32; i++)
        secret[i] = 0;
}
