/* SPDX-License-Identifier: GPL-3.0-only
 * Editor protocol: the CZ collection (nbank.c; FELUCCA_NATIVE_BANKS with CZ; included by editor.c;
 * web/EDITOR_PROTOCOL.md "CZ collection"). After Melodee 0.12's command 78 (keremimo/melodee editor_native.c,
 * Kerem Kilic, GPL-3.0-only): a tone travels as Casio's 144 bytes, two nibbles each, low first.
 *   66 CZ_BANK  0, slot                 -> 0, slot, rc, then (rc 0) 288 nibbles      rc 1: empty, or no such slot
 *               1, slot [, 288 nibbles] -> 1, slot, rc: written; no nibbles: erased  rc 1 arguments, 2 flash, 3 playing
 * The slots: 0..CZ_NUSER-1, the tracks play slot k as TONE U(k+1) (DESC of TONE: its last CZ_NUSER values). */
enum { ED_CZ_BANK = 66 };

static int ed_cz(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint8_t raw[144];
    const uint8_t *t;
    uint32_t op = na ? a[0] : 127u, k = na > 1u ? a[1] : 127u, i, rc = 0;
    if (cmd != ED_CZ_BANK)
        return 0;
    ed_b(op);
    ed_b(k);
    t = k < CZ_NUSER ? czb_tone(CZT_N + k) : 0;
    if (op > 1u || k >= CZ_NUSER || na != 2u + (op ? na > 2u : 0u) * 288u)
        rc = 1;
    else if (!op)
        rc = !t;
    else if (ed_flash_busy())
        rc = 3;
    else {
        for (i = 0; i < 144u && na > 2u; i++) {
            rc |= (uint32_t)(a[2u + 2u * i] | a[3u + 2u * i]) >> 4;   /* (a nibble past 15: 1) */
            raw[i] = (uint8_t)(a[2u + 2u * i] | a[3u + 2u * i] << 4);
        }
        rc = rc ? 1u : (uint32_t)czb_put(k, na > 2u ? raw : 0);
    }
    ed_b(rc);
    for (i = 0; !op && !rc && i < 144u; i++) {
        ed_b(t[i] & 15u);
        ed_b(t[i] >> 4);
    }
    return 1;
}
