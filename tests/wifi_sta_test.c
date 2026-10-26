/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi station on the host (firmware/src/wifi/): the WPA2 crypto against the published vectors.
 *   SHA-1 (FIPS 180-4 "abc"), HMAC-SHA1 (RFC 2202 case 1), the PMK (IEEE 802.11 PBKDF2 example "password" / "IEEE"),
 *   AES-128 both ways (FIPS-197 C.1), AES key unwrap (RFC 3394 §4.1)
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_crypto.c"

static int fails, checks;
static void hex(const char *s, uint8_t *out)
{
    while (s[0] && s[1]) {
        unsigned v;
        sscanf(s, "%2x", &v);
        *out++ = (uint8_t)v;
        s += 2;
    }
}
static void check(const char *what, const uint8_t *got, const char *want_hex, unsigned n)
{
    uint8_t w[64];
    hex(want_hex, w);
    checks++;
    if (memcmp(got, w, n)) {
        unsigned i;
        fails++;
        printf("FAIL %s: ", what);
        for (i = 0; i < n; i++)
            printf("%02x", got[i]);
        printf("\n");
    }
}

static void t_crypto(void)
{
    struct wc_sha1 s;
    uint8_t d[64], k[32], in[32];
    wc_sha1_init(&s);
    wc_sha1_add(&s, (const uint8_t *)"abc", 3);
    wc_sha1_done(&s, d);
    check("sha1 abc", d, "a9993e364706816aba3e25717850c26c9cd0d89d", 20);
    memset(k, 0x0b, 20);
    wc_hmac_sha1(k, 20, (const uint8_t *)"Hi There", 8, d);
    check("hmac rfc2202-1", d, "b617318655057264e28bc0b6fb378c8ef146be00", 20);
    wc_pmk("password", (const uint8_t *)"IEEE", 4, d);
    check("pmk password/IEEE", d, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", 32);
    hex("000102030405060708090a0b0c0d0e0f", k);
    hex("00112233445566778899aabbccddeeff", in);
    wc_aes128_encrypt(k, in, d);
    check("aes enc fips197", d, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);
    wc_aes128_decrypt(k, d, d);
    check("aes dec fips197", d, "00112233445566778899aabbccddeeff", 16);
    hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", in);
    checks++;
    if (!wc_aes_unwrap(k, in, 24, d)) {
        fails++;
        printf("FAIL unwrap integrity\n");
    }
    check("unwrap rfc3394", d, "00112233445566778899aabbccddeeff", 16);
}

int main(void)
{
    t_crypto();
    printf("wifi_sta_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
