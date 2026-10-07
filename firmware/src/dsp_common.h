/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * (a block taken from another project's file brings its author's line here, and its origin is named at the block;
 * the files whose copies a block replaced keep their own licence: they now call the GPL-3.0 block, docs/DSP-SHARED.md) */
/* Integer DSP primitives shared by every engine, effect and drum source, with no table and no firmware header:
 * libc.c, dsp.c and the float units of the X0X ports (x0x/x0x_drums.c, acid/acid_dsp.c) include it (docs/DSP-SHARED.md
 * lists what each block replaced and the proof that every caller's output stayed bit for bit the same).
 * Everything is always inlined, as the copies it replaces were (or compiled to the same code). Organised by kind:
 *   random: xorshift32 */
#ifndef FELUCCA_DSP_COMMON_H
#define FELUCCA_DSP_COMMON_H
#include <stdint.h>

#define DSP_INL static inline __attribute__((always_inline))

/* ---- random ---------------------------------------------------------------------------------------------- */

/* Marsaglia's xorshift32 (13, 17, 5): the next state, which is also the output. A zero state stays zero.
 * The body is dsp.c's noise32 (Felucca). It replaced identical copies: dsp.c noise32, libc.c rng (Felucca), phys_dsp.c px_rand (PHYS, from Felucca 1.0),
 * eng_acid.c tb_rng (TB-3PO, from X0X), eng_formant.c's RAND vowel (inline), cz_native.c's ring noise (CZ, from
 * Melodee: inline), x0x/drum909_dsp.h d9_noise (9W9's wa_noise, from X0X) */
DSP_INL uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
#endif /* FELUCCA_DSP_COMMON_H */
