/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's FM6 patches (isod89/sloop-fm1 v2.4, 8d3823f, GPL-3.0-only) <-> our project's FM6 voice, for the importer
 * (sl24_import.c) and the exporter (sl24_export.c). Included by project.c before them.
 *
 * A 2.4 project keeps an FM6 part's PTCH (P_E7: F1..F8 its factory patches, B1..B27 its patch bank, object 8 at
 * 0xE5000 / 0xE6000) and seven macros over that patch, not the voice; ours keeps the voice in the project
 * (project_t.fm6, a DX7 packed record: the same 128-byte layout as 2.4's bank records). So:
 *   import  the patch PTCH names (sl24_fm6.h F1..F8; a bank slot: its record, sl24_guard.c sl24_bank_rec or the
 *           editor's file; an empty slot: 2.4's INIT, as 2.4 plays it) with 2.4's macros put into it as its own
 *           eng_fm6.c fm6_sync does (ALG, FB, and on the modulators MRAT, MEG, VMOD: sl24_fm6_bake); MLVL becomes
 *           our MOD (the modulators' levels: 2.4 0.5625 dB a step, ours 0.375); DTUN (the carriers spread
 *           apart while playing) has no place in a voice: LOST;
 *   export  a voice that is one of F1..F8 exactly (with all six operators on) -> that PTCH; MOD -> MLVL; else
 *           the bank (sl24_export.c, with the editor's choice) or the closest factory patch. */
#include "sl24_fm6.h"                                   /* SL24_FM6_INIT, SL24_FM6_F[8] (tools/gen_sl24_fm6.py) */
#define SL24_NFAC 8u                                    /* PTCH F1..F8, then B1..B27 */
#define SL24_BANK_N 27u
#define SL24_BANK_MAGIC 0x42364D46u                     /* "FM6B": 2.4's fm6_bank.c fm6_bank_t */
#define SL24_BANK_LEN (16u + SL24_BANK_N * 128u)        /* magic, ver 1 + nslot 27 (u16 each), used bits, rsv; v[27][128] */
#define SL24_BANK_OBJ 8u                                /* its storage object (2.4's OBJ_FM6BANK; ours at 0xE5000: 9) */
_Static_assert(SL24_BANK_LEN == 3472u && sizeof SL24_FM6_F == SL24_NFAC * 128u, "2.4's FM6 bank and patches");

/* 2.4's macros E0..E6 (ALG FB MLVL MRAT MEG VMOD DTUN) into the packed voice pk, as 2.4 plays them (fm6_sync):
 * ALG 1..32 the algorithm (0: the patch's), FB added to the feedback, and on every operator that is not a carrier
 * of that algorithm: MRAT added to the coarse ratio (ratio mode), MEG the envelope rates (+ slower, 40 steps at
 * most), VMOD added to the velocity sensitivity */
static void sl24_fm6_bake(uint8_t *pk, const int16_t *e)
{
    uint32_t alg = e[0] >= 1 && e[0] <= 32 ? (uint32_t)e[0] - 1u : pk[110] & 31u, j, i;
    int32_t meg = clamp(e[4], -64, 63) * 40 / 64;
    pk[110] = (uint8_t)alg;
    pk[111] = (uint8_t)((pk[111] & 8u) | (uint32_t)clamp((pk[111] & 7) + e[1], 0, 7));
    for (j = 0; j < 6u; j++) {                          /* (buffer order: OP6 first, as FM6_ALG) */
        uint8_t *s = pk + 17u * j;
        if (!(FM6_ALG[alg][j] & 3u))
            continue;                                   /* a carrier */
        if (!(s[15] & 1u))
            s[15] = (uint8_t)((s[15] & 1u) | (uint32_t)clamp(((s[15] >> 1) & 31) + e[3], 0, 31) << 1);
        for (i = 0; i < 4u; i++)
            s[i] = (uint8_t)clamp(s[i] - meg, 0, 99);
        s[13] = (uint8_t)((s[13] & 3u) | (uint32_t)clamp(((s[13] >> 2) & 7) + e[5], 0, 7) << 2);
    }
}
/* MLVL (2.4, -64..63 = -36..+36 dB) <-> our MOD (-64..63, 0.375 dB a step): x 3/2, rounded half away from 0 */
static int16_t sl24_mod_in(int32_t m) { return (int16_t)clamp((3 * m + (m < 0 ? -1 : 1)) / 2, -64, 63); }
#if FELUCCA_SL24_EXPORT
static int16_t sl24_mod_out(int32_t m) { return (int16_t)clamp((2 * m + (m < 0 ? -1 : 1)) / 3, -64, 63); }
/* the PTCH F1..F8 the packed voice pk is exactly, -1 none */
static int sl24_fm6_factory(const uint8_t *pk)
{
    uint32_t n;
    for (n = 0; n < SL24_NFAC; n++)
        if (!memcmp(pk, SL24_FM6_F[n], 128u))
            return (int)n;
    return -1;
}
#endif
