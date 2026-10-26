/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Legacy Bluetooth authentication and key generation (Core spec Vol 2 Part H §6): SAFER+ as the spec's Ar and A'r,
 * E1 (SRES, ACO), E21 / E22 (link keys from an address / a PIN) and E3 (the E0 cipher key). Written from the
 * specification; portable C, no C library; host-tested against Part G §10 (tests/bt_crypto_test.c).
 *
 * Byte order is the spec's Part H / Part G §10 order, which is also the air order of LMP: octet 0 is the least
 * significant (RAND, keys, BD_ADDR as LAP low first, SRES, ACO, COF). */
#ifndef BT_SAFER_H
#define BT_SAFER_H
#include <stdint.h>

/* Ar (prime = 0) or A'r (prime = 1): out = SAFER+(key, in) */
void bts_ar(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], int prime);
/* E1: sres (4 octets) and aco (12) from the link key, the challenge and the claimant's BD_ADDR */
void bts_e1(const uint8_t key[16], const uint8_t rand[16], const uint8_t addr[6], uint8_t sres[4], uint8_t aco[12]);
/* E21: a unit / combination key part from RAND and BD_ADDR */
void bts_e21(const uint8_t rand[16], const uint8_t addr[6], uint8_t out[16]);
/* E22: a key from PIN' (plen octets, 1..16, already augmented) and RAND */
void bts_e22(const uint8_t rand[16], const uint8_t *pin, uint32_t plen, uint8_t out[16]);
/* E3: Kc from the link key, EN_RAND and COF (the ACO of the last authentication for a combination key) */
void bts_e3(const uint8_t key[16], const uint8_t rand[16], const uint8_t cof[12], uint8_t out[16]);
/* Kc' from Kc for a key of l octets (7..16; 16 and out of range: Kc itself), E3's order */
void bts_kc_reduce(const uint8_t kc[16], uint32_t l, uint8_t out[16]);

/* SSP (Core Vol 2 Part H 7.7, P-192): every value in the spec's order, most significant octet first (Part G 7's sample
 * data); LMP's air order is the reverse. u, v: the two public keys' x (24); nonces 16; addresses 6; w the DHKey (24) */
void bts_ssp_f1(const uint8_t u[24], const uint8_t v[24], const uint8_t x[16], uint8_t z, uint8_t out[16]);
uint32_t bts_ssp_g(const uint8_t u[24], const uint8_t v[24], const uint8_t x[16], const uint8_t y[16]);
void bts_ssp_f2(const uint8_t w[24], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[6], const uint8_t a2[6], uint8_t out[16]);
void bts_ssp_f3(const uint8_t w[24], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16], const uint8_t iocap[3],
                const uint8_t a1[6], const uint8_t a2[6], uint8_t out[16]);

/* ECDH P-192 key pair generation and shared secret (DHKey). Generated once at boot and stored in RAM.
 * Both functions are from micro-ecc (BSD-2-Clause); the public key is little-endian (as used in LMP). */
void bts_ecc_make_key(uint8_t pub[64], uint8_t priv[32]);
void bts_ecc_shared_secret(const uint8_t pub[64], const uint8_t priv[32], uint8_t secret[32]);

#endif
