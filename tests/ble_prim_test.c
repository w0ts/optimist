/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the BLE stack's primitives (firmware/src/ble/ble_prim.c, ble_aes.c) against published vectors:
 *   AES-128     FIPS-197 Appendix C.1; the LL session-key sample of the Core Specification (Vol 6 Part C 1:
 *               SK = e(LTK, SKDs || SKDm)); the S-box against one computed from GF(2^8)
 *   AES-CCM     the Core Specification's encrypted-packet samples (Vol 6 Part C 1): the central's
 *               LL_START_ENC_RSP (counter 0) decrypted, the peripheral's data packet (counter 1) encrypted;
 *               a changed bit fails the MIC; long payloads round trip
 *   CRC24       two packets as scapy (secdev/scapy test/scapy/layers/bluetooth4LE.uts) frames them, its CRC
 *               octets; data followed by its CRC leaves the register at 0
 *   whitening   channel 37's first 42 octets as an nRF24 BLE-advertising port publishes them (silver13/
 *               Eachine-E011 rx_bayang_protocol_ble.c, ble_whiten_37; octets least significant bit first);
 *               whitening twice is the identity
 *   CSA #1      the hop over all 37 channels visits each once per 37 events; remapping onto a sparse map lands
 *               on used channels only, as the specification's formula gives
 *   AA rules    the advertising AA and its one-bit neighbours, equal octets, long runs, too many transitions,
 *               the top six bits; a few valid ones */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ble/ble_prim.c"
#include "../firmware/src/ble/ble_aes.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void hex(const char *s, uint8_t *out, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    while (b) {
        if (b & 1)
            p ^= a;
        a = (uint8_t)(a << 1 ^ (a & 0x80 ? 0x1B : 0));
        b >>= 1;
    }
    return p;
}

static void test_aes(void)
{
    uint8_t k[16], pt[16], ct[16], out[16];
    int i, ok = 1;
    hex("000102030405060708090a0b0c0d0e0f", k, 16);
    hex("00112233445566778899aabbccddeeff", pt, 16);
    hex("69c4e0d86a7b0430d8cdb78070b4c55a", ct, 16);
    ble_aes128(k, pt, out);
    check("AES-128: FIPS-197 C.1", !memcmp(out, ct, 16));
    hex("4C68384139F574D836BCF34E9DFB01BF", k, 16);          /* LTK */
    hex("0213243546576879ACBDCEDFE0F10213", pt, 16);         /* SKD = SKDs || SKDm */
    hex("99AD1B5226A37E3E058E3B8E27C2C666", ct, 16);         /* SK */
    ble_aes128(k, pt, out);
    check("AES-128: Core Vol 6 Part C 1 session key", !memcmp(out, ct, 16));
    for (i = 0; i < 256; i++) {                               /* S-box = affine(inverse) */
        uint8_t inv = 0, s, x;
        int j;
        for (j = 1; j < 256 && i; j++)
            if (gmul((uint8_t)i, (uint8_t)j) == 1)
                inv = (uint8_t)j;
        s = inv;
        x = inv;
        for (j = 0; j < 4; j++) {
            x = (uint8_t)(x << 1 | x >> 7);
            s ^= x;
        }
        s ^= 0x63;
        ok &= ble_sbox[i] == s;
    }
    check("AES-128: S-box = affine map of the GF(2^8) inverse", ok);
}

static void sample_ccm(struct ble_ccm *c, uint8_t dir, uint32_t ctr)
{
    hex("99AD1B5226A37E3E058E3B8E27C2C666", c->key, 16);
    hex("24ABDCBABEBAAFDE", c->iv, 8);          /* IVm = 0xBADCAB24, IVs = 0xDEAFBABE, least significant first */
    c->ctr = ctr;
    c->ctr_hi = 0;
    c->dir = dir;
}

static void test_ccm(void)
{
    struct ble_ccm c;
    uint8_t p[260], ref[40];
    unsigned i, n, ok = 1;
    sample_ccm(&c, 1, 0);                        /* central -> peripheral, packet 0: LL_START_ENC_RSP */
    hex("9FCDA7F448", p, 5);
    check("CCM: sample START_ENC_RSP (0F 05 9F CD A7 F4 48) decrypts, MIC ok",
          ble_ccm_decrypt(&c, 0x0F, p, 5) && p[0] == 0x06 && c.ctr == 1);
    sample_ccm(&c, 0, 1);                        /* peripheral -> central, packet 1 */
    hex("17003736353433323130414243444546474849" "4A4B4C4D4E4F5051", p, 27);
    hex("F38881E7BD94C9C369B9A66846DD4786AA8C39CE540D0DAE3ADCDF" "89B96088", ref, 31);
    ble_ccm_encrypt(&c, 0x06, p, 27);
    check("CCM: sample data packet (06 1B ... -> 06 1F F3 88 ... 60 88)", !memcmp(p, ref, 31));
    sample_ccm(&c, 0, 1);
    p[3] ^= 0x10;
    check("CCM: one bit changed -> MIC fails, text left as it came",
          !ble_ccm_decrypt(&c, 0x06, p, 31) && p[3] == (ref[3] ^ 0x10) && c.ctr == 1);
    p[3] ^= 0x10;
    check("CCM: the header's NESN / SN / MD bits are not authenticated", ble_ccm_decrypt(&c, 0x06 ^ 0x1C, p, 31));
    for (n = 1; n <= 247; n += 13) {             /* long payloads, odd counters */
        struct ble_ccm e, d;
        uint8_t q[260];
        sample_ccm(&e, 0, 0xFFFFFFFFu);
        e.ctr_hi = 0x12;
        d = e;
        for (i = 0; i < n; i++)
            q[i] = p[i] = (uint8_t)(i * 7 + n);
        ble_ccm_encrypt(&e, 0x02, p, (uint8_t)n);
        ok &= ble_ccm_decrypt(&d, 0x02, p, (uint8_t)(n + 4)) && !memcmp(p, q, n) && d.ctr == 0 && d.ctr_hi == 0x13;
    }
    check("CCM: 1..247-octet payloads round trip, the counter carries into bit 32", ok);
}

static void test_crc(void)
{
    static const uint8_t adv[] = {0x00, 0x06, 0, 0, 0, 0, 0, 0};
    uint8_t big[2 + 234], air[3], pkt[2 + 234 + 3];
    uint32_t crc;
    crc = ble_crc24(BLE_ADV_CRC_INIT, adv, sizeof adv);
    ble_crc24_air(crc, air);
    check("CRC24: ADV_IND with AdvA 0 -> 5A 39 60 (scapy)", air[0] == 0x5A && air[1] == 0x39 && air[2] == 0x60);
    big[0] = 0x05;                               /* CONNECT_IND header, length 0xEA: 34 zero octets, 200 'X' */
    big[1] = 0xEA;
    memset(big + 2, 0, 34);
    memset(big + 36, 'X', 200);
    ble_crc24_air(ble_crc24(BLE_ADV_CRC_INIT, big, sizeof big), air);
    check("CRC24: 236-octet PDU -> 49 FC CF (scapy)", air[0] == 0x49 && air[1] == 0xFC && air[2] == 0xCF);
    crc = ble_crc24(0x123456, adv, sizeof adv);  /* the CRC as sent, after the data: the register ends at 0 */
    ble_crc24_air(crc, air);
    memcpy(pkt, adv, sizeof adv);
    memcpy(pkt + sizeof adv, air, 3);
    check("CRC24: data followed by its CRC (as sent) leaves the register at 0",
          ble_crc24(0x123456, pkt, sizeof adv + 3) == 0);
}

static void test_whiten(void)
{
    static const uint8_t seq37[] = {0x8D, 0xD2, 0x57, 0xA1, 0x3D, 0xA7, 0x66, 0xB0, 0x75, 0x31, 0x11, 0x48, 0x96, 0x77,
                                    0xF8, 0xE3, 0x46, 0xE9, 0xAB, 0xD0, 0x9E, 0x53, 0x33, 0xD8, 0xBA, 0x98, 0x08, 0x24,
                                    0xCB, 0x3B, 0xFC, 0x71, 0xA3, 0xF4, 0x55, 0x68, 0xCF, 0xA9, 0x19, 0x6C, 0x5D, 0x4C};
    uint8_t z[sizeof seq37], d[64], e[64];
    unsigned i, ok = 1, ch;
    memset(z, 0, sizeof z);
    ble_whiten(37, z, sizeof z);
    for (i = 0; i < sizeof seq37; i++)
        ok &= z[i] == seq37[i];
    check("whitening: channel 37's sequence (the published table)", ok);
    for (ch = 0, ok = 1; ch < 40; ch++) {
        for (i = 0; i < 64; i++)
            d[i] = e[i] = (uint8_t)(i * 31 + ch);
        ble_whiten((uint8_t)ch, d, 64);
        ok &= memcmp(d, e, 64) != 0;
        ble_whiten((uint8_t)ch, d, 64);
        ok &= !memcmp(d, e, 64);
    }
    check("whitening: twice is the identity on channels 0..39", ok);
}

static void test_csa1(void)
{
    uint8_t all[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F}, sparse[5] = {0x05, 0x00, 0x00, 0x00, 0x10};  /* 0, 2, 36 */
    struct ble_chmap m;
    uint8_t last = 0, seen[37], hop, ch;
    unsigned i, ok = 1;
    check("chmap: all 37 valid; one channel invalid", ble_chmap_set(&m, all) && m.n == 37 &&
          !ble_chmap_set(&m, (const uint8_t[5]){0x01, 0, 0, 0, 0}));
    ble_chmap_set(&m, all);
    for (hop = 5; hop <= 16; hop++) {
        memset(seen, 0, sizeof seen);
        last = 0;
        for (i = 0; i < 37; i++) {
            ch = ble_csa1_next(&last, hop, all, &m);
            ok &= ch == (uint8_t)(((i + 1) * hop) % 37);
            seen[ch]++;
        }
        for (i = 0; i < 37; i++)
            ok &= seen[i] == 1;
    }
    check("CSA #1: (last + hop) mod 37, every channel once in 37 events", ok);
    ble_chmap_set(&m, sparse);
    last = 0;
    ok = m.n == 3 && m.used[0] == 0 && m.used[1] == 2 && m.used[2] == 36;
    for (i = 0; i < 100; i++) {
        uint8_t un = (uint8_t)((last + 7) % 37), want = (un == 0 || un == 2 || un == 36) ? un : m.used[un % 3];
        ch = ble_csa1_next(&last, 7, sparse, &m);
        ok &= ch == want && last == un;
    }
    /* by hand: hop 7 from 0 -> unmapped 7, unused, 7 mod 3 = 1 -> channel 2; 14 mod 3 = 2 -> 36; 21 mod 3 = 0 -> 0 */
    last = 0;
    ok &= ble_csa1_next(&last, 7, sparse, &m) == 2 && ble_csa1_next(&last, 7, sparse, &m) == 36 &&
          ble_csa1_next(&last, 7, sparse, &m) == 0;                                /* 21 mod 3 = 0 -> 0 */
    check("CSA #1: remapping onto channels 0, 2, 36", ok);
}

static void test_aa(void)
{
    unsigned i, ok = 1;
    check("AA: the advertising AA is not valid", !ble_aa_valid(0x8E89BED6u));
    for (i = 0; i < 32; i++)
        ok &= !ble_aa_valid(0x8E89BED6u ^ (1u << i));
    check("AA: nor any one bit from it", ok);
    check("AA: four equal octets", !ble_aa_valid(0x5A5A5A5Au));
    check("AA: seven equal bits in a row", !ble_aa_valid(0x4A7F0A55u) && !ble_aa_valid(0x4A800A55u));
    check("AA: more than 24 transitions", !ble_aa_valid(0x55555556u) && !ble_aa_valid(0xAAAAAAA9u));
    check("AA: under two transitions in the top six bits", !ble_aa_valid(0x03A9C66Bu) && !ble_aa_valid(0xFDA9C66Bu));
    check("AA: valid ones", ble_aa_valid(0x50654A6Bu) && ble_aa_valid(0xAF9A9D2Bu) && ble_aa_valid(0x71764129u));
}

int main(void)
{
    test_aes();
    test_ccm();
    test_crc();
    test_whiten();
    test_csa1();
    test_aa();
    printf("%s\n", fails ? "BLE primitives: FAILED" : "BLE primitives: all passed");
    return fails != 0;
}
