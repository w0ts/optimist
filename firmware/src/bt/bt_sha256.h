/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* SHA-256 (FIPS PUB 180-4) and HMAC-SHA-256 (RFC 2104), for Secure Simple Pairing's f1 / g / f2 / f3 (Core spec Vol 2
 * Part H §7.7). Written from the standards; portable C, no C library; host-tested in tests/bt_crypto_test.c. */
#ifndef BT_SHA256_H
#define BT_SHA256_H
#include <stdint.h>

struct bt_sha256 {
    uint32_t h[8];
    uint8_t buf[64];
    uint32_t nbuf;
    uint32_t len_lo, len_hi;                       /* bytes so far */
};

void bt_sha256_init(struct bt_sha256 *s);
void bt_sha256_add(struct bt_sha256 *s, const uint8_t *p, uint32_t n);
void bt_sha256_done(struct bt_sha256 *s, uint8_t out[32]);

/* HMAC-SHA-256 with a key of klen bytes (<= 64) over the concatenation of up to 6 parts (p[i], n[i]); 0-length parts
 * are skipped */
void bt_hmac_sha256(const uint8_t *key, uint32_t klen, const uint8_t *const *p, const uint32_t *n, uint32_t parts,
                    uint8_t out[32]);

#endif
