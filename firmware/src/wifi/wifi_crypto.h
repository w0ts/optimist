/* SPDX-License-Identifier: GPL-3.0-only */
/* The crypto a WPA2-PSK station needs (IEEE 802.11-2020 §12.7): SHA-1 (FIPS 180-4), HMAC-SHA1 (RFC 2104), PBKDF2 (RFC
 * 8018) for the PMK from the passphrase, the PRF for the PTK (§12.7.1.2), AES-128 decryption (FIPS-197) for the AES key
 * unwrap (RFC 3394) of the group key. CCMP itself is the MAC's (docs/WIFI-HW-FACTS.md). Written for this tree, host-tested
 * against the published vectors (tests/wifi_sta_test.c). */
#ifndef WIFI_CRYPTO_H
#define WIFI_CRYPTO_H
#include <stdint.h>

struct wc_sha1 { uint32_t h[5]; uint64_t n; uint8_t b[64]; uint32_t bn; };
void wc_sha1_init(struct wc_sha1 *s);
void wc_sha1_add(struct wc_sha1 *s, const uint8_t *p, uint32_t n);
void wc_sha1_done(struct wc_sha1 *s, uint8_t out[20]);
void wc_hmac_sha1(const uint8_t *key, uint32_t kn, const uint8_t *p, uint32_t n, uint8_t out[20]);
/* the PMK: PBKDF2-HMAC-SHA1(passphrase, ssid, 4096, 32) */
void wc_pmk(const char *pass, const uint8_t *ssid, uint32_t ssid_n, uint8_t pmk[32]);
/* PRF-X(K, A, B): label A (its text), data B, out_n bytes (§12.7.1.2) */
void wc_prf(const uint8_t *k, uint32_t kn, const char *label, const uint8_t *b, uint32_t bn, uint8_t *out, uint32_t out_n);
void wc_aes128_decrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void wc_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
/* RFC 3394 unwrap: in n bytes (a multiple of 8, >= 24), out n - 8 bytes; 1 if the integrity check holds */
int wc_aes_unwrap(const uint8_t kek[16], const uint8_t *in, uint32_t n, uint8_t *out);
#endif
