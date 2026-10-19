/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of Bluetooth Classic authentication and Secure Simple Pairing (SSP) cryptography (firmware/src/bt/bt_safer.c).
 * Tested against Core spec Vol 2 Part G §10 and §7 reference test vectors. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../firmware/src/bt/bt_safer.h"
#include "../firmware/src/bt/bt_sha256.h"
#include "../firmware/src/bt/bt_sha256.c"
#include "../firmware/src/bt/bt_safer.c"
#include "bt_crypto_vectors.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    int set_fail;

    /* 1. SAFER+ E1 test vectors */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(e1_sets) / sizeof(e1_sets[0])); i++) {
        const struct e1_set *s = &e1_sets[i];
        uint8_t sres[4], aco[12];
        bts_e1(s->key, s->rand, s->addr, sres, aco);
        if (memcmp(sres, s->sres, 4) || memcmp(aco, s->aco, 12))
            set_fail++;
    }
    check("E1 authentication function (Part G §10.1): 4 test sets (SRES, ACO)", !set_fail);

    /* 2. SAFER+ E21 test vectors */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(e21_sets) / sizeof(e21_sets[0])); i++) {
        const struct e21_set *s = &e21_sets[i];
        uint8_t ka[16];
        bts_e21(s->rand, s->addr, ka);
        if (memcmp(ka, s->ka, 16))
            set_fail++;
    }
    check("E21 combination key function (Part G §10.2): 4 test sets", !set_fail);

    /* 3. SAFER+ E22 test vectors */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(e22_sets) / sizeof(e22_sets[0])); i++) {
        const struct e22_set *s = &e22_sets[i];
        uint8_t ka[16];
        bts_e22(s->rand, s->pin, 16, ka);
        if (memcmp(ka, s->ka, 16))
            set_fail++;
    }
    check("E22 PIN key function (Part G §10.3): 3 test sets", !set_fail);

    /* 4. SAFER+ E3 test vectors */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(e3_sets) / sizeof(e3_sets[0])); i++) {
        const struct e3_set *s = &e3_sets[i];
        uint8_t kenc[16];
        bts_e3(s->key, s->rand, s->cof, kenc);
        if (memcmp(kenc, s->kenc, 16))
            set_fail++;
    }
    check("E3 encryption key function (Part G §10.5): 4 test sets", !set_fail);

    /* 5. P-192 ECDH key agreement */
    set_fail = 0;
    uECC_Curve curve = uECC_secp192r1();
    for (int i = 0; i < (int)(sizeof(p192_sets) / sizeof(p192_sets[0])); i++) {
        const struct p192_set *s = &p192_sets[i];
        uint8_t pub[48], secret[24];
        if (!uECC_compute_public_key(s->priv_a, pub, curve))
            set_fail++;
        if (memcmp(pub, s->pub_ax, 24) || memcmp(pub + 24, s->pub_ay, 24))
            set_fail++;
        if (!uECC_shared_secret(pub, s->priv_b, secret, curve))
            set_fail++;
        if (memcmp(secret, s->dhkey, 24))
            set_fail++;
    }
    check("P-192 ECDH public key & DHKey computation (Part G §7): 2 test sets", !set_fail);

    /* 6. SSP f1 commitment function */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(f1_sets) / sizeof(f1_sets[0])); i++) {
        const struct f1_set *s = &f1_sets[i];
        uint8_t out[16];
        bts_ssp_f1(s->u, s->v, s->x, s->z, out);       /* (the API the pairing calls, not the HMAC by hand) */
        if (memcmp(out, s->out, 16))
            set_fail++;
    }
    check("SSP f1 commitment function (Part G §7.7.1): 2 test sets", !set_fail);

    /* 7. SSP g numeric comparison function */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(g_sets) / sizeof(g_sets[0])); i++) {
        const struct g_set *s = &g_sets[i];
        if (bts_ssp_g(s->u, s->v, s->x, s->y) != s->out)
            set_fail++;
    }
    check("SSP g numeric comparison function (Part G §7.7.2): 2 test sets", !set_fail);

    /* 8. SSP f2 key derivation function */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(f2_sets) / sizeof(f2_sets[0])); i++) {
        const struct f2_set *s = &f2_sets[i];
        uint8_t out[16];
        bts_ssp_f2(s->w, s->n1, s->n2, s->a1, s->a2, out);   /* ("btlk": the function's own, s->keyid is it) */
        if (memcmp(out, s->out, 16) || memcmp(s->keyid, "btlk", 4))
            set_fail++;
    }
    check("SSP f2 key derivation function (Part G §7.7.3): 2 test sets", !set_fail);

    /* 9. SSP f3 check function */
    set_fail = 0;
    for (int i = 0; i < (int)(sizeof(f3_sets) / sizeof(f3_sets[0])); i++) {
        const struct f3_set *s = &f3_sets[i];
        uint8_t out[16];
        bts_ssp_f3(s->w, s->n1, s->n2, s->r, s->iocap, s->a1, s->a2, out);
        if (memcmp(out, s->out, 16))
            set_fail++;
    }
    check("SSP f3 check function (Part G §7.7.4): 2 test sets", !set_fail);

    printf(fails ? "bt_crypto_test: %d FAILED\n" : "bt_crypto_test: all ok\n", fails);
    return fails != 0;
}
