/* SPDX-License-Identifier: GPL-3.0-only */
/* Secure Simple Pairing, Just Works, the FM-1 as the responder (firmware/src/bt/bt_ssp.c): a simulated initiator (the device
 * that starts pairing, as the Raspberry Pi of docs/BT-CLASSIC.md 14 did) built from the spec functions (bt_safer.c, its
 * f1 f2 f3 E1 E3 checked against Part G's sample data in tests/bt_crypto_test.c) runs the whole exchange through the
 * LMP PDUs: IO capabilities, both P-192 keys in encapsulated PDUs, the commitment and both nonces, the DHKey checks,
 * the authentication (au_rand / sres), the encryption start. Both sides must agree: our commitment, the initiator's
 * and our DHKey checks, the link key, SRES, Kc. A wrong DHKey check from the device is refused. This proves the two
 * halves agree with each other and with the spec's functions; it does not prove a real device's byte order (that is
 * the hardware test). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../firmware/src/bt/bt_sha256.c"
#include "../firmware/src/bt/bt_safer.c"
#include "../firmware/src/bt/bt_lmp.c"
#include "../firmware/src/bt/bt_ssp.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("bt_ssp: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t seed = 0x1234567u;
static int rng(uint8_t *d, unsigned n)                  /* (deterministic: the test repeats) */
{
    unsigned i;
    for (i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        d[i] = (uint8_t)(seed >> 16);
    }
    return 1;
}
/* what the FM-1 sent, in order */
static uint8_t out[64][BTL_MAX];
static uint32_t outl[64], nout, rd;
static void sink(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void)ctx;
    memcpy(out[nout], pdu, len);
    outl[nout++] = len;
}
static const uint8_t *next(uint32_t op, uint32_t *len)   /* the next PDU we sent: it must be op */
{
    uint32_t tid;
    if (rd >= nout || btl_opcode(out[rd], outl[rd], &tid) != op)
        return 0;
    *len = outl[rd];
    return out[rd++] + (op >> 8 == BTL_ESCAPE ? 2 : 1);
}
static void rev(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        d[i] = s[n - 1u - i];
}
static struct btp fm;
static void from_dev(const uint8_t *pdu, uint32_t len) { (void)btp_in(&fm, pdu, len); }
static void dev_pdu(uint32_t op, const uint8_t *par, uint32_t n)   /* the initiator sends op (TID 1: the slave's) */
{
    uint8_t b[BTL_MAX];
    uint32_t k = 0;
    if (op >> 8 == BTL_ESCAPE)
        b[k++] = (uint8_t)(BTL_ESCAPE << 1 | 1u), b[k++] = (uint8_t)op;
    else
        b[k++] = (uint8_t)(op << 1 | 1u);
    memcpy(b + k, par, n);
    from_dev(b, k + n);
}
static int accepted(uint32_t of)
{
    uint32_t len;
    const uint8_t *p = next(BTL_ACCEPTED, &len);
    return p && p[0] == of;
}

static void run(int bad_check)
{
    const uint8_t own[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, peer[6] = {0x93, 0x2a, 0xde, 0xdd, 0x3a, 0xd8};
    uint8_t apub[64], apriv[32], a_pub_spec[48], b_pub_spec[48], b_air[48], dh_le[32], dhkey[24];
    uint8_t na[16], na_air[16], nb[16], cb[16], cb_ok[16], iocap_a[3] = {3, 0, 4}, iocap_b[3], lk[16], lk_air[16];
    uint8_t a1[6], a2[6], r0[16] = {0}, e[16], e_air[16], au[16], sres[4], aco[12], kc[16];
    uint32_t len, i, ok = 1;
    const uint8_t *p;
    nout = rd = 0;
    btp_begin(&fm, own, peer, rng, sink, 0);
    uECC_set_rng(rng);
    bts_ecc_make_key(apub, apriv);                      /* the initiator's key pair */
    for (i = 0; i < 24u; i++)
        a_pub_spec[i] = apub[23u - i], a_pub_spec[24u + i] = apub[32u + 23u - i];
    /* 1. IO capabilities */
    dev_pdu(BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_REQ, iocap_a, 3);
    p = next(BTL_ESCAPE << 8 | BTL_X_IO_CAPABILITY_RES, &len);
    check("the device's IO capability request: ours back, NoInputNoOutput (Just Works)", p && p[0] == 3u);
    if (!p)
        return;
    memcpy(iocap_b, p, 3);
    /* 2. its public key: the header, three payloads, each accepted; then ours, each of ours accepted by it */
    {
        uint8_t h[3] = {1, 1, 48}, air[48];
        rev(air, a_pub_spec, 24), rev(air + 24, a_pub_spec + 24, 24);
        dev_pdu(BTL_ENCAPSULATED_HEADER, h, 3);
        ok &= accepted(BTL_ENCAPSULATED_HEADER);
        for (i = 0; i < 3u; i++) {
            dev_pdu(BTL_ENCAPSULATED_PAYLOAD, air + 16u * i, 16);
            ok &= accepted(BTL_ENCAPSULATED_PAYLOAD);
        }
        p = next(BTL_ENCAPSULATED_HEADER, &len);
        ok &= p && p[0] == 1u && p[1] == 1u && p[2] == 48u;
        for (i = 0; i < 3u; i++) {
            uint8_t a = (uint8_t)(i ? BTL_ENCAPSULATED_PAYLOAD : BTL_ENCAPSULATED_HEADER);
            dev_pdu(BTL_ACCEPTED, &a, 1);
            p = next(BTL_ENCAPSULATED_PAYLOAD, &len);
            ok &= p != 0;
            if (p)
                memcpy(b_air + 16u * i, p, 16);
        }
        rev(b_pub_spec, b_air, 24), rev(b_pub_spec + 24, b_air + 24, 24);
    }
    check("both P-192 public keys exchanged in encapsulated PDUs (a header, 3 x 16 octets, each accepted)", ok);
    /* 3. our commitment, after its accept of our last payload */
    {
        uint8_t a = BTL_ENCAPSULATED_PAYLOAD;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        p = next(BTL_SIMPLE_PAIRING_CONFIRM, &len);
        check("our commitment Cb follows the key exchange", p != 0);
        if (!p)
            return;
        rev(cb, p, 16);
    }
    {   /* the initiator's DHKey, from our key */
        uint8_t pb[64];
        for (i = 0; i < 24u; i++)
            pb[i] = b_pub_spec[23u - i], pb[32u + i] = b_pub_spec[47u - i];
        for (i = 24u; i < 32u; i++)
            pb[i] = pb[32u + i] = 0;
        bts_ecc_shared_secret(pb, apriv, dh_le);
        rev(dhkey, dh_le, 24);
    }
    check("both sides have the same DHKey", !memcmp(dhkey, fm.dhkey, 24));
    /* 4. the nonces: Na from it, accepted, then our Nb; it checks Cb = f1(PKbx, PKax, Nb, 0) */
    rng(na, 16);
    rev(na_air, na, 16);
    dev_pdu(BTL_SIMPLE_PAIRING_NUMBER, na_air, 16);
    ok = accepted(BTL_SIMPLE_PAIRING_NUMBER);
    p = next(BTL_SIMPLE_PAIRING_NUMBER, &len);
    ok &= p != 0;
    if (!p)
        return;
    rev(nb, p, 16);
    bts_ssp_f1(b_pub_spec, a_pub_spec, nb, 0, cb_ok);
    check("Na accepted, our Nb sent; our commitment checks: Cb = f1(PKbx, PKax, Nb, 0)", ok && !memcmp(cb, cb_ok, 16));
    /* 5. the DHKey checks: Ea = f3(DHKey, Na, Nb, 0, IOcapA, A, B) to us, then our Eb = f3(DHKey, Nb, Na, 0, IOcapB, B, A) */
    rev(a1, peer, 6), rev(a2, own, 6);
    bts_ssp_f3(dhkey, na, nb, r0, iocap_a, a1, a2, e);
    if (bad_check)
        e[5] ^= 1u;
    rev(e_air, e, 16);
    dev_pdu(BTL_DHKEY_CHECK, e_air, 16);
    if (bad_check) {
        uint32_t l2;
        const uint8_t *q = next(BTL_NOT_ACCEPTED, &l2);
        check("a wrong DHKey check from the device: refused, authentication failure, the pairing failed",
              q && q[0] == BTL_DHKEY_CHECK && q[1] == BTL_ERR_AUTH_FAILURE && fm.st == BTP_FAILED);
        return;
    }
    ok = accepted(BTL_DHKEY_CHECK);
    p = next(BTL_DHKEY_CHECK, &len);
    ok &= p != 0;
    if (p) {
        uint8_t eb[16];
        rev(eb, p, 16);
        bts_ssp_f3(dhkey, nb, na, r0, iocap_b, a2, a1, e);
        ok &= !memcmp(eb, e, 16);
    }
    check("its DHKey check accepted, ours sent and right: Eb = f3(DHKey, Nb, Na, 0, IOcapB, B, A)", ok);
    bts_ssp_f2(dhkey, na, nb, a1, a2, lk);
    rev(lk_air, lk, 16);
    check("the same link key both sides: LK = f2(DHKey, Na, Nb, \"btlk\", A, B)", fm.st == BTP_PAIRED && !memcmp(lk_air, fm.lk, 16));
    /* 6. authentication: its au_rand, our SRES = E1(LK, AU_RAND, B) */
    rng(au, 16);
    dev_pdu(BTL_AU_RAND, au, 16);
    p = next(BTL_SRES, &len);
    bts_e1(lk_air, au, own, sres, aco);
    check("its au_rand: our SRES = E1(LK, AU_RAND, our address)", p && !memcmp(p, sres, 4) && fm.st == BTP_AUTHED);
    /* 7. encryption: its mode request accepted; our key size (16) and, once taken, EN_RAND; Kc = E3(LK, EN_RAND, ACO) */
    {
        uint8_t on = 1, a;
        dev_pdu(BTL_ENCRYPTION_MODE_REQ, &on, 1);
        ok = accepted(BTL_ENCRYPTION_MODE_REQ);
        p = next(BTL_ENCRYPTION_KEY_SIZE_REQ, &len);
        ok &= p && p[0] == 16u;
        a = BTL_ENCRYPTION_KEY_SIZE_REQ;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        p = next(BTL_START_ENCRYPTION_REQ, &len);
        ok &= p != 0;
        if (p)
            bts_e3(lk_air, p, aco, kc);
        a = BTL_START_ENCRYPTION_REQ;
        dev_pdu(BTL_ACCEPTED, &a, 1);
        check("encryption: its request accepted, our key size 16 then start_encryption_req; Kc = E3(LK, EN_RAND, ACO) both sides",
              ok && !memcmp(kc, fm.kc, 16) && fm.st == BTP_ENCRYPTED);
    }
    check("every PDU we sent was looked at (none left over)", rd == nout);
}

int main(void)
{
    run(0);
    run(1);
    printf("bt_ssp: %d failed\n", fails);
    return fails != 0;
}
